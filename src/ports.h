/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Port indices, shared by the plugin, its GUI and the VST3/AU wrappers. They must match
 * bundle/seventy_three.ttl. The VST3 and AU parameter IDs are the same numbers. */
#ifndef PORTS_H
#define PORTS_H

#define ST_URI    "urn:davidian:seventy-three"
#define ST_UI_URI "urn:davidian:seventy-three#ui"
#define ST_NAME   "Davidian Seventy-Three"

enum {
    PORT_IN, PORT_OUT,
    PORT_GAIN,      /* sensitivity switch position, 0..19 (see GAIN_* in circuit.h) */
    PORT_IMPEDANCE, /* mic input: 0 LO (parallel primaries), 1 HI (series) */
    PORT_HPF,       /* 0 off, 1..4: 50, 80, 160, 300 Hz */
    PORT_LOW_FREQ,  /* 0 off, 1..4: 35, 60, 110, 220 Hz */
    PORT_LOW_GAIN,  /* dB, -16..+16 */
    PORT_MID_FREQ,  /* 0 off, 1..6: 0.36, 0.7, 1.6, 3.2, 4.8, 7.2 kHz */
    PORT_MID_GAIN,  /* dB, -18..+18 */
    PORT_HIGH_GAIN, /* dB, -16..+16, 12 kHz shelf */
    PORT_EQ_IN,     /* 0 out, 1 in */
    PORT_PHASE,     /* 0 normal, 1 inverted */
    PORT_OUTPUT,    /* output level, dB, -24..+12 */
    PORT_ENABLED,   /* 0 bypassed, 1 on */
    PORT_LATENCY,   /* output */
    PORT_COUNT
};

/* The host-visible parameters are PORT_FIRST_PARAM..PORT_LAST_PARAM. */
#define PORT_FIRST_PARAM PORT_GAIN
#define PORT_LAST_PARAM  PORT_ENABLED
#define N_PARAMS         (PORT_LAST_PARAM - PORT_FIRST_PARAM + 1)

#endif
