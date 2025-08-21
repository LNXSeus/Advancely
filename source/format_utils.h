//
// Created by Linus on 20.08.2025.
//

#ifndef FORMAT_UTILS_H
#define FORMAT_UTILS_H

#include <cstddef> // For size_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Formats a string like "acquire_hardware" into "Acquire Hardware".
 * It replaces underscores with spaces and capitalizes the first letter of each word.
 * @param input The source string.
 * @param output The buffer to write the formatted string to.
 * @param max_len The size of the output buffer.
 */
void format_category_string(const char *input, char *output, size_t max_len);

/**
 * @brief Formats a time in Minecraft ticks into a DD:HH:MM:SS.MS string.
 * @param ticks The total number of ticks (20 ticks per second).
 * @param output The buffer to write the formatted time string to.
 * @param max_len The size of the output buffer.
 */
void format_time(long long ticks, char *output, size_t max_len);

/**
 * @brief Formats a duration in seconds into a Hh Mm Ss string.
 * @param total_seconds The total number of seconds.
 * @param output The buffer to write the formatted time string to.
 * @param max_len The size of the output buffer.
 */
void format_time_since_update(float total_seconds, char *output, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif //FORMAT_UTILS_H
