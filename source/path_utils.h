//
// Created by Linus on 27.06.2025.
//

#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <stdbool.h>
#include <stddef.h>

#include "main.h"

#define MAX_PATH_LENGTH 1024 // Also defined in main.h

/**
 * @brief Enum to determine how the saves path is obtained.
 */
typedef enum {
    PATH_MODE_AUTO,   // Automatically detect the path from standard locations.
    PATH_MODE_MANUAL  // Use a user-provided path.
} PathMode;

/**
 * @brief Converts all backslashes in a path to forward slashes.
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
bool get_saves_path(char *out_path, size_t max_len, PathMode mode, const char* manual_path);

/**
 * @brief Finds the most recently modified world and gets its data file paths.
 *
 * Scans the provided Minecraft saves directory to find the world folder
 * that was modified last. It conditionally searches for advancements, unlocks,
 * and stats files based on the provided boolean flags.
 *
 * @param saves_path The full path to the .minecraft/saves directory.
 * @param out_adv_path A buffer to store the path to the advancements JSON file.
 * @param out_stats_path A buffer to store the path to the stats JSON file.
 * @param out_unlocks_path A buffer to store the path to the unlocks JSON file.
 * @param max_len The size of the output path buffers.
 * @param use_advancements Set to true for modern versions (1.12+).
 * @param use_unlocks Set to true for version 25w14craftmine.
 */
void find_latest_world_files(
    const char *saves_path,
    char *out_adv_path,
    char *out_stats_path,
    char *out_unlocks_path,
    size_t max_len,
    bool use_advancements,
    bool use_unlocks
);




#endif //PATH_UTILS_H
