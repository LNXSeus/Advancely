// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 04.07.2025.
//

#include <cstdio>
#include <cstdlib> // For free() and system()
#include <cstring>
#include <sys/stat.h> // For stat and mkdir
#include <cctype> // For isalnum
#include <functional>
#include <unordered_set>
#include <unordered_map>

#include "temp_creator_utils.h"
#include "logger.h"
#include "file_utils.h"
#include "path_utils.h" // For path_exists and get_parent_directory
#include "main.h" // For MAX_PATH_LENGTH
#include "template_scanner.h"
#include "file_utils.h" // For cJSON_from_file
#include "settings_utils.h" // For version checking

#include "tinyfiledialogs.h"
#define MINIZ_HEADER_FILE_ONLY
#include <algorithm>

#include "miniz.h"

#ifdef _WIN32
#include <direct.h> // For _mkdir
#include <windows.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0755) // 0755 provides read/write/execute for owner, read/execute for others
#include <dirent.h>
#include <unistd.h> // For rmdir()
#include <sys/wait.h>
#endif

bool parse_player_stats_for_import(const char *file_path, MC_Version version, std::vector<ImportableStat> &out_stats,
                                   char *error_message, size_t error_msg_size) {
    out_stats.clear();

    cJSON *root = cJSON_from_file(file_path);
    if (!root) {
        snprintf(error_message, error_msg_size, "Error: Could not read or parse the selected JSON file.");
        return false;
    }

    if (version <= MC_VERSION_1_6_4) {
        // Legacy .dat file (parsed as json)
        cJSON *stats_change = cJSON_GetObjectItem(root, "stats-change");
        if (cJSON_IsArray(stats_change)) {
            cJSON *stat_entry;
            cJSON_ArrayForEach(stat_entry, stats_change) {
                cJSON *item = stat_entry->child;
                // Legacy stats can also be achievements
                if (item && item->string) {
                    out_stats.push_back({item->string, false});
                }
            }
        }
    } else if (version <= MC_VERSION_1_12_2) {
        // Mid-era stats until 1.12.2
        // Mid-era flat json file
        cJSON *stat_entry = nullptr;
        cJSON_ArrayForEach(stat_entry, root) {
            // Only import entries that are simple numbers, excluding complex objects (achievements with criteria).
            if (stat_entry->string && cJSON_IsNumber(stat_entry)) {
                out_stats.push_back({stat_entry->string, false});
            }
        }
    } else {
        // Modern nested json file (1.13+)
        cJSON *stats_obj = cJSON_GetObjectItem(root, "stats");
        if (stats_obj) {
            cJSON *category_obj = nullptr;
            cJSON_ArrayForEach(category_obj, stats_obj) {
                if (category_obj->string) {
                    cJSON *stat_entry = nullptr;
                    cJSON_ArrayForEach(stat_entry, category_obj) {
                        if (stat_entry->string) {
                            char full_root_name[256];
                            snprintf(full_root_name, sizeof(full_root_name), "%s/%s", category_obj->string,
                                     stat_entry->string);
                            out_stats.push_back({full_root_name, false});
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    if (out_stats.empty()) {
        snprintf(error_message, error_msg_size, "No parsable stats found in the selected file.");
        return false;
    }
    return true;
}

bool parse_player_advancements_for_import(const char *file_path, MC_Version version,
                                          std::vector<ImportableAdvancement> &out_advancements,
                                          char *error_message, size_t error_msg_size) {
    out_advancements.clear();

    cJSON *root = cJSON_from_file(file_path);
    if (!root) {
        snprintf(error_message, error_msg_size, "Error: Could not read or parse the selected JSON file.");
        return false;
    }

    if (version <= MC_VERSION_1_6_4) {
        // --- Legacy .dat file parsing for achievements ---
        cJSON *stats_change = cJSON_GetObjectItem(root, "stats-change");
        if (cJSON_IsArray(stats_change)) {
            cJSON *stat_entry;
            cJSON_ArrayForEach(stat_entry, stats_change) {
                cJSON *item = stat_entry->child;
                // Legacy achievements start with ID 5242880. We check if the first character is '5'.
                if (item && item->string && item->string[0] == '5') {
                    ImportableAdvancement new_adv;
                    new_adv.root_name = item->string;
                    new_adv.is_done = true; // Any entry in the file means it's completed.
                    out_advancements.push_back(new_adv);
                }
            }
        }
    } else if (version <= MC_VERSION_1_11_2) {
        // --- Mid-era stats file parsing for ALL achievements ---
        cJSON *achievement_json = nullptr;
        cJSON_ArrayForEach(achievement_json, root) {
            // We only care about entries that are actual achievements.
            if (!achievement_json->string || strncmp(achievement_json->string, "achievement.", 12) != 0) {
                continue;
            }

            ImportableAdvancement new_adv;
            new_adv.root_name = achievement_json->string;
            new_adv.is_done = false; // Default state

            // Check if the achievement is simple (a number) or complex (an object).
            if (cJSON_IsNumber(achievement_json)) {
                // Simple achievement, e.g., "achievement.buildHoe": 1. It has no criteria.
                new_adv.is_done = true; // A number value indicates it's completed.
            } else if (cJSON_IsObject(achievement_json)) {
                // Complex achievement with criteria, e.g., "achievement.exploreAllBiomes".
                cJSON *progress_array = cJSON_GetObjectItem(achievement_json, "progress");
                if (cJSON_IsArray(progress_array)) {
                    cJSON *criterion_json = nullptr;
                    cJSON_ArrayForEach(criterion_json, progress_array) {
                        if (cJSON_IsString(criterion_json)) {
                            new_adv.criteria.push_back({criterion_json->valuestring, false});
                        }
                    }
                }
            }
            out_advancements.push_back(new_adv);
        }
    } else {
        // --- Modern advancements file parsing (existing logic) ---
        cJSON *advancement_json = nullptr;
        cJSON_ArrayForEach(advancement_json, root) {
            if (!advancement_json->string) continue;

            if (strcmp(advancement_json->string, "DataVersion") == 0) {
                continue;
            }

            ImportableAdvancement new_adv;
            new_adv.root_name = advancement_json->string;

            cJSON *done_item = cJSON_GetObjectItem(advancement_json, "done");
            new_adv.is_done = cJSON_IsTrue(done_item);

            cJSON *criteria_obj = cJSON_GetObjectItem(advancement_json, "criteria");
            if (criteria_obj) {
                cJSON *criterion_json = nullptr;
                cJSON_ArrayForEach(criterion_json, criteria_obj) {
                    if (criterion_json->string) {
                        new_adv.criteria.push_back({criterion_json->string, false});
                    }
                }
            }
            out_advancements.push_back(new_adv);
        }
    }

    cJSON_Delete(root);
    return true;
}

bool parse_player_unlocks_for_import(const char *file_path, std::vector<ImportableUnlock> &out_unlocks,
                                     char *error_message, size_t error_msg_size) {
    out_unlocks.clear();

    cJSON *root = cJSON_from_file(file_path);
    if (!root) {
        snprintf(error_message, error_msg_size, "Error: Could not read or parse the selected JSON file.");
        return false;
    }

    cJSON *obtained_obj = cJSON_GetObjectItem(root, "obtained");
    if (cJSON_IsObject(obtained_obj)) {
        cJSON *unlock_entry = nullptr;
        cJSON_ArrayForEach(unlock_entry, obtained_obj) {
            if (unlock_entry->string) {
                out_unlocks.push_back({unlock_entry->string, false});
            }
        }
    }

    cJSON_Delete(root);
    if (out_unlocks.empty()) {
        snprintf(error_message, error_msg_size, "No parsable unlocks found in the selected file.");
        return false;
    }
    return true;
}

// Helper to convert version string "1.16.1" to "1_16_1" for filename construction
static void version_to_filename_format(const char *version_in, char *version_out, size_t max_len) {
    strncpy(version_out, version_in, max_len - 1);
    version_out[max_len - 1] = '\0';
    for (char *p = version_out; *p; p++) {
        if (*p == '.') *p = '_'; // Convert dots to underscores
    }
}

// Local helper to check if a directory is empty (ignoring '.' and '..')
static bool is_directory_empty(const char *path) {
#ifdef _WIN32
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s\\*", path);
    WIN32_FIND_DATAA find_data;
    HANDLE h_find = FindFirstFileA(search_path, &find_data);

    if (h_find == INVALID_HANDLE_VALUE) {
        return true; // Directory doesn't exist or is inaccessible, treat as empty
    }

    do {
        if (strcmp(find_data.cFileName, ".") != 0 && strcmp(find_data.cFileName, "..") != 0) {
            FindClose(h_find);
            return false; // Found a file or directory
        }
    } while (FindNextFileA(h_find, &find_data) != 0);

    FindClose(h_find);
    return true;
#else // POSIX
    DIR *dir = opendir(path);
    if (dir == nullptr) {
        return true; // Cannot open, assume empty or non-existent
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            closedir(dir);
            return false; // Found something
        }
    }

    closedir(dir);
    return true;
#endif
}

// Local helper to check for invalid filename characters
static bool is_valid_filename_part(const char *part) {
    if (!part) return true; // An empty flag is valid
    for (const char *p = part; *p; ++p) {
        // Allow alphanumeric, underscore, dot and %
        if (!isalnum((unsigned char) *p) && *p != '_' && *p != '.' && *p != '%') {
            return false;
        }
    }
    return true;
}

// Local helper: rejects a combined category+flag name that contains a reserved sibling-file
// suffix token (_lang, _layout, _notes). The template scanner skips any file whose name contains
// one of these, so such a template would be undiscoverable and could clobber another template's
// language, layout, or notes file in the same category folder.
static bool name_uses_reserved_suffix_token(const char *category, const char *flag,
                                            char *error_message, size_t error_msg_size) {
    char combo[MAX_PATH_LENGTH];
    snprintf(combo, sizeof(combo), "%s%s", category ? category : "", flag ? flag : "");
    const char *tokens[] = {"_lang", "_layout", "_notes"};
    for (const char *tok: tokens) {
        if (strstr(combo, tok)) {
            snprintf(error_message, error_msg_size,
                     "Error: Template name cannot contain '%s'. That suffix is reserved for "
                     "language, layout, and notes files.", tok);
            return true;
        }
    }
    return false;
}

// NLocal helper to check if a string ends with a specific suffix
static bool ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) {
        return false;
    }
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) {
        return false;
    }
    return strncmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}

void fs_ensure_directory_exists(const char *path) {
    char *path_copy = strdup(path);
    if (!path_copy) return;

    // Iterate through the path and create each directory level
    for (char *p = path_copy + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char original_char = *p;
            *p = '\0'; // Temporarily terminate the string

            // Check if directory exists, if not, create it
            struct stat st;
            memset(&st, 0, sizeof(st));
            if (stat(path_copy, &st) == -1) {
                MKDIR(path_copy);
            }

            *p = original_char; // Restore the slash
        }
    }
    // After the loop, create the final directory if the path itself is a directory path
    struct stat st;
    memset(&st, 0, sizeof(st));
    if (stat(path_copy, &st) == -1) {
        MKDIR(path_copy);
    }

    free(path_copy);
    path_copy = nullptr;
}

// Local helper to copy a file from source to destination
static bool fs_copy_file(const char *src, const char *dest) {
    FILE *source_file = fopen(src, "rb");
    if (!source_file) return false;

    FILE *dest_file = fopen(dest, "wb");
    if (!dest_file) {
        fclose(source_file);
        return false;
    }

    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), source_file)) > 0) {
        fwrite(buffer, 1, bytes, dest_file);
    }

    fclose(source_file);
    fclose(dest_file);
    return true;
}

