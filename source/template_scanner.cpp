// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 02.09.2025.
//

#include "template_scanner.h"
#include "logger.h"
#include "path_utils.h" // For path_exists
#include "file_utils.h" // For cJSON_from_file
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
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

// Helper to read manual positions from JSON
void parse_manual_pos(cJSON *parent_json, const char *key, ManualPos *pos) {
    pos->is_set = false;
    pos->is_hidden_in_layout = false;
    pos->x = 0.0f;
    pos->y = 0.0f;
    pos->anchor = ANCHOR_TOP_LEFT; // Top left as default

    cJSON *pos_obj = cJSON_GetObjectItemCaseSensitive(parent_json, key);
    if (pos_obj && cJSON_IsObject(pos_obj)) {
        cJSON *x_val = cJSON_GetObjectItemCaseSensitive(pos_obj, "x");
        cJSON *y_val = cJSON_GetObjectItemCaseSensitive(pos_obj, "y");

        if (cJSON_IsNumber(x_val) && cJSON_IsNumber(y_val)) {
            pos->x = fminf(fmaxf((float) x_val->valuedouble, -MANUAL_POS_MAX), MANUAL_POS_MAX);
            pos->y = fminf(fmaxf((float) y_val->valuedouble, -MANUAL_POS_MAX), MANUAL_POS_MAX);
            pos->is_set = true;
        }

        cJSON *anchor_val = cJSON_GetObjectItemCaseSensitive(pos_obj, "anchor");
        if (cJSON_IsNumber(anchor_val)) {
            int a = anchor_val->valueint;
            if (a >= ANCHOR_TOP_LEFT && a <= ANCHOR_BOTTOM_RIGHT) {
                pos->anchor = (AnchorPoint) a;
            }
        }

        pos->is_hidden_in_layout = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(pos_obj, "hidden"));
    }
}

// Helper to convert version string "1.16.1" to "1_16_1"
static void version_to_filename_format(const char *version_in, char *version_out, size_t max_len) {
    strncpy(version_out, version_in, max_len - 1);
    version_out[max_len - 1] = '\0';
    for (char *p = version_out; *p; p++) {
        if (*p == '.') *p = '_'; // Convert dots to underscores
    }
}

