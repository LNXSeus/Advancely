//
// Created by Linus on 27.06.2025.
//

#include "path_utils.h" // includes main.h
#include <cstdio>
#include <cstdlib>
#include <cstring>


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
    if (path == nullptr) return; // In get_saves_path this function is only called on success (keeping it for safety)
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
    PWSTR appdata_path_wide = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata_path_wide))) {
        char appdata_path[MAX_PATH];
        wcstombs(appdata_path, appdata_path_wide, MAX_PATH);
        CoTaskMemFree(appdata_path_wide);
        snprintf(out_path, max_len, "%s/.minecraft/saves", appdata_path);
        return true;
    }
#else
    const char *home_dir = getenv("HOME");
    if (!home_dir) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home_dir = pw->pw_dir;
    }
    if (home_dir) {
#if __APPLE__
        snprintf(out_path, max_len, "%s/Library/Application Support/minecraft/saves", home_dir);
#else
        snprintf(out_path, max_len, "%s/.minecraft/saves", home_dir);
#endif
        return true;
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
            out_path[max_len - 1] = '\0'; // Ensure nullptr-termination
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
void find_player_data_files(
    const char *saves_path,
    MC_Version version,
    char *out_world_name,
    char *out_adv_path,
    char *out_stats_path,
    char *out_unlocks_path,
    size_t max_len) {
    // Clear output paths initially
    out_world_name[0] = '\0';
    out_adv_path[0] = '\0';
    out_stats_path[0] = '\0';
    out_unlocks_path[0] = '\0';

    // Always find the most recently modified world directoy first
    char latest_world_name[MAX_PATH_LENGTH] = {0};
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s/*", saves_path);

    WIN32_FIND_DATAA find_world_data;
    HANDLE h_find_world = FindFirstFileA(search_path, &find_world_data);
    if (h_find_world == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[PATH UTILS] Cannot find files in saves directory: %s/n", saves_path);
        return;
    }
    FILETIME latest_time = {0, 0};
    do {
        // Find the latest world
        if ((find_world_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(find_world_data.cFileName, ".") != 0
            && strcmp(find_world_data.cFileName, "..") != 0) {
            if (CompareFileTime(&find_world_data.ftLastWriteTime, &latest_time) > 0) {
                latest_time = find_world_data.ftLastWriteTime;
                strncpy(latest_world_name, find_world_data.cFileName, sizeof(latest_world_name) - 1);
            }
        }
    } while (FindNextFileA(h_find_world, &find_world_data) != 0);
    FindClose(h_find_world);

    if (strlen(latest_world_name) > 0) {
        strncpy(out_world_name, latest_world_name, max_len - 1);
        printf("[PATH UTILS] Found latest world: %s\n", out_world_name);
    } else {
        // Even if no worlds are found, we might need to find a global legacy file.
        // Set a placeholder name for display.
        strncpy(out_world_name, "No Worlds Found", max_len - 1);
    }

    // --- Era 1: Legacy Stats (MC 1.0 - 1.6.4) ---
    if (version <= MC_VERSION_1_6_4) {
        // Legacy: Stats are global. World name is still the latest world for snapshotting.
        strncpy(out_world_name, latest_world_name, max_len - 1);
        printf("[PATH UTILS} Legacy version detected. Using latest world for snapshot: %s\n", out_world_name);

        char mc_root_path[MAX_PATH_LENGTH];
        strncpy(mc_root_path, saves_path, sizeof(mc_root_path) - 1);

        // Navigate up from "/saves" to get the ".minecraft" root
        char *last_slash = strrchr(mc_root_path, '/');
        if (last_slash) *last_slash = '\0'; // Remove the last slash

        char stats_search_path[MAX_PATH_LENGTH];
        snprintf(stats_search_path, sizeof(stats_search_path), "%s/stats/*.dat", mc_root_path);

        WIN32_FIND_DATAA find_file_data;
        HANDLE h_find_file = FindFirstFileA(stats_search_path, &find_file_data);
        if (h_find_file != INVALID_HANDLE_VALUE) {
            snprintf(out_stats_path, max_len, "%s/stats/%s", mc_root_path, find_file_data.cFileName);
            printf("[PATH UTILS] Found legacy global stats file: %s\n", out_stats_path);
            FindClose(h_find_file);
        }
    } else {
        // Modern & Mid-era: Stats are per-world.
        if (strlen(latest_world_name) == 0) {
            fprintf(stderr, "[PATH UTILS] Could not find legacy stats .dat file in %s/stats/\n", saves_path);
            return;
        }

        // Copy the found world name to the output buffer
        strncpy(out_world_name, latest_world_name, max_len - 1);
        out_world_name[max_len - 1] = '\0'; // Ensure nullptr-termination
        printf("[PATH UTILS] Found latest world: %s\n", out_world_name);

        char temp_path[MAX_PATH_LENGTH];
        char sub_search_path[MAX_PATH_LENGTH];
        WIN32_FIND_DATAA find_file_data;
        HANDLE h_find_file;

        // Find stats file for both mid and modern eras

        snprintf(temp_path, max_len, "%s/%s/stats", saves_path, latest_world_name);
        normalize_path(temp_path);
        snprintf(sub_search_path, sizeof(sub_search_path), "%s/*.json", temp_path);
        normalize_path(sub_search_path);
        h_find_file = FindFirstFileA(sub_search_path, &find_file_data);
        if (h_find_file != INVALID_HANDLE_VALUE) {
            snprintf(out_stats_path, max_len, "%s/%s", temp_path, find_file_data.cFileName);
            normalize_path(out_stats_path);
            printf("[PATH UTILS] Found mid/modern era stats file: %s\n", out_stats_path);
            FindClose(h_find_file);
        }

        // Era 3: Modern Advancements/Unlocks (MC 1.12+)
        if (version >= MC_VERSION_1_12) {
            // Advancements search
            snprintf(temp_path, max_len, "%s/%s/advancements", saves_path, latest_world_name);
            normalize_path(temp_path);
            snprintf(sub_search_path, sizeof(sub_search_path), "%s/*.json", temp_path);
            normalize_path(sub_search_path);
            // search for any .json files
            h_find_file = FindFirstFileA(sub_search_path, &find_file_data);
            if (h_find_file != INVALID_HANDLE_VALUE) {
                // if file was found
                snprintf(out_adv_path, max_len, "%s/%s", temp_path, find_file_data.cFileName);
                normalize_path(out_adv_path);
                // printf("[PATH UTILS] Set advancements path to: %s\n", out_adv_path);
                FindClose(h_find_file); // Close the file handle
            }

            // UNLOCKS SEARCH for 25w14craftmine
            if (version == MC_VERSION_25W14CRAFTMINE) {
                snprintf(temp_path, max_len, "%s/%s/unlocks", saves_path, latest_world_name);
                normalize_path(temp_path);
                snprintf(sub_search_path, sizeof(sub_search_path), "%s/*.json", temp_path);
                normalize_path(sub_search_path);
                // search for any .json files in temp path
                h_find_file = FindFirstFileA(sub_search_path, &find_file_data);
                if (h_find_file != INVALID_HANDLE_VALUE) {
                    snprintf(out_unlocks_path, max_len, "%s/%s", temp_path, find_file_data.cFileName);
                    normalize_path(out_unlocks_path);
                    // printf("[PATH UTILS] Set unlocks path to: %s\n", out_unlocks_path);
                    FindClose(h_find_file); // Close the file handle
                }
            }
        }
    }
}
#else // LINUX/MAC
void find_player_data_files(
    const char *saves_path,
    MC_Version version,
    char *out_world_name,
    char *out_adv_path,
    char *out_stats_path,
    char *out_unlocks_path,
    size_t max_len
) {
    // Clear output paths initially
    out_world_name[0] = '\0';
    out_adv_path[0] = '\0';
    out_stats_path[0] = '\0';
    out_unlocks_path[0] = '\0';

    // Always find the most recently modified world directoy first
    char latest_world_name[MAX_PATH_LENGTH] = {0};
    DIR *saves_dir = opendir(saves_path);
    if (!saves_dir) {
        fprintf(stderr, "[PATH UTILS] Cannot open saves directoRy: %s\n", saves_path);
        return;
    }
    struct dirent *entry;
    time_t latest_time = 0;
    while ((entry = readdir(saves_dir)) != nullptr) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char world_path[MAX_PATH_LENGTH];
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

    if (strlen(latest_world_name) > 0) {
        strncpy(out_world_name, latest_world_name, max_len - 1);
        fprintf(stderr, "[PATH UTILS] No world directories found in saves path: %s\n", saves_path);
    }

// Step 2: Based on version, determine where to look for files.
    if (version <= MC_VERSION_1_6_4) {
        char mc_root_path[MAX_PATH_LENGTH];
        strncpy(mc_root_path, saves_path, sizeof(mc_root_path) - 1);
        char *last_slash = strrchr(mc_root_path, '/');
        if (last_slash) *last_slash = '\0';

        char stats_path_dir[MAX_PATH_LENGTH];
        snprintf(stats_path_dir, sizeof(stats_path_dir), "%s/stats", mc_root_path);
        DIR *stats_dir = opendir(stats_path_dir);
        if (stats_dir) {
            while ((entry = readdir(stats_dir)) != nullptr) {
                if (strstr(entry->d_name, ".dat")) {
                    snprintf(out_stats_path, max_len, "%s/%s", stats_path_dir, entry->d_name);
                    break;
                }
            }
            closedir(stats_dir);
        }
    } else {
        if (strlen(latest_world_name) == 0) {
            fprintf(stderr, "[PATH UTILS] No world directories found in saves path: %s\n", saves_path);
            return;
        }

        char folder_path[MAX_PATH_LENGTH];

        // Find stats file
        snprintf(folder_path, sizeof(folder_path), "%s/%s/stats", saves_path, latest_world_name);
        DIR *dir = opendir(folder_path);
        if (dir) {
            while ((entry = readdir(dir)) != nullptr) {
                if (strstr(entry->d_name, ".json")) {
                    snprintf(out_stats_path, max_len, "%s/%s", folder_path, entry->d_name);
                    break;
                }
            }
            closedir(dir);
        }

        // Find advancements/unlocks for modern era
        if (version >= MC_VERSION_1_12) {
            snprintf(folder_path, sizeof(folder_path), "%s/%s/advancements", saves_path, latest_world_name);
            dir = opendir(folder_path);
            if (dir) {
                while ((entry = readdir(dir)) != nullptr) {
                    if (strstr(entry->d_name, ".json")) {
                        snprintf(out_adv_path, max_len, "%s/%s", folder_path, entry->d_name);
                        break;
                    }
                }
                closedir(dir);
            }
            if (version == MC_VERSION_25W14CRAFTMINE) {
                snprintf(folder_path, sizeof(folder_path), "%s/%s/unlocks", saves_path, latest_world_name);
                dir = opendir(folder_path);
                if (dir) {
                    while ((entry = readdir(dir)) != nullptr) {
                        if (strstr(entry->d_name, ".json")) {
                            snprintf(out_unlocks_path, max_len, "%s/%s", folder_path, entry->d_name);
                            break;
                        }
                    }
                    closedir(dir);
                }
            }
        }
    }
}
#endif

bool path_exists(const char* path) {
#ifdef _WIN32
    DWORD file_attributes = GetFileAttributesA(path);
    return (file_attributes != INVALID_FILE_ATTRIBUTES);
#else
    struct stat buffer;
    return (stat(path, &buffer) == 0);
#endif
}