void fs_create_empty_template_file(const char *path) {
    FILE *file = fopen(path, "w");
    if (!file) {
        log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to create template file: %s\n", path);
        return;
    }

    const char *skeleton = "{\n"
            "  \"advancements\": {},\n"
            "  \"stats\": {},\n"
            "  \"unlocks\": [],\n"
            "  \"custom\": [],\n"
            "  \"multi_stage_goals\": []\n"
            "}\n";
    fputs(skeleton, file);
    fclose(file);
    log_message(LOG_INFO, "[TEMP CREATE UTILS] Created template file: %s\n", path);
}

void fs_create_empty_lang_file(const char *path) {
    FILE *file = fopen(path, "w");
    if (!file) {
        log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to create language file: %s\n", path);
        return;
    }
    fputs("{\n}\n", file);
    fclose(file);
    log_message(LOG_INFO, "[TEMP CREATE UTILS] Created language file: %s\n", path);
}

bool validate_and_create_template(const char *version, const char *category, const char *flag, char *error_message,
                                  size_t error_msg_size) {
    // 1. Validate Inputs
    if (!category || category[0] == '\0') {
        snprintf(error_message, error_msg_size, "Error: Category name cannot be empty.");
        return false;
    }
    if (!is_valid_filename_part(category)) {
        snprintf(error_message, error_msg_size,
                 "Error: Category contains invalid characters.\nOnly letters, numbers, and underscores are allowed.");
        return false;
    }
    if (!is_valid_filename_part(flag)) {
        snprintf(error_message, error_msg_size,
                 "Error: Optional Flag contains invalid characters.\nOnly letters, numbers, underscores, and dots are allowed.");
        return false;
    }

    // Prevent the category+flag combination from colliding with the reserved metadata file name
    {
        char reserved_check[MAX_PATH_LENGTH];
        snprintf(reserved_check, sizeof(reserved_check), "%s%s", category, flag ? flag : "");
        if (strcasecmp(reserved_check, "advancely_template") == 0) {
            snprintf(error_message, error_msg_size,
                     "Error: 'advancely_template' is a reserved name and cannot be used.");
            return false;
        }
    }

    if (name_uses_reserved_suffix_token(category, flag, error_message, error_msg_size)) {
        return false;
    }

    // Prevent using reserved "_snapshot" suffix in legacy versions
    MC_Version version_enum = settings_get_version_from_string(version);
    if (version_enum <= MC_VERSION_1_6_4) {
        char combined_name[MAX_PATH_LENGTH];
        snprintf(combined_name, sizeof(combined_name), "%s%s", category, flag);
        if (ends_with(combined_name, "_snapshot")) {
            snprintf(error_message, error_msg_size,
                     "Error: Template name cannot end with '_snapshot' for legacy versions.");
            return false;
        }
    }

    // 2. Check for Name Collisions by scanning all templates for the version
    char new_combo[MAX_PATH_LENGTH];
    snprintf(new_combo, sizeof(new_combo), "%s%s", category, flag);

    DiscoveredTemplate *existing_templates = nullptr;
    int existing_count = 0;
    scan_for_templates(version, &existing_templates, &existing_count);

    if (existing_templates) {
        for (int i = 0; i < existing_count; ++i) {
            char existing_combo[MAX_PATH_LENGTH];
            snprintf(existing_combo, sizeof(existing_combo), "%s%s",
                     existing_templates[i].category, existing_templates[i].optional_flag);

            if (strcmp(new_combo, existing_combo) == 0) {
                snprintf(error_message, error_msg_size,
                         "Error: Name collision. The name '%s' is already produced by template (category: '%s', flag: '%s').",
                         new_combo, existing_templates[i].category, existing_templates[i].optional_flag);
                free_discovered_templates(&existing_templates, &existing_count);
                return false;
            }
        }
        free_discovered_templates(&existing_templates, &existing_count);
    }

    // 3. Construct Paths
    char version_filename[64];
    strncpy(version_filename, version, sizeof(version_filename) - 1);
    version_filename[sizeof(version_filename) - 1] = '\0';
    for (char *p = version_filename; *p; p++) {
        if (*p == '.') *p = '_';
    }

    char base_path[MAX_PATH_LENGTH];
    snprintf(base_path, sizeof(base_path), "%s/templates/%s/%s/%s_%s%s", get_resources_path(),
             version, category, version_filename, category, flag);

    char template_path[MAX_PATH_LENGTH];
    char lang_path[MAX_PATH_LENGTH];
    snprintf(template_path, sizeof(template_path), "%s.json", base_path);
    snprintf(lang_path, sizeof(lang_path), "%s_lang.json", base_path);

    // 4. Create Directory and Files
    char dir_path[MAX_PATH_LENGTH];
    snprintf(dir_path, sizeof(dir_path), "%s/templates/%s/%s", get_resources_path(), version, category);
    fs_ensure_directory_exists(dir_path);
    fs_create_empty_template_file(template_path);
    fs_create_empty_lang_file(lang_path);

    return true;
}

