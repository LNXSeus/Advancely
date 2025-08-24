//
// Created by Linus on 24.08.2025.
//

#include "logger.h"
#include <cstdio>
#include <ctime>

// Global file pointer for the log file, only accessible within this file
static FILE *log_file = nullptr;

void log_init(void) {
    // Open the log file in write mode to clear it on each startup
    log_file = fopen("advancely_log.txt", "w");
    if (log_file == nullptr) {
        fprintf(stderr, "CRITICAL: Failed to open log file advancely_log.txt\n");
        return;
    }

    // Add a timestamp to the log file
    time_t now = time(0);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(log_file, "Advancely Log - %s\n", time_buf);
    fprintf(log_file, "========================================\n\n");
    fflush(log_file); // Ensure the header is written immediately
}

void log_message(const char *format, ...) {
    // Buffer to hold the formatted message
    char message[4096];
    va_list args;

    // Format the message
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Print the message to the standard console (stdout)
    printf("%s", message);

    // Also write the message to the log file if it's open
    if (log_file != nullptr) {
        fprintf(log_file, "%s", message);
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