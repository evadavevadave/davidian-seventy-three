/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The GUI panel: layout, drawing and mouse handling, independent of any windowing system.
 *
 * A tall single-channel module, laid out top to bottom in the order of a classic console
 * channel strip: sensitivity switch, 12 kHz shelf, the mid and low bands as concentric
 * controls (frequency on the outer skirt, gain on the inner knob), high-pass filter, then
 * the switches and output trim. The look is the Davidian house style (graphite panel,
 * cream lettering, amber accents, red LEDs, black knurled knobs); it uses no branding,
 * colours or panel artwork from any original console. Ranges, defaults and position
 * names come from params.h. */
#ifndef UI_PANEL_H
#define UI_PANEL_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../params.h"
#include "../ports.h"
#include "canvas.h"
#include "font.h"
#include "style.h"

#define PANEL_W 420
#define PANEL_H 800

/* W_INNER: the gain knob in the middle of a concentric pair (no scale of its own). */
enum { W_KNOB, W_INNER, W_STEP, W_SWITCH, W_BUTTON };

typedef struct {
    int kind, port;
    float x, y, r;
    float span;               /* half the rotation, radians (knobs and stepped knobs) */
    const char* label;        /* on the panel */
    const char* const* marks; /* stepped knobs: label at each position ("" for none) */
    const char* lo;           /* scale end labels for continuous knobs */
    const char* hi;
} Widget;

#define KNOB_SPAN (0.75f * CV_PI) /* +/-135 degrees */
#define DEG       (CV_PI / 180.0f)

static const char* const GAIN_MARKS[20] = {
    "+10", "", "0", "", "-10", "", "-20", "", "", "-30", "", "-40", "", "-50", "", "-60", "", "-70", "", "-80"
};
static const char* const HPF_MARKS[5] = { "OFF", "50", "80", "160", "300" };
static const char* const LOW_MARKS[5] = { "OFF", "35", "60", "110", "220" };
static const char* const MID_MARKS[7] = { "OFF", ".36", ".7", "1.6", "3.2", "4.8", "7.2" };

/* Drawn in this order; hit-tested in reverse, so an inner knob wins over its skirt. */
static const Widget WIDGETS[] = {
    { W_STEP, PORT_GAIN, 210, 208, 40, KNOB_SPAN, "SENSITIVITY  DBM", GAIN_MARKS, 0, 0 },
    { W_SWITCH, PORT_IMPEDANCE, 40, 250, 0, 0, "MIC IMP", 0, 0, 0 },
    { W_KNOB, PORT_HIGH_GAIN, 210, 372, 24, KNOB_SPAN, "HIGH  12 KHZ", 0, "-16", "+16" },
    { W_STEP, PORT_MID_FREQ, 115, 492, 36, 90 * DEG, "MID  KHZ", MID_MARKS, 0, 0 },
    { W_INNER, PORT_MID_GAIN, 115, 492, 21, KNOB_SPAN, "", 0, 0, 0 },
    { W_STEP, PORT_LOW_FREQ, 305, 492, 36, 64 * DEG, "LOW  HZ", LOW_MARKS, 0, 0 },
    { W_INNER, PORT_LOW_GAIN, 305, 492, 21, KNOB_SPAN, "", 0, 0, 0 },
    { W_STEP, PORT_HPF, 210, 595, 22, 64 * DEG, "HIGH-PASS  HZ", HPF_MARKS, 0, 0 },
    { W_BUTTON, PORT_PHASE, 70, 730, 15, 0, "PHASE", 0, 0, 0 },
    { W_BUTTON, PORT_EQ_IN, 150, 730, 15, 0, "EQ IN", 0, 0, 0 },
    { W_BUTTON, PORT_ENABLED, 230, 730, 15, 0, "POWER", 0, 0, 0 },
    { W_KNOB, PORT_OUTPUT, 344, 722, 20, KNOB_SPAN, "OUTPUT", 0, "-24", "+12" },
};
#define N_WIDGETS ((int)(sizeof WIDGETS / sizeof WIDGETS[0]))

/* The readout window. */
#define RO_X0 16
#define RO_Y0 74
#define RO_X1 404
#define RO_Y1 110

typedef void (*PanelWrite)(void* host, int port, float value);
/* Optional: brackets each user edit (begin = 1, then writes, then begin = 0), so hosts
 * that record automation see one gesture per drag. */
typedef void (*PanelGesture)(void* host, int port, int begin);