bool copy_template_files(const char *src_version, const char *src_category, const char *src_flag,
                         const char *dest_version, const char *dest_category, const char *dest_flag,
                         char *error_message, size_t error_msg_size) {
    // 1. Validate Destination Inputs
    if (!dest_category || dest_category[0] == '\0') {
        snprintf(error_message, error_msg_size, "Error: New category name cannot be empty.");
        return false;
    }
    if (!is_valid_filename_part(dest_category)) {
        snprintf(error_message, error_msg_size, "Error: New category contains invalid characters.");
        return false;
    }
    if (!is_valid_filename_part(dest_flag)) {
        snprintf(error_message, error_msg_size, "Error: New flag contains invalid characters.");
        return false;
    }
    // Prevent the category+flag combination from colliding with the reserved metadata file name
    {
        char reserved_check[MAX_PATH_LENGTH];
        snprintf(reserved_check, sizeof(reserved_check), "%s%s", dest_category, dest_flag ? dest_flag : "");
        if (strcasecmp(reserved_check, "advancely_template") == 0) {
            snprintf(error_message, error_msg_size,
                     "Error: 'advancely_template' is a reserved name and cannot be used.");
            return false;
        }
    }

    if (name_uses_reserved_suffix_token(dest_category, dest_flag, error_message, error_msg_size)) {
        return false;
    }
    // Check if destination is the same as source
    if (strcmp(src_version, dest_version) == 0 && strcmp(src_category, dest_category) == 0 && strcmp(
            src_flag, dest_flag) == 0) {
        snprintf(error_message, error_msg_size, "Error: New name must be different from the original.");
        return false;
    }

    // Prevent copying to a reserved "_snapshot" suffix
    MC_Version dest_version_enum = settings_get_version_from_string(dest_version);
    if (dest_version_enum <= MC_VERSION_1_6_4) {
        char combined_name[MAX_PATH_LENGTH];
        snprintf(combined_name, sizeof(combined_name), "%s%s", dest_category, dest_flag);
        if (ends_with(combined_name, "_snapshot")) {
            snprintf(error_message, error_msg_size,
                     "Error: Template name cannot end with '_snapshot' for legacy versions.");
            return false;
        }
    }

    // 2. Check for Name Collisions at Destination based on final filename
    char dest_version_filename[64];
    version_to_filename_format(dest_version, dest_version_filename, sizeof(dest_version_filename));
    char new_filename_part[MAX_PATH_LENGTH];
    snprintf(new_filename_part, sizeof(new_filename_part), "%s_%s%s", dest_version_filename, dest_category, dest_flag);

    DiscoveredTemplate *existing_templates = nullptr;
    int existing_count = 0;
    scan_for_templates(dest_version, &existing_templates, &existing_count);

    if (existing_templates) {
        for (int i = 0; i < existing_count; ++i) {
            char existing_version_filename[64];
            version_to_filename_format(dest_version, existing_version_filename, sizeof(existing_version_filename));
            char existing_filename_part[MAX_PATH_LENGTH];
            snprintf(existing_filename_part, sizeof(existing_filename_part), "%s_%s%s",
                     existing_version_filename, existing_templates[i].category, existing_templates[i].optional_flag);

            if (strcmp(new_filename_part, existing_filename_part) == 0) {
                snprintf(error_message, error_msg_size,
                         "Error: Name collision. This combination results in an existing filename.");
                free_discovered_templates(&existing_templates, &existing_count);
                return false;
            }
        }
        free_discovered_templates(&existing_templates, &existing_count);
    }


    // 3. Construct Source Paths
    char src_version_filename[64];
    strncpy(src_version_filename, src_version, sizeof(src_version_filename) - 1);
    src_version_filename[sizeof(src_version_filename) - 1] = '\0';
    for (char *p = src_version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char src_base_path[MAX_PATH_LENGTH];
    snprintf(src_base_path, sizeof(src_base_path), "%s/templates/%s/%s/%s_%s%s", get_resources_path(),
             src_version, src_category, src_version_filename, src_category, src_flag);

    char src_template_path[MAX_PATH_LENGTH];
    snprintf(src_template_path, sizeof(src_template_path), "%s.json", src_base_path);

    // 4. Check if source template is empty or invalid
    cJSON *src_template_json = cJSON_from_file(src_template_path);
    if (src_template_json == nullptr || src_template_json->child == nullptr) {
        snprintf(error_message, error_msg_size,
                 "Error: Source template file is empty or invalid and cannot be copied.");
        cJSON_Delete(src_template_json); // Delete and return
        return false;
    }
    cJSON_Delete(src_template_json); // We only needed to check it, now we free it.


    // 5. Construct Destination Paths
    char dest_base_path[MAX_PATH_LENGTH];
    snprintf(dest_base_path, sizeof(dest_base_path), "%s/templates/%s/%s/%s_%s%s", get_resources_path(),
             dest_version, dest_category, dest_version_filename, dest_category, dest_flag);

    char dest_template_path[MAX_PATH_LENGTH];
    snprintf(dest_template_path, sizeof(dest_template_path), "%s.json", dest_base_path);

    // 6. Create Directory and Copy Files
    char dest_dir_path[MAX_PATH_LENGTH];
    snprintf(dest_dir_path, sizeof(dest_dir_path), "%s/templates/%s/%s", get_resources_path(), dest_version,
             dest_category);
    fs_ensure_directory_exists(dest_dir_path);

    // Copy the main template file
    if (!fs_copy_file(src_template_path, dest_template_path)) {
        snprintf(error_message, error_msg_size, "Error: Failed to copy main template file.");
        return false;
    }

    // 7. Find and copy ALL associated language files using a direct scan
    char src_category_path[MAX_PATH_LENGTH];
    snprintf(src_category_path, sizeof(src_category_path), "%s/templates/%s/%s", get_resources_path(), src_version,
             src_category);

    char src_base_filename[MAX_PATH_LENGTH];
    snprintf(src_base_filename, sizeof(src_base_filename), "%s_%s%s", src_version_filename, src_category, src_flag);

#ifdef _WIN32
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s\\%s_lang*.json", src_category_path, src_base_filename);
    WIN32_FIND_DATAA find_data;
    HANDLE h_find = FindFirstFileA(search_path, &find_data);

    if (h_find != INVALID_HANDLE_VALUE) {
        do {
            const char *lang_suffix_start = strstr(find_data.cFileName, "_lang");
            if (lang_suffix_start) {
                char src_lang_path[MAX_PATH_LENGTH];
                snprintf(src_lang_path, sizeof(src_lang_path), "%s/%s", src_category_path, find_data.cFileName);

                char dest_lang_path[MAX_PATH_LENGTH];
                // FIX: Removed the extra ".json". The lang_suffix_start variable already contains the full suffix (e.g., "_lang.json" or "_lang_ger.json").
                snprintf(dest_lang_path, sizeof(dest_lang_path), "%s%s", dest_base_path, lang_suffix_start);

                if (!fs_copy_file(src_lang_path, dest_lang_path)) {
                    log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to copy language file: %s\n", src_lang_path);
                }
            }
        } while (FindNextFileA(h_find, &find_data) != 0);
        FindClose(h_find);
    }
#else // POSIX
    DIR *dir = opendir(src_category_path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strncmp(entry->d_name, src_base_filename, strlen(src_base_filename)) == 0 &&
                strstr(entry->d_name, "_lang") && strstr(entry->d_name, ".json")) {
                const char *lang_suffix_start = strstr(entry->d_name, "_lang");

                char src_lang_path[MAX_PATH_LENGTH];
                snprintf(src_lang_path, sizeof(src_lang_path), "%s/%s", src_category_path, entry->d_name);

                char dest_lang_path[MAX_PATH_LENGTH];
                // FIX: Removed the extra ".json". The lang_suffix_start variable already contains the full suffix (e.g., "_lang.json" or "_lang_ger.json").
                snprintf(dest_lang_path, sizeof(dest_lang_path), "%s%s", dest_base_path, lang_suffix_start);

                if (!fs_copy_file(src_lang_path, dest_lang_path)) {
                    log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to copy language file: %s\n", src_lang_path);
                }
            }
        }
        closedir(dir);
    }
#endif

    return true;
}


