//
// Created by Linus on 27.06.2025.
//

#include "path_utils.h" // includes main.h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#endif

// LOCAL HELPER FUNCTIONS

/**
 * @brief Converts DOUBLE backslashes in a path to SINGLE forward slashes.
 * This is a local helper function to ensure consistent path formats internally.
 * @param path The path string to modify in-place.
 */
void normalize_path(char *path) {
    // static because it's only used in this file
    if (path == NULL) return; // In get_saves_path this function is only called on success (keeping it for safety)
    for (char *p = path; *p; p++) {
        // Convert backslash to forward slash
        if (*p == '\\') {
            *p = '/';
        }
    }
}

/**
 * @brief Automatically detects the default .minecraft saves path (platform-specific).
 * This is a local helper function that contains the OS-specific logic for finding
 * the Minecraft installation directory.
 * @param out_path A buffer to store the resulting path.
 * @param max_len The size of the out_path buffer.
 * @return true if the path was found, false otherwise.
 */
static bool get_auto_saves_path(char *out_path, size_t max_len) {
    #ifdef _WIN32
        PWSTR appdata_path_wide = NULL; // Wide string UTF-16

        // FOLDERID_RoamingAppData is modern equivalent to CSIDL_APPDATA
        HRESULT hr = SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &appdata_path_wide);

        if (SUCCEEDED(hr)) {
            // path is returned as wide string -> need to convert

            char appdata_path[MAX_PATH];
            size_t converted_chars = 0;
            wcstombs_s(&converted_chars, appdata_path, MAX_PATH, appdata_path_wide, MAX_PATH - 1);

            // Free memory allocated by Windows API
            CoTaskMemFree(appdata_path_wide);

            // Build final path
            snprintf(out_path, max_len, "%s/.minecraft/saves", appdata_path);
            return true;
        }
    #else
    // Linux
        const char *home_dir = getenv("HOME");
        if (!home_dir) {
            struct passwd *pw = getpwuid(getuid());
            if (pw) home_dir = pw->pw_dir;
        }

        if (home_dir) {
    #if __APPLE__ // macOS
                snprintf(out_path, max_len, "%s/Library/Application Support/minecraft/saves", home_dir);
    #else // Linux
                snprintf(out_path, max_len, "%s/.minecraft/saves", home_dir);
    #endif
                return true; // success path was found
            }
    #endif
        return false;
}

// PUBLIC FUNCTIONS

bool get_saves_path(char *out_path, size_t max_len, PathMode mode, const char *manual_path) {
    if (!out_path || max_len == 0) return false;

    bool success = false;
    if (mode == PATH_MODE_AUTO) {
        success = get_auto_saves_path(out_path, max_len);
    } else if (mode == PATH_MODE_MANUAL) {
        if (manual_path && strlen(manual_path) > 0) {
            strncpy(out_path, manual_path, max_len - 1);
            out_path[max_len - 1] = '\0'; // Ensure null-termination
            success = true;
        } else {
            fprintf(stderr, "[PATH UTILS] Manual path is empty or invalid.\n");
        }
    }

    // normalize path
    if (success) {
        normalize_path(out_path);
    } else {
        out_path[0] = '\0';
    }

    return success;
}

