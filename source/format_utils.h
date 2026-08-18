// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
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
 * SPECIAL CASE: If the underscore is the first character it will just be removed and
 * not replaced with a space.The letter is still capitalized after.
 * @param input The source string.
 * @param output The buffer to write the formatted string to.
 * @param max_len The size of the output buffer.
 */
void format_category_string(const char *input, char *output, size_t max_len);

/**
 * @brief Formats a time in Minecraft ticks into a human-readable string.
 * @param ticks The total number of ticks (20 ticks per second).
 * @param output The buffer to write the formatted time string to.
 * @param max_len The size of the output buffer.
 * @param unit_spacing If true, a space is inserted before every unit suffix (e.g. "02 m 04 s").
 * @param always_show_ms If true, milliseconds are appended even when the time exceeds one minute.
 */
void format_time(long long ticks, char *output, size_t max_len, bool unit_spacing, bool always_show_ms);

/**
 * @brief Formats a time in milliseconds into the same human-readable string as format_time().
 *
 * Used for the exact final time reported by the SpeedrunIGT mod, which is millisecond-precise
 * instead of being quantized to the 50 ms of a Minecraft tick.
 *
 * @param millis The total number of milliseconds.
 * @param output The buffer to write the formatted time string to.
 * @param max_len The size of the output buffer.
 * @param unit_spacing If true, a space is inserted before every unit suffix (e.g. "02 m 04 s").
 * @param always_show_ms If true, milliseconds are appended even when the time exceeds one minute.
 */
void format_time_ms(long long millis, char *output, size_t max_len, bool unit_spacing, bool always_show_ms);

/**
 * @brief Formats an in-game time, preferring the SpeedrunIGT mod's millisecond-precise value.
 *
 * Every IGT Advancely displays goes through this. The tick count the game writes to the stats
 * file only resolves to 50 ms, so whenever SpeedrunIGT's record.json supplied a time it is used
 * instead and the ticks are just the fallback.
 *
 * @param ticks The fallback time in Minecraft ticks (20 ticks per second).
 * @param speedrunigt_ms The time from SpeedrunIGT's record.json in milliseconds, or 0 if unavailable.
 * @param output The buffer to write the formatted time string to.
 * @param max_len The size of the output buffer.
 * @param unit_spacing If true, a space is inserted before every unit suffix (e.g. "02 m 04 s").
 * @param always_show_ms If true, milliseconds are appended even when the time exceeds one minute.
 */
void format_igt(long long ticks, long long speedrunigt_ms, char *output, size_t max_len, bool unit_spacing,
                bool always_show_ms);

/**
 * @brief Formats a duration in seconds into a Hh Mm Ss string.
 * @param total_seconds The total number of seconds.
 * @param output The buffer to write the formatted time string to.
 * @param max_len The size of the output buffer.
 * @param unit_spacing If true, a space is inserted before every unit suffix (e.g. "1 h 2 m 3 s ago").
 */
void format_time_since_update(float total_seconds, char *output, size_t max_len, bool unit_spacing);

void trim_room_code(char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif //FORMAT_UTILS_H