bool delete_template_files(const char *version, const char *category, const char *flag) {
    char version_filename[64];
    strncpy(version_filename, version, sizeof(version_filename) - 1);
    version_filename[sizeof(version_filename) - 1] = '\0';
    for (char *p = version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char base_filename[MAX_PATH_LENGTH];
    snprintf(base_filename, sizeof(base_filename), "%s_%s%s", version_filename, category, flag);

    char category_path[MAX_PATH_LENGTH];
    snprintf(category_path, sizeof(category_path), "%s/templates/%s/%s", get_resources_path(), version, category);

    bool all_success = true;

#ifdef _WIN32
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s\\*", category_path); // Search entire directory
    WIN32_FIND_DATAA find_data;
    HANDLE h_find = FindFirstFileA(search_path, &find_data);

    if (h_find != INVALID_HANDLE_VALUE) {
        do {
            const char *filename = find_data.cFileName;
            size_t base_len = strlen(base_filename);

            // Check if the filename starts with the exact base name
            if (strncmp(filename, base_filename, base_len) == 0) {
                const char *suffix = filename + base_len;
                // Now check if the suffix is one of the valid ones for a template file
                if (strcmp(suffix, ".json") == 0 ||
                    strcmp(suffix, "_snapshot.json") == 0 ||
                    strcmp(suffix, "_notes.txt") == 0 ||
                    strncmp(suffix, "_lang", 5) == 0) // Catches _lang.json and _lang_...json
                {
                    char file_to_delete[MAX_PATH_LENGTH];
                    snprintf(file_to_delete, sizeof(file_to_delete), "%s/%s", category_path, filename);
                    if (remove(file_to_delete) != 0) {
                        log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to delete file: %s\n", file_to_delete);
                        all_success = false;
                    } else {
                        log_message(LOG_INFO, "[TEMP CREATE UTILS] Deleted file: %s\n", file_to_delete);
                    }
                }
            }
        } while (FindNextFileA(h_find, &find_data) != 0);
        FindClose(h_find);
    }
#else // POSIX
    DIR *dir = opendir(category_path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            const char *filename = entry->d_name;
            size_t base_len = strlen(base_filename);

            // Check if the filename starts with the exact base name
            if (strncmp(filename, base_filename, base_len) == 0) {
                const char *suffix = filename + base_len;
                // Now check if the suffix is one of the valid ones for a template file
                if (strcmp(suffix, ".json") == 0 ||
                    strcmp(suffix, "_snapshot.json") == 0 ||
                    strcmp(suffix, "_notes.txt") == 0 ||
                    strncmp(suffix, "_lang", 5) == 0) // Catches _lang.json and _lang_...json
                {
                    char file_to_delete[MAX_PATH_LENGTH];
                    snprintf(file_to_delete, sizeof(file_to_delete), "%s/%s", category_path, filename);
                    if (remove(file_to_delete) != 0) {
                        log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to delete file: %s\n", file_to_delete);
                        all_success = false;
                    } else {
                        log_message(LOG_INFO, "[TEMP CREATE UTILS] Deleted file: %s\n", file_to_delete);
                    }
                }
            }
        }
        closedir(dir);
    }
#endif

    // After deleting files, check if the category directory is empty and remove it
    if (all_success && is_directory_empty(category_path)) {
#ifdef _WIN32
        if (RemoveDirectoryA(category_path)) {
#else
            if (rmdir(category_path) == 0) {
#endif
            log_message(LOG_INFO, "[TEMP CREATE UTILS] Removed empty category directory: %s\n", category_path);

            // If category was removed, check if the parent version directory is now empty
            char version_path[MAX_PATH_LENGTH];
            snprintf(version_path, sizeof(version_path), "%s/templates/%s", get_resources_path(), version);
            if (is_directory_empty(version_path)) {
#ifdef _WIN32
                if (RemoveDirectoryA(version_path)) {
#else
                    if (rmdir(version_path) == 0) {
#endif
                    log_message(LOG_INFO, "[TEMP CREATE UTILS] Removed empty version directory: %s\n", version_path);
                }
            }
        }
    }
    return all_success;
}

// Helper to construct the base path for a template's files, reducing code duplication.
static void construct_template_base_path(const char *version, const char *category, const char *flag, char *out_path,
                                         size_t max_len) {
    char version_filename[64];
    strncpy(version_filename, version, sizeof(version_filename) - 1);
    version_filename[sizeof(version_filename) - 1] = '\0';
    for (char *p = version_filename; *p; p++) {
        if (*p == '.') *p = '_';
    }
    snprintf(out_path, max_len, "%s/templates/%s/%s/%s_%s%s", get_resources_path(),
             version, category, version_filename, category, flag);
}

bool validate_and_create_lang_file(const char *version, const char *category, const char *flag,
                                   const char *new_lang_flag, char *error_message, size_t error_msg_size) {
    // 1. Validate new_lang_flag
    if (!new_lang_flag || new_lang_flag[0] == '\0') {
        snprintf(error_message, error_msg_size, "Error: Language flag cannot be empty.");
        return false;
    }
    if (!is_valid_filename_part(new_lang_flag)) {
        snprintf(error_message, error_msg_size, "Error: Language flag contains invalid characters.");
        return false;
    }

    // 2. Construct path and check if it already exists
    char base_path[MAX_PATH_LENGTH];
    construct_template_base_path(version, category, flag, base_path, sizeof(base_path));

    char new_lang_path[MAX_PATH_LENGTH];
    snprintf(new_lang_path, sizeof(new_lang_path), "%s_lang_%s.json", base_path, new_lang_flag);

    if (path_exists(new_lang_path)) {
        snprintf(error_message, error_msg_size, "Error: A language file with the flag '%s' already exists.",
                 new_lang_flag);
        return false;
    }

    // 3. Create the empty file
    fs_create_empty_lang_file(new_lang_path);
    return true;
}

CopyLangResult copy_lang_file(const char *version, const char *category, const char *flag, const char *src_lang_flag,
                              const char *dest_lang_flag, char *error_message, size_t error_msg_size) {
    // 1. Validate dest_lang_flag
    if (!dest_lang_flag || dest_lang_flag[0] == '\0') {
        snprintf(error_message, error_msg_size, "Error: Destination language flag cannot be empty.");
        return COPY_LANG_FAIL;
    }
    if (!is_valid_filename_part(dest_lang_flag)) {
        snprintf(error_message, error_msg_size, "Error: Destination language flag contains invalid characters.");
        return COPY_LANG_FAIL;
    }
    if (strcmp(src_lang_flag, dest_lang_flag) == 0) {
        snprintf(error_message, error_msg_size, "Error: Destination flag must be different from the source.");
        return COPY_LANG_FAIL;
    }

    // 2. Construct paths
    char base_path[MAX_PATH_LENGTH];
    construct_template_base_path(version, category, flag, base_path, sizeof(base_path));

    char src_path[MAX_PATH_LENGTH];
    if (src_lang_flag[0] == '\0') {
        snprintf(src_path, sizeof(src_path), "%s_lang.json", base_path);
    } else {
        snprintf(src_path, sizeof(src_path), "%s_lang_%s.json", base_path, src_lang_flag);
    }

    // --- Fallback to default if source is empty ---
    bool used_fallback = false;
    if (src_lang_flag[0] != '\0') {
        // Only check for fallback if the source is not already the default
        cJSON *src_json = cJSON_from_file(src_path);
        if (src_json == nullptr || src_json->child == nullptr) {
            used_fallback = true;
        }
        cJSON_Delete(src_json); // Safely delete after check
    }

    if (used_fallback) {
        log_message(LOG_INFO, "[TEMP CREATE UTILS] Source language '%s' is empty, falling back to default for copy.",
                    src_lang_flag);
        snprintf(src_path, sizeof(src_path), "%s_lang.json", base_path); // Point src_path to the default
    }

    char dest_path[MAX_PATH_LENGTH];
    snprintf(dest_path, sizeof(dest_path), "%s_lang_%s.json", base_path, dest_lang_flag);

    /// 3. Validate existence
    if (!path_exists(src_path)) {
        snprintf(error_message, error_msg_size, "Error: Source language file not found.");
        return COPY_LANG_FAIL;
    }
    if (path_exists(dest_path)) {
        snprintf(error_message, error_msg_size, "Error: A language file with the flag '%s' already exists.",
                 dest_lang_flag);
        return COPY_LANG_FAIL;
    }

    // 4. Copy the file
    if (!fs_copy_file(src_path, dest_path)) {
        snprintf(error_message, error_msg_size, "Error: Failed to copy the language file.");
        return COPY_LANG_FAIL;
    }

    return used_fallback ? COPY_LANG_SUCCESS_FALLBACK : COPY_LANG_SUCCESS_DIRECT;
}

bool delete_lang_file(const char *version, const char *category, const char *flag, const char *lang_flag_to_delete,
                      char *error_message, size_t error_msg_size) {
    // 1. Validate flag (cannot delete default)
    if (!lang_flag_to_delete || lang_flag_to_delete[0] == '\0') {
        snprintf(error_message, error_msg_size, "Error: Cannot delete the default language file.");
        return false;
    }

    // 2. Construct path
    char base_path[MAX_PATH_LENGTH];
    construct_template_base_path(version, category, flag, base_path, sizeof(base_path));

    char lang_path[MAX_PATH_LENGTH];
    snprintf(lang_path, sizeof(lang_path), "%s_lang_%s.json", base_path, lang_flag_to_delete);

    // 3. Delete file
    if (remove(lang_path) != 0) {
        snprintf(error_message, error_msg_size, "Error: Failed to delete language file: %s", lang_path);
        log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to delete lang file: %s\n", lang_path);
        return false;
    }

    log_message(LOG_INFO, "[TEMP CREATE UTILS] Deleted lang file: %s\n", lang_path);
    return true;
}


// --- TEMPLATE IMPORT/EXPORT LOGIC ---

// Name of the metadata file embedded in exported template zips. It records the exact
// version/category/optional_flag so importing can pre-fill the fields without relying on
// filename heuristics. Older zips without this file fall back to filename parsing.
static const char *TEMPLATE_META_FILENAME = "advancely_template.json";


// Recursively collect all "icon" string values from a cJSON object/array
static void collect_icons_recursive(const cJSON *node, std::vector<std::string> &out) {
    if (!node) return;
    if (cJSON_IsObject(node)) {
        const cJSON *child = node->child;
        while (child) {
            if (strcmp(child->string, "icon") == 0 && cJSON_IsString(child)) {
                const char *val = child->valuestring;
                if (val && val[0] != '\0') {
                    // Deduplicate
                    bool found = false;
                    for (auto &s: out) {
                        if (s == val) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) out.push_back(val);
                }
            } else {
                collect_icons_recursive(child, out);
            }
            child = child->next;
        }
    } else if (cJSON_IsArray(node)) {
        const cJSON *elem = node->child;
        while (elem) {
            collect_icons_recursive(elem, out);
            elem = elem->next;
        }
    }
}

bool collect_icon_paths_from_template(const char *template_json_path,
                                      std::vector<std::string> &out_icon_paths) {
    cJSON *root = cJSON_from_file(template_json_path);
    if (!root) return false;
    collect_icons_recursive(root, out_icon_paths);
    cJSON_Delete(root);
    return true;
}

bool zip_contains_icons(const char *zip_path) {
    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return false;
    bool found = false;
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat fs;
        if (!mz_zip_reader_file_stat(&zip, i, &fs) || fs.m_is_directory) continue;
        if (strncmp(fs.m_filename, "icons/", 6) == 0) {
            found = true;
            break;
        }
    }
    mz_zip_reader_end(&zip);
    return found;
}

static bool create_zip_from_template(const char *output_zip_path, const DiscoveredTemplate &template_info,
                                     const char *version, bool include_icons,
                                     char *status_message, size_t msg_size) {
    mz_zip_archive zip_archive = {};
    if (!mz_zip_writer_init_file(&zip_archive, output_zip_path, 0)) {
        snprintf(status_message, msg_size, "Error: Could not create zip file at the specified location.");
        return false;
    }

    char version_filename[64];
    version_to_filename_format(version, version_filename, sizeof(version_filename));

    char base_filename[MAX_PATH_LENGTH];
    snprintf(base_filename, sizeof(base_filename), "%s_%s%s", version_filename, template_info.category,
             template_info.optional_flag);

    char category_path[MAX_PATH_LENGTH];
    snprintf(category_path, sizeof(category_path), "%s/templates/%s/%s", get_resources_path(), version,
             template_info.category);

    // --- Collect icon paths from main template JSON if needed ---
    std::vector<std::string> icon_paths;
    char main_template_full_path[MAX_PATH_LENGTH];
    snprintf(main_template_full_path, sizeof(main_template_full_path), "%s/%s.json", category_path, base_filename);

    if (include_icons) {
        collect_icon_paths_from_template(main_template_full_path, icon_paths);
    }


    bool file_added = false;

    // --- Lambda to add template files (platform-independent loop body) ---
    auto add_template_file = [&](const char *filename) -> bool {
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, sizeof(full_path), "%s/%s", category_path, filename);
        if (!mz_zip_writer_add_file(&zip_archive, filename, full_path, nullptr, 0, MZ_DEFAULT_COMPRESSION)) {
            snprintf(status_message, msg_size, "Error: Failed to add '%s' to zip.", filename);
            return false;
        }
        file_added = true;
        return true;
    };

#ifdef _WIN32
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s\\*", category_path);
    WIN32_FIND_DATAA find_data;
    HANDLE h_find = FindFirstFileA(search_path, &find_data);
    if (h_find != INVALID_HANDLE_VALUE) {
        do {
            const char *filename = find_data.cFileName;
            size_t base_len = strlen(base_filename);
            if (strncmp(filename, base_filename, base_len) == 0) {
                const char *suffix = filename + base_len;
                if (strcmp(suffix, ".json") == 0 || strncmp(suffix, "_lang", 5) == 0) {
                    if (!add_template_file(filename)) {
                        mz_zip_writer_end(&zip_archive);
                        FindClose(h_find);
                        return false;
                    }
                }
            }
        } while (FindNextFileA(h_find, &find_data) != 0);
        FindClose(h_find);
    }
#else
    DIR *dir = opendir(category_path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type == DT_REG) {
                const char *filename = entry->d_name;
                size_t base_len = strlen(base_filename);
                if (strncmp(filename, base_filename, base_len) == 0) {
                    const char *suffix = filename + base_len;
                    if (strcmp(suffix, ".json") == 0 || strncmp(suffix, "_lang", 5) == 0) {
                        if (!add_template_file(filename)) {
                            mz_zip_writer_end(&zip_archive);
                            closedir(dir);
                            return false;
                        }
                    }
                }
            }
        }
        closedir(dir);
    }