// Checks if a template JSON file contains any manual layout data (position keys or decorations).
static bool template_has_layout_data(const char *file_path) {
    cJSON *json = cJSON_from_file(file_path);
    if (!json) return false;

    // Check for a non-empty "decorations" array
    cJSON *decorations = cJSON_GetObjectItem(json, "decorations");
    if (decorations && cJSON_IsArray(decorations) && cJSON_GetArraySize(decorations) > 0) {
        cJSON_Delete(json);
        return true;
    }

    // Helper lambda to check if a JSON object has any position keys
    auto has_pos_keys = [](cJSON *obj) -> bool {
        return cJSON_GetObjectItem(obj, "icon_pos") || cJSON_GetObjectItem(obj, "text_pos") ||
               cJSON_GetObjectItem(obj, "progress_pos");
    };

    // Check trackable sections for any items (or their criteria) with position keys
    // These can be JSON arrays or objects depending on the template format
    const char *section_keys[] = {"advancements", "stats", "unlocks", "custom_goals", "multi_stage_goals"};
    for (int k = 0; k < 5; k++) {
        cJSON *section = cJSON_GetObjectItem(json, section_keys[k]);
        if (!section || (!cJSON_IsArray(section) && !cJSON_IsObject(section))) continue;
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, section) {
            if (has_pos_keys(item)) {
                cJSON_Delete(json);
                return true;
            }
            // Check nested criteria (advancements/stats have a "criteria" object with keyed entries)
            cJSON *criteria = cJSON_GetObjectItem(item, "criteria");
            if (criteria && cJSON_IsObject(criteria)) {
                cJSON *criterion = nullptr;
                cJSON_ArrayForEach(criterion, criteria) {
                    if (has_pos_keys(criterion)) {
                        cJSON_Delete(json);
                        return true;
                    }
                }
            }
        }
    }

    cJSON_Delete(json);
    return false;
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
    // --- WINDOWS IMPLEMENTATION ---
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

                // --- Phase 1: Find main template files ---
                const char *ext = strrchr(filename, '.');
                // Universal filters for language, notes, and non-json files
                if (!ext || strcmp(ext, ".json") != 0 || strstr(filename, "_lang") || strstr(filename, "_notes"))
                    continue;

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

                const char *flag_start = base_name + strlen(expected_prefix);

                DiscoveredTemplate dt = {};
                strncpy(dt.category, category, sizeof(dt.category) - 1);
                dt.category[sizeof(dt.category) - 1] = '\0';
                strncpy(dt.optional_flag, flag_start, sizeof(dt.optional_flag) - 1);
                dt.optional_flag[sizeof(dt.optional_flag) - 1] = '\0';

                // --- Phase 2: Find all associated language files for this template ---
                WIN32_FIND_DATAA find_lang_data;
                HANDLE h_find_lang = FindFirstFileA(file_search_path, &find_lang_data);
                if (h_find_lang != INVALID_HANDLE_VALUE) {
                    do {
                        const char *lang_filename = D_FILENAME(find_lang_data);
                        const char *lang_part = strstr(lang_filename, "_lang");
                        if (lang_part) {
                            char lang_base_name[MAX_PATH_LENGTH];
                            strncpy(lang_base_name, lang_filename, lang_part - lang_filename);
                            lang_base_name[lang_part - lang_filename] = '\0';

                            // This is the crucial check: ensure the base name is an EXACT match
                            if (strcmp(base_name, lang_base_name) == 0) {
                                if (strcmp(lang_part, "_lang.json") == 0) {
                                    dt.available_lang_flags.emplace_back(""); // Default
                                } else if (strncmp(lang_part, "_lang_", strlen("_lang_")) == 0) {
                                    const char *lang_flag_start = lang_part + strlen("_lang_");
                                    const char *lang_flag_end = strstr(lang_flag_start, ".json");
                                    if (lang_flag_end) {
                                        dt.available_lang_flags.emplace_back(
                                            lang_flag_start, lang_flag_end - lang_flag_start);
                                    }
                                }
                            }
                        }
                    } while (FindNextFileA(h_find_lang, &find_lang_data) != 0);
                    FindClose(h_find_lang);
                }

                if (dt.available_lang_flags.empty()) {
                    dt.available_lang_flags.emplace_back("");
                }
                std::sort(dt.available_lang_flags.begin(), dt.available_lang_flags.end());

                // Check if the template has manual layout data
                char template_file_path[MAX_PATH_LENGTH];
                snprintf(template_file_path, sizeof(template_file_path), "%s/%s", cat_path_str, filename);
                dt.has_layout = template_has_layout_data(template_file_path);

                found_templates_vec.push_back(std::move(dt));
            } while (FindNextFileA(h_find_file, &find_file_data) != 0);
            FindClose(h_find_file);
        }
    } while (FindNextFileA(h_find_cat, &find_cat_data) != 0);
    FindClose(h_find_cat);