typedef struct {
    uint32_t* px; /* what the window shows */
    uint32_t* bg; /* static artwork */
    Canvas c;
    float val[PORT_COUNT];
    int drag;      /* widget being dragged, or -1 */
    float drag_x, drag_y, drag_norm;
    int shown;     /* widget in the display, or -1 */
    int dirty;
    PanelWrite write;
    PanelGesture gesture; /* may be NULL; set after panel_init */
    void* host;
} Panel;

/* ---- Values ---- */

static const ParamInfo* w_info(const Widget* w) { return param_info(w->port); }

static float w_norm(const Widget* w, float v)
{
    const ParamInfo* p = w_info(w);
    const float n = (v - p->min) / (p->max - p->min);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

static float w_value(const Widget* w, float n)
{
    const ParamInfo* p = w_info(w);
    float v;
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    v = p->min + n * (p->max - p->min);
    return p->npos ? floorf(v + 0.5f) : v;
}

static int w_index(const Panel* p, const Widget* w)
{
    const ParamInfo* pi = w_info(w);
    const int n = pi->npos ? pi->npos : 2;
    const int i = (int)floorf(p->val[w->port] + 0.5f);
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

static void upper(char* s)
{
    for (; *s; ++s)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 32);
}

/* Upper case for the stroke font's readout. */
static void w_format(const Panel* p, const Widget* w, char* buf, size_t n)
{
    const ParamInfo* pi = w_info(w);
    if (pi->npos)
        snprintf(buf, n, "%s", pi->opts[w_index(p, w)]);
    else
        snprintf(buf, n, "%+.1f DB", p->val[w->port]);
    upper(buf);
}

static float step_angle(const Widget* w, int i)
{
    const int npos = w_info(w)->npos;
    return -w->span + 2.0f * w->span * (float)i / (float)(npos - 1);
}

static float mark_radius(const Widget* w) { return w->r + (w->r >= 36.0f ? 20.0f : 19.0f); }

/* ---- Static artwork ---- */

static void draw_screw(Canvas* c, float x, float y)
{
    cv_circle(c, x + 1, y + 1.5f, 6, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 6, 0xb8bbc2, 0x4a4d54, -0.4f, -0.5f, 1);
    cv_line(c, x - 4, y + 2, x + 4, y - 2, 1.6f, 0x2a2c30, 0.9f);
}

static void draw_section(Canvas* c, float x0, float y0, float x1, float y1, const char* title)
{
    const float tw = text_width(title, TXT_SECTION, 2.5f);
    const float cx = 0.5f * (x0 + x1);
    cv_rrect(c, x0, y0, x1, y1, 10, 0x000000, 0.18f);
    cv_rrect_line(c, x0, y0, x1, y1, 10, 1.4f, COL_LINE, 0.9f);
    /* Title sits in a gap in the top edge. */
    cv_rrect(c, cx - 0.5f * tw - 8, y0 - 6, cx + 0.5f * tw + 8, y0 + 6, 3,
             mix(COL_PANEL_TOP, COL_PANEL_BOT, y0 / PANEL_H), 1);
    text_center(c, cx, y0 - 4.5f, title, TXT_SECTION, 2.5f, 1.5f, COL_AMBER, 1);
}

static void draw_knob_scale(Canvas* c, const Widget* w)
{
    const int big = w->r >= 24.0f;
    const int ticks = 11;
    int i;
    for (i = 0; i < ticks; ++i) {
        const float a = -w->span + 2.0f * w->span * i / (ticks - 1);
        const float r0 = w->r + 8.0f, r1 = r0 + ((i % 5 == 0) ? 6.0f : 3.5f);
        cv_line(c, cv_ax(w->x, r0, a), cv_ay(w->y, r0, a), cv_ax(w->x, r1, a), cv_ay(w->y, r1, a),
                1.3f, COL_INK, 0.8f);
    }
    /* Dim track under the value arc. */
    cv_arc(c, w->x, w->y, w->r + 4.5f, 2.2f, -w->span, w->span, 0x000000, 0.55f);
    if (w->lo) {
        const float rr = w->r + 22.0f;
        text_center(c, cv_ax(w->x, rr, -w->span) - 4, cv_ay(w->y, rr, -w->span) - 3, w->lo, TXT_SMALL, 1, 1.1f, COL_DIM, 1);
        text_center(c, cv_ax(w->x, rr, w->span) + 4, cv_ay(w->y, rr, w->span) - 3, w->hi, TXT_SMALL, 1, 1.1f, COL_DIM, 1);
    }
    text_center(c, w->x, w->y + w->r + 24.0f, w->label, big ? TXT_LABEL : 8.0f, 2.0f, big ? 1.7f : 1.4f, COL_INK, 1);
}

static void draw_step_scale(Canvas* c, const Widget* w)
{
    const int npos = w_info(w)->npos;
    const float mr = mark_radius(w);
    int i;
    for (i = 0; i < npos; ++i) {
        const float a = step_angle(w, i);
        const float r0 = w->r + 5.0f, r1 = r0 + (w->marks[i][0] ? 5.0f : 3.0f);
        cv_line(c, cv_ax(w->x, r0, a), cv_ay(w->y, r0, a), cv_ax(w->x, r1, a), cv_ay(w->y, r1, a),
                1.3f, COL_INK, 0.8f);
        if (w->marks[i][0])
            text_center(c, cv_ax(w->x, mr, a), cv_ay(w->y, mr, a) - 3.5f, w->marks[i], TXT_SMALL, 1, 1.1f, COL_DIM, 1);
    }
    text_center(c, w->x, w->y + mr + (w->r >= 36.0f ? 10.0f : 8.0f), w->label, w->r >= 36.0f ? TXT_LABEL : 8.0f,
                2.0f, w->r >= 36.0f ? 1.7f : 1.4f, COL_INK, 1);
}

static void draw_switch_static(Canvas* c, const Widget* w)
{
    text_center(c, w->x + 12, w->y - 44, w->label, 7, 1.8f, 1.3f, COL_INK, 1);
    /* Mounting plate and nut. */
    cv_rrect(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 0x000000, 0.35f);
    cv_rrect_line(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 1, 0x71757d, 0.7f);
    cv_circle(c, w->x + 1, w->y + 2, 8.5f, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, 8.5f, COL_METAL_LIGHT, COL_METAL_DARK, -0.4f, -0.6f, 1);
    cv_circle(c, w->x, w->y, 5, 0x2d2f33, 1);
}

static void draw_button_static(Canvas* c, const Widget* w)
{
    cv_circle(c, w->x + 2, w->y + 4, w->r + 6, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, w->r + 5, 0xa9adb5, 0x3c3f45, -0.5f, -0.7f, 1);
    cv_ring(c, w->x, w->y, w->r + 5, 1.2f, 0x202226, 0.8f);
    text_center(c, w->x, w->y + w->r + 13, w->label, 8, 2.0f, 1.4f, COL_INK, 1);
    /* LED bezel */
    cv_circle(c, w->x, w->y - w->r - 19, 6.5f, 0x0e0e10, 1);
    cv_ring(c, w->x, w->y - w->r - 19, 6.5f, 1.4f, 0x8a8e96, 0.8f);
}

static void draw_background(Panel* p)
{
    Canvas c;
    int i, y;
    cv_init(&c, p->bg, PANEL_W, PANEL_H);
    for (y = 0; y < PANEL_H; ++y) {
        uint32_t col = mix(COL_PANEL_TOP, COL_PANEL_BOT, (float)y / PANEL_H);
        int x;
        for (x = 0; x < PANEL_W; ++x)
            p->bg[y * PANEL_W + x] = col;
    }
    cv_texture(&c, PANEL_TEXTURE);
    /* Bevelled edge */
    cv_rrect_line(&c, 1.5f, 1.5f, PANEL_W - 1.5f, PANEL_H - 1.5f, 6, 1.5f, 0x55595f, 0.8f);
    cv_rrect_line(&c, 3.5f, 3.5f, PANEL_W - 3.5f, PANEL_H - 3.5f, 5, 1.5f, 0x000000, 0.4f);

    /* Header */
    text_center(&c, PANEL_W / 2, 14, "DAVIDIAN", 10, 4.0f, 1.6f, COL_AMBER, 1);
    text_center(&c, PANEL_W / 2, 30, "SEVENTY-THREE", TXT_TITLE, 3.2f, 2.8f, COL_INK, 1);
    text_center(&c, PANEL_W / 2, 58, "CLASS-A MIC PREAMPLIFIER  /  EQUALISER", 6, 2.2f, 1.0f, COL_DIM, 1);
    /* Readout window */
    cv_rrect(&c, RO_X0, RO_Y0, RO_X1, RO_Y1, 6, 0x000000, 0.6f);
    cv_rrect(&c, RO_X0 + 2, RO_Y0 + 2, RO_X1 - 2, RO_Y1 - 2, 5, 0x0d0a06, 1);
    cv_rrect_line(&c, RO_X0, RO_Y0, RO_X1, RO_Y1, 6, 1.2f, 0x6d7179, 0.8f);

    draw_section(&c, 16, 124, 404, 302, "INPUT");
    draw_section(&c, 16, 318, 404, 662, "EQUALISER");
    draw_section(&c, 16, 678, 404, 786, "OUTPUT");

    /* Which side of the sensitivity switch is which input. */
    text_center(&c, 118, 214, "LINE", 8, 2.0f, 1.4f, COL_AMBER, 0.85f);
    text_center(&c, 306, 158, "MIC", 8, 2.0f, 1.4f, COL_AMBER, 0.85f);
    /* Concentric pairs: the inner knob's gain range under the label. */
    text_center(&c, 115, 577, "GAIN +/-18 DB", 6, 1.5f, 1.0f, COL_DIM, 1);
    text_center(&c, 305, 577, "GAIN +/-16 DB", 6, 1.5f, 1.0f, COL_DIM, 1);

    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB: draw_knob_scale(&c, w); break;
        case W_STEP: draw_step_scale(&c, w); break;
        case W_SWITCH: draw_switch_static(&c, w); break;
        case W_BUTTON: draw_button_static(&c, w); break;
        default: break;
        }
    }

    draw_screw(&c, 14, 14);
    draw_screw(&c, PANEL_W - 14, 14);
    draw_screw(&c, 14, PANEL_H - 14);
    draw_screw(&c, PANEL_W - 14, PANEL_H - 14);
}

