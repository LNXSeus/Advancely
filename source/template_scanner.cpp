//
// Created by Linus on 02.09.2025.
//

#include "template_scanner.h"
#include "logger.h"
#include "path_utils.h" // For path_exists
#include <vector>
#include <string>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// Helper macro
#ifdef _WIN32
#define D_FILENAME(finddata) (finddata.cFileName)
#else // POSIX
#define D_FILENAME(dirent) ((dirent)->d_name)
#endif

// Helper to convert version string "1.16.1" to "1_16_1"
static void version_to_filename_format(const char *version_in, char *version_out, size_t max_len) {
    strncpy(version_out, version_in, max_len - 1);
    version_out[max_len - 1] = '\0';
    for (char *p = version_out; *p; p++) {
        if (*p == '.') *p = '_'; // Convert dots to underscores
    }
}

void scan_for_templates(const char* version_str, DiscoveredTemplate** out_templates, int* out_count) {
    *out_templates = nullptr;
    *out_count = 0;
    if (!version_str || version_str[0] == '\0') return;

    char base_path[MAX_PATH_LENGTH];
    snprintf(base_path, sizeof(base_path), "resources/templates/%s", version_str);

    std::vector<DiscoveredTemplate> found_templates;

#ifdef _WIN32
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s/*", base_path);
    WIN32_FIND_DATAA find_cat_data;
    HANDLE h_find_cat = FindFirstFileA(search_path, &find_cat_data);

    if (h_find_cat == INVALID_HANDLE_VALUE) return;

    do {
        if ((find_cat_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(find_cat_data.cFileName, ".") != 0 && strcmp(find_cat_data.cFileName, "..") != 0) {
            const char* category = find_cat_data.cFileName;
            char cat_path[MAX_PATH_LENGTH];
            snprintf(cat_path, sizeof(cat_path), "%s/%s/*", base_path, category);

            WIN32_FIND_DATAA find_file_data;
            HANDLE h_find_file = FindFirstFileA(cat_path, &find_file_data);
            if (h_find_file == INVALID_HANDLE_VALUE) continue;

            do {
                const char* filename = D_FILENAME(find_file_data);
#else // POSIX
    DIR* version_dir = opendir(base_path);
    if (!version_dir) return;
    struct dirent* cat_entry;
    while ((cat_entry = readdir(version_dir)) != nullptr) {
        if (cat_entry->d_type == DT_DIR && strcmp(cat_entry->d_name, ".") != 0 && strcmp(cat_entry->d_name, "..") != 0) {
            const char* category = cat_entry->d_name;
            char cat_path[MAX_PATH_LENGTH];
            snprintf(cat_path, sizeof(cat_path), "%s/%s", base_path, category);
            DIR* cat_dir = opendir(cat_path);
            if (!cat_dir) continue;
            struct dirent* file_entry;
            while ((file_entry = readdir(cat_dir)) != nullptr) {
                const char* filename = D_FILENAME(file_entry);
#endif
                // --- COMMON LOGIC FOR BOTH PLATFORMS ---
                const char* ext = strrchr(filename, '.');
                if (!ext || strcmp(ext, ".json") != 0) continue;
                if (strstr(filename, "_lang.json") || strstr(filename, "_snapshot.json") || strstr(filename, "_notes.json")) continue;

                char base_name[MAX_PATH_LENGTH];
                strncpy(base_name, filename, ext - filename);
                base_name[ext - filename] = '\0';

                char lang_path[MAX_PATH_LENGTH];
                snprintf(lang_path, sizeof(lang_path), "%s/%s/%s_lang.json", base_path, category, base_name);
                bool lang_exists = path_exists(lang_path);
                if (!lang_exists) {
                    log_message(LOG_INFO, "[TEMPLATE SCAN] WARNING: Template '%s' is missing a _lang.json file.\n", filename);
                }

                // --- REVERTED AND CORRECTED VALIDATION LOGIC ---
                char version_fmt[64];
                version_to_filename_format(version_str, version_fmt, sizeof(version_fmt));

                char expected_prefix[MAX_PATH_LENGTH];
                snprintf(expected_prefix, sizeof(expected_prefix), "%s_%s", version_fmt, category);
                size_t expected_prefix_len = strlen(expected_prefix);

                if (strncmp(base_name, expected_prefix, expected_prefix_len) != 0) {
                    log_message(LOG_ERROR, "[TEMPLATE SCAN] ERROR: Template file '%s' in category '%s' has a naming mismatch. Expected prefix: '%s'. Skipping.\n", filename, category, expected_prefix);
                    continue;
                }

                // The optional flag is simply the entire rest of the string after the prefix.
                const char* flag_start = base_name + expected_prefix_len;

                DiscoveredTemplate dt;
                strncpy(dt.category, category, sizeof(dt.category) - 1);
                dt.category[sizeof(dt.category) - 1] = '\0';
                strncpy(dt.optional_flag, flag_start, sizeof(dt.optional_flag) - 1);
                dt.optional_flag[sizeof(dt.optional_flag) - 1] = '\0';
                dt.lang_file_exists = lang_exists;
                found_templates.push_back(dt);
#ifdef _WIN32
            } while (FindNextFileA(h_find_file, &find_file_data) != 0);
            FindClose(h_find_file);
        }
    } while (FindNextFileA(h_find_cat, &find_cat_data) != 0);
    FindClose(h_find_cat);
#else // POSIX
            }
            closedir(cat_dir);
        }
    }
    closedir(version_dir);
#endif

    if (!found_templates.empty()) {
        *out_count = found_templates.size();
        *out_templates = (DiscoveredTemplate*)malloc(sizeof(DiscoveredTemplate) * (*out_count));
        for (size_t i = 0; i < found_templates.size(); ++i) {
            (*out_templates)[i] = found_templates[i];
        }
    }
}

// TODO: Remove
// void scan_for_templates(const char* version_str, DiscoveredTemplate** out_templates, int* out_count) {
//     *out_templates = nullptr;
//     *out_count = 0;
//     if (!version_str || version_str[0] == '\0') return;
//
//     char base_path[MAX_PATH_LENGTH];
//     snprintf(base_path, sizeof(base_path), "resources/templates/%s", version_str);
//
//     std::vector<DiscoveredTemplate> found_templates;
//
// #ifdef _WIN32
//     char search_path[MAX_PATH_LENGTH];
//     snprintf(search_path, sizeof(search_path), "%s/*", base_path);
//     WIN32_FIND_DATAA find_cat_data;
//     HANDLE h_find_cat = FindFirstFileA(search_path, &find_cat_data);
//
//     if (h_find_cat == INVALID_HANDLE_VALUE) return;
//
//     do {
//         if ((find_cat_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(find_cat_data.cFileName, ".") != 0 && strcmp(find_cat_data.cFileName, "..") != 0) {
//             const char* category = find_cat_data.cFileName;
//             char cat_path[MAX_PATH_LENGTH];
//             snprintf(cat_path, sizeof(cat_path), "%s/%s/*", base_path, category);
//
//             WIN32_FIND_DATAA find_file_data;
//             HANDLE h_find_file = FindFirstFileA(cat_path, &find_file_data);
//             if (h_find_file == INVALID_HANDLE_VALUE) continue;
//
//             do {
//                 const char* filename = D_FILENAME(find_file_data);
// #else // POSIX
//     DIR* version_dir = opendir(base_path);
//     if (!version_dir) return;
//     struct dirent* cat_entry;
//     while ((cat_entry = readdir(version_dir)) != nullptr) {
//         if (cat_entry->d_type == DT_DIR && strcmp(cat_entry->d_name, ".") != 0 && strcmp(cat_entry->d_name, "..") != 0) {
//             const char* category = cat_entry->d_name;
//             char cat_path[MAX_PATH_LENGTH];
//             snprintf(cat_path, sizeof(cat_path), "%s/%s", base_path, category);
//             DIR* cat_dir = opendir(cat_path);
//             if (!cat_dir) continue;
//             struct dirent* file_entry;
//             while ((file_entry = readdir(cat_dir)) != nullptr) {
//                 const char* filename = D_FILENAME(file_entry);
// #endif
//                 // --- COMMON LOGIC FOR BOTH PLATFORMS STARTS HERE ---
//                 const char* ext = strrchr(filename, '.');
//                 if (!ext || strcmp(ext, ".json") != 0) continue;
//                 if (strstr(filename, "_lang.json") || strstr(filename, "_snapshot.json") || strstr(filename, "_notes.json")) continue;
//
//                 char base_name[MAX_PATH_LENGTH];
//                 strncpy(base_name, filename, ext - filename);
//                 base_name[ext - filename] = '\0';
//
//                 char lang_path[MAX_PATH_LENGTH];
//                 snprintf(lang_path, sizeof(lang_path), "%s/%s/%s_lang.json", base_path, category, base_name);
//                 bool lang_exists = path_exists(lang_path);
//                 if (!lang_exists) {
//                     log_message(LOG_INFO, "[TEMPLATE SCAN] WARNING: Template '%s' is missing a _lang.json file.\n", filename);
//                 }
//
//                 char notes_path[MAX_PATH_LENGTH];
//                 snprintf(notes_path, sizeof(notes_path), "%s/%s/%s_notes.txt", base_path, category, base_name);
//                 if(path_exists(notes_path)) {
//                     // This is expected, do nothing.
//                 }
//
//                 char version_prefix[64];
//                 version_to_filename_format(version_str, version_prefix, sizeof(version_prefix));
//                 size_t version_prefix_len = strlen(version_prefix);
//
//                 if (strncmp(base_name, version_prefix, version_prefix_len) != 0) {
//                     log_message(LOG_ERROR, "[TEMPLATE SCAN] ERROR: Template '%s' in category '%s' does not match version '%s'. Skipping.\n", filename, category, version_str);
//                     continue;
//                 }
//
//                 const char* remainder_after_version = base_name + version_prefix_len;
//                 size_t category_len = strlen(category);
//
//                 if (strncmp(remainder_after_version, category, category_len) != 0) {
//                     log_message(LOG_ERROR, "[TEMPLATE SCAN] ERROR: Template file '%s' name does not match its category folder '%s'. Skipping.\n", filename, category);
//                     continue;
//                 }
//
//                 const char* optional_flag = remainder_after_version + category_len;
//
//                 // Whatever is left after the version and category is the optional flag
//                 DiscoveredTemplate dt;
//                 strncpy(dt.category, category, sizeof(dt.category) - 1);
//                 dt.category[sizeof(dt.category) - 1] = '\0';
//                 strncpy(dt.optional_flag, optional_flag, sizeof(dt.optional_flag) - 1);
//                 dt.optional_flag[sizeof(dt.optional_flag) - 1] = '\0';
//                 dt.lang_file_exists = lang_exists;
//                 found_templates.push_back(dt);
//                 // --- COMMON LOGIC FOR BOTH PLATFORMS ENDS HERE ---
// #ifdef _WIN32
//             } while (FindNextFileA(h_find_file, &find_file_data) != 0);
//             FindClose(h_find_file);
//         }
//     } while (FindNextFileA(h_find_cat, &find_cat_data) != 0);
//     FindClose(h_find_cat);
// #else // POSIX
//             }
//             closedir(cat_dir);
//         }
//     }
//     closedir(version_dir);
// #endif
//
//     if (!found_templates.empty()) {
//         *out_count = found_templates.size();
//         *out_templates = (DiscoveredTemplate*)malloc(sizeof(DiscoveredTemplate) * (*out_count));
//         for (size_t i = 0; i < found_templates.size(); ++i) {
//             (*out_templates)[i] = found_templates[i];
//         }
//     }
// }

// TODO: Remove
// void scan_for_templates(const char* version_str, DiscoveredTemplate** out_templates, int* out_count) {
//     *out_templates = nullptr;
//     *out_count = 0;
//     if (!version_str || version_str[0] == '\0') return;
//
//     char base_path[MAX_PATH_LENGTH];
//     snprintf(base_path, sizeof(base_path), "resources/templates/%s", version_str);
//
//     std::vector<DiscoveredTemplate> found_templates;
//
// #ifdef _WIN32
//     char search_path[MAX_PATH_LENGTH];
//     snprintf(search_path, sizeof(search_path), "%s/*", base_path);
//     WIN32_FIND_DATAA find_cat_data;
//     HANDLE h_find_cat = FindFirstFileA(search_path, &find_cat_data);
//
//     if (h_find_cat == INVALID_HANDLE_VALUE) return;
//
//     do {
//         if ((find_cat_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(find_cat_data.cFileName, ".") != 0 && strcmp(find_cat_data.cFileName, "..") != 0) {
//             const char* category = find_cat_data.cFileName;
//             char cat_path[MAX_PATH_LENGTH];
//             snprintf(cat_path, sizeof(cat_path), "%s/%s/*", base_path, category);
//
//             WIN32_FIND_DATAA find_file_data;
//             HANDLE h_find_file = FindFirstFileA(cat_path, &find_file_data);
//             if (h_find_file == INVALID_HANDLE_VALUE) continue;
//
//             do {
// #else // POSIX
//     DIR* version_dir = opendir(base_path);
//     if (!version_dir) return;
//     struct dirent* cat_entry;
//     while ((cat_entry = readdir(version_dir)) != nullptr) {
//         if (cat_entry->d_type == DT_DIR && strcmp(cat_entry->d_name, ".") != 0 && strcmp(cat_entry->d_name, "..") != 0) {
//             const char* category = cat_entry->d_name;
//             char cat_path[MAX_PATH_LENGTH];
//             snprintf(cat_path, sizeof(cat_path), "%s/%s", base_path, category);
//             DIR* cat_dir = opendir(cat_path);
//             if (!cat_dir) continue;
//             struct dirent* file_entry;
//             while ((file_entry = readdir(cat_dir)) != nullptr) {
// #endif
//                 const char* filename = D_FILENAME(file_entry);
//                 const char* ext = strrchr(filename, '.');
//                 if (!ext || strcmp(ext, ".json") != 0) continue;
//                 if (strstr(filename, "_lang.json") || strstr(filename, "_snapshot.json") || strstr(filename, "_notes.json")) continue;
//
//                 char base_name[MAX_PATH_LENGTH];
//                 strncpy(base_name, filename, ext - filename);
//                 base_name[ext - filename] = '\0';
//
//                 // Check for _lang.json file
//                 char lang_path[MAX_PATH_LENGTH];
//                 snprintf(lang_path, sizeof(lang_path), "%s/%s/%s_lang.json", base_path, category, base_name);
//                 bool lang_exists = path_exists(lang_path);
//                 if (!lang_exists) {
//                     log_message(LOG_INFO, "[TEMPLATE SCAN] WARNING: Template '%s' is missing a _lang.json file.\n", filename);
//                 }
//
//                 // Check for _notes.txt file (and ignore it for validation purposes)
//                 char notes_path[MAX_PATH_LENGTH];
//                 snprintf(notes_path, sizeof(notes_path), "%s/%s/%s_notes.txt", base_path, category, base_name);
//                 if(path_exists(notes_path)) {
//                     // This is expected, do nothing.
//                 }
//
//                 // TODO: Optional flag DOESN'T ALWAYS START WITH AN UNDERSCORE
//                 // Validate filename format: [version]_[category]_[optional_flag]
//                 char version_fmt[64];
//                 version_to_filename_format(version_str, version_fmt, sizeof(version_fmt));
//
//                 char expected_prefix[MAX_PATH_LENGTH];
//                 snprintf(expected_prefix, sizeof(expected_prefix), "%s_%s", version_fmt, category);
//
//                 if (strncmp(base_name, expected_prefix, strlen(expected_prefix)) != 0) {
//                     log_message(LOG_ERROR, "[TEMPLATE SCAN] ERROR: Template file '%s' in category '%s' has a naming mismatch. Expected prefix: '%s'. Skipping.\n", filename, category, expected_prefix);
//                     continue;
//                 }
//
//                 // TODO: Optional flag DOESN'T ALWAYS START WITH AN UNDERSCORE
//                 // Extract optional flag
//                 const char* flag_start = base_name + strlen(expected_prefix);
//                 if (*flag_start == '_') {
//                     flag_start++; // Skip the underscore
//                 }
//
//                 DiscoveredTemplate dt;
//                 strncpy(dt.category, category, sizeof(dt.category) - 1);
//                 strncpy(dt.optional_flag, flag_start, sizeof(dt.optional_flag) - 1);
//                 dt.lang_file_exists = lang_exists;
//                 found_templates.push_back(dt);
//
// #ifdef _WIN32
//             } while (FindNextFileA(h_find_file, &find_file_data) != 0);
//             FindClose(h_find_file);
//         }
//     } while (FindNextFileA(h_find_cat, &find_cat_data) != 0);
//     FindClose(h_find_cat);
// #else
//             }
//             closedir(cat_dir);
//         }
//     }
//     closedir(version_dir);
// #endif
//
//     if (!found_templates.empty()) {
//         *out_count = found_templates.size();
//         *out_templates = (DiscoveredTemplate*)malloc(sizeof(DiscoveredTemplate) * (*out_count));
//         for (size_t i = 0; i < found_templates.size(); ++i) {
//             (*out_templates)[i] = found_templates[i];
//         }
//     }
// }

void free_discovered_templates(DiscoveredTemplate** templates, int* count) {
    if (*templates) {
        free(*templates);
        *templates = nullptr;
    }
    *count = 0;
}
