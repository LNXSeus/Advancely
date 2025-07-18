//
// Created by Linus on 27.06.2025.
//

#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <stdbool.h>
#include <stddef.h>

#include "main.h"
#include "data_structures.h" // For MC_Version enum and PathMode

#define MAX_PATH_LENGTH 1024 // Also defined in main.h

/**
 * @brief Enum to determine how the saves path is obtained.
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
bool get_saves_path(char *out_path, size_t max_len, PathMode mode, const char* manual_path);

/**
 * @brief Finds the player's data files based on the Minecraft version.
 *
 * This function contains version-specific logic to locate the correct statistics
 * and advancement/achievement files.
 * - For MC < 1.7.2, it finds the global .dat stats file.
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
    char *out_world_name,
    char *out_adv_path,
    char *out_stats_path,
    char *out_unlocks_path,
    size_t max_len
);

#endif //PATH_UTILS_H