/* ---- Moving parts ---- */

static void draw_knob_body(Canvas* c, const Widget* w, float ang, int skirt)
{
    const float r = w->r, x = w->x, y = w->y;
    const int knurls = r >= 30.0f ? 40 : (r >= 22.0f ? 32 : 24);
    int i;
    cv_circle(c, x + 2, y + 4, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (i = 0; i < knurls; ++i) {
        const float a = ang + 2.0f * CV_PI * i / knurls;
        cv_line(c, cv_ax(x, r * 0.84f, a), cv_ay(y, r * 0.84f, a), cv_ax(x, r * 0.98f, a),
                cv_ay(y, r * 0.98f, a), r >= 22.0f ? 1.6f : 1.2f, 0x000000, 0.55f);
    }
    cv_shaded_circle(c, x, y, r * 0.74f, 0x3c3e44, 0x18191c, -0.35f, -0.55f, 1);
    cv_ring(c, x, y, r * 0.74f, 1.0f, 0x6a6d74, 0.5f);
    if (skirt) /* stepped knobs: an amber pointer on the skirt that reaches the scale */
        cv_line(c, cv_ax(x, r * 0.78f, ang), cv_ay(y, r * 0.78f, ang), cv_ax(x, r + 4.0f, ang),
                cv_ay(y, r + 4.0f, ang), 2.6f, COL_AMBER, 1);
    cv_line(c, cv_ax(x, r * 0.2f, ang), cv_ay(y, r * 0.2f, ang), cv_ax(x, r * 0.7f, ang),
            cv_ay(y, r * 0.7f, ang), r >= 22.0f ? 3.0f : 2.2f, COL_INK, 1);
}

static void draw_knob(Canvas* c, const Widget* w, float n, int active)
{
    const float ang = -w->span + 2.0f * w->span * n;
    /* Bipolar gain knobs light the arc from the centre. */
    const float a0 = w_info(w)->min < 0.0f && w->port != PORT_OUTPUT ? 0.0f : -w->span;
    if (w->kind == W_INNER) /* a dark well in the skirt, under the inner knob's arc */
        cv_circle(c, w->x, w->y, w->r + 7.0f, 0x0b0b0d, 0.9f);
    if (fabsf(ang - a0) > 0.01f)
        cv_arc(c, w->x, w->y, w->r + 4.0f, 2.2f, ang < a0 ? ang : a0, ang < a0 ? a0 : ang,
               active ? 0xffd27f : COL_AMBER, 0.95f);
    draw_knob_body(c, w, ang, 0);
}

static void draw_step(Panel* p, const Widget* w, int active)
{
    const int idx = w_index(p, w);
    const float a = step_angle(w, idx);
    const float mr = mark_radius(w);
    if (w->marks[idx][0]) /* the selected position's label lights up */
        text_center(&p->c, cv_ax(w->x, mr, a), cv_ay(w->y, mr, a) - 3.5f, w->marks[idx], TXT_SMALL, 1, 1.1f,
                    active ? 0xffd27f : COL_AMBER, 1);
    draw_knob_body(&p->c, w, a, 1);
}

static void draw_switch(Canvas* c, const Widget* w, int idx)
{
    const float x = w->x, y = w->y;
    const ParamInfo* pi = w_info(w);
    float ey;
    int i;
    for (i = 0; i < 2; ++i) {
        char s[16];
        snprintf(s, sizeof s, "%s", pi->opts[i]);
        upper(s);
        text_draw(c, x + 15, y - 20.0f + 40.0f * i - 3.5f, s, 7, 1.6f, 1.25f, i == idx ? COL_AMBER : COL_DIM, 1);
    }
    ey = (idx == 0) ? y - 19.0f : y + 19.0f;
    cv_line(c, x + 1.5f, y + 2, x + 1.5f, ey + 2, 6, 0x000000, 0.45f);
    cv_line(c, x, y, x, ey, 5.5f, 0xb6b9bf, 1);
    cv_line(c, x - 1.2f, y, x - 1.2f, ey, 1.6f, 0xf0f1f3, 0.7f);
    cv_shaded_circle(c, x, ey, 4.8f, 0xf4f5f7, 0x7c7f86, -0.35f, -0.45f, 1);
}

static void draw_button(Canvas* c, const Widget* w, int on)
{
    const float lx = w->x, ly = w->y - w->r - 19;
    /* A latched button sits lower and darker. */
    cv_shaded_circle(c, w->x, w->y + (on ? 1.0f : 0.0f), w->r, on ? 0xc9ccd2 : 0xe3e5e9, 0x6b6e75, -0.4f, -0.55f, 1);
    cv_ring(c, w->x, w->y, w->r * 0.62f, 1.2f, 0x8e9198, 0.6f);
    cv_ring(c, w->x, w->y, w->r, 1.0f, 0x34363b, 0.8f);
    if (on) {
        cv_glow(c, lx, ly, 4.5f, 14, COL_RED, 0.35f);
        cv_shaded_circle(c, lx, ly, 4.8f, 0xffd0c8, COL_RED, -0.3f, -0.4f, 1);
    } else {
        cv_shaded_circle(c, lx, ly, 4.8f, 0x5a1f1b, 0x240a08, -0.3f, -0.4f, 1);
    }
}

static void draw_display(Panel* p)
{
    char val[64], name[64];
    Canvas* c = &p->c;
    if (p->shown < 0) {
        text_draw(c, RO_X0 + 14, RO_Y0 + 8, "SEVENTY-THREE", 8, 2.2f, 1.3f, COL_AMBER_DK, 1);
        text_draw(c, RO_X0 + 14, RO_Y0 + 22, "DRAG KNOBS - DOUBLE-CLICK RESETS", 7, 1.6f, 1.2f, 0x9c6a24, 1);
        return;
    }
    w_format(p, &WIDGETS[p->shown], val, sizeof val);
    snprintf(name, sizeof name, "%s", w_info(&WIDGETS[p->shown])->name);
    upper(name);
    text_draw(c, RO_X0 + 14, RO_Y0 + 13, name, 8, 2.2f, 1.3f, 0xc58b33, 1);
    text_draw(c, RO_X1 - 14 - text_width(val, 13, 2.0f), RO_Y0 + 11, val, 13, 2.0f, 2.0f, COL_AMBER, 1);
}

static void panel_draw(Panel* p)
{
    int i;
    memcpy(p->px, p->bg, sizeof(uint32_t) * PANEL_W * PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB:
        case W_INNER: draw_knob(&p->c, w, w_norm(w, p->val[w->port]), p->drag == i); break;
        case W_STEP: draw_step(p, w, p->drag == i); break;
        case W_SWITCH: draw_switch(&p->c, w, w_index(p, w)); break;
        case W_BUTTON: draw_button(&p->c, w, w_index(p, w)); break;
        }
    }
    draw_display(p);
    p->dirty = 0;
}

