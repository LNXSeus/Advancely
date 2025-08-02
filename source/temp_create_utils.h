//
// Created by Linus on 04.07.2025.
//

#ifndef TEMP_CREATE_UTILS_H
#define TEMP_CREATE_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Ensures that the directory for a given file path exists, creating it if necessary.
 * This function is cross-platform.
 * @param path The full path to a file, e.g., "resources/templates/1.21/my_cat/1_21_my_cat_flag.json".
 */
void fs_ensure_directory_exists(const char *path);

/**
 * @brief Creates a new, empty template JSON file with a basic skeleton.
 * @param path The full path where the file should be created.
 */
void fs_create_empty_template_file(const char *path);

/**
 * @brief Creates a new, empty language JSON file.
 * @param path The full path where the file should be created.
 */
void fs_create_empty_lang_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif //TEMP_CREATE_UTILS_H
