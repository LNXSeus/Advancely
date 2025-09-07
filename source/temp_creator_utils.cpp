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
#include "path_utils.h" // For path_exists
#include "main.h" // For MAX_PATH_LENGTH

#ifdef _WIN32
#include <direct.h> // For _mkdir
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0755) // 0755 provides read/write/execute for owner, read/execute for others
#endif

// Local helper to check for invalid filename characters
static bool is_valid_filename_part(const char* part) {
    if (!part) return true; // An empty flag is valid
    for (const char* p = part; *p; ++p) {
        // Allow alphanumeric, underscore, and dot
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '.') {
            return false;
        }
    }
    return true;
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
static bool fs_copy_file(const char* src, const char* dest) {
    FILE* source_file = fopen(src, "rb");
    if (!source_file) return false;

    FILE* dest_file = fopen(dest, "wb");
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

// TODO: Remove
// void fs_ensure_directory_exists(const char *path) {
//     char *path_copy = strdup(path);
//     if (!path_copy) return;
//
//     // Iterate through the path and create each directory level
//     for (char *p = path_copy + 1; *p; p++) {
//         if (*p == '/' || *p == '\\') {
//             char original_char = *p;
//             *p = '\0'; // Temporarily terminate the string
//
//             // Check if directory exists, if not, create it
//             struct stat st;
//             memset(&st, 0, sizeof(st));
//             if (stat(path_copy, &st) == -1) {
//                 MKDIR(path_copy);
//             }
//
//             *p = original_char; // Restore the slash
//         }
//     }
//     free(path_copy);
//     path_copy = nullptr;
// }

void fs_create_empty_template_file(const char *path) {
    FILE *file = fopen(path, "w");
    if (!file) {
        log_message(LOG_ERROR,"[TEMP CREATE UTILS] Failed to create template file: %s\n", path);
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
    log_message(LOG_INFO,"[TEMP CREATE UTILS] Created template file: %s\n", path);
}

void fs_create_empty_lang_file(const char *path) {
    FILE *file = fopen(path, "w");
    if (!file) {
        log_message(LOG_ERROR,"[TEMP CREATE UTILS] Failed to create language file: %s\n", path);
        return;
    }
    fputs("{\n}\n", file);
    fclose(file);
    log_message(LOG_INFO,"[TEMP CREATE UTILS] Created language file: %s\n", path);
}

bool validate_and_create_template(const char* version, const char* category, const char* flag, char* error_message, size_t error_msg_size) {
    // 1. Validate Inputs
    if (!category || category[0] == '\0') {
        snprintf(error_message, error_msg_size, "Error: Category name cannot be empty.");
        return false;
    }
    if (!is_valid_filename_part(category)) {
        snprintf(error_message, error_msg_size, "Error: Category contains invalid characters.\nOnly letters, numbers, and underscores are allowed.");
        return false;
    }
    if (!is_valid_filename_part(flag)) {
        snprintf(error_message, error_msg_size, "Error: Optional Flag contains invalid characters.\nOnly letters, numbers, underscores, and dots are allowed.");
        return false;
    }

    // 2. Construct Paths
    char version_filename[64];
    strncpy(version_filename, version, sizeof(version_filename) - 1);
    for (char *p = version_filename; *p; p++) {
        if (*p == '.') *p = '_';
    }

    char base_path[MAX_PATH_LENGTH];
    snprintf(base_path, sizeof(base_path), "resources/templates/%s/%s/%s_%s%s",
             version, category, version_filename, category, flag);

    char template_path[MAX_PATH_LENGTH];
    char lang_path[MAX_PATH_LENGTH];
    snprintf(template_path, sizeof(template_path), "%s.json", base_path);
    snprintf(lang_path, sizeof(lang_path), "%s_lang.json", base_path);

    // 3. Check if Template Already Exists
    if (path_exists(template_path)) {
        snprintf(error_message, error_msg_size, "Error: A template with this Version, Category, and Flag already exists.");
        return false;
    }

    // 4. Create Directory and Files
    char dir_path[MAX_PATH_LENGTH];
    snprintf(dir_path, sizeof(dir_path), "resources/templates/%s/%s", version, category);
    fs_ensure_directory_exists(dir_path);
    fs_create_empty_template_file(template_path);
    fs_create_empty_lang_file(lang_path);

    return true;
}

bool copy_template_files(const char* src_version, const char* src_category, const char* src_flag,
                         const char* dest_version, const char* dest_category, const char* dest_flag,
                         char* error_message, size_t error_msg_size) {
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
    if (strcmp(src_version, dest_version) == 0 && strcmp(src_category, dest_category) == 0 && strcmp(src_flag, dest_flag) == 0) {
        snprintf(error_message, error_msg_size, "Error: New name must be different from the original.");
        return false;
    }

    // 2. Construct Source Paths
    char src_version_filename[64];
    strncpy(src_version_filename, src_version, sizeof(src_version_filename) - 1);
    for (char *p = src_version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char src_base_path[MAX_PATH_LENGTH];
    snprintf(src_base_path, sizeof(src_base_path), "resources/templates/%s/%s/%s_%s%s",
             src_version, src_category, src_version_filename, src_category, src_flag);

    char src_template_path[MAX_PATH_LENGTH], src_lang_path[MAX_PATH_LENGTH];
    snprintf(src_template_path, sizeof(src_template_path), "%s.json", src_base_path);
    snprintf(src_lang_path, sizeof(src_lang_path), "%s_lang.json", src_base_path);

    // 3. Construct Destination Paths
    char dest_version_filename[64];
    strncpy(dest_version_filename, dest_version, sizeof(dest_version_filename) - 1);
    for (char *p = dest_version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char dest_base_path[MAX_PATH_LENGTH];
    snprintf(dest_base_path, sizeof(dest_base_path), "resources/templates/%s/%s/%s_%s%s",
             dest_version, dest_category, dest_version_filename, dest_category, dest_flag);

    char dest_template_path[MAX_PATH_LENGTH], dest_lang_path[MAX_PATH_LENGTH];
    snprintf(dest_template_path, sizeof(dest_template_path), "%s.json", dest_base_path);
    snprintf(dest_lang_path, sizeof(dest_lang_path), "%s_lang.json", dest_base_path);

    // 4. Check if Destination Already Exists
    if (path_exists(dest_template_path)) {
        snprintf(error_message, error_msg_size, "Error: A template with the new name already exists.");
        return false;
    }

    // 5. Create Directory and Copy Files
    char dest_dir_path[MAX_PATH_LENGTH];
    snprintf(dest_dir_path, sizeof(dest_dir_path), "resources/templates/%s/%s", dest_version, dest_category);
    fs_ensure_directory_exists(dest_dir_path);

    if (!fs_copy_file(src_template_path, dest_template_path)) {
        snprintf(error_message, error_msg_size, "Error: Failed to copy template file.");
        return false;
    }
    if (path_exists(src_lang_path)) {
        if (!fs_copy_file(src_lang_path, dest_lang_path)) {
            // This is not a critical failure, but we should warn the user.
            log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to copy language file for %s.\n", src_category);
        }
    }

    return true;
}


bool delete_template_files(const char* version, const char* category, const char* flag) {
    // Construct paths
    char version_filename[64];
    strncpy(version_filename, version, sizeof(version_filename) - 1);
    for (char *p = version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char base_path[MAX_PATH_LENGTH];
    snprintf(base_path, sizeof(base_path), "resources/templates/%s/%s/%s_%s%s",
             version, category, version_filename, category, flag);

    char template_path[MAX_PATH_LENGTH], lang_path[MAX_PATH_LENGTH];
    snprintf(template_path, sizeof(template_path), "%s.json", base_path);
    snprintf(lang_path, sizeof(lang_path), "%s_lang.json", base_path);

    bool success = true;
    if (remove(template_path) != 0) {
        log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to delete template file: %s\n", template_path);
        success = false;
    } else {
        log_message(LOG_INFO, "[TEMP CREATE UTILS] Deleted template file: %s\n", template_path);
    }

    if (path_exists(lang_path)) {
        if (remove(lang_path) != 0) {
            log_message(LOG_ERROR, "[TEMP CREATE UTILS] Failed to delete lang file: %s\n", lang_path);
            success = false;
        } else {
            log_message(LOG_INFO, "[TEMP CREATE UTILS] Deleted lang file: %s\n", lang_path);
        }
    }

    return success;
}

