//
// Created by Linus on 24.06.2025.
//

#include <stdio.h>
#include <cJSON.h>
#include <ctype.h>

#include "tracker.h"
#include "init_sdl.h"
#include "path_utils.h"
#include "settings_utils.h"
#include "file_utils.h" // has the cJSON_from_file function
#include "temp_create_utils.h"

// FOR VERSION-SPECIFIC PARSERS

/**
 * @brief (Era 1: 1.0-1.6.4) Save snapshot to file to simulate local stats.
 *
 * Thus remembers the snapshot even if the tracker is closed.
 *
 * @param t A pointer to the Tracker struct.
 */
static void tracker_save_snapshot_to_file(struct Tracker *t) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    // Save metadata
    cJSON_AddStringToObject(root, "snapshot_world_name", t->template_data->snapshot_world_name);
    cJSON_AddNumberToObject(root, "playtime_snapshot", t->template_data->playtime_snapshot);

    // Save achievement snapshot
    cJSON *ach_snapshot = cJSON_CreateObject();
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *ach = t->template_data->advancements[i];
        // Add the achievement to the snapshot with its status
        cJSON_AddBoolToObject(ach_snapshot, ach->root_name, ach->done_in_snapshot);
    }
    cJSON_AddItemToObject(root, "achievements", ach_snapshot);

    // Save stat snapshot
    cJSON *stat_snapshot = cJSON_CreateObject();
    for (int i = 0; i < t->template_data->stat_count; i++) {
        for (int j = 0; j < t->template_data->stats[i]->criteria_count; j++) {
            TrackableItem *sub_stat = t->template_data->stats[i]->criteria[j];

            // IMPORTANT Preventing duplicate entries in snapshot file
            if (!cJSON_HasObjectItem(stat_snapshot, sub_stat->root_name)) {
                cJSON_AddNumberToObject(stat_snapshot, sub_stat->root_name, sub_stat->initial_progress);
            }
        }
    }
    cJSON_AddItemToObject(root, "stats", stat_snapshot);

    // Write to file
    FILE *file = fopen(t->snapshot_path, "w");
    if (file) {
        char *json_str = cJSON_Print(root);
        fputs(json_str, file);
        fclose(file);
        free(json_str);
        printf("[TRACKER] Snapshot saved to %s\n", t->snapshot_path);
    }
    cJSON_Delete(root); // Clean up
}

/**
 * @brief (Era 1: 1.0-1.6.4) Load snapshot from file to simulate local stats.
 *
 * This is important when the tracker gets re-opened so it can "remember" the snapshot.
 *
 * @param t A pointer to the Tracker struct.
 */
static void tracker_load_snapshot_from_file(struct Tracker *t) {
    cJSON *snapshot_json = cJSON_from_file(t->snapshot_path);
    if (!snapshot_json) {
        printf("[TRACKER] No existing snapshot file found for this configuration.\n");
        return;
    }

    // Load metadata
    // It should keep the snapshot as long as player is on the same world
    cJSON *world_name_json = cJSON_GetObjectItem(snapshot_json, "snapshot_world_name");
    if (cJSON_IsString(world_name_json)) {
        strncpy(t->template_data->snapshot_world_name, world_name_json->valuestring, MAX_PATH_LENGTH - 1);
    }
    cJSON *playtime_json = cJSON_GetObjectItem(snapshot_json, "playtime_snapshot");
    if (cJSON_IsNumber(playtime_json)) {
        t->template_data->playtime_snapshot = playtime_json->valuedouble;
    }

    // Load achievement snapshot
    cJSON *ach_snapshot = cJSON_GetObjectItem(snapshot_json, "achievements");
    if (ach_snapshot) {
        for (int i = 0; i < t->template_data->advancement_count; i++) {
            TrackableCategory *ach = t->template_data->advancements[i];
            ach->done_in_snapshot = cJSON_IsTrue(cJSON_GetObjectItem(ach_snapshot, ach->root_name));
        }
    }

    // Load stat snapshot
    cJSON *stat_snapshot = cJSON_GetObjectItem(snapshot_json, "stats");
    if (stat_snapshot) {
        for (int i = 0; i < t->template_data->stat_count; i++) {
            for (int j = 0; j < t->template_data->stats[i]->criteria_count; j++) {
                TrackableItem *sub_stat = t->template_data->stats[i]->criteria[j];
                cJSON *stat_val = cJSON_GetObjectItem(stat_snapshot, sub_stat->root_name);
                if (cJSON_IsNumber(stat_val)) {
                    sub_stat->initial_progress = stat_val->valueint;
                }
            }
        }
    }
    cJSON_Delete(snapshot_json);
    printf("[TRACKER] Snapshot successfully loaded from %s\n", t->snapshot_path);
}

/**
 * @brief (Era 1: 1.0-1.6.4) Takes a snapshot of the current global stats including achievements.
 * This is called when a new world is loaded to establish a baseline for progress.
 */
static void tracker_snapshot_legacy_stats(struct Tracker *t) {
    cJSON *player_stats_json = cJSON_from_file(t->stats_path);
    if (!player_stats_json) {
        fprintf(stderr, "[TRACKER] Could not read stats file to create snapshot.\n");
        return;
    }

    // Get the stats-change array within the stats object
    cJSON *stats_change = cJSON_GetObjectItem(player_stats_json, "stats-change");
    if (!cJSON_IsArray(stats_change)) {
        cJSON_Delete(player_stats_json);
        return;
    }

    // Set the snapshot world name
    strncpy(t->template_data->snapshot_world_name, t->world_name, MAX_PATH_LENGTH - 1);
    t->template_data->snapshot_world_name[MAX_PATH_LENGTH - 1] = '\0';
    t->template_data->playtime_snapshot = 0;

    // Reset all initial progress fields first
    for (int i = 0; i < t->template_data->stat_count; i++) {
        for (int j = 0; j < t->template_data->stats[i]->criteria_count; j++) {
            t->template_data->stats[i]->criteria[j]->initial_progress = 0;
        }
    }
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        t->template_data->advancements[i]->done_in_snapshot = false;
    }


    // Iterate the live stats and store them as the initial values
    cJSON *stat_entry;
    cJSON_ArrayForEach(stat_entry, stats_change) {
        cJSON *item = stat_entry->child; // Then we go into the curly brackets
        if (item) {
            const char *key = item->string;
            int value = item->valueint;

            // Snapshot playtime
            if (strcmp(key, "1100") == 0) {
                t->template_data->playtime_snapshot = value;
            }

            // Snapshot other tracked stats (including playtime)
            for (int i = 0; i < t->template_data->stat_count; i++) {
                for (int j = 0; j < t->template_data->stats[i]->criteria_count; j++) {
                    TrackableItem *sub_stat = t->template_data->stats[i]->criteria[j];
                    if (strcmp(sub_stat->root_name, key) == 0) {
                        sub_stat->initial_progress = value;
                    }
                }
            }

            // Snapshot achievements
            for (int i = 0; i < t->template_data->advancement_count; i++) {
                TrackableCategory *ach = t->template_data->advancements[i];
                if (strcmp(ach->root_name, key) == 0 && value >= 1) {
                    ach->done_in_snapshot = true;
                }
            }
        }
    }
    cJSON_Delete(player_stats_json);

    // SAVE TO SNAPSHOT FILE
    tracker_save_snapshot_to_file(t);

    // TODO: DEBUGGING CODE TO PRINT WHAT THE SNAPSHOT LOOKS LIKE
    // --- ADD THIS EXPANDED DEBUGGING CODE ---
    printf("\n--- STARTING SNAPSHOT FOR WORLD: %s ---\n", t->template_data->snapshot_world_name);

    // Re-load the JSON file just for this debug print (this is inefficient but simple for debugging)
    cJSON *debug_json = cJSON_from_file(t->stats_path);
    if (debug_json) {
        cJSON *stats_change = cJSON_GetObjectItem(debug_json, "stats-change");
        if (cJSON_IsArray(stats_change)) {
            printf("\n--- LEGACY ACHIEVEMENT CHECK ---\n");
            // Loop through achievements from the template
            for (int i = 0; i < t->template_data->advancement_count; i++) {
                TrackableCategory *ach = t->template_data->advancements[i];
                bool found = false;
                int value = 0;

                // Search for this achievement in the player's stats data
                cJSON *stat_entry;
                cJSON_ArrayForEach(stat_entry, stats_change) {
                    cJSON *item = stat_entry->child;
                    if (item && strcmp(item->string, ach->root_name) == 0) {
                        found = true;
                        value = item->valueint;
                        break;
                    }
                }

                if (found) {
                    printf("  - Achievement '%s' (ID: %s): FOUND with value: %d\n", ach->display_name, ach->root_name,
                           value);
                } else {
                    printf("  - Achievement '%s' (ID: %s): NOT FOUND in player data\n", ach->display_name,
                           ach->root_name);
                }
            }
        }
        cJSON_Delete(debug_json);
    }

    printf("\n--- LEGACY STAT SNAPSHOT ---\n");
    printf("Playtime Snapshot: %lld ticks\n", t->template_data->playtime_snapshot);

    // Use a nested loop to print the stats
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        printf("  - Category '%s':\n", stat_cat->display_name);
        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];
            printf("    - Sub-Stat '%s' (ID: %s): Snapshot Value = %d\n",
                   sub_stat->display_name, sub_stat->root_name, sub_stat->initial_progress);
        }
    }
    printf("--- END OF SNAPSHOT ---\n\n");
}

