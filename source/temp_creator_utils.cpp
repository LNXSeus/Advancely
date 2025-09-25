//
// Created by Linus on 04.07.2025.
//

#include <cstdio>
#include <cstdlib> // For free() and system()
#include <cstring>
#include <sys/stat.h> // For stat and mkdir
#include <cctype> // For isalnum

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
#endif

bool parse_player_stats_for_import(const char* file_path, MC_Version version, std::vector<ImportableStat>& out_stats, char* error_message, size_t error_msg_size) {
    out_stats.clear();

    cJSON* root = cJSON_from_file(file_path);
    if (!root) {
        snprintf(error_message, error_msg_size, "Error: Could not read or parse the selected JSON file.");
        return false;
    }

    if (version <= MC_VERSION_1_6_4) {
        // Legacy .dat file (parsed as json)
        cJSON* stats_change = cJSON_GetObjectItem(root, "stats-change");
        if (cJSON_IsArray(stats_change)) {
            cJSON* stat_entry;
            cJSON_ArrayForEach(stat_entry, stats_change) {
                cJSON* item = stat_entry->child;
                if (item && item->string) {
                    out_stats.push_back({item->string, false});
                }
            }
        }
    } else if (version <= MC_VERSION_1_11_2) {
        // Mid-era flat json file
        cJSON* stat_entry = nullptr;
        cJSON_ArrayForEach(stat_entry, root) {
            // Only import entries that are simple numbers, excluding complex objects (achievements with criteria).
            if (stat_entry->string && cJSON_IsNumber(stat_entry)) {
                out_stats.push_back({stat_entry->string, false});
            }
        }
    } else {
        // Modern nested json file
        cJSON* stats_obj = cJSON_GetObjectItem(root, "stats");
        if (stats_obj) {
            cJSON* category_obj = nullptr;
            cJSON_ArrayForEach(category_obj, stats_obj) {
                if (category_obj->string) {
                    cJSON* stat_entry = nullptr;
                    cJSON_ArrayForEach(stat_entry, category_obj) {
                        if (stat_entry->string) {
                            char full_root_name[256];
                            snprintf(full_root_name, sizeof(full_root_name), "%s/%s", category_obj->string, stat_entry->string);
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

bool parse_player_advancements_for_import(const char *file_path, MC_Version version, std::vector<ImportableAdvancement> &out_advancements,
                                          char *error_message, size_t error_msg_size) {
    out_advancements.clear();

    cJSON *root = cJSON_from_file(file_path);
    if (!root) {
        snprintf(error_message, error_msg_size, "Error: Could not read or parse the selected JSON file.");
        return false;
    }

    if (version <= MC_VERSION_1_11_2) {
        // --- Mid-era stats file parsing for ALL achievements ---
        cJSON* achievement_json = nullptr;
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
    DIR* dir = opendir(src_category_path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strncmp(entry->d_name, src_base_filename, strlen(src_base_filename)) == 0 && strstr(entry->d_name, "_lang") && strstr(entry->d_name, ".json")) {
                const char* lang_suffix_start = strstr(entry->d_name, "_lang");

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
    DIR* dir = opendir(category_path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            const char* filename = entry->d_name;
            size_t base_len = strlen(base_filename);

            // Check if the filename starts with the exact base name
            if (strncmp(filename, base_filename, base_len) == 0) {
                const char* suffix = filename + base_len;
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


// Helper function to create a zip archive from a template's files
static bool create_zip_from_template(const char *output_zip_path, const DiscoveredTemplate &template_info,
                                     const char *version, char *status_message, size_t msg_size) {
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

    bool file_added = false;

#ifdef _WIN32
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s\\*", category_path); // Broad search first
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
                    strncmp(suffix, "_lang", 5) == 0) // Catches _lang.json and _lang_*.json
                {
                    char full_path[MAX_PATH_LENGTH];
                    snprintf(full_path, sizeof(full_path), "%s/%s", category_path, filename);
                    if (mz_zip_writer_add_file(&zip_archive, filename, full_path, nullptr, 0, MZ_DEFAULT_COMPRESSION)) {
                        file_added = true;
                    } else {
                        snprintf(status_message, msg_size, "Error: Failed to add '%s' to zip.", filename);
                        mz_zip_writer_end(&zip_archive);
                        FindClose(h_find);
                        return false;
                    }
                }
            }
        } while (FindNextFileA(h_find, &find_data) != 0);
        FindClose(h_find);
    }
#else // POSIX
    DIR* dir = opendir(category_path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // Check if it's a file
            if (entry->d_type == DT_REG) {
                const char* filename = entry->d_name;
                size_t base_len = strlen(base_filename);

                // Check if the filename starts with the exact base name
                if (strncmp(filename, base_filename, base_len) == 0) {
                    const char* suffix = filename + base_len;
                    // Now check if the suffix is one of the valid ones for a template file
                    if (strcmp(suffix, ".json") == 0 ||
                        strncmp(suffix, "_lang", 5) == 0) // Catches _lang.json and _lang_*.json
                    {
                        char full_path[MAX_PATH_LENGTH];
                        snprintf(full_path, sizeof(full_path), "%s/%s", category_path, filename);

                        if (mz_zip_writer_add_file(&zip_archive, filename, full_path, nullptr, 0, MZ_DEFAULT_COMPRESSION)) {
                            file_added = true;
                        } else {
                            snprintf(status_message, msg_size, "Error: Failed to add '%s' to zip.", filename);
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

    mz_zip_writer_finalize_archive(&zip_archive);
    mz_zip_writer_end(&zip_archive);
    return true;
}

// Main function to handle the export process
bool handle_export_template(const DiscoveredTemplate &selected_template, const char *version, char *status_message,
                            size_t msg_size) {
    char suggested_filename[MAX_PATH_LENGTH];
    snprintf(suggested_filename, sizeof(suggested_filename), "%s_%s%s.zip", version, selected_template.category,
             selected_template.optional_flag);

    const char *filter_patterns[1] = {"*.zip"};
    const char *save_path = tinyfd_saveFileDialog("Export Template", suggested_filename, 1, filter_patterns,
                                                  "ZIP archives");

    if (!save_path) {
        snprintf(status_message, msg_size, "Export canceled.");
        return false;
    }

    if (create_zip_from_template(save_path, selected_template, version, status_message, msg_size)) {
        snprintf(status_message, msg_size, "Template exported successfully!");
        return true;
    }
    // If it fails, create_zip_from_template will have already set the error message.
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
                       size_t msg_size) {
    mz_zip_archive zip_archive = {};
    if (!mz_zip_reader_init_file(&zip_archive, zip_path, 0)) {
        snprintf(error_message, msg_size, "Error: Could not read zip file.");
        return false;
    }

    char main_template_filename[MAX_PATH_LENGTH] = "";
    mz_uint num_files = mz_zip_reader_get_num_files(&zip_archive);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) continue;
        if (!file_stat.m_is_directory && strstr(file_stat.m_filename, ".json") && !
            strstr(file_stat.m_filename, "_lang")) {
            if (main_template_filename[0] != '\0') {
                snprintf(error_message, msg_size, "Error: Zip file contains multiple main template files.");
                mz_zip_reader_end(&zip_archive);
                return false;
            }
            strncpy(main_template_filename, file_stat.m_filename, sizeof(main_template_filename) - 1);
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

bool execute_import_from_zip(const char *zip_path, const char *version, const char *category, const char *flag,
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

        // Check if the file matches the old template structure for renaming
        if (strncmp(original_filename, old_base_filename, old_base_len) == 0) {
            const char *suffix = original_filename + old_base_len;
            snprintf(new_filename, sizeof(new_filename), "%s%s", new_base_filename, suffix);
        } else {
            // Not a core template file, extract with original name to preserve any extra files
            strncpy(new_filename, original_filename, sizeof(new_filename) - 1);
        }

        char dest_path[MAX_PATH_LENGTH];
        snprintf(dest_path, sizeof(dest_path), "%s%s", dest_dir, new_filename);
        if (!mz_zip_reader_extract_to_file(&zip_archive, i, dest_path, 0)) {
            snprintf(error_message, msg_size, "Error: Failed to extract '%s'.", original_filename);
            success = false;
            break;
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

    char command[MAX_PATH_LENGTH + 64];
#ifdef _WIN32
    path_to_windows_native(lang_path);
    snprintf(command, sizeof(command), "explorer /select,\"%s\"", lang_path);
#elif __APPLE__
    snprintf(command, sizeof(command), "open -R \"%s\"", lang_path);
#else // POSIX
    // Highlighting a file isn't a standard feature, so we open the parent directory.
    char parent_dir[MAX_PATH_LENGTH];
    if (get_parent_directory(lang_path, parent_dir, sizeof(parent_dir), 1)) {
        snprintf(command, sizeof(command), "xdg-open \"%s\"", parent_dir);
    } else {
        // Fallback if getting parent fails
        snprintf(command, sizeof(command), "xdg-open .");
    }
#endif
    system(command);
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
