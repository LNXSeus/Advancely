// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 24.08.2025.
//

#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

// Enum to specify the type of log message
typedef enum {
    LOG_INFO,
    LOG_ERROR
} LogLevel;

/**
* @brief Gives the logger a pointer to the main application settings.
* This is used to check if debug messages should be printed.
* @param settings A pointer to the AppSettings struct.
*/
void log_set_settings(const struct AppSettings *settings);

/**
 * @brief Initializes the logging system.
 * Creates/overwrites the log file and adds a timestamp.
 * @param is_overlay_process If true, logs to "advancely_overlay_log.txt".
 * If false, logs to "advancely_log.txt".
 */
void log_init(bool is_overlay_process);

/**
 * @brief Logs a formatted message to the console and the log file.
 * This function is a wrapper around printf/fprintf and is variadic.
 * This function does not proceed if the message is only informational and debug printing is disabled.
 * @param level The logging level (LOG_INFO for stdout, LOG_ERROR for stderr).
 * @param format The format string for the message.
 * @param ... The arguments for the format string.
 */
void log_message(LogLevel level, const char *format, ...);

/**
 * @brief Closes the log file.
 * Should be called once at the end of the program.
 */
void log_close(void);

#ifdef __cplusplus
}
#endif

#endif //LOGGER_H
