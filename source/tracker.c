//
// Created by Linus on 24.06.2025.
//

#include <stdio.h>
#include <cJSON.h>

#include "tracker.h"
#include "init_sdl.h"
#include "path_utils.h"
#include "settings_utils.h"
#include "file_utils.h" // has the cJSON_from_file function
#include "temp_create_utils.h"

// HELPER FUNCTION FOR PARSING
// TODO: Move these static functions into tracker_utils.c (then they are not static anymore)
/**
 * @brief Parses advancement categories and their criteria from the template, supporting modded advancements.
 */
static void parse_trackable_categories(cJSON *category_json, cJSON *lang_json, struct Tracker *t) {
    if (!category_json) {
        printf("[TRACKER] parse_trackable_categories: category_json is NULL\n");
        return;
    }

    t->template_data->advancement_count = 0;
    cJSON *count_item = NULL;
    if (category_json) {
        // count the number of advancements
        for (count_item = category_json->child; count_item != NULL; count_item = count_item->next) {
            t->template_data->advancement_count++;
        }
    }
    // count the number of advancements
    if (t->template_data->advancement_count == 0) {
        printf("[TRACKER] parse_trackable_categories: No advancements found in template.\n");
        return;
    }

    t->template_data->advancements = calloc(t->template_data->advancement_count, sizeof(TrackableCategory *));
    // * because it's an array
    if (!t->template_data->advancements) return; // allocation failed for advancements

    cJSON *cat_json = category_json->child;
    int i = 0;
    t->template_data->total_criteria_count = 0; // Initialize total criteria count

    while (cat_json) {
        TrackableCategory *new_cat = calloc(1, sizeof(TrackableCategory));
        if (new_cat) {
            strncpy(new_cat->root_name, cat_json->string, sizeof(new_cat->root_name) - 1);

            // TODO: Placeholder for getting display name and icon
            strncpy(new_cat->display_name, new_cat->root_name, sizeof(new_cat->display_name) - 1);

            // Build the language key from the root_name, e.g., "conquest:story/smelt_iron" -> "advancement.conquest.story.smelt_iron"
            char lang_key[256] = {0};
            char temp_root_name[192];
            strncpy(temp_root_name, new_cat->root_name, sizeof(temp_root_name) - 1);

            char *path_part = strchr(temp_root_name, ':');
            if (path_part) {
                // Found a namespace, replace ':' with '.'
                *path_part = '.';
                path_part++;
            } else {
                // No namespace found, treat the whole thing as a path
                path_part = temp_root_name;
            }

            // Construct the full key
            snprintf(lang_key, sizeof(lang_key), "advancement.%s", temp_root_name);

            // replace all slashes in the path with dots
            for (char *p = lang_key; *p; p++) {
                if (*p == '/') {
                    *p = '.';
                }
            }

            cJSON *lang_entry = cJSON_GetObjectItem(lang_json, lang_key);
            if (cJSON_IsString(lang_entry))
                strncpy(new_cat->display_name, lang_entry->valuestring,
                        sizeof(new_cat->display_name) - 1);
            else strncpy(new_cat->display_name, new_cat->root_name, sizeof(new_cat->display_name) - 1);

            cJSON *icon = cJSON_GetObjectItem(cat_json, "icon");
            if (cJSON_IsString(icon)) strncpy(new_cat->icon_path, icon->valuestring, sizeof(new_cat->icon_path) - 1);

            // Parse criteria
            cJSON *criteria_obj = cJSON_GetObjectItem(cat_json, "criteria");
            if (criteria_obj) {
                cJSON *crit_item = NULL;
                for (crit_item = criteria_obj->child; crit_item != NULL; crit_item = crit_item->next) {
                    new_cat->criteria_count++;
                }

                // count the number of criteria
                if (new_cat->criteria_count > 0) {
                    // if there are criteria
                    new_cat->criteria = calloc(new_cat->criteria_count, sizeof(TrackableItem *));
                    t->template_data->total_criteria_count += new_cat->criteria_count; // add to total criteria count
                    int k = 0;
                    for (crit_item = criteria_obj->child; crit_item != NULL; crit_item = crit_item->next) {
                        // for each criterion
                        TrackableItem *new_crit = calloc(1, sizeof(TrackableItem));
                        if (new_crit) {
                            strncpy(new_crit->root_name, crit_item->string, sizeof(new_crit->root_name) - 1);

                            // Parse the icon from the criterion's value object
                            if (cJSON_IsObject(crit_item)) {
                                cJSON *icon = cJSON_GetObjectItem(crit_item, "icon");
                                if (cJSON_IsString(icon)) {
                                    strncpy(new_crit->icon_path, icon->valuestring, sizeof(new_crit->icon_path) - 1);
                                }
                            }

                            // Look up the criterion's display name from the lang files
                            char crit_lang_key[256] = {0};

                            snprintf(crit_lang_key, sizeof(crit_lang_key), "%s.criteria.%s", lang_key,
                                     new_crit->root_name);

                            // Get the criterion's display name from the language file
                            cJSON *lang_entry = cJSON_GetObjectItem(lang_json, crit_lang_key);
                            if (cJSON_IsString(lang_entry)) {
                                strncpy(new_crit->display_name, lang_entry->valuestring,
                                        sizeof(new_crit->display_name) - 1);
                            } else {
                                // Fallback to the root_name if no translation is found
                                strncpy(new_crit->display_name, new_crit->root_name,
                                        sizeof(new_crit->display_name) - 1);
                            }

                            new_cat->criteria[k++] = new_crit; // Add the new criterion to the criteria array
                        }
                    }
                }
            }
            t->template_data->advancements[i++] = new_cat; // Add the new category to the advancements array
        }
        cat_json = cat_json->next; // Move to the next category
    }
}

/**
 * @brief Detects criteria that are shared across multiple advancements and flags them.
 *
 * This function iterates through all parsed advancements and their criteria to identify
 * criteria that have the same root_name. If a criterion is found in more than one
 * advancement, its 'is_shared' flag is set to true. This allows the rendering
 * logic to visually distinguish them, for example, by overlaying the parent
 * advancement's icon.
 *
 * @param t A pointer to the Tracker struct containing the template_data.
 */