#endif

    if (!file_added) {
        snprintf(status_message, msg_size, "Error: No files found to export for this template.");
        mz_zip_writer_end(&zip_archive);
        return false;
    }

    // --- Embed metadata so importing can pre-fill version/category/flag exactly ---
    {
        cJSON *meta = cJSON_CreateObject();
        cJSON_AddStringToObject(meta, "version", version);
        cJSON_AddStringToObject(meta, "category", template_info.category);
        cJSON_AddStringToObject(meta, "optional_flag", template_info.optional_flag);
        char *meta_str = cJSON_PrintUnformatted(meta);
        cJSON_Delete(meta);
        if (meta_str) {
            mz_zip_writer_add_mem(&zip_archive, TEMPLATE_META_FILENAME, meta_str, strlen(meta_str),
                                  MZ_DEFAULT_COMPRESSION);
            free(meta_str);
        }
    }

    // --- Add icon files to the zip under icons/<relative_path> ---
    if (include_icons) {
        char icons_base[MAX_PATH_LENGTH];
        snprintf(icons_base, sizeof(icons_base), "%s/icons", get_application_dir());

        for (const auto &rel_path: icon_paths) {
            char full_icon_path[MAX_PATH_LENGTH];
            snprintf(full_icon_path, sizeof(full_icon_path), "%s/%s", icons_base, rel_path.c_str());

            if (!path_exists(full_icon_path)) {
                log_message(LOG_INFO, "[EXPORT] Icon not found, skipping: %s\n", full_icon_path);
                continue;
            }

            // Zip entry path: "icons/<rel_path>"  e.g. "icons/mymod/custom.png"
            char zip_entry_name[MAX_PATH_LENGTH];
            snprintf(zip_entry_name, sizeof(zip_entry_name), "icons/%s", rel_path.c_str());

            if (!mz_zip_writer_add_file(&zip_archive, zip_entry_name, full_icon_path,
                                        nullptr, 0, MZ_DEFAULT_COMPRESSION)) {
                snprintf(status_message, msg_size, "Error: Failed to add icon '%s' to zip.", rel_path.c_str());
                mz_zip_writer_end(&zip_archive);
                return false;
            }
        }
    }

    mz_zip_writer_finalize_archive(&zip_archive);
    mz_zip_writer_end(&zip_archive);
    return true;
}

bool handle_export_template(const DiscoveredTemplate &selected_template, const char *version,
                            bool include_icons,
                            char *status_message, size_t msg_size) {
    char suggested_filename[MAX_PATH_LENGTH];
    snprintf(suggested_filename, sizeof(suggested_filename), "%s_%s%s.zip", version,
             selected_template.category, selected_template.optional_flag);

    const char *filter_patterns[1] = {"*.zip"};
    const char *save_path = tinyfd_saveFileDialog("Export Template", suggested_filename,
                                                  1, filter_patterns, "ZIP archives");
    if (!save_path) {
        snprintf(status_message, msg_size, "Export canceled.");
        return false;
    }

    if (create_zip_from_template(save_path, selected_template, version,
                                 include_icons, status_message, msg_size)) {
        if (include_icons)
            snprintf(status_message, msg_size, "Template exported successfully (with icons)!");
        else
            snprintf(status_message, msg_size, "Template exported successfully!");
        return true;
    }
    return false;
}

// Helper function to check if a template with a given name already exists.
bool template_exists(const char *version, const char *category, const char *flag) {
    DiscoveredTemplate *templates = nullptr;
    int count = 0;
    scan_for_templates(version, &templates, &count);
    bool exists = false;
    for (int i = 0; i < count; i++) {
        if (strcmp(templates[i].category, category) == 0 && strcmp(templates[i].optional_flag, flag) == 0) {
            exists = true;
            break;
        }
    }
    free_discovered_templates(&templates, &count);
    return exists;
}

// Helper to parse a template filename (e.g., "1.16.1_all_advancements_flag.json") into its components.
// Helper to parse a template filename (e.g., "1.16.1_all_advancements_flag.json") into its components.
static bool parse_template_filename(const char *filename, char *out_version, char *out_category, char *out_flag) {
    std::string fname_str(filename);

    // Remove .json extension
    size_t ext_pos = fname_str.rfind(".json");
    if (ext_pos == std::string::npos) return false;
    fname_str = fname_str.substr(0, ext_pos);

    // Find the version by checking against the known version list
    size_t best_match_len = 0;
    const char *version_match = nullptr;
    for (int i = 0; i < VERSION_STRINGS_COUNT; ++i) {
        std::string version_str_mangled = VERSION_STRINGS[i];
        std::replace(version_str_mangled.begin(), version_str_mangled.end(), '.', '_');
        if (fname_str.rfind(version_str_mangled, 0) == 0) {
            // Check if it starts with this version
            if (version_str_mangled.length() > best_match_len) {
                best_match_len = version_str_mangled.length();
                version_match = VERSION_STRINGS[i];
            }
        }
    }

    if (!version_match) return false; // No valid version found at the start of the filename
    strncpy(out_version, version_match, 64 - 1);
    out_version[64 - 1] = '\0';

    // The rest of the string is category + optional flag
    std::string remainder = fname_str.substr(best_match_len);
    if (remainder.empty() || remainder[0] != '_') return false; // Must have at least a category
    remainder = remainder.substr(1); // Remove leading underscore

    std::string category_part = remainder;
    std::string flag_part = "";

    // --- Step 1: Check for a numeric suffix (e.g., "test1", "category_123") ---
    size_t last_char_pos = category_part.length() - 1;
    size_t first_digit_pos = std::string::npos;

    // Scan backwards from the end to find where the number starts
    for (size_t i = last_char_pos; i != std::string::npos; --i) {
        if (isdigit(category_part[i])) {
            first_digit_pos = i;
        } else {
            break; // Stop at the first non-digit
        }
        if (i == 0) break;
    }

    // A numeric suffix was found, and it's not the entire string
    if (first_digit_pos != std::string::npos && first_digit_pos > 0) {
        size_t split_pos = first_digit_pos;
        // Check if the character before the number is an underscore and include it in the flag
        if (split_pos > 0 && category_part[split_pos - 1] == '_') {
            split_pos--;
        }
        flag_part = category_part.substr(split_pos);
        category_part = category_part.substr(0, split_pos);
    } else {
        // --- Step 2: Fallback to old heuristic for non-numeric flags (e.g., "_optimized") ---
        size_t last_underscore = category_part.rfind('_');
        if (last_underscore != std::string::npos) {
            // A part is considered a flag if it's short
            if ((category_part.length() - last_underscore) <= 10) {
                flag_part = category_part.substr(last_underscore);
                category_part = category_part.substr(0, last_underscore);
            }
        }
    }

    // Assign the final parsed parts
    strncpy(out_category, category_part.c_str(), MAX_PATH_LENGTH - 1);
    out_category[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(out_flag, flag_part.c_str(), MAX_PATH_LENGTH - 1);
    out_flag[MAX_PATH_LENGTH - 1] = '\0';

    return true;
}

bool get_info_from_zip(const char *zip_path, char *out_version, char *out_category, char *out_flag, char *error_message,
                       size_t msg_size, bool *out_from_metadata) {
    if (out_from_metadata) *out_from_metadata = false;
    mz_zip_archive zip_archive = {};
    if (!mz_zip_reader_init_file(&zip_archive, zip_path, 0)) {
        snprintf(error_message, msg_size, "Error: Could not read zip file.");
        return false;
    }

    // --- Prefer the embedded metadata file for an exact, heuristic-free result ---
    int meta_idx = mz_zip_reader_locate_file(&zip_archive, TEMPLATE_META_FILENAME, nullptr, 0);
    if (meta_idx >= 0) {
        size_t meta_size = 0;
        void *meta_buf = mz_zip_reader_extract_to_heap(&zip_archive, (mz_uint) meta_idx, &meta_size, 0);
        if (meta_buf) {
            cJSON *meta = cJSON_ParseWithLength((const char *) meta_buf, meta_size);
            free(meta_buf);
            if (meta) {
                cJSON *v = cJSON_GetObjectItemCaseSensitive(meta, "version");
                cJSON *c = cJSON_GetObjectItemCaseSensitive(meta, "category");
                cJSON *f = cJSON_GetObjectItemCaseSensitive(meta, "optional_flag");
                if (cJSON_IsString(v) && cJSON_IsString(c)) {
                    strncpy(out_version, v->valuestring, 64 - 1);
                    out_version[64 - 1] = '\0';
                    strncpy(out_category, c->valuestring, MAX_PATH_LENGTH - 1);
                    out_category[MAX_PATH_LENGTH - 1] = '\0';
                    const char *flag_val = cJSON_IsString(f) ? f->valuestring : "";
                    strncpy(out_flag, flag_val, MAX_PATH_LENGTH - 1);
                    out_flag[MAX_PATH_LENGTH - 1] = '\0';
                    if (out_from_metadata) *out_from_metadata = true;
                    cJSON_Delete(meta);
                    mz_zip_reader_end(&zip_archive);
                    return true;
                }
                cJSON_Delete(meta);
            }
        }
        // Metadata present but unreadable: fall through to filename parsing.
    }

    char main_template_filename[MAX_PATH_LENGTH] = "";
    mz_uint num_files = mz_zip_reader_get_num_files(&zip_archive);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) continue;
        if (strcmp(file_stat.m_filename, TEMPLATE_META_FILENAME) == 0) continue; // skip metadata file
        if (!file_stat.m_is_directory && strstr(file_stat.m_filename, ".json") && !
            strstr(file_stat.m_filename, "_lang")) {
            if (main_template_filename[0] != '\0') {
                snprintf(error_message, msg_size, "Error: Zip file contains multiple main template files.");
                mz_zip_reader_end(&zip_archive);
                return false;
            }
            strncpy(main_template_filename, file_stat.m_filename, sizeof(main_template_filename) - 1);
            main_template_filename[sizeof(main_template_filename) - 1] = '\0';
        }
    }

    mz_zip_reader_end(&zip_archive);

    if (main_template_filename[0] == '\0') {
        snprintf(error_message, msg_size,
                 "Error: Zip file does not contain a main template file (e.g., ..._all_advancements.json).");
        return false;
    }

    if (!parse_template_filename(main_template_filename, out_version, out_category, out_flag)) {
        snprintf(error_message, msg_size,
                 "Error: Could not parse template name from '%s'. Filename must be in '<VERSION>_<CATEGORY><FLAG>.json' format.",
                 main_template_filename);
        return false;
    }

    return true;
}

