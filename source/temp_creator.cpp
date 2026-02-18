// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 07.09.2025.
//

#include "temp_creator.h"

#include <algorithm>

#include "settings_utils.h"
#include "logger.h"
#include "template_scanner.h"
#include "temp_creator_utils.h"
#include "path_utils.h"
#include "global_event_handler.h"
#include "file_utils.h" // For cJSON_from_file
#include "dialog_utils.h" // For open_icon_file_dialog()

#include <vector>
#include <string>
#include <unordered_set> // For checking duplicates
#include <functional> // For std::function

#include "tinyfiledialogs.h"

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif

// Robustly opens a URL or local folder using SDL, falling back to system commands if needed.
/**
 * @brief Robustly opens a URL or local folder using SDL, falling back to system commands if needed.
 *
 * @param target The URL or local folder path to open.
 */
static void open_content(const char *target) {
    if (!target || target[0] == '\0') return;

    // Try SDL's cross-platform function first (Handles HTTPS links perfectly)
    if (SDL_OpenURL(target) == 0) {
        return; // Success
    }

    // Fallback for local directories if SDL_OpenURL fails (common on macOS/Linux for file paths)
#ifdef _WIN32
    // Windows: ShellExecute handles paths and URLs
    // (SDL_OpenURL usually maps to this anyway, but just in case)
    ShellExecuteA(nullptr, "open", target, nullptr, nullptr, SW_SHOW);
#elif defined(__APPLE__)
    // macOS: The 'open' command is non-blocking and handles both URLs and Paths
    char command[MAX_PATH_LENGTH + 16];
    snprintf(command, sizeof(command), "open \"%s\"", target);
    system(command);
#else
    // Linux: 'xdg-open' is the standard
    char command[MAX_PATH_LENGTH + 16];
    snprintf(command, sizeof(command), "xdg-open \"%s\"", target);
    system(command);
#endif
}

// Helper to check if a string ends with a specific suffix
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

// Local helper to check for invalid filename characters (for UI validation)
static bool is_valid_filename_part_for_ui(const char *part) {
    if (!part) return true; // An empty flag is valid
    for (const char *p = part; *p; ++p) {
        // Allow alphanumeric, underscore, dot and %
        if (!isalnum((unsigned char) *p) && *p != '_' && *p != '.' && *p != '%') {
            return false;
        }
    }
    return true;
}

// In-memory representation of a template for editing
struct EditorTrackableItem {
    char root_name[192];
    char display_name[192];
    char icon_path[256];
    int goal;
    bool is_hidden;
    bool in_2nd_row;
};

// A struct to hold a category (like an advancement) and its criteria
struct EditorTrackableCategory {
    char root_name[192];
    char display_name[192];
    char icon_path[256];
    bool is_hidden;
    bool in_2nd_row;
    bool is_recipe; // UI flag to distinguish recipes from advancements -> count towards progress percentage instead
    bool is_simple_stat; // UI flag to distinguish simple vs complex stats
    std::vector<EditorTrackableItem> criteria; // Criteria then are trackable items
};

// Structs for Multi-Stage Goal editing
struct EditorSubGoal {
    char stage_id[64];
    char display_text[192]; // is loaded from the lang file, not stored in the main template
    SubGoalType type;
    char parent_advancement[192];
    char root_name[192];
    int required_progress;
    char icon_path[256]; // Icon path for each stage
};

struct EditorMultiStageGoal {
    char root_name[192];
    char display_name[192]; // From lang file
    char icon_path[256];
    bool is_hidden;
    bool in_2nd_row;
    bool use_stage_icons;
    std::vector<EditorSubGoal> stages;
};

struct EditorTemplate {
    std::vector<EditorTrackableCategory> advancements;
    std::vector<EditorTrackableCategory> stats;
    std::vector<EditorTrackableItem> unlocks;
    std::vector<EditorTrackableItem> custom_goals;
    std::vector<EditorMultiStageGoal> multi_stage_goals;
};

// Helper function to compare two EditorTrackableItem structs
static bool are_editor_items_different(const EditorTrackableItem &a, const EditorTrackableItem &b) {
    return strcmp(a.root_name, b.root_name) != 0 ||
           strcmp(a.display_name, b.display_name) != 0 ||
           strcmp(a.icon_path, b.icon_path) != 0 ||
           a.goal != b.goal ||
           a.is_hidden != b.is_hidden ||
           a.in_2nd_row != b.in_2nd_row;
}

// Helper function to compare two EditorTrackableCategory structs
static bool are_editor_categories_different(const EditorTrackableCategory &a, const EditorTrackableCategory &b) {
    if (strcmp(a.root_name, b.root_name) != 0 ||
        strcmp(a.display_name, b.display_name) != 0 ||
        strcmp(a.icon_path, b.icon_path) != 0 ||
        a.is_hidden != b.is_hidden ||
        a.in_2nd_row != b.in_2nd_row ||
        a.is_recipe != b.is_recipe ||
        a.is_simple_stat != b.is_simple_stat ||
        a.criteria.size() != b.criteria.size()) {
        return true;
    }
    for (size_t i = 0; i < a.criteria.size(); ++i) {
        if (are_editor_items_different(a.criteria[i], b.criteria[i])) {
            return true;
        }
    }
    return false;
}

// Comparison helpers for multi-stage goals
static bool are_editor_sub_goals_different(const EditorSubGoal &a, const EditorSubGoal &b) {
    return strcmp(a.stage_id, b.stage_id) != 0 ||
           strcmp(a.display_text, b.display_text) != 0 ||
           a.type != b.type ||
           strcmp(a.parent_advancement, b.parent_advancement) != 0 ||
           strcmp(a.root_name, b.root_name) != 0 ||
           a.required_progress != b.required_progress ||
           strcmp(a.icon_path, b.icon_path) != 0;
}

static bool are_editor_multi_stage_goals_different(const EditorMultiStageGoal &a, const EditorMultiStageGoal &b) {
    if (strcmp(a.root_name, b.root_name) != 0 ||
        strcmp(a.display_name, b.display_name) != 0 ||
        strcmp(a.icon_path, b.icon_path) != 0 ||
        a.is_hidden != b.is_hidden ||
        a.in_2nd_row != b.in_2nd_row ||
        a.use_stage_icons != b.use_stage_icons ||
        a.stages.size() != b.stages.size()) {
        return true;
    }
    for (size_t i = 0; i < a.stages.size(); ++i) {
        if (are_editor_sub_goals_different(a.stages[i], b.stages[i])) {
            return true;
        }
    }
    return false;
}


// Main comparison function for the entire editor state
static bool are_editor_templates_different(const EditorTemplate &a, const EditorTemplate &b) {
    if (a.unlocks.size() != b.unlocks.size() ||
        a.custom_goals.size() != b.custom_goals.size() ||
        a.advancements.size() != b.advancements.size() ||
        a.stats.size() != b.stats.size() ||
        a.multi_stage_goals.size() != b.multi_stage_goals.size()) {
        return true;
    }
    for (size_t i = 0; i < a.unlocks.size(); ++i) {
        if (are_editor_items_different(a.unlocks[i], b.unlocks[i])) return true;
    }
    for (size_t i = 0; i < a.custom_goals.size(); ++i) {
        if (are_editor_items_different(a.custom_goals[i], b.custom_goals[i])) return true;
    }
    for (size_t i = 0; i < a.advancements.size(); ++i) {
        if (are_editor_categories_different(a.advancements[i], b.advancements[i])) return true;
    }
    for (size_t i = 0; i < a.stats.size(); ++i) {
        if (are_editor_categories_different(a.stats[i], b.stats[i])) return true;
    }
    for (size_t i = 0; i < a.multi_stage_goals.size(); ++i) {
        if (are_editor_multi_stage_goals_different(a.multi_stage_goals[i], b.multi_stage_goals[i])) return true;
    }
    return false;
}

// Helper to check for duplicate root_names in a vector of items
static bool has_duplicate_root_names(const std::vector<EditorTrackableItem> &items, char *error_message_buffer) {
    std::unordered_set<std::string> seen_names;
    for (const auto &item: items) {
        if (item.root_name[0] == '\0') {
            snprintf(error_message_buffer, 256, "Error: An item has an empty root name.");
            return true;
        }
        if (!seen_names.insert(item.root_name).second) {
            snprintf(error_message_buffer, 256, "Error: Duplicate root name found: '%s'", item.root_name);
            return true;
        }
    }
    return false;
}

// Helper to check for duplicate root_names in a vector of categories
static bool has_duplicate_category_root_names(const std::vector<EditorTrackableCategory> &items,
                                              char *error_message_buffer) {
    std::unordered_set<std::string> seen_names;
    for (const auto &item: items) {
        if (item.root_name[0] == '\0') {
            snprintf(error_message_buffer, 256, "Error: An advancement has an empty root name.");
            return true;
        }
        if (!seen_names.insert(item.root_name).second) {
            snprintf(error_message_buffer, 256, "Error: Duplicate advancement root name found: '%s'", item.root_name);
            return true;
        }
    }
    return false;
}

// Helper to check for duplicate root_names in a vector of multi-stage goals
static bool has_duplicate_ms_goal_root_names(const std::vector<EditorMultiStageGoal> &goals,
                                             char *error_message_buffer) {
    std::unordered_set<std::string> seen_names;
    for (const auto &goal: goals) {
        if (goal.root_name[0] == '\0') {
            snprintf(error_message_buffer, 256, "Error: A multi-stage goal has an empty root name.");
            return true;
        }
        if (!seen_names.insert(goal.root_name).second) {
            snprintf(error_message_buffer, 256, "Error: Duplicate multi-stage goal root name found: '%s'",
                     goal.root_name);
            return true;
        }
    }
    return false;
}

// Helper to check for duplicate stage IDs within a single multi-stage goal
static bool has_duplicate_stage_ids(const std::vector<EditorSubGoal> &stages, char *error_message_buffer) {
    std::unordered_set<std::string> seen_ids;
    for (const auto &stage: stages) {
        if (stage.stage_id[0] == '\0') {
            snprintf(error_message_buffer, 256, "Error: A stage has an empty ID.");
            return true;
        }
        if (!seen_ids.insert(stage.stage_id).second) {
            snprintf(error_message_buffer, 256, "Error: Duplicate stage ID found: '%s'", stage.stage_id);
            return true;
        }
    }
    return false;
}

// Helper to validate the structure of stages within multi-stage goals, especially asuring proper final stage
static bool validate_multi_stage_goal_stages(const std::vector<EditorMultiStageGoal> &goals,
                                             char *error_message_buffer) {
    for (const auto &goal: goals) {
        if (goal.stages.empty()) {
            // It's valid for a new goal to have no stages yet, so we don't flag this as an error.
            continue;
        }

        int final_stage_count = 0;
        int final_stage_index = -1;
        for (size_t i = 0; i < goal.stages.size(); ++i) {
            if (goal.stages[i].type == SUBGOAL_MANUAL) {
                final_stage_count++;
                final_stage_index = (int) i;
            }
        }

        // Rule 1: Must have one 'Final' stage.
        if (final_stage_count == 0) {
            snprintf(error_message_buffer, 256, "Error: Goal '%s' must have one stage of type 'Final'.",
                     goal.root_name);
            return false;
        }

        // Rule 3: Can only have one 'Final' stage.
        if (final_stage_count > 1) {
            snprintf(error_message_buffer, 256, "Error: Goal '%s' has more than one 'Final' stage.", goal.root_name);
            return false;
        }

        // Rule 2: The 'Final' stage must be the last one.
        if (final_stage_index != (int) goal.stages.size() - 1) {
            snprintf(error_message_buffer, 256, "Error: The 'Final' stage in goal '%s' must be the last in the list.",
                     goal.root_name);
            return false;
        }
    }
    return true;
}


// Helper to validate that all icon paths in a vector exist
static bool validate_icon_paths(const std::vector<EditorTrackableItem> &items, char *error_message_buffer) {
    for (const auto &item: items) {
        // Validate icon paths

        // If path is empty
        if (item.icon_path[0] == '\0') {
            snprintf(error_message_buffer, 256, "Error: Visible item '%s' is missing an icon path.",
                     item.root_name);
            return false;
        }
        // If path is non-empty
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, sizeof(full_path), "%s/icons/%s", get_application_dir(), item.icon_path);
        if (!path_exists(full_path)) {
            snprintf(error_message_buffer, 256, "Error: Icon file not found for '%s': '%s'", item.root_name,
                     item.icon_path);
            return false;
        }
    }
    return true;
}

// Helper to validate icon paths for multi-stage goals
static bool validate_ms_goal_icon_paths(const std::vector<EditorMultiStageGoal> &goals, char *error_message_buffer) {
    for (const auto &goal: goals) {
        // Validate icon paths

        // When path is empty
        if (goal.icon_path[0] == '\0') {
            snprintf(error_message_buffer, 256, "Error: Visible multi-stage goal '%s' is missing an icon path.",
                     goal.root_name);
            return false;
        }

        // When path is wrong
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, sizeof(full_path), "%s/icons/%s", get_application_dir(), goal.icon_path);
        if (!path_exists(full_path)) {
            snprintf(error_message_buffer, 256, "Error: Icon file not found for goal '%s': '%s'", goal.root_name,
                     goal.icon_path);
            return false;
        }

        // Validate Stage Icons if enabled
        if (goal.use_stage_icons) {
            for (const auto& stage : goal.stages) {
                if (stage.icon_path[0] == '\0') {
                    snprintf(error_message_buffer, 256, "Error: Stage '%s' in goal '%s' is missing an icon path.",
                             stage.stage_id, goal.root_name);
                    return false;
                }

                snprintf(full_path, sizeof(full_path), "%s/icons/%s", get_application_dir(), stage.icon_path);
                if (!path_exists(full_path)) {
                    snprintf(error_message_buffer, 256, "Error: Icon file not found for stage '%s': '%s'",
                             stage.stage_id, stage.icon_path);
                    return false;
                }
            }
        }
    }
    return true;
}

// Helper to validate icon paths for nested categories
static bool validate_category_icon_paths(const std::vector<EditorTrackableCategory> &categories,
                                         MC_Version version, char *error_message_buffer) {
    for (const auto &cat: categories) {
        // Check parent icon path

        if (cat.icon_path[0] == '\0') {
            // This is only an error if it's NOT a special legacy hidden stat.
            bool is_legacy_hidden_stat_exception = false;

            // The exception applies if:
            // 1. It's a legacy version.
            // 2. It's a simple stat with one criterion.
            // 3. That criterion has a goal of 0. (also has no icon ofc)
            if (version <= MC_VERSION_1_6_4 && cat.is_simple_stat && cat.criteria.size() == 1 && cat.criteria[0].goal ==
                0) {
                is_legacy_hidden_stat_exception = true;
            }

            if (!is_legacy_hidden_stat_exception) {
                snprintf(error_message_buffer, 256, "Error: Visible category '%s' is missing an icon path.",
                         cat.root_name);
                return false;
            }
        }
        // When path exists we validate correctness
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, sizeof(full_path), "%s/icons/%s", get_application_dir(), cat.icon_path);
        if (!path_exists(full_path)) {
            snprintf(error_message_buffer, 256, "Error: Icon file not found for '%s': '%s'", cat.root_name,
                     cat.icon_path);
            return false;
        }

        // Check criteria icon paths (excluding simple stats which inherit their icon)
        for (const auto &crit: cat.criteria) {
            // Validate icon paths

            // Check for empty path when it's empty and not a simple stat
            if (crit.icon_path[0] == '\0' && !cat.is_simple_stat) {
                snprintf(error_message_buffer, 256,
                         "Error: Visible criterion '%s' in category '%s' is missing an icon path.", crit.root_name,
                         cat.root_name);
                return false;
            }
            // check for incorrect path when it's not empty and complex stat
            if (crit.icon_path[0] != '\0') {
                char full_path[MAX_PATH_LENGTH];
                snprintf(full_path, sizeof(full_path), "%s/icons/%s", get_application_dir(), crit.icon_path);
                if (!path_exists(full_path)) {
                    snprintf(error_message_buffer, 256, "Error: Icon file not found for criterion '%s': '%s'",
                             crit.root_name, crit.icon_path);
                    return false;
                }
            }
        }
    }
    return true;
}

// Helper to parse a simple array like "unlocks" or "custom" from the template JSON
static void parse_editor_trackable_items(cJSON *json_array, std::vector<EditorTrackableItem> &item_vector,
                                         cJSON *lang_json, const char *lang_key_prefix) {
    item_vector.clear();
    if (!json_array) return;

    cJSON *item_json = nullptr;
    cJSON_ArrayForEach(item_json, json_array) {
        EditorTrackableItem new_item = {}; // Zero-initialize
        cJSON *root_name = cJSON_GetObjectItem(item_json, "root_name");
        cJSON *icon = cJSON_GetObjectItem(item_json, "icon");
        cJSON *target = cJSON_GetObjectItem(item_json, "target");
        cJSON *hidden = cJSON_GetObjectItem(item_json, "hidden");
        cJSON *in_2nd_row = cJSON_GetObjectItem(item_json, "in_2nd_row");

        if (cJSON_IsString(root_name)) {
            strncpy(new_item.root_name, root_name->valuestring, sizeof(new_item.root_name) - 1);
            new_item.root_name[sizeof(new_item.root_name) - 1] = '\0';
        }
        if (cJSON_IsString(icon)) {
            strncpy(new_item.icon_path, icon->valuestring, sizeof(new_item.icon_path) - 1);
            new_item.icon_path[sizeof(new_item.icon_path) - 1] = '\0';
        }
        if (cJSON_IsNumber(target)) new_item.goal = target->valueint;
        if (cJSON_IsBool(hidden)) new_item.is_hidden = cJSON_IsTrue(hidden);
        if (cJSON_IsBool(in_2nd_row)) new_item.in_2nd_row = cJSON_IsTrue(in_2nd_row);

        // Load display_name from lang file
        char lang_key[256];
        snprintf(lang_key, sizeof(lang_key), "%s%s", lang_key_prefix, new_item.root_name);
        cJSON *lang_entry = cJSON_GetObjectItem(lang_json, lang_key);
        if (cJSON_IsString(lang_entry)) {
            strncpy(new_item.display_name, lang_entry->valuestring, sizeof(new_item.display_name) - 1);
            new_item.display_name[sizeof(new_item.display_name) - 1] = '\0';
        } else {
            // Take root name if there's no lang entry, shouldn't be the case in editor itself
            strncpy(new_item.display_name, new_item.root_name, sizeof(new_item.display_name) - 1);
            new_item.display_name[sizeof(new_item.display_name) - 1] = '\0';
        }


        item_vector.push_back(new_item);
    }
}

// Helper to parse a category object like "advancements" or "stats"
static void parse_editor_trackable_categories(cJSON *json_object,
                                              std::vector<EditorTrackableCategory> &category_vector, cJSON *lang_json) {
    category_vector.clear();
    if (!json_object) return;

    cJSON *category_json = nullptr;
    cJSON_ArrayForEach(category_json, json_object) {
        EditorTrackableCategory new_cat = {}; // Zero initialize

        // Parse parent item properties
        strncpy(new_cat.root_name, category_json->string, sizeof(new_cat.root_name) - 1);
        new_cat.root_name[sizeof(new_cat.root_name) - 1] = '\0';
        cJSON *icon = cJSON_GetObjectItem(category_json, "icon");
        cJSON *hidden = cJSON_GetObjectItem(category_json, "hidden");
        cJSON *is_recipe_json = cJSON_GetObjectItem(category_json, "is_recipe");

        if (cJSON_IsString(icon)) {
            strncpy(new_cat.icon_path, icon->valuestring, sizeof(new_cat.icon_path) - 1);
            new_cat.icon_path[sizeof(new_cat.icon_path) - 1] = '\0';
        }
        if (cJSON_IsBool(hidden)) new_cat.is_hidden = cJSON_IsTrue(hidden);
        if (cJSON_IsBool(is_recipe_json)) new_cat.is_recipe = cJSON_IsTrue(is_recipe_json);

        char lang_key[256];
        char temp_root_name[291];
        strncpy(temp_root_name, new_cat.root_name, sizeof(temp_root_name) - 1);
        temp_root_name[sizeof(temp_root_name) - 1] = '\0';
        char *p = temp_root_name;

        // Replacing ":" and "/" with "." for lang key, ONLY for advancements
        while ((p = strpbrk(p, ":/")) != nullptr) *p = '.';

        snprintf(lang_key, sizeof(lang_key), "advancement.%s", temp_root_name);
        cJSON *lang_entry = cJSON_GetObjectItem(lang_json, lang_key);
        if (cJSON_IsString(lang_entry)) {
            strncpy(new_cat.display_name, lang_entry->valuestring, sizeof(new_cat.display_name) - 1);
            new_cat.display_name[sizeof(new_cat.display_name) - 1] = '\0';
        } else {
            strncpy(new_cat.display_name, new_cat.root_name, sizeof(new_cat.display_name) - 1);
            new_cat.display_name[sizeof(new_cat.display_name) - 1] = '\0';
        }


        // Parse the nested criteria using existing helper function
        cJSON *criteria_object = cJSON_GetObjectItem(category_json, "criteria");
        if (criteria_object) {
            // Advancements/Stats store criteria in an object, not an array
            std::vector<EditorTrackableItem> criteria_items;
            cJSON *criterion_json = nullptr;
            cJSON_ArrayForEach(criterion_json, criteria_object) {
                EditorTrackableItem new_crit = {};
                strncpy(new_crit.root_name, criterion_json->string, sizeof(new_crit.root_name) - 1);
                new_crit.root_name[sizeof(new_crit.root_name) - 1] = '\0';

                cJSON *crit_icon = cJSON_GetObjectItem(criterion_json, "icon");
                cJSON *crit_hidden = cJSON_GetObjectItem(criterion_json, "hidden");

                if (cJSON_IsString(crit_icon)) {
                    strncpy(new_crit.icon_path, crit_icon->valuestring, sizeof(new_crit.icon_path) - 1);
                    new_crit.icon_path[sizeof(new_crit.icon_path) - 1] = '\0';
                }
                if (cJSON_IsBool(crit_hidden)) new_crit.is_hidden = cJSON_IsTrue(crit_hidden);

                // Load display name for the criterion
                char crit_lang_key[512];
                snprintf(crit_lang_key, sizeof(crit_lang_key), "%s.criteria.%s", lang_key, new_crit.root_name);
                cJSON *crit_lang_entry = cJSON_GetObjectItem(lang_json, crit_lang_key);
                if (cJSON_IsString(crit_lang_entry)) {
                    strncpy(new_crit.display_name, crit_lang_entry->valuestring, sizeof(new_crit.display_name) - 1);
                    new_crit.display_name[sizeof(new_crit.display_name) - 1] = '\0';
                } else {
                    strncpy(new_crit.display_name, new_crit.root_name, sizeof(new_crit.display_name) - 1);
                    new_crit.display_name[sizeof(new_crit.display_name) - 1] = '\0';
                }

                criteria_items.push_back(new_crit);
            }
            new_cat.criteria = criteria_items;
        }
        category_vector.push_back(new_cat);
    }
}


// Specific parser for stats to handle simple vs complex structures
static void parse_editor_stats(cJSON *json_object, std::vector<EditorTrackableCategory> &category_vector,
                               cJSON *lang_json) {
    category_vector.clear();
    if (!json_object) return;

    cJSON *category_json = nullptr;
    cJSON_ArrayForEach(category_json, json_object) {
        EditorTrackableCategory new_cat = {}; // Zero initialize

        // Parse parent item properties
        strncpy(new_cat.root_name, category_json->string, sizeof(new_cat.root_name) - 1);
        new_cat.root_name[sizeof(new_cat.root_name) - 1] = '\0';
        cJSON *icon = cJSON_GetObjectItem(category_json, "icon");
        cJSON *hidden = cJSON_GetObjectItem(category_json, "hidden");
        cJSON *in_2nd_row = cJSON_GetObjectItem(category_json, "in_2nd_row");

        if (cJSON_IsString(icon)) {
            strncpy(new_cat.icon_path, icon->valuestring, sizeof(new_cat.icon_path) - 1);
            new_cat.icon_path[sizeof(new_cat.icon_path) - 1] = '\0';
        }
        if (cJSON_IsBool(hidden)) new_cat.is_hidden = cJSON_IsTrue(hidden);
        if (cJSON_IsBool(in_2nd_row)) new_cat.in_2nd_row = cJSON_IsTrue(in_2nd_row); // Only for main stat not sub-stats

        // Language file
        char cat_lang_key[256];
        snprintf(cat_lang_key, sizeof(cat_lang_key), "stat.%s", new_cat.root_name);
        cJSON *cat_lang_entry = cJSON_GetObjectItem(lang_json, cat_lang_key);
        if (cJSON_IsString(cat_lang_entry)) {
            strncpy(new_cat.display_name, cat_lang_entry->valuestring, sizeof(new_cat.display_name) - 1);
            new_cat.display_name[sizeof(new_cat.display_name) - 1] = '\0';
        } else {
            strncpy(new_cat.display_name, new_cat.root_name, sizeof(new_cat.display_name) - 1);
            new_cat.display_name[sizeof(new_cat.display_name) - 1] = '\0';
        }


        cJSON *criteria_object = cJSON_GetObjectItem(category_json, "criteria");
        if (criteria_object && criteria_object->child) {
            // Chase 1: Complex stat with a "criteria" block
            new_cat.is_simple_stat = false;
            cJSON *criterion_json = nullptr;
            cJSON_ArrayForEach(criterion_json, criteria_object) {
                EditorTrackableItem new_crit = {};
                strncpy(new_crit.root_name, criterion_json->string, sizeof(new_crit.root_name) - 1);
                new_crit.root_name[sizeof(new_crit.root_name) - 1] = '\0';

                cJSON *crit_icon = cJSON_GetObjectItem(criterion_json, "icon");
                cJSON *crit_hidden = cJSON_GetObjectItem(criterion_json, "hidden");
                cJSON *crit_target = cJSON_GetObjectItem(criterion_json, "target");

                if (cJSON_IsString(crit_icon)) {
                    strncpy(new_crit.icon_path, crit_icon->valuestring, sizeof(new_crit.icon_path) - 1);
                    new_crit.icon_path[sizeof(new_crit.icon_path) - 1] = '\0';
                }
                if (cJSON_IsBool(crit_hidden)) new_crit.is_hidden = cJSON_IsTrue(crit_hidden);
                if (cJSON_IsNumber(crit_target)) new_crit.goal = crit_target->valueint;

                // Stat criteria language file
                char crit_lang_key[512];
                snprintf(crit_lang_key, sizeof(crit_lang_key), "%s.criteria.%s", cat_lang_key, new_crit.root_name);
                cJSON *crit_lang_entry = cJSON_GetObjectItem(lang_json, crit_lang_key);
                if (cJSON_IsString(crit_lang_entry)) {
                    strncpy(new_crit.display_name, crit_lang_entry->valuestring, sizeof(new_crit.display_name) - 1);
                    new_crit.display_name[sizeof(new_crit.display_name) - 1] = '\0';
                } else {
                    strncpy(new_crit.display_name, new_crit.root_name, sizeof(new_crit.display_name) - 1);
                    new_crit.display_name[sizeof(new_crit.display_name) - 1] = '\0';
                }

                new_cat.criteria.push_back(new_crit);
            }
        } else {
            // Case 2: Simple stat without a "criteria" block
            new_cat.is_simple_stat = true;
            EditorTrackableItem new_crit = {};
            cJSON *stat_root_name = cJSON_GetObjectItem(category_json, "root_name");
            cJSON *target = cJSON_GetObjectItem(category_json, "target");

            if (cJSON_IsString(stat_root_name)) {
                strncpy(new_crit.root_name, stat_root_name->valuestring, sizeof(new_crit.root_name) - 1);
                new_crit.root_name[sizeof(new_crit.root_name) - 1] = '\0';
            } else {
                // Fallback for hidden MS goal stats (version <= 1.6.4)
                strncpy(new_crit.root_name, new_cat.root_name, sizeof(new_crit.root_name) - 1);
                new_crit.root_name[sizeof(new_crit.root_name) - 1] = '\0';
            }

            if (cJSON_IsNumber(target)) new_crit.goal = target->valueint;

            // For simple stats, the criterion's display name is the same as the category's
            new_cat.criteria.push_back(new_crit);
        }
        category_vector.push_back(new_cat);
    }
}

// Parser for multi-stage goals
static void parse_editor_multi_stage_goals(cJSON *json_array, std::vector<EditorMultiStageGoal> &goals_vector,
                                           cJSON *lang_json) {
    goals_vector.clear();
    if (!json_array) return;

    cJSON *goal_json;
    cJSON_ArrayForEach(goal_json, json_array) {
        EditorMultiStageGoal new_goal = {};

        cJSON *root_name = cJSON_GetObjectItem(goal_json, "root_name");
        cJSON *icon = cJSON_GetObjectItem(goal_json, "icon");
        cJSON *hidden = cJSON_GetObjectItem(goal_json, "hidden");
        cJSON *in_2nd_row = cJSON_GetObjectItem(goal_json, "in_2nd_row");
        cJSON *use_stage_icons = cJSON_GetObjectItem(goal_json, "use_stage_icons");

        if (cJSON_IsString(root_name)) {
            strncpy(new_goal.root_name, root_name->valuestring, sizeof(new_goal.root_name) - 1);
            new_goal.root_name[sizeof(new_goal.root_name) - 1] = '\0';
        }
        if (cJSON_IsString(icon)) {
            strncpy(new_goal.icon_path, icon->valuestring, sizeof(new_goal.icon_path) - 1);
            new_goal.icon_path[sizeof(new_goal.icon_path) - 1] = '\0';
        }
        if (cJSON_IsBool(hidden)) new_goal.is_hidden = cJSON_IsTrue(hidden);
        if (cJSON_IsBool(in_2nd_row)) new_goal.in_2nd_row = cJSON_IsTrue(in_2nd_row);
        if (cJSON_IsBool(use_stage_icons)) new_goal.use_stage_icons = cJSON_IsTrue(use_stage_icons);

        // Language file
        char goal_lang_key[256];
        snprintf(goal_lang_key, sizeof(goal_lang_key), "multi_stage_goal.%s.display_name", new_goal.root_name);
        cJSON *goal_lang_entry = cJSON_GetObjectItem(lang_json, goal_lang_key);
        if (cJSON_IsString(goal_lang_entry)) {
            strncpy(new_goal.display_name, goal_lang_entry->valuestring, sizeof(new_goal.display_name) - 1);
            new_goal.display_name[sizeof(new_goal.display_name) - 1] = '\0';
        } else {
            strncpy(new_goal.display_name, new_goal.root_name, sizeof(new_goal.display_name) - 1);
            new_goal.display_name[sizeof(new_goal.display_name) - 1] = '\0';
        }

        cJSON *stages_array = cJSON_GetObjectItem(goal_json, "stages");
        if (stages_array) {
            cJSON *stage_json;
            cJSON_ArrayForEach(stage_json, stages_array) {
                EditorSubGoal new_stage = {};

                cJSON *stage_id = cJSON_GetObjectItem(stage_json, "stage_id");
                cJSON *type = cJSON_GetObjectItem(stage_json, "type");
                cJSON *parent_adv = cJSON_GetObjectItem(stage_json, "parent_advancement");
                cJSON *stage_root = cJSON_GetObjectItem(stage_json, "root_name");
                cJSON *target = cJSON_GetObjectItem(stage_json, "target");

                if (cJSON_IsString(stage_id)) {
                    strncpy(new_stage.stage_id, stage_id->valuestring, sizeof(new_stage.stage_id) - 1);
                    new_stage.stage_id[sizeof(new_stage.stage_id) - 1] = '\0';
                }
                if (cJSON_IsString(parent_adv)) {
                    strncpy(new_stage.parent_advancement, parent_adv->valuestring,
                            sizeof(new_stage.parent_advancement) - 1);
                    new_stage.parent_advancement[sizeof(new_stage.parent_advancement) - 1] = '\0';
                }
                if (cJSON_IsString(stage_root)) {
                    strncpy(new_stage.root_name, stage_root->valuestring, sizeof(new_stage.root_name) - 1);
                    new_stage.root_name[sizeof(new_stage.root_name) - 1] = '\0';
                }
                if (cJSON_IsNumber(target)) new_stage.required_progress = target->valueint;

                // Per-stage icon file if used
                cJSON *stage_icon = cJSON_GetObjectItem(stage_json, "icon");
                if (cJSON_IsString(stage_icon)) {
                    strncpy(new_stage.icon_path, stage_icon->valuestring, sizeof(new_stage.icon_path) - 1);
                    new_stage.icon_path[sizeof(new_stage.icon_path) - 1] = '\0';
                }

                // Language file
                char stage_lang_key[512];
                snprintf(stage_lang_key, sizeof(stage_lang_key), "multi_stage_goal.%s.stage.%s", new_goal.root_name,
                         new_stage.stage_id);
                cJSON *stage_lang_entry = cJSON_GetObjectItem(lang_json, stage_lang_key);
                if (cJSON_IsString(stage_lang_entry)) {
                    strncpy(new_stage.display_text, stage_lang_entry->valuestring, sizeof(new_stage.display_text) - 1);
                    new_stage.display_text[sizeof(new_stage.display_text) - 1] = '\0';
                } else {
                    strncpy(new_stage.display_text, new_stage.stage_id, sizeof(new_stage.display_text) - 1);
                    new_stage.display_text[sizeof(new_stage.display_text) - 1] = '\0';
                }

                if (cJSON_IsString(type)) {
                    if (strcmp(type->valuestring, "stat") == 0) new_stage.type = SUBGOAL_STAT;
                    else if (strcmp(type->valuestring, "advancement") == 0) new_stage.type = SUBGOAL_ADVANCEMENT;
                    else if (strcmp(type->valuestring, "unlock") == 0) new_stage.type = SUBGOAL_UNLOCK;
                    else if (strcmp(type->valuestring, "criterion") == 0) new_stage.type = SUBGOAL_CRITERION;
                    else new_stage.type = SUBGOAL_MANUAL;
                }
                new_goal.stages.push_back(new_stage);
            }
        }
        goals_vector.push_back(new_goal);
    }
}

// Main function to load a whole template for editing
static bool load_template_for_editing(const char *version, const DiscoveredTemplate &template_info,
                                      const std::string &lang_flag,
                                      EditorTemplate &editor_data, char *status_message_buffer) {
    editor_data.advancements.clear();
    editor_data.stats.clear();
    editor_data.unlocks.clear();
    editor_data.custom_goals.clear();
    editor_data.multi_stage_goals.clear();

    char version_filename[64]; // Replacing . with _
    strncpy(version_filename, version, sizeof(version_filename) - 1);
    version_filename[sizeof(version_filename) - 1] = '\0';
    for (char *p = version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char base_path_str[MAX_PATH_LENGTH];
    snprintf(base_path_str, sizeof(base_path_str), "%s/templates/%s/%s/%s_%s%s", get_resources_path(),
             version, template_info.category, version_filename, template_info.category, template_info.optional_flag);

    char template_path[MAX_PATH_LENGTH];
    snprintf(template_path, sizeof(template_path), "%s.json", base_path_str);

    char lang_path[MAX_PATH_LENGTH];
    char lang_suffix[70];
    if (!lang_flag.empty()) {
        snprintf(lang_suffix, sizeof(lang_suffix), "_%s", lang_flag.c_str());
    } else {
        lang_suffix[0] = '\0';
    }
    snprintf(lang_path, sizeof(lang_path), "%s_lang%s.json", base_path_str, lang_suffix);

    cJSON *root = cJSON_from_file(template_path);
    if (!root) {
        snprintf(status_message_buffer, 256, "Error: Could not load template file for editing.");
        return false;
    }

    cJSON *lang_json = cJSON_from_file(lang_path);
    if (!lang_json) {
        lang_json = cJSON_CreateObject(); // Create an empty object if it doesn't exist to prevent crashes
    }

    parse_editor_trackable_categories(cJSON_GetObjectItem(root, "advancements"), editor_data.advancements, lang_json);
    parse_editor_stats(cJSON_GetObjectItem(root, "stats"), editor_data.stats, lang_json);
    parse_editor_trackable_items(cJSON_GetObjectItem(root, "unlocks"), editor_data.unlocks, lang_json, "unlock.");
    parse_editor_trackable_items(cJSON_GetObjectItem(root, "custom"), editor_data.custom_goals, lang_json, "custom.");
    parse_editor_multi_stage_goals(cJSON_GetObjectItem(root, "multi_stage_goals"), editor_data.multi_stage_goals,
                                   lang_json);

    cJSON_Delete(root);
    cJSON_Delete(lang_json);
    return true;
}

// Helper to serialize a vector of items back into a cJSON array, for unlocks and custom goals
static void serialize_editor_trackable_items(cJSON *parent, const char *key,
                                             const std::vector<EditorTrackableItem> &item_vector) {
    cJSON *array = cJSON_CreateArray();
    for (const auto &item: item_vector) {
        cJSON *item_json = cJSON_CreateObject();
        cJSON_AddStringToObject(item_json, "root_name", item.root_name);
        cJSON_AddStringToObject(item_json, "icon", item.icon_path);
        if (item.goal != 0) {
            // Only add target if it's not 0 (default for unlocks)
            cJSON_AddNumberToObject(item_json, "target", item.goal);
        }
        if (item.is_hidden) {
            cJSON_AddBoolToObject(item_json, "hidden", item.is_hidden);
        }
        if (item.in_2nd_row) {
            cJSON_AddBoolToObject(item_json, "in_2nd_row", true);
        }
        cJSON_AddItemToArray(array, item_json);
    }
    cJSON_AddItemToObject(parent, key, array);
}

// Helper to serialize a vector of categories back into a cJSON object
static void serialize_editor_trackable_categories(cJSON *parent, const char *key,
                                                  const std::vector<EditorTrackableCategory> &category_vector) {
    cJSON *cat_object = cJSON_CreateObject();
    for (const auto &cat: category_vector) {
        cJSON *cat_json = cJSON_CreateObject();
        cJSON_AddStringToObject(cat_json, "icon", cat.icon_path);
        if (cat.is_hidden) {
            cJSON_AddBoolToObject(cat_json, "hidden", cat.is_hidden);
        }
        // Save the is_recipe flag if it's true
        if (cat.is_recipe) {
            cJSON_AddBoolToObject(cat_json, "is_recipe", true);
        }

        // Create the nested criteria object
        cJSON *criteria_object = cJSON_CreateObject();
        for (const auto &crit: cat.criteria) {
            cJSON *crit_json = cJSON_CreateObject();
            cJSON_AddStringToObject(crit_json, "icon", crit.icon_path);
            if (crit.is_hidden) {
                cJSON_AddBoolToObject(crit_json, "hidden", crit.is_hidden);
            }
            cJSON_AddItemToObject(criteria_object, crit.root_name, crit_json);
        }
        cJSON_AddItemToObject(cat_json, "criteria", criteria_object);

        cJSON_AddItemToObject(cat_object, cat.root_name, cat_json);
    }
    cJSON_AddItemToObject(parent, key, cat_object);
}

// Specific serializer for stats
static void serialize_editor_stats(cJSON *parent, const std::vector<EditorTrackableCategory> &category_vector) {
    cJSON *cat_object = cJSON_CreateObject();
    for (const auto &cat: category_vector) {
        cJSON *cat_json = cJSON_CreateObject();
        cJSON_AddStringToObject(cat_json, "icon", cat.icon_path);
        if (cat.is_hidden) {
            cJSON_AddBoolToObject(cat_json, "hidden", cat.is_hidden);
        }
        if (cat.in_2nd_row) {
            cJSON_AddBoolToObject(cat_json, "in_2nd_row", true);
        }

        if (cat.is_simple_stat && !cat.criteria.empty()) {
            const auto &crit = cat.criteria[0];
            cJSON_AddStringToObject(cat_json, "root_name", crit.root_name);
            if (crit.goal != 0) {
                cJSON_AddNumberToObject(cat_json, "target", crit.goal);
            }
        } else {
            // Complex (multi-stat)
            cJSON *criteria_object = cJSON_CreateObject();
            for (const auto &crit: cat.criteria) {
                cJSON *crit_json = cJSON_CreateObject();
                cJSON_AddStringToObject(crit_json, "icon", crit.icon_path);
                if (crit.is_hidden) {
                    cJSON_AddBoolToObject(crit_json, "hidden", crit.is_hidden);
                }
                if (crit.goal != 0) {
                    cJSON_AddNumberToObject(crit_json, "target", crit.goal);
                }
                cJSON_AddItemToObject(criteria_object, crit.root_name, crit_json);
            }
            cJSON_AddItemToObject(cat_json, "criteria", criteria_object);
        }
        cJSON_AddItemToObject(cat_object, cat.root_name, cat_json);
    }
    cJSON_AddItemToObject(parent, "stats", cat_object);
}

// Serializer for multi-stage goals
static void serialize_editor_multi_stage_goals(cJSON *parent, const std::vector<EditorMultiStageGoal> &goals_vector) {
    cJSON *goals_array = cJSON_CreateArray();
    for (const auto &goal: goals_vector) {
        cJSON *goal_json = cJSON_CreateObject();
        cJSON_AddStringToObject(goal_json, "root_name", goal.root_name);
        cJSON_AddStringToObject(goal_json, "icon", goal.icon_path);
        if (goal.is_hidden) {
            cJSON_AddBoolToObject(goal_json, "hidden", goal.is_hidden);
        }
        if (goal.in_2nd_row) {
            cJSON_AddBoolToObject(goal_json, "in_2nd_row", true);
        }
        // Use icons per-stage
        if (goal.use_stage_icons) {
            cJSON_AddBoolToObject(goal_json, "use_stage_icons", true);
        }

        cJSON *stages_array = cJSON_CreateArray();
        for (const auto &stage: goal.stages) {
            cJSON *stage_json = cJSON_CreateObject();
            cJSON_AddStringToObject(stage_json, "stage_id", stage.stage_id);

            // Write Stage Icon if enabled and present
            if (goal.use_stage_icons && stage.icon_path[0] != '\0') {
                cJSON_AddStringToObject(stage_json, "icon", stage.icon_path);
            }

            const char *type_str = "manual";
            switch (stage.type) {
                case SUBGOAL_STAT: type_str = "stat";
                    break;
                case SUBGOAL_ADVANCEMENT: type_str = "advancement";
                    break;
                case SUBGOAL_UNLOCK: type_str = "unlock";
                    break;
                case SUBGOAL_CRITERION: type_str = "criterion";
                    break;
                case SUBGOAL_MANUAL: type_str = "final";
                    break;
            }
            cJSON_AddStringToObject(stage_json, "type", type_str);

            if (stage.type == SUBGOAL_CRITERION) {
                cJSON_AddStringToObject(stage_json, "parent_advancement", stage.parent_advancement);
            }
            cJSON_AddStringToObject(stage_json, "root_name", stage.root_name);
            if (stage.type != SUBGOAL_MANUAL) {
                cJSON_AddNumberToObject(stage_json, "target", stage.required_progress);
            }

            cJSON_AddItemToArray(stages_array, stage_json);
        }
        cJSON_AddItemToObject(goal_json, "stages", stages_array);
        cJSON_AddItemToArray(goals_array, goal_json);
    }
    cJSON_AddItemToObject(parent, "multi_stage_goals", goals_array);
}

// Main function to save the in-memory editor data back to a file
static bool save_template_from_editor(const char *version, const DiscoveredTemplate &template_info,
                                      const std::string &lang_flag,
                                      EditorTemplate &editor_data, char *status_message_buffer) {
    char version_filename[64];
    strncpy(version_filename, version, sizeof(version_filename) - 1);
    version_filename[sizeof(version_filename) - 1] = '\0';
    for (char *p = version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char base_path_str[MAX_PATH_LENGTH];
    snprintf(base_path_str, sizeof(base_path_str), "%s/templates/%s/%s/%s_%s%s", get_resources_path(),
             version, template_info.category, version_filename, template_info.category, template_info.optional_flag);

    char template_path[MAX_PATH_LENGTH];
    snprintf(template_path, sizeof(template_path), "%s.json", base_path_str);

    char lang_path[MAX_PATH_LENGTH];
    char lang_suffix[70];
    if (!lang_flag.empty()) {
        snprintf(lang_suffix, sizeof(lang_suffix), "_%s", lang_flag.c_str());
    } else {
        lang_suffix[0] = '\0';
    }
    snprintf(lang_path, sizeof(lang_path), "%s_lang%s.json", base_path_str, lang_suffix);

    // Read the existing file to preserve sections we aren't editing yet
    cJSON *root = cJSON_from_file(template_path);
    if (!root) {
        root = cJSON_CreateObject(); // Create a new object if the file is empty or doesn't exist
    }

    // Replace all editable sections with our new data
    cJSON_DeleteItemFromObject(root, "advancements");
    cJSON_DeleteItemFromObject(root, "stats");
    cJSON_DeleteItemFromObject(root, "unlocks");
    cJSON_DeleteItemFromObject(root, "custom");
    cJSON_DeleteItemFromObject(root, "multi_stage_goals");
    serialize_editor_trackable_categories(root, "advancements", editor_data.advancements);
    serialize_editor_stats(root, editor_data.stats);
    serialize_editor_trackable_items(root, "unlocks", editor_data.unlocks);
    serialize_editor_trackable_items(root, "custom", editor_data.custom_goals);
    serialize_editor_multi_stage_goals(root, editor_data.multi_stage_goals);

    // Write the modified JSON object back to the file
    FILE *file = fopen(template_path, "w");
    if (file) {
        char *json_str = cJSON_Print(root);
        if (json_str) {
            fputs(json_str, file);
            free(json_str);
        }
        fclose(file);
        // No message here, returns true on success
    } else {
        snprintf(status_message_buffer, 256, "Error: Failed to open template file for writing.");
        cJSON_Delete(root);
        return false;
    }

    cJSON_Delete(root);


    // SAVE LANG FILE WITH SPECIFIC ORDER
    cJSON *lang_json = cJSON_CreateObject();

    // 1. Advancements (Parent then Criteria)
    for (const auto &cat: editor_data.advancements) {
        char cat_lang_key[256];
        char temp_root_name[192];
        strncpy(temp_root_name, cat.root_name, sizeof(temp_root_name) - 1);
        temp_root_name[sizeof(temp_root_name) - 1] = '\0';
        char *p = temp_root_name;
        while ((p = strpbrk(p, ":/")) != nullptr) *p = '.';
        snprintf(cat_lang_key, sizeof(cat_lang_key), "advancement.%s", temp_root_name);
        cJSON_AddStringToObject(lang_json, cat_lang_key, cat.display_name);

        for (const auto &crit: cat.criteria) {
            char crit_lang_key[512];
            snprintf(crit_lang_key, sizeof(crit_lang_key), "%s.criteria.%s", cat_lang_key, crit.root_name);
            cJSON_AddStringToObject(lang_json, crit_lang_key, crit.display_name);
        }
    }

    // 2. Stats (Parent then Criteria)
    for (const auto &cat: editor_data.stats) {
        char cat_lang_key[256];
        snprintf(cat_lang_key, sizeof(cat_lang_key), "stat.%s", cat.root_name);
        cJSON_AddStringToObject(lang_json, cat_lang_key, cat.display_name);
        if (!cat.is_simple_stat) {
            for (const auto &crit: cat.criteria) {
                char crit_lang_key[512];
                snprintf(crit_lang_key, sizeof(crit_lang_key), "%s.criteria.%s", cat_lang_key, crit.root_name);
                cJSON_AddStringToObject(lang_json, crit_lang_key, crit.display_name);
            }
        }
    }

    // 3. Unlocks
    for (const auto &item: editor_data.unlocks) {
        char lang_key[256];
        snprintf(lang_key, sizeof(lang_key), "unlock.%s", item.root_name);
        cJSON_AddStringToObject(lang_json, lang_key, item.display_name);
    }

    // 4. Custom Goals
    for (const auto &item: editor_data.custom_goals) {
        char lang_key[256];
        snprintf(lang_key, sizeof(lang_key), "custom.%s", item.root_name);
        cJSON_AddStringToObject(lang_json, lang_key, item.display_name);
    }

    // 5. Multi-Stage Goals (Parent then Stages)
    for (const auto &goal: editor_data.multi_stage_goals) {
        char goal_lang_key[256];
        snprintf(goal_lang_key, sizeof(goal_lang_key), "multi_stage_goal.%s.display_name", goal.root_name);
        cJSON_AddStringToObject(lang_json, goal_lang_key, goal.display_name);
        for (const auto &stage: goal.stages) {
            char stage_lang_key[512];
            snprintf(stage_lang_key, sizeof(stage_lang_key), "multi_stage_goal.%s.stage.%s", goal.root_name,
                     stage.stage_id);
            cJSON_AddStringToObject(lang_json, stage_lang_key, stage.display_text);
        }
    }

    FILE *lang_file = fopen(lang_path, "w");
    if (lang_file) {
        char *lang_str = cJSON_Print(lang_json);
        if (lang_str) {
            fputs(lang_str, lang_file);
            free(lang_str);
        }
        fclose(lang_file);
    } else {
        snprintf(status_message_buffer, 256, "Error: Failed to open lang file for writing.");
        cJSON_Delete(lang_json);
        return false;
    }
    cJSON_Delete(lang_json);

    return true;
}

enum SaveMessageType {
    MSG_NONE,
    MSG_SUCCESS,
    MSG_ERROR
};

// New helper function to centralize validation and saving
static bool validate_and_save_template(const char *creator_version_str,
                                       const DiscoveredTemplate &selected_template_info,
                                       const std::string &lang_flag,
                                       EditorTemplate &current_template_data, EditorTemplate &saved_template_data,
                                       SaveMessageType &save_message_type, char *status_message,
                                       AppSettings *app_settings) {
    // Reset message state on new save attempt
    save_message_type = MSG_NONE;
    status_message[0] = '\0';

    bool validation_passed = true;

    // --- Advancements Validation ---
    MC_Version version = settings_get_version_from_string(creator_version_str); // Get version from string

    if (has_duplicate_category_root_names(current_template_data.advancements, status_message) ||
        !validate_category_icon_paths(current_template_data.advancements, version, status_message)) {
        validation_passed = false;
    }
    if (validation_passed) {
        for (const auto &adv: current_template_data.advancements) {
            if (has_duplicate_root_names(adv.criteria, status_message)) {
                validation_passed = false;
                break;
            }
        }
    }

    // --- Stats Validation ---
    if (validation_passed) {
        if (has_duplicate_category_root_names(current_template_data.stats, status_message) ||
            !validate_category_icon_paths(current_template_data.stats, version, status_message)) {
            validation_passed = false;
        }
    }
    // --- Check for stats/sub-stats with goal 0 ---
    if (validation_passed) {
        for (const auto &stat_cat: current_template_data.stats) {
            // Skip hidden legacy helper stats
            if (version <= MC_VERSION_1_6_4 && strncmp(stat_cat.root_name, "hidden_ms_stat_", 15) == 0) {
                continue; // Skip goal=0 check for hidden legacy helper stats, no target goal is the same as 0
            }
            for (const auto &crit: stat_cat.criteria) {
                // Regular stats/sub-stats shouldn't have a target value of 0
                if (!stat_cat.is_simple_stat && crit.goal == 0) {
                    snprintf(status_message, 256,
                             "Error: Sub-stat '%s' in category '%s' cannot have a Target Value of 0.", crit.root_name,
                             stat_cat.root_name);
                    validation_passed = false;
                    break; // Stop checking criteria for this category
                }
                // For simple stats that are NOT hidden legacy stats, check goal 0
                else if (stat_cat.is_simple_stat && crit.goal == 0) {
                    snprintf(status_message, 256, "Error: Stat '%s' cannot have a Target Value of 0.",
                             stat_cat.root_name);
                    validation_passed = false;
                    break; // Stop checking criteria (there's only one anyway)
                }
            }
            if (!validation_passed) break; // Stop checking categories
        }
    }
    // Prevent orphaned/manual 'hidden_ms_stat_' entries for legacy versions.
    if (validation_passed && version <= MC_VERSION_1_6_4) {
        // 1. Get all stat IDs required by multi-stage goals.
        std::unordered_set<std::string> required_ms_goal_stats;
        for (const auto &goal: current_template_data.multi_stage_goals) {
            for (const auto &stage: goal.stages) {
                if (stage.type == SUBGOAL_STAT && stage.root_name[0] != '\0') {
                    required_ms_goal_stats.insert(stage.root_name);
                }
            }
        }

        // 2. Check all stats with the reserved prefix.
        for (const auto &stat_cat: current_template_data.stats) {
            if (strncmp(stat_cat.root_name, "hidden_ms_stat_", 15) == 0) {
                // This stat has the reserved prefix. It's only valid if it's actually required.
                if (stat_cat.criteria.empty() || required_ms_goal_stats.find(stat_cat.criteria[0].root_name) ==
                    required_ms_goal_stats.end()) {
                    // This is an orphaned or malformed hidden stat. Flag it as an error.
                    snprintf(status_message, 256, "Error: The prefix 'hidden_ms_stat_' is reserved and was used\n"
                             "on a stat ('%s') that is not part of a multi-stage goal.", stat_cat.root_name);
                    validation_passed = false;
                    break;
                }
            }
        }
    }
    if (validation_passed) {
        for (const auto &stat_cat: current_template_data.stats) {
            if (has_duplicate_root_names(stat_cat.criteria, status_message)) {
                validation_passed = false;
                break;
            }
        }
    }

    // --- Unlocks & Custom Goals Validation ---
    if (validation_passed) {
        if (has_duplicate_root_names(current_template_data.unlocks, status_message) ||
            !validate_icon_paths(current_template_data.unlocks, status_message) ||
            has_duplicate_root_names(current_template_data.custom_goals, status_message) ||
            !validate_icon_paths(current_template_data.custom_goals, status_message)) {
            validation_passed = false;
        }
    }

    // --- Multi-Stage Goals Validation ---
    if (validation_passed) {
        if (has_duplicate_ms_goal_root_names(current_template_data.multi_stage_goals, status_message) ||
            !validate_ms_goal_icon_paths(current_template_data.multi_stage_goals, status_message) ||
            !validate_multi_stage_goal_stages(current_template_data.multi_stage_goals, status_message)) {
            validation_passed = false;
        }
    }
    if (validation_passed) {
        for (const auto &goal: current_template_data.multi_stage_goals) {
            if (has_duplicate_stage_ids(goal.stages, status_message)) {
                validation_passed = false;
                break;
            }
        }
    }

    // If all checks passed, attempt to save
    if (validation_passed) {
        if (save_template_from_editor(creator_version_str, selected_template_info, lang_flag, current_template_data,
                                      status_message)) {
            // Update snapshot to new clean state
            saved_template_data = current_template_data;
            save_message_type = MSG_SUCCESS;
            snprintf(status_message, 256, "Saved!");

            // Check if the saved template is the one currently active in the tracker
            bool is_active_template = (strcmp(creator_version_str, app_settings->version_str) == 0 &&
                                       strcmp(selected_template_info.category, app_settings->category) == 0 &&
                                       strcmp(selected_template_info.optional_flag,
                                              app_settings->optional_flag) == 0);

            if (is_active_template) {
                // Signal the main loop to reload the tracker data
                SDL_SetAtomicInt(&g_settings_changed, 1);
            }
            return true; // Indicate success
        } else {
            save_message_type = MSG_ERROR; // Save function failed
            return false;
        }
    } else {
        save_message_type = MSG_ERROR; // A validation check failed
        return false;
    }
}

// New function to automatically manage hidden legacy stats for multi-stage goals
static void synchronize_legacy_ms_goal_stats(EditorTemplate &editor_data) {
    // 1. Gather all unique stat root_names required by all multi-stage goal stages
    std::unordered_set<std::string> required_stat_root_names;
    for (const auto &goal: editor_data.multi_stage_goals) {
        for (const auto &stage: goal.stages) {
            if (stage.type == SUBGOAL_STAT && stage.root_name[0] != '\0') {
                required_stat_root_names.insert(stage.root_name);
            }
        }
    }

    // 2. Remove orphaned hidden stats that are no longer required
    // We use the erase-remove idiom to safely remove elements while iterating
    editor_data.stats.erase(
        std::remove_if(editor_data.stats.begin(), editor_data.stats.end(),
                       [&](const EditorTrackableCategory &stat_cat) {
                           // Check if this is one of our special hidden stats
                           if (strncmp(stat_cat.root_name, "hidden_ms_stat_", 15) == 0) {
                               if (stat_cat.criteria.empty()) return true; // Malformed, remove

                               // If this stat's tracked root_name is NOT in our required set, it's an orphan
                               if (required_stat_root_names.find(stat_cat.criteria[0].root_name) ==
                                   required_stat_root_names.end()) {
                                   return true; // Mark for removal
                               }
                           }
                           return false; // Keep this stat
                       }),
        editor_data.stats.end()
    );

    // 3. Add any required hidden stats that are missing.
    for (const auto &required_root_name: required_stat_root_names) {
        bool found = false;
        for (const auto &stat_cat: editor_data.stats) {
            if (strncmp(stat_cat.root_name, "hidden_ms_stat_", 15) == 0 && !stat_cat.criteria.empty() && stat_cat.
                criteria[0].root_name == required_root_name) {
                found = true;
                break;
            }
        }

        if (!found) {
            // This required stat doesn't exist yet, so create it
            EditorTrackableCategory new_hidden_stat = {};
            snprintf(new_hidden_stat.root_name, sizeof(new_hidden_stat.root_name), "hidden_ms_stat_%s",
                     required_root_name.c_str());
            new_hidden_stat.is_simple_stat = true; // Hidden stats are always simple stats
            new_hidden_stat.is_hidden = true; // Mark as hidden in template for consistency

            EditorTrackableItem new_crit = {};
            strncpy(new_crit.root_name, required_root_name.c_str(), sizeof(new_crit.root_name) - 1);
            new_crit.root_name[sizeof(new_crit.root_name) - 1] = '\0';
            new_hidden_stat.criteria.push_back(new_crit);

            editor_data.stats.push_back(new_hidden_stat);
        }
    }
}


// -------------------------------------------- END OF STATIC FUNCTIONS --------------------------------------------

void temp_creator_render_gui(bool *p_open, AppSettings *app_settings, ImFont *roboto_font, Tracker *t) {
    (void) t;

    if (!*p_open) {
        return;
    }

    // STATE MANAGEMENT
    static DiscoveredTemplate *discovered_templates = nullptr;
    static int discovered_template_count = 0;
    static char last_scanned_version[64] = "";
    static int selected_template_index = -1;

    // Language Management State
    static int selected_lang_index = -1;
    static bool show_create_lang_popup = false;
    static bool show_copy_lang_popup = false;
    static char lang_flag_buffer[64] = "";
    static std::string lang_to_copy_from = "";

    // State for the creator's independent version selection
    static bool was_open_last_frame = false;
    static int creator_version_idx = -1;
    static char creator_version_str[64] = "";

    // State for the "Create New" view
    static bool show_create_new_view = false;
    static char new_template_category[MAX_PATH_LENGTH] = "";
    static char new_template_flag[MAX_PATH_LENGTH] = "";

    // State for the "Copy" view
    static bool show_copy_view = false;
    static int copy_template_version_idx = -1;
    static char copy_template_category[MAX_PATH_LENGTH] = "";
    static char copy_template_flag[MAX_PATH_LENGTH] = "";

    // State for language import
    static bool show_import_lang_popup = false;
    static char import_lang_source_path[MAX_PATH_LENGTH] = "";
    static char import_lang_flag_buffer[64] = "";

    // State for the editor view
    static bool editing_template = false;
    static EditorTemplate current_template_data;
    static EditorTemplate saved_template_data; // A snapshot of the last saved state
    static DiscoveredTemplate selected_template_info;
    static std::string selected_lang_flag; // The language currently being edited
    static bool show_advancement_display_names = true;
    static bool show_stat_display_names = true;
    static bool show_ms_goal_display_names = true;
    static EditorTrackableCategory *selected_advancement = nullptr;
    static EditorTrackableCategory *selected_stat = nullptr;
    static EditorMultiStageGoal *selected_ms_goal = nullptr;
    static bool show_unsaved_changes_popup = false;
    static std::function<void()> pending_action = nullptr;

    // To switch between importing advancements or their criteria
    enum AdvancementImportMode { BATCH_ADVANCEMENT_IMPORT, CRITERIA_ONLY_IMPORT };
    static AdvancementImportMode current_advancement_import_mode = BATCH_ADVANCEMENT_IMPORT;

    // Searching within template creator
    static char tc_search_buffer[256] = "";
    static bool focus_tc_search_box = false; // Using Ctrl + F to focus the search box
    enum TemplateSearchScope {
        SCOPE_TEMPLATES,
        SCOPE_LANGUAGES,
        SCOPE_ADVANCEMENTS,
        SCOPE_STATS,
        SCOPE_UNLOCKS,
        SCOPE_CUSTOM,
        SCOPE_MULTISTAGE,
        // Details search scopes
        SCOPE_ADVANCEMENT_DETAILS,
        SCOPE_STAT_DETAILS,
        SCOPE_MULTISTAGE_DETAILS
    };
    static TemplateSearchScope current_search_scope = SCOPE_TEMPLATES;


    // State for user feedback next to save button in editor view
    static SaveMessageType save_message_type = MSG_NONE;
    static char status_message[256] = "";

    // State for the Import Confirmation view
    static bool show_import_confirmation_view = false;
    static char import_zip_path[MAX_PATH_LENGTH] = "";
    static int import_version_idx = -1;
    static char import_category[MAX_PATH_LENGTH] = "";
    static char import_flag[MAX_PATH_LENGTH] = "";

    // Imports from world file
    // Advancements
    static bool show_import_advancements_popup = false;
    static std::vector<ImportableAdvancement> importable_advancements;
    static char import_error_message[256] = "";
    static char import_search_buffer[256] = "";
    static bool import_select_criteria = false;
    static bool import_search_criteria_only = false;
    static int last_clicked_adv_index = -1;
    [[maybe_unused]] static int last_clicked_crit_index = -1;
    [[maybe_unused]] static ImportableAdvancement *last_clicked_crit_parent = nullptr;
    [[maybe_unused]] static ImportableAdvancement *stage_to_edit_source_adv = nullptr;
    [[maybe_unused]] static int last_clicked_criterion_index = -1; // For range selection
    static bool focus_import_search = false;

    // Stats
    static bool show_import_stats_popup = false;
    static std::vector<ImportableStat> importable_stats;
    static int last_clicked_stat_index = -1;

    // For multi-purpose stats import popup
    enum StatImportMode { IMPORT_AS_TOP_LEVEL, IMPORT_AS_SUB_STAT };
    static StatImportMode current_stat_import_mode = IMPORT_AS_TOP_LEVEL;

    // Unlocks
    static bool show_import_unlocks_popup = false;
    static std::vector<ImportableUnlock> importable_unlocks;
    static int last_clicked_unlock_index = -1;

    // For multi-purpose import popups
    enum ImportMode { BATCH_IMPORT, SINGLE_SELECT_STAGE };
    static ImportMode current_import_mode = BATCH_IMPORT; // Default to import multiple
    static EditorSubGoal *stage_to_edit = nullptr;

    // State for version dropdown with counts
    static std::vector<std::string> version_display_names;
    static std::vector<const char *> version_display_c_strs;
    static bool version_counts_generated = false;

    if (!version_counts_generated) {
        version_display_names.reserve(VERSION_STRINGS_COUNT);
        for (int i = 0; i < VERSION_STRINGS_COUNT; ++i) {
            DiscoveredTemplate *templates = nullptr;
            int count = 0;
            scan_for_templates(VERSION_STRINGS[i], &templates, &count);

            char buffer[128];
            if (count > 0) {
                snprintf(buffer, sizeof(buffer), "%s (%d)", VERSION_STRINGS[i], count);
            } else {
                strncpy(buffer, VERSION_STRINGS[i], sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';
            }
            version_display_names.push_back(buffer);

            free_discovered_templates(&templates, &count);
        }

        version_display_c_strs.reserve(VERSION_STRINGS_COUNT);
        for (const auto &name: version_display_names) {
            version_display_c_strs.push_back(name.c_str());
        }

        version_counts_generated = true;
    }

    // --- Version-dependent labels ---
    MC_Version creator_selected_version = settings_get_version_from_string(creator_version_str);

    // Advancement/Achievement
    const char *advancements_label_upper = (creator_selected_version <= MC_VERSION_1_11_2)
                                               ? "Achievement"
                                               : "Advancement";

    // Advancements/Achievements
    const char *advancements_label_plural_upper = (creator_selected_version <= MC_VERSION_1_11_2)
                                                      ? "Achievements"
                                                      : "Advancements";

    // advancements/achievements
    const char *advancements_label_plural_lower = (creator_selected_version <= MC_VERSION_1_11_2)
                                                      ? "achievements"
                                                      : "advancements";

    // advancement/achievement
    const char *advancements_label_singular_lower = (creator_selected_version <= MC_VERSION_1_11_2)
                                                        ? "achievement"
                                                        : "advancement";

    // LOGIC

    // Add state management for window open/close
    const bool just_opened = *p_open && !was_open_last_frame;
    was_open_last_frame = *p_open;

    if (just_opened) {
        // On first open, synchronize with the main app settings
        strncpy(creator_version_str, app_settings->version_str, sizeof(creator_version_str) - 1);
        creator_version_str[sizeof(creator_version_str) - 1] = '\0';
        for (int i = 0; i < VERSION_STRINGS_COUNT; i++) {
            if (strcmp(VERSION_STRINGS[i], creator_version_str) == 0) {
                creator_version_idx = i;
                break;
            }
        }
        last_scanned_version[0] = '\0'; // Force a scan on first open
    }

    // With this block, which correctly calculates the flag:
    bool editor_has_unsaved_changes = false; // Default to false.
    if (editing_template) {
        // Only perform the comparison if we are actually in the editor.
        editor_has_unsaved_changes = are_editor_templates_different(current_template_data, saved_template_data);
    }


    // Handle user trying to close the window with unsaved changes
    if (was_open_last_frame && !(*p_open) && editor_has_unsaved_changes) {
        *p_open = true; // Prevent window from closing
        show_unsaved_changes_popup = true;
        pending_action = [&]() { *p_open = false; }; // The pending action is to close the window
    }

    // Check if the selected template is the one currently in use
    bool is_current_template = false;
    if (selected_template_index != -1) {
        const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
        // The version is implicitly the same because the scanner uses app_settings->version_str
        if (strcmp(creator_version_str, app_settings->version_str) == 0 &&
            strcmp(selected.category, app_settings->category) == 0 &&
            strcmp(selected.optional_flag, app_settings->optional_flag) == 0) {
            is_current_template = true;
        }
    }


    // Rescan templates if the creator's version selection changes
    if (strcmp(last_scanned_version, creator_version_str) != 0) {
        free_discovered_templates(&discovered_templates, &discovered_template_count);
        scan_for_templates(creator_version_str, &discovered_templates, &discovered_template_count);
        strncpy(last_scanned_version, creator_version_str, sizeof(last_scanned_version) - 1);
        last_scanned_version[sizeof(last_scanned_version) - 1] = '\0';
        selected_template_index = -1; // Reset selection
        selected_lang_index = -1;
        status_message[0] = '\0'; // Clear status message
    }

    // UI RENDERING

    // Dynamically create the window title based on unsaved changes
    // On VERY FIRST OPEN it has this size -> nothing in imgui.ini file
    ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_FirstUseEver);
    ImGui::Begin("Template Editor", p_open);

    if (t) {
        // For global event handler to not clash with the other Ctrl + F or Cmd + F
        t->is_temp_creator_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    }

    // Handle Ctrl+F / Cmd+F to focus the search box, but only when this window is active.
    if (t && t->is_temp_creator_focused && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) && // No popups open
        (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_LeftSuper)) &&
        ImGui::IsKeyPressed(ImGuiKey_F)) {
        focus_tc_search_box = true;
    }

    // Handle Ctrl+Z / Cmd+Z to revert changes
    if (t && t->is_temp_creator_focused && editor_has_unsaved_changes && !ImGui::IsAnyItemActive() &&
        (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_LeftSuper)) &&
        ImGui::IsKeyPressed(ImGuiKey_Z)) {
        current_template_data = saved_template_data;
        save_message_type = MSG_NONE;
        status_message[0] = '\0';
    }

    if (roboto_font) {
        ImGui::PushFont(roboto_font);
    }

    // Version Selector
    ImGui::SetNextItemWidth(250); // Set a reasonable width for the combo box
    int original_version_idx = creator_version_idx; // UUse a temporary variable for the combo
    if (ImGui::Combo("Template Version", &creator_version_idx, version_display_c_strs.data(),
                     version_display_c_strs.size())) {
        // This block runs when the user makes a new selection.
        // creator_version_idx now holds the NEW index the user clicked.

        // If there are unsaved changes, we must block this action and show a popup.
        if (editing_template && editor_has_unsaved_changes) {
            // The user's newly selected index is stored for the pending action.
            int newly_selected_idx = creator_version_idx;

            // IMMEDIATELY REVERT the change in our state variable.
            // This makes the combo box snap back to the original value on the next frame.
            creator_version_idx = original_version_idx;

            // Now, show the popup and set the pending action to apply the change later.
            show_unsaved_changes_popup = true;
            // Capture by reference, but newly_selected_idx by value to ensure the lambda has access to the latest values
            pending_action = [&, newly_selected_idx]() {
                // The action to run if user clicks "Save" or "Discard"
                creator_version_idx = newly_selected_idx;
                strncpy(creator_version_str, VERSION_STRINGS[creator_version_idx], sizeof(creator_version_str) - 1);
                creator_version_str[sizeof(creator_version_str) - 1] = '\0';
                editing_template = false; // Always exit editor when switching version

                // Reset search buffer
                tc_search_buffer[0] = '\0';
                selected_advancement = nullptr;
                selected_stat = nullptr;
                selected_ms_goal = nullptr;
            };
        } else {
            // No unsaved changes, so the change is final.
            strncpy(creator_version_str, VERSION_STRINGS[creator_version_idx], sizeof(creator_version_str) - 1);
            creator_version_str[sizeof(creator_version_str) - 1] = '\0';
            editing_template = false; // Always exit editor on version change

            // Reset search and selection state
            tc_search_buffer[0] = '\0';
            selected_advancement = nullptr;
            selected_stat = nullptr;
            selected_ms_goal = nullptr;
        }
    }

    if (ImGui::IsItemHovered()) {
        char version_combo_tooltip_buffer[1024];
        snprintf(version_combo_tooltip_buffer, sizeof(version_combo_tooltip_buffer),
                 "Select the game version for which you want to manage templates.\n"
                 "The number in brackets shows how many templates are available for that version.");
        ImGui::SetTooltip("%s", version_combo_tooltip_buffer);
    }

    ImGui::SameLine();

    // TEMPLATE CREATOR SEARCH BOX
    {
        const float search_bar_width = 250.0f;
        const float scope_dropdown_width = 150.0f;
        const float clear_button_size = ImGui::GetFrameHeight(); // Use fixed, consistent size for the button
        const float spacing = ImGui::GetStyle().ItemSpacing.x;

        float total_controls_width = clear_button_size + spacing + search_bar_width + spacing + scope_dropdown_width;
        // Position cursor to the top-right
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - total_controls_width - ImGui::GetStyle().WindowPadding.x);

        // 1. Clear Button ('X')
        if (tc_search_buffer[0] != '\0') {
            if (ImGui::Button("X##ClearTCSearch", ImVec2(clear_button_size, clear_button_size))) {
                tc_search_buffer[0] = '\0';
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Clear Search");
            }
        } else {
            // Invisible dummy to maintain layout
            ImGui::Dummy(ImVec2(clear_button_size, clear_button_size));
        }
        ImGui::SameLine();

        // 2. Search Input Text Bar
        ImGui::SetNextItemWidth(search_bar_width);
        if (focus_tc_search_box) {
            ImGui::SetKeyboardFocusHere();
            focus_tc_search_box = false;
        }
        ImGui::InputTextWithHint("##TCSearch", "Search...", tc_search_buffer, sizeof(tc_search_buffer));
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[1024];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Filter the list by name, ID, icon path, or target value.\n\n"
                     "Press Ctrl+F (Cmd+F on macOS) to focus this field.");

            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::SameLine();

        // 3. Dynamic Scope Dropdown
        ImGui::SetNextItemWidth(scope_dropdown_width);

        // --- Version-Aware Type Dropdown ---

        const char *adv_ach_scope_name = (creator_selected_version <= MC_VERSION_1_11_2)
                                             ? "Achievements"
                                             : "Advancements";
        // Add a dynamic name for the details scope as well
        const char *adv_details_scope_name = (creator_selected_version <= MC_VERSION_1_11_2)
                                                 ? "Ach. Details"
                                                 : "Adv. Details";

        const char *scope_names[] = {
            "Templates", "Languages", adv_ach_scope_name, "Stats", "Unlocks", "Custom Goals", "Multi-Stage Goals",
            adv_details_scope_name, "Stat Details", "MS Goal Details"
        };

        // Determine the display name for the currently selected scope
        const char *current_scope_name = "Unknown";
        switch (current_search_scope) {
            case SCOPE_TEMPLATES: current_scope_name = scope_names[SCOPE_TEMPLATES];
                break;
            case SCOPE_LANGUAGES: current_scope_name = scope_names[SCOPE_LANGUAGES];
                break;
            case SCOPE_ADVANCEMENTS: current_scope_name = adv_ach_scope_name;
                break;
            case SCOPE_STATS: current_scope_name = scope_names[SCOPE_STATS];
                break;
            case SCOPE_UNLOCKS: current_scope_name = scope_names[SCOPE_UNLOCKS];
                break;
            case SCOPE_CUSTOM: current_scope_name = scope_names[SCOPE_CUSTOM];
                break;
            case SCOPE_MULTISTAGE: current_scope_name = scope_names[SCOPE_MULTISTAGE];
                break;
            case SCOPE_ADVANCEMENT_DETAILS: current_scope_name = adv_details_scope_name;
                break;
            case SCOPE_STAT_DETAILS: current_scope_name = scope_names[SCOPE_STAT_DETAILS];
                break;
            case SCOPE_MULTISTAGE_DETAILS: current_scope_name = scope_names[SCOPE_MULTISTAGE_DETAILS];
                break;
        }

        if (ImGui::BeginCombo("##Scope", current_scope_name)) {
            // --- Main List Scopes ---

            // Always available
            if (ImGui::Selectable(scope_names[SCOPE_TEMPLATES], current_search_scope == SCOPE_TEMPLATES)) {
                current_search_scope = SCOPE_TEMPLATES;
                tc_search_buffer[0] = '\0'; // Clear search on change
            }

            // Available only when viewing a template's language list (not in editor)
            if (selected_template_index != -1 && !editing_template && !show_create_new_view && !show_copy_view) {
                if (ImGui::Selectable(scope_names[SCOPE_LANGUAGES], current_search_scope == SCOPE_LANGUAGES)) {
                    current_search_scope = SCOPE_LANGUAGES;
                    tc_search_buffer[0] = '\0'; // Clear search on change
                }
            }

            // --- Editor Scopes (only available when editing a template) ---
            if (editing_template) {
                // Main Editor Tabs
                if (ImGui::Selectable(adv_ach_scope_name, current_search_scope == SCOPE_ADVANCEMENTS)) {
                    current_search_scope = SCOPE_ADVANCEMENTS;
                    tc_search_buffer[0] = '\0'; // Clear search on change
                }
                if (ImGui::Selectable(scope_names[SCOPE_STATS], current_search_scope == SCOPE_STATS)) {
                    current_search_scope = SCOPE_STATS;
                    tc_search_buffer[0] = '\0'; // Clear search on change
                }
                if (creator_selected_version == MC_VERSION_25W14CRAFTMINE) {
                    if (ImGui::Selectable(scope_names[SCOPE_UNLOCKS], current_search_scope == SCOPE_UNLOCKS)) {
                        current_search_scope = SCOPE_UNLOCKS;
                        tc_search_buffer[0] = '\0'; // Clear search on change
                    }
                }
                if (ImGui::Selectable(scope_names[SCOPE_CUSTOM], current_search_scope == SCOPE_CUSTOM)) {
                    current_search_scope = SCOPE_CUSTOM;
                    tc_search_buffer[0] = '\0'; // Clear search on change
                }
                if (ImGui::Selectable(scope_names[SCOPE_MULTISTAGE], current_search_scope == SCOPE_MULTISTAGE)) {
                    current_search_scope = SCOPE_MULTISTAGE;
                    tc_search_buffer[0] = '\0'; // Clear search on change
                }

                // Advancement Details
                if (ImGui::Selectable(adv_details_scope_name, current_search_scope == SCOPE_ADVANCEMENT_DETAILS)) {
                    current_search_scope = SCOPE_ADVANCEMENT_DETAILS;
                    tc_search_buffer[0] = '\0'; // Clear search on change
                }

                // Stat Details
                if (ImGui::Selectable(scope_names[SCOPE_STAT_DETAILS], current_search_scope == SCOPE_STAT_DETAILS)) {
                    current_search_scope = SCOPE_STAT_DETAILS;
                    tc_search_buffer[0] = '\0'; // Clear search on change
                }

                // Multi-Stage Goal Details
                if (ImGui::Selectable(scope_names[SCOPE_MULTISTAGE_DETAILS],
                                      current_search_scope == SCOPE_MULTISTAGE_DETAILS)) {
                    current_search_scope = SCOPE_MULTISTAGE_DETAILS;
                    tc_search_buffer[0] = '\0'; // Clear search on change
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[1024];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Change the search scope.\n\nYou can search the main lists (e.g., Templates, Stats)\nor filter the contents of a selected item's details panel.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
    }

    ImGui::Separator();


    // Left Pane: Template List
    ImGui::BeginChild("TemplateList", ImVec2(250, 0), true);
    ImGui::Text("Existing Templates");
    ImGui::Separator();

    // Determine if search is active for the template list
    bool is_template_search_active = (current_search_scope == SCOPE_TEMPLATES && tc_search_buffer[0] != '\0');

    // Create a filtered list of indices to count and render from
    std::vector<int> templates_to_render_indices;
    if (is_template_search_active) {
        for (int i = 0; i < discovered_template_count; i++) {
            char item_label[MAX_PATH_LENGTH * 2];
            if (discovered_templates[i].optional_flag[0] != '\0') {
                snprintf(item_label, sizeof(item_label), "%s%s", discovered_templates[i].category,
                         discovered_templates[i].optional_flag);
            } else {
                snprintf(item_label, sizeof(item_label), "%s", discovered_templates[i].category);
            }
            if (str_contains_insensitive(item_label, tc_search_buffer)) {
                templates_to_render_indices.push_back(i);
            }
        }
    } else {
        for (int i = 0; i < discovered_template_count; i++) {
            templates_to_render_indices.push_back(i);
        }
    }

    // --- Centered Counter for the list ---
    char counter_text[128];
    snprintf(counter_text, sizeof(counter_text), "%zu %s", templates_to_render_indices.size(),
             templates_to_render_indices.size() == 1 ? "Template" : "Templates");
    float text_width = ImGui::CalcTextSize(counter_text).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - text_width) * 0.5f);
    ImGui::TextDisabled("%s", counter_text);

    // Render the filtered list using the stored indices
    for (int i: templates_to_render_indices) {
        char item_label[MAX_PATH_LENGTH * 2];
        if (discovered_templates[i].optional_flag[0] != '\0') {
            // With optional flag, display right after category
            snprintf(item_label, sizeof(item_label), "%s%s", discovered_templates[i].category,
                     discovered_templates[i].optional_flag);
        } else {
            // Without optional flag
            snprintf(item_label, sizeof(item_label), "%s", discovered_templates[i].category);
        }

        if (ImGui::Selectable(item_label, selected_template_index == i)) {
            // Define the action of switching to a new template, capturing the correct original index 'i'
            auto switch_template_action = [&, i]() {
                selected_template_index = i;
                selected_lang_index = -1; // Reset language selection
                selected_lang_flag = ""; // default
                // If we are in the editor, we must update the loaded data
                if (editing_template) {
                    selected_template_info = discovered_templates[i];
                    if (load_template_for_editing(creator_version_str, selected_template_info, selected_lang_flag,
                                                  current_template_data,
                                                  status_message)) {
                        // Also update the 'saved' snapshot to reflect the newly loaded state
                        saved_template_data = current_template_data;

                        // Exit editor to prevent confusion
                        editing_template = false;

                        // Reset search buffer and selections
                        tc_search_buffer[0] = '\0';
                        selected_advancement = nullptr;
                        selected_stat = nullptr;
                        selected_ms_goal = nullptr;
                    }
                }
            };

            // If switching to a DIFFERENT template with unsaved changes, show popup
            if (editing_template && editor_has_unsaved_changes && selected_template_index != i) {
                show_unsaved_changes_popup = true;
                pending_action = switch_template_action;
            } else {
                // Otherwise, perform the switch immediately
                switch_template_action();
            }

            // On any new template selection, reset the language selection
            selected_lang_index = -1;
            selected_lang_flag = ""; // default

            // General state changes on any selection
            show_create_new_view = false;
            show_copy_view = false;
            status_message[0] = '\0';
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Pane: Actions & Editor View

    ImGui::BeginChild("ActionsView", ImVec2(0, 0));

    // --- Main Action Buttons ---

    bool has_unsaved_changes_in_editor = editing_template && editor_has_unsaved_changes;

    ImGui::BeginDisabled(has_unsaved_changes_in_editor);
    if (ImGui::Button("Create New Template")) {
        auto create_action = [&]() {
            show_create_new_view = true;
            show_copy_view = false;
            editing_template = false;
            selected_template_index = -1;
            status_message[0] = '\0';
            new_template_category[0] = '\0';
            new_template_flag[0] = '\0';

            // Reset search buffer
            tc_search_buffer[0] = '\0';
            selected_advancement = nullptr;
            selected_stat = nullptr;
            selected_ms_goal = nullptr;
        };
        if (editing_template && editor_has_unsaved_changes) {
            show_unsaved_changes_popup = true;
            pending_action = create_action;
        } else {
            create_action();
        }
    }
    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        char tooltip_buffer[256];
        if (has_unsaved_changes_in_editor) {
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "You have unsaved changes in the editor. Save them first.");
        } else {
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Create a new, empty template for version: %s",
                     creator_version_str);
        }
        ImGui::SetTooltip("%s", tooltip_buffer);
    }

    ImGui::SameLine();

    ImGui::BeginDisabled(selected_template_index == -1);
    if (ImGui::Button("Edit Template")) {
        auto action = [&]() {
            editing_template = true;
            show_create_new_view = false;
            show_copy_view = false;

            if (selected_template_index != -1) {
                if (selected_lang_index == -1) selected_lang_index = 0;

                selected_template_info = discovered_templates[selected_template_index];
                selected_lang_flag = selected_template_info.available_lang_flags[selected_lang_index];

                if (load_template_for_editing(creator_version_str, selected_template_info, selected_lang_flag,
                                              current_template_data, status_message)) {
                    saved_template_data = current_template_data;
                }
            }
        };
        if (editor_has_unsaved_changes) {
            show_unsaved_changes_popup = true;
            pending_action = action;
        } else {
            action();
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        char tooltip_buffer[128];
        if (selected_template_index == -1) {
            // When no template is selected
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Select a template from the list to edit.");
        } else {
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Open the editor for the selected template.");
        }
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    ImGui::SameLine();

    // Allow copying of the currently used template
    // Disabled on unsaved changes in editor or no template selected
    ImGui::BeginDisabled(has_unsaved_changes_in_editor || selected_template_index == -1);
    if (ImGui::Button("Copy Template")) {
        if (selected_template_index != -1) {
            show_copy_view = true;
            show_create_new_view = false;
            editing_template = false; // Still allow clicking other buttons (e.g., copy, delete, ...) when editing
            status_message[0] = '\0';

            // Pre-fill with selected template's info and append _copy to the flag
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            const char *dest_version = creator_version_str;

            // Pre-fill the category and version for the new copy
            strncpy(copy_template_category, selected.category, sizeof(copy_template_category) - 1);
            copy_template_category[sizeof(copy_template_category) - 1] = '\0';
            copy_template_version_idx = creator_version_idx;

            // --- Logic to find a unique name for the new flag ---
            char base_flag[MAX_PATH_LENGTH];
            strncpy(base_flag, selected.optional_flag, sizeof(base_flag) - 1);
            base_flag[sizeof(base_flag) - 1] = '\0';

            char new_flag[MAX_PATH_LENGTH];
            int copy_counter = 1; // 1 for the first attempt (_copy), 2+ for _copyN

            while (true) {
                if (copy_counter == 1) {
                    snprintf(new_flag, sizeof(new_flag), "%s_copy", base_flag);
                } else {
                    snprintf(new_flag, sizeof(new_flag), "%s_copy%d", base_flag, copy_counter);
                }

                // Construct the potential path to check if a template with this name already exists
                char dest_version_filename[64];
                strncpy(dest_version_filename, dest_version, sizeof(dest_version_filename) - 1);
                dest_version_filename[sizeof(dest_version_filename) - 1] = '\0';
                for (char *p = dest_version_filename; *p; p++) { if (*p == '.') *p = '_'; }

                char dest_template_path[MAX_PATH_LENGTH];
                snprintf(dest_template_path, sizeof(dest_template_path), "%s/templates/%s/%s/%s_%s%s.json",
                         get_resources_path(),
                         dest_version,
                         selected.category, // Check against the original category
                         dest_version_filename,
                         selected.category,
                         new_flag);

                if (!path_exists(dest_template_path)) {
                    break; // Found a unique name
                }
                copy_counter++; // Increment and try the next number
            }
            // Apply the new unique flag to the copy view's buffer
            strncpy(copy_template_flag, new_flag, sizeof(copy_template_flag) - 1);
            copy_template_flag[sizeof(copy_template_flag) - 1] = '\0';
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        char tooltip_buffer[1024];
        if (selected_template_index == -1) {
            // Tooltip for the DISABLED state.
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Select a template from the list to copy.");
        } else if (has_unsaved_changes_in_editor) {
            // Unsaved changes in editor
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "You have unsaved changes in the editor. Save them first.");
        } else {
            // Enabled state
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Creates a copy of the selected template. You can then change its version, category, or flag.\n\n"
                     "Note: This action copies the main template file and all of its\n"
                     "associated language files (e.g., _lang.json, _lang_eng.json).");
        }
        ImGui::SetTooltip("%s", tooltip_buffer);
    }

    ImGui::SameLine();

    // Determine if the selected template is the default one
    // Can't delete it
    bool is_default_template = false;
    if (selected_template_index != -1) {
        const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
        if (strcmp(creator_version_str, "1.16.1") == 0 &&
            strcmp(selected.category, "all_advancements") == 0 &&
            selected.optional_flag[0] == '\0') {
            is_default_template = true;
        }
    }

    // Disable if nothing is selected or the selected template is in use or unsaved changes in editor or default
    ImGui::BeginDisabled(
        selected_template_index == -1 || is_current_template || has_unsaved_changes_in_editor || is_default_template);
    if (ImGui::Button("Delete Template")) {
        if (selected_template_index != -1) {
            ImGui::OpenPopup("Delete Template?");
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        char tooltip_buffer[512];
        if (has_unsaved_changes_in_editor) {
            // Unsaved changes in editor
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "You have unsaved changes in the editor. Save them first.");
        } else if (is_default_template) {
            // Can't delete default template
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "The default template cannot be deleted.");
        } else if (selected_template_index != -1 && is_current_template) {
            // Selected template is in use
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Cannot delete the template currently in use.");
        } else if (selected_template_index != -1) {
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            if (selected.optional_flag[0] != '\0') {
                // WITH OPTIONAL FLAG
                if (creator_selected_version <= MC_VERSION_1_6_4) {
                    // Legacy snapshot mentioned
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Delete template:\nVersion: %s\nCategory: %s\nFlag: %s\n\n"
                             "This deletes the template, associated language files,\n"
                             "notes and snapshot file for global stats.\n"
                             "Empty folders within the 'templates' folder will also be deleted.\n"
                             "This action cannot be undone.",
                             creator_version_str, selected.category, selected.optional_flag);
                } else {
                    // No snapshot mentioned
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Delete template:\nVersion: %s\nCategory: %s\nFlag: %s\n\n"
                             "This deletes the template, associated language files and notes.\n"
                             "Empty folders within the 'templates' folder will also be deleted.\n"
                             "This action cannot be undone.",
                             creator_version_str, selected.category, selected.optional_flag);
                }
            } else {
                // NO OPTIONAL FLAG
                if (creator_selected_version <= MC_VERSION_1_6_4) {
                    // Legacy snapshot mentioned
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Delete template:\nVersion: %s\nCategory: %s\n\n"
                             "This deletes the template, associated language files,\n"
                             "notes and snapshot file for global stats.\n"
                             "Empty folders within the 'templates' folder will also be deleted.\n"
                             "This action cannot be undone.",
                             creator_version_str, selected.category);
                } else {
                    // No snapshot mentioned
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Delete template:\nVersion: %s\nCategory: %s\n\n"
                             "This deletes the template, associated language files and notes.\n"
                             "Empty folders within the 'templates' folder will also be deleted.\n"
                             "This action cannot be undone.",
                             creator_version_str, selected.category);
                }
            }
        } else {
            // Nothing selected
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Select a template from the list to delete.");
        }
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    ImGui::SameLine();

    // Import Template Button
    ImGui::BeginDisabled(has_unsaved_changes_in_editor);
    if (ImGui::Button("Import Template")) {
        auto import_action = [&]() {
#ifdef __APPLE__
            const char *filter_patterns[2] = {"*.zip", "public.zip-archive"};
            int filter_count = 2;
#else
            const char *filter_patterns[1] = {"*.zip"};
            int filter_count = 1;
#endif
            const char *open_path = tinyfd_openFileDialog("Import Template From Zip", "", filter_count, filter_patterns,
                                                          "Template ZIP Archive", 0);
            if (open_path) {
                char version[64], category[MAX_PATH_LENGTH], flag[MAX_PATH_LENGTH];
                if (get_info_from_zip(open_path, version, category, flag, status_message, sizeof(status_message))) {
                    // Success: Pre-fill the confirmation view
                    strncpy(import_zip_path, open_path, sizeof(import_zip_path) - 1);
                    import_zip_path[sizeof(import_zip_path) - 1] = '\0';

                    strncpy(import_category, category, sizeof(import_category) - 1);
                    import_category[sizeof(import_category) - 1] = '\0';

                    strncpy(import_flag, flag, sizeof(import_flag) - 1);
                    import_flag[sizeof(import_flag) - 1] = '\0';

                    // Pre-select the version dropdown to match the creator's current version.
                    import_version_idx = creator_version_idx;

                    show_import_confirmation_view = true;
                    show_create_new_view = false;
                    show_copy_view = false;
                    editing_template = false;
                } else {
                    // On failure, get_info_from_zip already set the status_message
                    ImGui::OpenPopup("Import Error"); // Trigger popup for other errors
                }
            }
        };

        if (editing_template && editor_has_unsaved_changes) {
            show_unsaved_changes_popup = true;
            pending_action = import_action;
        } else {
            import_action();
        }
    }
    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        char tooltip_buffer[512];
        if (has_unsaved_changes_in_editor) {
            // Unsaved changes in editor
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "You have unsaved changes in the editor. Save them first.");
        } else {
            // Legacy _snapshot naming restriction
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Import a template from a .zip file.\n"
                     "Import a full template package, including the main file and all language files.\n"
                     "You can then configure the version, category and flag before performing the import.\n"
                     "For legacy versions a template file cannot end in _snapshot.");
        }
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    ImGui::SameLine();

    // Export Template Button
    // Disabled when there are unsaved changes or no template is selected
    ImGui::BeginDisabled(has_unsaved_changes_in_editor || selected_template_index == -1);
    if (ImGui::Button("Export Template")) {
        if (selected_template_index != -1) {
            handle_export_template(discovered_templates[selected_template_index], creator_version_str, status_message,
                                   sizeof(status_message));
            save_message_type = MSG_SUCCESS; // Use success color to make the message visible
        } else {
            save_message_type = MSG_ERROR;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        char tooltip_buffer[256];
        if (selected_template_index == -1) {
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Select a template from the list to export.");
        } else if (has_unsaved_changes_in_editor) {
            // Unsaved changes in editor
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "You have unsaved changes in the editor. Save them first.");
        } else {
            // Exporting
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Export the selected template as a .zip file, including its main file and all language files.\n\n"
                     "Feel free to share them on the Official Advancely Discord (discord.gg/TyNgXDz)!");
        }
        ImGui::SetTooltip("%s", tooltip_buffer);
    }

    // Delete Confirmation Popup
    if (ImGui::BeginPopupModal("Delete Template?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (selected_template_index != -1) {
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            if (creator_selected_version <= MC_VERSION_1_6_4) {
                // Snapshot mentioned
                ImGui::Text("Are you sure you want to permanently delete this template and\n"
                    "all its associated files (language files, notes and snapshot for global stats)?\n"
                    "This action cannot be undone.");
            } else {
                // Snapshot not mentioned
                ImGui::Text("Are you sure you want to permanently delete this template and\n"
                    "all its associated files (language files and notes)?\n"
                    "This action cannot be undone.");
            }

            // Construct detailed info string
            char template_info[512];
            if (selected.optional_flag[0] != '\0') {
                snprintf(template_info, sizeof(template_info), "Template Version: %s\nCategory: %s\nFlag: %s",
                         creator_version_str, selected.category, selected.optional_flag);
            } else {
                snprintf(template_info, sizeof(template_info), "Template Version: %s\nCategory: %s",
                         creator_version_str, selected.category);
            }
            ImGui::TextUnformatted(template_info);
            ImGui::Separator();

            // Also pressing ENTER
            if (ImGui::Button("Delete", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                if (delete_template_files(creator_version_str, selected.category, selected.optional_flag)) {
                    snprintf(status_message, sizeof(status_message), "Template '%s' deleted.", selected.category);
                    SDL_SetAtomicInt(&g_templates_changed, 1); // Signal change
                    editing_template = false; // Exit the editor view
                } else {
                    snprintf(status_message, sizeof(status_message), "Error: Failed to delete template '%s'.",
                             selected.category);
                }
                selected_template_index = -1;
                last_scanned_version[0] = '\0'; // Force rescan
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[256];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer), "You can also press ENTER.\n"
                         "Deletes the template.");
                ImGui::SetTooltip("%s", tooltip_buffer);
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            // Also pressing ESCAPE
            if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[256];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer), "You can also press ESCAPE.\n"
                         "Keeps the template.");
                ImGui::SetTooltip("%s", tooltip_buffer);
            }
        }
        ImGui::EndPopup();
    }

    // --- Import Error Popup ---
    if (ImGui::BeginPopupModal("Import Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("An error occurred during import:");
        ImGui::Separator();
        ImGui::TextWrapped("%s", status_message); // Display the specific error
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "You can also press ENTER.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }

    if (show_import_lang_popup) ImGui::OpenPopup("Import Language");
    if (ImGui::BeginPopupModal("Import Language", &show_import_lang_popup, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char popup_error_msg[256] = "";
        const auto &selected = discovered_templates[selected_template_index];

        ImGui::Text("Importing for: '%s%s' for version %s.", selected.category, selected.optional_flag,
                    creator_version_str);
        ImGui::TextWrapped("Source: %s", import_lang_source_path);
        ImGui::Separator();
        ImGui::InputText("New Language Flag", import_lang_flag_buffer, sizeof(import_lang_flag_buffer));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Enter a flag for the new language (e.g., 'de', 'fr_ca').\nCannot be empty or contain special characters except for underscores, dots, and the %% sign.");
        }

        if (popup_error_msg[0] != '\0') {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", popup_error_msg);
        }

        if (ImGui::Button("Confirm Import", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            popup_error_msg[0] = '\0'; // Clear previous error
            if (execute_import_language_file(creator_version_str, selected.category, selected.optional_flag,
                                             import_lang_source_path, import_lang_flag_buffer, popup_error_msg,
                                             sizeof(popup_error_msg))) {
                SDL_SetAtomicInt(&g_templates_changed, 1);
                last_scanned_version[0] = '\0'; // Force rescan
                ImGui::CloseCurrentPopup();
                show_import_lang_popup = false;
            }
            // On failure, popup_error_msg is set and the modal remains open
        }
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[128];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Press ENTER to confirm the import.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            popup_error_msg[0] = '\0';
            ImGui::CloseCurrentPopup();
            show_import_lang_popup = false;
        }
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[128];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Press ESCAPE to cancel the import.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::EndPopup();
    }

    // --- Open World Folder Button (Right-Aligned) ---
    const char *world_folder_text = "Open World Folder";
    float world_folder_button_width = ImGui::CalcTextSize(world_folder_text).x + ImGui::GetStyle().FramePadding.x *
                                      2.0f;

    // --- Help Button (Right-Aligned) ---
    const char *help_text = "Help";
    // Calculate button width to align it properly
    float help_button_width = ImGui::CalcTextSize(help_text).x + ImGui::GetStyle().FramePadding.x * 2.0f;

    // Calculate the total width of all buttons on the right to align them correctly
    float right_buttons_width = world_folder_button_width + ImGui::GetStyle().ItemSpacing.x + help_button_width;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - right_buttons_width);

    // Get the version that is currently active in the main tracker settings
    MC_Version tracker_active_version = settings_get_version_from_string(app_settings->version_str);

    // Disable the button if no world is loaded
    ImGui::BeginDisabled(t->world_name[0] == '\0');
    if (ImGui::Button(world_folder_text)) {
        char path_to_open[MAX_PATH_LENGTH] = {0};

        // Determine the correct path based on version and settings
        if (tracker_active_version <= MC_VERSION_1_6_4 && !app_settings->using_stats_per_world_legacy) {
            // Legacy global stats: open the parent's "stats" folder
            char parent_dir[MAX_PATH_LENGTH];
            if (get_parent_directory(t->saves_path, parent_dir, sizeof(parent_dir), 1)) {
                snprintf(path_to_open, sizeof(path_to_open), "%s/stats", parent_dir);
            }
        } else {
            // All other cases: open the specific world folder
            snprintf(path_to_open, sizeof(path_to_open), "%s/%s", t->saves_path, t->world_name);
        }

        // Execute the system command to open the folder
        if (path_to_open[0] != '\0' && path_exists(path_to_open)) {
#ifdef _WIN32
            path_to_windows_native(path_to_open);
#endif
            open_content(path_to_open); // Clean replacement
        } else {
            log_message(LOG_ERROR, "[TEMP CREATOR] Could not open world folder, path does not exist: %s\n",
                        path_to_open);
        }
    }
    ImGui::EndDisabled();

    // Version-aware tooltip logic
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        char tooltip_buffer[1024];
        if (t->world_name[0] == '\0') {
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "No world is currently being tracked.");
        } else {
            if (tracker_active_version <= MC_VERSION_1_6_4) {
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Opens the folder containing the stats file for the current world.\n\n"
                         "It opens the global or local stats depending on your 'StatsPerWorld' setting.\n"
                         "Inside this folder you can find the '.dat' file which contains all of\n"
                         "your completed achievements and statistics.");
            } else if (tracker_active_version <= MC_VERSION_1_12_2) {
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Opens the folder for the current world.\n\n"
                         "Within this folder you can find:\n"
                         " • The 'advancements' folder (for 1.12.x), containing a '.json' file\n"
                         "   with your completed advancements and recipes.\n"
                         " • The 'stats' folder, containing a '.json' file with your statistics\n"
                         "   (and achievements for 1.7-1.11.2).");
            } else if (tracker_active_version == MC_VERSION_25W14CRAFTMINE) {
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Opens the folder for the current world.\n\n"
                         "Within this folder you can find:\n"
                         " • The 'advancements' folder, containing a '.json' file\n"
                         "   with your completed advancements and recipes.\n"
                         " • The 'stats' folder, containing a '.json' file with your statistics.\n"
                         " • The 'unlocks' folder, containing a '.json' file with your obtained unlocks.");
            } else {
                // Modern versions (1.13+)
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Opens the folder for the current world.\n\n"
                         "Within this folder you can find:\n"
                         " • The 'advancements' folder, containing a '.json' file\n"
                         "   with your completed advancements and recipes.\n"
                         " • The 'stats' folder, containing a '.json' file with your statistics.");
            }
        }
        ImGui::SetTooltip("%s", tooltip_buffer);
    }

    ImGui::SameLine();

    if (ImGui::Button(help_text)) {
        char reference_path[MAX_PATH_LENGTH];
        snprintf(reference_path, sizeof(reference_path), "%s/reference_files", get_application_dir());
#ifdef _WIN32
        path_to_windows_native(reference_path);
#endif
        open_content(reference_path); // Clean replacement
    }
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[1024];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Opens the reference files folder.\n\n"
                 "This folder contains guides and examples on how to achieve the\n"
                 "template functionality you want with version-specific help for root names.\n"
                 "It also contains example advancements-, stats- and unlocks files of a world\n"
                 "for every major version range as reference or to import.");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }

    ImGui::Separator();


    // --- Language Management UI (appears when a template is selected) ---
    if (selected_template_index != -1 && !editing_template && !show_create_new_view && !show_copy_view && !
        show_import_confirmation_view) {
        const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
        ImGui::Text("Languages for '%s%s'", selected.category, selected.optional_flag);

        // Replace ListBox with a filterable list
        bool is_lang_search_active = (current_search_scope == SCOPE_LANGUAGES && tc_search_buffer[0] != '\0');

        // Create a filtered list to count and render from
        std::vector<int> langs_to_render_indices;
        if (is_lang_search_active) {
            for (size_t i = 0; i < selected.available_lang_flags.size(); ++i) {
                const auto &flag = selected.available_lang_flags[i];
                const char *display_name = flag.empty() ? "Default (_lang.json)" : flag.c_str();
                if (str_contains_insensitive(display_name, tc_search_buffer)) {
                    langs_to_render_indices.push_back(i);
                }
            }
        } else {
            for (size_t i = 0; i < selected.available_lang_flags.size(); ++i) {
                langs_to_render_indices.push_back(i);
            }
        }

        // --- Right-aligned Counter for the list ---
        char lang_counter_text[128];
        snprintf(lang_counter_text, sizeof(lang_counter_text), "%zu %s", langs_to_render_indices.size(),
                 langs_to_render_indices.size() == 1 ? "Language" : "Languages");
        float lang_text_width = ImGui::CalcTextSize(lang_counter_text).x;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - lang_text_width);
        ImGui::TextDisabled("%s", lang_counter_text);


        // Use a child window to create a scrollable area for the list
        ImGui::BeginChild("LanguageListChild", ImVec2(-1, 125), true); // 125px height

        for (int i: langs_to_render_indices) {
            const auto &flag = selected.available_lang_flags[i];
            const char *display_name = flag.empty() ? "Default (_lang.json)" : flag.c_str();

            if (ImGui::Selectable(display_name, selected_lang_index == (int) i)) {
                selected_lang_index = i;
            }
        }
        ImGui::EndChild();

        auto create_action = [&]() {
            show_create_lang_popup = true;
            lang_flag_buffer[0] = '\0';
            status_message[0] = '\0';
        };
        if (ImGui::Button("Create Language")) {
            if (editor_has_unsaved_changes) {
                show_unsaved_changes_popup = true;
                pending_action = create_action;
            } else { create_action(); }
        }
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Create a new, empty language file for this template.\n"
                     "This will result in the root names becoming the display names.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(selected_lang_index == -1);
        auto copy_action = [&]() {
            show_copy_lang_popup = true;
            lang_flag_buffer[0] = '\0';
            status_message[0] = '\0';
            lang_to_copy_from = selected.available_lang_flags[selected_lang_index];
        };
        if (ImGui::Button("Copy Language")) {
            if (editor_has_unsaved_changes) {
                show_unsaved_changes_popup = true;
                pending_action = copy_action;
            } else { copy_action(); }
        }
        ImGui::EndDisabled();

        // Handle both enabled and disabled tooltips
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            char tooltip_buffer[512];
            if (selected_lang_index == -1) {
                snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Select a language to copy.");
            } else {
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Create a new language file by copying the contents of the selected language.\n"
                         "Copying a completely empty language file will fall back to copying the default language file.");
            }
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::SameLine();

        // DELETION LOGIC FOR LANGUAGES -> Only allow deletion if not default and not currently active
        bool can_delete = false;
        const char *disabled_tooltip = "";
        if (selected_lang_index != -1) {
            const std::string &selected_lang_in_creator = selected.available_lang_flags[selected_lang_index];

            // Rule 1: Cannot delete the default language file.
            bool is_default_lang = selected_lang_in_creator.empty();
            if (is_default_lang) {
                disabled_tooltip = "Cannot delete the default language file.";
            }

            // Rule 2: Cannot delete the language file currently active in the main settings.
            bool is_active_template = (strcmp(creator_version_str, app_settings->version_str) == 0 &&
                                       strcmp(selected.category, app_settings->category) == 0 &&
                                       strcmp(selected.optional_flag, app_settings->optional_flag) == 0);
            bool is_active_lang = (selected_lang_in_creator == app_settings->lang_flag);

            if (is_active_template && is_active_lang) {
                disabled_tooltip = "Cannot delete the language currently in use by the tracker.";
            }

            // The button is enabled only if neither rule is broken.
            if (!is_default_lang && !(is_active_template && is_active_lang)) {
                can_delete = true;
            }
        } else {
            disabled_tooltip = "Select a language to delete.";
        }

        ImGui::BeginDisabled(!can_delete);
        auto delete_action = [&]() { ImGui::OpenPopup("Delete Language?"); };
        if (ImGui::Button("Delete Language")) {
            if (editor_has_unsaved_changes) {
                show_unsaved_changes_popup = true;
                pending_action = delete_action;
            } else { delete_action(); }
        }
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (can_delete) {
                // Tooltip for ENABLED state
                char tooltip_buffer[256];
                const auto &lang_to_delete = selected.available_lang_flags[selected_lang_index];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Delete the '%s' language file.\n"
                         "This action cannot be undone.", lang_to_delete.c_str());
                ImGui::SetTooltip("%s", tooltip_buffer);
            } else {
                // Tooltip for DISABLED state (original logic)
                ImGui::SetTooltip("%s", disabled_tooltip);
            }
        }
        ImGui::SameLine();

        ImGui::BeginDisabled(has_unsaved_changes_in_editor);
        if (ImGui::Button("Import Language")) {
#ifdef __APPLE__
            const char *filter_patterns[2] = {"*.json", "public.json"};
            int filter_count = 2;
#else
            const char *filter_patterns[1] = {"*.json"};
            int filter_count = 1;
#endif
            const char *open_path = tinyfd_openFileDialog("Import Language File", "",
                                                          filter_count, filter_patterns, "JSON files", 0);
            if (open_path) {
                strncpy(import_lang_source_path, open_path, sizeof(import_lang_source_path) - 1);
                import_lang_source_path[sizeof(import_lang_source_path) - 1] = '\0';
                import_lang_flag_buffer[0] = '\0'; // Clear buffer for new import
                show_import_lang_popup = true;
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Import a language file (.json) for the selected template '%s%s'.\n"
                     "Any matching display name entries within the language file will be kept,\n"
                     "new ones will default to their respective root names.",
                     selected.category, selected.optional_flag);
            ImGui::SetTooltip("%s", tooltip_buffer);
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(selected_lang_index == -1);
        if (ImGui::Button("Export Language")) {
            if (selected_lang_index != -1) {
                const auto &lang_to_export = selected.available_lang_flags[selected_lang_index];
                handle_export_language(creator_version_str, selected.category, selected.optional_flag,
                                       lang_to_export.c_str());
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            char tooltip_buffer[256];
            if (selected_lang_index == -1) {
                snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Select a language to export.");
            } else {
                const auto &lang_to_export = selected.available_lang_flags[selected_lang_index];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Open the folder containing the language file for '%s' and select it.",
                         lang_to_export.empty() ? "Default" : lang_to_export.c_str());
            }
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
    } // End of Language Selector


    // "Editing Template" Form

    if (editing_template) {
        // --- CORE EDITOR VIEW ---
        ImGui::PushID(&selected_template_info); // Use address of info struct for a unique ID

        // Display info about the currently edited template
        char current_file_info[512];
        if (selected_template_info.optional_flag[0] != '\0') {
            snprintf(current_file_info, sizeof(current_file_info), "Editing: %s - %s%s", creator_version_str,
                     selected_template_info.category, selected_template_info.optional_flag);
        } else {
            snprintf(current_file_info, sizeof(current_file_info), "Editing: %s - %s", creator_version_str,
                     selected_template_info.category);
        }
        ImGui::TextDisabled("%s", current_file_info);

        // Add the Drag & Drop notice, aligned to the right
        const char *dnd_notice = "(Drag & drop list items to reorder)";
        float text_width = ImGui::CalcTextSize(dnd_notice).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - text_width - ImGui::GetStyle().WindowPadding.x);
        ImGui::TextDisabled("%s", dnd_notice);

        ImGui::Separator();

        // --- Language Selector inside Editor ---
        ImGui::SetNextItemWidth(250);
        std::vector<const char *> lang_display_names;
        for (const auto &flag: selected_template_info.available_lang_flags) {
            lang_display_names.push_back(flag.empty() ? "Default" : flag.c_str());
        }
        int current_lang_idx = -1;
        for (size_t i = 0; i < selected_template_info.available_lang_flags.size(); ++i) {
            if (selected_template_info.available_lang_flags[i] == selected_lang_flag) {
                current_lang_idx = i;
                break;
            }
        }

        if (ImGui::Combo("Display Language", &current_lang_idx, lang_display_names.data(),
                         (int) lang_display_names.size())) {
            auto switch_action = [&, current_lang_idx]() {
                selected_lang_flag = selected_template_info.available_lang_flags[current_lang_idx];
                if (load_template_for_editing(creator_version_str, selected_template_info, selected_lang_flag,
                                              current_template_data, status_message)) {
                    saved_template_data = current_template_data;
                    save_message_type = MSG_NONE;
                    status_message[0] = '\0';
                }
            };

            if (editor_has_unsaved_changes) {
                show_unsaved_changes_popup = true;
                pending_action = switch_action;
            } else {
                switch_action();
            }
        }
        // Display language tooltip
        if (ImGui::IsItemHovered()) {
            char select_lang_file_tooltip_buffer[1024];
            snprintf(select_lang_file_tooltip_buffer, sizeof(select_lang_file_tooltip_buffer),
                     "Select the language file for editing display names.\n\n"
                     "• Loading: Changing this selection will reload all 'Display Name' fields in the editor from the chosen file.\n"
                     "• Saving: Edits to display names are saved to the language selected here when you click the main 'Save' button.\n\n"
                     "This keeps the template's core structure separate from its translations.");
            ImGui::SetTooltip("%s", select_lang_file_tooltip_buffer);
        }
        ImGui::Separator();

        // Save when creator window is focused
        // Enter key is disabled when a popup is open
        if (ImGui::Button("Save") || (ImGui::IsKeyPressed(ImGuiKey_Enter) &&
                                      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !
                                      ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))) {
            validate_and_save_template(creator_version_str, selected_template_info, selected_lang_flag,
                                       current_template_data,
                                       saved_template_data, save_message_type, status_message, app_settings);
        }

        // Save button tooltip
        if (ImGui::IsItemHovered()) {
            char save_template_tooltip_buffer[1024];
            snprintf(save_template_tooltip_buffer, sizeof(save_template_tooltip_buffer),
                     "Press ENTER to save the currently edited template into the .json files.\n"
                     "Does not save on errors.");
            ImGui::SetTooltip("%s", save_template_tooltip_buffer);
        }

        // Calculate the unsaved changes flag on-the-fly each frame
        bool editor_has_unsaved_changes = are_editor_templates_different(current_template_data, saved_template_data);

        // This "Unsaved Changes" indicator will appear/disappear automatically
        if (editor_has_unsaved_changes) {
            ImGui::SameLine();
            // Replace the TextColored indicator with a Revert button
            if (ImGui::Button("Revert Changes")) {
                current_template_data = saved_template_data;
                save_message_type = MSG_NONE; // Clear any existing message
                status_message[0] = '\0'; // Clear the message text
            }
        }

        if (ImGui::IsItemHovered()) {
            char revert_changes_tooltip_buffer[1024];
            snprintf(revert_changes_tooltip_buffer, sizeof(revert_changes_tooltip_buffer),
                     "Discard all unsaved changes and reload from the last saved state.\n"
                     "(Ctrl+Z / Cmd+Z)");
            ImGui::SetTooltip("%s", revert_changes_tooltip_buffer);
        }

        // Render the success or error message next to the button
        if (save_message_type != MSG_NONE) {
            ImGui::SameLine();
            // Green or red
            ImVec4 color = (save_message_type == MSG_SUCCESS)
                               ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f)
                               : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            ImGui::TextColored(color, "%s", status_message);
        }

        // Popup window for unsaved changes
        if (show_unsaved_changes_popup) {
            ImGui::OpenPopup("Unsaved Changes");
            show_unsaved_changes_popup = false;
        }

        if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("You have unsaved changes. Do you want to save them?\n\n");
            if (ImGui::Button("Save", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                // Call the central validation and save function.
                // It returns true only if validation passes AND the file is saved successfully.
                bool save_successful = validate_and_save_template(creator_version_str, selected_template_info,
                                                                  selected_lang_flag,
                                                                  current_template_data,
                                                                  saved_template_data, save_message_type,
                                                                  status_message,
                                                                  app_settings);

                // Only proceed and close the popup if the save was successful.
                // If it fails, the popup remains open, and the error message is shown in the main editor view.
                if (save_successful) {
                    if (pending_action) pending_action();
                    ImGui::CloseCurrentPopup();
                }
            }
            // Save hover text
            if (ImGui::IsItemHovered()) {
                char press_enter_save_tooltip_buffer[1024];
                snprintf(press_enter_save_tooltip_buffer, sizeof(press_enter_save_tooltip_buffer),
                         "Press ENTER to save.");
                ImGui::SetTooltip("%s", press_enter_save_tooltip_buffer);
            }
            ImGui::SameLine();
            // Also use spacebar
            if (ImGui::Button("Discard", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Space)) {
                current_template_data = saved_template_data; // Restore saved data as current
                if (pending_action) pending_action();
                ImGui::CloseCurrentPopup();
            }
            // Discard hover text
            if (ImGui::IsItemHovered()) {
                char press_space_discard_tooltip_buffer[1024];
                snprintf(press_space_discard_tooltip_buffer, sizeof(press_space_discard_tooltip_buffer),
                         "Press SPACE to discard.");
                ImGui::SetTooltip("%s", press_space_discard_tooltip_buffer);
            }
            ImGui::SameLine();
            // Cancel or pressing ESC
            if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            // Cancel hover text
            if (ImGui::IsItemHovered()) {
                char press_esc_cancel_tooltip_buffer[1024];
                snprintf(press_esc_cancel_tooltip_buffer, sizeof(press_esc_cancel_tooltip_buffer),
                         "Press ESC to cancel.");
                ImGui::SetTooltip("%s", press_esc_cancel_tooltip_buffer);
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginTabBar("EditorTabs")) {
            // "Advancements" or "Achievements" tab depending on version
            if (ImGui::BeginTabItem(advancements_label_plural_upper)) {
                // TWO-PANE LAYOUT
                float pane_width = ImGui::GetContentRegionAvail().x * 0.4f;
                ImGui::BeginChild("AdvancementListPane", ImVec2(pane_width, 0), true);

                // LEFT PANE: List of Advancements/Achievements

                // Import advancements/achievements on its own line
                char import_button_label[64];
                snprintf(import_button_label, sizeof(import_button_label), "Import %s",
                         advancements_label_plural_upper);
                if (ImGui::Button(import_button_label)) {
                    current_advancement_import_mode = BATCH_ADVANCEMENT_IMPORT; // Reset mode to default
                    char start_path[MAX_PATH_LENGTH];
                    // Determine the correct starting path based on version and settings
                    if (creator_selected_version <= MC_VERSION_1_6_4) {
                        // Legacy achievements are in the stats file (global or local)
                        if (app_settings->using_stats_per_world_legacy) {
                            snprintf(start_path, sizeof(start_path), "%s/%s/stats/", t->saves_path, t->world_name);
                        } else {
                            char parent_dir[MAX_PATH_LENGTH];
                            if (get_parent_directory(t->saves_path, parent_dir, sizeof(parent_dir), 1)) {
                                snprintf(start_path, sizeof(start_path), "%s/stats/", parent_dir);
                            } else {
                                strncpy(start_path, t->saves_path, sizeof(start_path)); // Fallback
                                start_path[sizeof(start_path) - 1] = '\0';
                            }
                        }
                    } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                        // Mid-era achievements are in the per-world stats file
                        snprintf(start_path, sizeof(start_path), "%s/%s/stats/", t->saves_path, t->world_name);
                    } else {
                        // Modern advancements are in their own per-world folder
                        snprintf(start_path, sizeof(start_path), "%s/%s/advancements/", t->saves_path, t->world_name);
                    }

                    // --- Version-aware file filters ---
#ifdef __APPLE__
                    const char *json_filter[2] = {"*.json", "public.json"};
                    const char *dat_filter[2] = {"*.dat", "public.data"};
                    int filter_count = 2;
#else
                    const char *json_filter[1] = {"*.json"};
                    const char *dat_filter[1] = {"*.dat"};
                    int filter_count = 1;
#endif
                    const char **selected_filter = (creator_selected_version <= MC_VERSION_1_6_4)
                                                       ? dat_filter
                                                       : json_filter;
                    const char *filter_desc = (creator_selected_version <= MC_VERSION_1_6_4)
                                                  ? "DAT files"
                                                  : "JSON files";
                    const char *dialog_title = (creator_selected_version < MC_VERSION_1_12)
                                                   ? "Select Player Stats File"
                                                   : "Select Player Advancements File";
                    const char *selection = tinyfd_openFileDialog(dialog_title, start_path, filter_count,
                                                                  selected_filter, filter_desc, 0);

                    if (selection) {
                        import_error_message[0] = '\0';
                        if (parse_player_advancements_for_import(selection, creator_selected_version,
                                                                 importable_advancements,
                                                                 import_error_message, sizeof(import_error_message))) {
                            show_import_advancements_popup = true;
                            focus_import_search = true; // Focus search bar as soon as popup opens
                            import_search_criteria_only = false; // Reset search mode
                            import_select_criteria = false; // Reset selection mode
                        } else {
                            // If parsing fails, show the error in the main status message
                            save_message_type = MSG_ERROR;
                            strncpy(status_message, import_error_message, sizeof(status_message) - 1);
                            status_message[sizeof(status_message) - 1] = '\0';
                        }
                    }
                }
                if (ImGui::IsItemHovered()) {
                    char import_stats_tooltip[512];
                    if (creator_selected_version <= MC_VERSION_1_6_4) {
                        if (app_settings->using_stats_per_world_legacy) {
                            // Legacy using stats per world
                            snprintf(import_stats_tooltip, sizeof(import_stats_tooltip),
                                     "Import legacy achievements directly from a local world's player achievements .dat file.\n"
                                     "Cannot import already existing root names.");
                        } else {
                            // Legacy not using stats per world
                            snprintf(import_stats_tooltip, sizeof(import_stats_tooltip),
                                     "Import legacy achievements directly from a global world's player achievements .dat file.\n"
                                     "Cannot import already existing root names.");
                        }
                    } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                        // Mid-era
                        snprintf(import_stats_tooltip, sizeof(import_stats_tooltip),
                                 "Import mid-era achievements directly from a world's player achievements .json file.\n"
                                 "Cannot import already existing root names.");
                    } else {
                        // Modern
                        snprintf(import_stats_tooltip, sizeof(import_stats_tooltip),
                                 "Import modern advancements/recipes directly from a world's player advancements .json file.\n"
                                 "Cannot import already existing root names.");
                    }
                    ImGui::SetTooltip("%s", import_stats_tooltip);
                }
                char button_label[64];
                snprintf(button_label, sizeof(button_label), "Add New %s", advancements_label_upper);
                if (ImGui::Button(button_label)) {
                    // Create new adv/ach with default values
                    EditorTrackableCategory new_adv = {};
                    int counter = 1;
                    while (true) {
                        char temp_name[256];
                        if (creator_selected_version <= MC_VERSION_1_6_4) {
                            // Legacy
                            snprintf(temp_name, sizeof(new_adv.root_name), "5242880_%d", counter);
                        } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                            // Mid-era
                            snprintf(temp_name, sizeof(new_adv.root_name), "achievement.new_%d", counter);
                        } else {
                            // Modern
                            snprintf(temp_name, sizeof(new_adv.root_name), "awesome:new/advancement_%d", counter);
                        }

                        bool name_exists = false;
                        for (const auto &adv: current_template_data.advancements) {
                            if (strcmp(adv.root_name, temp_name) == 0) {
                                name_exists = true;
                                break;
                            }
                        }
                        if (!name_exists) {
                            strncpy(new_adv.root_name, temp_name, sizeof(new_adv.root_name) - 1);
                            new_adv.root_name[sizeof(new_adv.root_name) - 1] = '\0';
                            break;
                        }
                        counter++;
                    }
                    snprintf(new_adv.display_name, sizeof(new_adv.display_name), "New %s %d", advancements_label_upper,
                             counter);
                    strncpy(new_adv.icon_path, "blocks/placeholder.png", sizeof(new_adv.icon_path) - 1);
                    new_adv.icon_path[sizeof(new_adv.icon_path) - 1] = '\0';
                    current_template_data.advancements.push_back(new_adv);
                    save_message_type = MSG_NONE;
                }
                if (ImGui::IsItemHovered()) {
                    char add_new_advancement_tooltip_buffer[1024];
                    if (creator_selected_version <= MC_VERSION_1_6_4) {
                        // Legacy
                        snprintf(add_new_advancement_tooltip_buffer, sizeof(add_new_advancement_tooltip_buffer),
                                 "Add a new blank achievement to this template.\n\n"
                                 "Achievements act as a guide to completing tasks ingame\n"
                                 "and additionally serve as challenges.\n"
                                 "Advancely looks for achievements (e.g., '5242888' - The Lie)\n"
                                 "within the (global or local) stats file.\n\n"
                                 "Click the 'Help' button for more info.");
                    } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                        // Mid-era
                        snprintf(add_new_advancement_tooltip_buffer, sizeof(add_new_advancement_tooltip_buffer),
                                 "Add a new blank achievement to this template.\n\n"
                                 "Achievements act as a guide to completing tasks ingame\n"
                                 "and additionally serve as challenges.\n"
                                 "Advancely looks for achievements (e.g., 'achievement.buildWorkBench') within the stats file.\n\n"
                                 "Click the 'Help' button for more info.");
                    } else {
                        // Modern
                        snprintf(add_new_advancement_tooltip_buffer, sizeof(add_new_advancement_tooltip_buffer),
                                 "Add a new blank advancement or recipe to this template.\n\n"
                                 "Advancements act as a guide to completing tasks ingame and additionally serve as challenges.\n"
                                 "Recipes (e.g., crafting, smelting, ...) are a structured way to perform item and block transformations.\n"
                                 "Advancely looks for both advancements (e.g., 'minecraft:nether/all_effects') and recipes\n"
                                 "(e.g., 'minecraft:recipes/misc/mojang_banner_pattern') within the advancements file.\n\n"
                                 "Click the 'Help' button for more info.");
                    }
                    ImGui::SetTooltip("%s", add_new_advancement_tooltip_buffer);
                }
                ImGui::SameLine();
                ImGui::Checkbox("Show Display Names", &show_advancement_display_names);
                if (ImGui::IsItemHovered()) {
                    char show_display_names_tooltip_buffer[256];
                    snprintf(show_display_names_tooltip_buffer, sizeof(show_display_names_tooltip_buffer),
                             "Toggle between showing user-facing display names and internal root names in this list.");
                    ImGui::SetTooltip("%s", show_display_names_tooltip_buffer);
                }
                ImGui::Separator();

                // Filtering and Rendering Logic

                // 1. Create a list of pointers to render from
                std::vector<EditorTrackableCategory *> advancements_to_render;

                // 2. Populate the list: either with all items, or with filtered items.
                bool search_active = (tc_search_buffer[0] != '\0' && current_search_scope == SCOPE_ADVANCEMENTS);

                if (search_active) {
                    // Search is active for this scope: populate with filtered results.
                    for (auto &advancement: current_template_data.advancements) {
                        bool parent_match = str_contains_insensitive(advancement.display_name, tc_search_buffer) ||
                                            str_contains_insensitive(advancement.root_name, tc_search_buffer) ||
                                            str_contains_insensitive(advancement.icon_path, tc_search_buffer);

                        if (parent_match) {
                            advancements_to_render.push_back(&advancement);
                            continue; // Parent matches, no need to check children
                        }

                        // If parent doesn't match, check its children (criteria)
                        bool child_match = false;
                        for (const auto &criterion: advancement.criteria) {
                            if (str_contains_insensitive(criterion.display_name, tc_search_buffer) ||
                                str_contains_insensitive(criterion.root_name, tc_search_buffer) ||
                                str_contains_insensitive(criterion.icon_path, tc_search_buffer)) {
                                child_match = true;
                                break;
                            }
                        }

                        if (child_match) {
                            advancements_to_render.push_back(&advancement);
                        }
                    }
                } else {
                    // Search is not active: populate with pointers to all items.
                    for (auto &advancement: current_template_data.advancements) {
                        advancements_to_render.push_back(&advancement);
                    }
                }

                // --- Counter for the list ---
                char counter_text[128];
                snprintf(counter_text, sizeof(counter_text), "%zu %s", advancements_to_render.size(),
                         advancements_to_render.size() == 1
                             ? advancements_label_upper
                             : advancements_label_plural_upper);
                float text_width = ImGui::CalcTextSize(counter_text).x;
                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - text_width) *
                    0.5f);
                ImGui::TextDisabled("%s", counter_text);

                int advancement_to_remove_idx = -1;
                int advancement_to_copy_idx = -1; // To queue a copy action

                // State for drag and drop
                int adv_dnd_source_index = -1;
                int adv_dnd_target_index = -1;

                for (size_t i = 0; i < advancements_to_render.size(); ++i) {
                    auto &advancement = *advancements_to_render[i]; // Dereference the pointer
                    ImGui::PushID(&advancement); // Use pointer for a stable ID

                    const char *display_name = advancement.display_name;
                    const char *root_name = advancement.root_name;

                    // Show display names based on setting
                    const char *label = show_advancement_display_names
                                            ? (display_name[0] ? display_name : root_name)
                                            : root_name;
                    if (label[0] == '\0') {
                        char placeholder[64];
                        snprintf(placeholder, sizeof(placeholder), "[New %s]", advancements_label_upper);
                        label = placeholder;
                    }

                    // Draw the "X" (Remove) button
                    if (ImGui::Button("X")) {
                        advancement_to_remove_idx = i;
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[128];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Remove %s", label);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    ImGui::SameLine();

                    // Draw the "Copy" button
                    if (ImGui::Button("Copy")) {
                        advancement_to_copy_idx = i;
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[128];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Duplicate %s.", label);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    ImGui::SameLine();

                    if (ImGui::Selectable(label, &advancement == selected_advancement)) {
                        // Compare pointers
                        // Check if the user is selecting a *different* item than the one currently selected
                        if (&advancement != selected_advancement) {
                            if (editor_has_unsaved_changes) {
                                show_unsaved_changes_popup = true;
                                // The pending action now captures the pointer to the newly selected advancement
                                pending_action = [&, new_selection = &advancement]() {
                                    selected_advancement = new_selection;
                                };
                            } else {
                                // No unsaved changes, so we can select the new item immediately
                                selected_advancement = &advancement;
                            }
                        }
                    }

                    // DRAG AND DROP LOGIC
                    // Make the entire row a drag source
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        // Use a unique payload ID for this list
                        ImGui::SetDragDropPayload("ADVANCEMENT_DND", &i, sizeof(int));
                        ImGui::Text("Reorder %s", label);
                        ImGui::EndDragDropSource();
                    }
                    // Make the entire row a drop target
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ADVANCEMENT_DND")) {
                            adv_dnd_source_index = *(const int *) payload->Data;
                            adv_dnd_target_index = i;
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::PopID();
                }

                // Handle Drag and Drop reordering after the loop
                if (adv_dnd_source_index != -1 && adv_dnd_target_index != -1) {
                    // Get pointers to the source and target items from the list that was rendered
                    EditorTrackableCategory *source_item_ptr = advancements_to_render[adv_dnd_source_index];
                    EditorTrackableCategory *target_item_ptr = advancements_to_render[adv_dnd_target_index];

                    // Find the iterator for the source item in the original data vector
                    auto source_it = std::find_if(current_template_data.advancements.begin(),
                                                  current_template_data.advancements.end(),
                                                  [&](const EditorTrackableCategory &adv) {
                                                      return &adv == source_item_ptr;
                                                  });

                    // Create a copy of the item to move, then erase it
                    EditorTrackableCategory item_to_move = *source_item_ptr;
                    current_template_data.advancements.erase(source_it);

                    // Find the iterator for the target item in the (now modified) original vector
                    auto target_it = std::find_if(current_template_data.advancements.begin(),
                                                  current_template_data.advancements.end(),
                                                  [&](const EditorTrackableCategory &adv) {
                                                      return &adv == target_item_ptr;
                                                  });

                    // Insert the copied item at the target's position
                    current_template_data.advancements.insert(target_it, item_to_move);

                    save_message_type = MSG_NONE;
                }

                // Handle Copying
                if (advancement_to_copy_idx != -1) {
                    char selected_root_name_before_op[192] = {};
                    if (selected_advancement) {
                        strncpy(selected_root_name_before_op, selected_advancement->root_name,
                                sizeof(selected_root_name_before_op) - 1);
                    }
                    // Get a pointer to the source item to copy from the filtered list
                    const EditorTrackableCategory *source_adv_ptr = advancements_to_render[advancement_to_copy_idx];

                    // Perform a manual, safe copy to prevent memory corruption.
                    EditorTrackableCategory new_advancement;
                    strncpy(new_advancement.root_name, source_adv_ptr->root_name, sizeof(new_advancement.root_name));
                    new_advancement.root_name[sizeof(new_advancement.root_name) - 1] = '\0';
                    strncpy(new_advancement.display_name, source_adv_ptr->display_name,
                            sizeof(new_advancement.display_name));
                    new_advancement.display_name[sizeof(new_advancement.display_name) - 1] = '\0';
                    strncpy(new_advancement.icon_path, source_adv_ptr->icon_path, sizeof(new_advancement.icon_path));
                    new_advancement.icon_path[sizeof(new_advancement.icon_path) - 1] = '\0';
                    new_advancement.is_hidden = source_adv_ptr->is_hidden;
                    new_advancement.is_recipe = source_adv_ptr->is_recipe;
                    new_advancement.is_simple_stat = source_adv_ptr->is_simple_stat;
                    new_advancement.criteria = source_adv_ptr->criteria; // std::vector handles its own deep copy.

                    // --- Generate a unique root_name for the copy ---
                    char base_name[192];
                    strncpy(base_name, source_adv_ptr->root_name, sizeof(base_name) - 1);
                    base_name[sizeof(base_name) - 1] = '\0';

                    char new_name[192];
                    int copy_counter = 1;
                    while (true) {
                        if (copy_counter == 1) snprintf(new_name, sizeof(new_name), "%s_copy", base_name);
                        else snprintf(new_name, sizeof(new_name), "%s_copy%d", base_name, copy_counter);


                        // Check against the full original list to ensure the name is truly unique
                        bool name_exists = false;
                        for (const auto &adv: current_template_data.advancements) {
                            if (strcmp(adv.root_name, new_name) == 0) {
                                name_exists = true;
                                break;
                            }
                        }
                        if (!name_exists) break; // Found a unique name
                        copy_counter++;
                    }
                    strncpy(new_advancement.root_name, new_name, sizeof(new_advancement.root_name) - 1);
                    new_advancement.root_name[sizeof(new_advancement.root_name) - 1] = '\0';
                    // --- End of unique name generation ---

                    // Find the position of the source item in the original vector
                    auto it = std::find_if(current_template_data.advancements.begin(),
                                           current_template_data.advancements.end(),
                                           [&](const EditorTrackableCategory &adv) {
                                               return &adv == source_adv_ptr;
                                           });

                    // Insert the new copy right after the source item
                    if (it != current_template_data.advancements.end()) {
                        current_template_data.advancements.insert(it + 1, new_advancement);
                    } else {
                        // Fallback: if source not found (shouldn't happen), add to the end
                        current_template_data.advancements.push_back(new_advancement);
                    }
                    // Re-find and update the selected_advancement pointer
                    if (selected_root_name_before_op[0] != '\0') {
                        for (auto &adv: current_template_data.advancements) {
                            if (strcmp(adv.root_name, selected_root_name_before_op) == 0) {
                                selected_advancement = &adv;
                                break;
                            }
                        }
                    }
                    save_message_type = MSG_NONE;
                }

                // Handle Removal
                if (advancement_to_remove_idx != -1) {
                    char selected_root_name_before_op[192] = {};
                    if (selected_advancement) {
                        strncpy(selected_root_name_before_op, selected_advancement->root_name,
                                sizeof(selected_root_name_before_op) - 1);
                    }

                    EditorTrackableCategory *adv_to_remove = advancements_to_render[advancement_to_remove_idx];
                    if (selected_advancement == adv_to_remove) {
                        selected_advancement = nullptr;
                    }

                    current_template_data.advancements.erase(
                        std::remove_if(current_template_data.advancements.begin(),
                                       current_template_data.advancements.end(),
                                       [&](const EditorTrackableCategory &adv) { return &adv == adv_to_remove; }),
                        current_template_data.advancements.end()
                    );

                    // Re-find pointer if it wasn't the one being deleted
                    if (selected_advancement && selected_root_name_before_op[0] != '\0') {
                        for (auto &adv: current_template_data.advancements) {
                            if (strcmp(adv.root_name, selected_root_name_before_op) == 0) {
                                selected_advancement = &adv;
                                break;
                            }
                        }
                    }
                    save_message_type = MSG_NONE;
                }

                ImGui::EndChild(); // End of Left Pane
                ImGui::SameLine();

                // RIGHT PANE: Details of Selected Advancement
                ImGui::BeginChild("AdvancementDetailsPane", ImVec2(0, 0), true);
                if (selected_advancement != nullptr) {
                    auto &advancement = *selected_advancement; // Dereference pointer

                    char details_title[64];
                    snprintf(details_title, sizeof(details_title), "Edit %s Details", advancements_label_upper);
                    ImGui::Text("%s", details_title);
                    ImGui::Separator();

                    if (ImGui::InputText("Root Name", advancement.root_name, sizeof(advancement.root_name))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char root_name_tooltip_buffer[128];
                        if (creator_selected_version <= MC_VERSION_1_6_4) {
                            // Legacy
                            snprintf(root_name_tooltip_buffer, sizeof(root_name_tooltip_buffer),
                                     "The unique in-game ID for this %s, e.g., '5242896' (Sniper Duel).",
                                     advancements_label_upper);
                        } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                            // Mid-era
                            snprintf(root_name_tooltip_buffer, sizeof(root_name_tooltip_buffer),
                                     "The unique in-game ID for this %s, e.g., 'achievement.exploreAllBiomes'.",
                                     advancements_label_upper);
                        } else {
                            // Modern
                            snprintf(root_name_tooltip_buffer, sizeof(root_name_tooltip_buffer),
                                     "The unique in-game ID for this %s, e.g., 'minecraft:story/mine_stone'.",
                                     advancements_label_upper);
                        }
                        ImGui::SetTooltip("%s", root_name_tooltip_buffer);
                    }
                    if (ImGui::InputText("Display Name", advancement.display_name, sizeof(advancement.display_name))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char display_name_tooltip_buffer[128];
                        snprintf(display_name_tooltip_buffer, sizeof(display_name_tooltip_buffer),
                                 "The user-facing name that appears on the tracker/overlay.");
                        ImGui::SetTooltip("%s", display_name_tooltip_buffer);
                    }
                    if (ImGui::InputText("Icon Path", advancement.icon_path, sizeof(advancement.icon_path))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char icon_path_tooltip_buffer[128];
                        snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                 "Path to the icon file, relative to the 'resources/icons' directory.");
                        ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##AdvIcon")) {
                        char new_path[MAX_PATH_LENGTH];
                        if (open_icon_file_dialog(new_path, sizeof(new_path))) {
                            strncpy(advancement.icon_path, new_path, sizeof(advancement.icon_path) - 1);
                            advancement.icon_path[sizeof(advancement.icon_path) - 1] = '\0';
                            save_message_type = MSG_NONE;
                        }
                    }
                    // resource folder tooltip
                    if (ImGui::IsItemHovered()) {
                        char resource_folder_tooltip_buffer[1024];
                        snprintf(resource_folder_tooltip_buffer, sizeof(resource_folder_tooltip_buffer),
                                 "The icon must be inside the 'resources/icons' folder!");
                        ImGui::SetTooltip("%s", resource_folder_tooltip_buffer);
                    }
                    // Add the "Is Recipe" checkbox only for modern versions
                    if (creator_selected_version >= MC_VERSION_1_12) {
                        if (ImGui::Checkbox("Is Recipe", &advancement.is_recipe)) {
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char is_recipe_tooltip_buffer[512];
                            snprintf(is_recipe_tooltip_buffer, sizeof(is_recipe_tooltip_buffer),
                                     "Check this if the advancements entry is a recipe.\n"
                                     "Recipes have their own tracker section and count towards the\n"
                                     "percentage progress and not the main advancement counter.");
                            ImGui::SetTooltip("%s", is_recipe_tooltip_buffer);
                        }
                        ImGui::SameLine();
                    }
                    if (ImGui::Checkbox("Hidden", &advancement.is_hidden)) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char hidden_tooltip_buffer[256];
                        snprintf(hidden_tooltip_buffer, sizeof(hidden_tooltip_buffer),
                                 "If checked, this %s will be fully hidden on the overlay\n"
                                 "and hidden settings-based on the tracker.\n"
                                 "Visibility can be toggled in the main tracker settings.\n",
                                 advancements_label_singular_lower);
                        ImGui::SetTooltip("%s", hidden_tooltip_buffer);
                    }
                    // Conditionally render the criteria section only for versions that support it.
                    if (creator_selected_version > MC_VERSION_1_6_4) {
                        ImGui::Separator();
                        ImGui::Text("Criteria");

                        // --- Counter for the criteria list ---
                        char crit_counter_text[128];
                        bool is_details_search_active = (
                            current_search_scope == SCOPE_ADVANCEMENT_DETAILS && tc_search_buffer[0] != '\0');
                        int visible_criteria_count = 0;
                        if (!is_details_search_active) {
                            visible_criteria_count = advancement.criteria.size();
                        } else {
                            for (const auto &criterion: advancement.criteria) {
                                if (str_contains_insensitive(criterion.display_name, tc_search_buffer) ||
                                    str_contains_insensitive(criterion.root_name, tc_search_buffer) ||
                                    str_contains_insensitive(criterion.icon_path, tc_search_buffer)) {
                                    visible_criteria_count++;
                                }
                            }
                        }
                        snprintf(crit_counter_text, sizeof(crit_counter_text), "%d %s", visible_criteria_count,
                                 visible_criteria_count == 1 ? "Criterion" : "Criteria");
                        float crit_text_width = ImGui::CalcTextSize(crit_counter_text).x;
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - crit_text_width);
                        ImGui::TextDisabled("%s", crit_counter_text);

                        // Import Criteria Button
                        char import_crit_label[128];
                        snprintf(import_crit_label, sizeof(import_crit_label), "Import %s Criteria",
                                 advancements_label_upper);
                        if (ImGui::Button(import_crit_label)) {
                            current_advancement_import_mode = CRITERIA_ONLY_IMPORT;
                            // This logic is copied from the main "Import Advancements" button
                            char start_path[MAX_PATH_LENGTH];
                            if (creator_selected_version <= MC_VERSION_1_11_2) {
                                snprintf(start_path, sizeof(start_path), "%s/%s/stats/", t->saves_path, t->world_name);
                            } else {
                                snprintf(start_path, sizeof(start_path), "%s/%s/advancements/", t->saves_path,
                                         t->world_name);
                            }
#ifdef __APPLE__
                            const char *json_filter[2] = {"*.json", "public.json"};
                            int filter_count = 2;
#else
                            const char *json_filter[1] = {"*.json"};
                            int filter_count = 1;
#endif
                            const char *dialog_title = (creator_selected_version < MC_VERSION_1_12)
                                                           ? "Select Player Stats File"
                                                           : "Select Player Advancements File";

                            const char *selection = tinyfd_openFileDialog(
                                dialog_title, start_path, filter_count, json_filter, "JSON files", 0);

                            if (selection) {
                                import_error_message[0] = '\0';
                                if (parse_player_advancements_for_import(selection, creator_selected_version,
                                                                         importable_advancements,
                                                                         import_error_message,
                                                                         sizeof(import_error_message))) {
                                    show_import_advancements_popup = true;
                                    focus_import_search = true;
                                    import_search_criteria_only = true; // Default search to criteria
                                    import_select_criteria = true; // Default selection to criteria
                                } else {
                                    save_message_type = MSG_ERROR;
                                    strncpy(status_message, import_error_message, sizeof(status_message) - 1);
                                }
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            char tooltip[512];
                            if (creator_selected_version <= MC_VERSION_1_11_2) {
                                snprintf(tooltip, sizeof(tooltip),
                                         "Import criteria for this %s directly from a player stats file.",
                                         advancements_label_singular_lower);
                            } else {
                                snprintf(tooltip, sizeof(tooltip),
                                         "Import criteria for this %s directly from a player advancements file.",
                                         advancements_label_singular_lower);
                            }
                            ImGui::SetTooltip("%s", tooltip);
                        }
                        ImGui::SameLine();

                        char criterion_add_tooltip_buffer[256];
                        snprintf(criterion_add_tooltip_buffer, sizeof(criterion_add_tooltip_buffer),
                                 "Add New %s Criterion",
                                 advancements_label_upper);
                        if (ImGui::Button(criterion_add_tooltip_buffer)) {
                            // Create new adv/ach criterion with default values
                            EditorTrackableItem new_crit = {};
                            int counter = 1;
                            while (true) {
                                snprintf(new_crit.root_name, sizeof(new_crit.root_name), "new_criterion_%d", counter);
                                bool name_exists = false;
                                for (const auto &crit: advancement.criteria) {
                                    if (strcmp(crit.root_name, new_crit.root_name) == 0) {
                                        name_exists = true;
                                        break;
                                    }
                                }
                                if (!name_exists) break;
                                counter++;
                            }
                            snprintf(new_crit.display_name, sizeof(new_crit.display_name), "New Criterion %d", counter);
                            strncpy(new_crit.icon_path, "blocks/placeholder.png", sizeof(new_crit.icon_path) - 1);
                            new_crit.icon_path[sizeof(new_crit.icon_path) - 1] = '\0';
                            advancement.criteria.push_back(new_crit);
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char add_new_criterion_tooltip_buffer[1024];
                            if (creator_selected_version <= MC_VERSION_1_11_2) {
                                // Mid-era
                                snprintf(add_new_criterion_tooltip_buffer, sizeof(add_new_criterion_tooltip_buffer),
                                         "Add a new blank achievement criterion to this template.\n\n"
                                         "Achievements can have sub-tasks that must all be completed.\n"
                                         "Advancely looks for achievement criteria (e.g., 'Swampland' of 'achievement.exploreAllBiomes')\n"
                                         "within the stats file.\n\n"
                                         "Click the 'Help' button for more info.");
                            } else {
                                // Modern
                                snprintf(add_new_criterion_tooltip_buffer, sizeof(add_new_criterion_tooltip_buffer),
                                         "Add a new blank advancement/recipe criterion to this template.\n\n"
                                         "Advancements can have sub-tasks that must all be completed.\n"
                                         "Advancely looks for advancement criteria (e.g., 'enchanted_golden_apple'\n"
                                         "of 'minecraft:husbandry/balanced_diet') and recipe criteria\n"
                                         "(e.g., 'has_nether_star' of 'minecraft:recipes/misc/beacon') within the advancements file.\n\n"
                                         "Click the 'Help' button for more info.");
                            }
                            ImGui::SetTooltip("%s", add_new_criterion_tooltip_buffer);
                        }
                    } // End of conditional criteria block for above 1.6.4

                    // Determine if a details search is active
                    bool is_details_search_active = (
                        current_search_scope == SCOPE_ADVANCEMENT_DETAILS && tc_search_buffer[0] != '\0');

                    int criterion_to_remove = -1;
                    int criterion_to_copy = -1;

                    // State for drag and drop
                    int criterion_dnd_source_index = -1;
                    int criterion_dnd_target_index = -1;

                    for (size_t j = 0; j < advancement.criteria.size(); j++) {
                        auto &criterion = advancement.criteria[j];

                        if (is_details_search_active) {
                            if (!str_contains_insensitive(criterion.display_name, tc_search_buffer) &&
                                !str_contains_insensitive(criterion.root_name, tc_search_buffer) &&
                                !str_contains_insensitive(criterion.icon_path, tc_search_buffer)) {
                                continue;
                            }
                        }

                        ImGui::PushID(j);


                        // Add vertical spacing creating the gap
                        ImGui::Spacing();

                        // Create a wide, 8-pixel-high invisible button to act as our drop zone
                        ImGui::InvisibleButton("drop_target", ImVec2(-1, 8.0f));

                        // We make the separator a drop target to allow dropping between items
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CRITERION_DND")) {
                                criterion_dnd_source_index = *(const int *) payload->Data;
                                criterion_dnd_target_index = j;
                            }
                            ImGui::EndDragDropTarget();
                        }

                        // Draw a separator for visual feedback after the drop zone
                        ImGui::Separator();

                        // Use an invisible button overlaying the group as a drag handle
                        ImVec2 item_start_cursor_pos = ImGui::GetCursorScreenPos();
                        ImGui::BeginGroup();

                        if (ImGui::InputText("Criterion Root Name", criterion.root_name, sizeof(criterion.root_name))) {
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char root_name_tooltip_buffer[128];
                            if (creator_selected_version <= MC_VERSION_1_11_2) {
                                // Mid-era
                                snprintf(root_name_tooltip_buffer, sizeof(root_name_tooltip_buffer),
                                         "The unique in-game ID for this criterion, e.g., 'Forest'.");
                            } else {
                                // Modern
                                snprintf(root_name_tooltip_buffer, sizeof(root_name_tooltip_buffer),
                                         "The unique in-game ID for this criterion, e.g., 'minecraft:hoglin'.");
                            }
                            ImGui::SetTooltip("%s", root_name_tooltip_buffer);
                        }
                        if (ImGui::InputText("Display Name", criterion.display_name, sizeof(criterion.display_name))) {
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char display_name_tooltip_buffer[128];
                            snprintf(display_name_tooltip_buffer, sizeof(display_name_tooltip_buffer),
                                     "The user-facing name for this criterion.");
                            ImGui::SetTooltip("%s", display_name_tooltip_buffer);
                        }
                        if (ImGui::InputText("Icon Path", criterion.icon_path, sizeof(criterion.icon_path))) {
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char icon_path_tooltip_buffer[1024];
                            snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                     "Path to the icon file, relative to the 'resources/icons' directory.");
                            ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Browse##CritIcon")) {
                            char new_path[MAX_PATH_LENGTH];
                            if (open_icon_file_dialog(new_path, sizeof(new_path))) {
                                strncpy(criterion.icon_path, new_path, sizeof(criterion.icon_path) - 1);
                                criterion.icon_path[sizeof(criterion.icon_path) - 1] = '\0';
                                save_message_type = MSG_NONE;
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            char icon_path_tooltip_buffer[1024];
                            snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                     "The icon must be inside the 'resources/icons' folder!");
                            ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                        }
                        if (ImGui::Checkbox("Hidden", &criterion.is_hidden)) {
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char hidden_tooltip_buffer[1024];
                            snprintf(hidden_tooltip_buffer, sizeof(hidden_tooltip_buffer),
                                     "If checked, the icon of the criterion will be fully hidden on the overlay\n"
                                     "(within 1st row) and hidden settings-based on the tracker.\n"
                                     "The criterion name will still display below the %s name\n"
                                     "on the overlay if it's the last one remaining.\n"
                                     "This means it will still contribute to the horizontal spacing\n"
                                     "of the second row unless the advancement is hidden.\n"
                                     "Visibility can be toggled in the main tracker settings.",
                                     advancements_label_singular_lower);
                            ImGui::SetTooltip("%s", hidden_tooltip_buffer);
                        }

                        ImGui::SameLine();

                        // "Copy" button for criteria
                        if (ImGui::Button("Copy")) {
                            criterion_to_copy = j;
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[128];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Duplicate %s Criterion:\n%s",
                                     advancements_label_upper, criterion.root_name);
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }
                        ImGui::SameLine();

                        if (ImGui::Button("Remove")) {
                            criterion_to_remove = j;
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[128];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Remove %s Criterion:\n%s",
                                     advancements_label_upper, criterion.root_name);
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }

                        ImGui::EndGroup();
                        ImGui::SetCursorScreenPos(item_start_cursor_pos);
                        ImGui::InvisibleButton("dnd_handle", ImGui::GetItemRectSize());

                        // Use the flag to make the non-interactive group a drag source
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            ImGui::SetDragDropPayload("CRITERION_DND", &j, sizeof(int));
                            ImGui::Text("Reorder %s", criterion.root_name);
                            ImGui::EndDragDropSource();
                        }

                        ImGui::PopID();
                    }

                    // Final drop target to allow dropping at the end of the list
                    ImGui::InvisibleButton("final_drop_target_adv_crit", ImVec2(-1, 8.0f)); // Added larger drop zone
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CRITERION_DND")) {
                            criterion_dnd_source_index = *(const int *) payload->Data;
                            criterion_dnd_target_index = advancement.criteria.size();
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // Handle actions after the loop
                    if (criterion_dnd_source_index != -1 && criterion_dnd_target_index != -1 &&
                        criterion_dnd_source_index != criterion_dnd_target_index) {
                        EditorTrackableItem item_to_move = advancement.criteria[criterion_dnd_source_index];
                        advancement.criteria.erase(advancement.criteria.begin() + criterion_dnd_source_index);
                        if (criterion_dnd_target_index > criterion_dnd_source_index) criterion_dnd_target_index--;
                        advancement.criteria.insert(advancement.criteria.begin() + criterion_dnd_target_index,
                                                    item_to_move);
                        save_message_type = MSG_NONE;
                    }


                    if (criterion_to_remove != -1) {
                        advancement.criteria.erase(advancement.criteria.begin() + criterion_to_remove);
                        save_message_type = MSG_NONE;
                    }

                    // Logic to handle the copy action after the loop
                    if (criterion_to_copy != -1) {
                        const auto &source_criterion = advancement.criteria[criterion_to_copy];


                        // Perform a manual, safe copy.
                        EditorTrackableItem new_criterion;
                        strncpy(new_criterion.root_name, source_criterion.root_name, sizeof(new_criterion.root_name));
                        new_criterion.root_name[sizeof(new_criterion.root_name) - 1] = '\0';
                        strncpy(new_criterion.display_name, source_criterion.display_name,
                                sizeof(new_criterion.display_name));
                        new_criterion.display_name[sizeof(new_criterion.display_name) - 1] = '\0';
                        strncpy(new_criterion.icon_path, source_criterion.icon_path, sizeof(new_criterion.icon_path));
                        new_criterion.icon_path[sizeof(new_criterion.icon_path) - 1] = '\0';
                        new_criterion.goal = source_criterion.goal;
                        new_criterion.is_hidden = source_criterion.is_hidden;

                        char base_name[192];
                        strncpy(base_name, source_criterion.root_name, sizeof(base_name) - 1);
                        base_name[sizeof(base_name) - 1] = '\0';
                        char new_name[192];
                        int copy_counter = 1;
                        while (true) {
                            if (copy_counter == 1) snprintf(new_name, sizeof(new_name), "%s_copy", base_name);
                            else snprintf(new_name, sizeof(new_name), "%s_copy%d", base_name, copy_counter);
                            bool name_exists = false;
                            for (const auto &crit: advancement.criteria) {
                                if (strcmp(crit.root_name, new_name) == 0) {
                                    name_exists = true;
                                    break;
                                }
                            }
                            if (!name_exists) break;
                            copy_counter++;
                        }
                        strncpy(new_criterion.root_name, new_name, sizeof(new_criterion.root_name) - 1);
                        new_criterion.root_name[sizeof(new_criterion.root_name) - 1] = '\0';
                        advancement.criteria.insert(advancement.criteria.begin() + criterion_to_copy + 1,
                                                    new_criterion);
                        save_message_type = MSG_NONE;
                    }
                } else {
                    char select_prompt[128];
                    snprintf(select_prompt, sizeof(select_prompt), "Select an %s from the list to edit its details.",
                             advancements_label_upper);
                    ImGui::Text("%s", select_prompt);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Stats")) {
                // TWO PANE LAYOUT for Stats
                float pane_width = ImGui::GetContentRegionAvail().x * 0.4f;
                ImGui::BeginChild("StatListPane", ImVec2(pane_width, 0), true);

                if (ImGui::Button("Import Stats")) {
                    current_stat_import_mode = IMPORT_AS_TOP_LEVEL; // Set the mode as top level not sub-stat
                    char start_path[MAX_PATH_LENGTH];
                    // Determine the correct starting path based on version and settings
                    if (creator_selected_version <= MC_VERSION_1_6_4) {
                        if (app_settings->using_stats_per_world_legacy) {
                            // Legacy, Per-World: .../saves/WORLD_NAME/stats/
                            snprintf(start_path, sizeof(start_path), "%s/%s/stats/", t->saves_path, t->world_name);
                        } else {
                            // Legacy, Global: .../stats/
                            char parent_dir[MAX_PATH_LENGTH];
                            if (get_parent_directory(t->saves_path, parent_dir, sizeof(parent_dir), 1)) {
                                snprintf(start_path, sizeof(start_path), "%s/stats/", parent_dir);
                            } else {
                                strncpy(start_path, t->saves_path, sizeof(start_path)); // Fallback
                                start_path[sizeof(start_path) - 1] = '\0';
                            }
                        }
                    } else {
                        // Mid-era and Modern stats are always in a per-world stats folder
                        snprintf(start_path, sizeof(start_path), "%s/%s/stats/", t->saves_path, t->world_name);
                    }

                    // --- Version-aware file filters ---
#ifdef __APPLE__
                    const char *json_filter[2] = {"*.json", "public.json"};
                    const char *dat_filter[2] = {"*.dat", "public.data"};
                    int filter_count = 2;
#else
                    const char *json_filter[1] = {"*.json"};
                    const char *dat_filter[1] = {"*.dat"};
                    int filter_count = 1;
#endif
                    const char **selected_filter = (creator_selected_version <= MC_VERSION_1_6_4)
                                                       ? dat_filter
                                                       : json_filter;
                    const char *filter_desc = (creator_selected_version <= MC_VERSION_1_6_4)
                                                  ? "DAT files"
                                                  : "JSON files";

                    const char *selection = tinyfd_openFileDialog("Select Player Stats File", start_path, filter_count,
                                                                  selected_filter, filter_desc, 0);

                    if (selection) {
                        import_error_message[0] = '\0';
                        if (parse_player_stats_for_import(selection, creator_selected_version, importable_stats,
                                                          import_error_message, sizeof(import_error_message))) {
                            show_import_stats_popup = true;
                            last_clicked_stat_index = -1; // Reset range selection
                        } else {
                            save_message_type = MSG_ERROR;
                            strncpy(status_message, import_error_message, sizeof(status_message) - 1);
                            status_message[sizeof(status_message) - 1] = '\0';
                        }
                    }
                }
                if (ImGui::IsItemHovered()) {
                    char tooltip[512];

                    if (creator_selected_version <= MC_VERSION_1_6_4) {
                        if (app_settings->using_stats_per_world_legacy) {
                            // Legacy using stats per world
                            snprintf(tooltip, sizeof(tooltip),
                                     "Import stats directly from a local world's player stats/achievements .dat file.\n"
                                     "Cannot import already existing root names.");
                        } else {
                            // Legacy not using stats per world
                            snprintf(tooltip, sizeof(tooltip),
                                     "Import stats directly from a global world's player stats/achievements .dat file.\n"
                                     "Cannot import already existing root names.");
                        }
                    } else if (creator_selected_version <= MC_VERSION_1_12_2) {
                        // Mid-era (1.7.2 - 1.12.2)
                        snprintf(tooltip, sizeof(tooltip),
                                 "Import stats directly from a world's player stats .json file.\n"
                                 "(Also contains achievements for 1.7.2 - 1.11.2).\n"
                                 "Cannot import already existing root names.");
                    } else {
                        // Modern (1.13+)
                        snprintf(tooltip, sizeof(tooltip),
                                 "Import stats directly from a world's player stats .json file.\n"
                                 "Cannot import already existing root names.");
                    }
                    ImGui::SetTooltip("%s", tooltip);
                }

                // New Line
                if (ImGui::Button("Add New Stat")) {
                    // Create new stat category with default values
                    EditorTrackableCategory new_stat = {};
                    int counter = 1;
                    while (true) {
                        snprintf(new_stat.root_name, sizeof(new_stat.root_name), "new_stat_%d", counter);
                        bool name_exists = false;
                        for (const auto &stat: current_template_data.stats) {
                            if (strcmp(stat.root_name, new_stat.root_name) == 0) {
                                name_exists = true;
                                break;
                            }
                        }
                        if (!name_exists) break;
                        counter++;
                    }
                    snprintf(new_stat.display_name, sizeof(new_stat.display_name), "New Stat %d", counter);
                    strncpy(new_stat.icon_path, "blocks/placeholder.png", sizeof(new_stat.icon_path) - 1);
                    new_stat.icon_path[sizeof(new_stat.icon_path) - 1] = '\0';
                    new_stat.is_simple_stat = true; // Default to simple

                    // Add a default criterion for the simple stat
                    EditorTrackableItem new_crit = {};
                    // Version-aware root name for the new criterion
                    if (creator_selected_version <= MC_VERSION_1_6_4) {
                        strncpy(new_crit.root_name, "0", sizeof(new_crit.root_name) - 1);
                        new_crit.root_name[sizeof(new_crit.root_name) - 1] = '\0';
                        // Legacy stats are numeric IDs
                    } else if (creator_selected_version <= MC_VERSION_1_12_2) {
                        // Mid-era stats are prefixed
                        snprintf(new_crit.root_name, sizeof(new_crit.root_name), "stat.new_stat_%d", counter);
                    } else {
                        // Modern (1.13+)
                        snprintf(new_crit.root_name, sizeof(new_crit.root_name),
                                 "minecraft:custom/minecraft:new_stat_%d", counter); // Modern
                    }
                    new_crit.root_name[sizeof(new_crit.root_name) - 1] = '\0';
                    new_crit.goal = 1; // Default to a completable goal
                    new_stat.criteria.push_back(new_crit);

                    current_template_data.stats.push_back(new_stat);
                    save_message_type = MSG_NONE;
                }
                if (ImGui::IsItemHovered()) {
                    char add_stat_tooltip_buffer[1024];
                    if (creator_selected_version <= MC_VERSION_1_6_4) {
                        // Legacy
                        snprintf(add_stat_tooltip_buffer, sizeof(add_stat_tooltip_buffer),
                                 "Add a new blank stat to this template.\n\n"
                                 "Statistics allow tracking of certain actions in form of numerical data.\n"
                                 "Advancely looks for statistics (e.g., '16908566'  - Times Used of\n"
                                 "Diamond Pickaxe) in the (global or local) stats file.\n"
                                 "Simple achievements (e.g., '5242880' - Taking Inventory) can also act as stats\n"
                                 "(e.g., How many time you've opened your inventory).\n\n"
                                 "Click the 'Help' button for more info.");
                    } else if (creator_selected_version <= MC_VERSION_1_12_2) {
                        // Mid-era (1.7.2 - 1.12.2)
                        snprintf(add_stat_tooltip_buffer, sizeof(add_stat_tooltip_buffer),
                                 "Add a new blank stat to this template.\n\n"
                                 "Statistics allow tracking of certain actions in form of numerical data.\n"
                                 "Advancely looks for statistics (e.g., 'stat.mineBlock.minecraft.tallgrass') in the stats file.\n"
                                 "Simple achievements (e.g., 'achievement.mineWood') can also act as stats\n"
                                 "(e.g., Logs mined (any log type)).\n\n"
                                 "Click the 'Help' button for more info.");
                    } else {
                        // Modern (1.13+)
                        snprintf(add_stat_tooltip_buffer, sizeof(add_stat_tooltip_buffer),
                                 "Add a new blank stat to this template.\n\n"
                                 "Statistics allow tracking of certain actions in form of numerical data.\n"
                                 "Advancely looks for statistics (e.g., 'minecraft:custom/minecraft:jump') in the stats file.\n"
                                 "The format for Advancely always is 'namespace:category/namespace:stat',\n"
                                 "where the category is outside of the curly braces and the stat is inside.\n\n"
                                 "Click the 'Help' button for more info.");
                    }

                    ImGui::SetTooltip("%s", add_stat_tooltip_buffer);
                }
                ImGui::SameLine();
                ImGui::Checkbox("Show Display Names", &show_stat_display_names);
                if (ImGui::IsItemHovered()) {
                    char show_display_names_tooltip_buffer[1024];
                    snprintf(show_display_names_tooltip_buffer, sizeof(show_display_names_tooltip_buffer),
                             "Toggle between showing user-facing display names and internal root names in this list.");
                    ImGui::SetTooltip("%s", show_display_names_tooltip_buffer);
                }

                ImGui::Separator();

                // 1. Create a list of pointers to render from.
                std::vector<EditorTrackableCategory *> stats_to_render;

                // 2. Populate the list based on the search criteria.
                bool search_active = (tc_search_buffer[0] != '\0' && current_search_scope == SCOPE_STATS);

                if (search_active) {
                    for (auto &stat_cat: current_template_data.stats) {
                        // Skip internal helper stats from appearing in the list.
                        if (strncmp(stat_cat.root_name, "hidden_ms_stat_", 15) == 0) {
                            continue;
                        }
                        bool should_render = false;

                        // Always check parent-level fields first
                        // This covers the display name for both simple and complex stats.
                        if (str_contains_insensitive(stat_cat.display_name, tc_search_buffer) ||
                            str_contains_insensitive(stat_cat.root_name, tc_search_buffer) ||
                            str_contains_insensitive(stat_cat.icon_path, tc_search_buffer)) {
                            should_render = true;
                        }

                        // If parent didn't match, check child-level fields
                        if (!should_render) {
                            for (const auto &criterion: stat_cat.criteria) {
                                char goal_str[32];
                                snprintf(goal_str, sizeof(goal_str), "%d", criterion.goal);

                                // For complex stats, we also check the criterion's own display name.
                                bool name_match = !stat_cat.is_simple_stat && str_contains_insensitive(
                                                      criterion.display_name, tc_search_buffer);

                                // The core check for root name, icon path, and target goal, which applies to both simple and complex stats.
                                if (name_match ||
                                    str_contains_insensitive(criterion.root_name, tc_search_buffer) ||
                                    str_contains_insensitive(criterion.icon_path, tc_search_buffer) ||
                                    (criterion.goal != 0 && strstr(goal_str, tc_search_buffer) != nullptr)) {
                                    should_render = true;
                                    break; // A matching child was found, no need to check others.
                                }
                            }
                        }

                        if (should_render) {
                            stats_to_render.push_back(&stat_cat);
                        }
                    }
                } else {
                    // Search is inactive, so show all non-hidden stats.
                    for (auto &stat_cat: current_template_data.stats) {
                        // Skip internal helper stats from appearing in the list.
                        if (strncmp(stat_cat.root_name, "hidden_ms_stat_", 15) == 0) {
                            continue;
                        }
                        stats_to_render.push_back(&stat_cat);
                    }
                }

                // --- Counter for the list ---

                char counter_text[128];
                snprintf(counter_text, sizeof(counter_text), "%zu %s", stats_to_render.size(),
                         stats_to_render.size() == 1 ? "Stat" : "Stats");
                float text_width = ImGui::CalcTextSize(counter_text).x;
                // Center Count
                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - text_width) *
                    0.5f);
                ImGui::TextDisabled("%s", counter_text);

                // 3. Render the list using pointers.
                int stat_to_remove_idx = -1;
                int stat_to_copy_idx = -1;
                int stat_dnd_source_index = -1;
                int stat_dnd_target_index = -1;

                for (size_t i = 0; i < stats_to_render.size(); i++) {
                    auto &stat = *stats_to_render[i];
                    ImGui::PushID(&stat);

                    const char *display_name = stat.display_name;
                    const char *root_name = stat.root_name;
                    const char *label = show_stat_display_names
                                            ? (display_name[0] ? display_name : root_name)
                                            : root_name;
                    if (label[0] == '\0') {
                        label = "[New Stat]";
                    }

                    if (ImGui::Button("X")) { stat_to_remove_idx = i; }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[128];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Remove %s", label);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Copy")) { stat_to_copy_idx = i; }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[128];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Duplicate %s.", label);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    ImGui::SameLine();

                    if (ImGui::Selectable(label, &stat == selected_stat)) {
                        if (&stat != selected_stat) {
                            if (editor_has_unsaved_changes) {
                                show_unsaved_changes_popup = true;
                                pending_action = [&, new_selection = &stat]() {
                                    selected_stat = new_selection;
                                };
                            } else {
                                selected_stat = &stat;
                            }
                        }
                    }

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        ImGui::SetDragDropPayload("STAT_DND", &i, sizeof(int));
                        ImGui::Text("Reorder %s", label);
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("STAT_DND")) {
                            stat_dnd_source_index = *(const int *) payload->Data;
                            stat_dnd_target_index = i;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::PopID();
                }

                // Handle Drag and Drop
                if (stat_dnd_source_index != -1 && stat_dnd_target_index != -1) {
                    EditorTrackableCategory *source_item_ptr = stats_to_render[stat_dnd_source_index];
                    EditorTrackableCategory *target_item_ptr = stats_to_render[stat_dnd_target_index];

                    auto source_it = std::find_if(current_template_data.stats.begin(),
                                                  current_template_data.stats.end(),
                                                  [&](const EditorTrackableCategory &s) {
                                                      return &s == source_item_ptr;
                                                  });

                    EditorTrackableCategory item_to_move = *source_item_ptr;
                    current_template_data.stats.erase(source_it);

                    auto target_it = std::find_if(current_template_data.stats.begin(),
                                                  current_template_data.stats.end(),
                                                  [&](const EditorTrackableCategory &s) {
                                                      return &s == target_item_ptr;
                                                  });

                    current_template_data.stats.insert(target_it, item_to_move);
                    save_message_type = MSG_NONE;
                }

                // Handle Copying
                if (stat_to_copy_idx != -1) {
                    char selected_root_name_before_op[192] = {};
                    if (selected_stat) {
                        strncpy(selected_root_name_before_op, selected_stat->root_name,
                                sizeof(selected_root_name_before_op) - 1);
                    }
                    const EditorTrackableCategory *source_stat_ptr = stats_to_render[stat_to_copy_idx];

                    // Perform a manual, safe copy to prevent memory corruption from non-null-terminated strings.
                    EditorTrackableCategory new_stat;
                    strncpy(new_stat.root_name, source_stat_ptr->root_name, sizeof(new_stat.root_name));
                    new_stat.root_name[sizeof(new_stat.root_name) - 1] = '\0';
                    strncpy(new_stat.display_name, source_stat_ptr->display_name, sizeof(new_stat.display_name));
                    new_stat.display_name[sizeof(new_stat.display_name) - 1] = '\0';
                    strncpy(new_stat.icon_path, source_stat_ptr->icon_path, sizeof(new_stat.icon_path));
                    new_stat.icon_path[sizeof(new_stat.icon_path) - 1] = '\0';
                    new_stat.is_hidden = source_stat_ptr->is_hidden;
                    new_stat.is_recipe = source_stat_ptr->is_recipe;
                    new_stat.is_simple_stat = source_stat_ptr->is_simple_stat;
                    new_stat.criteria = source_stat_ptr->criteria; // std::vector handles its own deep copy safely.


                    // Now, generate a unique name for the new copy
                    char base_name[192];
                    strncpy(base_name, source_stat_ptr->root_name, sizeof(base_name));
                    base_name[sizeof(base_name) - 1] = '\0'; // Ensure base_name is safe to use in snprintf

                    char new_name[192];
                    int copy_counter = 1;
                    while (true) {
                        if (copy_counter == 1) snprintf(new_name, sizeof(new_name), "%s_copy", base_name);
                        else snprintf(new_name, sizeof(new_name), "%s_copy%d", base_name, copy_counter);
                        bool name_exists = false;
                        for (const auto &s: current_template_data.stats) {
                            if (strcmp(s.root_name, new_name) == 0) {
                                name_exists = true;
                                break;
                            }
                        }
                        if (!name_exists) break;
                        copy_counter++;
                    }

                    // Safely apply the new unique name
                    strncpy(new_stat.root_name, new_name, sizeof(new_stat.root_name));
                    new_stat.root_name[sizeof(new_stat.root_name) - 1] = '\0';

                    auto it = std::find_if(current_template_data.stats.begin(), current_template_data.stats.end(),
                                           [&](const EditorTrackableCategory &s) { return &s == source_stat_ptr; });

                    if (it != current_template_data.stats.end()) {
                        current_template_data.stats.insert(it + 1, new_stat);
                    } else {
                        current_template_data.stats.push_back(new_stat);
                    }

                    // Re-find and update the selected_stat pointer
                    if (selected_root_name_before_op[0] != '\0') {
                        for (auto &stat: current_template_data.stats) {
                            if (strcmp(stat.root_name, selected_root_name_before_op) == 0) {
                                selected_stat = &stat;
                                break;
                            }
                        }
                    }
                    save_message_type = MSG_NONE;
                }

                // Handle Removal
                if (stat_to_remove_idx != -1) {
                    char selected_root_name_before_op[192] = {};
                    if (selected_stat) {
                        strncpy(selected_root_name_before_op, selected_stat->root_name,
                                sizeof(selected_root_name_before_op) - 1);
                    }

                    EditorTrackableCategory *stat_to_remove = stats_to_render[stat_to_remove_idx];
                    if (selected_stat == stat_to_remove) {
                        selected_stat = nullptr;
                    }

                    current_template_data.stats.erase(
                        std::remove_if(current_template_data.stats.begin(), current_template_data.stats.end(),
                                       [&](const EditorTrackableCategory &s) { return &s == stat_to_remove; }),
                        current_template_data.stats.end()
                    );

                    // Re-find pointer if it wasn't the one being deleted
                    if (selected_stat && selected_root_name_before_op[0] != '\0') {
                        for (auto &stat: current_template_data.stats) {
                            if (strcmp(stat.root_name, selected_root_name_before_op) == 0) {
                                selected_stat = &stat;
                                break;
                            }
                        }
                    }
                    save_message_type = MSG_NONE;
                }
                ImGui::EndChild();
                ImGui::SameLine();

                ImGui::BeginChild("StatDetailsPane", ImVec2(0, 0), true);
                if (selected_stat != nullptr) {
                    auto &stat_cat = *selected_stat;

                    ImGui::Text("Edit Stat Details");
                    ImGui::Separator();

                    if (ImGui::InputText("Category Key", stat_cat.root_name, sizeof(stat_cat.root_name))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char root_name_tooltip_buffer[128];
                        snprintf(root_name_tooltip_buffer, sizeof(root_name_tooltip_buffer),
                                 "The unique key for this stat or stat category, e.g., 'stat:my_awesome_stat'.");
                        ImGui::SetTooltip("%s", root_name_tooltip_buffer);
                    }
                    if (ImGui::InputText("Display Name", stat_cat.display_name, sizeof(stat_cat.display_name))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char display_name_tooltip_buffer[128];
                        snprintf(display_name_tooltip_buffer, sizeof(display_name_tooltip_buffer),
                                 "The user-facing name for this single stat or stat category.");
                        ImGui::SetTooltip("%s", display_name_tooltip_buffer);
                    }
                    if (ImGui::InputText("Icon Path", stat_cat.icon_path, sizeof(stat_cat.icon_path))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char icon_path_tooltip_buffer[1024];
                        snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                 "Path to the icon file, relative to the 'resources/icons' directory.");
                        ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##StatIcon")) {
                        char new_path[MAX_PATH_LENGTH];
                        if (open_icon_file_dialog(new_path, sizeof(new_path))) {
                            strncpy(stat_cat.icon_path, new_path, sizeof(stat_cat.icon_path) - 1);
                            stat_cat.icon_path[sizeof(stat_cat.icon_path) - 1] = '\0';
                            save_message_type = MSG_NONE;
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        char icon_path_tooltip_buffer[1024];
                        snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                 "The icon must be inside the 'resources/icons' folder!");
                        ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                    }
                    if (ImGui::Checkbox("Hidden", &stat_cat.is_hidden)) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char hidden_tooltip_buffer[256];
                        snprintf(hidden_tooltip_buffer, sizeof(hidden_tooltip_buffer),
                                 "If checked, this stat (and all sub-stats) will be fully hidden on the overlay\n"
                                 "and hidden settings-based on the tracker.\n"
                                 "Visibility can be toggled in the main tracker settings");
                        ImGui::SetTooltip("%s", hidden_tooltip_buffer);
                    }

                    ImGui::SameLine();
                    if (ImGui::Checkbox("Row 2", &stat_cat.in_2nd_row)) {
                        save_message_type = MSG_NONE;
                    }
                    ImGui::SameLine();
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[256];
                        if (creator_selected_version != MC_VERSION_25W14CRAFTMINE) {
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Force this stat category to display on the 2nd row of the overlay\n"
                                     "(normally reserved for %s).", advancements_label_plural_lower);
                        } else {
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Force this stat category to display on the 2nd row of the overlay\n"
                                     "(normally reserved for %s/unlocks).", advancements_label_plural_lower);
                        }
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }

                    ImGui::SameLine();

                    // Invert the logic for the checkbox to be more intuitive for the user
                    bool is_multi_stat = !stat_cat.is_simple_stat;
                    if (ImGui::Checkbox("Multi-Stat Category", &is_multi_stat)) {
                        bool was_simple_stat = stat_cat.is_simple_stat;
                        stat_cat.is_simple_stat = !is_multi_stat;

                        // Ensure at least one criterion exists to avoid errors
                        if (stat_cat.criteria.empty()) {
                            stat_cat.criteria.push_back({});
                        }

                        // If switching FROM Simple TO Multi-Stat
                        if (was_simple_stat && !stat_cat.is_simple_stat) {
                            // Copy the parent's display name to the first criterion.
                            strncpy(stat_cat.criteria[0].display_name, stat_cat.display_name,
                                    sizeof(stat_cat.criteria[0].display_name) - 1);
                            stat_cat.criteria[0].display_name[sizeof(stat_cat.criteria[0].display_name) - 1] = '\0';

                            // Set a default placeholder icon for the newly visible sub-stat.
                            strncpy(stat_cat.criteria[0].icon_path, "blocks/placeholder.png",
                                    sizeof(stat_cat.criteria[0].icon_path) - 1);
                            stat_cat.criteria[0].icon_path[sizeof(stat_cat.criteria[0].icon_path) - 1] = '\0';
                        }
                        // If switching FROM Multi-Stat TO Simple Stat
                        else if (!was_simple_stat && stat_cat.is_simple_stat) {
                            // Copy the first criterion's display name up to the parent.
                            strncpy(stat_cat.display_name, stat_cat.criteria[0].display_name,
                                    sizeof(stat_cat.display_name) - 1);
                            stat_cat.display_name[sizeof(stat_cat.display_name) - 1] = '\0';

                            // If there were multiple criteria, keep only the first one.
                            if (stat_cat.criteria.size() > 1) {
                                EditorTrackableItem first_crit = stat_cat.criteria[0];
                                stat_cat.criteria.clear();
                                stat_cat.criteria.push_back(first_crit);
                            }
                        }
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char multi_stat_tooltip_buffer[256];
                        snprintf(multi_stat_tooltip_buffer, sizeof(multi_stat_tooltip_buffer),
                                 "Toggle between a simple, single stat and a complex category\n"
                                 "containing multiple sub-stats that individually act as a single stat,\n"
                                 "but have their own icons similar to %s criteria.", advancements_label_singular_lower);
                        ImGui::SetTooltip("%s", multi_stat_tooltip_buffer);
                    }
                    ImGui::Separator();

                    if (stat_cat.is_simple_stat) {
                        // UI for a simple stat
                        if (stat_cat.criteria.empty()) stat_cat.criteria.push_back({}); // Ensure one exists

                        auto &simple_crit = stat_cat.criteria[0];
                        if (ImGui::InputText("Stat Root Name", simple_crit.root_name, sizeof(simple_crit.root_name))) {
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char stat_root_name_tooltip_buffer[256];
                            if (creator_selected_version <= MC_VERSION_1_6_4) {
                                // Legacy
                                snprintf(stat_root_name_tooltip_buffer, sizeof(stat_root_name_tooltip_buffer),
                                         "The unique in-game ID for the stat to track, e.g., '16842813' (Furnace Crafted).");
                            } else if (creator_selected_version <= MC_VERSION_1_12_2) {
                                // Mid-era (1.7.2 - 1.12.2)
                                snprintf(stat_root_name_tooltip_buffer, sizeof(stat_root_name_tooltip_buffer),
                                         "The unique in-game ID for the stat to track, e.g., 'stat.sprintOneCm'.");
                            } else {
                                // Modern (1.13+)
                                snprintf(stat_root_name_tooltip_buffer, sizeof(stat_root_name_tooltip_buffer),
                                         "The unique in-game ID for the stat to track, e.g., 'minecraft:mined/minecraft:diamond_ore'.");
                            }
                            ImGui::SetTooltip("%s", stat_root_name_tooltip_buffer);
                        }
                        if (ImGui::InputInt("Target Value", &simple_crit.goal)) {
                            // Clamping to -1
                            if (simple_crit.goal < -1) {
                                simple_crit.goal = -1;
                            }
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char target_tooltip_buffer[512];
                            snprintf(target_tooltip_buffer, sizeof(target_tooltip_buffer),
                                     "Set the stat's behavior:\n"
                                     "-1 = Infinite counter (manual completion via checkbox).\n"
                                     ">0 = Progress-based counter (completes when value reached).\n"
                                     "0 = NOT ALLOWED (Use a Custom Goal toggle instead).");
                            ImGui::SetTooltip("%s", target_tooltip_buffer);
                        }
                    } else {
                        // UI for a complex, multi-stat category (similar to advancements)
                        ImGui::Text("Sub-Stats");

                        // --- Counter for the sub-stat list ---
                        char crit_counter_text[128];
                        bool is_details_search_active = (
                            current_search_scope == SCOPE_STAT_DETAILS && tc_search_buffer[0] != '\0');
                        int visible_criteria_count = 0;
                        if (!is_details_search_active) {
                            visible_criteria_count = stat_cat.criteria.size();
                        } else {
                            for (const auto &crit: stat_cat.criteria) {
                                char goal_str[32];
                                snprintf(goal_str, sizeof(goal_str), "%d", crit.goal);
                                if (str_contains_insensitive(crit.display_name, tc_search_buffer) ||
                                    str_contains_insensitive(crit.root_name, tc_search_buffer) ||
                                    str_contains_insensitive(crit.icon_path, tc_search_buffer) ||
                                    (crit.goal != 0 && strstr(goal_str, tc_search_buffer) != nullptr)) {
                                    visible_criteria_count++;
                                }
                            }
                        }
                        snprintf(crit_counter_text, sizeof(crit_counter_text), "%d %s", visible_criteria_count,
                                 visible_criteria_count == 1 ? "Sub-Stat" : "Sub-Stats");
                        float text_width = ImGui::CalcTextSize(crit_counter_text).x;
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - text_width);
                        ImGui::TextDisabled("%s", crit_counter_text);

                        // "Import Sub-Stats" button
                        if (ImGui::Button("Import Sub-Stats")) {
                            current_stat_import_mode = IMPORT_AS_SUB_STAT; // Set the mode
                            char start_path[MAX_PATH_LENGTH];
                            if (creator_selected_version <= MC_VERSION_1_6_4) {
                                if (app_settings->using_stats_per_world_legacy) {
                                    snprintf(start_path, sizeof(start_path), "%s/%s/stats/", t->saves_path,
                                             t->world_name);
                                } else {
                                    char parent_dir[MAX_PATH_LENGTH];
                                    if (get_parent_directory(t->saves_path, parent_dir, sizeof(parent_dir), 1)) {
                                        snprintf(start_path, sizeof(start_path), "%s/stats/", parent_dir);
                                    } else {
                                        strncpy(start_path, t->saves_path, sizeof(start_path));
                                    }
                                }
                            } else {
                                snprintf(start_path, sizeof(start_path), "%s/%s/stats/", t->saves_path, t->world_name);
                            }
#ifdef __APPLE__
                            const char *json_filter[2] = {"*.json", "public.json"};
                            const char *dat_filter[2] = {"*.dat", "public.data"};
                            int filter_count = 2;
#else
                            const char *json_filter[1] = {"*.json"};
                            const char *dat_filter[1] = {"*.dat"};
                            int filter_count = 1;
#endif
                            const char **selected_filter = (creator_selected_version <= MC_VERSION_1_6_4)
                                                               ? dat_filter
                                                               : json_filter;
                            const char *filter_desc = (creator_selected_version <= MC_VERSION_1_6_4)
                                                          ? "DAT files"
                                                          : "JSON files";
                            const char *selection = tinyfd_openFileDialog(
                                "Select Player Stats File", start_path, filter_count, selected_filter, filter_desc, 0);
                            if (selection) {
                                import_error_message[0] = '\0';
                                if (parse_player_stats_for_import(selection, creator_selected_version, importable_stats,
                                                                  import_error_message, sizeof(import_error_message))) {
                                    show_import_stats_popup = true;
                                    last_clicked_stat_index = -1;
                                } else {
                                    save_message_type = MSG_ERROR;
                                    strncpy(status_message, import_error_message, sizeof(status_message) - 1);
                                }
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            char tooltip[512];

                            if (creator_selected_version <= MC_VERSION_1_6_4) {
                                if (app_settings->using_stats_per_world_legacy) {
                                    // Legacy using stats per world
                                    snprintf(tooltip, sizeof(tooltip),
                                             "Import sub-stats directly from a local world's player stats/achievements .dat file.\n"
                                             "Cannot import already existing root names within this stat category.");
                                } else {
                                    // Legacy not using stats per world
                                    snprintf(tooltip, sizeof(tooltip),
                                             "Import sub-stats directly from a global world's player stats/achievements .dat file.\n"
                                             "Cannot import already existing root names within this stat category.");
                                }
                            } else if (creator_selected_version <= MC_VERSION_1_12_2) {
                                // Mid-era (1.7.2 - 1.12.2)
                                snprintf(tooltip, sizeof(tooltip),
                                         "Import sub-stats directly from a world's player stats/achievements .json file.\n"
                                         "Cannot import already existing root names within this stat category.");
                            } else {
                                // Modern(1.13+)
                                snprintf(tooltip, sizeof(tooltip),
                                         "Import sub-stats directly from a world's player stats .json file.\n"
                                         "Cannot import already existing root names within this stat category.");
                            }
                            ImGui::SetTooltip("%s", tooltip);
                        }
                        ImGui::SameLine();

                        // Add Criterion button only for multi-stat categories -> complex stats
                        if (ImGui::Button("Add New Sub-Stat")) {
                            // Create new stat criterion with default values
                            EditorTrackableItem new_crit = {};
                            int counter = 1;
                            while (true) {
                                snprintf(new_crit.root_name, sizeof(new_crit.root_name), "new_criterion_%d", counter);
                                bool name_exists = false;
                                for (const auto &crit: stat_cat.criteria) {
                                    if (strcmp(crit.root_name, new_crit.root_name) == 0) {
                                        name_exists = true;
                                        break;
                                    }
                                }
                                if (!name_exists) break;
                                counter++;
                            }
                            snprintf(new_crit.display_name, sizeof(new_crit.display_name), "New Criterion %d", counter);
                            strncpy(new_crit.icon_path, "blocks/placeholder.png", sizeof(new_crit.icon_path) - 1);
                            new_crit.icon_path[sizeof(new_crit.icon_path) - 1] = '\0';
                            new_crit.goal = 1; // Default to a completable goal
                            stat_cat.criteria.push_back(new_crit);
                            save_message_type = MSG_NONE;
                        }

                        // Tooltip for Add Criterion button
                        if (ImGui::IsItemHovered()) {
                            char add_stat_tooltip_buffer[1024];
                            snprintf(add_stat_tooltip_buffer, sizeof(add_stat_tooltip_buffer),
                                     "Add a new blank sub-stat to this template.\n\n"
                                     "Sub-Stats are functionally identical to stats,\n"
                                     "but have their own icons that then displays in\n"
                                     "the topmost row of the overlay.\n\n"
                                     "Click the 'Help' button for more info.");
                            ImGui::SetTooltip("%s", add_stat_tooltip_buffer);
                        }

                        int crit_to_remove = -1;
                        int crit_to_copy = -1;

                        int stat_crit_dnd_source_index = -1;
                        int stat_crit_dnd_target_index = -1;
                        for (size_t j = 0; j < stat_cat.criteria.size(); j++) {
                            auto &crit = stat_cat.criteria[j];

                            if (is_details_search_active) {
                                char goal_str[32];
                                snprintf(goal_str, sizeof(goal_str), "%d", crit.goal);
                                if (!str_contains_insensitive(crit.display_name, tc_search_buffer) &&
                                    !str_contains_insensitive(crit.root_name, tc_search_buffer) &&
                                    !str_contains_insensitive(crit.icon_path, tc_search_buffer) &&
                                    (crit.goal == 0 || strstr(goal_str, tc_search_buffer) == nullptr)) {
                                    continue;
                                }
                            }

                            ImGui::PushID(j);

                            ImGui::Spacing();
                            ImGui::InvisibleButton("drop_target", ImVec2(-1, 8.0f));
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("STAT_CRITERION_DND")) {
                                    stat_crit_dnd_source_index = *(const int *) payload->Data;
                                    stat_crit_dnd_target_index = j;
                                }
                                ImGui::EndDragDropTarget();
                            }

                            ImVec2 item_start_cursor_pos = ImGui::GetCursorScreenPos();
                            ImGui::BeginGroup();

                            ImGui::Separator();
                            if (ImGui::InputText("Sub-Stat Root Name", crit.root_name, sizeof(crit.root_name))) {
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::IsItemHovered()) {
                                char stat_root_name_tooltip_buffer[256];
                                if (creator_selected_version <= MC_VERSION_1_6_4) {
                                    // Legacy
                                    snprintf(stat_root_name_tooltip_buffer, sizeof(stat_root_name_tooltip_buffer),
                                             "The unique in-game ID for the stat to track,\n"
                                             "e.g., '1100' (Playtime in ticks), '16974109' (Gold Pickaxe Broken).");
                                } else if (creator_selected_version <= MC_VERSION_1_12_2) {
                                    // Mid-era (1.7.2 - 1.12.2)
                                    snprintf(stat_root_name_tooltip_buffer, sizeof(stat_root_name_tooltip_buffer),
                                             "The unique in-game ID for the stat to track, e.g., 'stat.sprintOneCm'.");
                                } else {
                                    // Modern(1.13+)
                                    snprintf(stat_root_name_tooltip_buffer, sizeof(stat_root_name_tooltip_buffer),
                                             "The unique in-game ID for the stat to track, e.g., 'minecraft:picked_up/minecraft:deepslate_emerald_ore'.");
                                }
                                ImGui::SetTooltip("%s", stat_root_name_tooltip_buffer);
                            }
                            if (ImGui::InputText("Display Name", crit.display_name, sizeof(crit.display_name))) {
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::IsItemHovered()) {
                                char display_name_tooltip_buffer[128];
                                snprintf(display_name_tooltip_buffer, sizeof(display_name_tooltip_buffer),
                                         "The user-facing name for this sub-stat.");
                                ImGui::SetTooltip("%s", display_name_tooltip_buffer);
                            }
                            if (ImGui::InputText("Icon Path", crit.icon_path, sizeof(crit.icon_path))) {
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::IsItemHovered()) {
                                char icon_path_tooltip_buffer[1024];
                                snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                         "Path to the icon file, relative to the 'resources/icons' directory.");
                                ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Browse##StatCritIcon")) {
                                char new_path[MAX_PATH_LENGTH];
                                if (open_icon_file_dialog(new_path, sizeof(new_path))) {
                                    strncpy(crit.icon_path, new_path, sizeof(crit.icon_path) - 1);
                                    crit.icon_path[sizeof(crit.icon_path) - 1] = '\0';
                                    save_message_type = MSG_NONE;
                                }
                            }
                            if (ImGui::IsItemHovered()) {
                                char icon_path_tooltip_buffer[1024];
                                snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                         "The icon must be inside the 'resources/icons' folder!");
                                ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                            }
                            if (ImGui::InputInt("Target Value", &crit.goal)) {
                                // Clamping to -1
                                if (crit.goal < -1) {
                                    crit.goal = -1;
                                }
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::IsItemHovered()) {
                                char target_tooltip_buffer[512];
                                snprintf(target_tooltip_buffer, sizeof(target_tooltip_buffer),
                                         "Set the sub-stat's behavior:\n"
                                         "-1 = Infinite counter (manual completion via checkbox).\n"
                                         ">0 = Progress-based counter (completes when value reached).\n"
                                         "0 = NOT ALLOWED (Use a Custom Goal toggle instead).");
                                ImGui::SetTooltip("%s", target_tooltip_buffer);
                            }
                            if (ImGui::Checkbox("Hidden", &crit.is_hidden)) {
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::IsItemHovered()) {
                                char hidden_tooltip_buffer[256];
                                snprintf(hidden_tooltip_buffer, sizeof(hidden_tooltip_buffer),
                                         "If checked, this sub-stat will be fully hidden on the overlay\n"
                                         "and hidden settings-based on the tracker.\n"
                                         "Visibility can be toggled in the main tracker settings.\n\n"
                                         "NOTE: Hidden sub-stats are also excluded from the cycle rotation\n"
                                         "on the overlay.");
                                ImGui::SetTooltip("%s", hidden_tooltip_buffer);
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("Copy")) {
                                crit_to_copy = j;
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buffer[128];
                                snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Duplicate Sub-Stat:\n%s",
                                         crit.root_name);
                                ImGui::SetTooltip("%s", tooltip_buffer);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Remove")) {
                                crit_to_remove = j;
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buffer[128];
                                snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Remove Sub-Stat:\n%s",
                                         crit.root_name);
                                ImGui::SetTooltip("%s", tooltip_buffer);
                            }
                            ImGui::EndGroup();
                            ImGui::SetCursorScreenPos(item_start_cursor_pos);
                            ImGui::InvisibleButton("dnd_handle", ImGui::GetItemRectSize());

                            if (ImGui::BeginDragDropSource()) {
                                ImGui::SetDragDropPayload("STAT_CRITERION_DND", &j, sizeof(int));
                                ImGui::Text("Reorder %s", crit.root_name);
                                ImGui::EndDragDropSource();
                            }

                            ImGui::PopID();
                        }

                        ImGui::InvisibleButton("final_drop_target_stat_crit", ImVec2(-1, 8.0f));
                        // Added larger drop zone
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("STAT_CRITERION_DND")) {
                                stat_crit_dnd_source_index = *(const int *) payload->Data;
                                stat_crit_dnd_target_index = stat_cat.criteria.size();
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (stat_crit_dnd_source_index != -1 && stat_crit_dnd_target_index != -1 &&
                            stat_crit_dnd_source_index != stat_crit_dnd_target_index) {
                            EditorTrackableItem item_to_move = stat_cat.criteria[stat_crit_dnd_source_index];
                            stat_cat.criteria.erase(stat_cat.criteria.begin() + stat_crit_dnd_source_index);
                            if (stat_crit_dnd_target_index > stat_crit_dnd_source_index) stat_crit_dnd_target_index--;
                            stat_cat.criteria.insert(stat_cat.criteria.begin() + stat_crit_dnd_target_index,
                                                     item_to_move);
                            save_message_type = MSG_NONE;
                        }

                        if (crit_to_remove != -1) {
                            stat_cat.criteria.erase(stat_cat.criteria.begin() + crit_to_remove);
                            save_message_type = MSG_NONE;
                        }

                        // Logic to handle the copy action after the loop
                        if (crit_to_copy != -1) {
                            const auto &source_criterion = stat_cat.criteria[crit_to_copy];

                            // Perform a manual, safe copy.
                            EditorTrackableItem new_crit;
                            strncpy(new_crit.root_name, source_criterion.root_name, sizeof(new_crit.root_name));
                            new_crit.root_name[sizeof(new_crit.root_name) - 1] = '\0';
                            strncpy(new_crit.display_name, source_criterion.display_name,
                                    sizeof(new_crit.display_name));
                            new_crit.display_name[sizeof(new_crit.display_name) - 1] = '\0';
                            strncpy(new_crit.icon_path, source_criterion.icon_path, sizeof(new_crit.icon_path));
                            new_crit.icon_path[sizeof(new_crit.icon_path) - 1] = '\0';
                            new_crit.goal = source_criterion.goal;
                            new_crit.is_hidden = source_criterion.is_hidden;

                            char base_name[192];
                            strncpy(base_name, source_criterion.root_name, sizeof(base_name) - 1);
                            base_name[sizeof(base_name) - 1] = '\0';
                            char new_name[192];
                            int copy_counter = 1;
                            while (true) {
                                if (copy_counter == 1) snprintf(new_name, sizeof(new_name), "%s_copy", base_name);
                                else snprintf(new_name, sizeof(new_name), "%s_copy%d", base_name, copy_counter);
                                bool name_exists = false;
                                for (const auto &c: stat_cat.criteria) {
                                    if (strcmp(c.root_name, new_name) == 0) {
                                        name_exists = true;
                                        break;
                                    }
                                }
                                if (!name_exists) break;
                                copy_counter++;
                            }
                            strncpy(new_crit.root_name, new_name, sizeof(new_crit.root_name) - 1);
                            new_crit.root_name[sizeof(new_crit.root_name) - 1] = '\0';
                            stat_cat.criteria.insert(stat_cat.criteria.begin() + crit_to_copy + 1, new_crit);
                            save_message_type = MSG_NONE;
                        }
                    }
                } else {
                    ImGui::Text("Select a Stat from the list to edit its details.");
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }


            // Only show the Unlocks tab for the specific version
            if (strcmp(creator_version_str, "25w14craftmine") == 0) {
                if (ImGui::BeginTabItem("Unlocks")) {
                    // Import unlocks button
                    if (ImGui::Button("Import Unlocks")) {
                        char start_path[MAX_PATH_LENGTH];
                        snprintf(start_path, sizeof(start_path), "%s/%s/unlocks/", t->saves_path, t->world_name);

#ifdef __APPLE__
                        const char *filter_patterns[2] = {"*.json", "public.json"};
                        int filter_count = 2;
#else
                        const char *filter_patterns[1] = {"*.json"};
                        int filter_count = 1;
#endif
                        const char *selection = tinyfd_openFileDialog("Select Player Unlocks File", start_path, filter_count,
                                                                      filter_patterns, "JSON files", 0);

                        if (selection) {
                            import_error_message[0] = '\0';
                            if (parse_player_unlocks_for_import(selection, importable_unlocks, import_error_message,
                                                                sizeof(import_error_message))) {
                                show_import_unlocks_popup = true;
                                last_clicked_unlock_index = -1; // Reset range selection
                                focus_import_search = true;
                            } else {
                                save_message_type = MSG_ERROR;
                                strncpy(status_message, import_error_message, sizeof(status_message) - 1);
                                status_message[sizeof(status_message) - 1] = '\0';
                            }
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[256];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Import unlocks directly from a world's player unlocks .json file.\n"
                                 "Cannot import already existing root names.");
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }

                    // Add new unlock button
                    ImGui::SameLine();
                    if (ImGui::Button("Add New Unlock")) {
                        // Create new unlock with default values
                        EditorTrackableItem new_unlock = {};
                        int counter = 1;
                        while (true) {
                            snprintf(new_unlock.root_name, sizeof(new_unlock.root_name), "minecraft:new_unlock_%d",
                                     counter);
                            bool name_exists = false;
                            for (const auto &unlock: current_template_data.unlocks) {
                                if (strcmp(unlock.root_name, new_unlock.root_name) == 0) {
                                    name_exists = true;
                                    break;
                                }
                            }
                            if (!name_exists) break;
                            counter++;
                        }
                        snprintf(new_unlock.display_name, sizeof(new_unlock.display_name), "New Unlock %d", counter);
                        strncpy(new_unlock.icon_path, "blocks/placeholder.png", sizeof(new_unlock.icon_path) - 1);
                        new_unlock.icon_path[sizeof(new_unlock.icon_path) - 1] = '\0';
                        current_template_data.unlocks.push_back(new_unlock);
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char add_unlock_tooltip_buffer[1024];
                        snprintf(add_unlock_tooltip_buffer, sizeof(add_unlock_tooltip_buffer),
                                 "Add a new blank unlock to this template.\n"
                                 "Player Unlocks are abilities to unlock using XP levels.\n"
                                 "Advancely looks for completed unlocks (e.g., 'minecraft:exploration')\n"
                                 "within the \"obtained\" object of the unlocks file.\n\n"
                                 "Click the 'Help' button for more info.");
                        ImGui::SetTooltip("%s", add_unlock_tooltip_buffer);
                    }

                    // Determine if search is active for this scope
                    bool is_unlock_search_active = (
                        current_search_scope == SCOPE_UNLOCKS && tc_search_buffer[0] != '\0');

                    // --- Counter for the list ---
                    char counter_text[128];
                    size_t count = 0;
                    if (!is_unlock_search_active) {
                        count = current_template_data.unlocks.size();
                    } else {
                        for (const auto &unlock: current_template_data.unlocks) {
                            if (str_contains_insensitive(unlock.display_name, tc_search_buffer) ||
                                str_contains_insensitive(unlock.root_name, tc_search_buffer) ||
                                str_contains_insensitive(unlock.icon_path, tc_search_buffer)) {
                                count++;
                            }
                        }
                    }
                    snprintf(counter_text, sizeof(counter_text), "%zu %s", count, count == 1 ? "Unlock" : "Unlocks");
                    float text_width = ImGui::CalcTextSize(counter_text).x;
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - text_width);
                    ImGui::TextDisabled("%s", counter_text);

                    int item_to_remove = -1;
                    int item_to_copy = -1;
                    int unlocks_dnd_source_index = -1;
                    int unlocks_dnd_target_index = -1;

                    for (size_t i = 0; i < current_template_data.unlocks.size(); i++) {
                        auto &unlock = current_template_data.unlocks[i];

                        // SEARCH FILTER
                        if (is_unlock_search_active) {
                            if (!str_contains_insensitive(unlock.display_name, tc_search_buffer) &&
                                !str_contains_insensitive(unlock.root_name, tc_search_buffer) &&
                                !str_contains_insensitive(unlock.icon_path, tc_search_buffer)) {
                                continue;
                            }
                        }

                        ImGui::PushID(i);

                        // Add some vertical spacing to create a gap
                        ImGui::Spacing();

                        // Create a wide, 8-pixel-high invisible button to act as our drop zone
                        ImGui::InvisibleButton("drop_target", ImVec2(-1, 8.0f));

                        // Drop target for dropping between items
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("UNLOCK_DND")) {
                                unlocks_dnd_source_index = *(const int *) payload->Data;
                                unlocks_dnd_target_index = i;
                            }
                            ImGui::EndDragDropTarget();
                        }

                        // Draw a separator for visual feedback after the drop zone
                        ImGui::Separator();

                        // The InvisibleButton is now only a drag SOURCE
                        ImVec2 item_start_cursor_pos = ImGui::GetCursorScreenPos();

                        // Make the whole item group a drag-drop source and target
                        ImGui::BeginGroup();


                        if (ImGui::InputText("Root Name", unlock.root_name, sizeof(unlock.root_name))) {
                            save_message_type = MSG_NONE; // Clear message on new edit
                        }
                        if (ImGui::IsItemHovered()) {
                            char root_name_tooltip_buffer[256];
                            snprintf(root_name_tooltip_buffer, sizeof(root_name_tooltip_buffer),
                                     "The unique in-game ID for this unlock, e.g., 'minecraft:exploration'.");
                            ImGui::SetTooltip("%s", root_name_tooltip_buffer);
                        }
                        if (ImGui::InputText("Display Name", unlock.display_name, sizeof(unlock.display_name))) {
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char display_name_tooltip_buffer[128];
                            snprintf(display_name_tooltip_buffer, sizeof(display_name_tooltip_buffer),
                                     "The user-facing name for this unlock.");
                            ImGui::SetTooltip("%s", display_name_tooltip_buffer);
                        }
                        if (ImGui::InputText("Icon Path", unlock.icon_path, sizeof(unlock.icon_path))) {
                            save_message_type = MSG_NONE; // Clear message on new edit
                        }
                        if (ImGui::IsItemHovered()) {
                            char icon_path_tooltip_buffer[1024];
                            snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                     "Path to the icon file, relative to the 'resources/icons' directory.");
                            ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Browse##UnlockIcon")) {
                            char new_path[MAX_PATH_LENGTH];
                            if (open_icon_file_dialog(new_path, sizeof(new_path))) {
                                strncpy(unlock.icon_path, new_path, sizeof(unlock.icon_path));
                                unlock.icon_path[sizeof(unlock.icon_path) - 1] = '\0';
                                save_message_type = MSG_NONE;
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            char icon_path_tooltip_buffer[1024];
                            snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                     "The icon must be inside the 'resources/icons' folder!");
                            ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                        }
                        if (ImGui::Checkbox("Hidden", &unlock.is_hidden)) {
                            save_message_type = MSG_NONE; // Clear message on new edit
                        }
                        if (ImGui::IsItemHovered()) {
                            char hidden_tooltip_buffer[256];
                            snprintf(hidden_tooltip_buffer, sizeof(hidden_tooltip_buffer),
                                     "If checked, this unlock will be fully hidden on the overlay\n"
                                     "and hidden settings-based on the tracker.\n"
                                     "Visibility can be toggled in the main tracker settings");
                            ImGui::SetTooltip("%s", hidden_tooltip_buffer);
                        }

                        ImGui::SameLine();

                        if (ImGui::Button("Copy")) {
                            item_to_copy = i;
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[128];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Duplicate Unlock:\n%s", unlock.root_name);
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Remove")) {
                            item_to_remove = i;
                            save_message_type = MSG_NONE; // Clear message on new edit
                        }
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[128];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Remove Unlock:\n%s", unlock.root_name);
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }

                        ImGui::EndGroup();

                        ImGui::SetCursorScreenPos(item_start_cursor_pos);
                        ImGui::InvisibleButton("dnd_handle", ImGui::GetItemRectSize());

                        if (ImGui::BeginDragDropSource()) {
                            ImGui::SetDragDropPayload("UNLOCK_DND", &i, sizeof(int));
                            ImGui::Text("Reorder %s", unlock.root_name);
                            ImGui::EndDragDropSource();
                        }

                        ImGui::PopID();
                    }

                    // Final drop target to allow dropping at the end of the list
                    ImGui::InvisibleButton("final_drop_target_unlocks", ImVec2(-1, 8.0f)); // Added larger drop zone
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("UNLOCK_DND")) {
                            unlocks_dnd_source_index = *(const int *) payload->Data;
                            unlocks_dnd_target_index = current_template_data.unlocks.size();
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (unlocks_dnd_source_index != -1 && unlocks_dnd_target_index != -1 && unlocks_dnd_source_index !=
                        unlocks_dnd_target_index) {
                        EditorTrackableItem item_to_move = current_template_data.unlocks[unlocks_dnd_source_index];
                        current_template_data.unlocks.erase(
                            current_template_data.unlocks.begin() + unlocks_dnd_source_index);
                        if (unlocks_dnd_target_index > unlocks_dnd_source_index) unlocks_dnd_target_index--;
                        current_template_data.unlocks.insert(
                            current_template_data.unlocks.begin() + unlocks_dnd_target_index, item_to_move);
                        save_message_type = MSG_NONE;
                    }

                    if (item_to_remove != -1) {
                        current_template_data.unlocks.erase(current_template_data.unlocks.begin() + item_to_remove);
                        save_message_type = MSG_NONE;
                    }

                    // Logic to handle the copy action after the loop
                    if (item_to_copy != -1) {
                        const auto &source_item = current_template_data.unlocks[item_to_copy];

                        // Perform a manual, safe copy.
                        EditorTrackableItem new_item;
                        strncpy(new_item.root_name, source_item.root_name, sizeof(new_item.root_name));
                        new_item.root_name[sizeof(new_item.root_name) - 1] = '\0';
                        strncpy(new_item.display_name, source_item.display_name, sizeof(new_item.display_name));
                        new_item.display_name[sizeof(new_item.display_name) - 1] = '\0';
                        strncpy(new_item.icon_path, source_item.icon_path, sizeof(new_item.icon_path));
                        new_item.icon_path[sizeof(new_item.icon_path) - 1] = '\0';
                        new_item.goal = source_item.goal;
                        new_item.is_hidden = source_item.is_hidden;

                        char base_name[192];
                        strncpy(base_name, source_item.root_name, sizeof(base_name) - 1);
                        base_name[sizeof(base_name) - 1] = '\0';
                        char new_name[192];
                        int copy_counter = 1;
                        while (true) {
                            if (copy_counter == 1) snprintf(new_name, sizeof(new_name), "%s_copy", base_name);
                            else snprintf(new_name, sizeof(new_name), "%s_copy%d", base_name, copy_counter);
                            bool name_exists = false;
                            for (const auto &item: current_template_data.unlocks) {
                                if (strcmp(item.root_name, new_name) == 0) {
                                    name_exists = true;
                                    break;
                                }
                            }
                            if (!name_exists) break;
                            copy_counter++;
                        }
                        strncpy(new_item.root_name, new_name, sizeof(new_item.root_name) - 1);
                        new_item.root_name[sizeof(new_item.root_name) - 1] = '\0';
                        current_template_data.unlocks.insert(current_template_data.unlocks.begin() + item_to_copy + 1,
                                                             new_item);
                        save_message_type = MSG_NONE;
                    }
                    ImGui::EndTabItem();
                }
            }
            if (ImGui::BeginTabItem
                ("Custom Goals")) {
                if (ImGui::Button("Add New Custom Goal")) {
                    // Create a new custom goal with default values
                    EditorTrackableItem new_goal = {};
                    int counter = 1;
                    while (true) {
                        snprintf(new_goal.root_name, sizeof(new_goal.root_name), "new_custom_goal_%d", counter);
                        bool name_exists = false;
                        for (const auto &goal: current_template_data.custom_goals) {
                            if (strcmp(goal.root_name, new_goal.root_name) == 0) {
                                name_exists = true;
                                break;
                            }
                        }
                        if (!name_exists) break;
                        counter++;
                    }
                    snprintf(new_goal.display_name, sizeof(new_goal.display_name), "New Custom Goal %d", counter);
                    strncpy(new_goal.icon_path, "blocks/placeholder.png", sizeof(new_goal.icon_path) - 1);
                    new_goal.icon_path[sizeof(new_goal.icon_path) - 1] = '\0';
                    new_goal.goal = 1; // Default to a progress-based counter
                    current_template_data.custom_goals.push_back(new_goal);
                    save_message_type = MSG_NONE;
                }
                if (ImGui::IsItemHovered()) {
                    char add_custom_goal_tooltip_buffer[1024];
                    snprintf(add_custom_goal_tooltip_buffer, sizeof(add_custom_goal_tooltip_buffer),
                             "Add a new blank custom goal to this template.\n"
                             "Custom Goals are useful for tracking objectives manually\n"
                             "that cannot be automatically detected by reading the game's world files.\n"
                             "E.g., the amount of times a structure has been visited.\n"
                             "Depending on the target value custom goals can have hotkeys.\n"
                             "These can then be configured in the settings window after selecting the template.\n"
                             "You need to be tabbed into the main tracker window for hotkeys to work.\n\n"
                             "Click the 'Help' button for more info.");
                    ImGui::SetTooltip("%s", add_custom_goal_tooltip_buffer);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(Hotkeys are configured in the main Settings window)");

                // Determine if search is active for this scope
                bool is_custom_search_active = (current_search_scope == SCOPE_CUSTOM && tc_search_buffer[0] != '\0');

                // Create a filtered list to render
                std::vector<EditorTrackableItem *> goals_to_render;
                if (is_custom_search_active) {
                    for (auto &goal: current_template_data.custom_goals) {
                        char goal_str[32];
                        snprintf(goal_str, sizeof(goal_str), "%d", goal.goal);

                        if (str_contains_insensitive(goal.display_name, tc_search_buffer) ||
                            str_contains_insensitive(goal.root_name, tc_search_buffer) ||
                            str_contains_insensitive(goal.icon_path, tc_search_buffer) ||
                            (goal.goal != 0 && strstr(goal_str, tc_search_buffer) != nullptr)) {
                            goals_to_render.push_back(&goal);
                        }
                    }
                } else {
                    for (auto &goal: current_template_data.custom_goals) {
                        goals_to_render.push_back(&goal);
                    }
                }

                // --- Counter for the list ---
                char counter_text[128];
                size_t count = goals_to_render.size();
                snprintf(counter_text, sizeof(counter_text), "%zu %s", count,
                         count == 1 ? "Custom Goal" : "Custom Goals");
                float text_width = ImGui::CalcTextSize(counter_text).x;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - text_width);
                ImGui::TextDisabled("%s", counter_text);

                int item_to_remove = -1;
                int item_to_copy = -1;
                int custom_dnd_source_index = -1;
                int custom_dnd_target_index = -1;

                for (size_t i = 0; i < goals_to_render.size(); ++i) {
                    auto &goal = *goals_to_render[i];

                    ImGui::PushID(i);

                    // Add some vertical spacing to create a gap
                    ImGui::Spacing();
                    //Create a wide, 8-pixel-high invisible button to act as our drop zone
                    ImGui::InvisibleButton("drop_target", ImVec2(-1, 8.0f));

                    // Drop target for dropping between items
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CUSTOM_GOAL_DND")) {
                            custom_dnd_source_index = *(const int *) payload->Data;
                            custom_dnd_target_index = i;
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // Draw a separator for visual feedback after the drop zone
                    ImGui::Separator();

                    // Get cursor position to overlay the invisible button later
                    ImVec2 item_start_cursor_pos = ImGui::GetCursorScreenPos();

                    ImGui::BeginGroup();
                    if (ImGui::InputText("Goal Root Name", goal.root_name, sizeof(goal.root_name))) {
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    if (ImGui::IsItemHovered()) {
                        char root_name_tooltip_buffer[256];
                        snprintf(root_name_tooltip_buffer, sizeof(root_name_tooltip_buffer),
                                 "A unique ID for this custom goal (e.g., 'fun_counter').\n"
                                 "This is used to save progress and assign hotkeys.");
                        ImGui::SetTooltip("%s", root_name_tooltip_buffer);
                    }
                    if (ImGui::InputText("Display Name", goal.display_name, sizeof(goal.display_name))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char display_name_tooltip_buffer[256];
                        snprintf(display_name_tooltip_buffer, sizeof(display_name_tooltip_buffer),
                                 "The user-facing name for this custom goal.\n"
                                 "If target value isn't 0 you'll find this name at the bottom\n"
                                 "of the settings window to configure hotkeys.");
                        ImGui::SetTooltip("%s", display_name_tooltip_buffer);
                    }
                    if (ImGui::InputText("Icon Path", goal.icon_path, sizeof(goal.icon_path))) {
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    if (ImGui::IsItemHovered()) {
                        char icon_path_tooltip_buffer[128];
                        snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                 "Path to the icon file, relative to the 'resources/icons' directory.");
                        ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##CritIcon")) {
                        char new_path[MAX_PATH_LENGTH];
                        if (open_icon_file_dialog(new_path, sizeof(new_path))) {
                            strncpy(goal.icon_path, new_path, sizeof(goal.icon_path) - 1);
                            goal.icon_path[sizeof(goal.icon_path) - 1] = '\0';
                            save_message_type = MSG_NONE;
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        char icon_path_tooltip_buffer[1024];
                        snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                 "The icon must be inside the 'resources/icons' folder!");
                        ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                    }
                    if (ImGui::InputInt("Target Value", &goal.goal)) {
                        // No values below -1 allowed
                        if (goal.goal < -1) {
                            goal.goal = -1;
                        }
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    if (ImGui::IsItemHovered()) {
                        char target_goal_tooltip_buffer[1024];
                        snprintf(target_goal_tooltip_buffer, sizeof(target_goal_tooltip_buffer),
                                 "Set the goal's behavior:\n"
                                 "0 = Simple on/off toggle.\n"
                                 "-1 = Infinite counter.\n"
                                 ">0 = Progress-based counter that completes at this value.");
                        ImGui::SetTooltip("%s", target_goal_tooltip_buffer);
                    }
                    if (ImGui::Checkbox("Hidden", &goal.is_hidden)) {
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    if (ImGui::IsItemHovered()) {
                        char hidden_tooltip_buffer[256];
                        snprintf(hidden_tooltip_buffer, sizeof(hidden_tooltip_buffer),
                                 "If checked, this custom goal will be fully hidden on the overlay\n"
                                 "and hidden settings-based on the tracker.\n"
                                 "Visibility can be toggled in the main tracker settings");
                        ImGui::SetTooltip("%s", hidden_tooltip_buffer);
                    }

                    ImGui::SameLine();
                    if (ImGui::Checkbox("Row 2", &goal.in_2nd_row)) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[256];
                        if (creator_selected_version != MC_VERSION_25W14CRAFTMINE) {
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Force this stat category to display on the 2nd row of the overlay\n"
                                     "(normally reserved for %s).", advancements_label_plural_lower);
                        } else {
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Force this stat category to display on the 2nd row of the overlay\n"
                                     "(normally reserved for %s/unlocks).", advancements_label_plural_lower);
                        }
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }

                    ImGui::SameLine();

                    // "Copy" button for custom goals
                    if (ImGui::Button("Copy")) {
                        item_to_copy = i;
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[128];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Duplicate Custom Goal:\n%s", goal.root_name);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove")) {
                        item_to_remove = i;
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[128];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Remove Custom Goal:\n%s", goal.root_name);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }

                    ImGui::EndGroup();

                    // Create an invisible button over the entire group. This is our drag handle.
                    ImGui::SetCursorScreenPos(item_start_cursor_pos);
                    ImGui::InvisibleButton("dnd_handle", ImGui::GetItemRectSize());

                    // Add the required flag here
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("CUSTOM_GOAL_DND", &i, sizeof(int));
                        ImGui::Text("Reorder %s", goal.root_name);
                        ImGui::EndDragDropSource();
                    }

                    ImGui::PopID();
                }

                // Final drop target for end of list
                ImGui::InvisibleButton("final_drop_target_custom", ImVec2(-1, 8.0f)); // Added larger drop zone
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CUSTOM_GOAL_DND")) {
                        custom_dnd_source_index = *(const int *) payload->Data;
                        custom_dnd_target_index = current_template_data.custom_goals.size();
                    }
                    ImGui::EndDragDropTarget();
                }

                if (custom_dnd_source_index != -1 && custom_dnd_target_index != -1 && custom_dnd_source_index !=
                    custom_dnd_target_index) {
                    EditorTrackableItem item_to_move = current_template_data.custom_goals[custom_dnd_source_index];
                    current_template_data.custom_goals.erase(
                        current_template_data.custom_goals.begin() + custom_dnd_source_index);
                    if (custom_dnd_target_index > custom_dnd_source_index) custom_dnd_target_index--;
                    current_template_data.custom_goals.insert(
                        current_template_data.custom_goals.begin() + custom_dnd_target_index, item_to_move);
                    save_message_type = MSG_NONE;
                }


                if (item_to_remove != -1) {
                    current_template_data.custom_goals.erase(
                        current_template_data.custom_goals.begin() + item_to_remove);
                    save_message_type = MSG_NONE;
                }

                // Logic to handle the copy action after the loop
                if (item_to_copy != -1) {
                    const auto &source_item = current_template_data.custom_goals[item_to_copy];

                    // Perform a manual, safe copy.
                    EditorTrackableItem new_item;
                    strncpy(new_item.root_name, source_item.root_name, sizeof(new_item.root_name));
                    new_item.root_name[sizeof(new_item.root_name) - 1] = '\0';
                    strncpy(new_item.display_name, source_item.display_name, sizeof(new_item.display_name));
                    new_item.display_name[sizeof(new_item.display_name) - 1] = '\0';
                    strncpy(new_item.icon_path, source_item.icon_path, sizeof(new_item.icon_path));
                    new_item.icon_path[sizeof(new_item.icon_path) - 1] = '\0';
                    new_item.goal = source_item.goal;
                    new_item.is_hidden = source_item.is_hidden;

                    char base_name[192];
                    strncpy(base_name, source_item.root_name, sizeof(base_name) - 1);
                    base_name[sizeof(base_name) - 1] = '\0';
                    char new_name[192];
                    int copy_counter = 1;
                    while (true) {
                        if (copy_counter == 1) snprintf(new_name, sizeof(new_name), "%s_copy", base_name);
                        else snprintf(new_name, sizeof(new_name), "%s_copy%d", base_name, copy_counter);
                        bool name_exists = false;
                        for (const auto &item: current_template_data.custom_goals) {
                            if (strcmp(item.root_name, new_name) == 0) {
                                name_exists = true;
                                break;
                            }
                        }
                        if (!name_exists) break;
                        copy_counter++;
                    }
                    strncpy(new_item.root_name, new_name, sizeof(new_item.root_name) - 1);
                    new_item.root_name[sizeof(new_item.root_name) - 1] = '\0';
                    current_template_data.custom_goals.insert(
                        current_template_data.custom_goals.begin() + item_to_copy + 1, new_item);
                    save_message_type = MSG_NONE;
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Multi-Stage Goals")) {
                // Flag to track changes in this version
                // Specifically for hidden legacy stats for multi-stage goals
                // This flag still tracks ALL CHANGES within multi-stage goals
                bool ms_goal_data_changed = false;

                // TWO-PANE LAYOUT for Multi-Stage Goals
                float pane_width = ImGui::GetContentRegionAvail().x * 0.4f;
                ImGui::BeginChild("MSGoalListPane", ImVec2(pane_width, 0), true);

                if (ImGui::Button("Add New Multi-Stage Goal")) {
                    // Before modifying the vector, store the root_name of the currently selected item.
                    char selected_root_name_before_op[192] = {};
                    if (selected_ms_goal) {
                        strncpy(selected_root_name_before_op, selected_ms_goal->root_name,
                                sizeof(selected_root_name_before_op) - 1);
                        selected_root_name_before_op[sizeof(selected_root_name_before_op) - 1] = '\0';
                    }


                    // Create a new multi-stage goal with default values and a final stage
                    EditorMultiStageGoal new_goal = {};
                    int counter = 1;
                    while (true) {
                        snprintf(new_goal.root_name, sizeof(new_goal.root_name), "new_ms_goal_%d", counter);
                        bool name_exists = false;
                        for (const auto &goal: current_template_data.multi_stage_goals) {
                            if (strcmp(goal.root_name, new_goal.root_name) == 0) {
                                name_exists = true;
                                break;
                            }
                        }
                        if (!name_exists) break;
                        counter++;
                    }
                    snprintf(new_goal.display_name, sizeof(new_goal.display_name), "New Multi-Stage Goal %d", counter);
                    strncpy(new_goal.icon_path, "blocks/placeholder.png", sizeof(new_goal.icon_path) - 1);
                    new_goal.icon_path[sizeof(new_goal.icon_path) - 1] = '\0';

                    // Add a default "Final" stage, which is required for validation
                    EditorSubGoal final_stage = {};
                    strncpy(final_stage.stage_id, "final", sizeof(final_stage.stage_id) - 1);
                    final_stage.stage_id[sizeof(final_stage.stage_id) - 1] = '\0';
                    strncpy(final_stage.display_text, "Final Stage", sizeof(final_stage.display_text) - 1);
                    final_stage.display_text[sizeof(final_stage.display_text) - 1] = '\0';
                    final_stage.type = SUBGOAL_MANUAL;
                    new_goal.stages.push_back(final_stage);

                    current_template_data.multi_stage_goals.push_back(new_goal);

                    // After modifying the vector, re-find the selected item to get a valid pointer.
                    if (selected_root_name_before_op[0] != '\0') {
                        for (auto &goal: current_template_data.multi_stage_goals) {
                            if (strcmp(goal.root_name, selected_root_name_before_op) == 0) {
                                selected_ms_goal = &goal;
                                break;
                            }
                        }
                    }

                    ms_goal_data_changed = true;
                    save_message_type = MSG_NONE;
                }
                if (ImGui::IsItemHovered()) {
                    char add_ms_goal_tooltip_buffer[1024]; // Increased buffer size for detailed text

                    if (creator_selected_version <= MC_VERSION_1_6_4) {
                        // Legacy Version Tooltip
                        snprintf(add_ms_goal_tooltip_buffer, sizeof(add_ms_goal_tooltip_buffer),
                                 "Add a new multi-stage goal to this template.\n\n"
                                 "Multi-Stage Goals get completed one stage at a time.\n"
                                 "The 'Type' of each stage determines how it is completed:\n"
                                 " • Stat / Achievement: The ID number (e.g., '2011' - Items Dropped)\n"
                                 "   to track in the stats file.\n"
                                 " • Final: The mandatory last stage that completes the goal.\n\n"
                                 "Click the 'Help' button for more info.");
                    } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                        // Mid-Era Version Tooltip
                        snprintf(add_ms_goal_tooltip_buffer, sizeof(add_ms_goal_tooltip_buffer),
                                 "Add a new multi-stage goal to this template.\n\n"
                                 "Multi-Stage Goals get completed one stage at a time.\n"
                                 "The 'Type' of each stage determines how it is completed:\n"
                                 " • Stat / Achievement: Root name (e.g., 'stat.craftItem.minecraft.planks')\n"
                                 "   to track in the stats file.\n"
                                 " • Criterion: A specific criterion (e.g., 'Sunflower Plains') of a parent achievement\n"
                                 "   (e.g., 'achievement.exploreAllBiomes').\n"
                                 " • Final: The mandatory last stage that completes the goal.\n\n"
                                 "Click the 'Help' button for more info.");
                    } else if (creator_selected_version <= MC_VERSION_1_12_2) {
                        // Modern with mid-era stats Version Tooltip
                        snprintf(add_ms_goal_tooltip_buffer, sizeof(add_ms_goal_tooltip_buffer),
                                 "Add a new multi-stage goal to this template.\n\n"
                                 "Multi-Stage Goals get completed one stage at a time.\n"
                                 "The 'Type' of each stage determines how it is completed:\n"
                                 " • Stat: Root name (e.g., 'stat.mobKills') from the stats file.\n"
                                 " • Advancement: Root name of an advancement (e.g., 'minecraft:husbandry/tame_an_animal')\n"
                                 "   or recipe (e.g., 'minecraft:recipes/redstone/stone_button') from the advancements file.\n"
                                 " • Criterion: A specific criterion (e.g., 'cave_spider')\n"
                                 "   of a parent advancement (e.g., 'minecraft:adventure/kill_a_mob').\n"
                                 " • Final: The mandatory last stage that completes the goal.\n\n"
                                 "Click the 'Help' button for more info.");
                    } else if (creator_selected_version == MC_VERSION_25W14CRAFTMINE) {
                        // 25w14craftmine Version Tooltip
                        snprintf(add_ms_goal_tooltip_buffer, sizeof(add_ms_goal_tooltip_buffer),
                                 "Add a new multi-stage goal to this template.\n\n"
                                 "Multi-Stage Goals get completed one stage at a time.\n"
                                 "The 'Type' of each stage determines how it is completed:\n"
                                 " • Stat: Root name (e.g., 'minecraft:mined/minecraft:spawner') from the stats file.\n"
                                 " • Advancement: Root name of an advancement (e.g., 'minecraft:end/levitate')\n"
                                 "   or recipe (e.g., 'minecraft:recipes/redstone/tnt') from the advancements file.\n"
                                 " • Criterion: A specific criterion (e.g., 'minecraft:wither_boss')\n"
                                 "   of a parent advancement (e.g., 'minecraft:mines/all_special_mines_completed').\n"
                                 " • Unlock: Root name (e.g., 'minecraft:exploration') from the unlocks file.\n"
                                 " • Final: The mandatory last stage that completes the goal.\n\n"
                                 "Click the 'Help' button for more info.");
                    } else {
                        // Modern Versions (1.13+ excluding craftmine) Tooltip
                        snprintf(add_ms_goal_tooltip_buffer, sizeof(add_ms_goal_tooltip_buffer),
                                 "Add a new multi-stage goal to this template.\n\n"
                                 "Multi-Stage Goals get completed one stage at a time.\n"
                                 "The 'Type' of each stage determines how it is completed:\n"
                                 " • Stat: Root name (e.g., 'minecraft:killed/minecraft:blaze') from the stats file.\n"
                                 " • Advancement: Root name of an advancement (e.g., 'minecraft:story/cure_zombie_villager')\n"
                                 "   or recipe (e.g., 'minecraft:recipes/decorations/anvil') from the advancements file.\n"
                                 " • Criterion: A specific criterion (e.g., 'minecraft:spotted')\n"
                                 "   of a parent advancement (e.g., 'minecraft:husbandry/whole_pack').\n"
                                 " • Final: The mandatory last stage that completes the goal.\n\n"
                                 "Click the 'Help' button for more info.");
                    }

                    ImGui::SetTooltip("%s", add_ms_goal_tooltip_buffer);
                }
                ImGui::SameLine();
                ImGui::Checkbox("Show Display Names", &show_ms_goal_display_names);
                ImGui::Separator();

                // 1. Create a list of pointers to render from.
                std::vector<EditorMultiStageGoal *> goals_to_render;

                // 2. Populate the list based on search criteria.
                bool search_active = (tc_search_buffer[0] != '\0' && current_search_scope == SCOPE_MULTISTAGE);

                if (search_active) {
                    for (auto &goal: current_template_data.multi_stage_goals) {
                        bool parent_match = str_contains_insensitive(goal.display_name, tc_search_buffer) ||
                                            str_contains_insensitive(goal.root_name, tc_search_buffer) ||
                                            str_contains_insensitive(goal.icon_path, tc_search_buffer);

                        if (parent_match) {
                            goals_to_render.push_back(&goal);
                            continue;
                        }

                        bool stage_match = false;
                        for (const auto &stage: goal.stages) {
                            // Convert target value into a string
                            char target_val_str[32];
                            snprintf(target_val_str, sizeof(target_val_str), "%d", stage.required_progress);

                            // Check standard fields
                            bool standard_match = str_contains_insensitive(stage.display_text, tc_search_buffer) ||
                                                  str_contains_insensitive(stage.stage_id, tc_search_buffer) ||
                                                  str_contains_insensitive(stage.root_name, tc_search_buffer) ||
                                                  str_contains_insensitive(stage.parent_advancement, tc_search_buffer) ||
                                                  strstr(target_val_str, tc_search_buffer) != nullptr;

                            // Check stage icon path IF enabled, so it works with search
                            bool icon_match = false;
                            if (goal.use_stage_icons) {
                                icon_match = str_contains_insensitive(stage.icon_path, tc_search_buffer);
                            }

                            if (standard_match || icon_match) {
                                stage_match = true;
                                break;
                            }
                        }

                        if (stage_match) {
                            goals_to_render.push_back(&goal);
                        }
                    }
                } else {
                    // Search is inactive, show all goals.
                    for (auto &goal: current_template_data.multi_stage_goals) {
                        goals_to_render.push_back(&goal);
                    }
                }

                // --- Counter for the list ---
                char counter_text[128];
                snprintf(counter_text, sizeof(counter_text), "%zu %s", goals_to_render.size(),
                         goals_to_render.size() == 1 ? "Multi-Stage Goal" : "Multi-Stage Goals");
                float text_width = ImGui::CalcTextSize(counter_text).x;
                // Center Count
                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - text_width) *
                    0.5f);
                ImGui::TextDisabled("%s", counter_text);

                // 3. Render the list using pointers.
                int goal_to_remove_idx = -1;
                int goal_to_copy_idx = -1;
                int ms_goal_dnd_source_index = -1;
                int ms_goal_dnd_target_index = -1;

                for (size_t i = 0; i < goals_to_render.size(); ++i) {
                    auto &goal = *goals_to_render[i];
                    ImGui::PushID(&goal);

                    const char *display_name = goal.display_name;
                    const char *root_name = goal.root_name;
                    const char *label = show_ms_goal_display_names
                                            ? (display_name[0] ? display_name : root_name)
                                            : root_name;
                    if (label[0] == '\0') {
                        label = "[New Goal]";
                    }

                    if (ImGui::Button("X")) { goal_to_remove_idx = i; }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[128];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Remove %s", label);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Copy")) { goal_to_copy_idx = i; }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[128];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Duplicate %s.", label);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    ImGui::SameLine();

                    if (ImGui::Selectable(label, &goal == selected_ms_goal)) {
                        if (&goal != selected_ms_goal) {
                            if (editor_has_unsaved_changes) {
                                show_unsaved_changes_popup = true;
                                pending_action = [&, new_selection = &goal]() {
                                    selected_ms_goal = new_selection;
                                };
                            } else {
                                selected_ms_goal = &goal;
                            }
                        }
                    }

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        ImGui::SetDragDropPayload("MS_GOAL_DND", &i, sizeof(int));
                        ImGui::Text("Reorder %s", label);
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MS_GOAL_DND")) {
                            ms_goal_dnd_source_index = *(const int *) payload->Data;
                            ms_goal_dnd_target_index = i;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::PopID();
                }

                // Handle Drag and Drop
                if (ms_goal_dnd_source_index != -1 && ms_goal_dnd_target_index != -1) {
                    EditorMultiStageGoal *source_item_ptr = goals_to_render[ms_goal_dnd_source_index];
                    EditorMultiStageGoal *target_item_ptr = goals_to_render[ms_goal_dnd_target_index];

                    auto source_it = std::find_if(current_template_data.multi_stage_goals.begin(),
                                                  current_template_data.multi_stage_goals.end(),
                                                  [&](const EditorMultiStageGoal &g) { return &g == source_item_ptr; });

                    EditorMultiStageGoal item_to_move = *source_item_ptr;
                    current_template_data.multi_stage_goals.erase(source_it);

                    auto target_it = std::find_if(current_template_data.multi_stage_goals.begin(),
                                                  current_template_data.multi_stage_goals.end(),
                                                  [&](const EditorMultiStageGoal &g) { return &g == target_item_ptr; });

                    current_template_data.multi_stage_goals.insert(target_it, item_to_move);
                    ms_goal_data_changed = true;
                    save_message_type = MSG_NONE;
                }

                // Handle Copying
                if (goal_to_copy_idx != -1) {
                    char selected_root_name_before_op[192] = {};
                    if (selected_ms_goal) {
                        strncpy(selected_root_name_before_op, selected_ms_goal->root_name,
                                sizeof(selected_root_name_before_op) - 1);
                    }

                    const EditorMultiStageGoal *source_goal_ptr = goals_to_render[goal_to_copy_idx];

                    // Perform a manual, safe copy to prevent memory corruption.
                    EditorMultiStageGoal new_goal = {};  // Zero-initialize to prevent garbage in unset fields
                    strncpy(new_goal.root_name, source_goal_ptr->root_name, sizeof(new_goal.root_name));
                    new_goal.root_name[sizeof(new_goal.root_name) - 1] = '\0';
                    strncpy(new_goal.display_name, source_goal_ptr->display_name, sizeof(new_goal.display_name));
                    new_goal.display_name[sizeof(new_goal.display_name) - 1] = '\0';
                    strncpy(new_goal.icon_path, source_goal_ptr->icon_path, sizeof(new_goal.icon_path));
                    new_goal.icon_path[sizeof(new_goal.icon_path) - 1] = '\0';
                    new_goal.is_hidden = source_goal_ptr->is_hidden;
                    new_goal.in_2nd_row = source_goal_ptr->in_2nd_row;
                    new_goal.use_stage_icons = source_goal_ptr->use_stage_icons;
                    new_goal.stages = source_goal_ptr->stages; // std::vector handles its own deep copy safely.

                    // Now, generate a unique name for the new copy
                    char base_name[192];
                    strncpy(base_name, new_goal.root_name, sizeof(base_name));
                    base_name[sizeof(base_name) - 1] = '\0'; // Ensure base_name is safe to use

                    char new_name[192];
                    int copy_counter = 1;
                    while (true) {
                        if (copy_counter == 1) snprintf(new_name, sizeof(new_name), "%s_copy", base_name);
                        else snprintf(new_name, sizeof(new_name), "%s_copy%d", base_name, copy_counter);
                        bool name_exists = false;
                        for (const auto &g: current_template_data.multi_stage_goals) {
                            if (strcmp(g.root_name, new_name) == 0) {
                                name_exists = true;
                                break;
                            }
                        }
                        if (!name_exists) break;
                        copy_counter++;
                    }

                    // Safely apply the new unique name
                    strncpy(new_goal.root_name, new_name, sizeof(new_goal.root_name));
                    new_goal.root_name[sizeof(new_goal.root_name) - 1] = '\0';


                    auto it = std::find_if(current_template_data.multi_stage_goals.begin(),
                                           current_template_data.multi_stage_goals.end(),
                                           [&](const EditorMultiStageGoal &g) { return &g == source_goal_ptr; });

                    if (it != current_template_data.multi_stage_goals.end()) {
                        current_template_data.multi_stage_goals.insert(it + 1, new_goal);
                    } else {
                        current_template_data.multi_stage_goals.push_back(new_goal);
                    }

                    // Re-find and update the selected_ms_goal pointer
                    if (selected_root_name_before_op[0] != '\0') {
                        for (auto &goal: current_template_data.multi_stage_goals) {
                            if (strcmp(goal.root_name, selected_root_name_before_op) == 0) {
                                selected_ms_goal = &goal;
                                break;
                            }
                        }
                    }
                    ms_goal_data_changed = true;
                    save_message_type = MSG_NONE;
                }

                // Handle Removal
                if (goal_to_remove_idx != -1) {
                    char selected_root_name_before_op[192] = {};
                    if (selected_ms_goal) {
                        strncpy(selected_root_name_before_op, selected_ms_goal->root_name,
                                sizeof(selected_root_name_before_op) - 1);
                    }

                    EditorMultiStageGoal *goal_to_remove = goals_to_render[goal_to_remove_idx];
                    if (selected_ms_goal == goal_to_remove) {
                        selected_ms_goal = nullptr;
                    }

                    current_template_data.multi_stage_goals.erase(
                        std::remove_if(current_template_data.multi_stage_goals.begin(),
                                       current_template_data.multi_stage_goals.end(),
                                       [&](const EditorMultiStageGoal &g) { return &g == goal_to_remove; }),
                        current_template_data.multi_stage_goals.end()
                    );

                    // Re-find pointer if it wasn't the one being deleted
                    if (selected_ms_goal && selected_root_name_before_op[0] != '\0') {
                        for (auto &goal: current_template_data.multi_stage_goals) {
                            if (strcmp(goal.root_name, selected_root_name_before_op) == 0) {
                                selected_ms_goal = &goal;
                                break;
                            }
                        }
                    }
                    ms_goal_data_changed = true;
                    save_message_type = MSG_NONE;
                }

                ImGui::EndChild();
                ImGui::SameLine();

                ImGui::BeginChild("MSGoalDetailsPane", ImVec2(0, 0), true);
                if (selected_ms_goal != nullptr) {
                    auto &goal = *selected_ms_goal;

                    ImGui::Text("Edit Multi-Stage Goal Details");
                    ImGui::Separator();

                    if (ImGui::InputText("Goal Root Name", goal.root_name, sizeof(goal.root_name))) {
                        ms_goal_data_changed = true;
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char root_name_tooltip_buffer[256];
                        snprintf(root_name_tooltip_buffer, sizeof(root_name_tooltip_buffer),
                                 "A unique ID for this entire multi-stage goal (e.g., 'awesome_ms_goal').");
                        ImGui::SetTooltip("%s", root_name_tooltip_buffer);
                    }
                    if (ImGui::InputText("Display Name", goal.display_name, sizeof(goal.display_name))) {
                        ms_goal_data_changed = true;
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char display_name_tooltip_buffer[128];
                        snprintf(display_name_tooltip_buffer, sizeof(display_name_tooltip_buffer),
                                 "The user-facing name for this multi-stage goal.");
                        ImGui::SetTooltip("%s", display_name_tooltip_buffer);
                    }
                    if (ImGui::InputText("Icon Path", goal.icon_path, sizeof(goal.icon_path))) {
                        ms_goal_data_changed = true;
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char icon_path_tooltip_buffer[1024];
                        snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                 "Path to the icon file, relative to the 'resources/icons' directory.");
                        ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##MSGoalIcon")) {
                        char new_path[MAX_PATH_LENGTH];
                        if (open_icon_file_dialog(new_path, sizeof(new_path))) {
                            strncpy(goal.icon_path, new_path, sizeof(goal.icon_path) - 1);
                            goal.icon_path[sizeof(goal.icon_path) - 1] = '\0';

                            ms_goal_data_changed = true;
                            save_message_type = MSG_NONE;
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        char icon_path_tooltip_buffer[1024];
                        snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                 "The icon must be inside the 'resources/icons' folder!");
                        ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                    }
                    if (ImGui::Checkbox("Hidden", &goal.is_hidden)) {
                        ms_goal_data_changed = true;
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char hidden_tooltip_buffer[512];
                        snprintf(hidden_tooltip_buffer, sizeof(hidden_tooltip_buffer),
                                 "If checked, this multi-stage goal will be fully hidden on the overlay\n"
                                 "and hidden settings-based on the tracker.\n"
                                 "Visibility can be toggled in the main tracker settings");
                        ImGui::SetTooltip("%s", hidden_tooltip_buffer);
                    }

                    ImGui::SameLine();
                    if (ImGui::Checkbox("Row 2", &goal.in_2nd_row)) {
                        ms_goal_data_changed = true;
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[256];
                        if (creator_selected_version != MC_VERSION_25W14CRAFTMINE) {
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Force this stat category to display on the 2nd row of the overlay\n"
                                     "(normally reserved for %s).", advancements_label_plural_lower);
                        } else {
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Force this stat category to display on the 2nd row of the overlay\n"
                                     "(normally reserved for %s/unlocks).", advancements_label_plural_lower);
                        }
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Per-Stage Icons", &goal.use_stage_icons)) {
                        ms_goal_data_changed = true;
                        save_message_type = MSG_NONE;

                        // Pre-fill stage icons with main goal icon to avoid errors
                        if (goal.use_stage_icons) {
                            for (auto &stage : goal.stages) {
                                // Only pre-fill if currently empty to preserve previous edits if toggled off/on
                                if (stage.icon_path[0] == '\0') {
                                    if (goal.icon_path[0] != '\0') {
                                        strncpy(stage.icon_path, goal.icon_path, sizeof(stage.icon_path) - 1);
                                    } else {
                                        // Fallback just in case main icon is also empty
                                        strncpy(stage.icon_path, "blocks/placeholder.png", sizeof(stage.icon_path) - 1);
                                    }
                                    stage.icon_path[sizeof(stage.icon_path) - 1] = '\0';
                                }
                            }
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip[256];
                        snprintf(tooltip, sizeof(tooltip), "Enable unique icons for every stage.\n"
                                 "If unchecked, the main goal icon is used for all stages.");
                        ImGui::SetTooltip("%s", tooltip);
                    }

                    ImGui::Separator();
                    ImGui::Text("Stages");

                    // --- Counter for the stages list ---
                    char stage_counter_text[128];
                    bool is_details_search_active = (
                        current_search_scope == SCOPE_MULTISTAGE_DETAILS && tc_search_buffer[0] != '\0');
                    int visible_stages_count = 0;
                    if (!is_details_search_active) {
                        visible_stages_count = goal.stages.size();
                    } else {
                        for (const auto &stage: goal.stages) {
                            char target_val_str[32];
                            snprintf(target_val_str, sizeof(target_val_str), "%d", stage.required_progress);
                            if (str_contains_insensitive(stage.display_text, tc_search_buffer) ||
                                str_contains_insensitive(stage.stage_id, tc_search_buffer) ||
                                str_contains_insensitive(stage.root_name, tc_search_buffer) ||
                                str_contains_insensitive(stage.parent_advancement, tc_search_buffer) ||
                                (stage.required_progress != 0 && strstr(target_val_str, tc_search_buffer) != nullptr)) {
                                visible_stages_count++;
                            }
                        }
                    }
                    snprintf(stage_counter_text, sizeof(stage_counter_text), "%d %s", visible_stages_count,
                             visible_stages_count == 1 ? "Stage" : "Stages");
                    float stage_text_width = ImGui::CalcTextSize(stage_counter_text).x;
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - stage_text_width);
                    ImGui::TextDisabled("%s", stage_counter_text);


                    if (ImGui::Button("Add New Stage")) {
                        // Create a new stage with default values and insert it before the final stage
                        EditorSubGoal new_stage = {};
                        int counter = 1;
                        while (true) {
                            snprintf(new_stage.stage_id, sizeof(new_stage.stage_id), "new_stage_%d", counter);
                            bool name_exists = false;
                            for (const auto &stage: goal.stages) {
                                if (strcmp(stage.stage_id, new_stage.stage_id) == 0) {
                                    name_exists = true;
                                    break;
                                }
                            }
                            if (!name_exists) break;
                            counter++;
                        }
                        snprintf(new_stage.display_text, sizeof(new_stage.display_text), "New Stage %d", counter);
                        new_stage.type = SUBGOAL_STAT; // Default to a common type
                        // Version-aware root name for the new stage's trigger
                        if (creator_selected_version <= MC_VERSION_1_6_4) {
                            strncpy(new_stage.root_name, "0", sizeof(new_stage.root_name) - 1);
                        } else if (creator_selected_version <= MC_VERSION_1_12_2) {
                            // Mid-era (1.7 -1.12.2)
                            strncpy(new_stage.root_name, "stat.cool", sizeof(new_stage.root_name) - 1);
                        } else {
                            // Modern (1.13+)
                            strncpy(new_stage.root_name, "minecraft:custom/minecraft:new_stat",
                                    sizeof(new_stage.root_name) - 1);
                        }
                        new_stage.root_name[sizeof(new_stage.root_name) - 1] = '\0';
                        new_stage.required_progress = 1;

                        // Insert the new stage just before the last element (which should be the "final" stage)
                        if (!goal.stages.empty()) {
                            goal.stages.insert(goal.stages.end() - 1, new_stage);
                        } else {
                            // Should not happen if new goals are created correctly, but as a fallback:
                            goal.stages.push_back(new_stage);
                        }

                        ms_goal_data_changed = true;
                        save_message_type = MSG_NONE;
                    }

                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[1024];

                        if (creator_selected_version <= MC_VERSION_1_6_4) {
                            // Legacy Version Tooltip
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Adds a new step to this multi-stage goal.\n\n"
                                     "Stages are completed sequentially. New stages are always added before the 'Final' stage.\n\n"
                                     "Available Stage Types for this version:\n"
                                     " • Stat / Achievement: Completes when a stat (e.g., 16777217 - Stone mined)\n"
                                     "   or achievement (e.g., 5242905 - Overkill) reaches the 'Target Value'.\n\n"
                                     "Click the 'Help' button for more info.");
                        } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                            // Mid-Era Version Tooltip
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Adds a new step to this multi-stage goal.\n\n"
                                     "Stages are completed sequentially. New stages are always added before the 'Final' stage.\n\n"
                                     "Available Stage Types for this version:\n"
                                     " • Stat / Achievement: Completes when a stat (e.g., stat.fallOneCm)\n"
                                     "   or achievement (e.g., 'achievement.buildPickaxe') reaches the 'Target Value'.\n"
                                     " • Criterion: Completes when a specific criterion (e.g., 'Deep Ocean')\n"
                                     "   of a parent achievement (e.g., 'achievement.exploreAllBiomes') is met.\n\n"
                                     "Click the 'Help' button for more info.");
                        } else if (creator_selected_version <= MC_VERSION_1_12_2) {
                            // Mid-era stats, modern advancements
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Adds a new step to this multi-stage goal.\n\n"
                                     "Stages are completed sequentially. New stages are always added before the 'Final' stage.\n\n"
                                     "Available Stage Types for this version:\n"
                                     " • Stat: Completes when a stat (e.g., 'stat.useItem.minecraft.beacon'\n"
                                     "   reaches the 'Target Value'.\n"
                                     " • Advancement: Completes when an advancement (e.g., 'minecraft:story/root')\n"
                                     "   or recipe (e.g., 'minecraft:recipes/tools/stone_shovel') is obtained.\n"
                                     " • Criterion: Completes when a specific criterion (e.g., 'bred_mooshroom')\n"
                                     "   of a parent advancement (e.g., 'minecraft:husbandry/bred_all_animals') is met.\n\n"
                                     "Click the 'Help' button for more info.");
                        } else if (creator_selected_version == MC_VERSION_25W14CRAFTMINE) {
                            // 25w14craftmine Version Tooltip
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Adds a new step to this multi-stage goal.\n\n"
                                     "Stages are completed sequentially. New stages are always added before the 'Final' stage.\n\n"
                                     "Available Stage Types for this version:\n"
                                     " • Stat: Completes when a stat (e.g., 'minecraft:custom/minecraft:aviate_one_cm')\n"
                                     "   reaches the 'Target Value'.\n"
                                     " • Advancement: Completes when an advancement (e.g., 'minecraft:feats/kuiper_world')\n"
                                     "   or recipe (e.g., 'minecraft:recipes/misc/exit_eye') is obtained.\n"
                                     " • Criterion: Completes when a specific criterion (e.g., 'minecraft:floating_islands_world')\n"
                                     "   of a parent advancement (e.g., 'minecraft:mines/all_mine_ingredients') is met.\n"
                                     " • Unlock: Completes when a specific player unlock (e.g., 'minecraft:jump_king_10') is obtained.\n\n"
                                     "Click the 'Help' button for more info.");
                        } else {
                            // Modern Versions (1.13+ excluding craftmine) Tooltip
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Adds a new step to this multi-stage goal.\n\n"
                                     "Stages are completed sequentially. New stages are always added before the 'Final' stage.\n\n"
                                     "Available Stage Types for this version:\n"
                                     " • Stat: Completes when a stat (e.g., 'minecraft:killed/minecraft:endermite'\n"
                                     "   reaches the 'Target Value'.\n"
                                     " • Advancement: Completes when an advancement (e.g., 'minecraft:nether/ride_strider')\n"
                                     "   or recipe (e.g., 'minecraft:recipes/decorations/grindstone') is obtained.\n"
                                     " • Criterion: Completes when a specific criterion (e.g., 'minecraft:creaking')\n"
                                     "   of a parent advancement (e.g., 'minecraft:adventure/kill_all_mobs') is met.\n\n"
                                     "Click the 'Help' button for more info.");
                        }

                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }

                    int stage_to_remove = -1;
                    int stage_to_copy = -1;

                    int stage_dnd_source_index = -1;
                    int stage_dnd_target_index = -1;

                    for (size_t j = 0; j < goal.stages.size(); ++j) {
                        auto &stage = goal.stages[j];

                        if (is_details_search_active) {
                            char target_val_str[32];
                            snprintf(target_val_str, sizeof(target_val_str), "%d", stage.required_progress);

                            // Check standard fields
                            bool standard_match = str_contains_insensitive(stage.display_text, tc_search_buffer) ||
                                                  str_contains_insensitive(stage.stage_id, tc_search_buffer) ||
                                                  str_contains_insensitive(stage.root_name, tc_search_buffer) ||
                                                  str_contains_insensitive(stage.parent_advancement, tc_search_buffer) ||
                                                  (stage.required_progress != 0 && strstr(target_val_str, tc_search_buffer) != nullptr);

                            // Check stage icon path IF enabled
                            bool icon_match = false;
                            if (goal.use_stage_icons) {
                                icon_match = str_contains_insensitive(stage.icon_path, tc_search_buffer);
                            }

                            // If neither matches, skip this stage
                            if (!standard_match && !icon_match) {
                                continue;
                            }
                        }

                        ImGui::PushID(j);

                        // Add some vertical spacing to create a gap
                        ImGui::Spacing();

                        // Use an invisible button as a drop target between items
                        ImGui::InvisibleButton("drop_target", ImVec2(-1, 8.0f));
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MS_STAGE_DND")) {
                                stage_dnd_source_index = *(const int *) payload->Data;
                                stage_dnd_target_index = j;
                            }
                            ImGui::EndDragDropTarget();
                        }

                        ImGui::Separator();

                        // Group all stage controls to make them a single drag source
                        ImGui::BeginGroup();
                        if (ImGui::InputText("Stage ID", stage.stage_id, sizeof(stage.stage_id))) {
                            ms_goal_data_changed = true;
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[256];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "A unique ID for this specific stage within the goal.");
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }
                        if (ImGui::InputText("Display Text", stage.display_text, sizeof(stage.display_text))) {
                            ms_goal_data_changed = true;
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[256];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "The text that appears on the tracker/overlay for this stage.\n"
                                     "For the 'Final' stage, put something like 'Stages Done!'.");
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }
                        // Conditional Icon Input per stage if enabled
                        if (goal.use_stage_icons) {
                            if (ImGui::InputText("Stage Icon", stage.icon_path, sizeof(stage.icon_path))) {
                                ms_goal_data_changed = true;
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::IsItemHovered()) {
                                char icon_path_tooltip_buffer[256];
                                snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                         "Path to the icon file for this specific stage,\n"
                                         "relative to the 'resources/icons' directory.");
                                ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Browse##StageIcon")) {
                                char new_path[MAX_PATH_LENGTH];
                                if (open_icon_file_dialog(new_path, sizeof(new_path))) {
                                    strncpy(stage.icon_path, new_path, sizeof(stage.icon_path) - 1);
                                    stage.icon_path[sizeof(stage.icon_path) - 1] = '\0';
                                    ms_goal_data_changed = true;
                                    save_message_type = MSG_NONE;
                                }
                            }
                            if (ImGui::IsItemHovered()) {
                                char icon_path_tooltip_buffer[128];
                                snprintf(icon_path_tooltip_buffer, sizeof(icon_path_tooltip_buffer),
                                         "The icon must be inside the 'resources/icons' folder!");
                                ImGui::SetTooltip("%s", icon_path_tooltip_buffer);
                            }
                        }
                        // --- Version-Aware Type Dropdown ---
                        const char *current_type_name = "Unknown";
                        switch (stage.type) {
                            case SUBGOAL_STAT:
                                // Use the clearer "Stat / Achievement" label for older versions
                                current_type_name = (creator_selected_version <= MC_VERSION_1_11_2)
                                                        ? "Stat / Achievement"
                                                        : "Stat";
                                break;
                            case SUBGOAL_ADVANCEMENT: current_type_name = advancements_label_upper;
                                break;
                            case SUBGOAL_UNLOCK: current_type_name = "Unlock";
                                break;
                            case SUBGOAL_CRITERION: current_type_name = "Criterion";
                                break;
                            case SUBGOAL_MANUAL: current_type_name = "Final";
                                break;
                        }

                        if (ImGui::BeginCombo("Type", current_type_name)) {
                            // Show "Stat / Achievement" for legacy and mid-era versions
                            if (creator_selected_version <= MC_VERSION_1_11_2) {
                                if (ImGui::Selectable("Stat / Achievement", stage.type == SUBGOAL_STAT)) {
                                    stage.type = SUBGOAL_STAT;
                                    ms_goal_data_changed = true;
                                    save_message_type = MSG_NONE;
                                }
                            }

                            // Show plain "Stat" for modern versions
                            if (creator_selected_version >= MC_VERSION_1_12) {
                                if (ImGui::Selectable("Stat", stage.type == SUBGOAL_STAT)) {
                                    stage.type = SUBGOAL_STAT;
                                    ms_goal_data_changed = true;
                                    save_message_type = MSG_NONE;
                                }
                            }

                            // "Advancement" type is only available for 1.12+
                            if (creator_selected_version >= MC_VERSION_1_12) {
                                if (ImGui::Selectable(advancements_label_upper, stage.type == SUBGOAL_ADVANCEMENT)) {
                                    stage.type = SUBGOAL_ADVANCEMENT;
                                    ms_goal_data_changed = true;
                                    save_message_type = MSG_NONE;
                                }
                            }

                            // "Criterion" type is available from mid-era (1.7.2) onwards
                            if (creator_selected_version >= MC_VERSION_1_7_2) {
                                if (ImGui::Selectable("Criterion", stage.type == SUBGOAL_CRITERION)) {
                                    stage.type = SUBGOAL_CRITERION;
                                    ms_goal_data_changed = true;
                                    save_message_type = MSG_NONE;
                                }
                            }

                            // "Unlock" type is only for 25w14craftmine
                            if (creator_selected_version == MC_VERSION_25W14CRAFTMINE) {
                                if (ImGui::Selectable("Unlock", stage.type == SUBGOAL_UNLOCK)) {
                                    stage.type = SUBGOAL_UNLOCK;
                                    ms_goal_data_changed = true;
                                    save_message_type = MSG_NONE;
                                }
                            }

                            // "Final" type is always available
                            if (ImGui::Selectable("Final", stage.type == SUBGOAL_MANUAL)) {
                                stage.type = SUBGOAL_MANUAL;
                                ms_goal_data_changed = true;
                                save_message_type = MSG_NONE;
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::IsItemHovered()) {
                            char type_tooltip_buffer[256];
                            if (creator_selected_version <= MC_VERSION_1_11_2) {
                                // not modern
                                snprintf(type_tooltip_buffer, sizeof(type_tooltip_buffer),
                                         "The type of event that will complete this stage.\n"
                                         "%s count as stats.\n"
                                         "There must be exactly one 'Final' stage ('Done!' - Stage),\n"
                                         "and it must be the last stage.\n"
                                         "Reaching the final stage completes the entire multi-stage goal.",
                                         advancements_label_plural_upper);
                            } else {
                                // modern
                                snprintf(type_tooltip_buffer, sizeof(type_tooltip_buffer),
                                         "The type of event that will complete this stage.\n"
                                         "%s can also be recipes.\n"
                                         "There must be exactly one 'Final' stage ('Done!' - Stage),\n"
                                         "and it must be the last stage.\n"
                                         "Reaching the final stage completes the entire multi-stage goal.",
                                         advancements_label_plural_upper);
                            }
                            ImGui::SetTooltip("%s", type_tooltip_buffer);
                        }


                        if (stage.type == SUBGOAL_CRITERION) {
                            char parent_label[64];
                            snprintf(parent_label, sizeof(parent_label), "Parent %s", advancements_label_upper);
                            if (ImGui::InputText(parent_label, stage.parent_advancement,
                                                 sizeof(stage.parent_advancement))) {
                                ms_goal_data_changed = true;
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buffer[512];
                                if (creator_selected_version <= MC_VERSION_1_11_2) {
                                    // Mid-era tooltip
                                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                             "The root name of the parent %s this criterion belongs to.\n"
                                             "e.g., 'achievement.exploreAllBiomes'",
                                             advancements_label_singular_lower);
                                } else {
                                    // Modern tooltip
                                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                             "The root name of the parent %s this criterion belongs to.\n"
                                             "e.g., 'minecraft:husbandry/bred_all_animals'",
                                             advancements_label_singular_lower);
                                }
                                ImGui::SetTooltip("%s", tooltip_buffer);
                            }
                        }

                        // "Final" stages don't need a target or Root Name
                        if (stage.type != SUBGOAL_MANUAL) {
                            if (ImGui::InputText("Trigger Root Name", stage.root_name, sizeof(stage.root_name))) {
                                ms_goal_data_changed = true;
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buffer[512];
                                if (creator_selected_version <= MC_VERSION_1_6_4) {
                                    // Legacy stage types
                                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                             "The root name of the stat (e.g., '2021' - Damage taken)\n"
                                             "or achievement (e.g., '5242902' - The End?) that triggers this stage's completion.");
                                } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                                    // mid-era stage types
                                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                             "The root name of the stat (e.g., 'stat.craftItem.minecraft.stick')\n"
                                             "or achievement (e.g., 'achievement.ghast') or criterion (e.g., 'Extreme Hills+ M')\n"
                                             "that triggers this stage's completion.");
                                } else if (creator_selected_version <= MC_VERSION_1_12_2) {
                                    // Mid-era stats, but modern advancements
                                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                             "The root name of the stat (e.g., 'stat.sneakTime'),\n"
                                             "advancement (e.g., 'minecraft:story/iron_tools')\n"
                                             "or criterion (e.g., 'cookie') that triggers this stage's completion.");
                                } else if (creator_selected_version == MC_VERSION_25W14CRAFTMINE) {
                                    // craftmine stage types
                                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                             "The root name of the stat (e.g., 'minecraft:killed_by/minecraft:ravager'),\n"
                                             "advancement (e.g., 'minecraft:mines/special_mine_completed'),\n"
                                             "unlock (e.g., 'minecraft:fire_wand') or criterion (e.g., 'minecraft:runemaster')\n"
                                             "that triggers this stage's completion.");
                                } else {
                                    // modern stage types without craftmine (1.13+)
                                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                             "The root name of the stat (e.g., 'minecraft:used/minecraft:acacia_boat'),\n"
                                             "advancement (e.g., 'minecraft:adventure/trim_with_all_exclusive_armor_patterns')\n"
                                             "or criterion (e.g., 'minecraft:lush_caves') that triggers this stage's completion.");
                                }
                                ImGui::SetTooltip("%s", tooltip_buffer);
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("Import##StageTrigger")) {
                                // Reset state for a new single-select import
                                current_import_mode = SINGLE_SELECT_STAGE; // One item selectable on import
                                stage_to_edit = &stage; // Remember which stage we're editing
                                import_search_buffer[0] = '\0';
                                importable_advancements.clear();
                                importable_stats.clear();
                                importable_unlocks.clear();

                                // Prepare to open the correct file dialog and popup
                                char start_path[MAX_PATH_LENGTH];
                                const char *selection = nullptr;

                                switch (stage.type) {
                                    case SUBGOAL_STAT: {
                                        if (creator_selected_version <= MC_VERSION_1_6_4) {
                                            if (app_settings->using_stats_per_world_legacy) {
                                                snprintf(start_path, sizeof(start_path), "%s/%s/stats/", t->saves_path,
                                                         t->world_name);
                                            } else if (get_parent_directory(
                                                t->saves_path, start_path, sizeof(start_path), 1)) {
                                                // Use a temporary buffer to prevent source/destination overlap
                                                char temp_parent_dir[MAX_PATH_LENGTH];
                                                strncpy(temp_parent_dir, start_path, sizeof(temp_parent_dir));
                                                temp_parent_dir[sizeof(temp_parent_dir) - 1] = '\0';
                                                snprintf(start_path, sizeof(start_path), "%s/stats/", temp_parent_dir);
                                            }
                                        } else {
                                            // Mid-era (1.7.2 - 1.12.2) AND Modern (1.13+)
                                            snprintf(start_path, sizeof(start_path), "%s/%s/stats/", t->saves_path,
                                                     t->world_name);
                                        }
#ifdef __APPLE__
                                        const char *json_filter[2] = {"*.json", "public.json"};
                                        const char *dat_filter[2] = {"*.dat", "public.data"};
                                        int filter_count = 2;
#else
                                        const char *json_filter[] = {"*.json"};
                                        const char *dat_filter[] = {"*.dat"};
                                        int filter_count = 1;
#endif
                                        selection = tinyfd_openFileDialog(
                                            "Select Player Stats File", start_path, filter_count,
                                            (creator_selected_version <= MC_VERSION_1_6_4) ? dat_filter : json_filter,
                                            (creator_selected_version <= MC_VERSION_1_6_4) ? "DAT files" : "JSON files",
                                            0);
                                        if (selection && parse_player_stats_for_import(
                                                selection, creator_selected_version, importable_stats,
                                                import_error_message, sizeof(import_error_message))) {
                                            show_import_stats_popup = true;
                                        }
                                        break;
                                    }
                                    case SUBGOAL_ADVANCEMENT:
                                    case SUBGOAL_CRITERION: {
                                        if (creator_selected_version <= MC_VERSION_1_6_4) {
                                            if (app_settings->using_stats_per_world_legacy) {
                                                snprintf(start_path, sizeof(start_path), "%s/%s/stats/", t->saves_path,
                                                         t->world_name);
                                            } else if (get_parent_directory(
                                                t->saves_path, start_path, sizeof(start_path), 1)) {
                                                // Use a temporary buffer to prevent source/destination overlap
                                                char temp_parent_dir[MAX_PATH_LENGTH];
                                                strncpy(temp_parent_dir, start_path, sizeof(temp_parent_dir));
                                                temp_parent_dir[sizeof(temp_parent_dir) - 1] = '\0';
                                                snprintf(start_path, sizeof(start_path), "%s/stats/", temp_parent_dir);
                                            }
                                        } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                                            // Mid-era (1.7.2 - 1.11.2)
                                            snprintf(start_path, sizeof(start_path), "%s/%s/stats/", t->saves_path,
                                                     t->world_name);
                                        } else {
                                            // Criterion are 1.12+
                                            snprintf(start_path, sizeof(start_path), "%s/%s/advancements/",
                                                     t->saves_path, t->world_name);
                                        }
#ifdef __APPLE__
                                        const char *json_filter[2] = {"*.json", "public.json"};
                                        const char *dat_filter[2] = {"*.dat", "public.data"};
                                        int filter_count = 2;
#else
                                        const char *json_filter[] = {"*.json"};
                                        const char *dat_filter[] = {"*.dat"};
                                        int filter_count = 1;
#endif
                                        selection = tinyfd_openFileDialog(
                                            "Select Player File", start_path, filter_count,
                                            (creator_selected_version <= MC_VERSION_1_6_4) ? dat_filter : json_filter,
                                            (creator_selected_version <= MC_VERSION_1_6_4) ? "DAT files" : "JSON files",
                                            0);
                                        if (selection && parse_player_advancements_for_import(
                                                selection, creator_selected_version, importable_advancements,
                                                import_error_message, sizeof(import_error_message))) {
                                            show_import_advancements_popup = true;
                                            // Set search mode based on the type of trigger being imported
                                            if (stage_to_edit->type == SUBGOAL_ADVANCEMENT) {
                                                import_search_criteria_only = false; // Default to advancement search
                                            } else if (stage_to_edit->type == SUBGOAL_CRITERION) {
                                                import_search_criteria_only = true; // Default to criterion search
                                            }
                                        }
                                        break;
                                    }
                                    case SUBGOAL_UNLOCK: {
                                        snprintf(start_path, sizeof(start_path), "%s/%s/unlocks/", t->saves_path,
                                                 t->world_name);
#ifdef __APPLE__
                                        const char *filter_patterns[2] = {"*.json", "public.json"};
                                        int filter_count = 2;
#else
                                        const char *filter_patterns[1] = {"*.json"};
                                        int filter_count = 1;
#endif
                                        selection = tinyfd_openFileDialog(
                                            "Select Player Unlocks File", start_path, filter_count, filter_patterns, "JSON files",
                                            0);
                                        if (selection && parse_player_unlocks_for_import(
                                                selection, importable_unlocks, import_error_message,
                                                sizeof(import_error_message))) {
                                            show_import_unlocks_popup = true;
                                        }
                                        break;
                                    }
                                    default:
                                        break;
                                }
                                if (selection && !show_import_stats_popup && !show_import_advancements_popup && !
                                    show_import_unlocks_popup) {
                                    save_message_type = MSG_ERROR;
                                    strncpy(status_message, import_error_message, sizeof(status_message) - 1);
                                }
                            }

                            // Import Stage Tooltip
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buffer[512];
                                switch (stage.type) {
                                    case SUBGOAL_STAT:
                                        if (creator_selected_version <= MC_VERSION_1_6_4) {
                                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                                     "Select a single stat or achievement from a\n"
                                                     "player's .dat file to use as a trigger.");
                                        } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                                     "Select a single stat or achievement from a\n"
                                                     "player's .json file to use as a trigger.");
                                        } else {
                                            // mid-era stats and modern achievements (1.12+) and modern (1.13+)
                                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                                     "Select a single stat from a\n"
                                                     "player's .json file to use as a trigger.");
                                        }
                                        break;
                                    case SUBGOAL_ADVANCEMENT:
                                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                                 "Select a single advancement or recipe from a\n"
                                                 "player's .json file to use as a trigger.");
                                        break;
                                    case SUBGOAL_CRITERION:
                                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                                 "Select a single criterion from a player's file.\n"
                                                 "The parent and criterion fields will be filled in automatically.");
                                        break;
                                    case SUBGOAL_UNLOCK:
                                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                                 "Select a single unlock from a\n"
                                                 "player's .json file to use as a trigger.");
                                        break;
                                    default:
                                        // This case should not be reachable as the button is not rendered for SUBGOAL_MANUAL.
                                        strncpy(tooltip_buffer, "Import a value from a player file.",
                                                sizeof(tooltip_buffer) - 1);
                                        tooltip_buffer[sizeof(tooltip_buffer) - 1] = '\0';
                                        break;
                                }
                                ImGui::SetTooltip("%s", tooltip_buffer);
                            }

                            // Only show target value for stat/achievements
                            if (stage.type == SUBGOAL_STAT) {
                                if (ImGui::InputInt("Target Value", &stage.required_progress)) {
                                    if (stage.required_progress < 1) {
                                        stage.required_progress = 1; // Clamp to minimum of 1
                                    }
                                    ms_goal_data_changed = true;
                                    save_message_type = MSG_NONE;
                                }
                                if (ImGui::IsItemHovered()) {
                                    char tooltip_buffer[256];
                                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                             "For 'Stat' type stages, this is the value the stat must reach to complete the stage.\n"
                                             "Must be 1 or greater.");
                                    ImGui::SetTooltip("%s", tooltip_buffer);
                                }
                            }
                        }

                        if (ImGui::Button("Copy")) {
                            stage_to_copy = j;
                            ms_goal_data_changed = true;
                            save_message_type = MSG_NONE;
                        }

                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[128];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Duplicate Stage:\n%s", stage.stage_id);
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }

                        ImGui::SameLine();

                        if (ImGui::Button("Remove")) {
                            stage_to_remove = j;
                            ms_goal_data_changed = true;
                            save_message_type = MSG_NONE;
                        }

                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[128];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Remove Stage:\n%s", stage.stage_id);
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }

                        ImGui::EndGroup();

                        // To make a non-interactive item a drag source we use the flag
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            ImGui::SetDragDropPayload("MS_STAGE_DND", &j, sizeof(int));
                            ImGui::Text("Reorder Stage: %s", stage.stage_id);
                            ImGui::EndDragDropSource();
                        }

                        ImGui::PopID();
                    }

                    // Final drop target for the end of the list
                    ImGui::InvisibleButton("final_drop_target_stage", ImVec2(-1, 8.0f)); // Added larger drop zone
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MS_STAGE_DND")) {
                            stage_dnd_source_index = *(const int *) payload->Data;
                            stage_dnd_target_index = goal.stages.size();
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // Handle stage reordering after the loop
                    if (stage_dnd_source_index != -1 && stage_dnd_target_index != -1 && stage_dnd_source_index !=
                        stage_dnd_target_index) {
                        EditorSubGoal item_to_move = goal.stages[stage_dnd_source_index];
                        goal.stages.erase(goal.stages.begin() + stage_dnd_source_index);
                        if (stage_dnd_target_index > stage_dnd_source_index) stage_dnd_target_index--;
                        goal.stages.insert(goal.stages.begin() + stage_dnd_target_index, item_to_move);
                        ms_goal_data_changed = true;
                        save_message_type = MSG_NONE;
                    }

                    if (stage_to_remove != -1) {
                        goal.stages.erase(goal.stages.begin() + stage_to_remove);
                        ms_goal_data_changed = true;
                        save_message_type = MSG_NONE;
                    }

                    // Logic to handle stage copy action after the loop
                    if (stage_to_copy != -1) {
                        const auto &source_stage = goal.stages[stage_to_copy];

                        // Perform a manual, safe copy.
                        EditorSubGoal new_stage;
                        strncpy(new_stage.stage_id, source_stage.stage_id, sizeof(new_stage.stage_id));
                        new_stage.stage_id[sizeof(new_stage.stage_id) - 1] = '\0';
                        strncpy(new_stage.display_text, source_stage.display_text, sizeof(new_stage.display_text));
                        new_stage.display_text[sizeof(new_stage.display_text) - 1] = '\0';
                        strncpy(new_stage.icon_path, source_stage.icon_path, sizeof(new_stage.icon_path));
                        new_stage.icon_path[sizeof(new_stage.icon_path) - 1] = '\0';
                        strncpy(new_stage.parent_advancement, source_stage.parent_advancement,
                                sizeof(new_stage.parent_advancement));
                        new_stage.parent_advancement[sizeof(new_stage.parent_advancement) - 1] = '\0';
                        strncpy(new_stage.root_name, source_stage.root_name, sizeof(new_stage.root_name));
                        new_stage.root_name[sizeof(new_stage.root_name) - 1] = '\0';
                        new_stage.type = source_stage.type;
                        new_stage.required_progress = source_stage.required_progress;

                        // If the source was the Final (SUBGOAL_MANUAL) stage, the copy cannot also be Final
                        // Reset it to SUBGOAL_STAT
                        if (new_stage.type == SUBGOAL_MANUAL) {
                            new_stage.type = SUBGOAL_STAT;
                            new_stage.required_progress = 1; // sensible default for a stat stage
                        }

                        char base_name[64];
                        strncpy(base_name, source_stage.stage_id, sizeof(base_name) - 1);
                        base_name[sizeof(base_name) - 1] = '\0';
                        char new_id[64];
                        int copy_counter = 1;
                        while (true) {
                            if (copy_counter == 1) snprintf(new_id, sizeof(new_id), "%s_copy", base_name);
                            else snprintf(new_id, sizeof(new_id), "%s_copy%d", base_name, copy_counter);
                            bool id_exists = false;
                            for (const auto &s: goal.stages) {
                                if (strcmp(s.stage_id, new_id) == 0) {
                                    id_exists = true;
                                    break;
                                }
                            }
                            if (!id_exists) break;
                            copy_counter++;
                        }
                        strncpy(new_stage.stage_id, new_id, sizeof(new_stage.stage_id) - 1);
                        new_stage.stage_id[sizeof(new_stage.stage_id) - 1] = '\0';
                        goal.stages.insert(goal.stages.begin() + stage_to_copy + 1, new_stage);
                        ms_goal_data_changed = true;
                        save_message_type = MSG_NONE;
                    }
                } else {
                    ImGui::Text("Select a Multi-Stage Goal from the list to edit its details.");
                }
                ImGui::EndChild();

                // Call the synchronization function at the end of the tab if changes were made
                // Properly synchronize hidden legacy multi-stage goal stats
                if (ms_goal_data_changed && creator_selected_version <= MC_VERSION_1_6_4) {
                    synchronize_legacy_ms_goal_stats(current_template_data);
                }

                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopID();
    } // End of editing_template

    // "Create New" Form
    else if (show_create_new_view) {
        ImGui::Text("Create a New Template for %s", creator_version_str);
        ImGui::Spacing();

        ImGui::InputText("Category Name", new_template_category, sizeof(new_template_category));
        if (ImGui::IsItemHovered()) {
            char template_category_name_tooltip_buffer[1024];
            snprintf(template_category_name_tooltip_buffer, sizeof(template_category_name_tooltip_buffer),
                     "The main classification for the template (e.g., 'all_advancements', 'all_trims').\n"
                     "Cannot contain spaces or special characters besides the %% sign.");
            ImGui::SetTooltip("%s", template_category_name_tooltip_buffer);
        }

        ImGui::InputText("Optional Flag", new_template_flag, sizeof(new_template_flag));
        if (ImGui::IsItemHovered()) {
            char optional_flag_tooltip_buffer[1024];
            snprintf(optional_flag_tooltip_buffer, sizeof(optional_flag_tooltip_buffer),
                     "A variant for the category (e.g., '_optimized', '_modded').\n"
                     "The optional flag immediately follows the category name\n"
                     "so it best practice to start with an underscore.\n"
                     "Cannot contain spaces or special characters besides the %% sign.");
            ImGui::SetTooltip("%s", optional_flag_tooltip_buffer);
        }

        // Also allow enter key ONLY WHEN the window is focused
        if (ImGui::Button("Create Template") || (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused(
                                                     ImGuiFocusedFlags_RootAndChildWindows))) {
            if (creator_version_idx >= 0) {
                char error_msg[256] = "";

                if (validate_and_create_template(creator_version_str, new_template_category, new_template_flag,
                                                 error_msg,
                                                 sizeof(error_msg))) {
                    show_create_new_view = false;
                    // Force a rescan by clearing the last scanned version
                    SDL_SetAtomicInt(&g_templates_changed, 1); // Signal change
                    last_scanned_version[0] = '\0';
                } else {
                    strncpy(status_message, error_msg, sizeof(status_message) - 1);
                    status_message[sizeof(status_message) - 1] = '\0';
                }
            } else {
                strncpy(status_message, "Error: A version must be selected.", sizeof(status_message) - 1);
                status_message[sizeof(status_message) - 1] = '\0';
            }
        }
        if (ImGui::IsItemHovered()) {
            char create_files_tooltip_buffer[256];
            snprintf(create_files_tooltip_buffer, sizeof(create_files_tooltip_buffer),
                     "Create the template and language files on disk.\nYou can also press ENTER.");
            ImGui::SetTooltip("%s", create_files_tooltip_buffer);
        }
        // Display status/error message
        if (status_message[0] != '\0') {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", status_message);
        }
    }

    // "Copy Template" Form
    else if (show_copy_view) {
        ImGui::Text("Copy Template");

        ImGui::Spacing();

        if (selected_template_index != -1) {
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            ImGui::Text("Copying from: %s", selected.category);
        }

        ImGui::Combo("New Template Version", &copy_template_version_idx, VERSION_STRINGS, VERSION_STRINGS_COUNT);
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Select the destination version for the new template.\n"
                     "This version influences certain functionality of the template\n"
                     "and how the tracker reads the game files.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::InputText("New Category Name", copy_template_category, sizeof(copy_template_category));
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "The main classification for the new template.\n"
                     "Cannot contain spaces or special characters except for underscores, dots, and the %% sign.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::InputText("New Optional Flag", copy_template_flag, sizeof(copy_template_flag));
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "A variant for the new category (e.g., '_optimized').\n"
                     "The optional flag immediately follows the category name\n"
                     "so it best practice to start with an underscore.\n"
                     "Cannot contain spaces or special characters except for underscores, dots, and the %% sign.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }

        // Allowing enter key to confirm copy WHEN the window is focused
        if (ImGui::Button("Confirm Copy") || (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused(
                                                  ImGuiFocusedFlags_RootAndChildWindows))) {
            if (selected_template_index != -1 && copy_template_version_idx >= 0) {
                const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
                const char *dest_version = VERSION_STRINGS[copy_template_version_idx];
                char error_msg[256] = "";

                // Properly copy the template selected in the template creator
                if (copy_template_files(creator_version_str, selected.category, selected.optional_flag,
                                        dest_version, copy_template_category, copy_template_flag,
                                        error_msg, sizeof(error_msg))) {
                    status_message[0] = '\0'; // Ensure status message is clear
                    show_copy_view = false;
                    SDL_SetAtomicInt(&g_templates_changed, 1); // Signal change
                    last_scanned_version[0] = '\0'; // Force rescan
                } else {
                    strncpy(status_message, error_msg, sizeof(status_message) - 1);
                    status_message[sizeof(status_message) - 1] = '\0';
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            char confirm_copy_tooltip_buffer[256];
            snprintf(confirm_copy_tooltip_buffer, sizeof(confirm_copy_tooltip_buffer),
                     "Create a copy of the selected template with the new name.\nYou can also press ENTER.");
            ImGui::SetTooltip("%s", confirm_copy_tooltip_buffer);
        }
        // Display status/error message
        if (status_message[0] != '\0') {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", status_message);
        }
    }

    // Import Confirmation View
    else if (show_import_confirmation_view) {
        ImGui::Text("Confirm Import");
        ImGui::Separator();
        ImGui::TextWrapped("Importing from: %s", import_zip_path);
        ImGui::Spacing();

        ImGui::Text("Please confirm or edit the details for the new template:");

        ImGui::Combo("Template Version", &import_version_idx, VERSION_STRINGS, VERSION_STRINGS_COUNT);
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Select the destination version for the new template.\n"
                     "This version influences certain functionality of the template\n"
                     "and how the tracker reads the game files.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::InputText("Category Name", import_category, sizeof(import_category));
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "The main classification for the new template.\n"
                     "Cannot contain spaces or special characters except for underscores, dots, and the %% sign.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::InputText("Optional Flag", import_flag, sizeof(import_flag));
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "A variant for the new category (e.g., '_optimized').\n"
                     "The optional flag immediately follows the category name\n"
                     "so it best practice to start with an underscore.\n"
                     "Cannot contain spaces or special characters except for underscores, dots, and the %% sign.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::Spacing();

        if (ImGui::Button("Confirm Import") || (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused())) {
            if (import_version_idx != -1) {
                const char *version_str = VERSION_STRINGS[import_version_idx];
                MC_Version version_enum = settings_get_version_from_string(version_str);

                char combined_name[MAX_PATH_LENGTH * 2];
                snprintf(combined_name, sizeof(combined_name), "%s%s", import_category, import_flag);
                // Add validation before executing the import
                if (import_category[0] == '\0') {
                    snprintf(status_message, sizeof(status_message), "Error: Category name cannot be empty.");
                    save_message_type = MSG_ERROR;
                } else if (!is_valid_filename_part_for_ui(import_category)) {
                    snprintf(status_message, sizeof(status_message), "Error: Category contains invalid characters.");
                    save_message_type = MSG_ERROR;
                } else if (!is_valid_filename_part_for_ui(import_flag)) {
                    snprintf(status_message, sizeof(status_message), "Error: Flag contains invalid characters.");
                    save_message_type = MSG_ERROR;
                } else if (version_enum <= MC_VERSION_1_6_4 && ends_with(combined_name, "_snapshot")) {
                    // BLOCKING _snapshot for legacy versions
                    snprintf(status_message, sizeof(status_message),
                             "Error: Template name cannot end with '_snapshot' for legacy versions.");
                    save_message_type = MSG_ERROR;
                } else {
                    if (execute_import_from_zip(import_zip_path, version_str, import_category, import_flag,
                                                status_message,
                                                sizeof(status_message))) {
                        // SUCCESS!
                        snprintf(status_message, sizeof(status_message), "Template imported to version %s!",
                                 version_str);
                        show_import_confirmation_view = false;
                        // Switch UI to the newly imported version
                        strncpy(creator_version_str, version_str, sizeof(creator_version_str) - 1);
                        creator_version_str[sizeof(creator_version_str) - 1] = '\0';
                        creator_version_idx = import_version_idx;
                        SDL_SetAtomicInt(&g_templates_changed, 1);
                        last_scanned_version[0] = '\0'; // Force rescan
                    }
                    save_message_type = MSG_ERROR; // Show status message (will be an error on failure)
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "You can also press ENTER.\n"
                     "Confirms the import.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            show_import_confirmation_view = false;
            status_message[0] = '\0';
        }
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Cancels the import.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }

        if (status_message[0] != '\0' && save_message_type == MSG_ERROR) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", status_message);
        }
    } else {
        // If no template is selected
        if (selected_template_index == -1) {
            ImGui::TextDisabled("Create a new template or select one from the list to begin.");
        }
    }

    if (show_create_lang_popup) ImGui::OpenPopup("Create New Language");
    if (ImGui::BeginPopupModal("Create New Language", &show_create_lang_popup, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char popup_error_msg[256] = "";
        const auto &selected = discovered_templates[selected_template_index];
        ImGui::Text("Create new language for '%s%s'", selected.category, selected.optional_flag);
        ImGui::InputText("New Language Flag", lang_flag_buffer, sizeof(lang_flag_buffer));
        if (ImGui::IsItemHovered()) {
            char create_language_tooltip_buffer[1024];
            snprintf(create_language_tooltip_buffer, sizeof(create_language_tooltip_buffer),
                     "E.g., 'de', 'fr_ca'. Cannot be empty or contain special characters besides the %% sign.");
            ImGui::SetTooltip("%s", create_language_tooltip_buffer);
        }

        if (popup_error_msg[0] != '\0') {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", popup_error_msg);
        }

        if (ImGui::Button("Create", ImVec2(120, 0)) || (
                !ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
            popup_error_msg[0] = '\0';
            if (validate_and_create_lang_file(creator_version_str, selected.category, selected.optional_flag,
                                              lang_flag_buffer, popup_error_msg, sizeof(popup_error_msg))) {
                SDL_SetAtomicInt(&g_templates_changed, 1);
                last_scanned_version[0] = '\0';
                ImGui::CloseCurrentPopup();
                show_create_lang_popup = false;
            }
        }
        // Confirm Hover text
        if (ImGui::IsItemHovered()) {
            char press_enter_confirm_tooltip_buffer[1024];
            snprintf(press_enter_confirm_tooltip_buffer, sizeof(press_enter_confirm_tooltip_buffer),
                     "Press ENTER to confirm.");
            ImGui::SetTooltip("%s", press_enter_confirm_tooltip_buffer);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            popup_error_msg[0] = '\0';
            ImGui::CloseCurrentPopup();
            show_create_lang_popup = false;
        }
        // Cancel hover text
        if (ImGui::IsItemHovered()) {
            char press_esc_cancel_tooltip_buffer[1024];
            snprintf(press_esc_cancel_tooltip_buffer, sizeof(press_esc_cancel_tooltip_buffer),
                     "Press ESC to cancel.");
            ImGui::SetTooltip("%s", press_esc_cancel_tooltip_buffer);
        }
        ImGui::EndPopup();
    }

    if (show_copy_lang_popup) ImGui::OpenPopup("Copy Language");
    if (ImGui::BeginPopupModal("Copy Language", &show_copy_lang_popup, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char popup_error_msg[256] = "";
        static bool show_fallback_warning = false;

        const auto &selected = discovered_templates[selected_template_index];
        ImGui::Text("Copy language '%s' to a new flag.",
                    lang_to_copy_from.empty() ? "Default" : lang_to_copy_from.c_str());

        // Disable input and show warning after a successful fallback
        ImGui::BeginDisabled(show_fallback_warning);
        ImGui::InputText("New Language Flag", lang_flag_buffer, sizeof(lang_flag_buffer));
        ImGui::EndDisabled();

        if (popup_error_msg[0] != '\0') {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", popup_error_msg);
        }
        if (show_fallback_warning) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                               "Warning: Source was empty. Copied from Default instead.");
        }

        if (ImGui::Button("Copy", ImVec2(120, 0)) || (!ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
            if (show_fallback_warning) {
                // This is the second OK click, just close the popup
                ImGui::CloseCurrentPopup();
                show_copy_lang_popup = false;
                show_fallback_warning = false; // Reset state
            } else {
                // This is the first OK click, attempt the copy
                popup_error_msg[0] = '\0'; // Clear previous errors
                CopyLangResult result = copy_lang_file(creator_version_str, selected.category, selected.optional_flag,
                                                       lang_to_copy_from.c_str(), lang_flag_buffer, popup_error_msg,
                                                       sizeof(popup_error_msg));

                if (result == COPY_LANG_SUCCESS_DIRECT) {
                    SDL_SetAtomicInt(&g_templates_changed, 1);
                    last_scanned_version[0] = '\0';
                    ImGui::CloseCurrentPopup();
                    show_copy_lang_popup = false;
                } else if (result == COPY_LANG_SUCCESS_FALLBACK) {
                    SDL_SetAtomicInt(&g_templates_changed, 1);
                    last_scanned_version[0] = '\0';
                    show_fallback_warning = true; // Show the warning and wait for another OK
                }
                // On COPY_LANG_FAIL, the error message is set and the popup remains open
            }
        }
        // Cancel hover text
        if (ImGui::IsItemHovered()) {
            char press_enter_confirm_tooltip_buffer[1024];
            snprintf(press_enter_confirm_tooltip_buffer, sizeof(press_enter_confirm_tooltip_buffer),
                     "Press ENTER to confirm.");
            ImGui::SetTooltip("%s", press_enter_confirm_tooltip_buffer);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            popup_error_msg[0] = '\0';
            show_fallback_warning = false; // Reset state
            ImGui::CloseCurrentPopup();
            show_copy_lang_popup = false;
        }
        // Cancel hover text
        if (ImGui::IsItemHovered()) {
            char press_esc_cancel_tooltip_buffer[1024];
            snprintf(press_esc_cancel_tooltip_buffer, sizeof(press_esc_cancel_tooltip_buffer),
                     "Press ESC to cancel.");
            ImGui::SetTooltip("%s", press_esc_cancel_tooltip_buffer);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Delete Language?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto &selected = discovered_templates[selected_template_index];
        const auto &lang_to_delete = selected.available_lang_flags[selected_lang_index];
        ImGui::Text("Are you sure you want to delete the '%s' language file?", lang_to_delete.c_str());
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0)) || (
                !ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
            char error_msg[256];
            if (delete_lang_file(creator_version_str, selected.category, selected.optional_flag, lang_to_delete.c_str(),
                                 error_msg, sizeof(error_msg))) {
                SDL_SetAtomicInt(&g_templates_changed, 1);
                last_scanned_version[0] = '\0';
                selected_lang_index = -1;
            } else {
                strncpy(status_message, error_msg, sizeof(status_message) - 1);
                status_message[sizeof(status_message) - 1] = '\0';
            }
            ImGui::CloseCurrentPopup();
        }
        // Cancel hover text
        if (ImGui::IsItemHovered()) {
            char press_enter_confirm_tooltip_buffer[1024];
            snprintf(press_enter_confirm_tooltip_buffer, sizeof(press_enter_confirm_tooltip_buffer),
                     "Press ENTER to confirm.");
            ImGui::SetTooltip("%s", press_enter_confirm_tooltip_buffer);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        // Cancel hover text
        if (ImGui::IsItemHovered()) {
            char press_esc_cancel_tooltip_buffer[1024];
            snprintf(press_esc_cancel_tooltip_buffer, sizeof(press_esc_cancel_tooltip_buffer),
                     "Press ESC to cancel.");
            ImGui::SetTooltip("%s", press_esc_cancel_tooltip_buffer);
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    // Import Advancement Popup Logic
    const char *import_popup_title = (current_advancement_import_mode == CRITERIA_ONLY_IMPORT)
                                         ? ((creator_selected_version <= MC_VERSION_1_11_2)
                                                ? "Import Achievement Criteria"
                                                : "Import Advancement Criteria")
                                         : ((creator_selected_version <= MC_VERSION_1_11_2)
                                                ? "Import Achievements from File"
                                                : "Import Advancements from File");

    if (show_import_advancements_popup) {
        ImGui::OpenPopup(import_popup_title);
    }
    if (ImGui::BeginPopupModal(import_popup_title, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        // Hotkey logic for search bar
        if ((ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_LeftSuper)) &&
            ImGui::IsKeyPressed(ImGuiKey_F)) {
            focus_import_search = true;
        }

        // --- Create a filtered list of advancements to display ---
        std::vector<ImportableAdvancement *> filtered_advancements;
        if (current_advancement_import_mode == CRITERIA_ONLY_IMPORT && selected_advancement) {
            // In criteria mode, find and show only the currently edited advancement
            for (auto &adv: importable_advancements) {
                if (adv.root_name == selected_advancement->root_name) {
                    filtered_advancements.push_back(&adv);
                    break;
                }
            }
            if (filtered_advancements.empty()) {
                snprintf(import_error_message, sizeof(import_error_message),
                         "Error: %s '%s' not found in the selected file.", advancements_label_upper,
                         selected_advancement->root_name);
            }
        } else {
            // BATCH_ADVANCEMENT_IMPORT mode
            if (import_search_buffer[0] != '\0') {
                if (import_search_criteria_only) {
                    for (auto &adv: importable_advancements) {
                        bool child_match = false;
                        for (const auto &crit: adv.criteria) {
                            if (str_contains_insensitive(crit.root_name.c_str(), import_search_buffer)) {
                                child_match = true;
                                break;
                            }
                        }
                        if (child_match) filtered_advancements.push_back(&adv);
                    }
                } else {
                    for (auto &adv: importable_advancements) {
                        if (str_contains_insensitive(adv.root_name.c_str(), import_search_buffer)) {
                            filtered_advancements.push_back(&adv);
                        }
                    }
                }
            } else {
                for (auto &adv: importable_advancements) {
                    filtered_advancements.push_back(&adv);
                }
            }
        }

        // --- Selection Controls & Search Bar (now operating on the filtered list) ---
        // Only show batch controls when not doing a single selection for a multi-stage goal
        if (current_import_mode == BATCH_IMPORT) {
            // Selecting multiple, not multi-stage goal stage
            if (ImGui::Button("Select All")) {
                for (auto *adv_ptr: filtered_advancements) {
                    // In criteria-only mode, we don't select the parent, only its children.
                    if (current_advancement_import_mode != CRITERIA_ONLY_IMPORT) {
                        adv_ptr->is_selected = true;
                    }
                    if (import_select_criteria) {
                        bool parent_matched =
                                str_contains_insensitive(adv_ptr->root_name.c_str(), import_search_buffer);
                        for (auto &crit: adv_ptr->criteria) {
                            // Select criteria only if they are visible (parent matched or they matched themselves)
                            if (import_search_buffer[0] == '\0' || parent_matched || str_contains_insensitive(
                                    crit.root_name.c_str(), import_search_buffer)) {
                                crit.is_selected = true;
                            }
                        }
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                char select_all_tooltip_buffer[512];
                if (creator_selected_version <= MC_VERSION_1_6_4) {
                    // Legacy
                    snprintf(select_all_tooltip_buffer, sizeof(select_all_tooltip_buffer),
                             "Selects all achievements visible in the current search.\n\n"
                             "You can also Shift+Click to select a range of items.");
                } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                    // Mid-era
                    snprintf(select_all_tooltip_buffer, sizeof(select_all_tooltip_buffer),
                             "Selects all achievements visible in the current search.\n"
                             "Also selects their criteria if 'Include Criteria' is checked.\n\n"
                             "You can also Shift+Click to select a range of items.");
                } else {
                    // Modern
                    snprintf(select_all_tooltip_buffer, sizeof(select_all_tooltip_buffer),
                             "Selects all advancements/recipes visible in the current search.\n"
                             "Also selects their criteria if 'Include Criteria' is checked.\n\n"
                             "You can also Shift+Click to select a range of items.");
                }
                ImGui::SetTooltip("%s", select_all_tooltip_buffer);
            }
            ImGui::SameLine();
            if (ImGui::Button("Deselect All")) {
                for (auto *adv_ptr: filtered_advancements) {
                    if (import_select_criteria) {
                        // Deselect ALL criteria of the visible parents, leaving parents selected
                        for (auto &crit: adv_ptr->criteria) {
                            crit.is_selected = false;
                        }
                    } else {
                        // Deselect everything (parents and their criteria)
                        adv_ptr->is_selected = false;
                        for (auto &crit: adv_ptr->criteria) {
                            crit.is_selected = false;
                        }
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                char deselct_all_tooltip_buffer[512];
                if (creator_selected_version <= MC_VERSION_1_6_4) {
                    snprintf(deselct_all_tooltip_buffer, sizeof(deselct_all_tooltip_buffer),
                             "Deselects all achievements in the current search.\n\n"
                             "You can also Shift+Click to deselect a range of items.");
                } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                    // Mid-era
                    snprintf(deselct_all_tooltip_buffer, sizeof(deselct_all_tooltip_buffer),
                             "Deselects all achievements and criteria in the current search.\n\n"
                             "If 'Include Criteria' is checked, only the criteria are deselected,\n"
                             "leaving the parent achievements selected.\n\n"
                             "You can also Shift+Click to deselect a range of items.");
                } else {
                    // Modern
                    snprintf(deselct_all_tooltip_buffer, sizeof(deselct_all_tooltip_buffer),
                             "Deselects all advancements/recipes and criteria in the current search.\n\n"
                             "If 'Include Criteria' is checked, only the criteria are deselected,\n"
                             "leaving the parent advancements selected.\n\n"
                             "You can also Shift+Click to deselect a range of items.");
                }
                ImGui::SetTooltip("%s", deselct_all_tooltip_buffer);
            }
            // --- Include Criteria Checkbox ONLY for 1.7+ and not in criteria-only import mode ---
            if (creator_selected_version > MC_VERSION_1_6_4 &&
                current_advancement_import_mode != CRITERIA_ONLY_IMPORT) {
                ImGui::SameLine();
                ImGui::Checkbox("Include Crit.", &import_select_criteria);
                if (ImGui::IsItemHovered()) {
                    char include_criteria_tooltip_buffer[512];
                    snprintf(include_criteria_tooltip_buffer, sizeof(include_criteria_tooltip_buffer),
                             "Changes the behavior of the SELECTION tools.\n\n"
                             "CHECKED: 'Select/Deselect All' and Shift+Click affect criteria.\n\n"
                             "UNCHECKED: 'Select/Deselect All' and Shift+Click affect parent advancements only.");
                    ImGui::SetTooltip(
                        "%s", include_criteria_tooltip_buffer);
                }
            }
        } // End of batch import

        // Store the cursor position after drawing the left-aligned controls
        float left_controls_end_x = ImGui::GetCursorPosX();

        // --- Right-aligned Controls ---
        const float search_bar_width = 200.0f;
        const float clear_button_width = ImGui::GetFrameHeight();
        const char *search_scope_label = "Crit. Search";
        const float search_scope_checkbox_width = (creator_selected_version > MC_VERSION_1_6_4)
                                                      ? (ImGui::CalcTextSize(search_scope_label).x +
                                                         ImGui::GetFrameHeightWithSpacing())
                                                      : 0.0f;

        const float right_controls_width = search_bar_width + clear_button_width + search_scope_checkbox_width + (
                                               ImGui::GetStyle().ItemSpacing.x * 2);

        // --- Position and render right-aligned controls ---
        float right_controls_start_x = ImGui::GetWindowWidth() - right_controls_width -
                                       ImGui::GetStyle().WindowPadding.x;

        // Only move to the same line if there is enough space to avoid overlap
        if (right_controls_start_x > left_controls_end_x) {
            ImGui::SameLine(right_controls_start_x);
        }

        // Add the new checkbox for search scope
        if (creator_selected_version > MC_VERSION_1_6_4) {
            ImGui::Checkbox(search_scope_label, &import_search_criteria_only);
            if (ImGui::IsItemHovered()) {
                char tooltip[1024];
                snprintf(tooltip, sizeof(tooltip), "Changes the scope of the search bar.\n\n"
                         "CHECKED: Search only criterion names.\n"
                         "UNCHECKED: Search only parent names.");
                ImGui::SetTooltip("%s", tooltip);
            }
        } else {
            ImGui::Dummy(ImVec2(search_scope_checkbox_width, ImGui::GetFrameHeight()));
        }


        ImGui::SameLine();

        // Render the "X" button or a dummy widget
        if (import_search_buffer[0] != '\0') {
            if (ImGui::Button("X##ClearImportSearch", ImVec2(clear_button_width, 0))) {
                import_search_buffer[0] = '\0';
            }
        } else {
            ImGui::Dummy(ImVec2(clear_button_width, 0));
        }

        ImGui::SameLine();

        // Set focus and render the search bar
        ImGui::SetNextItemWidth(search_bar_width);
        if (focus_import_search) {
            ImGui::SetKeyboardFocusHere();
            focus_import_search = false;
        }

        // Import Advancements Search
        ImGui::InputTextWithHint("##ImportSearch", "Search...", import_search_buffer, sizeof(import_search_buffer));
        if (ImGui::IsItemHovered()) {
            char import_search_tooltip_buffer[1024];
            if (creator_selected_version <= MC_VERSION_1_6_4) {
                // No criteria
                snprintf(import_search_tooltip_buffer, sizeof(import_search_tooltip_buffer),
                         "Filter list by achievement root name (case-insensitive).\n"
                         "Use the checkbox to the left to toggle search scope.\n\n"
                         "Press Ctrl+F or Cmd+F to focus.");
            } else if (creator_selected_version <= MC_VERSION_1_11_2) {
                // Achievements with criteria
                snprintf(import_search_tooltip_buffer, sizeof(import_search_tooltip_buffer),
                         "Filter list by achievement or criterion root name (case-insensitive).\n"
                         "Use the checkbox to the left to toggle search scope.\n\n"
                         "Press Ctrl+F or Cmd+F to focus.");
            } else {
                // Advancements
                snprintf(import_search_tooltip_buffer, sizeof(import_search_tooltip_buffer),
                         "Filter list by advancement/recipe or criterion root name (case-insensitive).\n"
                         "Use the checkbox to the left to toggle search scope.\n\n"
                         "Press Ctrl+F or Cmd+F to focus.");
            }
            ImGui::SetTooltip("%s", import_search_tooltip_buffer);
        }
        ImGui::Separator();

        // --- Render List (using filtered list from search) ---
        if (importable_advancements.empty()) {
            ImGui::Text("No advancements found in the selected file.");
        } else {
            ImGui::BeginChild("ImporterScrollingRegion", ImVec2(600, 400), true);
            for (size_t i = 0; i < filtered_advancements.size(); ++i) {
                auto &adv = *filtered_advancements[i];
                ImGui::PushID(adv.root_name.c_str());

                // Determine if we are in the special criterion-select mode
                bool is_criterion_select_mode = (
                    current_import_mode == SINGLE_SELECT_STAGE && stage_to_edit->type == SUBGOAL_CRITERION);

                bool disable_parent_checkbox = is_criterion_select_mode || (
                                                   current_advancement_import_mode == CRITERIA_ONLY_IMPORT);
                if (disable_parent_checkbox) {
                    ImGui::BeginDisabled(); // Disable advancement checkbox
                }

                // All other modes the achievements/advancements checkbox is interactive
                if (ImGui::Checkbox(adv.root_name.c_str(), &adv.is_selected)) {
                    if (current_import_mode == SINGLE_SELECT_STAGE && stage_to_edit->type != SUBGOAL_CRITERION) {
                        if (adv.is_selected) {
                            for (auto &other_adv: importable_advancements) {
                                if (&other_adv != &adv) other_adv.is_selected = false;
                                for (auto &crit: other_adv.criteria) crit.is_selected = false;
                            }
                        }
                    } else if (current_import_mode == BATCH_IMPORT) {
                        if (ImGui::GetIO().KeyShift && last_clicked_adv_index != -1) {
                            int start = std::min((int) i, last_clicked_adv_index);
                            int end = std::max((int) i, last_clicked_adv_index);
                            for (int j = start; j <= end; ++j) {
                                auto &ranged_adv = *filtered_advancements[j];
                                ranged_adv.is_selected = adv.is_selected;
                                if (import_select_criteria) {
                                    for (auto &crit: ranged_adv.criteria) crit.is_selected = adv.is_selected;
                                }
                            }
                        }
                        last_clicked_adv_index = i;
                        last_clicked_crit_parent = nullptr;
                        last_clicked_crit_index = -1;
                        if (!adv.is_selected) {
                            for (auto &crit: adv.criteria) crit.is_selected = false;
                        }
                    }
                }
                if (disable_parent_checkbox) {
                    ImGui::EndDisabled();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (current_advancement_import_mode == CRITERIA_ONLY_IMPORT) {
                        char tooltip_buffer[256];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Only criteria can be selected in this mode.");
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    } else if (is_criterion_select_mode) {
                        char tooltip_buffer[256];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Select a specific criterion below.\n"
                                 "The parent will be selected automatically.");
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                }
                if (!adv.criteria.empty()) {
                    ImGui::Indent();
                    bool parent_matched = str_contains_insensitive(adv.root_name.c_str(), import_search_buffer);
                    for (size_t j = 0; j < adv.criteria.size(); ++j) {
                        auto &crit = adv.criteria[j];
                        if (import_search_buffer[0] != '\0' && !parent_matched && !str_contains_insensitive(
                                crit.root_name.c_str(), import_search_buffer)) {
                            continue;
                        }

                        // Determine if the criterion checkbox should be disabled
                        bool disable_crit_checkbox = (current_import_mode == SINGLE_SELECT_STAGE &&
                                                      stage_to_edit->type == SUBGOAL_ADVANCEMENT);

                        // Don't allow clicking criteria if the stage is a subgoal advancement
                        if (disable_crit_checkbox) {
                            ImGui::BeginDisabled();
                        }

                        if (ImGui::Checkbox(crit.root_name.c_str(), &crit.is_selected)) {
                            if (current_import_mode == SINGLE_SELECT_STAGE) {
                                if (crit.is_selected) {
                                    for (auto &other_adv: importable_advancements) {
                                        other_adv.is_selected = false;
                                        for (auto &other_crit: other_adv.criteria) {
                                            if (&other_crit != &crit) other_crit.is_selected = false;
                                        }
                                    }
                                    adv.is_selected = true;
                                }
                            } else if (current_import_mode == BATCH_IMPORT) {
                                if (crit.is_selected) adv.is_selected = true;
                                if (ImGui::GetIO().KeyShift && import_select_criteria && last_clicked_crit_parent == &
                                    adv && last_clicked_crit_index != -1) {
                                    int start = std::min((int) j, last_clicked_crit_index);
                                    int end = std::max((int) j, last_clicked_crit_index);
                                    for (int k = start; k <= end; ++k) {
                                        auto &ranged_crit = adv.criteria[k];
                                        if (import_search_buffer[0] == '\0' || parent_matched ||
                                            str_contains_insensitive(ranged_crit.root_name.c_str(),
                                                                     import_search_buffer)) {
                                            ranged_crit.is_selected = crit.is_selected;
                                        }
                                    }
                                }
                                last_clicked_adv_index = -1;
                                last_clicked_crit_parent = &adv;
                                last_clicked_crit_index = j;
                            }
                        }

                        // If only advancements is being import
                        if (disable_crit_checkbox) {
                            ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                char tooltip[256];
                                snprintf(tooltip, sizeof(tooltip), "Switch the stage's 'Type' to 'Criterion'\n"
                                         "to import a specific criterion.");
                                ImGui::SetTooltip("%s", tooltip);
                            }
                        }
                    }
                    ImGui::Unindent();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        if (import_error_message[0] != '\0') {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", import_error_message);
        }

        // --- Count selected items to display next to the buttons ---
        int selected_adv_count = 0;
        int selected_crit_count = 0;
        for (const auto &adv: importable_advancements) {
            if (adv.is_selected) selected_adv_count++;
            for (const auto &crit: adv.criteria) {
                if (crit.is_selected) selected_crit_count++;
            }
        }

        // --- Bottom Controls ---
        const char *confirm_text = (current_import_mode == SINGLE_SELECT_STAGE) ? "Select" : "Confirm Import";

        if (ImGui::Button(confirm_text, ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            if (selected_adv_count == 0 && selected_crit_count == 0) {
                snprintf(import_error_message, sizeof(import_error_message), "Error: No items selected for import.");
            } else if (current_advancement_import_mode == CRITERIA_ONLY_IMPORT) {
                // Importing Advancement/Achievement Criteria
                import_error_message[0] = '\0';
                bool has_duplicates = false;

                std::unordered_set<std::string> existing_crit_names;
                for (const auto &crit: selected_advancement->criteria) {
                    existing_crit_names.insert(crit.root_name);
                }

                ImportableAdvancement *source_adv = nullptr;
                if (!filtered_advancements.empty()) {
                    source_adv = filtered_advancements[0];
                }

                if (source_adv) {
                    for (const auto &new_crit: source_adv->criteria) {
                        if (new_crit.is_selected) {
                            if (existing_crit_names.count(new_crit.root_name)) {
                                snprintf(import_error_message, sizeof(import_error_message),
                                         "Error: Criterion '%s' already exists.", new_crit.root_name.c_str());
                                has_duplicates = true;
                                break;
                            }
                        }
                    }
                }

                if (!has_duplicates && source_adv) {
                    for (const auto &new_crit: source_adv->criteria) {
                        if (new_crit.is_selected) {
                            EditorTrackableItem imported_crit = {};
                            strncpy(imported_crit.root_name, new_crit.root_name.c_str(),
                                    sizeof(imported_crit.root_name) - 1);
                            imported_crit.root_name[sizeof(imported_crit.root_name) - 1] = '\0';
                            strncpy(imported_crit.display_name, new_crit.root_name.c_str(),
                                    sizeof(imported_crit.display_name) - 1);
                            imported_crit.display_name[sizeof(imported_crit.display_name) - 1] = '\0';
                            strncpy(imported_crit.icon_path, "blocks/placeholder.png",
                                    sizeof(imported_crit.icon_path) - 1);
                            imported_crit.icon_path[sizeof(imported_crit.icon_path) - 1] = '\0';
                            selected_advancement->criteria.push_back(imported_crit);
                        }
                    }
                    show_import_advancements_popup = false; // Close on success
                    ImGui::CloseCurrentPopup();
                }
            } else if (current_import_mode == SINGLE_SELECT_STAGE && stage_to_edit != nullptr) {
                // Part of a stage import
                ImportableAdvancement *selected_adv = nullptr;
                ImportableCriterion *selected_crit = nullptr;
                for (auto &adv: importable_advancements) {
                    if (adv.is_selected) selected_adv = &adv;
                    for (auto &crit: adv.criteria) {
                        if (crit.is_selected) selected_crit = &crit;
                    }
                }
                if (stage_to_edit->type == SUBGOAL_CRITERION && selected_crit != nullptr && selected_adv != nullptr) {
                    strncpy(stage_to_edit->parent_advancement, selected_adv->root_name.c_str(),
                            sizeof(stage_to_edit->parent_advancement) - 1);
                    stage_to_edit->parent_advancement[sizeof(stage_to_edit->parent_advancement) - 1] = '\0';
                    strncpy(stage_to_edit->root_name, selected_crit->root_name.c_str(),
                            sizeof(stage_to_edit->root_name) - 1);
                    stage_to_edit->root_name[sizeof(stage_to_edit->root_name) - 1] = '\0';
                    // Manually trigger sync if it's a legacy version
                    if (creator_selected_version <= MC_VERSION_1_6_4) {
                        synchronize_legacy_ms_goal_stats(current_template_data);
                    }
                } else if (stage_to_edit->type == SUBGOAL_ADVANCEMENT && selected_adv != nullptr) {
                    strncpy(stage_to_edit->root_name, selected_adv->root_name.c_str(),
                            sizeof(stage_to_edit->root_name) - 1);
                    stage_to_edit->root_name[sizeof(stage_to_edit->root_name) - 1] = '\0';
                    // Manually trigger sync if it's a legacy version
                    if (creator_selected_version <= MC_VERSION_1_6_4) {
                        synchronize_legacy_ms_goal_stats(current_template_data);
                    }
                }
                show_import_advancements_popup = false;
                ImGui::CloseCurrentPopup();
            } else {
                // Regular batch import
                import_error_message[0] = '\0'; // Clear previous error
                bool has_duplicates = false;

                // 1. Check for duplicate root names before importing
                std::unordered_set<std::string> existing_names;
                for (const auto &existing_adv: current_template_data.advancements) {
                    existing_names.insert(existing_adv.root_name);
                }
                for (const auto &new_adv: importable_advancements) {
                    if (new_adv.is_selected && existing_names.count(new_adv.root_name)) {
                        snprintf(import_error_message, sizeof(import_error_message),
                                 "Error: Advancement '%s' already exists in the template.", new_adv.root_name.c_str());
                        has_duplicates = true;
                        break;
                    }
                }

                // 2. If no duplicates, proceed with import
                if (!has_duplicates) {
                    for (const auto &new_adv: importable_advancements) {
                        if (new_adv.is_selected) {
                            EditorTrackableCategory imported_cat = {};
                            strncpy(imported_cat.root_name, new_adv.root_name.c_str(),
                                    sizeof(imported_cat.root_name) - 1);
                            imported_cat.root_name[sizeof(imported_cat.root_name) - 1] = '\0';
                            strncpy(imported_cat.display_name, new_adv.root_name.c_str(),
                                    sizeof(imported_cat.display_name) - 1); // Default display name
                            imported_cat.display_name[sizeof(imported_cat.display_name) - 1] = '\0';
                            strncpy(imported_cat.icon_path, "blocks/placeholder.png",
                                    sizeof(imported_cat.icon_path) - 1);
                            imported_cat.icon_path[sizeof(imported_cat.icon_path) - 1] = '\0';

                            // Rule C: Check for recipes
                            if (new_adv.root_name.find(":recipes/") != std::string::npos) {
                                imported_cat.is_recipe = true;
                            }

                            // Rules A & B: Determine if criteria should be added
                            bool is_simple = new_adv.is_done && new_adv.criteria.size() == 1;
                            if (!is_simple) {
                                for (const auto &new_crit: new_adv.criteria) {
                                    if (new_crit.is_selected) {
                                        EditorTrackableItem imported_crit = {};
                                        strncpy(imported_crit.root_name, new_crit.root_name.c_str(),
                                                sizeof(imported_crit.root_name) - 1);
                                        imported_crit.root_name[sizeof(imported_crit.root_name) - 1] = '\0';
                                        strncpy(imported_crit.display_name, new_crit.root_name.c_str(),
                                                sizeof(imported_crit.display_name) - 1);
                                        imported_crit.display_name[sizeof(imported_crit.display_name) - 1] = '\0';
                                        strncpy(imported_crit.icon_path, "blocks/placeholder.png",
                                                sizeof(imported_crit.icon_path) - 1);
                                        imported_crit.icon_path[sizeof(imported_crit.icon_path) - 1] = '\0';
                                        imported_cat.criteria.push_back(imported_crit);
                                    }
                                }
                            }
                            current_template_data.advancements.push_back(imported_cat);
                        }
                    }
                    show_import_advancements_popup = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!show_import_advancements_popup) {
                // If closed
                current_import_mode = BATCH_IMPORT;
                current_advancement_import_mode = BATCH_ADVANCEMENT_IMPORT;
                stage_to_edit = nullptr;
                import_search_buffer[0] = '\0'; // Clear search after import
            }
        }
        if (ImGui::IsItemHovered()) {
            char import_advancements_tooltip_buffer[512];
            snprintf(import_advancements_tooltip_buffer, sizeof(import_advancements_tooltip_buffer),
                     "Import selected items into the template.\n(You can also press ENTER)");
            ImGui::SetTooltip("%s", import_advancements_tooltip_buffer);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            show_import_advancements_popup = false;
            ImGui::CloseCurrentPopup(); // Close popup actually
            current_import_mode = BATCH_IMPORT;
            current_advancement_import_mode = BATCH_ADVANCEMENT_IMPORT;
            stage_to_edit = nullptr;
            import_error_message[0] = '\0';
            import_search_buffer[0] = '\0'; // Clear search after cancel
        }
        if (ImGui::IsItemHovered()) {
            char import_advancements_tooltip_buffer[512];
            snprintf(import_advancements_tooltip_buffer, sizeof(import_advancements_tooltip_buffer),
                     "Cancel the import and close this window.\n(You can also press ESCAPE)");
            ImGui::SetTooltip("%s", import_advancements_tooltip_buffer);
        }

        // --- Display the counters, aligned to the right ---
        ImGui::SameLine();
        char counter_text[128];
        if (current_advancement_import_mode == CRITERIA_ONLY_IMPORT) {
            snprintf(counter_text, sizeof(counter_text), "Selected: %d Criteria", selected_crit_count);
        } else if (creator_selected_version <= MC_VERSION_1_6_4) {
            // Only achievements
            if (current_import_mode == BATCH_IMPORT) {
                snprintf(counter_text, sizeof(counter_text), "Selected: %d Achievements", selected_adv_count);
            } else {
                snprintf(counter_text, sizeof(counter_text), "Selected: %d / 1 Achievements", selected_adv_count);
            }
        } else {
            // Achievements/Advancements and criteria
            if (current_import_mode == BATCH_IMPORT) {
                snprintf(counter_text, sizeof(counter_text), "Selected: %d %s, %d Criteria",
                         selected_adv_count, advancements_label_plural_upper, selected_crit_count);
            } else {
                // SINGLE_SELECT_STAGE mode
                if (stage_to_edit != nullptr && stage_to_edit->type == SUBGOAL_ADVANCEMENT) {
                    // For SUBGOAL_ADVANCEMENT, only show the parent count
                    snprintf(counter_text, sizeof(counter_text), "Selected: %d / 1 %s",
                             selected_adv_count, advancements_label_upper);
                } else {
                    // For SUBGOAL_CRITERION, show both counts
                    snprintf(counter_text, sizeof(counter_text),
                             "Selected: %d / 1 %s, %d / 1 Criteria", selected_adv_count, advancements_label_upper,
                             selected_crit_count);
                }
            }
        }

        // Calculate position to right-align the text
        float text_width = ImGui::CalcTextSize(counter_text).x;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - text_width - ImGui::GetStyle().WindowPadding.x);
        ImGui::Text("%s", counter_text);

        ImGui::EndPopup();
    } // End of import advancements popup

    // Import Stats OR Sub-Stats from file
    const char *stats_import_title = (current_import_mode == SINGLE_SELECT_STAGE)
                                         ? "Select Stat from File"
                                         : (current_stat_import_mode == IMPORT_AS_SUB_STAT)
                                               ? "Import Sub-Stats from File"
                                               : "Import Stats from File";

    if (show_import_stats_popup) {
        ImGui::OpenPopup(stats_import_title);
    }
    if (ImGui::BeginPopupModal(stats_import_title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Hotkey logic for search bar
        if ((ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_LeftSuper)) &&
            ImGui::IsKeyPressed(ImGuiKey_F)) {
            focus_import_search = true;
        }

        // --- Create a filtered list of stats to display and operate on ---
        std::vector<ImportableStat *> filtered_stats;
        if (import_search_buffer[0] != '\0') {
            for (auto &stat: importable_stats) {
                if (str_contains_insensitive(stat.root_name.c_str(), import_search_buffer)) {
                    filtered_stats.push_back(&stat);
                }
            }
        } else {
            // If no search, all stats are "filtered"
            for (auto &stat: importable_stats) {
                filtered_stats.push_back(&stat);
            }
        }

        // --- Left-aligned Controls (only for batch import) ---
        if (current_import_mode == BATCH_IMPORT) {
            if (ImGui::Button("Select All")) {
                for (auto *stat_ptr: filtered_stats) {
                    stat_ptr->is_selected = true;
                }
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[256];
                if (current_stat_import_mode == IMPORT_AS_SUB_STAT) {
                    // Sub-stats
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Selects all sub-stats in the current search.\n\nYou can also Shift+Click to select a range.");
                } else {
                    // Regular stats
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Selects all stats in the current search.\n\nYou can also Shift+Click to select a range.");
                }
                ImGui::SetTooltip("%s", tooltip_buffer);
            }
            ImGui::SameLine();
            if (ImGui::Button("Deselect All")) {
                for (auto *stat_ptr: filtered_stats) {
                    stat_ptr->is_selected = false;
                }
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[256];
                if (current_stat_import_mode == IMPORT_AS_SUB_STAT) {
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Deselects all sub-stats in the current search.\n\n"
                             "You can also Shift+Click to deselect a range.");
                } else {
                    // Regular stats
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Deselects all stats in the current search.\n\n"
                             "You can also Shift+Click to deselect a range.");
                }
                ImGui::SetTooltip("%s", tooltip_buffer);
            }
        }


        // --- Right-aligned Controls ---
        const float search_bar_width = 250.0f;
        const float clear_button_width = ImGui::GetFrameHeight();
        const float right_controls_width = search_bar_width + clear_button_width + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(ImGui::GetWindowWidth() - right_controls_width - ImGui::GetStyle().WindowPadding.x);
        if (import_search_buffer[0] != '\0') {
            if (ImGui::Button("X##ClearImportStatsSearch", ImVec2(clear_button_width, 0))) {
                import_search_buffer[0] = '\0';
            }
        } else {
            ImGui::Dummy(ImVec2(clear_button_width, 0));
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(search_bar_width);
        if (focus_import_search) {
            ImGui::SetKeyboardFocusHere();
            focus_import_search = false;
        }
        ImGui::InputTextWithHint("##ImportStatsSearch", "Search...", import_search_buffer,
                                 sizeof(import_search_buffer));
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            if (current_stat_import_mode == IMPORT_AS_SUB_STAT) {
                // Sub-stats
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Filter list by sub-stat root name (case-insensitive).\nPress Ctrl+F or Cmd+F to focus.");
            } else {
                // Regular stats
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Filter list by stat root name (case-insensitive).\nPress Ctrl+F or Cmd+F to focus.");
            }
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::Separator();

        // --- Render List or empty message ---
        if (importable_stats.empty()) {
            if (current_stat_import_mode == IMPORT_AS_SUB_STAT) {
                // Sub-stats
                ImGui::Text("No parsable sub-stats found in the selected file.");
            } else {
                // Regular stats
                ImGui::Text("No parsable stats found in the selected file.");
            }
        } else {
            ImGui::BeginChild("StatsImporterScrollingRegion", ImVec2(600, 400), true);
            for (size_t i = 0; i < filtered_stats.size(); ++i) {
                auto &stat = *filtered_stats[i];
                ImGui::PushID(&stat); // Add unique ID scope for each widget

                if (ImGui::Checkbox(stat.root_name.c_str(), &stat.is_selected)) {
                    if (current_import_mode == SINGLE_SELECT_STAGE) {
                        if (stat.is_selected) {
                            // If it was just checked
                            // Deselect all others
                            for (auto &other_stat: importable_stats) {
                                if (&other_stat != &stat) {
                                    other_stat.is_selected = false;
                                }
                            }
                        }
                    } else if (ImGui::GetIO().KeyShift && last_clicked_stat_index != -1) {
                        int start = std::min((int) i, last_clicked_stat_index);
                        int end = std::max((int) i, last_clicked_stat_index);
                        for (int j = start; j <= end; ++j) {
                            filtered_stats[j]->is_selected = stat.is_selected;
                        }
                    }
                    last_clicked_stat_index = i;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        // --- Bottom Controls ---
        if (import_error_message[0] != '\0') {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", import_error_message);
        }

        // --- Count selected items ---
        int selected_stats_count = 0;
        for (const auto &stat: importable_stats) {
            if (stat.is_selected) {
                selected_stats_count++;
            }
        }

        const char *confirm_text = (current_import_mode == SINGLE_SELECT_STAGE) ? "Select" : "Confirm Import";
        if (ImGui::Button(confirm_text, ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            import_error_message[0] = '\0';

            if (selected_stats_count == 0) {
                snprintf(import_error_message, sizeof(import_error_message), "Error: No stats selected for import.");
            } else if (current_import_mode == SINGLE_SELECT_STAGE && stage_to_edit != nullptr) {
                // --- Logic for SINGLE_SELECT_STAGE ---
                for (const auto &new_stat: importable_stats) {
                    if (new_stat.is_selected) {
                        strncpy(stage_to_edit->root_name, new_stat.root_name.c_str(),
                                sizeof(stage_to_edit->root_name) - 1);
                        stage_to_edit->root_name[sizeof(stage_to_edit->root_name) - 1] = '\0';
                        // Manually trigger sync if it's a legacy version
                        if (creator_selected_version <= MC_VERSION_1_6_4) {
                            synchronize_legacy_ms_goal_stats(current_template_data);
                        }
                        break; // Only import the first one found
                    }
                }
                show_import_stats_popup = false;
                ImGui::CloseCurrentPopup();
            } else {
                // BATCH_IMPORT
                if (current_stat_import_mode == IMPORT_AS_SUB_STAT) {
                    // --- Logic to import as SUB-STATS (CRITERIA) ---
                    if (selected_stat != nullptr) {
                        std::unordered_set<std::string> existing_names;
                        for (const auto &crit: selected_stat->criteria) {
                            existing_names.insert(crit.root_name);
                        }
                        for (const auto &new_stat: importable_stats) {
                            if (new_stat.is_selected) {
                                if (existing_names.count(new_stat.root_name)) {
                                    snprintf(import_error_message, sizeof(import_error_message),
                                             "Error: Sub-stat '%s' already exists.", new_stat.root_name.c_str());
                                    break;
                                }
                                EditorTrackableItem new_crit = {};
                                strncpy(new_crit.root_name, new_stat.root_name.c_str(), sizeof(new_crit.root_name) - 1);
                                new_crit.root_name[sizeof(new_crit.root_name) - 1] = '\0';
                                strncpy(new_crit.display_name, new_stat.root_name.c_str(),
                                        sizeof(new_crit.display_name) - 1);
                                new_crit.display_name[sizeof(new_crit.display_name) - 1] = '\0';
                                strncpy(new_crit.icon_path, "blocks/placeholder.png", sizeof(new_crit.icon_path) - 1);
                                new_crit.icon_path[sizeof(new_crit.icon_path) - 1] = '\0';
                                new_crit.goal = 1;
                                selected_stat->criteria.push_back(new_crit);
                            }
                        }
                    }
                } else {
                    // IMPORT_AS_TOP_LEVEL
                    // --- Logic to import as TOP-LEVEL STATS ---
                    std::unordered_set<std::string> existing_names;
                    for (const auto &existing_stat: current_template_data.stats) {
                        if (existing_stat.is_simple_stat && !existing_stat.criteria.empty()) {
                            existing_names.insert(existing_stat.criteria[0].root_name);
                        }
                    }
                    for (const auto &new_stat: importable_stats) {
                        if (new_stat.is_selected) {
                            if (existing_names.count(new_stat.root_name)) {
                                snprintf(import_error_message, sizeof(import_error_message),
                                         "Error: Stat '%s' already exists.", new_stat.root_name.c_str());
                                break;
                            }
                            EditorTrackableCategory imported_stat = {};
                            strncpy(imported_stat.root_name, new_stat.root_name.c_str(),
                                    sizeof(imported_stat.root_name) - 1);
                            imported_stat.root_name[sizeof(imported_stat.root_name) - 1] = '\0';
                            strncpy(imported_stat.display_name, new_stat.root_name.c_str(),
                                    sizeof(imported_stat.display_name) - 1);
                            imported_stat.display_name[sizeof(imported_stat.display_name) - 1] = '\0';
                            strncpy(imported_stat.icon_path, "blocks/placeholder.png",
                                    sizeof(imported_stat.icon_path) - 1);
                            imported_stat.icon_path[sizeof(imported_stat.icon_path) - 1] = '\0';
                            imported_stat.is_simple_stat = true;
                            EditorTrackableItem crit = {};
                            strncpy(crit.root_name, new_stat.root_name.c_str(), sizeof(crit.root_name) - 1);
                            crit.root_name[sizeof(crit.root_name) - 1] = '\0';
                            crit.goal = 1;
                            imported_stat.criteria.push_back(crit);
                            current_template_data.stats.push_back(imported_stat);
                        }
                    }
                }
                // Close popup only if no error occurred during batch import
                if (import_error_message[0] == '\0') {
                    show_import_stats_popup = false;
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!show_import_stats_popup) {
                // If closed by any path
                current_import_mode = BATCH_IMPORT;
                stage_to_edit = nullptr;
                import_search_buffer[0] = '\0';
            }
        }

        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            if (current_stat_import_mode == IMPORT_AS_SUB_STAT) {
                // Sub-stats
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Import selected sub-stats into the template.\n(You can also press ENTER)");
            } else {
                // Regular stats
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Import selected stats into the template.\n(You can also press ENTER)");
            }
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            show_import_stats_popup = false;
            ImGui::CloseCurrentPopup(); // Actually closing popup
            current_import_mode = BATCH_IMPORT; // Reset to batch import on cancel
            stage_to_edit = nullptr;
            import_search_buffer[0] = '\0';
        }
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Cancel the import and close this window.\n(You can also press ESCAPE)");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }

        // --- Display the counter, aligned to the right ---
        ImGui::SameLine();
        char counter_text[128];
        if (current_import_mode == BATCH_IMPORT) {
            // Arbitrary amount
            snprintf(counter_text, sizeof(counter_text), "Selected: %d %s", selected_stats_count,
                     (current_stat_import_mode == IMPORT_AS_SUB_STAT) ? "Sub-Stats" : "Stats");
        } else {
            // Maximum of 1
            snprintf(counter_text, sizeof(counter_text), "Selected: %d / 1 %s", selected_stats_count,
                     (current_stat_import_mode == IMPORT_AS_SUB_STAT) ? "Sub-Stats" : "Stats");
        }
        // Calculate position to right-align the text
        float text_width = ImGui::CalcTextSize(counter_text).x;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - text_width - ImGui::GetStyle().WindowPadding.x);
        ImGui::Text("%s", counter_text);

        ImGui::EndPopup();
    } // End of import stats popup


    // Import Unlocks from file
    const char *unlocks_import_title = (current_import_mode == SINGLE_SELECT_STAGE)
                                           ? "Select Unlock from File"
                                           : "Import Unlocks from File";

    if (show_import_unlocks_popup) {
        ImGui::OpenPopup(unlocks_import_title);
    }
    if (ImGui::BeginPopupModal(unlocks_import_title, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        // Hotkey logic for search bar
        if ((ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_LeftSuper)) &&
            ImGui::IsKeyPressed(ImGuiKey_F)) {
            focus_import_search = true;
        }

        // --- Create a filtered list of unlocks to display and operate on ---
        std::vector<ImportableUnlock *> filtered_unlocks;
        if (import_search_buffer[0] != '\0') {
            for (auto &unlock: importable_unlocks) {
                if (str_contains_insensitive(unlock.root_name.c_str(), import_search_buffer)) {
                    filtered_unlocks.push_back(&unlock);
                }
            }
        } else {
            for (auto &unlock: importable_unlocks) {
                filtered_unlocks.push_back(&unlock);
            }
        }

        // --- Left-aligned Controls (for batch import only) ---
        if (current_import_mode == BATCH_IMPORT) {
            if (ImGui::Button("Select All")) {
                for (auto *unlock_ptr: filtered_unlocks) {
                    unlock_ptr->is_selected = true;
                }
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[256];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Selects all unlocks in the current search.\n\nYou can also Shift+Click to select a range.");
                ImGui::SetTooltip("%s", tooltip_buffer);
            }
            ImGui::SameLine();
            if (ImGui::Button("Deselect All")) {
                for (auto *unlock_ptr: filtered_unlocks) {
                    unlock_ptr->is_selected = false;
                }
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[256];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer), "Deselects all unlocks in the current search.");
                ImGui::SetTooltip("%s", tooltip_buffer);
            }
        }

        // --- Right-aligned Controls ---
        const float search_bar_width = 250.0f;
        const float clear_button_width = ImGui::GetFrameHeight();
        const float right_controls_width = search_bar_width + clear_button_width + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(ImGui::GetWindowWidth() - right_controls_width - ImGui::GetStyle().WindowPadding.x);
        if (import_search_buffer[0] != '\0') {
            if (ImGui::Button("X##ClearImportUnlocksSearch", ImVec2(clear_button_width, 0))) {
                import_search_buffer[0] = '\0';
            }
        } else {
            ImGui::Dummy(ImVec2(clear_button_width, 0));
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(search_bar_width);
        if (focus_import_search) {
            ImGui::SetKeyboardFocusHere();
            focus_import_search = false;
        }
        ImGui::InputTextWithHint("##ImportUnlocksSearch", "Search...", import_search_buffer,
                                 sizeof(import_search_buffer));
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Filter list by unlock root name (case-insensitive).\nPress Ctrl+F or Cmd+F to focus.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::Separator();

        // --- Render List or empty message ---
        if (importable_unlocks.empty()) {
            ImGui::Text("No parsable unlocks found in the selected file.");
        } else {
            ImGui::BeginChild("UnlocksImporterScrollingRegion", ImVec2(600, 400), true);
            for (size_t i = 0; i < filtered_unlocks.size(); ++i) {
                auto &unlock = *filtered_unlocks[i];
                ImGui::PushID(&unlock);

                if (ImGui::Checkbox(unlock.root_name.c_str(), &unlock.is_selected)) {
                    if (current_import_mode == SINGLE_SELECT_STAGE) {
                        // Only one can be checked
                        if (unlock.is_selected) {
                            // If it was just checked
                            for (auto &other_unlock: importable_unlocks) {
                                if (&other_unlock != &unlock) {
                                    other_unlock.is_selected = false;
                                }
                            }
                        }
                    } else if (ImGui::GetIO().KeyShift && last_clicked_unlock_index != -1) {
                        int start = std::min((int) i, last_clicked_unlock_index);
                        int end = std::max((int) i, last_clicked_unlock_index);
                        for (int j = start; j <= end; ++j) {
                            filtered_unlocks[j]->is_selected = unlock.is_selected;
                        }
                    }
                    last_clicked_unlock_index = i;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        // --- Bottom Controls ---
        if (import_error_message[0] != '\0') {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", import_error_message);
        }

        int selected_unlocks_count = 0;
        for (const auto &unlock: importable_unlocks) {
            if (unlock.is_selected) {
                selected_unlocks_count++;
            }
        }

        const char *confirm_unlock_text = (current_import_mode == SINGLE_SELECT_STAGE) ? "Select" : "Confirm Import";

        if (ImGui::Button(confirm_unlock_text, ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            import_error_message[0] = '\0';
            if (selected_unlocks_count == 0) {
                snprintf(import_error_message, sizeof(import_error_message), "Error: No unlocks selected for import.");
            } else if (current_import_mode == SINGLE_SELECT_STAGE && stage_to_edit != nullptr) {
                for (const auto &new_unlock: importable_unlocks) {
                    if (new_unlock.is_selected) {
                        strncpy(stage_to_edit->root_name, new_unlock.root_name.c_str(),
                                sizeof(stage_to_edit->root_name) - 1);
                        stage_to_edit->root_name[sizeof(stage_to_edit->root_name) - 1] = '\0';
                        // Manually trigger sync (will only run if version is also legacy)
                        if (creator_selected_version <= MC_VERSION_1_6_4) {
                            synchronize_legacy_ms_goal_stats(current_template_data);
                        }
                        break;
                    }
                }
                show_import_unlocks_popup = false;
                ImGui::CloseCurrentPopup();
            } else {
                std::unordered_set<std::string> existing_names;
                for (const auto &existing_unlock: current_template_data.unlocks) {
                    existing_names.insert(existing_unlock.root_name);
                }

                for (const auto &new_unlock: importable_unlocks) {
                    if (new_unlock.is_selected) {
                        if (existing_names.count(new_unlock.root_name)) {
                            snprintf(import_error_message, sizeof(import_error_message),
                                     "Error: Unlock '%s' already exists.", new_unlock.root_name.c_str());
                            break;
                        }
                        EditorTrackableItem imported_unlock = {};
                        strncpy(imported_unlock.root_name, new_unlock.root_name.c_str(),
                                sizeof(imported_unlock.root_name) - 1);
                        imported_unlock.root_name[sizeof(imported_unlock.root_name) - 1] = '\0';
                        strncpy(imported_unlock.display_name, new_unlock.root_name.c_str(),
                                sizeof(imported_unlock.display_name) - 1);
                        imported_unlock.display_name[sizeof(imported_unlock.display_name) - 1] = '\0';
                        strncpy(imported_unlock.icon_path, "blocks/placeholder.png",
                                sizeof(imported_unlock.icon_path) - 1);
                        imported_unlock.icon_path[sizeof(imported_unlock.icon_path) - 1] = '\0';
                        current_template_data.unlocks.push_back(imported_unlock);
                    }
                }
                if (import_error_message[0] == '\0') {
                    show_import_unlocks_popup = false;
                    ImGui::CloseCurrentPopup();
                    import_search_buffer[0] = '\0';
                }
            }
            if (!show_import_unlocks_popup) {
                current_import_mode = BATCH_IMPORT;
                stage_to_edit = nullptr;
                import_search_buffer[0] = '\0';
            }
        }
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Import selected unlocks into the template.\n(You can also press ENTER)");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            show_import_unlocks_popup = false;
            ImGui::CloseCurrentPopup();
            current_import_mode = BATCH_IMPORT;
            stage_to_edit = nullptr;
            import_search_buffer[0] = '\0';
        }
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[256];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Cancel the import and close this window.\n(You can also press ESCAPE)");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }

        ImGui::SameLine();
        char counter_text[128];
        if (current_import_mode == BATCH_IMPORT) {
            snprintf(counter_text, sizeof(counter_text), "Selected: %d Unlocks", selected_unlocks_count);
        } else {
            snprintf(counter_text, sizeof(counter_text), "Selected: %d / 1 Unlocks", selected_unlocks_count);
        }
        float text_width = ImGui::CalcTextSize(counter_text).x;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - text_width - ImGui::GetStyle().WindowPadding.x);
        ImGui::Text("%s", counter_text);

        ImGui::EndPopup();
    } // End of import unlocks popup

    if (roboto_font) {
        ImGui::PopFont();
    }
    ImGui::End();
}
