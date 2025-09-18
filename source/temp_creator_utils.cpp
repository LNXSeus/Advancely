//
// Created by Linus on 04.07.2025.
//

#include "temp_creator_utils.h"
#include "logger.h"
#include <cstdio>
#include <cstdlib> // For free()
#include <cstring>
#include <sys/stat.h> // For stat and mkdir
#include <cctype> // For isalnum

#include "file_utils.h"
#include "path_utils.h" // For path_exists
#include "main.h" // For MAX_PATH_LENGTH
#include "template_scanner.h"
#include "file_utils.h" // For cJSON_from_file
#include "settings_utils.h" // For version checking

#ifdef _WIN32
#include <direct.h> // For _mkdir
#include <windows.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0755) // 0755 provides read/write/execute for owner, read/execute for others
#include <dirent.h>
#endif

// Local helper to check for invalid filename characters
static bool is_valid_filename_part(const char *part) {
    if (!part) return true; // An empty flag is valid
    for (const char *p = part; *p; ++p) {
        // Allow alphanumeric, underscore, and dot
        if (!isalnum((unsigned char) *p) && *p != '_' && *p != '.') {
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

    // 2. Check for Name Collisions at Destination
    char new_combo[MAX_PATH_LENGTH];
    snprintf(new_combo, sizeof(new_combo), "%s%s", dest_category, dest_flag);

    DiscoveredTemplate *existing_templates = nullptr;
    int existing_count = 0;
    scan_for_templates(dest_version, &existing_templates, &existing_count);

    if (existing_templates) {
        for (int i = 0; i < existing_count; ++i) {
            char existing_combo[MAX_PATH_LENGTH];
            snprintf(existing_combo, sizeof(existing_combo), "%s%s",
                     existing_templates[i].category, existing_templates[i].optional_flag);

            if (strcmp(new_combo, existing_combo) == 0) {
                snprintf(error_message, error_msg_size,
                         "Error: Name collision. A template with category '%s' and flag '%s' already produces the name '%s'.",
                         existing_templates[i].category, existing_templates[i].optional_flag, existing_combo);
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
    char dest_version_filename[64];
    strncpy(dest_version_filename, dest_version, sizeof(dest_version_filename) - 1);
    for (char *p = dest_version_filename; *p; p++) { if (*p == '.') *p = '_'; }

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
    snprintf(src_category_path, sizeof(src_category_path), "%s/templates/%s/%s", get_resources_path(), src_version, src_category);

    char src_base_filename[MAX_PATH_LENGTH];
    snprintf(src_base_filename, sizeof(src_base_filename), "%s_%s%s", src_version_filename, src_category, src_flag);

#ifdef _WIN32
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s\\%s_lang*.json", src_category_path, src_base_filename);
    WIN32_FIND_DATAA find_data;
    HANDLE h_find = FindFirstFileA(search_path, &find_data);

    if (h_find != INVALID_HANDLE_VALUE) {
        do {
            const char* lang_suffix_start = strstr(find_data.cFileName, "_lang");
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
    snprintf(search_path, sizeof(search_path), "%s/%s*.json", category_path, base_filename);
    WIN32_FIND_DATAA find_data;
    HANDLE h_find = FindFirstFileA(search_path, &find_data);

    if (h_find != INVALID_HANDLE_VALUE) {
        do {
            char file_to_delete[MAX_PATH_LENGTH];
            snprintf(file_to_delete, sizeof(file_to_delete), "%s/%s", category_path, find_data.cFileName);
            if (remove(file_to_delete) != 0) {
                log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to delete file: %s\n", file_to_delete);
                all_success = false;
            } else {
                log_message(LOG_INFO, "[TEMP CREATE UTILS] Deleted file: %s\n", file_to_delete);
            }
        } while (FindNextFileA(h_find, &find_data) != 0);
        FindClose(h_find);
    }
#else // POSIX
    DIR* dir = opendir(category_path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strncmp(entry->d_name, base_filename, strlen(base_filename)) == 0 && strstr(entry->d_name, ".json")) {
                 char file_to_delete[MAX_PATH_LENGTH];
                 snprintf(file_to_delete, sizeof(file_to_delete), "%s/%s", category_path, entry->d_name);
                 if (remove(file_to_delete) != 0) {
                    log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to delete file: %s\n", file_to_delete);
                    all_success = false;
                 } else {
                    log_message(LOG_INFO, "[TEMP CREATE UTILS] Deleted file: %s\n", file_to_delete);
                 }
            }
        }
        closedir(dir);
    }
#endif
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