bool execute_import_from_zip(const char *zip_path, const char *version, const char *category,
                             const char *flag, bool import_icons,
                             char *error_message, size_t msg_size) {
    // Final validation before extracting
    char version_filename[64];
    version_to_filename_format(version, version_filename, sizeof(version_filename));
    char new_filename_part[MAX_PATH_LENGTH];
    snprintf(new_filename_part, sizeof(new_filename_part), "%s_%s%s", version_filename, category, flag);

    DiscoveredTemplate *templates = nullptr;
    int count = 0;
    scan_for_templates(version, &templates, &count);
    bool exists = false;
    if (templates) {
        for (int i = 0; i < count; i++) {
            char existing_version_filename[64];
            version_to_filename_format(version, existing_version_filename, sizeof(existing_version_filename));
            char existing_filename_part[MAX_PATH_LENGTH];
            snprintf(existing_filename_part, sizeof(existing_filename_part), "%s_%s%s",
                     existing_version_filename, templates[i].category, templates[i].optional_flag);
            if (strcmp(new_filename_part, existing_filename_part) == 0) {
                exists = true;
                break;
            }
        }
        free_discovered_templates(&templates, &count);
    }

    if (exists) {
        snprintf(error_message, msg_size, "Error: A template with this name already exists for version %s.", version);
        return false;
    }


    // --- Get original template info from zip to construct old base filename ---
    char old_version[64], old_category[MAX_PATH_LENGTH], old_flag[MAX_PATH_LENGTH];
    if (!get_info_from_zip(zip_path, old_version, old_category, old_flag, error_message, msg_size)) {
        return false; // Should not happen as it's checked before, but for robustness
    }
    char old_version_filename[64];
    version_to_filename_format(old_version, old_version_filename, sizeof(old_version_filename));
    char old_base_filename[MAX_PATH_LENGTH];
    snprintf(old_base_filename, sizeof(old_base_filename), "%s_%s%s", old_version_filename, old_category, old_flag);

    // --- Construct new base filename from user's confirmed details ---
    char new_base_filename[MAX_PATH_LENGTH];
    snprintf(new_base_filename, sizeof(new_base_filename), "%s_%s%s", version_filename, category, flag);

    char dest_dir[MAX_PATH_LENGTH];
    snprintf(dest_dir, sizeof(dest_dir), "%s/templates/%s/%s/", get_resources_path(), version, category);
    fs_ensure_directory_exists(dest_dir);

    mz_zip_archive zip_archive = {};
    if (!mz_zip_reader_init_file(&zip_archive, zip_path, 0)) {
        snprintf(error_message, msg_size, "Error: Could not re-read zip file for extraction.");
        return false;
    }

    bool success = true;
    mz_uint num_files = mz_zip_reader_get_num_files(&zip_archive);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat) || file_stat.m_is_directory) continue;

        char new_filename[MAX_PATH_LENGTH];
        const char *original_filename = file_stat.m_filename;
        size_t old_base_len = strlen(old_base_filename);

        // Skip the embedded metadata file; it is only used to pre-fill the import fields.
        if (strcmp(original_filename, TEMPLATE_META_FILENAME) == 0) continue;

        // Check if the file matches the old template structure for renaming
        if (strncmp(original_filename, old_base_filename, old_base_len) == 0) {
            const char *suffix = original_filename + old_base_len;
            snprintf(new_filename, sizeof(new_filename), "%s%s", new_base_filename, suffix);
        } else {
            // Skip icon files — handled separately in the icon extraction pass below
            if (strncmp(original_filename, "icons/", 6) == 0) continue;
            // Not a core template file, extract with original name to preserve any extra files
            strncpy(new_filename, original_filename, sizeof(new_filename) - 1);
            new_filename[sizeof(new_filename) - 1] = '\0';
        }

        char dest_path[MAX_PATH_LENGTH];
        snprintf(dest_path, sizeof(dest_path), "%s%s", dest_dir, new_filename);
        if (!mz_zip_reader_extract_to_file(&zip_archive, i, dest_path, 0)) {
            snprintf(error_message, msg_size, "Error: Failed to extract '%s'.", original_filename);
            success = false;
            break;
        }
    }

    // --- Extract icon files if requested ---
    if (import_icons && success) {
        char icons_base[MAX_PATH_LENGTH];
        snprintf(icons_base, sizeof(icons_base), "%s/icons", get_application_dir());

        // Re-iterate the zip for icon entries
        for (mz_uint i = 0; i < num_files; i++) {
            mz_zip_archive_file_stat file_stat;
            if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat) || file_stat.m_is_directory) continue;

            if (strncmp(file_stat.m_filename, "icons/", 6) != 0) continue;

            // Destination: resources/icons/<everything after "icons/">
            const char *rel_icon_path = file_stat.m_filename + 6; // skip "icons/"
            char dest_icon_path[MAX_PATH_LENGTH];
            snprintf(dest_icon_path, sizeof(dest_icon_path), "%s/%s", icons_base, rel_icon_path);

            char dest_icon_dir[MAX_PATH_LENGTH];
            get_parent_directory(dest_icon_path, dest_icon_dir, sizeof(dest_icon_dir), 1);
            // Append trailing slash so fs_ensure_directory_exists treats it as a directory
            size_t dir_len = strlen(dest_icon_dir);
            if (dir_len > 0 && dest_icon_dir[dir_len - 1] != '/') {
                dest_icon_dir[dir_len] = '/';
                dest_icon_dir[dir_len + 1] = '\0';
            }
            fs_ensure_directory_exists(dest_icon_dir);

            if (!mz_zip_reader_extract_to_file(&zip_archive, i, dest_icon_path, 0)) {
                log_message(LOG_INFO, "[IMPORT] Warning: Could not extract icon '%s', skipping.\n",
                            file_stat.m_filename);
                // Non-fatal: continue extracting other icons
            }
        }
    }

    mz_zip_reader_end(&zip_archive);
    return success;
}

void handle_export_language(const char *version, const char *category, const char *flag,
                            const char *lang_flag_to_export) {
    char base_path[MAX_PATH_LENGTH];
    construct_template_base_path(version, category, flag, base_path, sizeof(base_path));

    char lang_path[MAX_PATH_LENGTH];
    if (lang_flag_to_export[0] == '\0') {
        snprintf(lang_path, sizeof(lang_path), "%s_lang.json", base_path);
    } else {
        snprintf(lang_path, sizeof(lang_path), "%s_lang_%s.json", base_path, lang_flag_to_export);
    }

#ifdef _WIN32
    char command[MAX_PATH_LENGTH + 64];
    path_to_windows_native(lang_path);
    snprintf(command, sizeof(command), "/select,\"%s\"", lang_path);
    ShellExecuteA(nullptr, "open", "explorer", command, nullptr, SW_SHOW);
#else // macOS and Linux
    pid_t pid = fork();
    if (pid == 0) {  // Child process
#if __APPLE__
    // Use "open -R" to reveal the file in Finder
    char *args[] = {(char *) "open", (char *) "-R", lang_path, nullptr};
    execvp(args[0], args);
#else // Linux
    // Highlighting a file isn't a standard feature, so we open the parent directory.
    char parent_dir[MAX_PATH_LENGTH];
    if (get_parent_directory(lang_path, parent_dir, sizeof(parent_dir), 1)) {
        char *args[] = {(char *) "xdg-open", parent_dir, nullptr};
        execvp(args[0], args);
    }
#endif
    _exit(127); // Exit if exec fails
    } else if (pid < 0) {
        log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to fork process to open folder.\n");
    }
#endif
}