/* ---- Setup and host values ---- */

static int panel_init(Panel* p, PanelWrite write, void* host)
{
    int i;
    memset(p, 0, sizeof *p);
    p->px = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    p->bg = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    if (!p->px || !p->bg) {
        free(p->px);
        free(p->bg);
        return 0;
    }
    cv_init(&p->c, p->px, PANEL_W, PANEL_H);
    for (i = PORT_FIRST_PARAM; i <= PORT_LAST_PARAM; ++i)
        p->val[i] = param_info(i)->def;
    p->drag = -1;
    p->shown = -1;
    p->write = write;
    p->host = host;
    draw_background(p);
    panel_draw(p);
    return 1;
}

static void panel_free(Panel* p)
{
    free(p->px);
    free(p->bg);
    p->px = p->bg = NULL;
}

/* A value from the host. */
static void panel_set(Panel* p, int port, float v)
{
    if (port < 0 || port >= PORT_COUNT || !isfinite(v))
        return;
    if (p->val[port] != v) {
        p->val[port] = v;
        p->dirty = 1;
    }
}

/* ---- Mouse ---- */

static void set_value(Panel* p, int wi, float v)
{
    const Widget* w = &WIDGETS[wi];
    const ParamInfo* pi = w_info(w);
    if (pi->npos)
        v = floorf(v + 0.5f);
    if (v < pi->min) v = pi->min;
    if (v > pi->max) v = pi->max;
    p->shown = wi;
    p->dirty = 1;
    if (p->val[w->port] != v) {
        /* Outside a drag (clicks, wheel, double-click) each change is its own gesture. */
        const int lone = p->drag != wi && p->gesture;
        p->val[w->port] = v;
        if (lone)
            p->gesture(p->host, w->port, 1);
        if (p->write)
            p->write(p->host, w->port, v);
        if (lone)
            p->gesture(p->host, w->port, 0);
    }
}