static void tracker_detect_shared_criteria(struct Tracker *t) {
    if (!t || !t->template_data || t->template_data->advancement_count == 0) {
        printf("[TRACKER] tracker_detect_shared_criteria: t or t->template_data is NULL\n");
        return;
    }

    // A temporary structure to hold counts of each criterion
    typedef struct {
        char root_name[192];
        int count;
    } CriterionCounter;

    // Use a dynamic array to store the counters
    int capacity = t->template_data->total_criteria_count;
    if (capacity == 0) return; // No criteria to check

    CriterionCounter *counts = malloc(capacity * sizeof(CriterionCounter));
    if (!counts) {
        fprintf(stderr, "[TRACKER] Failed to allocate memory for criterion counters.\n");
        return;
    }
    int unique_criteria_count = 0;

    // Count all criteria occurences
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *adv = t->template_data->advancements[i];
        for (int j = 0; j < adv->criteria_count; j++) {
            TrackableItem *crit = adv->criteria[j];
            bool found = false;

            // Check if we've already counted this criterion name
            for (int k = 0; k < unique_criteria_count; k++) {
                // compare current root_name with all other root_names
                if (strcmp(counts[k].root_name, crit->root_name) == 0) {
                    counts[k].count++;
                    found = true;
                    break;
                }
            }

            // If not found, add it as a new entry
            if (!found) {
                strncpy(counts[unique_criteria_count].root_name, crit->root_name, 191);
                counts[unique_criteria_count].count = 1;
                unique_criteria_count++;
            }
        }
    }

    // Flag the shared criteria is_shared flag
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        // Loop through advancements
        TrackableCategory *adv = t->template_data->advancements[i];
        for (int j = 0; j < adv->criteria_count; j++) {
            // Loop through criteria
            TrackableItem *crit = adv->criteria[j];

            // Find our criterion in the counters
            for (int k = 0; k < unique_criteria_count; k++) {
                if (strcmp(counts[k].root_name, crit->root_name) == 0) {
                    if (counts[k].count > 1) {
                        crit->is_shared = true; // Set the is_shared flag
                    }
                    break;
                }
            }
        }
    }
    // Clean up the temporary counter
    free(counts);
    counts = NULL;
    printf("[TRACKER] Shared criteria detection complete.\n");
}

/**
 * @brief Parses a cJSON array of simple trackable items (like stats or unlocks) into an array of TrackableItem structs.
 *
 * This function iterates through a JSON array, allocating and populating a TrackableItem for each entry.
 * It extracts the root name, icon path, and goal value from the template and looks up the display name in the language file.
 *
 * @param category_json The cJSON array for the "stats" or "unlocks" key from the template file.
 * @param lang_json The cJSON object from the language file to look up display names.
 * @param items_array A pointer to the array of TrackableItem pointers to be populated.
 * @param count A pointer to an integer that will store the number of items parsed.
 */
static void parse_simple_trackables(cJSON *category_json, cJSON *lang_json, TrackableItem ***items_array, int *count) {
    if (!category_json) {
        printf("[TRACKER] parse_simple_trackables: category_json is NULL\n");
        return;
    }
    *count = cJSON_GetArraySize(category_json);
    if (*count == 0) {
        printf("[TRACKER] parse_simple_trackables: No items found\n");
        return;
    }

    *items_array = calloc(*count, sizeof(TrackableItem *));
    if (!*items_array) return;

    cJSON *item_json = NULL;
    int i = 0;
    cJSON_ArrayForEach(item_json, category_json) {
        TrackableItem *new_item = calloc(1, sizeof(TrackableItem));
        if (new_item) {
            cJSON *root_name_json = cJSON_GetObjectItem(item_json, "root_name");
            if (cJSON_IsString(root_name_json)) {
                strncpy(new_item->root_name, root_name_json->valuestring, sizeof(new_item->root_name) - 1);
            } else {
                // Skip this item if it has no root_name
                free(new_item);
                continue;
            }

            // Get display name from lang file
            cJSON *lang_entry = cJSON_GetObjectItem(lang_json, new_item->root_name);
            if (cJSON_IsString(lang_entry)) {
                strncpy(new_item->display_name, lang_entry->valuestring, sizeof(new_item->display_name) - 1);
            } else {
                strncpy(new_item->display_name, new_item->root_name, sizeof(new_item->display_name) - 1);
            }

            // Get other properties from the template
            cJSON *icon = cJSON_GetObjectItem(item_json, "icon");
            if (cJSON_IsString(icon)) {
                strncpy(new_item->icon_path, icon->valuestring, sizeof(new_item->icon_path) - 1);
            }

            cJSON *target = cJSON_GetObjectItem(item_json, "target");
            if (cJSON_IsNumber(target)) {
                new_item->goal = target->valueint;
            }

            (*items_array)[i++] = new_item;
        }
    }
}

/**
 * @brief Parses the "multi_stage_goals" from the template JSON file.
 *
 * This function reads the multi_stage_goals array from the template, creating a
 * MultiStageGoal for each entry and parsing its corresponding sequence of sub-goal stages.
 *
 * @param goals_json The cJSON object for the "multi_stage_goals" key from the template file.
 * @param lang_json The cJSON object from the language file (not used here but kept for consistency).
 * @param goals_array A pointer to the array of MultiStageGoal pointers to be populated.
 * @param count A pointer to an integer that will store the number of goals parsed.
 */