bool execute_import_language_file(const char *version, const char *category, const char *flag, const char *source_path,
                                  const char *new_lang_flag, char *error_message, size_t error_msg_size) {
    // 1. Validate new_lang_flag
    if (!new_lang_flag || new_lang_flag[0] == '\0') {
        snprintf(error_message, error_msg_size, "Error: New language flag cannot be empty.");
        return false;
    }
    if (!is_valid_filename_part(new_lang_flag)) {
        snprintf(error_message, error_msg_size, "Error: New language flag contains invalid characters.");
        return false;
    }

    // 2. Construct destination path and check for collision
    char base_path[MAX_PATH_LENGTH];
    construct_template_base_path(version, category, flag, base_path, sizeof(base_path));

    char dest_path[MAX_PATH_LENGTH];
    snprintf(dest_path, sizeof(dest_path), "%s_lang_%s.json", base_path, new_lang_flag);

    if (path_exists(dest_path)) {
        snprintf(error_message, error_msg_size,
                 "Error: A language file with the flag '%s' already exists for this template.", new_lang_flag);
        return false;
    }

    // 3. Copy the file
    if (!fs_copy_file(source_path, dest_path)) {
        snprintf(error_message, error_msg_size, "Error: Failed to copy the language file.");
        return false;
    }

    log_message(LOG_INFO, "[TEMP CREATE UTILS] Imported language file from '%s' to '%s'\n", source_path, dest_path);
    return true;
}

std::vector<int> compute_new_advancement_indices(
    const std::vector<std::string> &template_root_names,
    const std::vector<ImportableAdvancement> &import_advs,
    bool include_recipes) {
    std::unordered_set<std::string> template_set(template_root_names.begin(), template_root_names.end());
    std::vector<int> result;
    result.reserve(import_advs.size());
    for (int i = 0; i < (int) import_advs.size(); i++) {
        const auto &adv = import_advs[i];
        if (!include_recipes && adv.root_name.find(":recipes/") != std::string::npos) continue;
        if (template_set.find(adv.root_name) == template_set.end()) result.push_back(i);
    }
    return result;
}

static std::string basename_after_last_slash(const std::string &s) {
    size_t pos = s.find_last_of('/');
    return (pos == std::string::npos) ? s : s.substr(pos + 1);
}

std::vector<RenameRow> compute_rename_candidates(
    const std::vector<std::string> &template_root_names,
    const std::vector<std::vector<std::string> > &template_criteria,
    const std::vector<ImportableAdvancement> &import_advs,
    bool include_recipes,
    bool match_basename,
    bool match_overlap) {
    std::vector<RenameRow> result;
    if (!match_basename && !match_overlap) return result;
    std::unordered_set<std::string> template_set(template_root_names.begin(), template_root_names.end());

    std::vector<int> candidate_imports;
    candidate_imports.reserve(import_advs.size());
    for (int i = 0; i < (int) import_advs.size(); i++) {
        const auto &adv = import_advs[i];
        if (!include_recipes && adv.root_name.find(":recipes/") != std::string::npos) continue;
        if (template_set.find(adv.root_name) != template_set.end()) continue;
        candidate_imports.push_back(i);
    }
    if (candidate_imports.empty()) return result;

    std::unordered_set<std::string> import_set;
    import_set.reserve(import_advs.size() * 2);
    for (const auto &adv: import_advs) import_set.insert(adv.root_name);

    std::vector<std::unordered_set<std::string> > import_crit_sets(import_advs.size());
    for (int idx: candidate_imports) {
        const auto &adv = import_advs[idx];
        for (const auto &c: adv.criteria) import_crit_sets[idx].insert(c.root_name);
    }

    for (int t = 0; t < (int) template_root_names.size(); t++) {
        const std::string &tname = template_root_names[t];
        if (import_set.find(tname) != import_set.end()) continue;
        std::string tbase = basename_after_last_slash(tname);

        const std::vector<std::string> &tcrits =
                (t < (int) template_criteria.size()) ? template_criteria[t] : std::vector<std::string>{};
        std::unordered_set<std::string> tcrit_set(tcrits.begin(), tcrits.end());

        std::vector<RenameCandidate> cands;
        for (int idx: candidate_imports) {
            const auto &iadv = import_advs[idx];
            std::string ibase = basename_after_last_slash(iadv.root_name);
            bool basename_hit = match_basename && !tbase.empty() && tbase == ibase;

            int overlap = 0;
            int smaller = std::min((int) tcrit_set.size(), (int) import_crit_sets[idx].size());
            if (smaller > 0) {
                const auto &small_set = (tcrit_set.size() <= import_crit_sets[idx].size())
                                            ? tcrit_set
                                            : import_crit_sets[idx];
                const auto &big_set = (tcrit_set.size() <= import_crit_sets[idx].size())
                                          ? import_crit_sets[idx]
                                          : tcrit_set;
                for (const auto &name: small_set) if (big_set.find(name) != big_set.end()) overlap++;
            }

            bool overlap_hit = false;
            if (match_overlap && smaller > 0 &&
                (double) overlap / (double) smaller >= 0.8) {
                overlap_hit = true;
            }

            if (basename_hit || overlap_hit) {
                RenameCandidate rc{};
                rc.import_index = idx;
                rc.criteria_overlap = overlap;
                rc.smaller_criteria_size = smaller;
                rc.basename_match = basename_hit;
                rc.overlap_match = overlap_hit;
                cands.push_back(rc);
            }
        }

        if (cands.empty()) continue;
        std::sort(cands.begin(), cands.end(), [](const RenameCandidate &a, const RenameCandidate &b) {
            if (a.basename_match != b.basename_match) return a.basename_match;
            double sa = (a.smaller_criteria_size > 0)
                            ? (double) a.criteria_overlap / (double) a.smaller_criteria_size
                            : 0.0;
            double sb = (b.smaller_criteria_size > 0)
                            ? (double) b.criteria_overlap / (double) b.smaller_criteria_size
                            : 0.0;
            if (sa != sb) return sa > sb;
            return a.import_index < b.import_index;
        });

        RenameRow row;
        row.template_index = t;
        row.candidates = std::move(cands);
        result.push_back(std::move(row));
    }

    return result;
}

std::vector<CriteriaDeltaRow> compute_criteria_deltas(
    const std::vector<std::string> &template_root_names,
    const std::vector<std::vector<std::string> > &template_criteria,
    const std::vector<ImportableAdvancement> &import_advs,
    bool include_recipes) {
    std::vector<CriteriaDeltaRow> result;

    std::unordered_map<std::string, int> import_index_by_root;
    import_index_by_root.reserve(import_advs.size() * 2);
    for (int i = 0; i < (int) import_advs.size(); i++) {
        import_index_by_root[import_advs[i].root_name] = i;
    }

    for (int t = 0; t < (int) template_root_names.size(); t++) {
        const std::string &tname = template_root_names[t];
        if (!include_recipes && tname.find(":recipes/") != std::string::npos) continue;
        auto it = import_index_by_root.find(tname);
        if (it == import_index_by_root.end()) continue;
        const auto &iadv = import_advs[it->second];

        const std::vector<std::string> empty_v;
        const std::vector<std::string> &tcrits =
                (t < (int) template_criteria.size()) ? template_criteria[t] : empty_v;
        std::unordered_set<std::string> tcrit_set(tcrits.begin(), tcrits.end());
        std::unordered_set<std::string> icrit_set;
        icrit_set.reserve(iadv.criteria.size() * 2);
        for (const auto &c: iadv.criteria) icrit_set.insert(c.root_name);

        CriteriaDeltaRow row;
        row.template_index = t;
        for (const auto &c: iadv.criteria) {
            if (tcrit_set.find(c.root_name) == tcrit_set.end()) {
                CriterionDelta d{};
                d.root_name = c.root_name;
                d.is_new = true;
                d.is_selected = false;
                row.deltas.push_back(std::move(d));
            }
        }
        for (const auto &cname: tcrits) {
            if (icrit_set.find(cname) == icrit_set.end()) {
                CriterionDelta d{};
                d.root_name = cname;
                d.is_new = false;
                d.is_selected = false;
                row.deltas.push_back(std::move(d));
            }
        }
        if (!row.deltas.empty()) result.push_back(std::move(row));
    }

    return result;
}

static cJSON *parse_zip_entry_as_json(mz_zip_archive *zip, mz_uint file_index) {
    size_t out_size = 0;
    void *buf = mz_zip_reader_extract_to_heap(zip, file_index, &out_size, 0);
    if (!buf) return nullptr;
    std::string text((const char *) buf, out_size);
    mz_free(buf);
    return cJSON_Parse(text.c_str());
}

// Import-from-template accepts either an exported .zip archive or a raw (unzipped) main
// template .json file. This helper distinguishes the two so the readers below can branch.
static bool is_unzipped_template_path(const char *path) {
    return path != nullptr && ends_with(path, ".json");
}

// Returns the base filename (no directory, no ".json") of a raw template path into out_base.
static void unzipped_template_base(const char *path, char *out_base, size_t out_size) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *fname = slash > bslash ? slash : bslash;
    fname = fname ? fname + 1 : path;
    snprintf(out_base, out_size, "%s", fname);
    size_t len = strlen(out_base);
    if (len >= 5 && strcmp(out_base + len - 5, ".json") == 0) out_base[len - 5] = '\0';
}