/**
 * @brief (Era 1: 1.0-1.6.4) Parses legacy .dat stats files.
 */
static void tracker_update_stats_legacy(struct Tracker *t, const cJSON *player_stats_json) {
    if (!player_stats_json) return;

    cJSON *stats_change = cJSON_GetObjectItem(player_stats_json, "stats-change");
    if (!cJSON_IsArray(stats_change)) return;

    // Reset achievement counts before updating
    t->template_data->advancements_completed_count = 0;

    // Loop to update achievement progress
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *ach = t->template_data->advancements[i];
        bool is_currently_done = false;

        cJSON *stat_entry;
        cJSON_ArrayForEach(stat_entry, stats_change) {
            cJSON *item = stat_entry->child;
            if (item && strcmp(item->string, ach->root_name) == 0 && item->valueint >= 1) {
                is_currently_done = true;
                break;
            }
        }
        ach->done = is_currently_done;

        if (ach->done && !ach->done_in_snapshot) {
            t->template_data->advancements_completed_count++;
        }
    }

    cJSON *settings_json = cJSON_from_file(SETTINGS_FILE_PATH);
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : NULL;

    t->template_data->play_time_ticks = 0;
    t->template_data->stats_completed_count = 0;
    t->template_data->stats_completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        stat_cat->completed_criteria_count = 0;

        // Check for parent override
        cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : NULL;
        stat_cat->is_manually_completed = cJSON_IsBool(parent_override);
        bool parent_forced_true = stat_cat->is_manually_completed && cJSON_IsTrue(parent_override);

        // Loop through each SUB-STAT
        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];

            // Always get live progress from file first
            sub_stat->progress = 0;
            cJSON *stat_entry;
            cJSON_ArrayForEach(stat_entry, stats_change) {
                cJSON *item = stat_entry->child;
                if (item && strcmp(item->string, sub_stat->root_name) == 0) {
                    int diff = item->valueint - sub_stat->initial_progress; // Difference to snapshot
                    sub_stat->progress = diff > 0 ? diff : 0;
                    break;
                }
            }

            // When completed through playing
            bool naturally_done = (sub_stat->goal > 0 && sub_stat->progress >= sub_stat->goal);

            // For single-criterion stats, the override key is the parent's.
            // For multi-criterion, it's the specific sub-stat key.
            // Creates ONE criteria behind the scenes, even if template doesn't have "criteria" field
            cJSON *sub_override;
            if (stat_cat->criteria_count == 1) { // WHEN IT HAS NO SUB-STATS
                sub_override = parent_override;
                sub_stat->is_manually_completed = stat_cat->is_manually_completed;
            } else {
                // Multi-criterion
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name, sub_stat->root_name);
                sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : NULL;
                sub_stat->is_manually_completed = cJSON_IsBool(sub_override);
            }

            bool sub_forced_true = sub_stat->is_manually_completed && cJSON_IsTrue(sub_override);

            // Sub-stat is done if it's naturally completed OR manually forced to be true (sub-stat or parent)
            sub_stat->done = naturally_done || sub_forced_true || parent_forced_true;

            // Increment category's completed count
            if (sub_stat->done) {
                stat_cat->completed_criteria_count++;
            }
        }

        // Determine final 'done' status for PARENT category
        bool all_children_done = (stat_cat->criteria_count > 0 && stat_cat->completed_criteria_count >= stat_cat->criteria_count);

        // A category is done if all its children are done OR it's manually forced to be true
        stat_cat->done = all_children_done || parent_forced_true;

        if (stat_cat->done) t->template_data->stats_completed_count++;
        t->template_data->stats_completed_criteria_count += stat_cat->completed_criteria_count;
    }

    // Handle playtime separately
    cJSON *stat_entry;
    cJSON_ArrayForEach(stat_entry, stats_change) {
        cJSON *item = stat_entry->child;
        if (item && strcmp(item->string, "1100") == 0) {
            long long diff = item->valueint - t->template_data->playtime_snapshot;
            t->template_data->play_time_ticks = (diff > 0) ? diff : 0;
            break;
        }
    }

    cJSON_Delete(settings_json);
}

/**
 * @brief (Era 2: 1.7.2-1.11.2) Parses unified JSON with achievements and stats.
 */
static void tracker_update_achievements_and_stats_mid(struct Tracker *t, const cJSON *player_stats_json) {
    if (!player_stats_json) return;

    t->template_data->advancements_completed_count = 0;
    t->template_data->completed_criteria_count = 0;
    t->template_data->play_time_ticks = 0;

    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *ach = t->template_data->advancements[i];
        ach->completed_criteria_count = 0;
        ach->done = false;
        cJSON *ach_entry = cJSON_GetObjectItem(player_stats_json, ach->root_name);
        if (!ach_entry) continue;
        if (cJSON_IsNumber(ach_entry)) {
            ach->done = (ach_entry->valueint > 0);
        } else if (cJSON_IsObject(ach_entry)) {
            cJSON *progress_array = cJSON_GetObjectItem(ach_entry, "progress");
            if (cJSON_IsArray(progress_array)) {
                for (int j = 0; j < ach->criteria_count; j++) {
                    TrackableItem *crit = ach->criteria[j];
                    crit->done = false;
                    cJSON *progress_item;
                    cJSON_ArrayForEach(progress_item, progress_array) {
                        if (cJSON_IsString(progress_item) && strcmp(progress_item->valuestring, crit->root_name) == 0) {
                            crit->done = true;
                            ach->completed_criteria_count++;
                            break;
                        }
                    }
                }
            }
            if (ach->criteria_count > 0 && ach->completed_criteria_count >= ach->criteria_count) {
                ach->done = true;
            }
        }
        if (ach->done) {
            t->template_data->advancements_completed_count++;
        }
        t->template_data->completed_criteria_count += ach->completed_criteria_count;
    }

    // Stats logic with sub-stats
    cJSON *settings_json = cJSON_from_file(SETTINGS_FILE_PATH);
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : NULL;

    t->template_data->stats_completed_count = 0;
    t->template_data->stats_completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        // Iterate through stats
        TrackableCategory *stat_cat = t->template_data->stats[i];
        stat_cat->completed_criteria_count = 0;

        cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : NULL;
        stat_cat->is_manually_completed = cJSON_IsBool(parent_override);
        bool parent_forced_true = stat_cat->is_manually_completed && cJSON_IsTrue(parent_override);

        // Iterate through sub-stats
        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];

            cJSON *stat_entry = cJSON_GetObjectItem(player_stats_json, sub_stat->root_name);
            sub_stat->progress = cJSON_IsNumber(stat_entry) ? stat_entry->valueint : 0;

            // Determine natural completion
            bool naturally_done = (sub_stat->goal > 0 && sub_stat->progress >= sub_stat->goal);

            // Check for sub-stat override
            cJSON *sub_override;
            // If there are no sub-stats, use parent override (NO ".criteria." used for "regular" stats)
            // Creates ONE criteria behind the scenes, even if template doesn't have "criteria" field
            if (stat_cat->criteria_count == 1) {
                sub_override = parent_override;
                sub_stat->is_manually_completed = stat_cat->is_manually_completed;
            } else {
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name, sub_stat->root_name);
                sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : NULL;
                sub_stat->is_manually_completed = cJSON_IsBool(sub_override);
            }

            bool sub_forced_true = sub_stat->is_manually_completed && cJSON_IsTrue(sub_override);

            // Either naturally done OR manually overridden to be true  (itself or by parent)
            sub_stat->done = naturally_done || sub_forced_true || parent_forced_true;

            // Increment completed count
            if (sub_stat->done) stat_cat->completed_criteria_count++;
        }

        bool all_children_done = (stat_cat->criteria_count > 0 && stat_cat->completed_criteria_count >= stat_cat->criteria_count);

        // Either all children done OR parent manually overridden to be true
        stat_cat->done = all_children_done || parent_forced_true;

        if (stat_cat->done) t->template_data->stats_completed_count++;
        t->template_data->stats_completed_criteria_count += stat_cat->completed_criteria_count;
    }

    cJSON_Delete(settings_json);

    // Update mid-era playtime
    cJSON *play_time_entry = cJSON_GetObjectItem(player_stats_json, "stat.playOneMinute");
    if (cJSON_IsNumber(play_time_entry)) {
        t->template_data->play_time_ticks = (long long) play_time_entry->valuedouble;
    }
}

/**
 * @brief (Era 3: 1.12+) Updates advancement progress from modern JSON files.
 */
static void tracker_update_advancements_modern(struct Tracker *t, const cJSON *player_adv_json) {
    if (!player_adv_json) return;

    t->template_data->advancements_completed_count = 0;
    t->template_data->completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *adv = t->template_data->advancements[i];
        adv->completed_criteria_count = 0;

        cJSON *player_entry = cJSON_GetObjectItem(player_adv_json, adv->root_name);
        // take root name (from template) from player advancements
        if (player_entry) {
            adv->done = cJSON_IsTrue(cJSON_GetObjectItem(player_entry, "done"));
            if (adv->done) {
                t->template_data->advancements_completed_count++;
            }

            cJSON *player_criteria = cJSON_GetObjectItem(player_entry, "criteria");
            if (player_criteria) {
                // If advancement has criteria
                for (int j = 0; j < adv->criteria_count; j++) {
                    TrackableItem *crit = adv->criteria[j];

                    // Set to done if the entry exists
                    if (cJSON_HasObjectItem(player_criteria, crit->root_name)) {
                        crit->done = true;
                        adv->completed_criteria_count++;
                    } else {
                        crit->done = false;
                    }
                }
            }
        } else {
            adv->done = false;
            for (int j = 0; j < adv->criteria_count; j++) {
                adv->criteria[j]->done = false;
            }
        }
        t->template_data->completed_criteria_count += adv->completed_criteria_count;
    }
}