static void parse_multi_stage_goals(cJSON *goals_json, cJSON *lang_json, MultiStageGoal ***goals_array, int *count) {
    (void) lang_json;
    if (!goals_json) {
        printf("[TRACKER] parse_multi_stage_goals: goals_json is NULL\n");
        *count = 0;
        // goals_array = NULL;
        return;
    }

    *count = cJSON_GetArraySize(goals_json);
    if (*count == 0) {
        printf("[TRACKER] parse_multi_stage_goals: No goals found\n");
        // goals_array = NULL;
        return;
    }

    *goals_array = calloc(*count, sizeof(MultiStageGoal *));
    if (!*goals_array) {
        fprintf(stderr, "[TRACKER] Failed to allocate memory for MultiStageGoal array.\n");
        *count = 0;
        return;
    }

    cJSON *goal_item_json = NULL;
    int i = 0;
    cJSON_ArrayForEach(goal_item_json, goals_json) {
        // Iterate through each goal
        MultiStageGoal *new_goal = calloc(1, sizeof(MultiStageGoal));
        if (!new_goal) continue;

        // Parse root_name and icon
        cJSON *root_name = cJSON_GetObjectItem(goal_item_json, "root_name");
        cJSON *icon = cJSON_GetObjectItem(goal_item_json, "icon");

        if (cJSON_IsString(root_name))
            strncpy(new_goal->root_name, root_name->valuestring,
                    sizeof(new_goal->root_name) - 1);
        if (cJSON_IsString(icon)) strncpy(new_goal->icon_path, icon->valuestring, sizeof(new_goal->icon_path) - 1);


        // Look up display name from lang file
        char goal_lang_key[256];
        snprintf(goal_lang_key, sizeof(goal_lang_key), "multi_stage_goal.%s.display_name", new_goal->root_name);
        cJSON *goal_lang_entry = cJSON_GetObjectItem(lang_json, goal_lang_key);

        // If the display name is not found in the lang file, use the root name
        if (cJSON_IsString(goal_lang_entry)) {
            strncpy(new_goal->display_name, goal_lang_entry->valuestring, sizeof(new_goal->display_name) - 1);
        } else {
            strncpy(new_goal->display_name, new_goal->root_name, sizeof(new_goal->display_name) - 1);
        }

        // Parse stages
        cJSON *stages_json = cJSON_GetObjectItem(goal_item_json, "stages");
        new_goal->stage_count = cJSON_GetArraySize(stages_json);
        if (new_goal->stage_count > 0) {
            // Allocate memory for the stages array
            new_goal->stages = calloc(new_goal->stage_count, sizeof(SubGoal *));
            if (!new_goal->stages) {
                free(new_goal);
                continue;
            }

            cJSON *stage_item_json = NULL;
            int j = 0;
            cJSON_ArrayForEach(stage_item_json, stages_json) {
                SubGoal *new_stage = calloc(1, sizeof(SubGoal));
                if (!new_stage) continue;

                // parse stage_id and other properties
                cJSON *text = cJSON_GetObjectItem(stage_item_json, "display_text");
                cJSON *stage_id = cJSON_GetObjectItem(stage_item_json, "stage_id");
                cJSON *type = cJSON_GetObjectItem(stage_item_json, "type");
                cJSON *root = cJSON_GetObjectItem(stage_item_json, "root_name");
                cJSON *target_val = cJSON_GetObjectItem(stage_item_json, "target");

                if (cJSON_IsString(text))
                    strncpy(new_stage->display_text, text->valuestring,
                            sizeof(new_stage->display_text) - 1);
                if (cJSON_IsString(stage_id)) strncpy(new_stage->stage_id, stage_id->valuestring,
                                                      sizeof(new_stage->stage_id) - 1);
                if (cJSON_IsString(root))
                    strncpy(new_stage->root_name, root->valuestring,
                            sizeof(new_stage->root_name) - 1);
                if (cJSON_IsNumber(target_val)) new_stage->required_progress = target_val->valueint; // This is a number


                // Look up stage display name from lang file
                char stage_lang_key[256];
                snprintf(stage_lang_key, sizeof(stage_lang_key), "multi_stage_goal.%s.stage.%s", new_goal->root_name,
                         new_stage->stage_id);
                cJSON *stage_lang_entry = cJSON_GetObjectItem(lang_json, stage_lang_key);
                // take stage key and search in lang file

                // If the display name is not found in the lang file, use the stage ID
                if (cJSON_IsString(stage_lang_entry)) {
                    strncpy(new_stage->display_text, stage_lang_entry->valuestring,
                            sizeof(new_stage->display_text) - 1);
                } else {
                    strncpy(new_stage->display_text, new_stage->stage_id,
                            sizeof(new_stage->display_text) - 1); // Fallback
                }

                // Parse type
                if (cJSON_IsString(type)) {
                    if (strcmp(type->valuestring, "stat") == 0) new_stage->type = SUBGOAL_STAT;
                    else if (strcmp(type->valuestring, "advancement") == 0) new_stage->type = SUBGOAL_ADVANCEMENT;
                    else if (strcmp(type->valuestring, "unlock") == 0) new_stage->type = SUBGOAL_UNLOCK;
                    else new_stage->type = SUBGOAL_MANUAL; // "final" maps to manual/unused
                }
                // Add the stage to the goal
                new_goal->stages[j++] = new_stage;
            }
        }
        // Add the goal to the array
        (*goals_array)[i++] = new_goal;
    }
}

/**
 * @brief Updates advancement and criteria progress from a pre-loaded cJSON object.
 * @param t A pointer to the Tracker struct.
 * @param player_adv_json The parsed player advancements JSON file.
 */
