/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Davidian Seventy-Three: the audio engine shared by the LV2, VST3 and AU wrappers.
 * The controls are read through an array of float pointers indexed like the LV2 ports
 * (ports.h); each wrapper points them at its own parameter values. Audio goes through
 * engine_process (one or two independent channels) or, for LV2, engine_run on the mono
 * PORT_IN/PORT_OUT buffers. The circuit model is in circuit.h. */
#ifndef ENGINE_H
#define ENGINE_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "circuit.h"
#include "oversample.h"
#include "ports.h"

/* Flush denormals to zero while processing, so decaying filter states can't stall the CPU. */
#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__)
#    include <xmmintrin.h>
typedef unsigned FpState;
static FpState fp_enter(void)
{
    const FpState s = _mm_getcsr();
    _mm_setcsr(s | 0x8040); /* FTZ | DAZ */
    return s;
}
static void fp_leave(FpState s) { _mm_setcsr(s); }
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
typedef unsigned long long FpState;
static FpState fp_enter(void)
{
    FpState s;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(s));
    __asm__ __volatile__("msr fpcr, %0" : : "r"(s | (1ULL << 24))); /* FZ */
    return s;
}
static void fp_leave(FpState s) { __asm__ __volatile__("msr fpcr, %0" : : "r"(s)); }
#else
typedef int FpState;
static FpState fp_enter(void) { return 0; }
static void fp_leave(FpState s) { (void)s; }
#endif

#define CTRL_BLOCK 32
#define MAX_CH     2

typedef struct {
    Channel ch;
    Oversampler ovs;
    double dry[OS_LATENCY + 1];
    int dry_pos;
} ChannelState;

typedef struct {
    float* port[PORT_COUNT];
    double rate;
    int os, latency;
    Coefs coefs;
    ChannelControls cur;   /* smoothed */
    int ctl_valid;
    int hpf_pos, low_pos, mid_pos; /* last selected frequencies (1-based, never off) */
    ChannelState chan[MAX_CH];
    double wet;    /* 1 on, 0 bypassed; ramps */
    double ramp;   /* per-sample step for the switch crossfades */
    double smooth; /* per-block smoothing coefficient for the gains */
} Plugin;

static float port_or(const Plugin* p, int i, float def)
{
    const float v = p->port[i] ? *p->port[i] : def;
    return isfinite(v) ? v : def;
}