/**
 * @brief (Era 3: 1.12+) Updates stat progress from modern JSON files.
 */
static void tracker_update_stats_modern(struct Tracker *t, const cJSON *player_stats_json, const cJSON *settings_json,
                                        MC_Version version) {
    if (!player_stats_json) return;

    cJSON *stats_obj = cJSON_GetObjectItem(player_stats_json, "stats");
    if (!stats_obj) return;

    // Get the playtime
    cJSON *custom_stats = cJSON_GetObjectItem(stats_obj, "minecraft:custom");
    if (custom_stats) {
        // Use play_one_minute for 1.12 -> 1.16.5 and play_time for 1.17+
        const char *playtime_key = (version >= MC_VERSION_1_17) ? "minecraft:play_time" : "minecraft:play_one_minute";
        cJSON *play_time = cJSON_GetObjectItem(custom_stats, playtime_key);
        if (cJSON_IsNumber(play_time)) {
            t->template_data->play_time_ticks = (long long) play_time->valuedouble;
        }
    }

    // Manually override the stat progress
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : NULL;

    t->template_data->stats_completed_count = 0;
    t->template_data->stats_completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        stat_cat->completed_criteria_count = 0;

        // Override check
        cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : NULL;
        stat_cat->is_manually_completed = cJSON_IsBool(parent_override);
        bool parent_forced_true = stat_cat->is_manually_completed && cJSON_IsTrue(parent_override);

        // Loop through all sub stats
        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];
            sub_stat->progress = 0;

            char root_name_copy[192];
            strncpy(root_name_copy, sub_stat->root_name, sizeof(root_name_copy) - 1);
            root_name_copy[sizeof(root_name_copy) - 1] = '\0';

            // Null-terminating the first forward slash to create the language key so nether/create beacon
            // becomes nether.create_beacon
            char *category_key = root_name_copy;
            char *item_key = strchr(root_name_copy, '/');
            if (item_key) {
                *item_key = '\0';
                item_key++; // Move the pointer to the next character after the forward slash
                cJSON *category_obj = cJSON_GetObjectItem(stats_obj, category_key);
                if (category_obj) {
                    cJSON *stat_value = cJSON_GetObjectItem(category_obj, item_key);
                    if (cJSON_IsNumber(stat_value)) {
                        sub_stat->progress = stat_value->valueint;
                    }
                }
            }

            // Determine natural completion
            bool naturally_done = (sub_stat->goal > 0 && sub_stat->progress >= sub_stat->goal);

            cJSON *sub_override;
            // If there are no sub stats, so "regular" stat doesn't use ".criteria.", then just counts as parent
            // Creates ONE criteria behind the scenes, even if template doesn't have "criteria" field
            if (stat_cat->criteria_count == 1) {
                sub_override = parent_override;
                sub_stat->is_manually_completed = stat_cat->is_manually_completed;
            } else {
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                         sub_stat->root_name);
                sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : NULL;
                sub_stat->is_manually_completed = cJSON_IsBool(sub_override);
            }
            bool sub_forced_true = sub_stat->is_manually_completed && cJSON_IsTrue(sub_override);

            // Either naturally done OR manually overridden to true (itself OR parent)
            sub_stat->done = naturally_done || sub_forced_true || parent_forced_true;

            if (sub_stat->done) stat_cat->completed_criteria_count++;
        }

        bool all_children_done = (stat_cat->criteria_count > 0 && stat_cat->completed_criteria_count >= stat_cat->criteria_count);

        stat_cat->done = all_children_done || parent_forced_true;

        // Update the percentage progress properly
        if (stat_cat->done) t->template_data->stats_completed_count++;
        t->template_data->stats_completed_criteria_count += stat_cat->completed_criteria_count;
    }
}

// HELPER FUNCTION FOR PARSING
/**
 * @brief Parses advancement or stat categories and their criteria from the template, supporting modded advancements/stats.
 *
 * @param category_json The JSON object containing the categories.
 * @param lang_json The JSON object containing the language keys.
 * @param categories_array A pointer to an array of TrackableCategory pointers to store the parsed categories.
 * @param count A pointer to an integer to store the number of parsed categories.
 * @param total_criteria_count A pointer to an integer to store the total number of criteria across all categories.
 * @param lang_key_prefix The prefix for language keys. (e.g., "advancement." or "stat.")
 * @param is_stat_category A boolean indicating whether the categories are for stats. False means advancements.
 */