static void tracker_update_advancement_progress(struct Tracker *t, const cJSON *player_adv_json) {
    if (!player_adv_json) return;

    t->template_data->advancements_completed_count = 0;
    t->template_data->completed_criteria_count = 0;

    // printf("[TRACKER] Reading player advancements from: %s\n", t->advancements_path);

    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *adv = t->template_data->advancements[i]; // Get the current advancement
        adv->completed_criteria_count = 0; // Reset completed criteria count for this advancement

        cJSON *player_entry = cJSON_GetObjectItem(player_adv_json, adv->root_name);
        // Get the entry for this advancement
        if (player_entry) {
            adv->done = cJSON_IsTrue(cJSON_GetObjectItem(player_entry, "done"));
            if (adv->done) {
                t->template_data->advancements_completed_count++;
            }

            // Criteria don't have a "done" field, so we just check if the entry exists
            cJSON *player_criteria = cJSON_GetObjectItem(player_entry, "criteria");
            if (player_criteria) {
                // If the entry exists, we have criteria
                for (int j = 0; j < adv->criteria_count; j++) {
                    TrackableItem *crit = adv->criteria[j];
                    // Check if the criteria exist -> meaning it's been completed
                    if (cJSON_HasObjectItem(player_criteria, crit->root_name)) {
                        crit->done = true;
                        adv->completed_criteria_count++;
                    } else {
                        crit->done = false;
                    }
                }
            }
        } else {
            // If the entry doesn't exist, reset everything
            adv->done = false;
            for (int j = 0; j < adv->criteria_count; j++) {
                adv->criteria[j]->done = false;
            }
        }
        // Update completed criteria count for this advancement
        t->template_data->completed_criteria_count += adv->completed_criteria_count;
    }
}

/**
 * @brief Updates unlock progress from a pre-loaded cJSON object and counts completed unlocks.
 * @param t A pointer to the Tracker struct.
 * @param player_unlocks_json The parsed player unlocks JSON file.
 */
static void tracker_update_unlock_progress(struct Tracker *t, const cJSON *player_unlocks_json) {
    if (!player_unlocks_json) return;

    // printf("[TRACKER] Reading player unlocks from: %s\n", t->unlocks_path);

    cJSON *obtained_obj = cJSON_GetObjectItem(player_unlocks_json, "obtained"); // Top level object in unlocks file
    if (!obtained_obj) {
        fprintf(stderr, "[TRACKER] Failed to find 'obtained' object in player unlocks file.\n");
        return;
    }

    t->template_data->unlocks_completed_count = 0;

    for (int i = 0; i < t->template_data->unlock_count; i++) {
        TrackableItem *unlock = t->template_data->unlocks[i];
        cJSON *unlock_status = cJSON_GetObjectItem(obtained_obj, unlock->root_name);

        // Check if the unlock is obtained
        if (cJSON_IsTrue(unlock_status)) {
            unlock->done = true;
            t->template_data->unlocks_completed_count++;
        } else {
            unlock->done = false;
        }
    }
}


/**
 * @brief Updates stat progress from a pre-loaded cJSON object. Supports modded stats. No hardcoded "minecraft:".
 * @param t A pointer to the Tracker struct.
 * @param player_stats_json The parsed player stats JSON file.
 */
static void tracker_update_stat_progress(struct Tracker *t, const cJSON *player_stats_json,
                                         const cJSON *settings_json) {
    if (!player_stats_json) return;

    // printf("[TRACKER] Reading player stats from: %s\n", t->stats_path);


    cJSON *stats_obj = cJSON_GetObjectItem(player_stats_json, "stats");
    if (!stats_obj) {
        fprintf(stderr, "[TRACKER] Failed to find 'stats' object in player stats file.\n");
        return;
    }

    // MANUAL OVERRIDE FOR STATS GOAL
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : NULL;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableItem *stat = t->template_data->stats[i];

        stat->progress = 0; // Default to 0
        stat->is_manually_completed = false;

        // The root_name is now in the format "full:category/full:item", e.g., "minecraft:mined/minecraft:dirt"
        char root_name_copy[192];
        strncpy(root_name_copy, stat->root_name, sizeof(root_name_copy) - 1);
        root_name_copy[sizeof(root_name_copy) - 1] = '\0'; // Ensure null termination

        // Find the separator '/' to distinguish category and item

        char *item_key = strchr(root_name_copy, '/');
        if (item_key) {
            *item_key = '\0'; // Split the string with null terminator
            item_key++; // Move pointer to the start of the item key

            char *category_key = root_name_copy;

            // Look up the category (e.g., "minecraft:mined")
            cJSON *category_obj = cJSON_GetObjectItem(stats_obj, category_key);
            if (category_obj) {
                // Look up the item within that category (e.g., "minecraft:dirt")

                // Check if the stat exists in the category
                cJSON *stat_value = cJSON_GetObjectItem(category_obj, item_key);
                if (cJSON_IsNumber(stat_value)) {
                    stat->progress = stat_value->valueint;
                }
            }
        }

        if (override_obj) {
            cJSON *override_status = cJSON_GetObjectItem(override_obj, stat->root_name);
            if (cJSON_IsBool(override_status)) {
                if (cJSON_IsTrue(override_status)) {
                    stat->done = true;
                    stat->is_manually_completed = true;
                }
            }
        }

        // If not manually completed, determine 'done' status by progress
        if (!stat->is_manually_completed) {
            stat->done = (stat->goal > 0 && stat->progress >= stat->goal);
        }
    }
}

/**
 * @brief Updates the 'done' status of custom goals by reading from the settings file.
 * @param t A pointer to the Tracker struct.
 * @param settings_json A pointer to the parsed settings.json cJSON object.
 */
static void tracker_update_custom_progress(struct Tracker *t, cJSON *settings_json) {
    if (!settings_json) {
        printf("[TRACKER] Failed to load or parse settings file.\n");
        return;
    }

    cJSON *progress_obj = cJSON_GetObjectItem(settings_json, "custom_progress");
    if (!cJSON_IsObject(progress_obj)) {
        return; // NO custom progress saved yet, which is fine
    }

    // Iterate through the custom goals
    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        TrackableItem *item = t->template_data->custom_goals[i];
        cJSON *item_progress_json = cJSON_GetObjectItem(progress_obj, item->root_name);

        // Check if the goal in the template has a target defined
        if (item->goal > 0) {
            // It's a COUNTER goal
            item->progress = cJSON_IsNumber(item_progress_json) ? item_progress_json->valueint : 0;
            item->done = (item->progress >= item->goal);
        } else {
            // It's a simple TOGGLE goal
            item->done = cJSON_IsTrue(item_progress_json);
            item->progress = item->done ? 1 : 0; // Set progress for consistency
        }
    }
}

