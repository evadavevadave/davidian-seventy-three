/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Davidian Seventy-Three: the circuit model of one channel.
 *
 * Signal path, as on the reference schematic:
 *
 *   mic in -> input transformer (1:2 / 1:4, HI/LO) --+
 *                                                    +-> amp A (sensitivity switch sets its
 *   line in -> line transformer -> switched pad -----+   feedback) -> amp B (+10 dB)
 *     -> high-pass filter (3rd order) -> low shelf, mid bell, 12 kHz high shelf
 *     -> EQ amp -> output level -> class-A output stage -> output transformer -> out
 *
 * Levels are volts referred to the output, with 0 dBFS = +18 dBu (a full-scale sine is
 * 6.16 V rms, 8.72 V peak). The sensitivity markings are the input level that gives
 * 0 dBu out, so the gain of a position is minus its marking. On the mic positions the
 * input is treated as a microphone signal 40 dB below line level, so MIC -40 is unity
 * gain from input to output and MIC -80 is +40 dB.
 *
 * What comes from the schematic: the switch positions and frequencies (its switch
 * charts), the topology, the 18 dB/octave filter (three sections on the high-pass card),
 * the transformer loading networks (C1 180p, C6 2n2, R64 1k5 + C9 10n). The insides of
 * the amplifier and equaliser cards are not on this drawing, so the open-loop gain,
 * clipping levels, transformer core limits and EQ Q values are estimates, chosen to match
 * the published specifications of this kind of channel, which test/test_dsp.c checks:
 * +/-0.5 dB from 20 Hz to 20 kHz, -3 dB near 40 kHz, under 0.07% THD from 50 Hz to
 * 10 kHz at +20 dBu out, and over +26 dBu maximum output. Beyond the specifications they
 * give gain-dependent bandwidth at high gain, low-frequency transformer saturation and a
 * resonant low shelf. Noise is not modelled. */
#ifndef CIRCUIT_H
#define CIRCUIT_H

#include <math.h>
#include <string.h>

#define CK_PI 3.14159265358979323846

/* ---- Switch tables (from the schematic's switch charts) ---- */

#define N_GAIN      20
#define N_GAIN_LINE 7 /* positions 0..6 are the line input, +10 to -20 */
static const double HPF_HZ[5] = { 0, 50, 80, 160, 300 };
static const double LOW_HZ[5] = { 0, 35, 60, 110, 220 };
static const double MID_HZ[7] = { 0, 360, 700, 1600, 3200, 4800, 7200 };
#define HIGH_HZ 12000.0

/* Sensitivity marking of a switch position, dBm: +10..-20 on the line input, -20..-80 mic. */
static int gain_marking(int pos)
{
    return pos < N_GAIN_LINE ? 10 - 5 * pos : -20 - 5 * (pos - N_GAIN_LINE);
}
static int gain_is_mic(int pos) { return pos >= N_GAIN_LINE; }

/* ---- Calibration and estimated circuit constants ---- */

#define VREF        8.72  /* volts peak at 0 dBFS (+18 dBu sine) */
#define MIC_PAD_DB  40.0  /* a digital mic input stands for a signal this far below line */
#define HI_LOSS_DB  3.5   /* HI (series primaries): half the turns ratio, less source loading */
#define AMP_B_DB    10.0  /* fixed gain of the second amplifier */
#define AMP_A_LINE  10.0  /* amp A gain on the line positions (the pad does the rest) */
#define AOL_DB      100.0 /* amp A open-loop gain (estimate) */
#define GBW_HZ      1.9e8 /* amp A gain-bandwidth: 60 kHz at its 70 dB maximum (estimate) */
#define LOW_Q       1.0   /* low shelf: inductor-capacitor network, slightly resonant */
#define MID_Q       1.0   /* mid bell, constant-Q style boost/cut pair */
#define HPF_Q       1.0   /* the 2nd-order part of the 3rd-order Butterworth high-pass */

/* ---- Filters ---- */

typedef struct {
    double b0, b1, b2, a1, a2, z1, z2;
} Biquad;

static double bq_run(Biquad* q, double x)
{
    const double y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}


/* Analog H(s) = (b0 s^2 + b1 s + b2) / (a0 s^2 + a1 s + a2), with s normalised to
 * 2 pi f0, to digital by the bilinear transform with prewarping at f0. Keeps the state,
 * so coefficients can change while running. */