static void drag_start(Panel* p, int wi)
{
    p->drag = wi;
    if (p->gesture)
        p->gesture(p->host, WIDGETS[wi].port, 1);
}

static int hit(float x, float y)
{
    int i;
    for (i = N_WIDGETS - 1; i >= 0; --i) {
        const Widget* w = &WIDGETS[i];
        const float dx = x - w->x, dy = y - w->y;
        const float grab = w->kind == W_INNER ? w->r + 6.0f : w->r + 8.0f;
        switch (w->kind) {
        case W_SWITCH:
            if (dx >= -14 && dx <= 36 && dy >= -30 && dy <= 30)
                return i;
            break;
        default:
            if (dx * dx + dy * dy <= grab * grab)
                return i;
        }
    }
    return -1;
}

/* Pixels of vertical drag per full travel: stepped knobs move about 14 px per position. */
static float drag_range(const Widget* w)
{
    const int npos = w_info(w)->npos;
    return npos ? 14.0f * (npos - 1) : 180.0f;
}

static void panel_mouse_down(Panel* p, float x, float y, int dbl)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    switch (w->kind) {
    case W_SWITCH:
        set_value(p, i, y < w->y ? 0.0f : 1.0f);
        break;
    case W_BUTTON:
        set_value(p, i, w_index(p, w) ? 0.0f : 1.0f);
        break;
    default:
        if (dbl) {
            set_value(p, i, w_info(w)->def);
            return;
        }
        drag_start(p, i);
        p->drag_y = y;
        p->drag_norm = w_norm(w, p->val[w->port]);
        p->shown = i;
        p->dirty = 1;
        break;
    }
}

