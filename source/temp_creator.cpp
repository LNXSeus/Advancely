//
// Created by Linus on 07.09.2025.
//

#include "temp_creator.h"
#include "settings_utils.h"
#include "logger.h"
#include "template_scanner.h"
#include "temp_creator_utils.h"
#include "path_utils.h"
#include "global_event_handler.h"
#include "file_utils.h" // For cJSON_from_file

#include <vector>
#include <string>
#include <unordered_set> // For checking duplicates
#include <functional> // For std::function

// In-memory representation of a template for editing
struct EditorTrackableItem {
    char root_name[192];
    char display_name[192];
    char icon_path[256];
    int goal;
    bool is_hidden;
};

// A struct to hold a category (like an advancement) and its criteria
struct EditorTrackableCategory {
    char root_name[192];
    char display_name[192];
    char icon_path[256];
    bool is_hidden;
    bool is_simple_stat; // UI flag to distinguish simple vs complex stats
    std::vector<EditorTrackableItem> criteria; // Criteria then are trackable items
};

// Structs for Multi-Stage Goal editing
struct EditorSubGoal {
    char stage_id[64];
    // display_text is loaded from the lang file, not stored in the main template
    SubGoalType type;
    char parent_advancement[192];
    char root_name[192];
    int required_progress;
};

struct EditorMultiStageGoal {
    char root_name[192];
    char display_name[192]; // From lang file
    char icon_path[256];
    bool is_hidden;
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
           strcmp(a.icon_path, b.icon_path) != 0 ||
           a.goal != b.goal ||
           a.is_hidden != b.is_hidden;
}