static void tracker_parse_categories(cJSON *category_json, cJSON *lang_json, TrackableCategory ***categories_array,
                                     int *count, int *total_criteria_count, const char *lang_key_prefix,
                                     bool is_stat_category) {
    if (!category_json) {
        printf("[TRACKER] tracker_parse_categories: category_json is NULL\n");
        return;
    }

    *count = 0;
    for (cJSON *i = category_json->child; i != NULL; i = i->next) (*count)++;
    if (*count == 0) return;

    *categories_array = calloc(*count, sizeof(TrackableCategory *));
    if (!*categories_array) return;

    cJSON *cat_json = category_json->child;
    int i = 0;
    *total_criteria_count = 0;

    while (cat_json) {
        TrackableCategory *new_cat = calloc(1, sizeof(TrackableCategory));
        if (!new_cat) {
            cat_json = cat_json->next;
            continue;
        }

        if (cat_json->string) {
            strncpy(new_cat->root_name, cat_json->string, sizeof(new_cat->root_name) - 1);
        } else {
            fprintf(stderr, "[TRACKER] PARSE ERROR: Found a JSON item with a NULL key. Skipping.\n");
            free(new_cat);
            cat_json = cat_json->next;
            continue;
        }

        char cat_lang_key[256];
        // Correctly generate the parent language key
        if (!is_stat_category) {
            // Transform for advancements
            char temp_root_name[192];
            strncpy(temp_root_name, new_cat->root_name, sizeof(temp_root_name) - 1);
            char *path_part = strchr(temp_root_name, ':');
            if (path_part) *path_part = '.';
            snprintf(cat_lang_key, sizeof(cat_lang_key), "%s%s", lang_key_prefix, temp_root_name);
            for (char *p = cat_lang_key; *p; p++) if (*p == '/') *p = '.';
        } else {
            // Use raw name for stats
            snprintf(cat_lang_key, sizeof(cat_lang_key), "%s%s", lang_key_prefix, new_cat->root_name);
        }

        cJSON *lang_entry = cJSON_GetObjectItem(lang_json, cat_lang_key);
        if (cJSON_IsString(lang_entry))
            strncpy(new_cat->display_name, lang_entry->valuestring,
                    sizeof(new_cat->display_name) - 1);
        else strncpy(new_cat->display_name, new_cat->root_name, sizeof(new_cat->display_name) - 1);

        cJSON *icon = cJSON_GetObjectItem(cat_json, "icon");
        if (cJSON_IsString(icon)) strncpy(new_cat->icon_path, icon->valuestring, sizeof(new_cat->icon_path) - 1);

        cJSON *criteria_obj = cJSON_GetObjectItem(cat_json, "criteria");
        if (criteria_obj && cJSON_IsObject(criteria_obj)) {
            // MULTI-CRITERION CASE
            for (cJSON *c = criteria_obj->child; c != NULL; c = c->next) new_cat->criteria_count++;
            if (new_cat->criteria_count > 0) {
                new_cat->criteria = calloc(new_cat->criteria_count, sizeof(TrackableItem *));
                *total_criteria_count += new_cat->criteria_count;
                int k = 0;
                for (cJSON *crit_item = criteria_obj->child; crit_item != NULL; crit_item = crit_item->next) {
                    TrackableItem *new_crit = calloc(1, sizeof(TrackableItem));
                    if (new_crit) {
                        strncpy(new_crit->root_name, crit_item->string, sizeof(new_crit->root_name) - 1);
                        if (is_stat_category) {
                            cJSON *target = cJSON_GetObjectItem(crit_item, "target");
                            if (cJSON_IsNumber(target)) new_crit->goal = target->valueint;
                        }
                        char crit_lang_key[256];
                        snprintf(crit_lang_key, sizeof(crit_lang_key), "%s.criteria.%s", cat_lang_key,
                                 new_crit->root_name);
                        cJSON *crit_lang_entry = cJSON_GetObjectItem(lang_json, crit_lang_key);
                        if (cJSON_IsString(crit_lang_entry))
                            strncpy(new_crit->display_name,
                                    crit_lang_entry->valuestring,
                                    sizeof(new_crit->display_name) - 1);
                        else strncpy(new_crit->display_name, new_crit->root_name, sizeof(new_crit->display_name) - 1);

                        new_cat->criteria[k++] = new_crit;
                    }
                }
            }
        } else {
            // SINGLE-CRITERION SPECIAL CASE
            new_cat->criteria_count = 1;
            *total_criteria_count += 1;
            new_cat->criteria = calloc(1, sizeof(TrackableItem *));
            if (new_cat->criteria) {
                TrackableItem *the_criterion = calloc(1, sizeof(TrackableItem));
                if (the_criterion) {
                    cJSON *crit_root_name_json = cJSON_GetObjectItem(cat_json, "root_name");
                    if (cJSON_IsString(crit_root_name_json)) {
                        strncpy(the_criterion->root_name, crit_root_name_json->valuestring,
                                sizeof(the_criterion->root_name) - 1);
                    }
                    strncpy(the_criterion->display_name, new_cat->display_name,
                            sizeof(the_criterion->display_name) - 1);
                    if (is_stat_category) {
                        cJSON *target = cJSON_GetObjectItem(cat_json, "target");
                        if (cJSON_IsNumber(target)) the_criterion->goal = target->valueint;
                    }
                    new_cat->criteria[0] = the_criterion;
                }
            }
        }
        (*categories_array)[i++] = new_cat;
        cat_json = cat_json->next;
        // if (new_cat) {
        //     if (cat_json->string) {
        //         strncpy(new_cat->root_name, cat_json->string, sizeof(new_cat->root_name) - 1);
        //     } else {
        //         fprintf(stderr, "[TRACKER] PARSE ERROR: Found a JSON item with a NULL kay. Skipping.\n");
        //         free(new_cat);
        //         cat_json = cat_json->next;
        //         continue; // Skip the rest of the loop for this invalid item
        //     }
        //     // Language key lookup for the parent category
        //     char cat_lang_key[256];
        //
        //     // ONLY FOR ADVANCEMENTS TRANSFORM THE NAME TO CREATE THE KEY
        //     if (!is_stat_category) {
        //         char temp_root_name[192];
        //         strncpy(temp_root_name, new_cat->root_name, sizeof(temp_root_name) - 1);
        //
        //         // REPLACING ALL ':' WITH '.' AND '/' WITH '.'
        //         // Replace the first ':' with a '.' (for namespace in lang file)
        //         char *path_part = strchr(temp_root_name, ':');
        //         if (path_part) {
        //             *path_part = '.';
        //         }
        //
        //         // Construct the full base key (e.g., "advancement.minecraft.story/smelt_iron")
        //         snprintf(cat_lang_key, sizeof(cat_lang_key), "%s%s", lang_key_prefix, temp_root_name);
        //
        //         // Replace all slashes '/' with dots '.'
        //         for (char *p = cat_lang_key; *p; p++) {
        //             if (*p == '/') *p = '.';
        //         }
        //     } else {
        //         // FOR STATS: Use the root_name directly without any transformation
        //         snprintf(cat_lang_key, sizeof(cat_lang_key), "%s%s", lang_key_prefix, new_cat->root_name);
        //     }
        //
        //     cJSON *lang_entry = cJSON_GetObjectItem(lang_json, cat_lang_key);
        //     if (cJSON_IsString(lang_entry)) {
        //         strncpy(new_cat->display_name, lang_entry->valuestring, sizeof(new_cat->display_name) - 1);
        //     } else {
        //         // No language entry found so take the root_name
        //         strncpy(new_cat->display_name, new_cat->root_name, sizeof(new_cat->display_name) - 1);
        //     }
        //
        //     cJSON *icon = cJSON_GetObjectItem(cat_json, "icon");
        //     if (cJSON_IsString(icon)) {
        //         // Set the icon path
        //         strncpy(new_cat->icon_path, icon->valuestring, sizeof(new_cat->icon_path) - 1);
        //     }
        //
        //     // Parse criteria (sub-items)
        //     cJSON *criteria_obj = cJSON_GetObjectItem(cat_json, "criteria");
        //
        //     // Handle BOTH SINGLE and MULTI criteria stats and advancements
        //     if (criteria_obj && cJSON_IsObject(criteria_obj)) {
        //         // Count the number of criteria
        //         for (cJSON *c = criteria_obj->child; c != NULL; c = c->next) new_cat->criteria_count++;
        //         if (new_cat->criteria_count > 0) {
        //             // Allocate memory for the criteria
        //             new_cat->criteria = calloc(new_cat->criteria_count, sizeof(TrackableItem *));
        //             *total_criteria_count += new_cat->criteria_count;
        //             int k = 0;
        //             for (cJSON *crit_item = criteria_obj->child; crit_item != NULL; crit_item = crit_item->next) {
        //                 TrackableItem *new_crit = calloc(1, sizeof(TrackableItem));
        //                 if (new_crit) {
        //                     strncpy(new_crit->root_name, crit_item->string, sizeof(new_crit->root_name) - 1);
        //
        //                     // Parse target for stats
        //                     if (is_stat_category) {
        //                         cJSON *target = cJSON_GetObjectItem(crit_item, "target");
        //                         if (cJSON_IsNumber(target)) new_crit->goal = target->valueint;
        //                     }
        //
        //                     // Language key lookup for the criterion
        //                     char crit_lang_key[256];
        //                     snprintf(crit_lang_key, sizeof(crit_lang_key), "%s.criteria.%s", cat_lang_key,
        //                              new_crit->root_name);
        //                     lang_entry = cJSON_GetObjectItem(lang_json, crit_lang_key);
        //                     if (cJSON_IsString(lang_entry)) {
        //                         strncpy(new_crit->display_name, lang_entry->valuestring,
        //                                 sizeof(new_crit->display_name) - 1);
        //                     } else {
        //                         strncpy(new_crit->display_name, new_crit->root_name,
        //                                 sizeof(new_crit->display_name) - 1);
        //                     }
        //
        //                     new_cat->criteria[k++] = new_crit; // Add to the category
        //                 }
        //             }
        //         }
        //     } else {
        //         // SINGLE-CRITERION SPECIAL CASE
        //         new_cat->criteria_count = 1;
        //         *total_criteria_count += 1;
        //         new_cat->criteria = calloc(1, sizeof(TrackableItem *));
        //         if (new_cat->criteria) {
        //             TrackableItem *the_criterion = calloc(1, sizeof(TrackableItem));
        //             if (the_criterion) {
        //                 cJSON *crit_root_name_json = cJSON_GetObjectItem(cat_json, "root_name");
        //                 if (cJSON_IsString(crit_root_name_json)) {
        //                     strncpy(the_criterion->root_name, crit_root_name_json->valuestring,
        //                             sizeof(the_criterion->root_name) - 1);
        //                 }
        //                 // Single criterion can just inherit the parent's display name
        //                 strncpy(the_criterion->display_name, new_cat->display_name,
        //                         sizeof(the_criterion->display_name) - 1);
        //
        //                 if (is_stat_category) {
        //                     // Parse target for stats
        //                     cJSON *target = cJSON_GetObjectItem(cat_json, "target");
        //                     if (cJSON_IsNumber(target)) the_criterion->goal = target->valueint;
        //                 }
        //                 new_cat->criteria[0] = the_criterion;
        //             }
        //         }
        //     }
        //     (*categories_array)[i++] = new_cat;
        // }
        // cat_json = cat_json->next; // Move to the next category
    }
}

// Helper struct for counting
typedef struct {
    char root_name[192];
    int count;
} CriterionCounter;

// helper function to process and count all sub-items from a list of categories
static int count_all_sub_items(CriterionCounter **counts, int capacity, int current_unique_count,
                               TrackableCategory **categories, int cat_count) {
    if (!categories) return current_unique_count;

    for (int i = 0; i < cat_count; i++) {
        for (int j = 0; j < categories[i]->criteria_count; j++) {
            TrackableItem *crit = categories[i]->criteria[j];
            bool found = false;
            for (int k = 0; k < current_unique_count; k++) {
                if (strcmp((*counts)[k].root_name, crit->root_name) == 0) {
                    (*counts)[k].count++;
                    found = true;
                    break;
                }
            }

            // If the criterion is not found in the counts array, add it
            if (!found && current_unique_count < capacity) {
                strncpy((*counts)[current_unique_count].root_name, crit->root_name, 191);
                (*counts)[current_unique_count].count = 1;
                current_unique_count++;
            }
        }
    }
    return current_unique_count;
}

// Helper function to flag the items that are shared
static void flag_shared_sub_items(CriterionCounter *counts, int unique_count, TrackableCategory **categories,
                                  int cat_count) {
    if (!categories) return;

    for (int i = 0; i < cat_count; i++) {
        for (int j = 0; j < categories[i]->criteria_count; j++) {
            TrackableItem *crit = categories[i]->criteria[j];
            crit->is_shared = false; // Reset first
            for (int k = 0; k < unique_count; k++) {
                // If the criterion is found in more than one advancement or stat
                if (strcmp(counts[k].root_name, crit->root_name) == 0 && counts[k].count > 1) {
                    crit->is_shared = true;
                    break;
                }
            }
        }
    }
}

/**
 * @brief Detects criteria that are shared across multiple advancements or stats and flags them.
 *
 * This function iterates through all parsed advancements or stats and their criteria to identify
 * criteria that have the same root_name. If a criterion is found in more than one
 * advancement or stat, its 'is_shared' flag is set to true. This allows the rendering
 * logic to visually distinguish them, for example, by overlaying the parent
 * advancement's or stat's icon.
 *
 * @param cats1 An array of TrackableCategory pointers representing advancements or stats.
 * @param count1 The number of advancements or stats in the cats1 array.
 * @param cats2 An array of TrackableCategory pointers representing advancements or stats.
 * @param count2 The number of advancements or stats in the cats2 array.
 */