static void bq_analog2(Biquad* q, double b0, double b1, double b2, double a0, double a1, double a2,
                       double f0, double fs)
{
    double K, K2, A0;
    if (f0 > 0.49 * fs)
        f0 = 0.49 * fs;
    K = 1.0 / tan(CK_PI * f0 / fs);
    K2 = K * K;
    A0 = a0 * K2 + a1 * K + a2;
    q->b0 = (b0 * K2 + b1 * K + b2) / A0;
    q->b1 = 2.0 * (b2 - b0 * K2) / A0;
    q->b2 = (b0 * K2 - b1 * K + b2) / A0;
    q->a1 = 2.0 * (a2 - a0 * K2) / A0;
    q->a2 = (a0 * K2 - a1 * K + a2) / A0;
}

/* First order: H(s) = (b0 s + b1) / (a0 s + a1), s normalised to 2 pi f0. */
static void bq_analog1(Biquad* q, double b0, double b1, double a0, double a1, double f0, double fs)
{
    double K, A0;
    if (f0 > 0.49 * fs)
        f0 = 0.49 * fs;
    K = 1.0 / tan(CK_PI * f0 / fs);
    A0 = a0 * K + a1;
    q->b0 = (b0 * K + b1) / A0;
    q->b1 = (b1 - b0 * K) / A0;
    q->b2 = 0.0;
    q->a1 = (a1 - a0 * K) / A0;
    q->a2 = 0.0;
}

static void lp2(Biquad* q, double f, double Q, double fs) { bq_analog2(q, 0, 0, 1, 1, 1.0 / Q, 1, f, fs); }
static void hp2(Biquad* q, double f, double Q, double fs) { bq_analog2(q, 1, 0, 0, 1, 1.0 / Q, 1, f, fs); }
static void lp1(Biquad* q, double f, double fs) { bq_analog1(q, 0, 1, 1, 1, f, fs); }
static void hp1(Biquad* q, double f, double fs) { bq_analog1(q, 1, 0, 1, 1, f, fs); }

/* The EQ curves as analog prototypes. The test checks the plugin against these. */

/* Low shelf, second order, gain g dB below f (RBJ analog form, A^2 = gain). */
static void eq_low(Biquad* q, double f, double db, double fs)
{
    const double A = pow(10.0, db / 40.0), sA = sqrt(A);
    bq_analog2(q, A, A * sA / LOW_Q, A * A, A, sA / LOW_Q, 1.0, f, fs);
}

/* Bell: peak gain g dB at f. Boost and cut are mirror images. */
static void eq_mid(Biquad* q, double f, double db, double fs)
{
    const double G = pow(10.0, fabs(db) / 20.0);
    if (db >= 0.0)
        bq_analog2(q, 1, G / MID_Q, 1, 1, 1.0 / MID_Q, 1, f, fs);
    else
        bq_analog2(q, 1, 1.0 / MID_Q, 1, 1, G / MID_Q, 1, f, fs);
}

/* High shelf, first order, half the gain (in dB) at f. */
static void eq_high(Biquad* q, double f, double db, double fs)
{
    const double sG = pow(10.0, db / 40.0);
    bq_analog1(q, sG, 1.0, 1.0 / sG, 1.0, f, fs);
}

/* ---- Transformer: core flux with soft saturation ----
 * The source resistance R drives the magnetising inductance L in parallel with the
 * (reflected) load: out = v - (R/L) phi (1 + (phi/phis)^4), d phi/dt = out. Linear, that
 * is a first-order high-pass at R/(2 pi L); near saturation the inductance falls, so low
 * frequencies at high level lose level and gain odd harmonics. Trapezoidal integration,
 * solved by Newton's method each sample. */
typedef struct {
    double wc, phis, k; /* R/L (rad/s), saturation flux (V s), T/2 */
    double phi, out;
} Xfmr;

static void xf_init(Xfmr* t, double fc, double phis, double fs)
{
    t->wc = 2.0 * CK_PI * fc;
    t->phis = phis;
    t->k = 0.5 / fs;
    t->phi = t->out = 0.0;
}

static double xf_run(Xfmr* t, double v)
{
    const double base = t->phi + t->k * (v + t->out);
    const double kw = t->k * t->wc, ip = 1.0 / t->phis;
    double p = t->phi, q, q4;
    int i;
    for (i = 0; i < 3; ++i) {
        q = p * ip;
        q4 = q * q * q * q;
        p -= (p + kw * p * (1.0 + q4) - base) / (1.0 + kw * (1.0 + 5.0 * q4));
    }
    q = p * ip;
    q4 = q * q * q * q;
    t->phi = p;
    t->out = v - t->wc * p * (1.0 + q4);
    return t->out;
}

