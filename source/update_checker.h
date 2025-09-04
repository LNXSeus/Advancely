//
// Created by Linus on 03.09.2025.
//

#ifndef UPDATE_CHECKER_H
#define UPDATE_CHECKER_H

#include <cstddef> // For size_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Helper function to recursively delete a directory and its contents.
 * @param path The path to the directory to delete.
 */
void delete_directory_recursively(const char* path);

/**
 * @brief Checks GitHub for the latest release of Advancely.
 *
 * Compares the current application version against the latest release tag on GitHub.
 * If a new version is found, it populates the output parameters with the new
 * version string and the direct download URL for the release asset (zip file).
 *
 * @param current_version The version string of the running application (e.g., "v0.9.53").
 * @param out_latest_version A buffer to store the latest version string from GitHub.
 * @param max_len The size of the out_latest_version buffer.
 * @param out_download_url A buffer to store the asset download URL.
 * @param url_max_len The size of the out_download_url buffer.
 * @return true if a new version is available, false otherwise or on error.
 */
bool check_for_updates(const char* current_version, char* out_latest_version, size_t max_len, char* out_download_url, size_t url_max_len);

/**
* @brief Downloads the update zip file from a given URL.
*
* Uses curl to download the file and save it as "update.zip" in the application's
* root directory.
*
* @param url The direct URL to the .zip release asset.
* @return true on successful download, false otherwise.
*/
bool download_update_zip(const char* url);

/**
* @brief Applies the downloaded and extracted update.
*
* This function will:
* 1. Scan the "update_temp" directory.
* 2. Copy new and modified files to the application's root directory.
* 3. Skip user-specific files like settings.json and *_notes.txt.
* 4. Exit replace the .exe and other files and relaunch the application.
*
* @param main_executable_path The path to the currently running executable.
* @return true if the application should restart, false on error.
*/
bool apply_update(const char* main_executable_path);

#ifdef __cplusplus
}
#endif

#endif //UPDATE_CHECKER_H
