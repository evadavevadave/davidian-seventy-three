/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The GUI checks shared by ui_check.c (Win32), ui_check_mac.m (Cocoa), vst3_check.c and
 * au_check_mac.m. The including file defines check(), the last_port/last_value/writes
 * globals filled by its write callback, and these mouse helpers in panel coordinates
 * (top-left origin): click(w, x, y), drag(w, x, y, dy), dblclick(w, x, y), wheel_up(w, x, y).
 * The host must have sent the defaults (UI_DEFAULTS) before ui_script runs. */
#ifndef UI_SCRIPT_H
#define UI_SCRIPT_H

#include <math.h>
#include <stdio.h>

#include "../src/ports.h"

/* Port values as a host sends them on opening: the defaults in seventy_three.ttl. */
static const float UI_DEFAULTS[PORT_COUNT] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0 };

static void ui_script(WIDGET_T w)
{
    char label[160];

    /* The sensitivity switch moves one position per 14 px of drag. */
    drag(w, 210, 208, -15);
    snprintf(label, sizeof label, "dragging Sensitivity up 15 px sends port %d = %.0f", last_port, last_value);
    check(label, last_port == PORT_GAIN && last_value == 3.0f);
    drag(w, 210, 208, -400);
    snprintf(label, sizeof label, "dragging Sensitivity far up stops at position %.0f (Mic -80)", last_value);
    check(label, last_port == PORT_GAIN && last_value == 19.0f);
    dblclick(w, 210, 208);
    snprintf(label, sizeof label, "double-click resets Sensitivity to %.0f (Line 0)", last_value);
    check(label, last_port == PORT_GAIN && last_value == 2.0f);

    /* Gain knobs move 1/180 of their travel per pixel. */
    drag(w, 210, 372, -45);
    snprintf(label, sizeof label, "dragging High up 45 px sends %+.2f dB", last_value);
    check(label, last_port == PORT_HIGH_GAIN && fabsf(last_value - 8.0f) < 0.01f);
    dblclick(w, 210, 372);
    check("double-click resets High to 0 dB", last_port == PORT_HIGH_GAIN && last_value == 0.0f);
    /* Concentric pair: the inner knob is the gain, the skirt around it the frequency. */
    drag(w, 115, 492, -45);
    snprintf(label, sizeof label, "dragging the inner Mid knob up 45 px sends port %d = %+.2f dB", last_port, last_value);
    check(label, last_port == PORT_MID_GAIN && fabsf(last_value - 9.0f) < 0.01f);
    drag(w, 305 + 32, 492, -15);
    snprintf(label, sizeof label, "dragging the Low skirt up 15 px sends port %d = %.0f", last_port, last_value);
    check(label, last_port == PORT_LOW_FREQ && last_value == 1.0f);
    drag(w, 344, 722, 18);
    snprintf(label, sizeof label, "dragging Output down 18 px sends %.2f dB", last_value);
    check(label, last_port == PORT_OUTPUT && fabsf(last_value + 3.6f) < 0.01f);

    click(w, 40, 250 + 18);
    check("bottom of the impedance switch selects HI", last_port == PORT_IMPEDANCE && last_value == 1.0f);
    click(w, 40, 250 - 18);
    check("top of the impedance switch selects LO", last_port == PORT_IMPEDANCE && last_value == 0.0f);
    click(w, 70, 730);
    check("phase button inverts", last_port == PORT_PHASE && last_value == 1.0f);
    click(w, 150, 730);
    check("EQ IN button takes the equaliser out", last_port == PORT_EQ_IN && last_value == 0.0f);
    click(w, 230, 730);
    check("power button bypasses", last_port == PORT_ENABLED && last_value == 0.0f);

    writes = 0;
    wheel_up(w, 115, 492 - 33);
    snprintf(label, sizeof label, "mouse wheel on the Mid frequency skirt sends position %.0f", last_value);
    check(label, writes == 1 && last_port == PORT_MID_FREQ && last_value == 1.0f);
    click(w, 5, 5);
    check("clicking empty panel sends nothing new", writes == 1);
}

#endif