static void tracker_detect_shared_sub_items(struct Tracker *t) {
    int total_criteria = t->template_data->total_criteria_count + t->template_data->stat_total_criteria_count;
    if (total_criteria == 0) return;

    CriterionCounter *counts = calloc(total_criteria, sizeof(CriterionCounter));
    if (!counts) return;

    int unique_count = 0;
    unique_count = count_all_sub_items(&counts, total_criteria, unique_count, t->template_data->advancements,
                                       t->template_data->advancement_count);
    unique_count = count_all_sub_items(&counts, total_criteria, unique_count, t->template_data->stats,
                                       t->template_data->stat_count);

    flag_shared_sub_items(counts, unique_count, t->template_data->advancements, t->template_data->advancement_count);
    flag_shared_sub_items(counts, unique_count, t->template_data->stats, t->template_data->stat_count);

    free(counts);
    counts = NULL;
    printf("[TRACKER] Shared sub-item detection complete.\n");
}

/**
 * @brief Parses a cJSON array of simple trackable items (like unlocks or custom goals) into an array of TrackableItem structs.
 *
 * This function iterates through a JSON array, allocating and populating a TrackableItem for each entry.
 * It extracts the root name, icon path, and goal value from the template and looks up the display name in the language file.
 * The language file uses "stat." and "unlock." prefixes now as well.
 *
 * @param category_json The cJSON array for the "stats" or "unlocks" key from the template file.
 * @param lang_json The cJSON object from the language file to look up display names.
 * @param items_array A pointer to the array of TrackableItem pointers to be populated.
 * @param count A pointer to an integer that will store the number of items parsed.
 * @param lang_key_prefix The prefix to use when looking up display names in the language file.
 */