#ifdef _WIN32
void find_latest_world_files(
    const char *saves_path,
    char *out_adv_path,
    char *out_stats_path,
    char *out_unlocks_path,
    size_t max_len,
    bool use_advancements,
    bool use_unlocks) {

    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s/*", saves_path); // Search for files in saves directory

    WIN32_FIND_DATAA find_world_data;
    HANDLE h_find_world = FindFirstFileA(search_path, &find_world_data);
    if (h_find_world == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[PATH UTILS] Cannot find files in saves directory: %s. Error: %lu\n", saves_path, GetLastError());
        return;
    }

    FILETIME latest_time = {0, 0};
    char latest_world_name[MAX_PATH_LENGTH] = {0}; // Buffer to store the name of the latest world

    // Find the most recently modified world
    do {
        // continue while there are more files
        // "." ensures directory is not current directory and ".." ensures directory is not parent directory
        if ((find_world_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(find_world_data.cFileName, ".") != 0
            && strcmp(find_world_data.cFileName, "..") != 0) {
            // Compares last write time with latest one found -> updates latest
            if (CompareFileTime(&find_world_data.ftLastWriteTime, &latest_time) > 0) {
                latest_time = find_world_data.ftLastWriteTime;
                // copy directory name into latest_world_name buffer
                strncpy(latest_world_name, find_world_data.cFileName, MAX_PATH_LENGTH - 1); // TODO: Display this on tracker screen
            }
        }
    } while (FindNextFileA(h_find_world, &find_world_data) != 0);
    FindClose(h_find_world);

    if (strlen(latest_world_name) == 0) {
        fprintf(stderr, "[PATH UTILS] No World directories found in saves path: %s\n", saves_path);
        out_adv_path[0] = '\0';
        out_stats_path[0] = '\0';
        return;
    }
    printf("[PATH UTILS] Found latest world: %s\n", latest_world_name);

    WIN32_FIND_DATAA find_file_data;
    HANDLE h_find_file;
    char temp_path[MAX_PATH_LENGTH];
    char sub_search_path[MAX_PATH_LENGTH];

    // VERSION BASED LOGIC

    // ADVANCEMENTS SEARCH
    if (use_advancements) {
        snprintf(temp_path, max_len, "%s/%s/advancements", saves_path, latest_world_name);
        normalize_path(temp_path);
        snprintf(sub_search_path, sizeof(sub_search_path), "%s/*.json", temp_path); // search for any .json files
        h_find_file = FindFirstFileA(sub_search_path, &find_file_data);
        if (h_find_file != INVALID_HANDLE_VALUE) { // if file was found
            snprintf(out_adv_path, max_len, "%s/%s", temp_path, find_file_data.cFileName);
            // printf("[PATH UTILS] Set advancements path to: %s\n", out_adv_path);
            FindClose(h_find_file); // Close the file handle
        } else { out_adv_path[0] = '\0'; }
    } else { out_adv_path[0] = '\0'; }

    // UNLOCKS SEARCH for 25w14craftmine
    if (use_unlocks) {
        snprintf(temp_path, max_len, "%s/%s/unlocks", saves_path, latest_world_name);
        normalize_path(temp_path);
        snprintf(sub_search_path, sizeof(sub_search_path), "%s/*.json", temp_path); // search for any .json files in temp path
        h_find_file = FindFirstFileA(sub_search_path, &find_file_data);
        if (h_find_file != INVALID_HANDLE_VALUE) {
            snprintf(out_unlocks_path, max_len, "%s/%s", temp_path, find_file_data.cFileName);
            // printf("[PATH UTILS] Set unlocks path to: %s\n", out_unlocks_path);
            FindClose(h_find_file); // Close the file handle
        } else { out_unlocks_path[0] = '\0'; }
    } else { out_unlocks_path[0] = '\0'; }

    // STATS SEARCH (ALWAYS USED)
    snprintf(temp_path, max_len, "%s/%s/stats", saves_path, latest_world_name);
    normalize_path(temp_path);
    snprintf(sub_search_path, sizeof(sub_search_path), "%s/*.json", temp_path); // search for any .json files in temp path
    h_find_file = FindFirstFileA(sub_search_path, &find_file_data);
    if (h_find_file != INVALID_HANDLE_VALUE) {
        snprintf(out_stats_path, max_len, "%s/%s", temp_path, find_file_data.cFileName);
        // printf("[PATH UTILS] Set stats path to: %s\n", out_stats_path);
        FindClose(h_find_file); // Close the file handle
    } else { out_stats_path[0] = '\0'; }
}
#else // LINUX/MAC
void find_latest_world_files(
    const char *saves_path,
    char *out_adv_path,
    char *out_stats_path,
    char *out_unlocks_path,
    size_t max_len,
    bool use_advancements,
    bool use_unlocks
) {
    DIR *saves_dir = opendir(saves_path);
    if (!saves_dir) {
        fprintf(stderr, "[PATH UTILS] Cannot open saves directory: %s\n", saves_path);
        return;
    }

    struct dirent *entry;
    char latest_world_name[MAX_PATH_LEN] = {0};
    time_t latest_time = 0;
    while ((entry = readdir(saves_dir)) != NULL) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char world_path[MAX_PATH_LEN];
            snprintf(world_path, sizeof(world_path), "%s/%s", saves_path, entry->d_name);
            struct stat stat_buf;
            if (stat(world_path, &stat_buf) == 0) {
                if (stat_buf.st_mtime > latest_time) {
                    latest_time = stat_buf.st_mtime;
                    strncpy(latest_world_name, entry->d_name, sizeof(latest_world_name) - 1);
                }
            }
        }
    }
    closedir(saves_dir);

    if (strlen(latest_world_name) == 0) {
        fprintf(stderr, "[PATH UTILS] No world directories found in saves path: %s\n", saves_path);
        out_adv_path[0] = '\0';
        out_stats_path[0] = '\0';
        out_unlocks_path[0] = '\0';
        return;
    }
    printf("[PATH UTILS] Found latest world: %s\n", latest_world_name);

    char folder_path[MAX_PATH_LEN];

    // --- Advancements Search ---
    if (use_advancements) {
        snprintf(folder_path, sizeof(folder_path), "%s/%s/advancements", saves_path, latest_world_name);
        DIR *dir = opendir(folder_path);
        if (dir) {
            while ((entry = readdir(dir)) != NULL) {
                if (strstr(entry->d_name, ".json")) {
                    snprintf(out_adv_path, max_len, "%s/%s", folder_path, entry->d_name);
                    printf("[PATH UTILS] Set advancements path to: %s\n", out_adv_path);
                    break;
                }
            }
            closedir(dir);
        } else { out_adv_path[0] = '\0'; }
    } else { out_adv_path[0] = '\0'; }

    // --- Unlocks Search ---
    if (use_unlocks) {
        snprintf(folder_path, sizeof(folder_path), "%s/%s/unlocks", saves_path, latest_world_name);
        DIR *dir = opendir(folder_path);
        if (dir) {
            while ((entry = readdir(dir)) != NULL) {
                if (strstr(entry->d_name, ".json")) {
                    snprintf(out_unlocks_path, max_len, "%s/%s", folder_path, entry->d_name);
                    printf("[PATH UTILS] Set unlocks path to: %s\n", out_unlocks_path);
                    break;
                }
            }
            closedir(dir);
        } else { out_unlocks_path[0] = '\0'; }
    } else { out_unlocks_path[0] = '\0'; }

    // --- Stats Search ---
    snprintf(folder_path, sizeof(folder_path), "%s/%s/stats", saves_path, latest_world_name);
    DIR *dir = opendir(folder_path);
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (strstr(entry->d_name, ".json")) {
                snprintf(out_stats_path, max_len, "%s/%s", folder_path, entry->d_name);
                printf("[PATH UTILS] Set stats path to: %s\n", out_stats_path);
                break;
            }
        }
        closedir(dir);
    } else { out_stats_path[0] = '\0'; }
}
#endif
