//
// Created by Linus on 02.09.2025.
//

#include "template_scanner.h"
#include "logger.h"
#include "path_utils.h" // For path_exists
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include "settings_utils.h" // For version checking

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

void scan_for_templates(const char *version_str, DiscoveredTemplate **out_templates, int *out_count) {
    *out_templates = nullptr;
    *out_count = 0;
    if (!version_str || version_str[0] == '\0') return;

    // Determine if we are scanning for a legacy version up front
    MC_Version version_enum = settings_get_version_from_string(version_str);
    bool is_legacy_version = (version_enum <= MC_VERSION_1_6_4);


    std::vector<DiscoveredTemplate> found_templates_vec;
    char base_path[MAX_PATH_LENGTH];
    snprintf(base_path, sizeof(base_path), "%s/templates/%s", get_resources_path(), version_str);

#ifdef _WIN32
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s/*", base_path);
    WIN32_FIND_DATAA find_cat_data;
    HANDLE h_find_cat = FindFirstFileA(search_path, &find_cat_data);
    if (h_find_cat == INVALID_HANDLE_VALUE) return;

    do {
        if ((find_cat_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(find_cat_data.cFileName, ".") != 0 &&
            strcmp(find_cat_data.cFileName, "..") != 0) {
            const char *category = find_cat_data.cFileName;
            char cat_path_str[MAX_PATH_LENGTH];
            snprintf(cat_path_str, sizeof(cat_path_str), "%s/%s", base_path, category);

            char file_search_path[MAX_PATH_LENGTH];
            snprintf(file_search_path, sizeof(file_search_path), "%s/*", cat_path_str);

            WIN32_FIND_DATAA find_file_data;
            HANDLE h_find_file = FindFirstFileA(file_search_path, &find_file_data);
            if (h_find_file == INVALID_HANDLE_VALUE) continue;

            do {
                const char *filename = D_FILENAME(find_file_data);
#else // POSIX
    DIR* version_dir = opendir(base_path);
    if (!version_dir) return;
    struct dirent* cat_entry;
    while ((cat_entry = readdir(version_dir)) != nullptr) {
        if (cat_entry->d_type == DT_DIR && strcmp(cat_entry->d_name, ".") != 0 && strcmp(cat_entry->d_name, "..") != 0) {
            const char* category = cat_entry->d_name;
            char cat_path_str[MAX_PATH_LENGTH];
            snprintf(cat_path_str, sizeof(cat_path_str), "%s/%s", base_path, category);
            DIR* cat_dir = opendir(cat_path_str);
            if (!cat_dir) continue;
            struct dirent* file_entry;
            while ((file_entry = readdir(cat_dir)) != nullptr) {
                const char* filename = D_FILENAME(file_entry);
#endif
                // --- Phase 1: Find main template files ---
                const char* ext = strrchr(filename, '.');
                // Universal filters for language, notes, and non-json files
                if (!ext || strcmp(ext, ".json") != 0 || strstr(filename, "_lang") || strstr(filename, "_notes")) continue;

                // Legacy-only filter for _snapshot files
                if (is_legacy_version && strstr(filename, "_snapshot")) continue;

                char base_name[MAX_PATH_LENGTH];
                strncpy(base_name, filename, ext - filename);
                base_name[ext - filename] = '\0';

                char version_fmt[64];
                version_to_filename_format(version_str, version_fmt, sizeof(version_fmt));

                char expected_prefix[MAX_PATH_LENGTH];
                snprintf(expected_prefix, sizeof(expected_prefix), "%s_%s", version_fmt, category);
                if (strncmp(base_name, expected_prefix, strlen(expected_prefix)) != 0) continue;

                const char* flag_start = base_name + strlen(expected_prefix);

                DiscoveredTemplate dt = {};
                strncpy(dt.category, category, sizeof(dt.category) - 1);
                strncpy(dt.optional_flag, flag_start, sizeof(dt.optional_flag) - 1);

                // --- Phase 2: Find all associated language files for this template ---
                #ifdef _WIN32
                    WIN32_FIND_DATAA find_lang_data;
                    HANDLE h_find_lang = FindFirstFileA(file_search_path, &find_lang_data);
                    if (h_find_lang != INVALID_HANDLE_VALUE) {
                        do {
                            const char* lang_filename = D_FILENAME(find_lang_data);
                #else
                    rewinddir(cat_dir);
                    struct dirent* lang_entry;
                    while ((lang_entry = readdir(cat_dir)) != nullptr) {
                        const char* lang_filename = lang_entry->d_name;
                #endif
                        const char* lang_part = strstr(lang_filename, "_lang");
                        if (lang_part) {
                            char lang_base_name[MAX_PATH_LENGTH];
                            strncpy(lang_base_name, lang_filename, lang_part - lang_filename);
                            lang_base_name[lang_part - lang_filename] = '\0';

                            // This is the crucial check: ensure the base name is an EXACT match
                            if (strcmp(base_name, lang_base_name) == 0) {
                                if (strcmp(lang_part, "_lang.json") == 0) {
                                    dt.available_lang_flags.emplace_back(""); // Default
                                } else if (strncmp(lang_part, "_lang_", strlen("_lang_")) == 0) {
                                    const char* lang_flag_start = lang_part + strlen("_lang_");
                                    const char* lang_flag_end = strstr(lang_flag_start, ".json");
                                    if (lang_flag_end) {
                                        dt.available_lang_flags.emplace_back(lang_flag_start, lang_flag_end - lang_flag_start);
                                    }
                                }
                            }
                        }
                #ifdef _WIN32
                        } while (FindNextFileA(h_find_lang, &find_lang_data) != 0);
                        FindClose(h_find_lang);
                    }
                #else
                    }
                #endif

                if (dt.available_lang_flags.empty()) {
                    dt.available_lang_flags.emplace_back("");
                }
                std::sort(dt.available_lang_flags.begin(), dt.available_lang_flags.end());
                found_templates_vec.push_back(std::move(dt));

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

    if (!found_templates_vec.empty()) {
        *out_count = found_templates_vec.size();
        *out_templates = new DiscoveredTemplate[*out_count];
        for (size_t i = 0; i < found_templates_vec.size(); ++i) {
            (*out_templates)[i] = std::move(found_templates_vec[i]);
        }
    }
}

void free_discovered_templates(DiscoveredTemplate** templates, int* count) {
    if (*templates) {
        delete[] *templates;
        *templates = nullptr;
    }
    *count = 0;
}