/**
 * @brief Updates multi-stage goal progress from preloaded cJSON objects.
 * @param t A pointer to the Tracker struct.
 * @param player_adv_json The parsed player advancements JSON file.
 * @param player_stats_json The parsed player stats JSON file.
 * @param player_unlocks_json The parsed player unlocks JSON file.
 */
static void tracker_update_multi_stage_progress(struct Tracker *t, const cJSON *player_adv_json,
                                                const cJSON *player_stats_json, const cJSON *player_unlocks_json) {
    if (t->template_data->multi_stage_goal_count == 0) return;

    if (!player_adv_json && !player_stats_json) {
        printf(
            "[TRACKER] Failed to load or parse player advancements or player stats file to update multi-stage goal progress.\n");
        return;
    }

    cJSON *stats_obj = player_stats_json ? cJSON_GetObjectItem(player_stats_json, "stats") : NULL;

    // Iterate through the multi-stage goals
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];

        // Reset and re-evaluate progress from the start (important for world-changes when tracker is running)
        goal->current_stage = 0;

        // Loop through each stage sequentially to determine the current progress
        for (int j = 0; j < goal->stage_count; j++) {
            SubGoal *stage_to_check = goal->stages[j];
            bool stage_completed = false; // Default to false

            stage_to_check->current_stat_progress = 0;

            // Stop checking if we are on the final stage (SUBGOAL_MANUAL)
            if (stage_to_check->type == SUBGOAL_MANUAL) {
                break;
            }

            switch (stage_to_check->type) {
                case SUBGOAL_ADVANCEMENT:
                    // We take the item with the root name then check if it is done then call this stage completed
                    if (player_adv_json) {
                        cJSON *adv_entry = cJSON_GetObjectItem(player_adv_json, stage_to_check->root_name);
                        // Get the entry for this advancement
                        if (adv_entry && cJSON_IsTrue(cJSON_GetObjectItem(adv_entry, "done"))) {
                            stage_completed = true;
                        }
                    }
                    break;

                case SUBGOAL_STAT:
                    if (stats_obj) {
                        // Handle format "full:category/full:item"
                        char root_name_copy[192];
                        strncpy(root_name_copy, stage_to_check->root_name, sizeof(root_name_copy) - 1);
                        root_name_copy[sizeof(root_name_copy) - 1] = '\0'; // Ensure null termination

                        char *item_key = strchr(root_name_copy, '/');
                        if (item_key) {
                            *item_key = '\0'; // Split the string
                            item_key++;
                            char *category_key = root_name_copy;

                            // Check if the category exists
                            cJSON *category_obj = cJSON_GetObjectItem(stats_obj, category_key);
                            if (category_obj) {
                                cJSON *stat_value = cJSON_GetObjectItem(category_obj, item_key);
                                if (cJSON_IsNumber(stat_value)) {
                                    stage_to_check->current_stat_progress = stat_value->valueint;
                                    if (stage_to_check->current_stat_progress >= stage_to_check->required_progress) {
                                        stage_completed = true;
                                    }
                                }
                            }
                        }
                    }
                    break;

                case SUBGOAL_UNLOCK:
                    if (player_unlocks_json) {
                        cJSON *obtained_obj = cJSON_GetObjectItem(player_unlocks_json, "obtained");
                        if (obtained_obj) {
                            cJSON *unlock_status = cJSON_GetObjectItem(obtained_obj, stage_to_check->root_name);
                            if (cJSON_IsTrue(unlock_status)) {
                                stage_completed = true;
                            }
                        }
                    }
                case SUBGOAL_MANUAL: // Already handled above, when string in subgoal ISN'T "stat" or "advancement"
                default:
                    break; // Manual stages are not updated here
            }

            if (stage_completed) {
                goal->current_stage = j + 1; // Move to the next stage
            } else {
                break;
            }
        }
    }
}


/**
 * @brief Calculates the overall progress percentage based on all tracked items. Advancements are separately!!
 *
 * It first calculates the total number of "steps" (e.g., criteria, stats, unlocks, custom goals, and multi-stage goals),
 * then the number of completed "steps", and finally calculates the overall progress percentage.
 *
 * @param t A pointer to the Tracker struct.
 *
 */
static void tracker_calculate_overall_progress(struct Tracker *t) {
    if (!t || !t->template_data) return; // || because we can't be sure if the template_data is initialized

    // calculate the total number of "steps"
    int total_steps = 0;
    // INCLUDES SUB-CRITERIA PROGRESS
    total_steps += t->template_data->total_criteria_count;
    total_steps += t->template_data->stat_count;
    total_steps += t->template_data->unlock_count;
    total_steps += t->template_data->custom_goal_count;
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        // Each stage (except the last) is a step
        total_steps += (t->template_data->multi_stage_goals[i]->stage_count - 1);
    }

    // Calculate the number of completed "steps"
    int completed_steps = 0;
    completed_steps += t->template_data->completed_criteria_count;
    completed_steps += t->template_data->unlocks_completed_count;
    for (int i = 0; i < t->template_data->stat_count; i++) {
        if (t->template_data->stats[i]->done) completed_steps++;
    }
    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        if (t->template_data->custom_goals[i]->done) completed_steps++;
    }
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        completed_steps += t->template_data->multi_stage_goals[i]->current_stage;
    }

    // Calculate the overall progress percentage
    if (total_steps > 0) {
        t->template_data->overall_progress_percentage = ((float) completed_steps / (float) total_steps) * 100.0f;
    } else {
        t->template_data->overall_progress_percentage = 0.0f;
    }
}


/**
 * @brief Frees all dynamically allocated memory within a TemplateData struct.
 *
 * To avoid memory leaks when switching templates during runtime.
 * It only frees the CONTENT of the TemplateData struct NOT the struct itself.
 *
 * @param td A pointer to the TemplateData struct to be freed.
 */