/* ---- Amplifier stage ----
 * Class-A transistor stages with feedback. The open-loop curvature gives second
 * harmonic that the loop gain divides down (c2), and the output swing ends in a knee whose
 * hardness (h, 0 soft to 1 hard) also grows with loop gain. Clipping is asymmetric. */
typedef struct {
    double vp, vn; /* clip levels, positive and negative, volts */
    double c2, h;
} Stage;

static double knee(double x, double h)
{
    const double x2 = x * x;
    const double s2 = x / sqrt(1.0 + x2);
    const double x8 = x2 * x2 * x2 * x2;
    const double s8 = x / sqrt(sqrt(sqrt(1.0 + x8)));
    return s2 + h * (s8 - s2);
}

static double stage_run(const Stage* s, double u)
{
    double x = u / s->vp;
    x += s->c2 * x * x / (1.0 + x * x);
    if (x >= 0.0)
        return s->vp * knee(x, s->h);
    {
        const double r = s->vn / s->vp;
        return s->vn * knee(x / r, s->h);
    }
}

/* Amp A with g dB of closed-loop gain. */
static void stage_a_set(Stage* s, double gain_db)
{
    const double lg_db = AOL_DB - gain_db;
    const double lg = pow(10.0, lg_db / 20.0);
    const double v = 31.5 * pow(10.0, -AMP_B_DB / 20.0); /* 31.5 V output-referred */
    s->vp = v;
    s->vn = 1.1 * v;
    s->c2 = 0.5 / (1.0 + lg);
    s->h = lg_db > 60.0 ? 1.0 : (lg_db < 0.0 ? 0.9 : 0.9 + lg_db / 600.0);
}

/* ---- One channel ---- */

/* Control values, smoothed by the engine. */
typedef struct {
    double amp_a_db;  /* amp A gain */
    double pad_db;    /* line pad */
    double mic;       /* 1 mic input, 0 line; crossfades */
    double hi;        /* 1 HI impedance, 0 LO; crossfades */
    double hpf;       /* 1 filter in, 0 out; crossfades */
    double low_db, mid_db, high_db; /* effective EQ gains (0 when a band is off) */
    double out_db;
    double pol;       /* +1 normal, -1 inverted; ramps through 0 */
} ChannelControls;

/* Coefficients shared by the channels. */
typedef struct {
    double fs;
    Biquad hf_mic_lo, hf_mic_hi, hf_line, hf_out; /* transformer high-frequency networks */
    Biquad bw_a;                                  /* amp A bandwidth */
    Biquad dc;                                    /* coupling capacitors */
    Biquad hpf1, hpf2;
    Biquad low, mid, high;
    Stage a, b, eq, out;
    double gain_mic, gain_line, gain_pad, gain_a, gain_out; /* volts per volt */
} Coefs;

typedef struct {
    Xfmr t_mic_lo, t_mic_hi, t_line, t_out;
    Biquad hf_mic_lo, hf_mic_hi, hf_line, hf_out, bw_a, dc_a, dc_b, dc_eq, hpf1, hpf2, low, mid, high;
} Channel;

static void bq_copy_coefs(Biquad* d, const Biquad* s)
{
    d->b0 = s->b0; d->b1 = s->b1; d->b2 = s->b2;
    d->a1 = s->a1; d->a2 = s->a2;
}

static void coefs_init(Coefs* c, double fs)
{
    c->fs = fs;
    /* Mic transformer with C1 180p across the secondary: LO is well damped by the low source
     * impedance; HI (series primaries, 1200 ohms) rings a little, lower down. */
    lp2(&c->hf_mic_lo, 42000.0, 0.75, fs);
    lp2(&c->hf_mic_hi, 37000.0, 1.0, fs);
    lp2(&c->hf_line, 43000.0, 0.75, fs); /* line transformer with C6 2n2 */
    lp2(&c->hf_out, 50000.0, 0.7, fs);   /* output transformer with the R64/C9 Zobel */
    hp1(&c->dc, 2.0, fs);
    c->b.vp = 31.5; c->b.vn = 33.5; c->b.c2 = 0.0005; c->b.h = 1.0;
    c->eq.vp = 33.5; c->eq.vn = 33.5; c->eq.c2 = 0.0005; c->eq.h = 1.0;
    /* Single-ended class A into the output transformer: the most second harmonic. */
    c->out.vp = 35.5; c->out.vn = 31.5; c->out.c2 = 0.002; c->out.h = 1.0;
}

/* Called when the smoothed controls change (once per control block at most). The
 * positions are the last selected frequencies (never "off": a band that is switched off
 * has its gain ramped to 0 dB at its last frequency, so switching does not click). */