static int port_pos(const Plugin* p, int i, int def, int n)
{
    const int v = (int)floorf(port_or(p, i, (float)def) + 0.5f);
    return v < 0 ? 0 : (v >= n ? n - 1 : v);
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* Control targets from the ports. */
static void read_targets(Plugin* p, ChannelControls* t)
{
    const int gain = port_pos(p, PORT_GAIN, 2, N_GAIN);
    const int hpf = port_pos(p, PORT_HPF, 0, 5);
    const int low = port_pos(p, PORT_LOW_FREQ, 0, 5);
    const int mid = port_pos(p, PORT_MID_FREQ, 0, 7);
    const int eq_in = port_or(p, PORT_EQ_IN, 1.0f) > 0.5f;
    const double g = -gain_marking(gain); /* dB from the input of amp A's path to 0 dBu out */
    if (gain_is_mic(gain)) {
        t->mic = 1.0;
        t->amp_a_db = g - AMP_B_DB;
        t->pad_db = p->ctl_valid ? p->cur.pad_db : -20.0;
    } else {
        t->mic = 0.0;
        t->amp_a_db = AMP_A_LINE;
        t->pad_db = g - AMP_A_LINE - AMP_B_DB;
    }
    t->hi = port_or(p, PORT_IMPEDANCE, 0.0f) > 0.5f ? 1.0 : 0.0;
    t->hpf = hpf > 0 ? 1.0 : 0.0;
    t->low_db = eq_in && low > 0 ? clampd(port_or(p, PORT_LOW_GAIN, 0.0f), -16.0, 16.0) : 0.0;
    t->mid_db = eq_in && mid > 0 ? clampd(port_or(p, PORT_MID_GAIN, 0.0f), -18.0, 18.0) : 0.0;
    t->high_db = eq_in ? clampd(port_or(p, PORT_HIGH_GAIN, 0.0f), -16.0, 16.0) : 0.0;
    t->out_db = clampd(port_or(p, PORT_OUTPUT, 0.0f), -24.0, 12.0);
    t->pol = port_or(p, PORT_PHASE, 0.0f) > 0.5f ? -1.0 : 1.0;
    if (hpf > 0)
        p->hpf_pos = hpf;
    if (low > 0)
        p->low_pos = low;
    if (mid > 0)
        p->mid_pos = mid;
}

static int approach(double* cur, double tgt, double a, double snap)
{
    const double d = tgt - *cur;
    if (d == 0.0)
        return 0;
    *cur = fabs(d) < snap ? tgt : *cur + a * d;
    return 1;
}

/* Moves cur toward tgt by at most step. */
static int ramp_to(double* cur, double tgt, double step)
{
    const double d = tgt - *cur;
    if (d == 0.0)
        return 0;
    *cur = fabs(d) <= step ? tgt : *cur + (d > 0.0 ? step : -step);
    return 1;
}

/* Moves the gains toward their targets once per control block; the crossfades (mic/line,
 * impedance, filter in/out, phase) move in fixed steps per block. Returns 1 if any
 * coefficient needs recomputing. */
static int smooth_controls(Plugin* p)
{
    ChannelControls t;
    const int hpf0 = p->hpf_pos, low0 = p->low_pos, mid0 = p->mid_pos;
    const double step = p->ramp * CTRL_BLOCK;
    int changed = 0;
    read_targets(p, &t);
    if (!p->ctl_valid) {
        p->cur = t;
        p->ctl_valid = 1;
        return 1;
    }
    changed |= approach(&p->cur.amp_a_db, t.amp_a_db, p->smooth, 0.01);
    changed |= approach(&p->cur.pad_db, t.pad_db, p->smooth, 0.01);
    changed |= approach(&p->cur.low_db, t.low_db, p->smooth, 0.005);
    changed |= approach(&p->cur.mid_db, t.mid_db, p->smooth, 0.005);
    changed |= approach(&p->cur.high_db, t.high_db, p->smooth, 0.005);
    changed |= approach(&p->cur.out_db, t.out_db, p->smooth, 0.005);
    changed |= ramp_to(&p->cur.mic, t.mic, step);
    changed |= ramp_to(&p->cur.hi, t.hi, step);
    ramp_to(&p->cur.hpf, t.hpf, step);
    ramp_to(&p->cur.pol, t.pol, 2.0 * step);
    return changed || hpf0 != p->hpf_pos || low0 != p->low_pos || mid0 != p->mid_pos;
}

static void update_coefs(Plugin* p)
{
    int c;
    coefs_set(&p->coefs, &p->cur, p->hpf_pos, p->low_pos, p->mid_pos);
    for (c = 0; c < MAX_CH; ++c)
        channel_take(&p->chan[c].ch, &p->coefs);
}

/* The rate is fixed for the life of the engine: make a new one when it changes. */
static Plugin* engine_new(double rate)
{
    Plugin* p = (Plugin*)calloc(1, sizeof(Plugin));
    if (!p)
        return NULL;
    p->rate = rate;
    p->os = rate <= 50000.0 ? 4 : (rate <= 100000.0 ? 2 : 1);
    p->latency = p->os > 1 ? OS_LATENCY : 0;
    p->ramp = 1.0 / (0.01 * rate);
    p->smooth = 1.0 - exp(-CTRL_BLOCK / (0.02 * rate));
    p->hpf_pos = 2;
    p->low_pos = 2;
    p->mid_pos = 3;
    return p;
}

/* Latency in samples at this engine's rate. */
static int engine_latency(const Plugin* p) { return p->latency; }

static void engine_connect(Plugin* p, uint32_t port, float* data)
{
    if (port < PORT_COUNT)
        p->port[port] = data;
}

/* Clears the filter and transformer state; the controls jump to their current values. */
static void engine_activate(Plugin* p)
{
    int c;
    p->ctl_valid = 0;
    smooth_controls(p);
    coefs_init(&p->coefs, p->rate * p->os);
    for (c = 0; c < MAX_CH; ++c) {
        channel_init(&p->chan[c].ch, p->rate * p->os);
        os_init(&p->chan[c].ovs, p->os);
        memset(p->chan[c].dry, 0, sizeof p->chan[c].dry);
        p->chan[c].dry_pos = 0;
    }
    update_coefs(p);
    p->wet = port_or(p, PORT_ENABLED, 1.0f) > 0.5f ? 1.0 : 0.0;
}

/* nch (1 or 2) independent channels with the same settings. in[c] and out[c] may be the
 * same buffer. */
static void engine_process(Plugin* p, const float* const* in, float* const* out, int nch, uint32_t n)
{
    const double wet_t = port_or(p, PORT_ENABLED, 1.0f) > 0.5f ? 1.0 : 0.0;
    double buf[OS_MAX];
    uint32_t i;
    int c, k;
    const FpState fp = fp_enter();
    if (nch > MAX_CH)
        nch = MAX_CH;
    for (i = 0; i < n; ++i) {
        double wet;
        if (i % CTRL_BLOCK == 0 && smooth_controls(p))
            update_coefs(p);
        p->wet += wet_t > p->wet ? p->ramp : (wet_t < p->wet ? -p->ramp : 0.0);
        p->wet = clampd(p->wet, 0.0, 1.0);
        wet = p->wet;
        for (c = 0; c < nch; ++c) {
            ChannelState* s = &p->chan[c];
            double x = in[c] ? in[c][i] : 0.0, y, dry;
            if (!isfinite(x))
                x = 0.0;
            os_up(&s->ovs, x, buf);
            for (k = 0; k < p->os; ++k)
                buf[k] = channel_run(&s->ch, &p->coefs, &p->cur, buf[k]);
            y = os_down(&s->ovs, buf);
            /* Bypass is delayed by the same latency, so switching does not jump in time. */
            s->dry[s->dry_pos] = x;
            dry = s->dry[(s->dry_pos + OS_LATENCY + 1 - p->latency) % (OS_LATENCY + 1)];
            s->dry_pos = (s->dry_pos + 1) % (OS_LATENCY + 1);
            y = dry + wet * (y - dry);
            if (!isfinite(y)) { /* never pass garbage to the host; restart the model */
                y = 0.0;
                channel_init(&s->ch, p->rate * p->os);
                channel_take(&s->ch, &p->coefs);
                os_init(&s->ovs, p->os);
            }
            if (out[c])
                out[c][i] = (float)y;
        }
    }
    fp_leave(fp);
}

/* LV2: one channel on PORT_IN/PORT_OUT, which may be the same buffer. */
static void engine_run(Plugin* p, uint32_t n)
{
    const float* in[1];
    float* out[1];
    if (p->port[PORT_LATENCY])
        *p->port[PORT_LATENCY] = (float)p->latency;
    if (!p->port[PORT_IN] || !p->port[PORT_OUT])
        return;
    in[0] = p->port[PORT_IN];
    out[0] = p->port[PORT_OUT];
    engine_process(p, in, out, 1, n);
}

static void engine_free(Plugin* p) { free(p); }

#endif
