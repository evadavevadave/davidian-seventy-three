/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Tests for the circuit model and the plugin. Usage: test_dsp
 * Compiles plugin.c in directly, so it tests the same code the bundle runs. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <time.h>

#include "../src/plugin.c"
#include "../src/params.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL: ");                                                                    \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static double db(double x) { return 20.0 * log10(x); }

/* ---- A host ---- */

typedef struct {
    LV2_Handle p;
    double rate;
    float ctl[PORT_COUNT];
} Host;

static const float DEFAULTS[PORT_COUNT] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0 };

static void host_open(Host* h, double rate)
{
    int i;
    h->rate = rate;
    h->p = instantiate(&descriptor, rate, "", NULL);
    memcpy(h->ctl, DEFAULTS, sizeof h->ctl);
    for (i = 2; i < PORT_COUNT; ++i)
        connect_port(h->p, (uint32_t)i, &h->ctl[i]);
}

/* Runs a sine of amplitude amp (1 = 0 dBFS) for secs and measures the last half: the gain at
 * f, the THD (harmonics 2..9) and the output peak. Starts from a clean state. */
static void sine_run(Host* h, double f, double amp, double secs, double* gain, double* thd, double* peak)
{
    const int n = (int)(secs * h->rate), blk = 256;
    float* in = (float*)malloc(sizeof(float) * n);
    float* out = (float*)malloc(sizeof(float) * n);
    int i, k, start;
    double hs = 0.0, fund = 0.0, pk = 0.0;
    for (i = 0; i < n; ++i)
        in[i] = (float)(amp * sin(2.0 * CK_PI * f * i / h->rate));
    activate(h->p);
    for (i = 0; i < n; i += blk) {
        const int m = n - i < blk ? n - i : blk;
        connect_port(h->p, PORT_IN, in + i);
        connect_port(h->p, PORT_OUT, out + i);
        run(h->p, (uint32_t)m);
    }
    /* A whole number of cycles from the second half. */
    {
        const int per = (int)floor((n / 2) * f / h->rate);
        const int len = (int)floor(per * h->rate / f);
        start = n - len;
        for (k = 1; k <= 9; ++k) {
            double re = 0.0, im = 0.0, a;
            if (k * f >= 0.5 * h->rate * 0.95)
                break;
            for (i = start; i < n; ++i) {
                const double w = 2.0 * CK_PI * k * f * (i - start) / h->rate;
                re += out[i] * cos(w);
                im += out[i] * sin(w);
            }
            a = 2.0 * sqrt(re * re + im * im) / len;
            if (k == 1)
                fund = a;
            else
                hs += a * a;
        }
        for (i = start; i < n; ++i)
            if (fabs(out[i]) > pk)
                pk = fabs(out[i]);
    }
    *gain = fund / amp;
    *thd = fund > 0 ? sqrt(hs) / fund : 0.0;
    *peak = pk;
    free(in);
    free(out);
}

static double gain_at(Host* h, double f, double amp)
{
    double g, thd, pk;
    sine_run(h, f, amp, f < 100.0 ? 1.0 : 0.25, &g, &thd, &pk);
    return g;
}

/* ---- Independent analog references for the EQ (complex arithmetic by hand) ---- */