// Helper function to compare two EditorTrackableCategory structs
static bool are_editor_categories_different(const EditorTrackableCategory &a, const EditorTrackableCategory &b) {
    if (strcmp(a.root_name, b.root_name) != 0 ||
        strcmp(a.icon_path, b.icon_path) != 0 ||
        a.is_hidden != b.is_hidden ||
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
           a.type != b.type ||
           strcmp(a.parent_advancement, b.parent_advancement) != 0 ||
           strcmp(a.root_name, b.root_name) != 0 ||
           a.required_progress != b.required_progress;
}

static bool are_editor_multi_stage_goals_different(const EditorMultiStageGoal &a, const EditorMultiStageGoal &b) {
    if (strcmp(a.root_name, b.root_name) != 0 ||
        strcmp(a.icon_path, b.icon_path) != 0 ||
        a.is_hidden != b.is_hidden ||
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


// Helper to validate that all icon paths in a vector exist
static bool validate_icon_paths(const std::vector<EditorTrackableItem> &items, char *error_message_buffer) {
    for (const auto &item: items) {
        if (item.icon_path[0] == '\0') {
            continue; // Skip empty paths
        }
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, sizeof(full_path), "resources/icons/%s", item.icon_path);
        if (!path_exists(full_path)) {
            snprintf(error_message_buffer, 256, "Error: Icon file not found: '%s'", item.icon_path);
            return false;
        }
    }
    return true;
}

// Helper to validate icon paths for multi-stage goals
static bool validate_ms_goal_icon_paths(const std::vector<EditorMultiStageGoal> &goals, char *error_message_buffer) {
    for (const auto &goal: goals) {
        if (goal.icon_path[0] != '\0') {
            char full_path[MAX_PATH_LENGTH];
            snprintf(full_path, sizeof(full_path), "resources/icons/%s", goal.icon_path);
            if (!path_exists(full_path)) {
                snprintf(error_message_buffer, 256, "Error: Icon file not found for goal '%s': '%s'", goal.root_name,
                         goal.icon_path);
                return false;
            }
        }
    }
    return true;
}

// Helper to validate icon paths for nested categories
static bool validate_category_icon_paths(const std::vector<EditorTrackableCategory> &categories,
                                         char *error_message_buffer) {
    for (const auto &cat: categories) {
        // Check parent icon path
        if (cat.icon_path[0] != '\0') {
            char full_path[MAX_PATH_LENGTH];
            snprintf(full_path, sizeof(full_path), "resources/icons/%s", cat.icon_path);
            if (!path_exists(full_path)) {
                snprintf(error_message_buffer, 256, "Error: Icon file not found for '%s': '%s'", cat.root_name,
                         cat.icon_path);
                return false;
            }
        }
        // Check criteria icon paths
        for (const auto &crit: cat.criteria) {
            if (crit.icon_path[0] != '\0') {
                char full_path[MAX_PATH_LENGTH];
                snprintf(full_path, sizeof(full_path), "resources/icons/%s", crit.icon_path);
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
static void parse_editor_trackable_items(cJSON *json_array, std::vector<EditorTrackableItem> &item_vector) {
    item_vector.clear();
    if (!json_array) return;

    cJSON *item_json = nullptr;
    cJSON_ArrayForEach(item_json, json_array) {
        EditorTrackableItem new_item = {}; // Zero-initialize
        cJSON *root_name = cJSON_GetObjectItem(item_json, "root_name");
        cJSON *icon = cJSON_GetObjectItem(item_json, "icon");
        cJSON *target = cJSON_GetObjectItem(item_json, "target");
        cJSON *hidden = cJSON_GetObjectItem(item_json, "hidden");

        if (cJSON_IsString(root_name))
            strncpy(new_item.root_name, root_name->valuestring,
                    sizeof(new_item.root_name) - 1);
        if (cJSON_IsString(icon)) strncpy(new_item.icon_path, icon->valuestring, sizeof(new_item.icon_path) - 1);
        if (cJSON_IsNumber(target)) new_item.goal = target->valueint;
        if (cJSON_IsBool(hidden)) new_item.is_hidden = cJSON_IsTrue(hidden);

        // TODO: Load display_name from lang file

        item_vector.push_back(new_item);
    }
}

// Helper to parse a category object like "advancements" or "stats"
static void parse_editor_trackable_categories(cJSON *json_object,
                                              std::vector<EditorTrackableCategory> &category_vector) {
    category_vector.clear();
    if (!json_object) return;

    cJSON *category_json = nullptr;
    cJSON_ArrayForEach(category_json, json_object) {
        EditorTrackableCategory new_cat = {}; // Zero initialize

        // Parse parent item properties
        strncpy(new_cat.root_name, category_json->string, sizeof(new_cat.root_name) - 1);
        cJSON *icon = cJSON_GetObjectItem(category_json, "icon");
        cJSON *hidden = cJSON_GetObjectItem(category_json, "hidden");

        if (cJSON_IsString(icon)) strncpy(new_cat.icon_path, icon->valuestring, sizeof(new_cat.icon_path) - 1);
        if (cJSON_IsBool(hidden)) new_cat.is_hidden = cJSON_IsTrue(hidden);

        // TODO: Load display_name from lang file

        // Parse the nested criteria using existing helper function
        cJSON *criteria_object = cJSON_GetObjectItem(category_json, "criteria");
        if (criteria_object) {
            // Advancements/Stats store criteria in an object, not an array
            std::vector<EditorTrackableItem> criteria_items;
            cJSON *criterion_json = nullptr;
            cJSON_ArrayForEach(criterion_json, criteria_object) {
                EditorTrackableItem new_crit = {};
                strncpy(new_crit.root_name, criterion_json->string, sizeof(new_crit.root_name) - 1);

                cJSON *crit_icon = cJSON_GetObjectItem(criterion_json, "icon");
                cJSON *crit_hidden = cJSON_GetObjectItem(criterion_json, "hidden");

                if (cJSON_IsString(crit_icon))
                    strncpy(new_crit.icon_path, crit_icon->valuestring,
                            sizeof(new_crit.icon_path) - 1);
                if (cJSON_IsBool(crit_hidden)) new_crit.is_hidden = cJSON_IsTrue(crit_hidden);

                criteria_items.push_back(new_crit);
            }
            new_cat.criteria = criteria_items;
        }
        category_vector.push_back(new_cat);
    }
}

// Specific parser for stats to handle simple vs complex structures
static void parse_editor_stats(cJSON *json_object, std::vector<EditorTrackableCategory> &category_vector) {
    category_vector.clear();
    if (!json_object) return;

    cJSON *category_json = nullptr;
    cJSON_ArrayForEach(category_json, json_object) {
        EditorTrackableCategory new_cat = {}; // Zero initialize

        // Parse parent item properties
        strncpy(new_cat.root_name, category_json->string, sizeof(new_cat.root_name) - 1);
        cJSON *icon = cJSON_GetObjectItem(category_json, "icon");
        cJSON *hidden = cJSON_GetObjectItem(category_json, "hidden");

        if (cJSON_IsString(icon)) {
            strncpy(new_cat.icon_path, icon->valuestring, sizeof(new_cat.icon_path) - 1);
        }
        if (cJSON_IsBool(hidden)) new_cat.is_hidden = cJSON_IsTrue(hidden);


        cJSON *criteria_object = cJSON_GetObjectItem(category_json, "criteria");
        if (criteria_object && criteria_object->child) {
            // Chase 1: Complex stat with a "criteria" block
            new_cat.is_simple_stat = false;
            cJSON *criterion_json = nullptr;
            cJSON_ArrayForEach(criterion_json, criteria_object) {
                EditorTrackableItem new_crit = {};
                strncpy(new_crit.root_name, criterion_json->string, sizeof(new_crit.root_name) - 1);

                cJSON *crit_icon = cJSON_GetObjectItem(criterion_json, "icon");
                cJSON *crit_hidden = cJSON_GetObjectItem(criterion_json, "hidden");
                cJSON *crit_target = cJSON_GetObjectItem(criterion_json, "target");

                if (cJSON_IsString(crit_icon)) {
                    strncpy(new_crit.icon_path, crit_icon->valuestring, sizeof(new_crit.icon_path) - 1);
                }
                if (cJSON_IsBool(crit_hidden)) new_crit.is_hidden = cJSON_IsTrue(crit_hidden);
                if (cJSON_IsNumber(crit_target)) new_crit.goal = crit_target->valueint;

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
            } else {
                // Fallback for hidden MS goal stats (version <= 1.6.4)
                strncpy(new_crit.root_name, new_cat.root_name, sizeof(new_crit.root_name) - 1);
            }

            if (cJSON_IsNumber(target)) new_crit.goal = target->valueint;

            new_cat.criteria.push_back(new_crit);
        }
        category_vector.push_back(new_cat);
    }
}

// Parser for multi-stage goals
static void parse_editor_multi_stage_goals(cJSON *json_array, std::vector<EditorMultiStageGoal> &goals_vector) {
    goals_vector.clear();
    if (!json_array) return;

    cJSON *goal_json;
    cJSON_ArrayForEach(goal_json, json_array) {
        EditorMultiStageGoal new_goal = {};

        cJSON *root_name = cJSON_GetObjectItem(goal_json, "root_name");
        cJSON *icon = cJSON_GetObjectItem(goal_json, "icon");
        cJSON *hidden = cJSON_GetObjectItem(goal_json, "hidden");

        if (cJSON_IsString(root_name)) {
            strncpy(new_goal.root_name, root_name->valuestring, sizeof(new_goal.root_name) - 1);
        }
        if (cJSON_IsString(icon)) strncpy(new_goal.icon_path, icon->valuestring, sizeof(new_goal.icon_path) - 1);
        if (cJSON_IsBool(hidden)) new_goal.is_hidden = cJSON_IsTrue(hidden);

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
                }
                if (cJSON_IsString(parent_adv)) {
                    strncpy(new_stage.parent_advancement, parent_adv->valuestring,
                            sizeof(new_stage.parent_advancement) - 1);
                }
                if (cJSON_IsString(stage_root)) {
                    strncpy(new_stage.root_name, stage_root->valuestring, sizeof(new_stage.root_name) - 1);
                }
                if (cJSON_IsNumber(target)) new_stage.required_progress = target->valueint;

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
                                      EditorTemplate &editor_data, char *status_message_buffer) {
    editor_data.advancements.clear();
    editor_data.stats.clear();
    editor_data.unlocks.clear();
    editor_data.custom_goals.clear();
    editor_data.multi_stage_goals.clear();

    char version_filename[64];
    strncpy(version_filename, version, sizeof(version_filename) - 1);
    version_filename[sizeof(version_filename) - 1] = '\0';
    for (char *p = version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char template_path[MAX_PATH_LENGTH];
    snprintf(template_path, sizeof(template_path), "resources/templates/%s/%s/%s_%s%s.json",
             version, template_info.category, version_filename, template_info.category, template_info.optional_flag);

    cJSON *root = cJSON_from_file(template_path);
    if (!root) {
        snprintf(status_message_buffer, 256, "Error: Could not load template file for editing.");
        return false;
    }

    parse_editor_trackable_categories(cJSON_GetObjectItem(root, "advancements"), editor_data.advancements);
    parse_editor_stats(cJSON_GetObjectItem(root, "stats"), editor_data.stats);
    parse_editor_trackable_items(cJSON_GetObjectItem(root, "unlocks"), editor_data.unlocks);
    parse_editor_trackable_items(cJSON_GetObjectItem(root, "custom"), editor_data.custom_goals);
    parse_editor_multi_stage_goals(cJSON_GetObjectItem(root, "multi_stage_goals"), editor_data.multi_stage_goals);

    cJSON_Delete(root);
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

        cJSON *stages_array = cJSON_CreateArray();
        for (const auto &stage: goal.stages) {
            cJSON *stage_json = cJSON_CreateObject();
            cJSON_AddStringToObject(stage_json, "stage_id", stage.stage_id);

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
                                      EditorTemplate &editor_data, char *status_message_buffer) {
    char version_filename[64];
    strncpy(version_filename, version, sizeof(version_filename) - 1);
    version_filename[sizeof(version_filename) - 1] = '\0';
    for (char *p = version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char template_path[MAX_PATH_LENGTH];
    snprintf(template_path, sizeof(template_path), "resources/templates/%s/%s/%s_%s%s.json",
             version, template_info.category, version_filename, template_info.category, template_info.optional_flag);

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
    return true;
}


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

    // State for the creator's independent version selection
    static bool was_open_last_frame = false;
    static int creator_version_idx = -1;
    static char creator_version_str[64] = "";

    // State for the "Create New" view
    static bool show_create_new_view = false;
    static int new_template_version_idx = -1;
    static char new_template_category[MAX_PATH_LENGTH] = "";
    static char new_template_flag[MAX_PATH_LENGTH] = "";

    // State for the "Copy" view
    static bool show_copy_view = false;
    static int copy_template_version_idx = -1;
    static char copy_template_category[MAX_PATH_LENGTH] = "";
    static char copy_template_flag[MAX_PATH_LENGTH] = "";

    // State for the editor view
    static bool editing_template = false;
    static EditorTemplate current_template_data;
    static EditorTemplate saved_template_data; // A snapshot of the last saved state
    static DiscoveredTemplate selected_template_info;
    static int selected_advancement_index = -1; // Tracks which advancement is currently selected in the editor
    static int selected_stat_index = -1;
    static int selected_ms_goal_index = -1;
    static bool show_unsaved_changes_popup = false;
    static std::function<void()> pending_action = nullptr;


    // State for user feedback next to save button in editor view
    enum SaveMessageType { MSG_NONE, MSG_SUCCESS, MSG_ERROR };
    static SaveMessageType save_message_type = MSG_NONE;
    static char status_message[256] = "";

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
        if (strcmp(selected.category, app_settings->category) == 0 &&
            strcmp(selected.optional_flag, app_settings->optional_flag) == 0) {
            is_current_template = true;
        }
    }


    // Rescan templates if the creator's version selection changes
    if (strcmp(last_scanned_version, creator_version_str) != 0) {
        free_discovered_templates(&discovered_templates, &discovered_template_count);
        scan_for_templates(creator_version_str, &discovered_templates, &discovered_template_count);
        strncpy(last_scanned_version, creator_version_str, sizeof(last_scanned_version) - 1);
        selected_template_index = -1; // Reset selection
        status_message[0] = '\0'; // Clear status message
    }

    // UI RENDERING

    // Dynamically create the window title based on unsaved changes
    // On VERY FIRST OPEN it has this size -> nothing in imgui.ini file
    ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_FirstUseEver);
    ImGui::Begin("Template Creator", p_open);

    if (roboto_font) {
        ImGui::PushFont(roboto_font);
    }

    // Version Selector
    ImGui::SetNextItemWidth(250); // Set a reasonable width for the combo box
    int original_version_idx = creator_version_idx; // UUse a temporary variable for the combo
    if (ImGui::Combo("Version", &creator_version_idx, VERSION_STRINGS, VERSION_STRINGS_COUNT)) {
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
            };
        } else {
            // No unsaved changes, so the change is final. Just update the string version.
            strncpy(creator_version_str, VERSION_STRINGS[creator_version_idx], sizeof(creator_version_str) - 1);
            creator_version_str[sizeof(creator_version_str) - 1] = '\0';
            editing_template = false; // Always exit editor on version change
        }
    }
    ImGui::Separator();

    // Left Pane: Template List
    ImGui::BeginChild("TemplateList", ImVec2(250, 0), true);
    ImGui::Text("Existing Templates");
    ImGui::Separator();

    for (int i = 0; i < discovered_template_count; i++) {
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
            // Only trigger unsaved changes logic if selecting a DIFFERENT template
            if (editing_template && editor_has_unsaved_changes && selected_template_index != (int) i) {
                show_unsaved_changes_popup = true;
                pending_action = [&, i]() {
                    selected_template_index = i;
                    selected_template_info = discovered_templates[i];
                    load_template_for_editing(creator_version_str, selected_template_info, current_template_data,
                                              status_message);
                };
            } else {
                // If not editing, or no unsaved changes, or re-selecting the same template, just update the index
                selected_template_index = i;
                if (editing_template) {
                    // If already editing, reload the data to discard any accidental non-flagged UI changes
                    load_template_for_editing(creator_version_str, discovered_templates[i], current_template_data,
                                              status_message);
                }
            }

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

    if (ImGui::Button("Create New Template")) {
        auto create_action = [&]() {
            show_create_new_view = true;
            show_copy_view = false;
            editing_template = false;
            selected_template_index = -1;
            status_message[0] = '\0';
            new_template_category[0] = '\0';
            new_template_flag[0] = '\0';
        };
        if (editing_template && editor_has_unsaved_changes) {
            show_unsaved_changes_popup = true;
            pending_action = create_action;
        } else {
            create_action();
        }
    }

    ImGui::SameLine();

    ImGui::BeginDisabled(selected_template_index == -1);
    if (ImGui::Button("Edit Template")) {
        if (selected_template_index != -1) {
            editing_template = true; // Only true here
            show_create_new_view = false;
            show_copy_view = false;


            // Store the info and load the data for the first time
            selected_template_info = discovered_templates[selected_template_index];
            if (load_template_for_editing(creator_version_str, selected_template_info, current_template_data,
                                          status_message)) {
                // On successful load, create the snapshot of the clean state
                saved_template_data = current_template_data;
            }

            // TODO: Add logic to load the selected template file into current_template_data
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    // Allow copying of the currently used template
    ImGui::BeginDisabled(selected_template_index == -1);
    if (ImGui::Button("Copy Template")) {
        if (selected_template_index != -1) {
            show_copy_view = true;
            show_create_new_view = false;
            editing_template = false; // Still allow clicking other buttons (e.g., copy, delete, ...) when editing
            status_message[0] = '\0';

            // Pre-fill with selected template's info
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            strncpy(copy_template_category, selected.category, sizeof(copy_template_category) - 1);
            strncpy(copy_template_flag, selected.optional_flag, sizeof(copy_template_flag) - 1);
            // Default copy destination to the currently viewed version
            copy_template_version_idx = creator_version_idx;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Copy the currently selected template. You can modify its version, category or optional flag.");

    ImGui::EndDisabled();
    ImGui::SameLine();

    // Disable if nothing is selected or the selected template is in use
    ImGui::BeginDisabled(selected_template_index == -1 || is_current_template);
    if (ImGui::Button("Delete Template")) {
        if (selected_template_index != -1) {
            ImGui::OpenPopup("Delete Template?");
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (selected_template_index != -1 && is_current_template) {
            ImGui::SetTooltip("Cannot delete the template currently in use.");
        } else if (selected_template_index != -1) {
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            char tooltip_text[512];
            if (selected.optional_flag[0] != '\0') {
                snprintf(tooltip_text, sizeof(tooltip_text), "Delete template:\nVersion: %s\nCategory: %s\nFlag: %s",
                         creator_version_str, selected.category, selected.optional_flag);
            } else {
                snprintf(tooltip_text, sizeof(tooltip_text), "Delete template:\nVersion: %s\nCategory: %s",
                         creator_version_str, selected.category);
            }
            ImGui::SetTooltip(tooltip_text);
        } else {
            ImGui::SetTooltip("Delete the currently selected template.");
        }
    }

    // Delete Confirmation Popup
    if (ImGui::BeginPopupModal("Delete Template?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (selected_template_index != -1) {
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            ImGui::Text("Are you sure you want to permanently delete this template?\nThis action cannot be undone.");

            // Construct detailed info string
            char template_info[512];
            if (selected.optional_flag[0] != '\0') {
                snprintf(template_info, sizeof(template_info), "Version: %s\nCategory: %s\nFlag: %s",
                         creator_version_str, selected.category, selected.optional_flag);
            } else {
                snprintf(template_info, sizeof(template_info), "Version: %s\nCategory: %s",
                         creator_version_str, selected.category);
            }
            ImGui::TextUnformatted(template_info);
            ImGui::Separator();

            if (ImGui::Button("OK", ImVec2(120, 0))) {
                if (delete_template_files(creator_version_str, selected.category, selected.optional_flag)) {
                    snprintf(status_message, sizeof(status_message), "Template '%s' deleted.", selected.category);
                    SDL_SetAtomicInt(&g_templates_changed, 1); // Signal change
                } else {
                    snprintf(status_message, sizeof(status_message), "Error: Failed to delete template '%s'.",
                             selected.category);
                }
                selected_template_index = -1;
                last_scanned_version[0] = '\0'; // Force rescan
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();


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
        ImGui::Separator();

        // Save when creator window is focused
        // Enter key is disabled when a popup is open
        if (ImGui::Button("Save") || (ImGui::IsKeyPressed(ImGuiKey_Enter) &&
                                      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !
                                      ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))) {
            // Reset message state on new save attempt
            save_message_type = MSG_NONE;
            status_message[0] = '\0';

            bool validation_passed = true;

            // --- Advancements Validation ---
            if (has_duplicate_category_root_names(current_template_data.advancements, status_message) ||
                !validate_category_icon_paths(current_template_data.advancements, status_message)) {
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
                    !validate_category_icon_paths(current_template_data.stats, status_message)) {
                    validation_passed = false;
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
                    !validate_ms_goal_icon_paths(current_template_data.multi_stage_goals, status_message)) {
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
                if (save_template_from_editor(creator_version_str, selected_template_info, current_template_data,
                                              status_message)) {
                    // Update snapshot to new clean state
                    saved_template_data = current_template_data;
                    save_message_type = MSG_SUCCESS;
                    snprintf(status_message, sizeof(status_message), "Saved!");
                } else {
                    save_message_type = MSG_ERROR; // Save function failed
                }
            } else {
                save_message_type = MSG_ERROR; // A validation check failed
            }
        }

        // Save button tooltip
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Press ENTER to save the currently edited template into the .json files.\n"
                "Does not save on errors.");

        // Calculate the unsaved changes flag on-the-fly each frame
        bool editor_has_unsaved_changes = are_editor_templates_different(current_template_data, saved_template_data);

        // This "Unsaved Changes" indicator will appear/disappear automatically
        if (editor_has_unsaved_changes) {
            ImGui::SameLine();
            // Replace the TextColored indicator with a Revert button
            if (ImGui::Button("Revert Changes")) {
                current_template_data = saved_template_data;
                save_message_type = MSG_NONE; // Clear any existing message
                status_message[0] = '\0';     // Clear the message text
            }
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
                if (save_template_from_editor(creator_version_str, selected_template_info, current_template_data,
                                              status_message)) {
                    saved_template_data = current_template_data; // Update snapshot on successful save
                    if (pending_action) pending_action();
                }
                ImGui::CloseCurrentPopup();
            }
            // Save hover text
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Press ENTER to save.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(120, 0))) {
                current_template_data = saved_template_data; // Restore saved data as current
                if (pending_action) pending_action();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            // Cancel or pressing ESC
            if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            // Cancel hover text
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Press ESC to cancel.");
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginTabBar("EditorTabs")) {
            if (ImGui::BeginTabItem("Advancements")) {
                // TWO-PANE LAYOUT
                float pane_width = ImGui::GetContentRegionAvail().x * 0.4f;
                ImGui::BeginChild("AdvancementListPane", ImVec2(pane_width, 0), true);

                // LEFT PANE: List of Advancements
                if (ImGui::Button("Add New Advancement")) {
                    current_template_data.advancements.push_back({});
                    save_message_type = MSG_NONE;
                }
                ImGui::Separator();

                int advancement_to_remove = -1;
                int advancement_to_copy = -1; // To queue a copy action

                // State for drag and drop
                int adv_dnd_source_index = -1;
                int adv_dnd_target_index = -1;

                for (size_t i = 0; i < current_template_data.advancements.size(); ++i) {
                    ImGui::PushID(i);
                    const char *label = current_template_data.advancements[i].root_name[0]
                                            ? current_template_data.advancements[i].root_name
                                            : "[New Advancement]";

                    // Draw the "X" (Remove) button
                    if (ImGui::Button("X")) {
                        advancement_to_remove = i;
                        save_message_type = MSG_NONE;
                    }
                    ImGui::SameLine();

                    // Draw the "Copy" button
                    if (ImGui::Button("Copy")) {
                        advancement_to_copy = i;
                        save_message_type = MSG_NONE;
                    }
                    ImGui::SameLine();

                    // Draw the selectable, which now takes the remaining space
                    if (ImGui::Selectable(label, selected_advancement_index == (int) i)) {
                        // (Logic to handle selection and unsaved changes remains the same)
                        if (selected_advancement_index != (int) i) {
                            if (editor_has_unsaved_changes) {
                                show_unsaved_changes_popup = true;
                                pending_action = [&, i]() {
                                    // Pneding action only changes the index
                                    selected_advancement_index = i;
                                };
                            } else {
                                selected_advancement_index = i;
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

                // Handle Drag and Drop reordering after the loop to avoid modifying the vector while iterating
                if (adv_dnd_source_index != -1 && adv_dnd_target_index != -1) {
                    EditorTrackableCategory item_to_move = current_template_data.advancements[adv_dnd_source_index];
                    current_template_data.advancements.erase(
                        current_template_data.advancements.begin() + adv_dnd_source_index);
                    current_template_data.advancements.insert(
                        current_template_data.advancements.begin() + adv_dnd_target_index, item_to_move);

                    // Update selection to follow the moved item
                    if (selected_advancement_index == adv_dnd_source_index) {
                        selected_advancement_index = adv_dnd_target_index;
                    }

                    save_message_type = MSG_NONE;
                }

                // Handle removal
                if (advancement_to_remove != -1) {
                    // If the removed item is the selected one, deselect it
                    if (selected_advancement_index == advancement_to_remove) {
                        selected_advancement_index = -1;
                    }
                    // If we remove an item before the selected one, we need to shift the index down
                    else if (selected_advancement_index > advancement_to_remove) {
                        selected_advancement_index--;
                    }
                    current_template_data.advancements.erase(
                        current_template_data.advancements.begin() + advancement_to_remove);
                    save_message_type = MSG_NONE;
                }

                // Handle copying
                if (advancement_to_copy != -1) {
                    const auto &source_advancement = current_template_data.advancements[advancement_to_copy];
                    EditorTrackableCategory new_advancement = source_advancement; // Create a deep copy

                    char base_name[192];
                    strncpy(base_name, source_advancement.root_name, sizeof(base_name) - 1);
                    base_name[sizeof(base_name) - 1] = '\0';

                    char new_name[192];
                    int copy_counter = 1;

                    // Loop to find a unique name
                    while (true) {
                        if (copy_counter == 1) {
                            snprintf(new_name, sizeof(new_name), "%s_copy", base_name);
                        } else {
                            snprintf(new_name, sizeof(new_name), "%s_copy%d", base_name, copy_counter);
                        }

                        // Check if this name already exists
                        bool name_exists = false;
                        for (const auto &adv: current_template_data.advancements) {
                            if (strcmp(adv.root_name, new_name) == 0) {
                                name_exists = true;
                                break;
                            }
                        }

                        if (!name_exists) {
                            break; // Found a unique name
                        }
                        copy_counter++; // Increment and try the next number
                    }

                    // Apply the new unique name and insert the copy
                    strncpy(new_advancement.root_name, new_name, sizeof(new_advancement.root_name) - 1);
                    current_template_data.advancements.insert(
                        current_template_data.advancements.begin() + advancement_to_copy + 1, new_advancement);
                }

                ImGui::EndChild(); // End of Left Pane
                ImGui::SameLine();

                // RIGHT PANE: Details of Selected Advancement
                ImGui::BeginChild("AdvancementDetailsPane", ImVec2(0, 0), true);
                if (selected_advancement_index != -1 && (size_t) selected_advancement_index < current_template_data.
                    advancements.size()) {
                    auto &advancement = current_template_data.advancements[selected_advancement_index];

                    ImGui::Text("Edit Advancement Details");
                    ImGui::Separator();

                    if (ImGui::InputText("Root Name", advancement.root_name, sizeof(advancement.root_name))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::InputText("Icon Path", advancement.icon_path, sizeof(advancement.icon_path))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::Checkbox("Hidden", &advancement.is_hidden)) {
                        save_message_type = MSG_NONE;
                    }
                    ImGui::Separator();
                    ImGui::Text("Criteria");

                    if (ImGui::Button("Add New Criterion")) {
                        advancement.criteria.push_back({});
                        save_message_type = MSG_NONE;
                    }

                    int criterion_to_remove = -1;
                    int criterion_to_copy = -1;

                    // State for drag and drop
                    int criterion_dnd_source_index = -1;
                    int criterion_dnd_target_index = -1;

                    for (size_t j = 0; j < advancement.criteria.size(); j++) {
                        ImGui::PushID(j);
                        auto &criterion = advancement.criteria[j];

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

                        if (ImGui::InputText("Root Name", criterion.root_name, sizeof(criterion.root_name))) {
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::InputText("Icon Path", criterion.icon_path, sizeof(criterion.icon_path))) {
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::Checkbox("Hidden", &criterion.is_hidden)) {
                            save_message_type = MSG_NONE;
                        }

                        // "Copy" button for criteria
                        if (ImGui::Button("Copy")) {
                            criterion_to_copy = j;
                            save_message_type = MSG_NONE;
                        }
                        ImGui::SameLine();

                        if (ImGui::Button("Remove")) {
                            criterion_to_remove = j;
                            save_message_type = MSG_NONE;
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
                        EditorTrackableItem new_criterion = source_criterion; // Create Deepcopy
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
                        advancement.criteria.insert(advancement.criteria.begin() + criterion_to_copy + 1,
                                                    new_criterion);
                    }
                } else {
                    ImGui::Text("Select an advancement from the list to edit its details.");
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Stats")) {
                // TWO PANE LAYOUT for Stats
                float pane_width = ImGui::GetContentRegionAvail().x * 0.4f;
                ImGui::BeginChild("StatListPane", ImVec2(pane_width, 0), true);

                if (ImGui::Button("Add New Stat")) {
                    EditorTrackableCategory new_stat = {};
                    new_stat.is_simple_stat = true; // Default new stat to simple
                    new_stat.criteria.push_back({}); // Add one empty criterion
                    current_template_data.stats.push_back(new_stat);
                    save_message_type = MSG_NONE;
                }
                ImGui::Separator();

                int stat_to_remove = -1;
                int stat_to_copy = -1; // To queue a copy action

                // State for drag and drop
                int stat_dnd_source_index = -1;
                int stat_dnd_target_index = -1;

                for (size_t i = 0; i < current_template_data.stats.size(); i++) {
                    ImGui::PushID(i);
                    const char *label = current_template_data.stats[i].root_name[0]
                                            ? current_template_data.stats[i].root_name
                                            : "[New Stat]";

                    // Draw the "X" (Remove) button
                    if (ImGui::Button("X")) {
                        stat_to_remove = i;
                        save_message_type = MSG_NONE;
                    }
                    ImGui::SameLine();

                    // Draw the "Copy" button
                    if (ImGui::Button("Copy")) {
                        stat_to_copy = i;
                        save_message_type = MSG_NONE;
                    }
                    ImGui::SameLine();

                    // Draw the selectable, which now takes the remaining space
                    if (ImGui::Selectable(label, selected_stat_index == (int) i)) {
                        // (Logic to handle selection and unsaved changes remains the same)
                        if (selected_stat_index != (int) i) {
                            if (editor_has_unsaved_changes) {
                                show_unsaved_changes_popup = true;
                                pending_action = [&, i]() {
                                    selected_stat_index = i;
                                };
                            } else {
                                selected_stat_index = i;
                            }
                        }
                    }

                    // DRAG AND DROP LOGIC
                    // Make the entire row a drag source
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        // Use a unique payload ID for this list
                        ImGui::SetDragDropPayload("STAT_DND", &i, sizeof(int));
                        ImGui::Text("Reorder %s", label);
                        ImGui::EndDragDropSource();
                    }
                    // Make the entire row a drop target
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("STAT_DND")) {
                            stat_dnd_source_index = *(const int *) payload->Data;
                            stat_dnd_target_index = i;
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::PopID();
                }

                // Handle Drag and Drop reordering after the loop to avoid modifying the vector while iterating
                if (stat_dnd_source_index != -1 && stat_dnd_target_index != -1) {
                    EditorTrackableCategory item_to_move = current_template_data.stats[stat_dnd_source_index];
                    current_template_data.stats.erase(
                        current_template_data.stats.begin() + stat_dnd_source_index);
                    current_template_data.stats.insert(
                        current_template_data.stats.begin() + stat_dnd_target_index, item_to_move);

                    if (selected_stat_index == stat_dnd_source_index) {
                        selected_stat_index = stat_dnd_target_index;
                    } else if (selected_stat_index > stat_dnd_source_index && selected_stat_index <=
                               stat_dnd_target_index) {
                        selected_stat_index--;
                    } else if (selected_stat_index < stat_dnd_source_index && selected_stat_index >=
                               stat_dnd_target_index) {
                        selected_stat_index++;
                    }
                    save_message_type = MSG_NONE;
                }

                if (stat_to_remove != -1) {
                    if (selected_stat_index == stat_to_remove) selected_stat_index = -1;
                    else if (selected_stat_index > stat_to_remove) selected_stat_index--;
                    current_template_data.stats.erase(current_template_data.stats.begin() + stat_to_remove);
                    save_message_type = MSG_NONE;
                }

                // Handle copying
                if (stat_to_copy != -1) {
                    const auto &source_stat = current_template_data.stats[stat_to_copy];
                    EditorTrackableCategory new_stat = source_stat; // Create a deep copy

                    char base_name[192];
                    strncpy(base_name, source_stat.root_name, sizeof(base_name) - 1);
                    base_name[sizeof(base_name) - 1] = '\0';

                    char new_name[192];
                    int copy_counter = 1;

                    // Loop to find a unique name
                    while (true) {
                        if (copy_counter == 1) {
                            snprintf(new_name, sizeof(new_name), "%s_copy", base_name);
                        } else {
                            snprintf(new_name, sizeof(new_name), "%s_copy%d", base_name, copy_counter);
                        }

                        // Check if this name already exists
                        bool name_exists = false;
                        for (const auto &stat: current_template_data.stats) {
                            if (strcmp(stat.root_name, new_name) == 0) {
                                name_exists = true;
                                break;
                            }
                        }

                        if (!name_exists) {
                            break; // Found a unique name
                        }
                        copy_counter++; // Increment and try the next number
                    }

                    // Apply the new unique name and insert the copy
                    strncpy(new_stat.root_name, new_name, sizeof(new_stat.root_name) - 1);
                    current_template_data.stats.insert(
                        current_template_data.stats.begin() + stat_to_copy + 1, new_stat);
                }

                ImGui::EndChild();
                ImGui::SameLine();

                ImGui::BeginChild("StatDetailsPane", ImVec2(0, 0), true);
                if (selected_stat_index != -1 && (size_t) selected_stat_index < current_template_data.stats.size()) {
                    auto &stat_cat = current_template_data.stats[selected_stat_index];

                    if (ImGui::InputText("Category Key", stat_cat.root_name, sizeof(stat_cat.root_name))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::InputText("Icon Path", stat_cat.icon_path, sizeof(stat_cat.icon_path))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::Checkbox("Hidden", &stat_cat.is_hidden)) {
                        save_message_type = MSG_NONE;
                    }

                    // Invert the logic for the checkbox to be more intuitive for the user
                    bool is_multi_stat = !stat_cat.is_simple_stat;
                    if (ImGui::Checkbox("Multi-Stat Category", &is_multi_stat)) {
                        stat_cat.is_simple_stat = !is_multi_stat;
                        // If we are switching FROM multi-stat TO simple-stat and have more than one criterion,
                        // we keep the first one and discard the rest.
                        if (stat_cat.is_simple_stat && stat_cat.criteria.size() > 1) {
                            EditorTrackableItem first_crit = stat_cat.criteria[0];
                            stat_cat.criteria.clear();
                            stat_cat.criteria.push_back(first_crit);
                        }
                        save_message_type = MSG_NONE;
                    }
                    ImGui::Separator();

                    if (stat_cat.is_simple_stat) {
                        // UI for a simple stat
                        if (stat_cat.criteria.empty()) stat_cat.criteria.push_back({}); // Ensure one exists

                        auto &simple_crit = stat_cat.criteria[0];
                        if (ImGui::InputText("Stat Root Name", simple_crit.root_name, sizeof(simple_crit.root_name))) {
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::InputInt("Target", &simple_crit.goal)) {
                            save_message_type = MSG_NONE;
                        }
                    } else {
                        // UI for a complex, multi-stat category (similar to advancements)
                        ImGui::Text("Criteria");

                        // Add Criterion button only for multi-stat categories -> complex stats
                        if (ImGui::Button("Add Criterion")) {
                            stat_cat.criteria.push_back({});
                            save_message_type = MSG_NONE;
                        }

                        int crit_to_remove = -1;
                        int crit_to_copy = -1;

                        int stat_crit_dnd_source_index = -1;
                        int stat_crit_dnd_target_index = -1;
                        for (size_t j = 0; j < stat_cat.criteria.size(); j++) {
                            auto &crit = stat_cat.criteria[j];
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
                            if (ImGui::InputText("Root Name", crit.root_name, sizeof(crit.root_name))) {
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::InputText("Icon Path", crit.icon_path, sizeof(crit.icon_path))) {
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::InputInt("Target", &crit.goal)) {
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::Checkbox("Hidden", &crit.is_hidden)) {
                                save_message_type = MSG_NONE;
                            }
                            if (ImGui::Button("Copy")) {
                                crit_to_copy = j;
                                save_message_type = MSG_NONE;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Remove")) {
                                crit_to_remove = j;
                                save_message_type = MSG_NONE;
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
                            EditorTrackableItem new_criterion = source_criterion;
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
                            strncpy(new_criterion.root_name, new_name, sizeof(new_criterion.root_name) - 1);
                            stat_cat.criteria.insert(stat_cat.criteria.begin() + crit_to_copy + 1, new_criterion);
                            save_message_type = MSG_NONE;
                        }
                    }
                } else {
                    ImGui::Text("Select a stat from the list to edit its details.");
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }


            // Only show the Unlocks tab for the specific version
            if (strcmp(creator_version_str, "25w14craftmine") == 0) {
                if (ImGui::BeginTabItem("Unlocks")) {
                    if (ImGui::Button("Add New Unlock")) {
                        current_template_data.unlocks.push_back({});
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    ImGui::Separator();
                    int item_to_remove = -1;
                    int item_to_copy = -1;
                    int unlocks_dnd_source_index = -1;
                    int unlocks_dnd_target_index = -1;
                    for (size_t i = 0; i < current_template_data.unlocks.size(); i++) {
                        ImGui::PushID(i);
                        auto &unlock = current_template_data.unlocks[i];

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
                        if (ImGui::InputText("Icon Path", unlock.icon_path, sizeof(unlock.icon_path))) {
                            save_message_type = MSG_NONE; // Clear message on new edit
                        }
                        if (ImGui::Checkbox("Hidden", &unlock.is_hidden)) {
                            save_message_type = MSG_NONE; // Clear message on new edit
                        }

                        if (ImGui::Button("Copy")) {
                            item_to_copy = i;
                            save_message_type = MSG_NONE;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Remove")) {
                            item_to_remove = i;
                            save_message_type = MSG_NONE; // Clear message on new edit
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
                        EditorTrackableItem new_item = source_item;
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
                        current_template_data.unlocks.insert(current_template_data.unlocks.begin() + item_to_copy + 1,
                                                             new_item);
                    }
                    ImGui::EndTabItem();
                }
            }
            if (ImGui::BeginTabItem
                ("Custom Goals")) {
                if (ImGui::Button("Add New Custom Goal")) {
                    current_template_data.custom_goals.push_back({});
                    save_message_type = MSG_NONE; // Clear message on new edit
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(Hotkeys are configured in the main Settings window)");
                int item_to_remove = -1;
                int item_to_copy = -1;
                int custom_dnd_source_index = -1;
                int custom_dnd_target_index = -1;
                for (size_t i = 0; i < current_template_data.custom_goals.size(); ++i) {
                    ImGui::PushID(i);
                    auto &goal = current_template_data.custom_goals[i];

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
                    if (ImGui::InputText("Root Name", goal.root_name, sizeof(goal.root_name))) {
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    if (ImGui::InputText("Icon Path", goal.icon_path, sizeof(goal.icon_path))) {
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    if (ImGui::InputInt("Target Goal", &goal.goal)) {
                        // No values below -1 allowed
                        if (goal.goal < -1) {
                            goal.goal = -1;
                        }
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "0 for a simple toggle, -1 for an infinite counter, >0 for a progress-based counter.");
                    if (ImGui::Checkbox("Hidden", &goal.is_hidden)) {
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }

                    // "Copy" button for custom goals
                    if (ImGui::Button("Copy")) {
                        item_to_copy = i;
                        save_message_type = MSG_NONE;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove")) {
                        item_to_remove = i;
                        save_message_type = MSG_NONE; // Clear message on new edit
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
                    EditorTrackableItem new_item = source_item;
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
                    current_template_data.custom_goals.insert(
                        current_template_data.custom_goals.begin() + item_to_copy + 1, new_item);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Multi-Stage Goals")) {
                // TWO-PANE LAYOUT for Multi-Stage Goals
                float pane_width = ImGui::GetContentRegionAvail().x * 0.4f;
                ImGui::BeginChild("MSGoalListPane", ImVec2(pane_width, 0), true);

                if (ImGui::Button("Add New Multi-Stage Goal")) {
                    current_template_data.multi_stage_goals.push_back({});
                    save_message_type = MSG_NONE;
                }
                ImGui::Separator();

                int goal_to_remove = -1;
                int goal_to_copy = -1;

                int ms_goal_dnd_source_index = -1;
                int ms_goal_dnd_target_index = -1;

                for (size_t i = 0; i < current_template_data.multi_stage_goals.size(); ++i) {
                    ImGui::PushID(i);
                    const char *label = current_template_data.multi_stage_goals[i].root_name[0]
                                            ? current_template_data.multi_stage_goals[i].root_name
                                            : "[New Goal]";
                    if (ImGui::Button("X")) {
                        goal_to_remove = i;
                        save_message_type = MSG_NONE;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Copy")) {
                        goal_to_copy = i;
                        save_message_type = MSG_NONE;
                    }
                    ImGui::SameLine();
                    ImGui::SameLine();
                    if (ImGui::Selectable(label, selected_ms_goal_index == (int) i)) {
                        if (selected_ms_goal_index != (int) i) {
                            if (editor_has_unsaved_changes) {
                                show_unsaved_changes_popup = true;
                                pending_action = [&, i]() {
                                    // Only changing the index
                                    selected_ms_goal_index = i;
                                };
                            } else {
                                selected_ms_goal_index = i;
                            }
                        }
                    }
                    // DRAG AND DROP LOGIC
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

                // Handle Drag and Drop reordering after the loop
                if (ms_goal_dnd_source_index != -1 && ms_goal_dnd_target_index != -1) {
                    EditorMultiStageGoal item_to_move = current_template_data.multi_stage_goals[
                        ms_goal_dnd_source_index];
                    current_template_data.multi_stage_goals.erase(
                        current_template_data.multi_stage_goals.begin() + ms_goal_dnd_source_index);
                    current_template_data.multi_stage_goals.insert(
                        current_template_data.multi_stage_goals.begin() + ms_goal_dnd_target_index, item_to_move);

                    // Update selection to follow the moved item
                    if (selected_ms_goal_index == ms_goal_dnd_source_index) {
                        selected_ms_goal_index = ms_goal_dnd_target_index;
                    } else if (selected_ms_goal_index > ms_goal_dnd_source_index && selected_ms_goal_index <=
                               ms_goal_dnd_target_index) {
                        selected_ms_goal_index--;
                    } else if (selected_ms_goal_index < ms_goal_dnd_source_index && selected_ms_goal_index >=
                               ms_goal_dnd_target_index) {
                        selected_ms_goal_index++;
                    }
                    save_message_type = MSG_NONE;
                }

                if (goal_to_remove != -1) {
                    if (selected_ms_goal_index == goal_to_remove) selected_ms_goal_index = -1;
                    else if (selected_ms_goal_index > goal_to_remove) selected_ms_goal_index--;
                    current_template_data.multi_stage_goals.erase(
                        current_template_data.multi_stage_goals.begin() + goal_to_remove);
                    save_message_type = MSG_NONE;
                }

                if (goal_to_copy != -1) {
                    const auto &source_goal = current_template_data.multi_stage_goals[goal_to_copy];
                    EditorMultiStageGoal new_goal = source_goal; // Deep copy
                    char base_name[192];
                    strncpy(base_name, source_goal.root_name, sizeof(base_name) - 1);
                    base_name[sizeof(base_name) - 1] = '\0';
                    char new_name[192];
                    int copy_counter = 1;
                    while (true) {
                        if (copy_counter == 1) snprintf(new_name, sizeof(new_name), "%s_copy", base_name);
                        else snprintf(new_name, sizeof(new_name), "%s_copy%d", base_name, copy_counter);
                        bool name_exists = false;
                        for (const auto &mg: current_template_data.multi_stage_goals) {
                            if (strcmp(mg.root_name, new_name) == 0) {
                                name_exists = true;
                                break;
                            }
                        }
                        if (!name_exists) break;
                        copy_counter++;
                    }
                    strncpy(new_goal.root_name, new_name, sizeof(new_goal.root_name) - 1);
                    current_template_data.multi_stage_goals.insert(
                        current_template_data.multi_stage_goals.begin() + goal_to_copy + 1, new_goal);
                    save_message_type = MSG_NONE;
                }

                ImGui::EndChild();
                ImGui::SameLine();

                ImGui::BeginChild("MSGoalDetailsPane", ImVec2(0, 0), true);
                if (selected_ms_goal_index != -1 && (size_t) selected_ms_goal_index < current_template_data.
                    multi_stage_goals.size()) {
                    auto &goal = current_template_data.multi_stage_goals[selected_ms_goal_index];

                    if (ImGui::InputText("Goal Root Name", goal.root_name, sizeof(goal.root_name))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::InputText("Icon Path", goal.icon_path, sizeof(goal.icon_path))) {
                        save_message_type = MSG_NONE;
                    }
                    if (ImGui::Checkbox("Hidden", &goal.is_hidden)) {
                        save_message_type = MSG_NONE;
                    }
                    ImGui::Separator();

                    ImGui::Text("Stages");
                    if (ImGui::Button("Add New Stage")) {
                        goal.stages.push_back({});
                        save_message_type = MSG_NONE;
                    }

                    int stage_to_remove = -1;
                    int stage_to_copy = -1;

                    int stage_dnd_source_index = -1;
                    int stage_dnd_target_index = -1;

                    // TODO: Only show "Unlock" in this list if the version is craftmine
                    const char *type_names[] = {"Stat", "Advancement", "Unlock", "Criterion", "Final"};

                    for (size_t j = 0; j < goal.stages.size(); ++j) {
                        auto &stage = goal.stages[j];
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
                        // ImVec2 item_start_cursor_pos = ImGui::GetCursorPos(); // TODO: Not needed?
                        ImGui::BeginGroup();
                        if (ImGui::InputText("Stage ID", stage.stage_id, sizeof(stage.stage_id))) {
                            save_message_type = MSG_NONE;
                        }
                        if (ImGui::Combo("Type", (int *) &stage.type, type_names, IM_ARRAYSIZE(type_names))) {
                            // Clear parent_advancement if type is not criterion
                            if (stage.type != SUBGOAL_CRITERION) {
                                stage.parent_advancement[0] = '\0';
                            }
                            save_message_type = MSG_NONE;
                        }

                        if (stage.type == SUBGOAL_CRITERION) {
                            if (ImGui::InputText("Parent Advancement", stage.parent_advancement,
                                                 sizeof(stage.parent_advancement))) {
                                save_message_type = MSG_NONE;
                            }
                        }

                        if (ImGui::InputText("Trigger Root Name", stage.root_name, sizeof(stage.root_name))) {
                            save_message_type = MSG_NONE;
                        }

                        if (stage.type != SUBGOAL_MANUAL) {
                            // "Final" stages don't need a target
                            if (ImGui::InputInt("Target Value", &stage.required_progress)) {
                                save_message_type = MSG_NONE;
                            }
                        }

                        if (ImGui::Button("Copy")) {
                            stage_to_copy = j;
                            save_message_type = MSG_NONE;
                        }

                        ImGui::SameLine();

                        if (ImGui::Button("Remove")) {
                            stage_to_remove = j;
                            save_message_type = MSG_NONE;
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
                        save_message_type = MSG_NONE;
                    }

                    if (stage_to_remove != -1) {
                        goal.stages.erase(goal.stages.begin() + stage_to_remove);
                        save_message_type = MSG_NONE;
                    }

                    // Logic to handle stage copy action after the loop
                    if (stage_to_copy != -1) {
                        const auto &source_stage = goal.stages[stage_to_copy];
                        EditorSubGoal new_stage = source_stage;
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
                        goal.stages.insert(goal.stages.begin() + stage_to_copy + 1, new_stage);
                        save_message_type = MSG_NONE;
                    }
                } else {
                    ImGui::Text("Select a multi-stage goal from the list to edit.");
                }
                ImGui::EndChild();
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
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "The main classification for the template (e.g., 'all_advancements', 'all_trims').\nCannot contain spaces or special characters.");

        ImGui::InputText("Optional Flag", new_template_flag, sizeof(new_template_flag));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "A variant for the category (e.g., '_optimized', '_modded').\nCannot contain spaces or special characters.");

        if (ImGui::Button("Create Files")) {
            if (new_template_version_idx >= 0) {
                char error_msg[256] = "";

                if (validate_and_create_template(creator_version_str, new_template_category, new_template_flag,
                                                 error_msg,
                                                 sizeof(error_msg))) {
                    snprintf(status_message, sizeof(status_message), "Success! Template '%s' created.",
                             new_template_category);
                    show_create_new_view = false;
                    // Force a rescan by clearing the last scanned version
                    SDL_SetAtomicInt(&g_templates_changed, 1); // Signal change
                    last_scanned_version[0] = '\0';
                } else {
                    strncpy(status_message, error_msg, sizeof(status_message) - 1);
                }
            } else {
                strncpy(status_message, "Error: A version must be selected.", sizeof(status_message) - 1);
            }
        }
    }

    // "Copy" Form
    else if (show_copy_view) {
        ImGui::Text("Copy Template");

        ImGui::Spacing();

        if (selected_template_index != -1) {
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            ImGui::Text("Copying from: %s", selected.category);
        }

        if (ImGui::Combo("New Version", &copy_template_version_idx, VERSION_STRINGS, VERSION_STRINGS_COUNT)) {
        }
        ImGui::InputText("New Category Name", copy_template_category, sizeof(copy_template_category));
        ImGui::InputText("New Optional Flag", copy_template_flag, sizeof(copy_template_flag));

        if (ImGui::Button("Confirm Copy")) {
            if (selected_template_index != -1 && copy_template_version_idx >= 0) {
                const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
                const char *dest_version = VERSION_STRINGS[copy_template_version_idx];
                char error_msg[256] = "";

                if (copy_template_files(app_settings->version_str, selected.category, selected.optional_flag,
                                        dest_version, copy_template_category, copy_template_flag,
                                        error_msg, sizeof(error_msg))) {
                    snprintf(status_message, sizeof(status_message), "Success! Template copied to '%s'.",
                             copy_template_category);
                    show_copy_view = false;
                    SDL_SetAtomicInt(&g_templates_changed, 1); // Signal change
                    last_scanned_version[0] = '\0'; // Force rescan
                } else {
                    strncpy(status_message, error_msg, sizeof(status_message) - 1);
                }
            }
        }
    }

    ImGui::EndChild();

    if (roboto_font) {
        ImGui::PopFont();
    }
    ImGui::End();
}
