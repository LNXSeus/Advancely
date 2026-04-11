// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 27.06.2025.
//

#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <cstddef>
#include "main.h" // For MAX_PATH_LENGTH
#include "data_structures.h" // For MC_Version enum and PathMode enum

#ifdef __cplusplus
extern "C" {
#endif

struct AppSettings;

/**
* @brief Converts backslashes in a path to forward slashes for consistency.
* @param path The path string to modify in-place.
*/
void normalize_path(char *path);

/**
 * @brief Gets the final, normalized path to the .minecraft saves folder based on the selected mode.
 *
 * This function acts as the single point of truth for determining the saves path.
 * It can either automatically detect the path or use a manually provided one.
 * It also normalizes the path to use forward slashes for cross-platform consistency.
 *
 * @param out_path A buffer to store the resulting, normalized path.
 * @param max_len The size of the out_path buffer.
 * @param mode The detection mode (auto or manual).
 * @param manual_path The path to use if mode is PATH_MODE_MANUAL. Can be NULL if mode is auto.
 * @return True if a valid path was found and stored, false otherwise.
 */
bool get_saves_path(char *out_path, size_t max_len, PathMode mode, const char *manual_path);

/**
 * @brief Finds the player's data files based on the Minecraft version.
 *
 * This function contains version-specific logic to locate the correct statistics
 * and advancement/achievement files.
 * - For MC < 1.7.2:
 *   -´If StatsPerWorld mod is enabled: It looks for per-world .dat stats file.
 *   - If StatsPerWorld mod is disabled: it finds the global .dat stats file.
 * - For MC 1.7.2-1.11.2, it finds the per-world stats JSON containing achievements.
 * - For MC 1.12+, it finds the separate per-world advancements and stats JSONs.
 *
 * @param saves_path The full path to the .minecraft/saves directory.
 * @param version The Minecraft version from the MC_Version enum.
 * @param out_world_name A buffer to store the name of the latest world (or "Global").
 * @param out_adv_path A buffer to store the path to the advancements/achievements file.
 * @param out_stats_path A buffer to store the path to the stats file.
 * @param out_unlocks_path A buffer to store the path to the unlocks JSON file.
 * @param max_len The size of the output path buffers.
 */
void find_player_data_files(
    const char *saves_path,
    MC_Version version,
    bool use_stats_per_world_mod, // When StatsPerWorld mod is enabled for legacy versions
    const struct AppSettings *settings, // For debug prints
    char *out_world_name,
    char *out_adv_path,
    char *out_stats_path,
    char *out_unlocks_path,
    size_t max_len
);

/**
 * @brief Finds player data files for a specific player by UUID/username in co-op mode.
 *
 * Unlike find_player_data_files() which finds the first file via glob, this function
 * constructs deterministic paths for a specific player. The world name must already
 * be known (determined by the host's initial scan).
 *
 * - For MC 1.7+: stats/advancements are <uuid>.json
 * - For MC <= 1.6.4: stats are stats_<username_lowercase>_unsent.dat
 * - For 25w14craftmine: unlocks are <uuid>.json in unlocks/
 *
 * @param saves_path The full path to the .minecraft/saves directory.
 * @param version The Minecraft version enum.
 * @param use_stats_per_world_mod Whether the StatsPerWorld mod is in use (legacy only).
 * @param world_name The already-determined world name (from host's initial scan).
 * @param uuid The player's UUID (with hyphens, e.g., "069a79f4-44e9-4726-a5be-fca90e38aaf5").
 * @param username The player's Minecraft username (used for legacy .dat filenames).
 * @param out_adv_path Buffer for the advancements file path.
 * @param out_stats_path Buffer for the stats file path.
 * @param out_unlocks_path Buffer for the unlocks file path.
 * @param max_len The size of the output path buffers.
 */
void find_player_data_files_for_uuid(
    const char *saves_path,
    MC_Version version,
    bool use_stats_per_world_mod,
    const char *world_name,
    const char *uuid,
    const char *username,
    char *out_adv_path,
    char *out_stats_path,
    char *out_unlocks_path,
    size_t max_len
);

/**
* @brief Checks if a given file or directory path exists.
* @param path The path to check.
* @return true if the path exists, false otherwise.
*/
bool path_exists(const char *path);

/**
* @brief Gets a parent directory path by navigating up a specified number of levels.
* This is used to get to the instance path from the saves path.
* @param original_path The starting path.
* @param out_path A buffer to store the resulting parent path.
* @param max_len The size of the out_path buffer.
* @param levels The number of directory levels to go up.
* @return true if the operation was successful, false otherwise.
*/
bool get_parent_directory(const char *original_path, char *out_path, size_t max_len, int levels);

/**
* @brief Converts a path with forward slashes to a native Windows path with backslashes, in-place.
* Used for "Open instance folder" button.
* @param path The path string to modify.
*/
void path_to_windows_native(char *path);

/**
* @brief Gets the full absolute path of the currently running executable.
* This function is cross-platform.
* @param out_path A buffer to store the resulting path.
* @param max_len The size of the out_path buffer.
* @return true on success, false on failure.
*/
bool get_executable_path(char *out_path, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif //PATH_UTILS_H