typedef struct { double re, im; } Cx;
static Cx cx(double re, double im) { Cx z; z.re = re; z.im = im; return z; }
static Cx cx_mul(Cx a, Cx b) { return cx(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re); }
static Cx cx_div(Cx a, Cx b)
{
    const double d = b.re * b.re + b.im * b.im;
    return cx((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}
static double cx_abs(Cx a) { return sqrt(a.re * a.re + a.im * a.im); }
/* c2 s^2 + c1 s + c0 at s = j w */
static Cx poly2(double c2, double c1, double c0, double w) { return cx(c0 - c2 * w * w, c1 * w); }

static double ref_low(double f, double f0, double dbg)
{
    const double A = pow(10.0, dbg / 40.0), w = f / f0, q = 1.0;
    return cx_abs(cx_div(cx_mul(cx(A, 0), poly2(1, sqrt(A) / q, A, w)), poly2(A, sqrt(A) / q, 1, w)));
}

static double ref_mid(double f, double f0, double dbg)
{
    const double G = pow(10.0, fabs(dbg) / 20.0), w = f / f0, q = 1.0;
    const Cx num = poly2(1, (dbg >= 0 ? G : 1.0) / q, 1, w), den = poly2(1, (dbg >= 0 ? 1.0 : G) / q, 1, w);
    return cx_abs(cx_div(num, den));
}

static double ref_high(double f, double dbg)
{
    const double s = pow(10.0, dbg / 40.0), w = f / 12000.0;
    return cx_abs(cx_div(cx(1.0, s * w), cx(1.0, w / s)));
}

static double ref_hpf(double f, double fc)
{
    const double r = fc / f;
    return 1.0 / sqrt(1.0 + r * r * r * r * r * r); /* 3rd-order Butterworth */
}

/* ---- 1. Gain structure ---- */

static void test_gain(void)
{
    Host h;
    int pos;
    double g;
    printf("Sensitivity switch (1 kHz, low level):\n");
    host_open(&h, 48000.0);
    for (pos = 0; pos < N_GAIN; ++pos) {
        const int mark = gain_marking(pos);
        const double expect = gain_is_mic(pos) ? -mark - MIC_PAD_DB : -mark;
        const double amp = pow(10.0, (-30.0 - expect) / 20.0); /* output about -30 dBFS */
        h.ctl[PORT_GAIN] = (float)pos;
        g = db(gain_at(&h, 1000.0, amp > 1.0 ? 1.0 : amp));
        printf("  %-4s %+4d dBm: %+6.2f dB (expected %+5.1f)\n", gain_is_mic(pos) ? "MIC" : "LINE", mark, g, expect);
        CHECK(fabs(g - expect) < 0.3, "sensitivity position %d gain %.2f dB, expected %.1f", pos, g, expect);
    }
    h.ctl[PORT_GAIN] = 15; /* mic -60 */
    h.ctl[PORT_IMPEDANCE] = 1;
    g = db(gain_at(&h, 1000.0, 0.001)) - 20.0;
    printf("  mic impedance HI: %+.2f dB relative to LO (expected %+.1f)\n", g, -HI_LOSS_DB);
    CHECK(fabs(g + HI_LOSS_DB) < 0.3, "HI impedance level");
    h.ctl[PORT_IMPEDANCE] = 0;
    h.ctl[PORT_GAIN] = 2;
    h.ctl[PORT_OUTPUT] = -6.0f;
    g = db(gain_at(&h, 1000.0, 0.1));
    printf("  output level -6 dB: %+.2f dB\n", g);
    CHECK(fabs(g + 6.0) < 0.2, "output level");
    cleanup(h.p);
}

/* ---- 2. Frequency response and EQ against the analog curves ---- */

static void test_response(void)
{
    static const double F[] = { 20, 30, 50, 100, 300, 1000, 3000, 8000, 12000, 16000, 20000 };
    const int nf = (int)(sizeof F / sizeof F[0]);
    double flat[16];
    Host h;
    int i;
    const double amp = 0.03; /* -30 dBFS: linear */
    host_open(&h, 48000.0);
    printf("Frequency response, line 0, EQ flat (48 kHz):\n ");
    for (i = 0; i < nf; ++i) {
        flat[i] = gain_at(&h, F[i], amp);
        printf(" %g:%+.2f", F[i], db(flat[i]));
        if (F[i] >= 30 && F[i] <= 16000)
            CHECK(fabs(db(flat[i])) < 0.5, "flat response %.2f dB at %g Hz", db(flat[i]), F[i]);
    }
    printf("\n");
    CHECK(db(flat[0]) > -1.0 && db(flat[nf - 1]) > -0.5, "response at the band edges");

    {
        struct { int port, pos; double gain; } bands[] = {
            { PORT_HIGH_GAIN, 0, 16 }, { PORT_HIGH_GAIN, 0, -16 }, { PORT_LOW_GAIN, 2, 16 },
            { PORT_LOW_GAIN, 4, -16 }, { PORT_LOW_GAIN, 1, 10 }, { PORT_MID_GAIN, 3, 18 },
            { PORT_MID_GAIN, 1, -12 }, { PORT_MID_GAIN, 6, 9 }, { PORT_MID_GAIN, 5, -18 },
        };
        int b;
        printf("EQ against its analog prototype (deviation, dB):\n");
        for (b = 0; b < (int)(sizeof bands / sizeof bands[0]); ++b) {
            double worst = 0.0;
            memcpy(h.ctl, DEFAULTS, sizeof h.ctl);
            h.ctl[bands[b].port] = (float)bands[b].gain;
            if (bands[b].port == PORT_LOW_GAIN)
                h.ctl[PORT_LOW_FREQ] = (float)bands[b].pos;
            if (bands[b].port == PORT_MID_GAIN)
                h.ctl[PORT_MID_FREQ] = (float)bands[b].pos;
            for (i = 0; i < nf; ++i) {
                const double a = bands[b].gain > 0 ? amp * pow(10.0, -bands[b].gain / 20.0) : amp;
                const double meas = db(gain_at(&h, F[i], a) / flat[i]);
                double ref;
                if (bands[b].port == PORT_HIGH_GAIN)
                    ref = db(ref_high(F[i], bands[b].gain));
                else if (bands[b].port == PORT_LOW_GAIN)
                    ref = db(ref_low(F[i], LOW_HZ[bands[b].pos], bands[b].gain));
                else
                    ref = db(ref_mid(F[i], MID_HZ[bands[b].pos], bands[b].gain));
                if (fabs(meas - ref) > fabs(worst))
                    worst = meas - ref;
            }
            printf("  %-5s %-7s %+5.1f dB: worst %+.3f\n",
                   bands[b].port == PORT_HIGH_GAIN ? "HIGH" : bands[b].port == PORT_LOW_GAIN ? "LOW" : "MID",
                   bands[b].port == PORT_HIGH_GAIN ? "12 kHz" : bands[b].port == PORT_LOW_GAIN ? LOW_OPTS[bands[b].pos] : MID_OPTS[bands[b].pos],
                   bands[b].gain, worst);
            CHECK(fabs(worst) < 0.3, "EQ band %d deviates %.3f dB from its analog curve", b, worst);
        }
        /* Peak of a full mid boost and the low shelf's resonance. */
        memcpy(h.ctl, DEFAULTS, sizeof h.ctl);
        h.ctl[PORT_MID_FREQ] = 3;
        h.ctl[PORT_MID_GAIN] = 18;
        printf("  mid 1.6 kHz +18: %+.2f dB at 1.6 kHz\n", db(gain_at(&h, 1600.0, 0.003)));
        CHECK(fabs(db(gain_at(&h, 1600.0, 0.003)) - 18.0) < 0.3, "mid boost peak");
        /* EQ out: everything flat again; a band switched off ignores its gain. */
        h.ctl[PORT_EQ_IN] = 0;
        h.ctl[PORT_HIGH_GAIN] = 16;
        h.ctl[PORT_LOW_FREQ] = 2;
        h.ctl[PORT_LOW_GAIN] = 16;
        printf("  EQ out: 1.6 kHz %+.2f, 12 kHz %+.2f, 60 Hz %+.2f dB\n", db(gain_at(&h, 1600.0, amp)),
               db(gain_at(&h, 12000.0, amp)), db(gain_at(&h, 60.0, amp)));
        CHECK(fabs(db(gain_at(&h, 1600.0, amp))) < 0.3 && fabs(db(gain_at(&h, 12000.0, amp) / flat[8])) < 0.3
                  && fabs(db(gain_at(&h, 60.0, amp))) < 0.3, "EQ out is not flat");
        h.ctl[PORT_EQ_IN] = 1;
        h.ctl[PORT_LOW_FREQ] = 0;
        h.ctl[PORT_HIGH_GAIN] = 0;
        h.ctl[PORT_MID_GAIN] = 0;
        CHECK(fabs(db(gain_at(&h, 60.0, amp))) < 0.3, "low band off still boosts");
    }

    printf("High-pass filter against a 3rd-order Butterworth (dB at fc/2, fc, 2fc):\n");
    {
        int pos;
        for (pos = 1; pos <= 4; ++pos) {
            const double fc = HPF_HZ[pos];
            const double r[3] = { 0.5, 1.0, 2.0 };
            int j;
            memcpy(h.ctl, DEFAULTS, sizeof h.ctl);
            h.ctl[PORT_HPF] = (float)pos;
            printf("  %-6s", HPF_OPTS[pos]);
            for (j = 0; j < 3; ++j) {
                const double f = fc * r[j];
                const double fl = gain_at(&h, f, amp);
                double flat_f;
                h.ctl[PORT_HPF] = 0;
                flat_f = gain_at(&h, f, amp);
                h.ctl[PORT_HPF] = (float)pos;
                printf(" %+7.2f (%+7.2f)", db(fl / flat_f), db(ref_hpf(f, fc)));
                CHECK(fabs(db(fl / flat_f) - db(ref_hpf(f, fc))) < 0.3, "HPF %g Hz at %g Hz", fc, f);
            }
            printf("\n");
        }
    }
    cleanup(h.p);

    /* The same response at the other sample rates (2x and 1x oversampling). */
    {
        const double rates[3] = { 44100.0, 96000.0, 192000.0 };
        int r;
        for (r = 0; r < 3; ++r) {
            double g1, g12;
            host_open(&h, rates[r]);
            h.ctl[PORT_MID_FREQ] = 4;
            h.ctl[PORT_MID_GAIN] = 12;
            g1 = db(gain_at(&h, 3200.0, 0.01));
            h.ctl[PORT_MID_GAIN] = 0;
            h.ctl[PORT_HIGH_GAIN] = 16;
            g12 = db(gain_at(&h, 12000.0, 0.005));
            printf("  %6.0f Hz: mid 3.2k +12 -> %+.2f dB, high +16 at 12 kHz -> %+.2f dB (ref %+.2f)\n", rates[r], g1, g12,
                   db(ref_high(12000.0, 16)));
            CHECK(fabs(g1 - 12.0) < 0.3 && fabs(g12 - db(ref_high(12000.0, 16))) < 0.4, "EQ at %.0f Hz", rates[r]);
            cleanup(h.p);
        }
    }
}

/* ---- 3. Distortion, saturation and bandwidth ---- */

static void test_nonlinear(void)
{
    Host h;
    double g, thd, pk, thd_lo, thd_hi, thd_lf, bw40, bw80;
    host_open(&h, 48000.0);
    printf("Distortion:\n");
    sine_run(&h, 1000.0, 0.125, 0.5, &g, &thd, &pk);
    printf("  line 0, 0 dBu out, 1 kHz: THD %.4f%%\n", 100 * thd);
    CHECK(thd < 0.0005, "too much distortion at nominal level");
    thd_lo = thd;
    h.ctl[PORT_GAIN] = 0; /* line +10: -10 dB */
    sine_run(&h, 1000.0, 1.0, 0.5, &g, &thd, &pk);
    printf("  line +10, 0 dBFS in (+8 dBu out): THD %.4f%%\n", 100 * thd);
    h.ctl[PORT_GAIN] = 6; /* line -20: +20 dB */
    sine_run(&h, 1000.0, 0.2, 0.5, &g, &thd, &pk);
    printf("  line -20, +24 dBu out: THD %.3f%%, gain %+.1f dB\n", 100 * thd, db(g));
    thd_hi = thd;
    CHECK(thd_hi > 10 * thd_lo && thd_hi > 0.001, "driving the amplifiers does not distort");
    sine_run(&h, 1000.0, 1.0, 0.5, &g, &thd, &pk);
    printf("  line -20, overdriven by 14 dB: THD %.2f%%, peak %.2f (%+.1f dBu)\n", 100 * thd, pk, db(pk * VREF / 1.0954));
    CHECK(pk < 4.0 && thd > 0.03, "hard overdrive is not limited by the circuit");
    /* Output transformer at low frequencies. */
    h.ctl[PORT_GAIN] = 2;
    h.ctl[PORT_OUTPUT] = 6.0f;
    sine_run(&h, 1000.0, 1.0, 0.5, &g, &thd, &pk);
    thd_hi = thd;
    sine_run(&h, 20.0, 1.0, 2.0, &g, &thd_lf, &pk);
    printf("  +24 dBu out: THD %.3f%% at 1 kHz, %.3f%% at 20 Hz (transformer core)\n", 100 * thd_hi, 100 * thd_lf);
    CHECK(thd_lf > 2.0 * thd_hi, "no low-frequency transformer saturation");
    h.ctl[PORT_OUTPUT] = 0.0f;
    /* Amp A at high gain: more distortion and less bandwidth than at moderate gain. */
    h.ctl[PORT_GAIN] = 19; /* mic -80 */
    sine_run(&h, 1000.0, pow(10.0, -40.0 / 20.0) * 0.125, 0.5, &g, &thd, &pk);
    printf("  mic -80, 0 dBu out: THD %.4f%%\n", 100 * thd);
    thd_hi = thd;
    bw80 = db(gain_at(&h, 20000.0, 0.0003) / gain_at(&h, 1000.0, 0.0003));
    h.ctl[PORT_GAIN] = 11; /* mic -40 */
    sine_run(&h, 1000.0, 0.125, 0.5, &g, &thd, &pk);
    printf("  mic -40, 0 dBu out: THD %.4f%%\n", 100 * thd);
    CHECK(thd_hi > 2.0 * thd, "amp A distortion does not rise with gain");
    bw40 = db(gain_at(&h, 20000.0, 0.03) / gain_at(&h, 1000.0, 0.03));
    printf("  20 kHz relative to 1 kHz: %+.2f dB at mic -40, %+.2f dB at mic -80\n", bw40, bw80);
    CHECK(bw80 < bw40 - 0.1 && bw80 > -0.5, "gain-dependent bandwidth");
    cleanup(h.p);
}

/* ---- 4. Switches, bypass, silence, stereo ---- */

static void test_plugin(void)
{
    Host h;
    float in[4096], out[4096];
    int i;
    double m;
    printf("Plugin:\n");
    host_open(&h, 48000.0);
    /* Phase: the output flips sign. */
    {
        double g0, g1, t, pk;
        float a[8192], b[8192];
        for (i = 0; i < 8192; ++i)
            in[i % 4096] = 0.0f;
        for (i = 0; i < 2; ++i) {
            int j;
            h.ctl[PORT_PHASE] = (float)i;
            activate(h.p);
            for (j = 0; j < 8192; j += 256) {
                int k;
                for (k = 0; k < 256; ++k)
                    in[k] = (float)(0.1 * sin(2.0 * CK_PI * 440.0 * (j + k) / 48000.0));
                connect_port(h.p, PORT_IN, in);
                connect_port(h.p, PORT_OUT, (i ? b : a) + j);
                run(h.p, 256);
            }
        }
        for (g0 = g1 = 0.0, i = 4096; i < 8192; ++i) {
            g0 += a[i] * b[i];
            g1 += a[i] * a[i];
        }
        printf("  phase: correlation of normal and inverted output %.3f\n", g0 / g1);
        CHECK(g0 / g1 < -0.99, "phase switch does not invert");
        h.ctl[PORT_PHASE] = 0;
        (void)t;
        (void)pk;
    }
    /* Silence in, silence out; bypass passes the input delayed by the latency. */
    memset(in, 0, sizeof in);
    connect_port(h.p, PORT_IN, in);
    connect_port(h.p, PORT_OUT, out);
    h.ctl[PORT_GAIN] = 19;
    activate(h.p);
    for (i = 0; i < 20; ++i)
        run(h.p, 4096);
    for (m = 0.0, i = 0; i < 4096; ++i)
        if (fabs(out[i]) > m)
            m = fabs(out[i]);
    printf("  silence at full gain: output peak %.2e, latency port %.0f\n", m, h.ctl[PORT_LATENCY]);
    CHECK(m < 1e-6, "noise with no input");
    CHECK(h.ctl[PORT_LATENCY] == OS_LATENCY, "latency port");
    h.ctl[PORT_ENABLED] = 0.0f;
    activate(h.p);
    in[10] = 1.0f;
    run(h.p, 4096);
    printf("  bypass: impulse at 10 comes out at %d\n", out[10 + OS_LATENCY] == 1.0f ? 10 + OS_LATENCY : -1);
    CHECK(out[10 + OS_LATENCY] == 1.0f, "bypass delay is not the reported latency");
    /* Garbage in: no NaN out, and it recovers. */
    h.ctl[PORT_ENABLED] = 1.0f;
    activate(h.p);
    in[10] = NAN;
    in[20] = INFINITY;
    in[30] = 1e30f;
    run(h.p, 4096);
    memset(in, 0, sizeof in);
    for (i = 0; i < 10; ++i)
        run(h.p, 4096);
    for (m = 0.0, i = 0; i < 4096; ++i)
        m = isfinite(out[i]) ? (fabs(out[i]) > m ? fabs(out[i]) : m) : 1e9;
    printf("  after NaN, inf and 1e30 input: output peak %.2e\n", m);
    CHECK(m < 1e-3, "does not recover from garbage input");
    cleanup(h.p);

    /* Two channels through engine_process match the mono path. */
    {
        Plugin* e = engine_new(48000.0);
        Plugin* mono = engine_new(48000.0);
        float l[2048], r[2048], o1[2048], o2[2048], om[2048];
        const float* ins[2];
        float* outs[2];
        double d = 0.0;
        float ctl[PORT_COUNT];
        memcpy(ctl, DEFAULTS, sizeof ctl);
        ctl[PORT_GAIN] = 6;
        ctl[PORT_MID_FREQ] = 2;
        ctl[PORT_MID_GAIN] = 8;
        for (i = 2; i < PORT_COUNT; ++i) {
            engine_connect(e, (uint32_t)i, &ctl[i]);
            engine_connect(mono, (uint32_t)i, &ctl[i]);
        }
        for (i = 0; i < 2048; ++i) {
            l[i] = (float)(0.3 * sin(i * 0.05));
            r[i] = (float)(0.2 * sin(i * 0.013));
        }
        engine_activate(e);
        engine_activate(mono);
        ins[0] = l; ins[1] = r; outs[0] = o1; outs[1] = o2;
        engine_process(e, ins, outs, 2, 2048);
        ins[0] = r; outs[0] = om;
        engine_process(mono, ins, outs, 1, 2048);
        for (i = 0; i < 2048; ++i)
            if (fabs(o2[i] - om[i]) > d)
                d = fabs(o2[i] - om[i]);
        printf("  stereo: right channel differs from a mono run by %.2e\n", d);
        CHECK(d < 1e-6, "channels are not independent");
        engine_free(e);
        engine_free(mono);
    }
}

/* ---- 5. The published specifications of this kind of channel ----
 *   frequency response   +/-0.5 dB 20 Hz - 20 kHz, -3 dB at 40 kHz
 *   distortion           under 0.07% THD, 50 Hz - 10 kHz, at +20 dBu out
 *   maximum output       over +26 dBu
 * Output dBu = output dBFS + 18 (see VREF). */

static double amp_for_dbu_out(double dbu, double gain_db) { return pow(10.0, (dbu - 18.0 - gain_db) / 20.0); }

static void test_spec(void)
{
    static const double F[] = { 20, 31.5, 50, 100, 1000, 10000, 16000, 20000 };
    static const double FT[] = { 50, 100, 1000, 5000, 10000 };
    static const int GAINS[2] = { 4, 13 }; /* line -10 and mic -50: both +10 dB */
    Host h;
    double g, thd, pk, worst;
    int i, j;
    printf("Published specifications:\n");

    host_open(&h, 48000.0);
    for (j = 0; j < 2; ++j) {
        h.ctl[PORT_GAIN] = (float)GAINS[j];
        worst = 0.0;
        for (i = 0; i < (int)(sizeof F / sizeof F[0]); ++i) {
            g = db(gain_at(&h, F[i], 0.01) / gain_at(&h, 1000.0, 0.01));
            if (fabs(g) > fabs(worst))
                worst = g;
        }
        printf("  response 20 Hz - 20 kHz (%s, 48 kHz): worst %+.2f dB (spec +/-0.5)\n", GAIN_OPTS[GAINS[j]], worst);
        CHECK(fabs(worst) <= 0.5, "response outside +/-0.5 dB, 20 Hz - 20 kHz");
    }
    cleanup(h.p);

    host_open(&h, 192000.0);
    for (j = 0; j < 2; ++j) {
        h.ctl[PORT_GAIN] = (float)GAINS[j];
        g = db(gain_at(&h, 40000.0, 0.01) / gain_at(&h, 1000.0, 0.01));
        printf("  40 kHz (%s, 192 kHz): %+.2f dB (spec -3)\n", GAIN_OPTS[GAINS[j]], g);
        CHECK(g < -2.0 && g > -4.0, "-3 dB point not near 40 kHz");
    }
    cleanup(h.p);

    host_open(&h, 96000.0);
    for (j = 0; j < 2; ++j) {
        h.ctl[PORT_GAIN] = (float)GAINS[j];
        printf("  THD at +20 dBu out (%s):", GAIN_OPTS[GAINS[j]]);
        for (i = 0; i < (int)(sizeof FT / sizeof FT[0]); ++i) {
            sine_run(&h, FT[i], amp_for_dbu_out(20.0, 10.0), FT[i] < 100 ? 2.0 : 0.5, &g, &thd, &pk);
            printf(" %g Hz %.4f%%", FT[i], 100 * thd);
            CHECK(thd < 0.0007, "THD %.4f%% at %g Hz, +20 dBu out", 100 * thd, FT[i]);
        }
        printf(" (spec < 0.07%%)\n");
    }
    {
        double dbu = 20.0, at1 = 0.0;
        h.ctl[PORT_GAIN] = 6; /* line -20: +20 dB */
        for (dbu = 20.0; dbu <= 32.0; dbu += 0.5) {
            sine_run(&h, 1000.0, amp_for_dbu_out(dbu, 20.0), 0.3, &g, &thd, &pk);
            if (thd >= 0.01)
                break;
            at1 = dbu;
        }
        printf("  maximum output (1 kHz, under 1%% THD): +%.1f dBu (spec over +26)\n", at1);
        CHECK(at1 >= 26.0 && at1 <= 29.0, "maximum output +%.1f dBu", at1);
    }
    cleanup(h.p);
}

/* ---- 6. Cost ---- */

static void test_cpu(void)
{
    Host h;
    const int n = 48000 * 10;
    float* in = (float*)malloc(sizeof(float) * n);
    float* out = (float*)malloc(sizeof(float) * n);
    clock_t t0;
    double secs;
    int i;
    for (i = 0; i < n; ++i)
        in[i] = (float)(0.3 * sin(2.0 * CK_PI * 110.0 * i / 48000.0));
    host_open(&h, 48000.0);
    h.ctl[PORT_GAIN] = 15;
    h.ctl[PORT_HPF] = 2;
    h.ctl[PORT_LOW_FREQ] = 2;
    h.ctl[PORT_LOW_GAIN] = 6;
    h.ctl[PORT_MID_FREQ] = 3;
    h.ctl[PORT_MID_GAIN] = 6;
    activate(h.p);
    t0 = clock();
    for (i = 0; i < n; i += 256) {
        connect_port(h.p, PORT_IN, in + i);
        connect_port(h.p, PORT_OUT, out + i);
        run(h.p, 256);
    }
    secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("CPU: 10 s of mono audio at 48 kHz (4x oversampled) in %.2f s = %.1f%% of one core\n", secs, 10.0 * secs);
    CHECK(secs < 3.0, "too slow for real time");
    cleanup(h.p);
    free(in);
    free(out);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_gain();
    test_response();
    test_nonlinear();
    test_plugin();
    test_spec();
    test_cpu();
    printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