#else // POSIX (Linux/macOS) implementation with Vector buffering fix
    DIR *version_dir = opendir(base_path);
    if (!version_dir) return;
    struct dirent *cat_entry;
    while ((cat_entry = readdir(version_dir)) != nullptr) {
        if (cat_entry->d_type == DT_DIR && strcmp(cat_entry->d_name, ".") != 0 && strcmp(cat_entry->d_name, "..") !=
            0) {
            const char *category = cat_entry->d_name;
            char cat_path_str[MAX_PATH_LENGTH];
            snprintf(cat_path_str, sizeof(cat_path_str), "%s/%s", base_path, category);

            DIR *cat_dir = opendir(cat_path_str);
            if (!cat_dir) continue;

            // Read all files into memory first
            // This avoids the nested rewinddir/readdir bug where the inner loop
            // consumes the stream meant for the outer loop on some filesystems.
            std::vector<std::string> file_list;
            struct dirent *file_entry;
            while ((file_entry = readdir(cat_dir)) != nullptr) {
                file_list.push_back(file_entry->d_name);
            }
            closedir(cat_dir); // Close immediately after reading

            // Iterate the list to find templates
            for (const auto &filename_str: file_list) {
                const char *filename = filename_str.c_str();

                // --- Phase 1: Find main template files ---
                const char *ext = strrchr(filename, '.');
                if (!ext || strcmp(ext, ".json") != 0 || strstr(filename, "_lang") || strstr(filename, "_notes"))
                    continue;

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

                const char *flag_start = base_name + strlen(expected_prefix);

                DiscoveredTemplate dt = {};
                strncpy(dt.category, category, sizeof(dt.category) - 1);
                dt.category[sizeof(dt.category) - 1] = '\0';
                strncpy(dt.optional_flag, flag_start, sizeof(dt.optional_flag) - 1);
                dt.optional_flag[sizeof(dt.optional_flag) - 1] = '\0';

                // --- Phase 2: Find all associated language files for THIS template ---
                // Iterate the SAME file_list from memory
                for (const auto &lang_filename_str: file_list) {
                    const char *lang_filename = lang_filename_str.c_str();
                    const char *lang_part = strstr(lang_filename, "_lang");
                    if (lang_part) {
                        char lang_base_name[MAX_PATH_LENGTH];
                        strncpy(lang_base_name, lang_filename, lang_part - lang_filename);
                        lang_base_name[lang_part - lang_filename] = '\0';

                        // This is the crucial check: ensure the base name is an EXACT match
                        if (strcmp(base_name, lang_base_name) == 0) {
                            if (strcmp(lang_part, "_lang.json") == 0) {
                                dt.available_lang_flags.emplace_back(""); // Default
                            } else if (strncmp(lang_part, "_lang_", strlen("_lang_")) == 0) {
                                const char *lang_flag_start = lang_part + strlen("_lang_");
                                const char *lang_flag_end = strstr(lang_flag_start, ".json");
                                if (lang_flag_end) {
                                    dt.available_lang_flags.emplace_back(
                                        lang_flag_start, lang_flag_end - lang_flag_start);
                                }
                            }
                        }
                    }
                }

                if (dt.available_lang_flags.empty()) {
                    dt.available_lang_flags.emplace_back("");
                }
                std::sort(dt.available_lang_flags.begin(), dt.available_lang_flags.end());

                // Check if the template has manual layout data
                char template_file_path[MAX_PATH_LENGTH];
                snprintf(template_file_path, sizeof(template_file_path), "%s/%s", cat_path_str, filename);
                dt.has_layout = template_has_layout_data(template_file_path);

                found_templates_vec.push_back(std::move(dt));
            }
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

void free_discovered_templates(DiscoveredTemplate **templates, int *count) {
    if (*templates) {
        delete[] *templates;
        *templates = nullptr;
    }
    *count = 0;
}

// ---- Template Goal Hash (FNV-1a 64-bit) ----

static const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
static const uint64_t FNV_PRIME = 1099511628211ULL;

static uint64_t fnv1a_bytes(uint64_t hash, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

static uint64_t fnv1a_str(uint64_t hash, const char *str) {
    if (!str) return hash;
    return fnv1a_bytes(hash, str, strlen(str));
}

static uint64_t fnv1a_int(uint64_t hash, int value) {
    return fnv1a_bytes(hash, &value, sizeof(value));
}

// Hash a linked_goals array: each entry has root_name, optional stage_id, optional parent_root
static uint64_t hash_linked_goals(uint64_t hash, cJSON *linked_goals) {
    if (!linked_goals || !cJSON_IsArray(linked_goals)) return hash;
    cJSON *lg = nullptr;
    cJSON_ArrayForEach(lg, linked_goals) {
        cJSON *rn = cJSON_GetObjectItem(lg, "root_name");
        if (rn && rn->valuestring) hash = fnv1a_str(hash, rn->valuestring);
        cJSON *sid = cJSON_GetObjectItem(lg, "stage_id");
        if (sid && sid->valuestring) hash = fnv1a_str(hash, sid->valuestring);
        cJSON *pr = cJSON_GetObjectItem(lg, "parent_root");
        if (pr && pr->valuestring) hash = fnv1a_str(hash, pr->valuestring);
    }
    return hash;
}

// Hash the linked_goal_mode field ("and" or "or") if present
static uint64_t hash_linked_goal_mode(uint64_t hash, cJSON *obj) {
    cJSON *mode = cJSON_GetObjectItem(obj, "linked_goal_mode");
    if (mode && cJSON_IsString(mode)) hash = fnv1a_str(hash, mode->valuestring);
    return hash;
}

uint64_t compute_template_goal_hash(const char *template_file_path) {
    if (!template_file_path) return 0;

    cJSON *json = cJSON_from_file(template_file_path);
    if (!json) return 0;

    uint64_t hash = FNV_OFFSET_BASIS;

    // --- Advancements (object keyed by root_name) ---
    cJSON *advancements = cJSON_GetObjectItem(json, "advancements");
    if (advancements && cJSON_IsObject(advancements)) {
        cJSON *adv = nullptr;
        cJSON_ArrayForEach(adv, advancements) {
            hash = fnv1a_str(hash, adv->string); // root_name is the key
            // is_recipe flag affects tracking (recipes vs real advancements)
            cJSON *recipe = cJSON_GetObjectItem(adv, "is_recipe");
            if (recipe && cJSON_IsTrue(recipe)) hash = fnv1a_int(hash, 1);
            // Criteria keys (sub-criteria names matter for tracking)
            cJSON *criteria = cJSON_GetObjectItem(adv, "criteria");
            if (criteria && cJSON_IsObject(criteria)) {
                cJSON *crit = nullptr;
                cJSON_ArrayForEach(crit, criteria) {
                    hash = fnv1a_str(hash, crit->string); // criterion key
                    cJSON *target = cJSON_GetObjectItem(crit, "target");
                    if (target && cJSON_IsNumber(target)) hash = fnv1a_int(hash, target->valueint);
                }
            }
        }
    }

    // --- Stats (object keyed by category ID) ---
    cJSON *stats = cJSON_GetObjectItem(json, "stats");
    if (stats && cJSON_IsObject(stats)) {
        cJSON *stat = nullptr;
        cJSON_ArrayForEach(stat, stats) {
            hash = fnv1a_str(hash, stat->string); // category key e.g. "stat_category:log_collection"
            // Single-stat categories have root_name + target at top level
            cJSON *rn = cJSON_GetObjectItem(stat, "root_name");
            if (rn && rn->valuestring) hash = fnv1a_str(hash, rn->valuestring);
            cJSON *target = cJSON_GetObjectItem(stat, "target");
            if (target && cJSON_IsNumber(target)) hash = fnv1a_int(hash, target->valueint);
            // Multi-stat categories have criteria with sub-stat keys + targets
            cJSON *criteria = cJSON_GetObjectItem(stat, "criteria");
            if (criteria && cJSON_IsObject(criteria)) {
                cJSON *crit = nullptr;
                cJSON_ArrayForEach(crit, criteria) {
                    hash = fnv1a_str(hash, crit->string); // sub-stat key
                    cJSON *ct = cJSON_GetObjectItem(crit, "target");
                    if (ct && cJSON_IsNumber(ct)) hash = fnv1a_int(hash, ct->valueint);
                    // Sub-stat criteria can also have linked_goals + mode
                    hash = hash_linked_goals(hash, cJSON_GetObjectItem(crit, "linked_goals"));
                    hash = hash_linked_goal_mode(hash, crit);
                }
            }
            // Stat category linked_goals + mode (for auto-completion)
            hash = hash_linked_goals(hash, cJSON_GetObjectItem(stat, "linked_goals"));
            hash = hash_linked_goal_mode(hash, stat);
        }
    }

    // --- Unlocks (array of objects with root_name) ---
    cJSON *unlocks = cJSON_GetObjectItem(json, "unlocks");
    if (unlocks && cJSON_IsArray(unlocks)) {
        cJSON *unlock = nullptr;
        cJSON_ArrayForEach(unlock, unlocks) {
            cJSON *rn = cJSON_GetObjectItem(unlock, "root_name");
            if (rn && rn->valuestring) hash = fnv1a_str(hash, rn->valuestring);
        }
    }

    // --- Custom goals (array, JSON key is "custom") ---
    cJSON *custom = cJSON_GetObjectItem(json, "custom");
    if (custom && cJSON_IsArray(custom)) {
        cJSON *cg = nullptr;
        cJSON_ArrayForEach(cg, custom) {
            cJSON *rn = cJSON_GetObjectItem(cg, "root_name");
            if (rn && rn->valuestring) hash = fnv1a_str(hash, rn->valuestring);
            cJSON *target = cJSON_GetObjectItem(cg, "target");
            if (target && cJSON_IsNumber(target)) hash = fnv1a_int(hash, target->valueint);
            // Linked goals only apply when target <= 0 (manual/infinite custom goals)
            int target_val = (target && cJSON_IsNumber(target)) ? target->valueint : 0;
            if (target_val <= 0) {
                hash = hash_linked_goals(hash, cJSON_GetObjectItem(cg, "linked_goals"));
                hash = hash_linked_goal_mode(hash, cg);
            }
        }
    }

    // --- Multi-stage goals (array) ---
    cJSON *ms_goals = cJSON_GetObjectItem(json, "multi_stage_goals");
    if (ms_goals && cJSON_IsArray(ms_goals)) {
        cJSON *msg = nullptr;
        cJSON_ArrayForEach(msg, ms_goals) {
            cJSON *rn = cJSON_GetObjectItem(msg, "root_name");
            if (rn && rn->valuestring) hash = fnv1a_str(hash, rn->valuestring);
            cJSON *stages = cJSON_GetObjectItem(msg, "stages");
            if (stages && cJSON_IsArray(stages)) {
                cJSON *stage = nullptr;
                cJSON_ArrayForEach(stage, stages) {
                    cJSON *sid = cJSON_GetObjectItem(stage, "stage_id");
                    if (sid && sid->valuestring) hash = fnv1a_str(hash, sid->valuestring);
                    cJSON *type = cJSON_GetObjectItem(stage, "type");
                    if (type && type->valuestring) hash = fnv1a_str(hash, type->valuestring);
                    cJSON *srn = cJSON_GetObjectItem(stage, "root_name");
                    if (srn && srn->valuestring) hash = fnv1a_str(hash, srn->valuestring);
                    cJSON *parent = cJSON_GetObjectItem(stage, "parent_advancement");
                    if (parent && parent->valuestring) hash = fnv1a_str(hash, parent->valuestring);
                    cJSON *starget = cJSON_GetObjectItem(stage, "target");
                    if (starget && cJSON_IsNumber(starget)) hash = fnv1a_int(hash, starget->valueint);
                }
            }
        }
    }

    // --- Counter goals (array) ---
    cJSON *counters = cJSON_GetObjectItem(json, "counter_goals");
    if (counters && cJSON_IsArray(counters)) {
        cJSON *ctr = nullptr;
        cJSON_ArrayForEach(ctr, counters) {
            cJSON *rn = cJSON_GetObjectItem(ctr, "root_name");
            if (rn && rn->valuestring) hash = fnv1a_str(hash, rn->valuestring);
            hash = hash_linked_goals(hash, cJSON_GetObjectItem(ctr, "linked_goals"));
        }
    }

    cJSON_Delete(json);
    return hash;
}
