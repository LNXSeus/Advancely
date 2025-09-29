// Copyright (c) 2025 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 24.08.2025.
//

#include "logger.h"
#include "settings_utils.h" // Include the full header for the AppSettings definition
#include <cstdio>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#endif

// Global file pointer for the log file, only accessible within this file
static FILE *log_file = nullptr;

// A static pointer to the application settings
static const AppSettings *g_app_settings = nullptr;

void log_set_settings(const AppSettings *settings) {
    g_app_settings = settings;
}

void log_init() {
    // Open the log file in write mode to clear it on each startup
    log_file = fopen("advancely_log.txt", "w");
    if (log_file == nullptr) {
        fprintf(stderr, "CRITICAL: Failed to open log file advancely_log.txt\n");
        return;
    }

    // Add a timestamp to the log file
    const time_t now = time(nullptr);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(log_file, "Advancely Log - %s\n", time_buf);
    fprintf(log_file, "========================================\n\n");
    fflush(log_file); // Ensure the header is written immediately
}

void log_message(LogLevel level, const char *format, ...) {
    // If the message is informational, only proceed if debug printing is enabled.
    if (level == LOG_INFO && (g_app_settings == nullptr || !g_app_settings->print_debug_status)) {
        return;
    }

    // Buffer to hold the formatted message
    char message[4096];
    va_list args;

    // Format the message
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Print to the appropriate console stream
    if (level == LOG_ERROR) {
#ifdef _WIN32
        HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
        WORD saved_attributes;

        // Save current attributes
        GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
        saved_attributes = consoleInfo.wAttributes;

        // Set text color to red
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        fprintf(stderr, "%s", message);
        // Restore original attributes
        SetConsoleTextAttribute(hConsole, saved_attributes);
#else
        // Use ANSI escape codes for other platforms (Linux, macOS)
        fprintf(stderr, "\033[91m%s\033[0m", message);
#endif
    } else {
        printf("%s", message);
    }

    // Also write the message to the log file if it's open, with a timestamp
    if (log_file != nullptr) {
        char time_buf[16];
        time_t now = time(0);
        strftime(time_buf, sizeof(time_buf), "%H:%M:%S", localtime(&now));
        fprintf(log_file, "[%s] %s", time_buf, message);
        fflush(log_file); // Ensure log is written immediately in case of a crash
    }
}

void log_close(void) {
    if (log_file != nullptr) {
        fprintf(log_file, "\n========================================\n");
        fprintf(log_file, "Log finished.\n");
        fclose(log_file);
        log_file = nullptr;
    }
}
