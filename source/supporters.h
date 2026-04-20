// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 21.04.2026.
//

#ifndef SUPPORTERS_H
#define SUPPORTERS_H

// ============================================================
// SUPPORTERS LIST
// Add new supporters here. The overlay and settings window
// both read from this single source of truth.
// ============================================================
typedef struct {
    const char *name;
    float amount;
} Supporter;

static const Supporter SUPPORTERS[] = {
    {"zurtleTif", 20.0f},
    {"ethansplace98", 30.0f},
    {"Totorewa", 31.0f},
    {"Zesskyo", 10.0f}
};
static const int NUM_SUPPORTERS = (int) (sizeof(SUPPORTERS) / sizeof(Supporter));

#endif // SUPPORTERS_H
