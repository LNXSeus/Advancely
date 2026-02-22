// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 14.09.2025.
//

#ifndef DIALOG_UTILS_H
#define DIALOG_UTILS_H

#include <cstddef>

bool open_icon_file_dialog(char *out_relative_path, size_t max_len);

bool open_font_file_dialog(char *out_filename, size_t max_len);

/**
 * @brief Opens a file dialog for selecting GUI background textures (.png, .gif).
 * Starts in the 'resources/gui/' directory. Files outside this directory are copied in automatically.
 * @param out_relative_path Buffer to store the texture filename (relative to resources/gui/).
 * @param max_len Size of the output buffer.
 * @return true if a valid file was selected, false otherwise (cancel or invalid).
 */
bool open_gui_texture_dialog(char *out_relative_path, size_t max_len);

/**
 * @brief Opens a folder dialog for selecting a Minecraft saves directory.
 * Stores the full absolute path. Used for PATH_MODE_MANUAL.
 * @param out_path Buffer to store the selected folder path.
 * @param max_len Size of the output buffer.
 * @return true if a folder was selected, false if cancelled.
 */
bool open_saves_folder_dialog(char *out_path, size_t max_len);

/**
 * @brief Opens a folder dialog for selecting a specific world folder inside a saves directory.
 * Stores the full absolute path. Used for PATH_MODE_FIXED_WORLD.
 * @param out_path Buffer to store the selected folder path.
 * @param max_len Size of the output buffer.
 * @param saves_path Optional starting directory hint (the current saves path). May be NULL.
 * @return true if a folder was selected, false if cancelled.
 */
bool open_world_folder_dialog(char *out_path, size_t max_len, const char *saves_path);

#endif // DIALOG_UTILS_H