static void tracker_free_template_data(TemplateData *td) {
    if (!td) return;

    // Free advancements data
    for (int i = 0; i < td->advancement_count; i++) {
        if (td->advancements[i]) {
            for (int j = 0; j < td->advancements[i]->criteria_count; j++) {
                free(td->advancements[i]->criteria[j]);
            }
            free(td->advancements[i]->criteria);
            free(td->advancements[i]);
        }
    }
    free(td->advancements);


    // Free stats data
    for (int i = 0; i < td->stat_count; i++) {
        free(td->stats[i]);
    }
    free(td->stats);

    // Free unlocks data
    for (int i = 0; i < td->unlock_count; i++) {
        free(td->unlocks[i]);
    }
    free(td->unlocks);

    // Free custom goal data
    if (td->custom_goals) {
        for (int i = 0; i < td->custom_goal_count; i++) {
            free(td->custom_goals[i]);
        }
        free(td->custom_goals);
    }

    // Free multi-stage goals data
    if (td->multi_stage_goals) {
        for (int i = 0; i < td->multi_stage_goal_count; i++) {
            MultiStageGoal *goal = td->multi_stage_goals[i];
            if (goal) {
                for (int j = 0; j < goal->stage_count; j++) {
                    free(goal->stages[j]);
                }
                free(goal->stages);
                free(goal);
            }
        }
        free(td->multi_stage_goals);
    }
}

void settings_save_custom_progress(TemplateData *td) {
    if (!td) return;

    // Read the existing settings file
    cJSON *settings_json = cJSON_from_file(SETTINGS_FILE_PATH);
    if (!settings_json) {
        // If this file doesn't exist, create it
        settings_json = cJSON_CreateObject();
        if (!settings_json) {
            fprintf(stderr, "[TRACKER] Failed to create settings file.\n");
            return;
        }

        // Get or create the custom_progress object
        cJSON *progress_obj = cJSON_GetObjectItem(settings_json, "custom_progress");
        if (!progress_obj) {
            progress_obj = cJSON_AddObjectToObject(settings_json, "custom_progress");
        }

        // Update the progress for each custom goal
        for (int i = 0; i < td->custom_goal_count; i++) {
            TrackableItem *item = td->custom_goals[i];
            cJSON *item_json = cJSON_GetObjectItem(progress_obj, item->root_name);

            // Update the progress
            if (item_json) {
                cJSON_SetNumberValue(item_json, item->progress);
            } else { // If it doesn't exist, add it
                cJSON_AddNumberToObject(progress_obj, item->root_name, item->progress);
            }

        }

        // Write the modified JSON back to the file
        FILE *file = fopen(SETTINGS_FILE_PATH, "w");
        if (file) {
            char *json_str = cJSON_Print(settings_json); // render the cJSON object to text
            fputs(json_str, file); // write to the file
            fclose(file);
            free(json_str);
        } else {
            fprintf(stderr, "[TRACKER] Failed to open settings file for writing.\n");
        }

    }

    cJSON_Delete(settings_json);

}


bool tracker_new(struct Tracker **tracker) {
    // Allocate memory for the tracker struct itself
    *tracker = calloc(1, sizeof(struct Tracker));
    if (*tracker == NULL) {
        fprintf(stderr, "[TRACKER] Failed to allocate memory for tracker.\n");
        return false;
    }

    struct Tracker *t = *tracker;

    // Initialize SDL components for the tracker
    if (!tracker_init_sdl(t)) {
        free(t);
        *tracker = NULL;
        tracker = NULL;
        return false;
    }

    // Allocate the main data container, TODO: Give user another chance to select a different template
    t->template_data = calloc(1, sizeof(TemplateData));
    if (!t->template_data) {
        fprintf(stderr, "[TRACKER] Failed to allocate memory for template data.\n");

        if (t->renderer) {
            SDL_DestroyRenderer(t->renderer);
            t->renderer = NULL;
        }
        if (t->window) {
            SDL_DestroyWindow(t->window);
            t->window = NULL;
        }

        free(t);
        *tracker = NULL;
        tracker = NULL;
        return false;
    }

    // Initialize paths (also during runtime)
    tracker_reinit_paths(t);

    // Parse the advancement template JSON file
    tracker_load_and_parse_data(t);


    return true; // Success
}

void tracker_events(struct Tracker *t, SDL_Event *event, bool *is_running, bool *settings_opened) {
    (void) t;

    switch (event->type) {
        // This should be handled in the global event handler
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            *is_running = false;
            break;

        case SDL_EVENT_KEY_DOWN:
            if (event->key.repeat == 0) {
                switch (event->key.scancode) {
                    case SDL_SCANCODE_ESCAPE:
                        // printf("[TRACKER] Escape key pressed in tracker: Opening settings window now.\n");
                        // Open settings window, TOGGLE settings_opened
                        *settings_opened = !(*settings_opened);
                        break;
                    default:
                        break;
                }
            }
            break;
        // TODO: Work with mouse events
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            //printf("[TRACKER] Mouse button pressed in tracker.\n");
            // TODO: Where toggling custom goals will be implemented, create new settings_save() function
            // 1. Get mouse coordinates: event->button.x, event->button.y

            // 2. Loop through all custom goals and check if the mouse is inside the bounding box of a goal's checkbox.
            //    (This requires you to store the positions of rendered elements)

            // 3. If a match is found, toggle the goal's status:
            //    t->template_data->custom_items[i]->done = !t->template_data->custom_items[i]->done;

            // 4. Immediately save the changes back to settings.json
            //    settings_save(t->template_data); // A new function you'll create


            break;
        case SDL_EVENT_MOUSE_MOTION:
            // printf("[TRACKER] Mouse moved in tracker.\n");
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            // printf("[TRACKER] Mouse button released in tracker.\n");
            break;
        default:
            break;
    }
}

