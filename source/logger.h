//
// Created by Linus on 24.08.2025.
//

#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

/**
 * @brief Initializes the logging system.
 * Creates/overwrites advancely_log.txt and adds a timestamp.
 */
void log_init(void);

/**
 * @brief Logs a formatted message to the console and the log file.
 * This function is a wrapper around printf/fprintf and is variadic.
 * @param format The format string for the message.
 * @param ... The arguments for the format string.
 */
void log_message(const char *format, ...);

/**
 * @brief Closes the log file.
 * Should be called once at the end of the program.
 */
void log_close(void);

#ifdef __cplusplus
}
#endif

#endif //LOGGER_H