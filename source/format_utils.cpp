// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 20.08.2025.
//

#include "format_utils.h"
#include <cctype>
#include <cstdio>

void format_category_string(const char *input, char *output, size_t max_len) {
    if (!input || !output || max_len == 0) {
        if (output && max_len > 0) output[0] = '\0';
        return;
    }

    size_t i = 0;
    bool capitalize_next = true;

    // If the string starts with an underscore, skip it but still capitalize the next letter.
    if (*input == '_') {
        input++;
    }

    while (*input && i < max_len - 1) {
        if (*input == '_') {
            output[i++] = ' ';
            capitalize_next = true;
        } else {
            output[i++] = capitalize_next ? (char) toupper((unsigned char) *input) : *input;
            capitalize_next = false;
        }
        input++;
    }
    output[i] = '\0';
}

void format_time(long long ticks, char *output, size_t max_len, bool unit_spacing, bool always_show_ms) {
    if (!output || max_len == 0) return;

    long long total_seconds = ticks / 20;
    long long days_total = total_seconds / 86400;
    long long years = days_total / 365;
    long long days = days_total % 365;

    long long hours = (total_seconds % 86400) / 3600;
    long long minutes = (total_seconds % 3600) / 60;
    long long seconds = total_seconds % 60;
    long long milliseconds = (ticks % 20) * 50;

    // Spacing inserted between number and unit when unit_spacing is on
    const char *sp = unit_spacing ? " " : "";

    // When unit_spacing is on, ms are always shown as a separate token (04 s 500 ms).
    // When unit_spacing is off, ms are embedded as decimal seconds (04.500s).
    bool show_ms_separate = always_show_ms || (years == 0 && days == 0 && hours == 0 && minutes == 0);

    if (years > 0) {
        snprintf(output, max_len, "%lld%sy %lld%sd %02lld%sh %02lld%sm %02lld%ss",
                 years, sp, days, sp, hours, sp, minutes, sp, seconds, sp);
    } else if (days > 0) {
        snprintf(output, max_len, "%lld%sd %02lld%sh %02lld%sm %02lld%ss",
                 days, sp, hours, sp, minutes, sp, seconds, sp);
    } else if (hours > 0) {
        if (show_ms_separate) {
            if (unit_spacing)
                snprintf(output, max_len, "%02lld h %02lld m %02lld s %03lld ms", hours, minutes, seconds, milliseconds);
            else
                snprintf(output, max_len, "%02lldh %02lldm %02lld.%03llds", hours, minutes, seconds, milliseconds);
        } else {
            snprintf(output, max_len, "%02lld%sh %02lld%sm %02lld%ss", hours, sp, minutes, sp, seconds, sp);
        }
    } else if (minutes > 0) {
        if (show_ms_separate) {
            if (unit_spacing)
                snprintf(output, max_len, "%02lld m %02lld s %03lld ms", minutes, seconds, milliseconds);
            else
                snprintf(output, max_len, "%02lldm %02lld.%03llds", minutes, seconds, milliseconds);
        } else {
            snprintf(output, max_len, "%02lld%sm %02lld%ss", minutes, sp, seconds, sp);
        }
    } else {
        if (unit_spacing)
            snprintf(output, max_len, "%02lld s %03lld ms", seconds, milliseconds);
        else
            snprintf(output, max_len, "%02lld.%03llds", seconds, milliseconds);
    }
}


void format_time_since_update(float total_seconds, char *output, size_t max_len) {
    if (!output || max_len == 0) return;

    int total_sec_int = (int) total_seconds;
    int hours = total_sec_int / 3600;
    int minutes = (total_sec_int % 3600) / 60;
    int seconds = total_sec_int % 60;

    if (hours > 0) {
        snprintf(output, max_len, "%dh %dm %ds ago", hours, minutes, seconds);
    } else if (minutes > 0) {
        snprintf(output, max_len, "%dm %ds ago", minutes, seconds);
    } else {
        snprintf(output, max_len, "%ds ago", seconds);
    }
}