cJSON *read_template_json_from_zip(const char *zip_path, char *error_message, size_t msg_size) {
    if (is_unzipped_template_path(zip_path)) {
        cJSON *root = cJSON_from_file(zip_path);
        if (!root) {
            snprintf(error_message, msg_size, "Error: Failed to read or parse the template JSON file.");
            return nullptr;
        }
        return root;
    }
    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) {
        snprintf(error_message, msg_size, "Error: Could not read zip file.");
        return nullptr;
    }
    int main_index = -1;
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat fs;
        if (!mz_zip_reader_file_stat(&zip, i, &fs) || fs.m_is_directory) continue;
        const char *name = fs.m_filename;
        if (strcmp(name, TEMPLATE_META_FILENAME) == 0) continue; // skip metadata file
        const char *ext = strstr(name, ".json");
        if (!ext) continue;
        if (strstr(name, "_lang")) continue;
        if (main_index >= 0) {
            snprintf(error_message, msg_size, "Error: Zip contains multiple main template files.");
            mz_zip_reader_end(&zip);
            return nullptr;
        }
        main_index = (int) i;
    }
    if (main_index < 0) {
        snprintf(error_message, msg_size, "Error: Zip does not contain a main template JSON file.");
        mz_zip_reader_end(&zip);
        return nullptr;
    }
    cJSON *root = parse_zip_entry_as_json(&zip, (mz_uint) main_index);
    mz_zip_reader_end(&zip);
    if (!root) {
        snprintf(error_message, msg_size, "Error: Failed to parse the template JSON inside the zip.");
        return nullptr;
    }
    return root;
}

// Returns the language flag encoded in a lang filename, or empty string for the default file.
// Returns std::string with value {nullopt-equivalent: returns "<NOMATCH>"} when the name is not a lang file.
// Caller compares result against "<NOMATCH>" sentinel.
static std::string lang_flag_from_filename(const char *name) {
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    const char *dot = strstr(base, ".json");
    if (!dot) return "<NOMATCH>";
    // Walk backwards to find "_lang" or "_lang_<flag>" before .json
    // We look for "_lang" substring; the matched one must end at dot or be followed by "_<flag>".
    const char *lang_marker = nullptr;
    for (const char *p = base; p < dot - 4; p++) {
        if (strncmp(p, "_lang", 5) == 0) {
            const char *after = p + 5;
            if (after == dot) {
                lang_marker = p;
                break;
            } // _lang.json
            if (*after == '_' && after < dot) {
                lang_marker = p;
                break;
            } // _lang_<flag>.json
        }
    }
    if (!lang_marker) return "<NOMATCH>";
    const char *after = lang_marker + 5;
    if (after == dot) return std::string(); // default
    // after starts with '_'; flag is between after+1 and dot
    return std::string(after + 1, dot - (after + 1));
}

cJSON *read_lang_json_from_zip(const char *zip_path, const char *flag) {
    if (is_unzipped_template_path(zip_path)) {
        // The lang file sits next to the main template file with a "_lang[_<flag>].json" suffix.
        std::string lang_path(zip_path);
        lang_path.erase(lang_path.size() - 5); // strip ".json"
        lang_path += "_lang";
        if (flag && flag[0] != '\0') {
            lang_path += "_";
            lang_path += flag;
        }
        lang_path += ".json";
        return cJSON_from_file(lang_path.c_str());
    }
    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return nullptr;
    std::string want_flag = (flag ? flag : "");
    int lang_index = -1;
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat fs;
        if (!mz_zip_reader_file_stat(&zip, i, &fs) || fs.m_is_directory) continue;
        std::string got = lang_flag_from_filename(fs.m_filename);
        if (got == "<NOMATCH>") continue;
        if (got != want_flag) continue;
        lang_index = (int) i;
        break;
    }
    if (lang_index < 0) {
        mz_zip_reader_end(&zip);
        return nullptr;
    }
    cJSON *root = parse_zip_entry_as_json(&zip, (mz_uint) lang_index);
    mz_zip_reader_end(&zip);
    return root;
}

std::vector<std::string> list_lang_flags_in_zip(const char *zip_path) {
    std::vector<std::string> out;
    if (is_unzipped_template_path(zip_path)) {
        // Scan the template's directory for "<base>_lang*.json" siblings.
        char dir[MAX_PATH_LENGTH];
        if (!get_parent_directory(zip_path, dir, sizeof(dir), 1)) return out;
        char base[MAX_PATH_LENGTH];
        unzipped_template_base(zip_path, base, sizeof(base));
        char prefix[MAX_PATH_LENGTH];
        snprintf(prefix, sizeof(prefix), "%s_lang", base);
#ifdef _WIN32
        char search_path[MAX_PATH_LENGTH];
        snprintf(search_path, sizeof(search_path), "%s\\%s*.json", dir, prefix);
        WIN32_FIND_DATAA find_data;
        HANDLE h_find = FindFirstFileA(search_path, &find_data);
        if (h_find != INVALID_HANDLE_VALUE) {
            do {
                std::string got = lang_flag_from_filename(find_data.cFileName);
                if (got == "<NOMATCH>") continue;
                bool dup = false;
                for (const auto &f: out) if (f == got) { dup = true; break; }
                if (!dup) out.push_back(std::move(got));
            } while (FindNextFileA(h_find, &find_data) != 0);
            FindClose(h_find);
        }
#else
        size_t prefix_len = strlen(prefix);
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *entry;
            while ((entry = readdir(d)) != nullptr) {
                if (strncmp(entry->d_name, prefix, prefix_len) != 0) continue;
                if (!strstr(entry->d_name, ".json")) continue;
                std::string got = lang_flag_from_filename(entry->d_name);
                if (got == "<NOMATCH>") continue;
                bool dup = false;
                for (const auto &f: out) if (f == got) { dup = true; break; }
                if (!dup) out.push_back(std::move(got));
            }
            closedir(d);
        }
#endif
        return out;
    }
    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return out;
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat fs;
        if (!mz_zip_reader_file_stat(&zip, i, &fs) || fs.m_is_directory) continue;
        std::string got = lang_flag_from_filename(fs.m_filename);
        if (got == "<NOMATCH>") continue;
        bool dup = false;
        for (const auto &f: out) if (f == got) {
            dup = true;
            break;
        }
        if (!dup) out.push_back(std::move(got));
    }
    mz_zip_reader_end(&zip);
    return out;
}

int extract_zip_icons_by_paths(const char *zip_path, const std::vector<std::string> &icon_paths) {
    if (icon_paths.empty()) return 0;

    if (is_unzipped_template_path(zip_path)) {
        // Pull referenced icons from an "icons/" folder next to the template file (as an
        // unzipped export would have). If there's no such folder (e.g. importing from a
        // template already installed locally), the icons are shared already and nothing is copied.
        char dir[MAX_PATH_LENGTH];
        if (!get_parent_directory(zip_path, dir, sizeof(dir), 1)) return 0;
        char icons_base[MAX_PATH_LENGTH];
        snprintf(icons_base, sizeof(icons_base), "%s/icons", get_application_dir());

        std::unordered_set<std::string> seen;
        int written = 0;
        for (const auto &p: icon_paths) {
            if (p.empty() || !seen.insert(p).second) continue;
            char src_icon[MAX_PATH_LENGTH];
            snprintf(src_icon, sizeof(src_icon), "%s/icons/%s", dir, p.c_str());
            if (!path_exists(src_icon)) continue;
            char dest_path[MAX_PATH_LENGTH];
            snprintf(dest_path, sizeof(dest_path), "%s/%s", icons_base, p.c_str());
            if (path_exists(dest_path)) continue;
            char dest_dir[MAX_PATH_LENGTH];
            get_parent_directory(dest_path, dest_dir, sizeof(dest_dir), 1);
            size_t dir_len = strlen(dest_dir);
            if (dir_len > 0 && dest_dir[dir_len - 1] != '/') {
                dest_dir[dir_len] = '/';
                dest_dir[dir_len + 1] = '\0';
            }
            fs_ensure_directory_exists(dest_dir);
            if (fs_copy_file(src_icon, dest_path)) {
                written++;
            } else {
                log_message(LOG_INFO, "[IMPORT FROM TEMPLATE] Warning: Could not copy '%s'.\n", src_icon);
            }
        }
        return written;
    }

    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return 0;

    std::unordered_set<std::string> wanted;
    for (const auto &p: icon_paths) {
        if (p.empty()) continue;
        wanted.insert("icons/" + p);
    }
    if (wanted.empty()) {
        mz_zip_reader_end(&zip);
        return 0;
    }

    char icons_base[MAX_PATH_LENGTH];
    snprintf(icons_base, sizeof(icons_base), "%s/icons", get_application_dir());

    int written = 0;
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat fs;
        if (!mz_zip_reader_file_stat(&zip, i, &fs) || fs.m_is_directory) continue;
        if (wanted.find(fs.m_filename) == wanted.end()) continue;
        const char *rel = fs.m_filename + 6; // skip "icons/"
        char dest_path[MAX_PATH_LENGTH];
        snprintf(dest_path, sizeof(dest_path), "%s/%s", icons_base, rel);
        if (path_exists(dest_path)) continue;
        char dest_dir[MAX_PATH_LENGTH];
        get_parent_directory(dest_path, dest_dir, sizeof(dest_dir), 1);
        size_t dir_len = strlen(dest_dir);
        if (dir_len > 0 && dest_dir[dir_len - 1] != '/') {
            dest_dir[dir_len] = '/';
            dest_dir[dir_len + 1] = '\0';
        }
        fs_ensure_directory_exists(dest_dir);
        if (mz_zip_reader_extract_to_file(&zip, i, dest_path, 0)) {
            written++;
        } else {
            log_message(LOG_INFO, "[IMPORT FROM TEMPLATE] Warning: Could not extract '%s'.\n", fs.m_filename);
        }
    }
    mz_zip_reader_end(&zip);
    return written;
}
