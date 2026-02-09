// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, or use this software
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

void format_time(long long ticks, char *output, size_t max_len) {
    if (!output || max_len == 0) return;

    long long total_seconds = ticks / 20;
    long long days_total = total_seconds / 86400; // Total days first
    long long years = days_total / 365; // Simple year calculation
    long long days = days_total % 365; // Remaining days after years

    long long hours = (total_seconds % 86400) / 3600;
    long long minutes = (total_seconds % 3600) / 60;
    long long seconds = total_seconds % 60;
    long long milliseconds = (ticks % 20) * 50; // Milliseconds remain for precision at low durations

    // Build the string conditionally based on the largest non-zero unit
    if (years > 0) {
        snprintf(output, max_len, "%lldy %lldd %02lldh %02lldm %02llds", years, days, hours, minutes, seconds);
    } else if (days > 0) {
        snprintf(output, max_len, "%lldd %02lldh %02lldm %02llds", days, hours, minutes, seconds);
    } else if (hours > 0) {
        snprintf(output, max_len, "%02lldh %02lldm %02llds", hours, minutes, seconds);
    } else if (minutes > 0) {
        snprintf(output, max_len, "%02lldm %02llds", minutes, seconds);
    } else {
        // Only show milliseconds if total time is less than a minute
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