static void coefs_set(Coefs* c, const ChannelControls* k, int hpf_pos, int low_pos, int mid_pos)
{
    const double fs = c->fs;
    stage_a_set(&c->a, k->amp_a_db);
    lp1(&c->bw_a, GBW_HZ / pow(10.0, k->amp_a_db / 20.0), fs);
    hp1(&c->hpf1, HPF_HZ[hpf_pos], fs);
    hp2(&c->hpf2, HPF_HZ[hpf_pos], HPF_Q, fs);
    eq_low(&c->low, LOW_HZ[low_pos], k->low_db, fs);
    eq_mid(&c->mid, MID_HZ[mid_pos], k->mid_db, fs);
    eq_high(&c->high, HIGH_HZ, k->high_db, fs);
    c->gain_mic = VREF * pow(10.0, -(MIC_PAD_DB + k->hi * HI_LOSS_DB) / 20.0);
    c->gain_line = VREF;
    c->gain_pad = pow(10.0, k->pad_db / 20.0);
    c->gain_a = pow(10.0, k->amp_a_db / 20.0);
    c->gain_out = pow(10.0, k->out_db / 20.0);
}

static void channel_init(Channel* ch, double fs)
{
    memset(ch, 0, sizeof *ch);
    xf_init(&ch->t_mic_lo, 2.0, 0.03, fs);  /* mic input transformer, LO */
    xf_init(&ch->t_mic_hi, 0.5, 0.03, fs);  /* the same core, four times the inductance */
    xf_init(&ch->t_line, 2.0, 0.06, fs);    /* line input transformer */
    xf_init(&ch->t_out, 2.0, 0.11, fs);     /* output transformer: soft from about +22 dBu at 20 Hz */
}

/* Keeps the filter states, takes the shared coefficients. */
static void channel_take(Channel* ch, const Coefs* c)
{
    bq_copy_coefs(&ch->hf_mic_lo, &c->hf_mic_lo);
    bq_copy_coefs(&ch->hf_mic_hi, &c->hf_mic_hi);
    bq_copy_coefs(&ch->hf_line, &c->hf_line);
    bq_copy_coefs(&ch->hf_out, &c->hf_out);
    bq_copy_coefs(&ch->bw_a, &c->bw_a);
    bq_copy_coefs(&ch->dc_a, &c->dc);
    bq_copy_coefs(&ch->dc_b, &c->dc);
    bq_copy_coefs(&ch->dc_eq, &c->dc);
    bq_copy_coefs(&ch->hpf1, &c->hpf1);
    bq_copy_coefs(&ch->hpf2, &c->hpf2);
    bq_copy_coefs(&ch->low, &c->low);
    bq_copy_coefs(&ch->mid, &c->mid);
    bq_copy_coefs(&ch->high, &c->high);
}

/* One sample at the oversampled rate. x is the plugin input (1.0 = 0 dBFS); returns the
 * output in the same units. */
static double channel_run(Channel* ch, const Coefs* c, const ChannelControls* k, double x)
{
    double mic, line, v, h;
    /* Inputs: both transformers run all the time so the selector can crossfade. */
    mic = x * c->gain_mic;
    {
        const double lo = bq_run(&ch->hf_mic_lo, xf_run(&ch->t_mic_lo, mic));
        const double hi = bq_run(&ch->hf_mic_hi, xf_run(&ch->t_mic_hi, mic));
        mic = lo + k->hi * (hi - lo);
    }
    line = bq_run(&ch->hf_line, xf_run(&ch->t_line, x * c->gain_line)) * c->gain_pad;
    v = line + k->mic * (mic - line);
    /* Amp A, then amp B. */
    v = bq_run(&ch->bw_a, v * c->gain_a);
    v = bq_run(&ch->dc_a, stage_run(&c->a, v));
    v = bq_run(&ch->dc_b, stage_run(&c->b, v * 3.16227766016838)); /* +10 dB */
    /* High-pass filter. */
    h = bq_run(&ch->hpf2, bq_run(&ch->hpf1, v));
    v += k->hpf * (h - v);
    /* Equaliser and its amplifier. */
    v = bq_run(&ch->high, bq_run(&ch->mid, bq_run(&ch->low, v)));
    v = bq_run(&ch->dc_eq, stage_run(&c->eq, v));
    /* Output level, output stage, output transformer. */
    v = stage_run(&c->out, v * c->gain_out);
    v = bq_run(&ch->hf_out, xf_run(&ch->t_out, v));
    return k->pol * v / VREF;
}

#endif