// Periodically recheck file changes
void tracker_update(struct Tracker *t, float *deltaTime) {
    // Use deltaTime for animations
    // game logic goes here
    (void) deltaTime;

    // Load all necessary player files ONCE
    cJSON *player_adv_json = (strlen(t->advancements_path) > 0) ? cJSON_from_file(t->advancements_path) : NULL;
    cJSON *player_stats_json = (strlen(t->stats_path) > 0) ? cJSON_from_file(t->stats_path) : NULL;
    cJSON *player_unlocks_json = (strlen(t->unlocks_path) > 0) ? cJSON_from_file(t->unlocks_path) : NULL;
    cJSON *settings_json = cJSON_from_file(SETTINGS_FILE_PATH);

    // Pass the parsed data to the update functions
    tracker_update_advancement_progress(t, player_adv_json);
    tracker_update_unlock_progress(t, player_unlocks_json);
    tracker_update_stat_progress(t, player_stats_json, settings_json);
    tracker_update_custom_progress(t, settings_json);
    tracker_update_multi_stage_progress(t, player_adv_json, player_stats_json, player_unlocks_json);

    // Calculate the final overall progress
    // Advancements themselves are seperate, but THIS TRACKS SUB-ADVANCEMENTS AND EVERYTHING ELSE
    tracker_calculate_overall_progress(t);

    // Clean up the parsed JSON objects
    cJSON_Delete(player_adv_json);
    cJSON_Delete(player_stats_json);
    cJSON_Delete(player_unlocks_json);
    cJSON_Delete(settings_json);
}

void tracker_render(struct Tracker *t) {
    // Set draw color and clear screen
    SDL_SetRenderDrawColor(t->renderer, TRACKER_BACKGROUND_COLOR.r, TRACKER_BACKGROUND_COLOR.g,
                           TRACKER_BACKGROUND_COLOR.b, TRACKER_BACKGROUND_COLOR.a);
    SDL_RenderClear(t->renderer);

    // TODO: Draw the advancement icons
    // ... inside your loop for drawing advancements ...
    // for (int i = 0; i < t->template_data->advancement_count; ++i) {
    //     TrackableCategory* adv = t->template_data->advancements[i];
    //
    //     // ... loop through criteria to draw them ...
    //     for (int j = 0; j < adv->criteria_count; ++j) {
    //         TrackableItem* crit = adv->criteria[j];
    //
    //         // 1. Draw the criterion's own icon (e.g., from crit->texture)
    //
    //         // 2. Check the flag
    //         if (crit->is_shared) {
    //             // If it's a shared criterion, also draw the parent
    //             // advancement's icon (e.g., from adv->texture)
    //             // as an overlay or next to the criterion's icon.
    //         }
    //     }
    // }
    // Drawing happens here

    // present backbuffer
    SDL_RenderPresent(t->renderer);
}

void tracker_reinit_template(struct Tracker *t) {
    if (!t) return;

    printf("[TRACKER] Re-initializing template...\n");

    // Update the paths from settings.json
    tracker_reinit_paths(t);

    // Free all the old advancement, stat, etc. data
    if (t->template_data) {
        tracker_free_template_data(t->template_data);

        // Reset the entire struct to zero to clear dangling pointers and old counts.
        memset(t->template_data, 0, sizeof(TemplateData));
    }

    // Load and parse data from the new template files
    tracker_load_and_parse_data(t);
}

