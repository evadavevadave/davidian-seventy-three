/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The parameter table shared by the VST3 and AU wrappers and the GUI: names, ranges,
 * defaults and the names of each switch position. Indexed by port - PORT_FIRST_PARAM.
 * The ranges and defaults must match bundle/seventy_three.ttl. */
#ifndef PARAMS_H
#define PARAMS_H

#include "ports.h"

typedef struct {
    const char* name;
    const char* short_name;
    const char* unit; /* "dB" or "" */
    float min, max, def;
    int npos;                 /* switch positions, or 0 for a continuous control */
    const char* const* opts;  /* npos names */
} ParamInfo;

static const char* const GAIN_OPTS[20] = {
    "Line +10", "Line +5", "Line 0", "Line -5", "Line -10", "Line -15", "Line -20",
    "Mic -20", "Mic -25", "Mic -30", "Mic -35", "Mic -40", "Mic -45", "Mic -50",
    "Mic -55", "Mic -60", "Mic -65", "Mic -70", "Mic -75", "Mic -80"
};
static const char* const IMP_OPTS[2] = { "Lo", "Hi" };
static const char* const HPF_OPTS[5] = { "Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz" };
static const char* const LOW_OPTS[5] = { "Off", "35 Hz", "60 Hz", "110 Hz", "220 Hz" };
static const char* const MID_OPTS[7] = { "Off", "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" };
static const char* const EQ_OPTS[2] = { "Out", "In" };
static const char* const PHASE_OPTS[2] = { "Normal", "Inverted" };
static const char* const ON_OPTS[2] = { "Bypass", "On" };

static const ParamInfo PARAM_INFO[N_PARAMS] = {
    { "Sensitivity", "Gain", "", 0, 19, 2, 20, GAIN_OPTS },
    { "Mic Impedance", "Mic Imp", "", 0, 1, 0, 2, IMP_OPTS },
    { "High-Pass Filter", "HPF", "", 0, 4, 0, 5, HPF_OPTS },
    { "Low Frequency", "Low Frq", "", 0, 4, 0, 5, LOW_OPTS },
    { "Low Gain", "Low", "dB", -16, 16, 0, 0, 0 },
    { "Mid Frequency", "Mid Frq", "", 0, 6, 0, 7, MID_OPTS },
    { "Mid Gain", "Mid", "dB", -18, 18, 0, 0, 0 },
    { "High Gain (12 kHz)", "High", "dB", -16, 16, 0, 0, 0 },
    { "EQ In", "EQ In", "", 0, 1, 1, 2, EQ_OPTS },
    { "Phase", "Phase", "", 0, 1, 0, 2, PHASE_OPTS },
    { "Output", "Output", "dB", -24, 12, 0, 0, 0 },
    { "Power", "Power", "", 0, 1, 1, 2, ON_OPTS },
};

static const ParamInfo* param_info(int port)
{
    return port >= PORT_FIRST_PARAM && port <= PORT_LAST_PARAM ? &PARAM_INFO[port - PORT_FIRST_PARAM] : 0;
}

#endif