static void tracker_parse_simple_trackables(cJSON *category_json, cJSON *lang_json, TrackableItem ***items_array,
                                            int *count, const char *lang_key_prefix) {
    if (!category_json) {
        printf("[TRACKER] tracker_parse_simple_trackables: category_json is NULL\n");
        return;
    }
    *count = cJSON_GetArraySize(category_json);
    if (*count == 0) {
        printf("[TRACKER] tracker_parse_simple_trackables: No items found\n");
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

            // Construct the full language key with the prefix (for "stat." or "unlock.")
            char lang_key[256];
            snprintf(lang_key, sizeof(lang_key), "%s%s", lang_key_prefix, new_item->root_name);

            // Get display name from lang file using the new kay
            cJSON *lang_entry = cJSON_GetObjectItem(lang_json, lang_key);
            if (cJSON_IsString(lang_entry)) {
                // If string copy value as display name otherwise root name
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
static void tracker_parse_multi_stage_goals(cJSON *goals_json, cJSON *lang_json, MultiStageGoal ***goals_array,
                                            int *count) {
    (void) lang_json;
    if (!goals_json) {
        printf("[TRACKER] tracker_parse_multi_stage_goals: goals_json is NULL\n");
        *count = 0;
        // goals_array = NULL;
        return;
    }

    *count = cJSON_GetArraySize(goals_json);
    if (*count == 0) {
        printf("[TRACKER] tracker_parse_multi_stage_goals: No goals found\n");
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


        // "multi_stage_goal.<root_name>.display_name"
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
                cJSON *parent_adv = cJSON_GetObjectItem(stage_item_json, "parent_advancement");
                cJSON *root = cJSON_GetObjectItem(stage_item_json, "root_name");
                cJSON *target_val = cJSON_GetObjectItem(stage_item_json, "target");

                if (cJSON_IsString(text))
                    strncpy(new_stage->display_text, text->valuestring,
                            sizeof(new_stage->display_text) - 1);
                if (cJSON_IsString(stage_id))
                    strncpy(new_stage->stage_id, stage_id->valuestring,
                            sizeof(new_stage->stage_id) - 1);
                if (cJSON_IsString(parent_adv))
                    strncpy(new_stage->parent_advancement, parent_adv->valuestring,
                            sizeof(new_stage->parent_advancement) - 1);
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
                    else if (strcmp(type->valuestring, "criterion") == 0) new_stage->type = SUBGOAL_CRITERION;
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

// /**
//  * @brief Updates advancement and criteria progress from a pre-loaded cJSON object.
//  * @param t A pointer to the Tracker struct.
//  * @param player_adv_json The parsed player advancements JSON file.
//  */
// static void tracker_update_advancement_progress(struct Tracker *t, const cJSON *player_adv_json) {
//     if (!player_adv_json) return;
//
//     t->template_data->advancements_completed_count = 0;
//     t->template_data->completed_criteria_count = 0;
//
//     // printf("[TRACKER] Reading player advancements from: %s\n", t->advancements_path);
//
//     for (int i = 0; i < t->template_data->advancement_count; i++) {
//         TrackableCategory *adv = t->template_data->advancements[i]; // Get the current advancement
//         adv->completed_criteria_count = 0; // Reset completed criteria count for this advancement
//
//         cJSON *player_entry = cJSON_GetObjectItem(player_adv_json, adv->root_name);
//         // Get the entry for this advancement
//         if (player_entry) {
//             adv->done = cJSON_IsTrue(cJSON_GetObjectItem(player_entry, "done"));
//             if (adv->done) {
//                 t->template_data->advancements_completed_count++;
//             }
//
//             // Criteria don't have a "done" field, so we just check if the entry exists
//             cJSON *player_criteria = cJSON_GetObjectItem(player_entry, "criteria");
//             if (player_criteria) {
//                 // If the entry exists, we have criteria
//                 for (int j = 0; j < adv->criteria_count; j++) {
//                     TrackableItem *crit = adv->criteria[j];
//                     // Check if the criteria exist -> meaning it's been completed
//                     if (cJSON_HasObjectItem(player_criteria, crit->root_name)) {
//                         crit->done = true;
//                         adv->completed_criteria_count++;
//                     } else {
//                         crit->done = false;
//                     }
//                 }
//             }
//         } else {
//             // If the entry doesn't exist, reset everything
//             adv->done = false;
//             for (int j = 0; j < adv->criteria_count; j++) {
//                 adv->criteria[j]->done = false;
//             }
//         }
//         // Update completed criteria count for this advancement
//         t->template_data->completed_criteria_count += adv->completed_criteria_count;
//     }
// }

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

        // Reset state before each update
        item->done = false;
        item->progress = 0;

        if (item->goal == -1) {
            // INFINITE COUNTER WITH TARGET -1 and MANUAL OVERRIDE
            if (cJSON_IsTrue(item_progress_json)) {
                // Manually overridden to be complete
                item->done = true;
                item->progress = 1; // Set progress to 1 for consistency
            } else if (cJSON_IsNumber(item_progress_json)) {
                // It's a counter, but can't be completed manually
                item->progress = item_progress_json->valueint;
                item->done = false;
            }
        } else if (item->goal > 0) {
            // Normal counter
            if (cJSON_IsNumber(item_progress_json)) {
                item->progress = item_progress_json->valueint;
            }
            item->done = (item->progress >= item->goal);
        } else {
            // Simple toggle (target 0 or not set)
            if (cJSON_IsTrue(item_progress_json)) {
                item->done = true;
                item->progress = 1;
            }
        }
    }
}

/**
 * @brief Updates multi-stage goal progress from preloaded cJSON objects.
 * @param t A pointer to the Tracker struct.
 * @param player_adv_json The parsed player advancements JSON file.
 * @param player_stats_json The parsed player stats JSON file.
 * @param player_unlocks_json The parsed player unlocks JSON file.
 * @param version The game version from the MC_Version enum.
 */
static void tracker_update_multi_stage_progress(struct Tracker *t, const cJSON *player_adv_json,
                                                const cJSON *player_stats_json, const cJSON *player_unlocks_json,
                                                MC_Version version) {
    if (t->template_data->multi_stage_goal_count == 0) return;

    if (!player_adv_json && !player_stats_json) {
        printf(
            "[TRACKER] Failed to load or parse player advancements or player stats file to update multi-stage goal progress.\n");
        return;
    }

    // Iterate through the multi-stage goals
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];

        // Reset and re-evaluate progress from the start (important for world-changes when tracker is running)
        goal->current_stage = 0;

        // Loop through each stage sequentially to determine the current progress
        for (int j = 0; j < goal->stage_count; j++) {
            SubGoal *stage_to_check = goal->stages[j];
            bool stage_completed = false; // Default to false
            int current_progress = 0;
            bool stat_found = false;

            stage_to_check->current_stat_progress = 0;

            // Stop checking if we are on the final stage (SUBGOAL_MANUAL), anything that isn't "stat" or "advancement"
            if (stage_to_check->type == SUBGOAL_MANUAL) break;

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

                case SUBGOAL_STAT: {
                    // Braces to create a new scope
                    if (version <= MC_VERSION_1_6_4) {
                        // For Legacy, we MUST look up the pre-calculated, snapshot-aware progress
                        // First check stats list
                        for (int c_idx = 0; c_idx < t->template_data->stat_count && !stat_found; c_idx++) {
                            for (int s_idx = 0; s_idx < t->template_data->stats[c_idx]->criteria_count; s_idx++) {
                                TrackableItem *sub = t->template_data->stats[c_idx]->criteria[s_idx];
                                if (strcmp(sub->root_name, stage_to_check->root_name) == 0) {
                                    current_progress = sub->progress;
                                    stat_found = true;
                                    break;
                                }
                            }
                        }
                        // If not found, check achievements list
                        if (!stat_found) {
                            for (int a_idx = 0; a_idx < t->template_data->advancement_count; a_idx++) {
                                TrackableCategory *ach = t->template_data->advancements[a_idx];
                                if (strcmp(ach->root_name, stage_to_check->root_name) == 0) {
                                    // For achievements, progress is based on the raw value from the file
                                    cJSON *stats_change = cJSON_GetObjectItem(player_stats_json, "stats-change");
                                    cJSON *stat_entry;
                                    cJSON_ArrayForEach(stat_entry, stats_change) {
                                        cJSON *item = stat_entry->child;
                                        if (item && strcmp(item->string, ach->root_name) == 0) {
                                            current_progress = item->valueint;
                                            stat_found = true;
                                            break;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    } else if (version <= MC_VERSION_1_11_2) {
                        // MID ERA: Parse directly from the flat JSON structure
                        cJSON *stat_entry = cJSON_GetObjectItem(player_stats_json, stage_to_check->root_name);
                        if (cJSON_IsNumber(stat_entry)) {
                            current_progress = stat_entry->valueint;
                            stat_found = true;
                        }
                    } else {
                        // MODERN ERA: Parse directly from the nested "stats" object
                        cJSON *stats_obj = cJSON_GetObjectItem(player_stats_json, "stats");
                        if (stats_obj) {
                            char root_name_copy[192];
                            strncpy(root_name_copy, stage_to_check->root_name, sizeof(root_name_copy) - 1);
                            char *item_key = strchr(root_name_copy, '/');
                            if (item_key) {
                                *item_key = '\0';
                                item_key++;
                                cJSON *category_obj = cJSON_GetObjectItem(stats_obj, root_name_copy);
                                if (category_obj) {
                                    cJSON *stat_value = cJSON_GetObjectItem(category_obj, item_key);
                                    if (cJSON_IsNumber(stat_value)) {
                                        current_progress = stat_value->valueint;
                                        stat_found = true;
                                    }
                                }
                            }
                        }
                    }

                    // Check for completion
                    if (stat_found && current_progress >= stage_to_check->required_progress) {
                        stage_completed = true;
                    }
                    stage_to_check->current_stat_progress = current_progress;
                    break;
                }

                // Can use advancement criteria for a stage
                case SUBGOAL_CRITERION: {
                    // Advancement Criteria are not possible for Legacy (<= 1.6.4)
                    if (version >= MC_VERSION_1_12) {
                        // Modern Era: Check for the criterion key in player_adv_json
                        if (player_adv_json) {
                            cJSON *adv_entry = cJSON_GetObjectItem(player_adv_json, stage_to_check->parent_advancement);
                            if (adv_entry) {
                                cJSON *criteria_obj = cJSON_GetObjectItem(adv_entry, "criteria");
                                if (criteria_obj && cJSON_HasObjectItem(criteria_obj, stage_to_check->root_name)) {
                                    stage_completed = true;
                                }
                            }
                        }
                    } else if (version >= MC_VERSION_1_7_2) {
                        // Mid Era: Check for the biome name in the 'progress' array of player_stats_json (player stats file)
                        if (player_stats_json) {
                            cJSON *ach_entry = cJSON_GetObjectItem(player_stats_json,
                                                                   stage_to_check->parent_advancement);
                            if (ach_entry) {
                                cJSON *progress_array = cJSON_GetObjectItem(ach_entry, "progress");
                                if (cJSON_IsArray(progress_array)) {
                                    cJSON *progress_item;
                                    cJSON_ArrayForEach(progress_item, progress_array) {
                                        if (cJSON_IsString(progress_item) && strcmp(
                                                progress_item->valuestring, stage_to_check->root_name) == 0) {
                                            stage_completed = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    break;
                }

                // Check if certain unlock is done within a stage (25w14craftmine)
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
                    break;
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
 * It first calculates the total number of "steps" (e.g., criteria, sub-stats or stat if no sub-stats, unlocks, custom goals, and multi-stage goals),
 * then the number of completed "steps", and finally calculates the overall progress percentage.
 *
 * @param t A pointer to the Tracker struct.
 *
 */
static void tracker_calculate_overall_progress(struct Tracker *t) {
    if (!t || !t->template_data) return; // || because we can't be sure if the template_data is initialized

    // calculate the total number of "steps"
    int total_steps = 0;
    int completed_steps = 0;

    // Advancements
    total_steps += t->template_data->total_criteria_count;
    completed_steps += t->template_data->completed_criteria_count;

    // Stats:
    // - For multi-criteria stats, each criterion is a step.
    // - For single-criterion stats, the parent category itself is one step.
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        if (stat_cat->criteria_count > 1) {
            // For multi-criteria stats, count each criterion
            total_steps += stat_cat->criteria_count;
            completed_steps += stat_cat->completed_criteria_count;
        } else {
            // For single-criterion stats, count the parent category as one step
            total_steps += 1;
            if (stat_cat->done) completed_steps += 1;
        }
    }

    // Unlocks
    total_steps += t->template_data->unlock_count;
    completed_steps += t->template_data->unlocks_completed_count;

    // Custom goals
    total_steps += t->template_data->custom_goal_count;
    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        if (t->template_data->custom_goals[i]->done) completed_steps++;
    }

    // Multi-Stage Goals
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        total_steps += (t->template_data->multi_stage_goals[i]->stage_count - 1); // Final stage is not counted
        completed_steps += t->template_data->multi_stage_goals[i]->current_stage;
    }

    // Set 100% if no steps are found
    if (total_steps > 0) {
        t->template_data->overall_progress_percentage = ((float) completed_steps / (float) total_steps) * 100.0f;
    } else {
        t->template_data->overall_progress_percentage = 100.0f; // Default to 100% if no trackable items
    }
}

/**
 * @brief Frees an array of TrackableItem pointers.
 *
 * This is used in tracker_free_template_data(). Used for stats, unlocks and custom goals.
 *
 * @param items The array of TrackableItem pointers to be freed.
 * @param count The number of elements in the array.
 */
static void free_trackable_items(TrackableItem **items, int count) {
    if (!items) return;
    for (int i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

/**
 * @brief Frees an array of TrackableCategory pointers.
 *
 * Used within tracker_free_template_data().
 *
 * @param categories TrackableCategory array
 * @param count Number of categories
 */
static void free_trackable_categories(TrackableCategory **categories, int count) {
    if (!categories) return;
    for (int i = 0; i < count; i++) {
        if (categories[i]) {
            // First, free the inner criteria array using the other helper
            free_trackable_items(categories[i]->criteria, categories[i]->criteria_count);

            // Then free the category struct itself
            free(categories[i]);
        }
    }
    free(categories);
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


    // Free categories
    if (td->advancements) {
        free_trackable_categories(td->advancements, td->advancement_count);
    }

    if (td->stats) {
        free_trackable_categories(td->stats, td->stat_count);
    }

    if (td->unlocks) {
        free_trackable_items(td->unlocks, td->unlock_count);
    }

    // Free custom goal data
    if (td->custom_goals) {
        free_trackable_items(td->custom_goals, td->custom_goal_count);
    }

    // Free multi-stage goals data, don't think it works with free_trackable_categories()
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

    td->advancements = NULL;
    td->stats = NULL;
    td->unlocks = NULL;
    td->custom_goals = NULL;
    td->multi_stage_goals = NULL;
}

/**
 * @brief Formats a string like "acquire_hardware" into "Acquire Hardware".
 * It replaces underscores with spaces and capitalizes the first letter of each word.
 * @param input The source string.
 * @param output The buffer to write the formatted string to.
 * @param max_len The size of the output buffer.
 */
static void format_category_string(const char *input, char *output, size_t max_len) {
    if (!input || !output || max_len == 0) return;

    size_t i = 0;
    bool capitalize_next = true;

    while (*input && i < max_len - 1) {
        if (*input == '_') {
            output[i++] = ' ';
            capitalize_next = true;
        } else {
            output[i++] = capitalize_next ? (char) toupper(*input) : *input;
            capitalize_next = false;
        }
        input++;
    }
    output[i] = '\0';
}

/**
 * @brief Formats a time in Minecraft ticks into a DD:HH:MM:SS.MS string.
 * @param ticks The total number of ticks (20 ticks per second).
 * @param output The buffer to write the formatted time string to.
 * @param max_len The size of the output buffer.
 */
static void format_time(long long ticks, char *output, size_t max_len) {
    if (!output || max_len == 0) return;

    long long total_seconds = ticks / 20;
    long long days = total_seconds / 86400;
    long long hours = total_seconds / 3600;
    long long minutes = (total_seconds % 3600) / 60;
    long long seconds = total_seconds % 60;
    long long milliseconds = ticks % 20 * 50;

    // Use a more compact format for the title bar
    if (days > 0) {
        snprintf(output, max_len, "%lld %02lld:%02lld:%02lld.%03lld", days, hours, minutes, seconds, milliseconds);
    } else if (hours > 0) {
        snprintf(output, max_len, "%02lld:%02lld:%02lld.%03lld", hours, minutes, seconds, milliseconds);
    } else if (minutes > 0) {
        snprintf(output, max_len, "%02lld:%02lld.%03lld", minutes, seconds, milliseconds);
    } else {
        snprintf(output, max_len, "%02lld.%03lld", seconds, milliseconds);
    }
}


// ----------------------------------------- END OF STATIC FUNCTIONS -----------------------------------------


bool tracker_new(struct Tracker **tracker, const AppSettings *settings) {
    // Allocate memory for the tracker struct itself
    *tracker = calloc(1, sizeof(struct Tracker));
    if (*tracker == NULL) {
        fprintf(stderr, "[TRACKER] Failed to allocate memory for tracker.\n");
        return false;
    }

    struct Tracker *t = *tracker;

    // Initialize SDL components for the tracker
    if (!tracker_init_sdl(t, settings)) {
        free(t);
        *tracker = NULL;
        tracker = NULL;
        return false;
    }

    // Allocate the main data container
    t->template_data = calloc(1, sizeof(TemplateData));
    if (!t->template_data) {
        fprintf(stderr, "[TRACKER] Failed to allocate memory for template data.\n");
        tracker_free(tracker);
        return false;
    }

    // Ensure snapshot world name is initially empty
    t->template_data->snapshot_world_name[0] = '\0';

    // Initialize paths (also during runtime)
    tracker_reinit_paths(t, settings);

    // Parse the advancement template JSON file
    tracker_load_and_parse_data(t);

    return true; // Success
}

void tracker_events(struct Tracker *t, SDL_Event *event, bool *is_running, bool *settings_opened) {
    (void) t; // Not directly used, but kept for consistency

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
                    // Window move/resize events are handled in main.c
                    default:
                        break;
                }
            }
            break;
        // TODO: Work with mouse events
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            //printf("[TRACKER] Mouse button pressed in tracker.\n");
            // TODO: Make mouse events work to check off custom goals
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

    AppSettings settings;
    settings_load(&settings);
    MC_Version version = settings_get_version_from_string(settings.version_str);

    // Legacy Snapshot Logic
    // If the version is legacy and the current world name doesn't match the snapshot's world name,
    // it means we've loaded a new world and need to take a new snapshot of the global stats
    if (version <= MC_VERSION_1_6_4 && strcmp(t->world_name, t->template_data->snapshot_world_name) != 0) {
        printf("[TRACKER] Legacy world change detected. Taking new stat snapshot for world: %s\n", t->world_name);
        tracker_snapshot_legacy_stats(t);
    }

    // Load all necessary player files ONCE
    cJSON *player_adv_json = NULL; // (strlen(t->advancements_path) > 0) ? cJSON_from_file(t->advancements_path) : NULL;
    cJSON *player_stats_json = (strlen(t->stats_path) > 0) ? cJSON_from_file(t->stats_path) : NULL;
    cJSON *player_unlocks_json = (strlen(t->unlocks_path) > 0) ? cJSON_from_file(t->unlocks_path) : NULL;
    cJSON *settings_json = cJSON_from_file(SETTINGS_FILE_PATH);

    // Version-based Dispatch
    if (version <= MC_VERSION_1_6_4) {
        tracker_update_stats_legacy(t, player_stats_json);
    } else if (version >= MC_VERSION_1_7_2 && version <= MC_VERSION_1_11_2) {
        tracker_update_achievements_and_stats_mid(t, player_stats_json);
    } else if (version >= MC_VERSION_1_12) {
        player_adv_json = (strlen(t->advancements_path) > 0) ? cJSON_from_file(t->advancements_path) : NULL;
        tracker_update_advancements_modern(t, player_adv_json);

        // Needs version for playtime as 1.17 renames minecraft:play_one_minute into minecraft:play_time
        tracker_update_stats_modern(t, player_stats_json, settings_json, version);
        tracker_update_unlock_progress(t, player_unlocks_json); // Just returns if unlocks don't exist
    }

    // Pass the parsed data to the update functions
    // tracker_update_advancement_progress(t, player_adv_json); // TODO: Remove once possible
    // tracker_update_stat_progress(t, player_stats_json, settings_json); // TODO: Remove once possible
    tracker_update_custom_progress(t, settings_json);
    tracker_update_multi_stage_progress(t, player_adv_json, player_stats_json, player_unlocks_json, version);
    tracker_calculate_overall_progress(t); //THIS TRACKS SUB-ADVANCEMENTS AND EVERYTHING ELSE

    // Clean up the parsed JSON objects
    cJSON_Delete(player_adv_json);
    cJSON_Delete(player_stats_json);
    cJSON_Delete(player_unlocks_json);
    cJSON_Delete(settings_json);
}

void tracker_render(struct Tracker *t, const AppSettings *settings) {
    // Set draw color and clear screen
    SDL_SetRenderDrawColor(t->renderer, settings->tracker_bg_color.r, settings->tracker_bg_color.g,
                           settings->tracker_bg_color.b, settings->tracker_bg_color.a);
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

void tracker_reinit_template(struct Tracker *t, const AppSettings *settings) {
    if (!t) return;

    printf("[TRACKER] Re-initializing template...\n");

    // Update the paths from settings.json
    tracker_reinit_paths(t, settings);

    // Free all the old advancement, stat, etc. data
    if (t->template_data) {
        tracker_free_template_data(t->template_data);

        // Reset the entire struct to zero to clear dangling pointers and old counts.
        memset(t->template_data, 0, sizeof(TemplateData));

        // After clearing, ensure the snapshot name is also cleared to force a new snapshot
        t->template_data->snapshot_world_name[0] = '\0';
    }
    // Load and parse data from the new template files
    tracker_load_and_parse_data(t);
}

void tracker_reinit_paths(struct Tracker *t, const AppSettings *settings) {
    if (!t || !settings) return;

    // Copy the template and lang paths and snapshot path (for legacy snapshots if needed)
    strncpy(t->advancement_template_path, settings->template_path, MAX_PATH_LENGTH - 1);
    t->advancement_template_path[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(t->lang_path, settings->lang_path, MAX_PATH_LENGTH - 1);
    t->lang_path[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(t->snapshot_path, settings->snapshot_path, MAX_PATH_LENGTH - 1);
    t->snapshot_path[MAX_PATH_LENGTH - 1] = '\0';


    MC_Version version = settings_get_version_from_string(settings->version_str);

    // Get the final, normalized saves path using the loaded settings
    if (get_saves_path(t->saves_path, MAX_PATH_LENGTH, settings->path_mode, settings->manual_saves_path)) {
        printf("[TRACKER] Using saves path: %s\n", t->saves_path);

        // Find the specific world files using the correct flag
        find_player_data_files(
            t->saves_path,
            version,
            t->world_name,
            t->advancements_path,
            t->stats_path,
            t->unlocks_path,
            MAX_PATH_LENGTH);
    } else {
        fprintf(stderr, "[TRACKER] CRITICAL: Failed to get saves path.\n");

        // Ensure paths are empty
        t->saves_path[0] = '\0';
        t->world_name[0] = '\0';
        t->advancements_path[0] = '\0';
        t->stats_path[0] = '\0';
        t->unlocks_path[0] = '\0';
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
    // False as it's for advancements
    tracker_parse_categories(advancements_json, t->template_data->lang_json, &t->template_data->advancements,
                             &t->template_data->advancement_count, &t->template_data->total_criteria_count,
                             "advancement.", false);

    // True as it's for stats
    tracker_parse_categories(stats_json, t->template_data->lang_json, &t->template_data->stats,
                             &t->template_data->stat_count, &t->template_data->stat_total_criteria_count, "stat.",
                             true);

    // Parse "unlock." prefix for unlocks
    tracker_parse_simple_trackables(unlocks_json, t->template_data->lang_json, &t->template_data->unlocks,
                                    &t->template_data->unlock_count, "unlock.");

    // Parse "custom." prefix for custom goals
    tracker_parse_simple_trackables(custom_json, t->template_data->lang_json, &t->template_data->custom_goals,
                                    &t->template_data->custom_goal_count, "custom.");

    tracker_parse_multi_stage_goals(multi_stage_goals_json, t->template_data->lang_json,
                                    &t->template_data->multi_stage_goals,
                                    &t->template_data->multi_stage_goal_count);

    // Detect and flag criteria that are shared between multiple advancements
    tracker_detect_shared_sub_items(t);

    printf("[TRACKER] Initial template parsing complete.\n");

    // LOADING SNAPSHOT FROM FILE
    tracker_load_snapshot_from_file(t);

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

void tracker_update_title(struct Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data || !settings) return;

    char title_buffer[512];
    char formatted_category[128];
    char formatted_optional_flag[128];
    char formatted_time[64];

    // Format the category and optional flag strings
    format_category_string(settings->category, formatted_category, sizeof(formatted_category));
    format_category_string(settings->optional_flag, formatted_optional_flag, sizeof(formatted_optional_flag));

    format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));

    // Displaying Ach or Adv depending on the version
    // Get version from string
    MC_Version version = settings_get_version_from_string(settings->version_str);

    if (version >= MC_VERSION_1_12) {
        // Creating the title buffer
        snprintf(title_buffer, sizeof(title_buffer),
                 "  Advancely  %s    |    %s    -    %s    -    %s%s    |    Adv: %d/%d    |    Progress: %.2f%%    |    %s IGT",
                 ADVANCELY_VERSION,
                 t->world_name,
                 settings->version_str,
                 formatted_category,
                 formatted_optional_flag,
                 t->template_data->advancements_completed_count,
                 t->template_data->advancement_count,
                 t->template_data->overall_progress_percentage,
                 formatted_time);
    } else {
        snprintf(title_buffer, sizeof(title_buffer),
                 "  Advancely  %s    |    %s    -    %s    -    %s%s    |    Ach: %d/%d    |    Progress: %.2f%%    |    %s IGT",
                 ADVANCELY_VERSION,
                 t->world_name,
                 settings->version_str,
                 formatted_category,
                 formatted_optional_flag,
                 t->template_data->advancements_completed_count,
                 t->template_data->advancement_count,
                 t->template_data->overall_progress_percentage,
                 formatted_time);
    }


    // Putting buffer into Window title
    SDL_SetWindowTitle(t->window, title_buffer);
}

void tracker_print_debug_status(struct Tracker *t) {
    if (!t || !t->template_data) return;

    AppSettings settings;
    settings_load(&settings);
    cJSON *settings_json = cJSON_from_file(SETTINGS_FILE_PATH);
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : NULL;

    // Also load the current game version used
    MC_Version version = settings_get_version_from_string(settings.version_str);

    char formatted_category[128];
    format_category_string(settings.category, formatted_category, sizeof(formatted_category));

    // Format the time to DD:HH:MM:SS.MS
    char formatted_time[128];
    format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));

    printf("\n============================================================\n");
    printf(" World:      %s\n", t->world_name);
    printf(" Version:    %s\n", settings.version_str);

    // When category isn't empty
    if (settings.category[0] != '\0') {
        printf(" Category:   %s\n", formatted_category);
    }
    // When flag isn't empty
    if (settings.optional_flag[0] != '\0') {
        printf(" Flag:       %s\n", settings.optional_flag);
    }
    printf(" Play Time:  %s\n", formatted_time);
    printf("============================================================\n");


    // Check if the run is completed, check both advancement and overall progress
    if (t->template_data->advancements_completed_count >= t->template_data->advancement_count && t->template_data->
        overall_progress_percentage >= 100.0f) {
        printf("\n                  *** RUN COMPLETE! ***\n\n");
        printf("                  Final Time: %s\n\n", formatted_time);
        printf("============================================================\n\n");
    } else {
        // Advancements or Achievements
        if (version >= MC_VERSION_1_12) {
            printf("[Advancements] %d / %d completed\n", t->template_data->advancements_completed_count,
                   t->template_data->advancement_count);
        } else {
            printf("[Achievements] %d / %d completed\n", t->template_data->advancements_completed_count,
                   t->template_data->advancement_count);
        }
        for (int i = 0; i < t->template_data->advancement_count; i++) {
            TrackableCategory *adv = t->template_data->advancements[i];

            const char *status_text;
            // For Legacy only show completed if earned in this session's snapshot
            if (version <= MC_VERSION_1_6_4) {
                bool earned_this_session = adv->done && !adv->done_in_snapshot;
                // status_text = earned_this_session ? "COMPLETED" : "INCOMPLETE";
                // To see pre-existing ones:
                status_text = earned_this_session ? "COMPLETED (New)" : (adv->done ? "COMPLETED (Old)" : "INCOMPLETE");
            } else {
                // For Modern, the 'done' flag is always correct within the local stats file
                status_text = adv->done ? "COMPLETED" : "INCOMPLETE";
            }

            // Only print criteria count if there are more than 1
            if (adv->criteria_count > 1) {
                printf("  - %s (%d/%d criteria): %s\n", adv->display_name, adv->completed_criteria_count,
                       adv->criteria_count,
                       status_text);
            } else {
                printf("  - %s: %s\n", adv->display_name, status_text);
            }


            for (int j = 0; j < adv->criteria_count; j++) {
                TrackableItem *crit = adv->criteria[j];
                // takes translation from the language file otherwise root_name
                // Print if criteria is shared
                printf("    - %s: %s%s\n", crit->display_name, crit->is_shared ? "SHARED - " : "",
                       crit->done ? "DONE" : "NOT DONE");
            }
        }

        // Other categories...

        // Now supporting stats with criteria
        for (int i = 0; i < t->template_data->stat_count; i++) {
            TrackableCategory *stat_cat = t->template_data->stats[i];

            // Hide a no-icon stat ONLY if the version is pre-1.7.2 (legacy)
            // With target value being 0 or NOT EXISTENT and legacy version it should act as hidden stat for multi-stage
            // It's a "tracker-only" stat with no goal, USED WITHIN MULTI-STAGE FOR LEGACY VERSIONS
            if (version < MC_VERSION_1_6_4 && stat_cat->icon_path[0] == '\0') continue;

            const char *status_text;
            cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : NULL;
            if (stat_cat->done) {
                status_text = (stat_cat->is_manually_completed && cJSON_IsTrue(parent_override)) ? "COMPLETED (MANUAL)" : "COMPLETED";
            } else {
                status_text = "INCOMPLETE";
            }

            // Check if this is a single-criterion stat
            if (stat_cat->criteria_count == 1) {
                // --- SINGLE-CRITERION PRINT FORMAT ---
                TrackableItem *sub_stat = stat_cat->criteria[0];

                // Status of the single criterion
                const char *sub_status_text;
                if (sub_stat->done) {
                    sub_status_text = (sub_stat->is_manually_completed && cJSON_IsTrue(parent_override)) ? "DONE (MANUAL)" : "DONE";
                } else {
                    sub_status_text = "NOT DONE";
                }

                // Check if the single criterion has a target greater 0 or -1
                if (sub_stat->goal > 0) {
                    // It's a completable single-criterion stat
                    printf("[Stat] %s: %d / %d - %s\n",
                           stat_cat->display_name,
                           sub_stat->progress,
                           sub_stat->goal,
                           sub_status_text);
                } else if (sub_stat->goal == -1) {
                    // When target is -1 it acts as infinte counter, then goal doesn't get printed
                    // It's a completable single-criterion stat
                    printf("[Stat] %s: %d - %s\n",
                           stat_cat->display_name,
                           sub_stat->progress,
                           sub_status_text);
                } else if (sub_stat->goal == 0 && version <= MC_VERSION_1_6_4) {
                    // When target value would be 0 or NOT EXISTENT, but "icon" key exists somehow
                    // THE "icon" key for this stat SHOULD BE REMOVED to act as hidden stat for multi-stage for legacy
                    printf("[Stat Tracker] %s: %d\n",
                           stat_cat->display_name,
                           sub_stat->progress);
                } else {
                    // When target value is 0 or NOT EXISTENT for mid-era and modern versions
                    // Then it's a MISTAKE IN THE TEMPLATE FILE
                    printf("[Stat] %s: %d\n - HAS GOAL OF %d, which it shouldn't have. This stat can't be completed.\n",
                           stat_cat->display_name,
                           sub_stat->progress,
                           sub_stat->goal);
                }
            } else {
                // Full stat category uses the status of the category, others use sub_status above
                printf("[Stat Category] %s (%d/%d): %s\n",
                       stat_cat->display_name,
                       stat_cat->completed_criteria_count,
                       stat_cat->criteria_count,
                       status_text);

                // Print each sub-stat (criterion)
                for (int j = 0; j < stat_cat->criteria_count; j++) {
                    TrackableItem *sub_stat = stat_cat->criteria[j];
                    const char *sub_status_text;
                    char sub_stat_key[512];
                    snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name, sub_stat->root_name);
                    cJSON *sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : NULL;
                    if (sub_stat->done) {
                        sub_status_text = (sub_stat->is_manually_completed && cJSON_IsTrue(sub_override)) ? "DONE (MANUAL)" : "DONE";
                    } else {
                        sub_status_text = "NOT DONE";
                    }
                    // You could add a check here for sub_stat->goal != -1 if you don't want to print progress for untracked stats
                    printf("  - %s: %s%d / %d - %s\n",
                           sub_stat->display_name,
                           sub_stat->is_shared ? "SHARED - " : "",
                           sub_stat->progress,
                           sub_stat->goal,
                           sub_status_text);
                }
            }
        }


        // Only print unlocks if they exist
        if (t->template_data->unlock_count > 0) {
            printf("[Unlocks] %d / %d completed\n", t->template_data->unlocks_completed_count,
                   t->template_data->unlock_count);
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
            if (custom_goal->goal == -1) {
                // Case 1: Infinite counter. Show the count, or "COMPLETED" if overridden.
                if (custom_goal->done) {
                    printf("[Custom Counter] %s: COMPLETED (MANUAL)\n", custom_goal->display_name);
                } else {
                    printf("[Custom Counter] %s: %d\n", custom_goal->display_name, custom_goal->progress);
                }
            } else if (custom_goal->goal > 0) {
                // Case 2: Normal counter with a target goal.
                printf("[Custom Goal] %s: %d / %d - %s\n",
                       custom_goal->display_name,
                       custom_goal->progress,
                       custom_goal->goal,
                       custom_goal->done ? "COMPLETED" : "INCOMPLETE");
            } else {
                // Case 3: Simple on/off toggle when goal == 0 or not set
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

        // Advancement/Achievement Progress AGAIN
        if (version >= MC_VERSION_1_12) {
            printf("\n[Advancements] %d / %d completed\n", t->template_data->advancements_completed_count,
                   t->template_data->advancement_count);
        } else {
            printf("\n[Achievements] %d / %d completed\n", t->template_data->advancements_completed_count,
                   t->template_data->advancement_count);
        }


        // Overall Progress
        printf("[Overall Progress] %.2f%%\n", t->template_data->overall_progress_percentage);
        printf("============================================================\n\n");
    }
    // Force the output buffer to write to the console immediately
    fflush(stdout);
}