void tracker_reinit_paths(struct Tracker *t) {
    if (!t) return;

    AppSettings settings;
    settings_load(&settings);

    // Copy the template and lang paths
    strncpy(t->advancement_template_path, settings.template_path, MAX_PATH_LENGTH - 1);
    t->advancement_template_path[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(t->lang_path, settings.lang_path, MAX_PATH_LENGTH - 1);
    t->lang_path[MAX_PATH_LENGTH - 1] = '\0';

    MC_Version version = settings_get_version_from_string(settings.version_str);
    bool use_advancements = (version >= MC_VERSION_1_12);
    bool use_unlocks = (version == MC_VERSION_25W14CRAFTMINE);

    // Get the final, normalized saves path using the loaded settings
    if (get_saves_path(t->saves_path, MAX_PATH_LENGTH, settings.path_mode, settings.manual_saves_path)) {
        printf("[TRACKER] Using saves path: %s\n", t->saves_path);

        // Find the specific world files using the correct flag
        find_latest_world_files(
            t->saves_path,
            t->advancements_path,
            t->stats_path,
            t->unlocks_path,
            MAX_PATH_LENGTH,
            use_advancements,
            use_unlocks
        );
    } else {
        fprintf(stderr, "[TRACKER] CRITICAL: Failed to get saves path.\n");

        // Ensure paths are empty
        t->saves_path[0] = '\0';
        t->advancements_path[0] = '\0';
        t->stats_path[0] = '\0';
        t->unlocks_path[0] = '\0';
        return;
    }
}

void tracker_load_and_parse_data(struct Tracker *t) {
    printf("[TRACKER] Loading advancement template from: %s\n", t->advancement_template_path);
    cJSON *template_json = cJSON_from_file(t->advancement_template_path);

    // Check if template file exists otherwise create it using temp_create_utils.c
    if (!template_json) {
        fprintf(stderr, "[TRACKER] Template file not found: %s\n", t->advancement_template_path);
        fprintf(stderr, "[TRACKER] Attempting to create new template and language files...\n");

        // Ensure directory structure exists
        fs_ensure_directory_exists(t->advancement_template_path);

        // Create the empty template and language files
        // TODO: Allow user to populate the language and template files through the settings
        fs_create_empty_template_file(t->advancement_template_path);
        fs_create_empty_lang_file(t->lang_path);

        // Try to load the newly create template file again
        template_json = cJSON_from_file(t->advancement_template_path);

        if (!template_json) {
            fprintf(stderr, "[TRACKER] CRITICAL: Failed to load the newly created template file.\n");
            return;
        }
    }

    t->template_data->lang_json = cJSON_from_file(t->lang_path);
    if (!t->template_data->lang_json) {
        // Handle case where lang file might still be missing for some reason
        t->template_data->lang_json = cJSON_CreateObject();
    }

    // Load settings.json to check for custom progress
    cJSON *settings_json = cJSON_from_file(SETTINGS_FILE_PATH);
    if (!settings_json) {
        fprintf(stderr, "[TRACKER] Failed to load or parse settings file.\n");
        return;
    }

    // Parse the 3 main categories from the template
    cJSON *advancements_json = cJSON_GetObjectItem(template_json, "advancements");
    cJSON *stats_json = cJSON_GetObjectItem(template_json, "stats");
    cJSON *unlocks_json = cJSON_GetObjectItem(template_json, "unlocks");
    cJSON *custom_json = cJSON_GetObjectItem(template_json, "custom"); // Custom goals, manually checked of by user
    cJSON *multi_stage_goals_json = cJSON_GetObjectItem(template_json, "multi_stage_goals");

    // Parse the 5 main categories
    parse_trackable_categories(advancements_json, t->template_data->lang_json, t);
    parse_simple_trackables(stats_json, t->template_data->lang_json, &t->template_data->stats,
                            &t->template_data->stat_count);
    parse_simple_trackables(unlocks_json, t->template_data->lang_json, &t->template_data->unlocks,
                            &t->template_data->unlock_count);
    parse_simple_trackables(custom_json, t->template_data->lang_json, &t->template_data->custom_goals,
                            &t->template_data->custom_goal_count); // Update progress from player files
    parse_multi_stage_goals(multi_stage_goals_json, t->template_data->lang_json, &t->template_data->multi_stage_goals,
                            &t->template_data->multi_stage_goal_count);

    // Detect and flag criteria that are shared between multiple advancements
    tracker_detect_shared_criteria(t);

    printf("[TRACKER] Initial template parsing complete.\n");

    cJSON_Delete(template_json);
    if (t->template_data->lang_json) {
        cJSON_Delete(t->template_data->lang_json);
        t->template_data->lang_json = NULL;
    }
    // No need to delete settings_json, because it's not parsed, handled in tracker_update()
}


void tracker_free(struct Tracker **tracker) {
    if (tracker && *tracker) {
        struct Tracker *t = *tracker;

        if (t->template_data) {
            tracker_free_template_data(t->template_data); // This ONLY frees the CONTENT of the struct
            free(t->template_data); // This frees the struct
        }

        if (t->renderer) {
            SDL_DestroyRenderer(t->renderer);
            // We still have an address
            t->renderer = NULL;
        }

        if (t->window) {
            SDL_DestroyWindow(t->window);
            // We still have an address
            t->window = NULL;
        }

        // tracker is heap allocated so free it
        free(t);
        *tracker = NULL;
        tracker = NULL;
        printf("[TRACKER] Tracker freed!\n");
    }
}


void tracker_print_debug_status(struct Tracker *t) {
    if (!t || !t->template_data) return;

    printf("\n--- Player Progress Status ---\n");

    // Advancements and Criteria
    printf("[Advancements] %d / %d completed\n", t->template_data->advancements_completed_count,
           t->template_data->advancement_count);
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *adv = t->template_data->advancements[i];
        printf("  - %s (%d/%d criteria): %s\n", adv->display_name, adv->completed_criteria_count, adv->criteria_count,
               adv->done ? "COMPLETED" : "INCOMPLETE");
        for (int j = 0; j < adv->criteria_count; j++) {
            TrackableItem *crit = adv->criteria[j];
            // takes translation from the language file otherwise root_name
            // Print if criteria is shared
            printf("    - %s: %s%s\n", crit->display_name, crit->is_shared ? "SHARED - " : "",
                   crit->done ? "DONE" : "NOT DONE");
        }
    }

    // Other categories...
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableItem *stat = t->template_data->stats[i];
        printf("[Stat] %s: %d / %d - %s\n", stat->display_name, stat->progress, stat->goal,
               stat->done ? "COMPLETE" : "INCOMPLETE");
    }


    // Only print unlocks if they exist
    if (t->template_data->unlock_count > 0) {
        printf("[Unlocks] %d / %d completed\n", t->template_data->unlocks_completed_count, t->template_data->unlock_count);
    }

    // Loop to print each unlock individually
    for (int i = 0; i < t->template_data->unlock_count; i++) {
        TrackableItem *unlock = t->template_data->unlocks[i];
        if (!unlock) continue;
        printf("  - %s: %s\n", unlock->display_name, unlock->done ? "UNLOCKED" : "LOCKED");
    }


    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        TrackableItem *custom_goal = t->template_data->custom_goals[i];

        // Check if the custom goal is a counter (has a target > 0)
        if (custom_goal->goal > 0) {
            printf("[Custom Goal] %s: %d / %d - %s\n",
                custom_goal->display_name,
                custom_goal->progress,
                custom_goal->goal,
                custom_goal->done ? "COMPLETED" : "INCOMPLETE");
        } else { // Otherwise it's a simple toggle (no target entry needs to be specified)
            printf("[Custom Goal] %s: %s\n",
                custom_goal->display_name,
                custom_goal->done ? "COMPLETED" : "INCOMPLETE");
        }
    }

    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        if (goal && goal->stages && goal->current_stage < goal->stage_count) {
            SubGoal *active_stage = goal->stages[goal->current_stage];

            // Check if the active stage is a stat and print its progress
            if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress > 0) {
                printf("[Multi-Stage Goal] %s: %s (%d/%d)\n",
                       goal->display_name,
                       active_stage->display_text,
                       active_stage->current_stat_progress,
                       active_stage->required_progress);
            } else {
                // If it's not "stat" print this
                printf("[Multi-Stage Goal] %s: %s\n", goal->display_name, active_stage->display_text);
            }
        }
    }

    // Advancement Progress AGAIN
    printf("\n[Advancements] %d / %d completed\n", t->template_data->advancements_completed_count,
           t->template_data->advancement_count);
    // Overall Progress
    printf("[Overall Progress] %.2f%%\n", t->template_data->overall_progress_percentage);
    printf("----------------------------\n\n");

    // Force the output buffer to write to the console immediately
    fflush(stdout);
}