static void panel_mouse_move(Panel* p, float x, float y, int fine)
{
    const Widget* w;
    float n;
    (void)x;
    if (p->drag < 0)
        return;
    w = &WIDGETS[p->drag];
    n = p->drag_norm + (p->drag_y - y) / (drag_range(w) * (fine ? 5.0f : 1.0f));
    if (w->kind == W_STEP) {
        /* Keep the unrounded position, so slow drags still get to the next step. */
        set_value(p, p->drag, w_value(w, n));
        if (n < 0.0f || n > 1.0f) {
            p->drag_norm = n < 0.0f ? 0.0f : 1.0f;
            p->drag_y = y;
        }
        return;
    }
    set_value(p, p->drag, w_value(w, n));
    /* Fine mode needs a fresh reference so switching modes mid-drag doesn't jump. */
    p->drag_norm = w_norm(w, p->val[w->port]);
    p->drag_y = y;
}

static void panel_mouse_up(Panel* p)
{
    const int d = p->drag;
    if (d < 0)
        return;
    p->dirty = 1;
    p->drag = -1;
    if (p->gesture)
        p->gesture(p->host, WIDGETS[d].port, 0);
}

static void panel_wheel(Panel* p, float x, float y, float steps, int fine)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    if (w->kind == W_STEP || w->kind == W_SWITCH)
        set_value(p, i, p->val[w->port] + (steps > 0 ? 1.0f : -1.0f));
    else if (w->kind != W_BUTTON)
        set_value(p, i, w_value(w, w_norm(w, p->val[w->port]) + steps * (fine ? 0.004f : 0.025f)));
}

#endif
