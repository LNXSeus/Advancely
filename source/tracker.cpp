//
// Created by Linus on 24.06.2025.
//

#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "tracker.h"
#include "init_sdl.h"
#include "path_utils.h"
#include "settings_utils.h"
#include "file_utils.h" // has the cJSON_from_file function
#include "temp_create_utils.h"
#include "global_event_handler.h"

#include <cJSON.h>
#include <cmath>

#include "imgui_internal.h"


/**
 * @brief Loads an SDL_Texture from a file and sets its scale mode.
 * @param renderer The SDL_Renderer to use.
 * @param path The path to the image file.
 * @param scale_mode The SDL_ScaleMode to apply (e.g., SDL_SCALEMODE_NEAREST).
 * @return A pointer to the created SDL_Texture, or nullptr on failure.
 */
static SDL_Texture *load_texture_with_scale_mode(SDL_Renderer *renderer, const char *path, SDL_ScaleMode scale_mode) {
    if (path == nullptr || path[0] == '\0') {
        fprintf(stderr, "[TRACKER - TEXTURE LOAD] Invalid path for texture: %s\n", path);
        return nullptr;
    }

    SDL_Surface *surface = IMG_Load(path);
    if (!surface) {
        fprintf(stderr, "[TRACKER - TEXTURE LOAD] Failed to load image %s: %s\n", path, SDL_GetError());
        return nullptr;
    }

    SDL_Texture *new_texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface); // Clean up the surface after creating the texture
    if (!new_texture) {
        fprintf(stderr, "[TRACKER - TEXTURE LOAD] Failed to create texture from surface %s: %s\n", path,
                SDL_GetError());
        return nullptr;
    }

    SDL_SetTextureScaleMode(new_texture, scale_mode);
    return new_texture;
}

// FOR VERSION-SPECIFIC PARSERS

/**
 * @brief (Era 1: 1.0-1.6.4) Save snapshot to file to simulate local stats.
 *
 * Thus remembers the snapshot even if the tracker is closed.
 *
 * @param t A pointer to the Tracker struct.
 */
static void tracker_save_snapshot_to_file(Tracker *t) {
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
static void tracker_load_snapshot_from_file(Tracker *t) {
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
static void tracker_snapshot_legacy_stats(Tracker *t) {
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
static void tracker_update_stats_legacy(Tracker *t, const cJSON *player_stats_json) {
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
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : nullptr;

    t->template_data->play_time_ticks = 0;
    t->template_data->stats_completed_count = 0;
    t->template_data->stats_completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        stat_cat->completed_criteria_count = 0;

        // Check for parent override
        cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : nullptr;
        bool parent_forced_true = cJSON_IsBool(parent_override) && cJSON_IsTrue(parent_override);
        stat_cat->is_manually_completed = parent_forced_true;

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
            if (stat_cat->criteria_count == 1) {
                // WHEN IT HAS NO SUB-STATS
                sub_override = parent_override;
            } else {
                // Multi-criterion
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                         sub_stat->root_name);
                sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : nullptr;
            }

            bool sub_forced_true = cJSON_IsBool(sub_override) && cJSON_IsTrue(sub_override);
            sub_stat->is_manually_completed = sub_forced_true;


            // Sub-stat is done if it's naturally completed OR manually forced to be true (sub-stat or parent)
            sub_stat->done = naturally_done || sub_forced_true || parent_forced_true;

            // Increment category's completed count
            if (sub_stat->done) {
                stat_cat->completed_criteria_count++;
            }
        }

        // Determine final 'done' status for PARENT category
        bool all_children_done = (stat_cat->criteria_count > 0 && stat_cat->completed_criteria_count >= stat_cat->
                                  criteria_count);

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
static void tracker_update_achievements_and_stats_mid(Tracker *t, const cJSON *player_stats_json) {
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
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : nullptr;

    t->template_data->stats_completed_count = 0;
    t->template_data->stats_completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        // Iterate through stats
        TrackableCategory *stat_cat = t->template_data->stats[i];
        stat_cat->completed_criteria_count = 0;

        cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : nullptr;
        bool parent_forced_true = cJSON_IsBool(parent_override) && cJSON_IsTrue(parent_override);
        stat_cat->is_manually_completed = parent_forced_true;

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
            } else {
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                         sub_stat->root_name);
                sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : nullptr;
            }

            bool sub_forced_true = cJSON_IsBool(sub_override) && cJSON_IsTrue(sub_override);
            sub_stat->is_manually_completed = sub_forced_true;


            // Either naturally done OR manually overridden to be true  (itself or by parent)
            sub_stat->done = naturally_done || sub_forced_true || parent_forced_true;

            // Increment completed count
            if (sub_stat->done) stat_cat->completed_criteria_count++;
        }

        bool all_children_done = (stat_cat->criteria_count > 0 && stat_cat->completed_criteria_count >= stat_cat->
                                  criteria_count);

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
static void tracker_update_advancements_modern(Tracker *t, const cJSON *player_adv_json) {
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
static void tracker_update_stats_modern(Tracker *t, const cJSON *player_stats_json, const cJSON *settings_json,
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
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : nullptr;

    t->template_data->stats_completed_count = 0;
    t->template_data->stats_completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        stat_cat->completed_criteria_count = 0;

        // Override check
        cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : nullptr;
        bool parent_forced_true = cJSON_IsBool(parent_override) && cJSON_IsTrue(parent_override);
        stat_cat->is_manually_completed = parent_forced_true;

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
            } else {
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                         sub_stat->root_name);
                sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : nullptr;
            }
            bool sub_forced_true = cJSON_IsBool(sub_override) && cJSON_IsTrue(sub_override);
            sub_stat->is_manually_completed = sub_forced_true;

            // Either naturally done OR manually overridden to true (itself OR parent)
            sub_stat->done = naturally_done || sub_forced_true || parent_forced_true;

            if (sub_stat->done) stat_cat->completed_criteria_count++;
        }

        bool all_children_done = (stat_cat->criteria_count > 0 && stat_cat->completed_criteria_count >= stat_cat->
                                  criteria_count);

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
 * @param t A pointer to the Tracker object.
 * @param category_json The JSON object containing the categories.
 * @param lang_json The JSON object containing the language keys.
 * @param categories_array A pointer to an array of TrackableCategory pointers to store the parsed categories.
 * @param count A pointer to an integer to store the number of parsed categories.
 * @param total_criteria_count A pointer to an integer to store the total number of criteria across all categories.
 * @param lang_key_prefix The prefix for language keys. (e.g., "advancement." or "stat.")
 * @param is_stat_category A boolean indicating whether the categories are for stats. False means advancements.
 */
static void tracker_parse_categories(Tracker *t, cJSON *category_json, cJSON *lang_json,
                                     TrackableCategory ***categories_array,
                                     int *count, int *total_criteria_count, const char *lang_key_prefix,
                                     bool is_stat_category) {
    if (!category_json) {
        printf("[TRACKER] tracker_parse_categories: category_json is nullptr\n");
        return;
    }

    *count = 0;
    for (cJSON *i = category_json->child; i != nullptr; i = i->next) (*count)++;
    if (*count == 0) return;

    *categories_array = (TrackableCategory **) calloc(*count, sizeof(TrackableCategory *));
    if (!*categories_array) return;

    cJSON *cat_json = category_json->child;
    int i = 0;
    *total_criteria_count = 0;

    while (cat_json) {
        TrackableCategory *new_cat = (TrackableCategory *) calloc(1, sizeof(TrackableCategory));
        if (!new_cat) {
            cat_json = cat_json->next;
            continue;
        }

        if (cat_json->string) {
            strncpy(new_cat->root_name, cat_json->string, sizeof(new_cat->root_name) - 1);
        } else {
            fprintf(stderr, "[TRACKER] PARSE ERROR: Found a JSON item with a nullptr key. Skipping.\n");
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
        if (cJSON_IsString(icon)) {
            char full_icon_path[sizeof(new_cat->icon_path)];

            // Put whatever is in "icon" into "resources/icons/"
            snprintf(full_icon_path, sizeof(full_icon_path), "resources/icons/%s", icon->valuestring);
            strncpy(new_cat->icon_path, full_icon_path, sizeof(new_cat->icon_path) - 1);

            new_cat->texture = load_texture_with_scale_mode(t->renderer, new_cat->icon_path, SDL_SCALEMODE_NEAREST);
        }

        cJSON *criteria_obj = cJSON_GetObjectItem(cat_json, "criteria");
        if (criteria_obj && cJSON_IsObject(criteria_obj) && criteria_obj->child != nullptr) {
            // MULTI-CRITERION CASE
            for (cJSON *c = criteria_obj->child; c != nullptr; c = c->next) new_cat->criteria_count++;
            if (new_cat->criteria_count > 0) {
                new_cat->criteria = (TrackableItem **) calloc(new_cat->criteria_count, sizeof(TrackableItem *));
                *total_criteria_count += new_cat->criteria_count;
                int k = 0;
                for (cJSON *crit_item = criteria_obj->child; crit_item != nullptr; crit_item = crit_item->next) {
                    TrackableItem *new_crit = (TrackableItem *) calloc(1, sizeof(TrackableItem));
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

                        cJSON *crit_icon = cJSON_GetObjectItem(crit_item, "icon");
                        if (cJSON_IsString(crit_icon) && crit_icon->valuestring[0] != '\0') {
                            char full_crit_icon_path[sizeof(new_crit->icon_path)];
                            snprintf(full_crit_icon_path, sizeof(full_crit_icon_path), "resources/icons/%s",
                                     crit_icon->valuestring);
                            strncpy(new_crit->icon_path, full_crit_icon_path, sizeof(new_crit->icon_path) - 1);
                            new_crit->texture = load_texture_with_scale_mode(
                                t->renderer, new_crit->icon_path, SDL_SCALEMODE_NEAREST);
                        }

                        new_cat->criteria[k++] = new_crit;
                    }
                }
            }
        } else {
            // SINGLE-CRITERION SPECIAL CASE
            new_cat->criteria_count = 1;
            *total_criteria_count += 1;
            new_cat->criteria = (TrackableItem **) calloc(1, sizeof(TrackableItem *));
            if (new_cat->criteria) {
                TrackableItem *the_criterion = (TrackableItem *) calloc(1, sizeof(TrackableItem));
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
                    // Copy the parent's icon path and texture to the single criterion
                    strncpy(the_criterion->icon_path, new_cat->icon_path, sizeof(the_criterion->icon_path) - 1);
                    the_criterion->texture = new_cat->texture; // Reuse the already loaded texture
                    new_cat->criteria[0] = the_criterion;
                }
            }
        }
        (*categories_array)[i++] = new_cat;
        cat_json = cat_json->next;
    }
}

// Helper for counting
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
 * @param t The Tracker struct.
 */
static void tracker_detect_shared_sub_items(Tracker *t) {
    int total_criteria = t->template_data->total_criteria_count + t->template_data->stat_total_criteria_count;
    if (total_criteria == 0) return;

    CriterionCounter *counts = (CriterionCounter *) calloc(total_criteria, sizeof(CriterionCounter));
    if (!counts) return;

    int unique_count = 0;
    unique_count = count_all_sub_items(&counts, total_criteria, unique_count, t->template_data->advancements,
                                       t->template_data->advancement_count);
    unique_count = count_all_sub_items(&counts, total_criteria, unique_count, t->template_data->stats,
                                       t->template_data->stat_count);

    flag_shared_sub_items(counts, unique_count, t->template_data->advancements, t->template_data->advancement_count);
    flag_shared_sub_items(counts, unique_count, t->template_data->stats, t->template_data->stat_count);

    free(counts);
    counts = nullptr;
    printf("[TRACKER] Shared sub-item detection complete.\n");
}

/**
 * @brief Parses a cJSON array of simple trackable items (like unlocks or custom goals) into an array of TrackableItem structs.
 *
 * This function iterates through a JSON array, allocating and populating a TrackableItem for each entry.
 * It extracts the root name, icon path, and goal value from the template and looks up the display name in the language file.
 * The language file uses "stat." and "unlock." prefixes now as well.
 *
 *  @param t Pointer to the Tracker struct.
 * @param category_json The cJSON array for the "stats" or "unlocks" key from the template file.
 * @param lang_json The cJSON object from the language file to look up display names.
 * @param items_array A pointer to the array of TrackableItem pointers to be populated.
 * @param count A pointer to an integer that will store the number of items parsed.
 * @param lang_key_prefix The prefix to use when looking up display names in the language file.
 */
static void tracker_parse_simple_trackables(Tracker *t, cJSON *category_json, cJSON *lang_json,
                                            TrackableItem ***items_array,
                                            int *count, const char *lang_key_prefix) {
    (void) t;
    if (!category_json) {
        printf("[TRACKER] tracker_parse_simple_trackables: category_json is nullptr\n");
        return;
    }
    *count = cJSON_GetArraySize(category_json);
    if (*count == 0) {
        printf("[TRACKER] tracker_parse_simple_trackables: No items found\n");
        return;
    }

    *items_array = (TrackableItem **) calloc(*count, sizeof(TrackableItem *));
    if (!*items_array) return;

    cJSON *item_json = nullptr;
    int i = 0;
    cJSON_ArrayForEach(item_json, category_json) {
        TrackableItem *new_item = (TrackableItem *) calloc(1, sizeof(TrackableItem));
        if (new_item) {
            cJSON *root_name_json = cJSON_GetObjectItem(item_json, "root_name");
            if (cJSON_IsString(root_name_json)) {
                strncpy(new_item->root_name, root_name_json->valuestring, sizeof(new_item->root_name) - 1);
            } else {
                // Skip this item if it has no root_name
                free(new_item);
                continue;
            }

            // Conthe full language key with the prefix (for "stat." or "unlock.")
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
                // Append "icon" to "resources/icons/"
                char full_icon_path[sizeof(new_item->icon_path)];
                snprintf(full_icon_path, sizeof(full_icon_path), "resources/icons/%s", icon->valuestring);
                strncpy(new_item->icon_path, full_icon_path, sizeof(new_item->icon_path) - 1);
                new_item->texture = load_texture_with_scale_mode(t->renderer, new_item->icon_path,
                                                                 SDL_SCALEMODE_NEAREST);
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
 *@param t Pointer to the Tracker struct.
 * @param goals_json The cJSON object for the "multi_stage_goals" key from the template file.
 * @param lang_json The cJSON object from the language file (not used here but kept for consistency).
 * @param goals_array A pointer to the array of MultiStageGoal pointers to be populated.
 * @param count A pointer to an integer that will store the number of goals parsed.
 */
static void tracker_parse_multi_stage_goals(Tracker *t, cJSON *goals_json, cJSON *lang_json,
                                            MultiStageGoal ***goals_array,
                                            int *count) {
    (void) t;
    (void) lang_json;
    if (!goals_json) {
        printf("[TRACKER] tracker_parse_multi_stage_goals: goals_json is nullptr\n");
        *count = 0;
        // goals_array = nullptr;
        return;
    }

    *count = cJSON_GetArraySize(goals_json);
    if (*count == 0) {
        printf("[TRACKER] tracker_parse_multi_stage_goals: No goals found\n");
        // goals_array = nullptr;
        return;
    }

    *goals_array = (MultiStageGoal **) calloc(*count, sizeof(MultiStageGoal *));
    if (!*goals_array) {
        fprintf(stderr, "[TRACKER] Failed to allocate memory for MultiStageGoal array.\n");
        *count = 0;
        return;
    }

    cJSON *goal_item_json = nullptr;
    int i = 0;
    cJSON_ArrayForEach(goal_item_json, goals_json) {
        // Iterate through each goal
        MultiStageGoal *new_goal = (MultiStageGoal *) calloc(1, sizeof(MultiStageGoal));
        if (!new_goal) continue;

        // Parse root_name and icon
        cJSON *root_name = cJSON_GetObjectItem(goal_item_json, "root_name");
        cJSON *icon = cJSON_GetObjectItem(goal_item_json, "icon");

        if (cJSON_IsString(root_name))
            strncpy(new_goal->root_name, root_name->valuestring,
                    sizeof(new_goal->root_name) - 1);
        if (cJSON_IsString(icon)) {
            char full_icon_path[sizeof(new_goal->icon_path)];
            snprintf(full_icon_path, sizeof(full_icon_path), "resources/icons/%s", icon->valuestring);
            strncpy(new_goal->icon_path, full_icon_path, sizeof(new_goal->icon_path) - 1);
            new_goal->texture = load_texture_with_scale_mode(t->renderer, new_goal->icon_path, SDL_SCALEMODE_NEAREST);
        }


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
            new_goal->stages = (SubGoal **) calloc(new_goal->stage_count, sizeof(SubGoal *));
            if (!new_goal->stages) {
                free(new_goal);
                continue;
            }

            cJSON *stage_item_json = nullptr;
            int j = 0;
            cJSON_ArrayForEach(stage_item_json, stages_json) {
                SubGoal *new_stage = (SubGoal *) calloc(1, sizeof(SubGoal));
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


/**
 * @brief Updates unlock progress from a pre-loaded cJSON object and counts completed unlocks.
 * @param t A pointer to the Tracker struct.
 * @param player_unlocks_json The parsed player unlocks JSON file.
 */
static void tracker_update_unlock_progress(Tracker *t, const cJSON *player_unlocks_json) {
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
static void tracker_update_custom_progress(Tracker *t, cJSON *settings_json) {
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
static void tracker_update_multi_stage_progress(Tracker *t, const cJSON *player_adv_json,
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
static void tracker_calculate_overall_progress(Tracker *t) {
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

            // Then free the category itself
            free(categories[i]);
        }
    }
    free(categories);
}

/**
 * @brief Frees all dynamically allocated memory within a TemplateData struct.
 *
 * To avoid memory leaks when switching templates during runtime.
 * It only frees the CONTENT of the TemplateData NOT the itself.
 *
 * @param td A pointer to the TemplateData to be freed.
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

    td->advancements = nullptr;
    td->stats = nullptr;
    td->unlocks = nullptr;
    td->custom_goals = nullptr;
    td->multi_stage_goals = nullptr;
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

/**
 * @brief Formats a duration in seconds into a Hh Mm Ss string.
 * @param total_seconds The total number of seconds.
 * @param output The buffer to write the formatted time string to.
 * @param max_len The size of the output buffer.
 */
static void format_time_since_update(float total_seconds, char *output, size_t max_len) {
    if (!output || max_len == 0) return;

    int total_sec_int = (int) total_seconds;
    int hours = total_sec_int / 3600;
    int minutes = (total_sec_int % 3600) / 60;
    int seconds = total_sec_int % 60;

    if (hours > 0) {
        snprintf(output, max_len, "%dh %dm %ds ago", hours, minutes, seconds);
    } else if (minutes > 0) {
        snprintf(output, max_len, "%dm %ds ago", minutes, seconds);
    } else {
        snprintf(output, max_len, "%ds ago", seconds);
    }
}


// ----------------------------------------- END OF STATIC FUNCTIONS -----------------------------------------


bool tracker_new(Tracker **tracker, const AppSettings *settings) {
    // Allocate memory for the tracker itself
    *tracker = (Tracker *) malloc(sizeof(Tracker));
    if (*tracker == nullptr) {
        fprintf(stderr, "[TRACKER] Failed to allocate memory for tracker.\n");
        return false;
    }

    Tracker *t = *tracker;

    // Explicitly initialize all members
    t->window = nullptr;
    t->renderer = nullptr;
    t->template_data = nullptr;

    // Initialize all string buffers to empty strings
    t->advancement_template_path[0] = '\0';
    t->lang_path[0] = '\0';
    t->saves_path[0] = '\0';
    t->world_name[0] = '\0';
    t->advancements_path[0] = '\0';
    t->unlocks_path[0] = '\0';
    t->stats_path[0] = '\0';
    t->snapshot_path[0] = '\0';

    // Initialize camera and zoom
    t->camera_offset = ImVec2(0.0f, 0.0f);
    t->zoom_level = 1.0f;
    t->layout_locked = false;
    t->locked_layout_width = 0.0f;

    // Initialize time since last update
    t->time_since_last_update = 0.0f;

    // Initialize SDL components for the tracker
    if (!tracker_init_sdl(t, settings)) {
        free(t);
        *tracker = nullptr;
        tracker = nullptr;
        return false;
    }

    // Initialize SDL_ttf
    t->minecraft_font = TTF_OpenFont("resources/fonts/Minecraft.ttf", 24);
    if (!t->minecraft_font) {
        fprintf(stderr, "[TRACKER] Failed to load Minecraft font: %s\n", SDL_GetError());
        tracker_free(tracker);
        return false;
    }

    // Load global background textures
    t->adv_bg = load_texture_with_scale_mode(t->renderer, "resources/gui/advancement_background.png",
                                             SDL_SCALEMODE_NEAREST);
    t->adv_bg_half_done = load_texture_with_scale_mode(t->renderer,
                                                       "resources/gui/advancement_background_half_done.png",
                                                       SDL_SCALEMODE_NEAREST);
    t->adv_bg_done = load_texture_with_scale_mode(t->renderer, "resources/gui/advancement_background_done.png",
                                                  SDL_SCALEMODE_NEAREST);
    if (!t->adv_bg || !t->adv_bg_half_done || !t->adv_bg_done) {
        fprintf(stderr, "[TRACKER] Failed to load advancement background textures.\n");
        tracker_free(tracker);
        return false;
    }

    // Allocate the main data container
    t->template_data = (TemplateData *) calloc(1, sizeof(TemplateData));
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

void tracker_events(Tracker *t, SDL_Event *event, bool *is_running, bool *settings_opened) {
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
void tracker_update(Tracker *t, float *deltaTime) {
    // Use deltaTime for animations
    // game logic goes here
    (void) deltaTime;

    struct AppSettings settings;
    settings_load(&settings);
    MC_Version version = settings_get_version_from_string(settings.version_str);

    // Legacy Snapshot Logic
    // ONLY USING SNAPSHOTTING LOGIC IF *NOT* USING StatsPerWorld MOD
    // If StatsPerWorld is enabled, we don't need to take snapshots, but read directly from the
    // per-world stat files, like mid-era versions, but still reading with IDs and not strings
    // If the version is legacy and the current world name doesn't match the snapshot's world name,
    // it means we've loaded a new world and need to take a new snapshot of the global stats
    if (version <= MC_VERSION_1_6_4 && !settings.using_stats_per_world_legacy && strcmp(
            t->world_name, t->template_data->snapshot_world_name) != 0) {
        printf("[TRACKER] Legacy world change detected. Taking new stat snapshot for world: %s\n", t->world_name);
        tracker_snapshot_legacy_stats(t); // Take a new snapshot when StatsPerWorld is disabled in legacy version
    }

    // Load all necessary player files ONCE
    cJSON *player_adv_json = nullptr;
    // (strlen(t->advancements_path) > 0) ? cJSON_from_file(t->advancements_path) : nullptr;
    cJSON *player_stats_json = (strlen(t->stats_path) > 0) ? cJSON_from_file(t->stats_path) : nullptr;
    cJSON *player_unlocks_json = (strlen(t->unlocks_path) > 0) ? cJSON_from_file(t->unlocks_path) : nullptr;
    cJSON *settings_json = cJSON_from_file(SETTINGS_FILE_PATH);

    // Version-based Dispatch
    if (version <= MC_VERSION_1_6_4) {
        // If StatsPerWorld mod is enabled, stats file is per-world, still using IDs
        tracker_update_stats_legacy(t, player_stats_json);
    } else if (version >= MC_VERSION_1_7_2 && version <= MC_VERSION_1_11_2) {
        tracker_update_achievements_and_stats_mid(t, player_stats_json);
    } else if (version >= MC_VERSION_1_12) {
        player_adv_json = (strlen(t->advancements_path) > 0) ? cJSON_from_file(t->advancements_path) : nullptr;
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

void tracker_render(Tracker *t, const AppSettings *settings) {
    (void) t;
    (void) settings;
    // Set draw color and clear screen
    // SDL_SetRenderDrawColor(t->renderer, settings->tracker_bg_color.r, settings->tracker_bg_color.g,
    //                        settings->tracker_bg_color.b, settings->tracker_bg_color.a);
    // SDL_RenderClear(t->renderer); // TODO: Remove this, in main function directly


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
    // SDL_RenderPresent(t->renderer);
}


// -------------------------------------------- TRACKER RENDERING START --------------------------------------------


/**
 * @brief Helper to draw a separator line with a title for a new section.
 *
 * @param t The tracker instance.
 * @param current_y The current y position in the world.
 * @param title The title of the section.
 * @param text_color The text color for the title.
 */
static void render_section_separator(Tracker *t, const AppSettings *settings, float &current_y, const char *title,
                                     ImU32 text_color) {
    ImGuiIO &io = ImGui::GetIO();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    float zoom = t->zoom_level;

    current_y += 40.0f; // Padding before the separator

    // Measure the text
    ImVec2 text_size = ImGui::CalcTextSize(title);
    float screen_width_in_world = io.DisplaySize.x / zoom;

    // Center text in screen space (ignore camera_offset.x so it doesn't shift with the world)
    float text_pos_x_in_world = (screen_width_in_world - text_size.x) / 2.0f;
    ImVec2 final_text_pos = ImVec2(text_pos_x_in_world * zoom,
                                   current_y * zoom + t->camera_offset.y);

    draw_list->AddText(nullptr, 18.0f * zoom, final_text_pos, text_color, title);

    // Draw a shorter, centered line (40% of the visible width)
    float line_width_in_world = screen_width_in_world * TRACKER_SEPARATOR_LINE_WIDTH;
    float line_start_x_in_world = (screen_width_in_world - line_width_in_world) / 2.0f;
    float line_end_x_in_world = line_start_x_in_world + line_width_in_world;

    ImVec2 line_start = ImVec2(line_start_x_in_world * zoom,
                               (current_y + 30) * zoom + t->camera_offset.y);
    ImVec2 line_end = ImVec2(line_end_x_in_world * zoom,
                             (current_y + 30) * zoom + t->camera_offset.y);

    draw_list->AddLine(line_start, line_end,
                       IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                ADVANCELY_FADED_ALPHA), 1.0f * zoom);

    current_y += 50.0f; // Padding after the separator
}


/**
 * @brief Renders a section of items that are TrackableCategories (e.g., Advancements, Stats).
 * This function handles the uniform grid layout and the two-pass rendering for simple vs. complex items.
 * It distinguishes between advancements and stats with the bool flag and manages all the checkbox logic,
 * communicating with the settings.json file.
 * @param t The tracker instance.
 * @param settings The app settings.
 * @param current_y The current y position in the world.
 * @param categories The array of TrackableCategory pointers.
 * @param count The number of TrackableCategory pointers in the array.
 * @param section_title The title of the section.
 * @param is_stat_section True if the section is for stats, false if it's for advancements.
 */
static void render_trackable_category_section(Tracker *t, const AppSettings *settings, float &current_y,
                                              TrackableCategory **categories, int count, const char *section_title,
                                              bool is_stat_section) {
    int visible_count = 0;
    for (int i = 0; i < count; ++i) {
        if (categories[i] && (!categories[i]->done || !settings->remove_completed_goals)) {
            visible_count++;
            break;
        }
    }
    if (visible_count == 0) return;

    ImGuiIO &io = ImGui::GetIO();

    // Use the locked width if layout is locked
    float wrapping_width = t->layout_locked ? t->locked_layout_width : (io.DisplaySize.x / t->zoom_level);


    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 text_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                settings->text_color.a);
    ImU32 text_color_faded = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b, 100);
    ImU32 icon_tint_faded = IM_COL32(255, 255, 255, 100);

    // Define checkbox colors from settings
    ImU32 checkmark_color = text_color;
    ImU32 checkbox_fill_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                         ADVANCELY_FADED_ALPHA);
    ImU32 checkbox_hover_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                          (int)fminf(255.0f, ADVANCELY_FADED_ALPHA + 60));

    render_section_separator(t, settings, current_y, section_title, text_color);

    float uniform_item_width = 0.0f;
    for (int i = 0; i < count; i++) {
        TrackableCategory *cat = categories[i];
        if (!cat || (cat->done && settings->remove_completed_goals)) continue;

        float required_width = ImGui::CalcTextSize(cat->display_name).x;
        if (cat->criteria_count > 1) {
            for (int j = 0; j < cat->criteria_count; j++) {
                TrackableItem *crit = cat->criteria[j];
                if (crit && (!crit->done || !settings->remove_completed_goals)) {
                    // FIX: Update width calculation for new layout: [Icon] [Checkbox] [Text] (Progress)
                    char crit_progress_text[32] = "";
                    if (is_stat_section) {
                        if (crit->goal > 0) {
                            snprintf(crit_progress_text, sizeof(crit_progress_text), "(%d / %d)", crit->progress,
                                     crit->goal);
                        } else if (crit->goal == -1) {
                            snprintf(crit_progress_text, sizeof(crit_progress_text), "(%d)", crit->progress);
                        }
                    }
                    float crit_text_width = ImGui::CalcTextSize(crit->display_name).x;
                    float crit_progress_width = ImGui::CalcTextSize(crit_progress_text).x;
                    // Icon(32) + pad + Checkbox(20) + pad + Text + pad + Progress
                    float total_crit_width = 32 + 4 + 20 + 4 + crit_text_width + (crit_progress_width > 0
                                                 ? 4 + crit_progress_width
                                                 : 0);
                    required_width = fmaxf(required_width, total_crit_width);
                }
            }
        }
        uniform_item_width = fmaxf(uniform_item_width, fmaxf(96.0f, required_width));
    }

    float padding = 50.0f, current_x = padding, row_max_height = 0.0f;
    const float horizontal_spacing = 32.0f, vertical_spacing = 48.0f;

    auto render_pass = [&](bool complex_pass) {
        for (int i = 0; i < count; i++) {
            TrackableCategory *cat = categories[i];
            bool is_complex = cat && cat->criteria_count > 1;
            if (!cat || (is_complex != complex_pass) || (cat->done && settings->remove_completed_goals)) continue;

            char progress_text[32] = "";
            if (is_stat_section) {
                if (is_complex) {
                    snprintf(progress_text, sizeof(progress_text), "(%d / %d)", cat->completed_criteria_count,
                             cat->criteria_count);
                } else if (cat->criteria_count == 1) {
                    TrackableItem *crit = cat->criteria[0];
                    if (crit->goal > 0) {
                        snprintf(progress_text, sizeof(progress_text), "(%d / %d)", crit->progress, crit->goal);
                    } else if (crit->goal == -1) {
                        snprintf(progress_text, sizeof(progress_text), "(%d)", crit->progress);
                    }
                }
            }

            ImVec2 text_size = ImGui::CalcTextSize(cat->display_name);
            ImVec2 progress_text_size = ImGui::CalcTextSize(progress_text);
            int visible_criteria = 0;
            if (is_complex) {
                for (int j = 0; j < cat->criteria_count; j++)
                    if (
                        cat->criteria[j] && (!cat->criteria[j]->done || !settings->remove_completed_goals))
                        visible_criteria
                                ++;
            }
            float item_height = 96.0f + text_size.y + 4.0f + ((float) visible_criteria * 36.0f);
            if (progress_text[0] != '\0') item_height += progress_text_size.y + 4.0f;

            if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
                current_x = padding;
                current_y += row_max_height;
                row_max_height = 0.0f;
            }

            ImVec2 screen_pos = ImVec2((current_x * t->zoom_level) + t->camera_offset.x,
                                       (current_y * t->zoom_level) + t->camera_offset.y);
            ImVec2 bg_size = ImVec2(96.0f, 96.0f);

            SDL_Texture *bg_texture_to_use = t->adv_bg; // Default to normal background
            if (cat->done) {
                bg_texture_to_use = t->adv_bg_done;
            } else {
                bool has_progress = false;
                if (is_complex) {
                    has_progress = cat->completed_criteria_count > 0;
                } else if (cat->criteria_count == 1) {
                    has_progress = cat->criteria[0]->progress > 0;
                }
                if (has_progress) {
                    bg_texture_to_use = t->adv_bg_half_done;
                }
            }

            if (bg_texture_to_use)
                draw_list->AddImage((void *) bg_texture_to_use, screen_pos,
                                    ImVec2(screen_pos.x + bg_size.x * t->zoom_level,
                                           screen_pos.y + bg_size.y * t->zoom_level));
            if (cat->texture)
                draw_list->AddImage((void *) cat->texture,
                                    ImVec2(screen_pos.x + 16.0f * t->zoom_level,
                                           screen_pos.y + 16.0f * t->zoom_level),
                                    ImVec2(screen_pos.x + 80.0f * t->zoom_level,
                                           screen_pos.y + 80.0f * t->zoom_level));

            float text_y_pos = screen_pos.y + bg_size.y * t->zoom_level;
            draw_list->AddText(nullptr, 16.0f * t->zoom_level,
                               ImVec2(screen_pos.x + (bg_size.x * t->zoom_level - text_size.x * t->zoom_level) * 0.5f,
                                      text_y_pos), text_color, cat->display_name);

            if (progress_text[0] != '\0') {
                text_y_pos += text_size.y * t->zoom_level + 4.0f;
                draw_list->AddText(nullptr, 14.0f * t->zoom_level,
                                   ImVec2(
                                       screen_pos.x + (bg_size.x * t->zoom_level - progress_text_size.x * t->zoom_level)
                                       * 0.5f, text_y_pos), text_color, progress_text);
            }

            if (is_complex) {
                float sub_item_y_offset = current_y + bg_size.y + text_size.y + 4.0f;
                if (progress_text[0] != '\0') sub_item_y_offset += progress_text_size.y + 4.0f;
                sub_item_y_offset += 12.0f;

                for (int j = 0; j < cat->criteria_count; j++) {
                    TrackableItem *crit = cat->criteria[j];
                    if (!crit || (crit->done && settings->remove_completed_goals)) continue;

                    ImVec2 crit_base_pos = ImVec2((current_x * t->zoom_level) + t->camera_offset.x,
                                                  (sub_item_y_offset * t->zoom_level) + t->camera_offset.y);
                    float current_element_x = crit_base_pos.x;

                    // Draw Icon
                    if (crit->texture) {
                        ImU32 icon_tint = crit->done ? icon_tint_faded : IM_COL32_WHITE;
                        draw_list->AddImage((void *) crit->texture, crit_base_pos,
                                            ImVec2(crit_base_pos.x + 32 * t->zoom_level,
                                                   crit_base_pos.y + 32 * t->zoom_level), ImVec2(0, 0), ImVec2(1, 1),
                                            icon_tint);
                    }
                    current_element_x += 32 * t->zoom_level + 4 * t->zoom_level;

                    // Draw Checkbox for Sub-Stat
                    // Draw Checkbox for Sub-Stat
                    if (is_stat_section) {
                        ImVec2 check_pos = ImVec2(current_element_x, crit_base_pos.y + 6 * t->zoom_level);
                        ImRect checkbox_rect(
                            check_pos, ImVec2(check_pos.x + 20 * t->zoom_level, check_pos.y + 20 * t->zoom_level));
                        bool is_hovered = ImGui::IsMouseHoveringRect(checkbox_rect.Min, checkbox_rect.Max);
                        ImU32 check_fill_color = is_hovered ? checkbox_hover_color : checkbox_fill_color;
                        draw_list->AddRectFilled(checkbox_rect.Min, checkbox_rect.Max, check_fill_color,
                                                 3.0f * t->zoom_level);
                        draw_list->AddRect(checkbox_rect.Min, checkbox_rect.Max, text_color, 3.0f * t->zoom_level);

                        if (crit->is_manually_completed) {
                            ImVec2 p1 = ImVec2(check_pos.x + 5 * t->zoom_level, check_pos.y + 10 * t->zoom_level);
                            ImVec2 p2 = ImVec2(check_pos.x + 9 * t->zoom_level, check_pos.y + 15 * t->zoom_level);
                            ImVec2 p3 = ImVec2(check_pos.x + 15 * t->zoom_level, check_pos.y + 6 * t->zoom_level);
                            draw_list->AddLine(p1, p2, checkmark_color, 2.0f * t->zoom_level);
                            draw_list->AddLine(p2, p3, checkmark_color, 2.0f * t->zoom_level);
                        }

                        if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            crit->is_manually_completed = !crit->is_manually_completed;
                            crit->done = crit->is_manually_completed
                                             ? true
                                             : (crit->progress >= crit->goal && crit->goal > 0);
                            settings_save(settings, t->template_data);
                            SDL_SetAtomicInt(&g_needs_update, 1);
                        }
                        current_element_x += 20 * t->zoom_level + 4 * t->zoom_level;
                    }

                    // Draw Text and Progress
                    ImU32 current_text_color = crit->done ? text_color_faded : text_color;
                    draw_list->AddText(nullptr, 14.0f * t->zoom_level,
                                       ImVec2(current_element_x, crit_base_pos.y + 8 * t->zoom_level),
                                       current_text_color, crit->display_name);
                    current_element_x += ImGui::CalcTextSize(crit->display_name).x * t->zoom_level + 4 * t->zoom_level;

                    char crit_progress_text[32] = "";
                    if (is_stat_section) {
                        if (crit->goal > 0) {
                            snprintf(crit_progress_text, sizeof(crit_progress_text), "(%d / %d)", crit->progress,
                                     crit->goal);
                        } else if (crit->goal == -1) {
                            snprintf(crit_progress_text, sizeof(crit_progress_text), "(%d)", crit->progress);
                        }
                        if (crit_progress_text[0] != '\0') {
                            draw_list->AddText(nullptr, 14.0f * t->zoom_level,
                                               ImVec2(current_element_x, crit_base_pos.y + 8 * t->zoom_level),
                                               current_text_color, crit_progress_text);
                        }
                    }

                    sub_item_y_offset += 36.0f;
                }
            }

            if (is_stat_section) {
                ImVec2 check_pos = ImVec2(screen_pos.x + 70 * t->zoom_level, screen_pos.y + 5 * t->zoom_level);
                ImRect checkbox_rect(check_pos, ImVec2(check_pos.x + 20 * t->zoom_level,
                                                       check_pos.y + 20 * t->zoom_level));
                bool is_hovered = ImGui::IsMouseHoveringRect(checkbox_rect.Min, checkbox_rect.Max);
                ImU32 check_fill_color = is_hovered ? checkbox_hover_color : checkbox_fill_color;
                draw_list->AddRectFilled(checkbox_rect.Min, checkbox_rect.Max, check_fill_color, 3.0f * t->zoom_level);
                draw_list->AddRect(checkbox_rect.Min, checkbox_rect.Max, text_color, 3.0f * t->zoom_level);

                // Only draw checkmark if manually completed
                if (cat->is_manually_completed) {
                    ImVec2 p1 = ImVec2(check_pos.x + 5 * t->zoom_level, check_pos.y + 10 * t->zoom_level);
                    ImVec2 p2 = ImVec2(check_pos.x + 9 * t->zoom_level, check_pos.y + 15 * t->zoom_level);
                    ImVec2 p3 = ImVec2(check_pos.x + 15 * t->zoom_level, check_pos.y + 6 * t->zoom_level);
                    draw_list->AddLine(p1, p2, checkmark_color, 2.0f * t->zoom_level);
                    draw_list->AddLine(p2, p3, checkmark_color, 2.0f * t->zoom_level);
                }

                if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    cat->is_manually_completed = !cat->is_manually_completed;
                    bool is_naturally_done = (cat->completed_criteria_count >= cat->criteria_count && cat->
                                              criteria_count > 0);
                    cat->done = cat->is_manually_completed || is_naturally_done;

                    for (int j = 0; j < cat->criteria_count; ++j) {
                        TrackableItem *crit = cat->criteria[j];
                        bool crit_is_naturally_done = (crit->goal > 0 && crit->progress >= crit->goal);
                        // Child is done if parent forces it, it forces itself, or it's naturally done
                        crit->done = cat->is_manually_completed || crit->is_manually_completed ||
                                     crit_is_naturally_done;
                    }
                    settings_save(settings, t->template_data);
                    SDL_SetAtomicInt(&g_needs_update, 1);
                }
            }
            current_x += uniform_item_width + horizontal_spacing;
            row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
        }
    };

    render_pass(false);
    render_pass(true);
    current_y += row_max_height;
}

/**
 * @brief Renders a section of items that are simple TrackableItems (e.g., Unlocks).
 */
static void render_simple_item_section(Tracker *t, const AppSettings *settings, float &current_y, TrackableItem **items,
                                       int count, const char *section_title) {
    int visible_count = 0;
    for (int i = 0; i < count; ++i) {
        if (items[i] && (!items[i]->done || !settings->remove_completed_goals)) {
            visible_count++;
            break;
        }
    }
    if (visible_count == 0) return;

    ImGuiIO &io = ImGui::GetIO();

    // Use locked width if layout is locked
    float wrapping_width = t->layout_locked ? t->locked_layout_width : (io.DisplaySize.x / t->zoom_level);


    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 text_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                settings->text_color.a);

    render_section_separator(t, settings, current_y, section_title, text_color);

    float uniform_item_width = 0.0f;
    for (int i = 0; i < count; i++) {
        TrackableItem *item = items[i];
        if (!item || (item->done && settings->remove_completed_goals)) continue;
        uniform_item_width = fmaxf(uniform_item_width, fmaxf(96.0f, ImGui::CalcTextSize(item->display_name).x));
    }

    float padding = 50.0f, current_x = padding, row_max_height = 0.0f;
    const float horizontal_spacing = 32.0f, vertical_spacing = 48.0f;

    for (int i = 0; i < count; i++) {
        TrackableItem *item = items[i];
        if (!item || (item->done && settings->remove_completed_goals)) continue;

        float item_height = 96.0f + ImGui::CalcTextSize(item->display_name).y + 4.0f;
        if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
            current_x = padding;
            current_y += row_max_height;
            row_max_height = 0.0f;
        }

        ImVec2 screen_pos = ImVec2((current_x * t->zoom_level) + t->camera_offset.x,
                                   (current_y * t->zoom_level) + t->camera_offset.y);
        ImVec2 bg_size = ImVec2(96.0f, 96.0f);
        SDL_Texture *bg_texture = item->done ? t->adv_bg_done : t->adv_bg;
        if (bg_texture)
            draw_list->AddImage((void *) bg_texture, screen_pos,
                                ImVec2(screen_pos.x + bg_size.x * t->zoom_level,
                                       screen_pos.y + bg_size.y * t->zoom_level));
        if (item->texture)
            draw_list->AddImage((void *) item->texture,
                                ImVec2(screen_pos.x + 16.0f * t->zoom_level,
                                       screen_pos.y + 16.0f * t->zoom_level),
                                ImVec2(screen_pos.x + 80.0f * t->zoom_level,
                                       screen_pos.y + 80.0f * t->zoom_level));
        ImVec2 text_size = ImGui::CalcTextSize(item->display_name);
        draw_list->AddText(nullptr, 16.0f * t->zoom_level,
                           ImVec2(screen_pos.x + (bg_size.x * t->zoom_level - text_size.x * t->zoom_level) * 0.5f,
                                  screen_pos.y + bg_size.y * t->zoom_level), text_color, item->display_name);

        current_x += uniform_item_width + horizontal_spacing;
        row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
    }
    current_y += row_max_height;
}

/**
 * @brief Renders the Custom Goals section with interactive checkboxes.
 */
static void render_custom_goals_section(Tracker *t, const AppSettings *settings, float &current_y,
                                        const char *section_title) {
    int count = t->template_data->custom_goal_count;
    // Pre-check for visible items
    int visible_count = 0;
    for (int i = 0; i < count; ++i) {
        if (t->template_data->custom_goals[i] && (
                !t->template_data->custom_goals[i]->done || !settings->remove_completed_goals)) {
            visible_count++;
            break;
        }
    }
    if (visible_count == 0) return;

    ImGuiIO &io = ImGui::GetIO();


    // Use locked width if layout is locked
    float wrapping_width = t->layout_locked ? t->locked_layout_width : (io.DisplaySize.x / t->zoom_level);


    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 text_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                settings->text_color.a);

    // Define checkbox colors from settings
    ImU32 checkmark_color = text_color;
    ImU32 checkbox_fill_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                         ADVANCELY_FADED_ALPHA);
    ImU32 checkbox_hover_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                          (int)fminf(255.0f, ADVANCELY_FADED_ALPHA + 60));

    render_section_separator(t, settings, current_y, section_title, text_color);

    float uniform_item_width = 0.0f;
    for (int i = 0; i < count; i++) {
        TrackableItem *item = t->template_data->custom_goals[i];
        if (!item || (item->done && settings->remove_completed_goals)) continue;
        uniform_item_width = fmaxf(uniform_item_width, fmaxf(96.0f, ImGui::CalcTextSize(item->display_name).x));
    }

    float padding = 50.0f, current_x = padding, row_max_height = 0.0f;
    const float horizontal_spacing = 32.0f, vertical_spacing = 48.0f;

    for (int i = 0; i < count; i++) {
        TrackableItem *item = t->template_data->custom_goals[i];
        if (!item || (item->done && settings->remove_completed_goals)) continue;

        char progress_text[32] = "";
        if (item->goal > 0) snprintf(progress_text, sizeof(progress_text), "(%d / %d)", item->progress, item->goal);
        else if (item->goal == -1) snprintf(progress_text, sizeof(progress_text), "(%d)", item->progress);

        ImVec2 text_size = ImGui::CalcTextSize(item->display_name);
        ImVec2 progress_text_size = ImGui::CalcTextSize(progress_text);
        float item_height = 96.0f + text_size.y + 4.0f + (item->goal != 0 ? progress_text_size.y + 4.0f : 0);

        if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
            current_x = padding;
            current_y += row_max_height;
            row_max_height = 0.0f;
        }

        ImVec2 screen_pos = ImVec2((current_x * t->zoom_level) + t->camera_offset.x,
                                   (current_y * t->zoom_level) + t->camera_offset.y);
        ImVec2 bg_size = ImVec2(96.0f, 96.0f);
        SDL_Texture *bg_texture = item->done ? t->adv_bg_done : t->adv_bg;

        // Select background based on progress for counters, half-done when in between
        if (item->done) {
            bg_texture = t->adv_bg_done;
        } else if ((item->goal > 0 || item->goal == -1) && item->progress > 0) {
            // Specifically checking for -1 as well
            bg_texture = t->adv_bg_half_done;
        } else {
            bg_texture = t->adv_bg;
        }

        if (bg_texture)
            draw_list->AddImage((void *) bg_texture, screen_pos,
                                ImVec2(screen_pos.x + bg_size.x * t->zoom_level,
                                       screen_pos.y + bg_size.y * t->zoom_level));
        if (item->texture)
            draw_list->AddImage((void *) item->texture,
                                ImVec2(screen_pos.x + 16.0f * t->zoom_level,
                                       screen_pos.y + 16.0f * t->zoom_level),
                                ImVec2(screen_pos.x + 80.0f * t->zoom_level,
                                       screen_pos.y + 80.0f * t->zoom_level));

        draw_list->AddText(nullptr, 16.0f * t->zoom_level,
                           ImVec2(screen_pos.x + (bg_size.x * t->zoom_level - text_size.x * t->zoom_level) * 0.5f,
                                  screen_pos.y + bg_size.y * t->zoom_level), text_color, item->display_name);
        if (progress_text[0] != '\0') {
            draw_list->AddText(nullptr, 14.0f * t->zoom_level,
                               ImVec2(
                                   screen_pos.x + (bg_size.x * t->zoom_level - progress_text_size.x * t->zoom_level) *
                                   0.5f, screen_pos.y + (bg_size.y + text_size.y + 4.0f) * t->zoom_level), text_color,
                               progress_text);
        }

        // Checkbox logic for manual override
        bool can_be_overridden = (item->goal <= 0 || item->goal == -1);
        if (can_be_overridden) {
            ImVec2 check_pos = ImVec2(screen_pos.x + 70 * t->zoom_level, screen_pos.y + 5 * t->zoom_level);
            ImVec2 check_size = ImVec2(20 * t->zoom_level, 20 * t->zoom_level);
            ImRect checkbox_rect(check_pos, ImVec2(check_pos.x + check_size.x, check_pos.y + check_size.y));

            bool is_hovered = ImGui::IsMouseHoveringRect(checkbox_rect.Min, checkbox_rect.Max);
            ImU32 check_fill_color = is_hovered ? checkbox_hover_color : checkbox_fill_color;
            draw_list->AddRectFilled(checkbox_rect.Min, checkbox_rect.Max, check_fill_color, 3.0f * t->zoom_level);
            draw_list->AddRect(checkbox_rect.Min, checkbox_rect.Max, text_color,
                               3.0f * t->zoom_level);

            if (item->done) {
                draw_list->AddLine(ImVec2(check_pos.x + 5 * t->zoom_level, check_pos.y + 10 * t->zoom_level),
                                   ImVec2(check_pos.x + 9 * t->zoom_level, check_pos.y + 15 * t->zoom_level),
                                   checkmark_color, 2.0f * t->zoom_level);
                draw_list->AddLine(ImVec2(check_pos.x + 9 * t->zoom_level, check_pos.y + 15 * t->zoom_level),
                                   ImVec2(check_pos.x + 15 * t->zoom_level, check_pos.y + 6 * t->zoom_level),
                                   checkmark_color, 2.0f * t->zoom_level);
            }

            if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                item->done = !item->done;
                item->is_manually_completed = true;
                settings_save(settings, t->template_data);
                SDL_SetAtomicInt(&g_needs_update, 1);
            }
        }

        current_x += uniform_item_width + horizontal_spacing;
        row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
    }
    current_y += row_max_height;
}

/**
 * @brief Renders the Multi-Stage Goals section.
 */
static void render_multistage_goals_section(Tracker *t, const AppSettings *settings, float &current_y,
                                            const char *section_title) {
    int count = t->template_data->multi_stage_goal_count;
    // Pre-check for visible items
    int visible_count = 0;
    for (int i = 0; i < count; ++i) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        bool is_done = goal && (goal->current_stage >= goal->stage_count - 1);
        if (goal && (!is_done || !settings->remove_completed_goals)) {
            visible_count++;
            break;
        }
    }
    if (visible_count == 0) return;

    ImGuiIO &io = ImGui::GetIO();

    // Use locked width if layout is locked
    float wrapping_width = t->layout_locked ? t->locked_layout_width : (io.DisplaySize.x / t->zoom_level);


    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 text_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                settings->text_color.a);

    render_section_separator(t, settings, current_y, section_title, text_color);

    float uniform_item_width = 0.0f;
    for (int i = 0; i < count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        if (!goal) continue;
        uniform_item_width = fmaxf(uniform_item_width, fmaxf(96.0f, ImGui::CalcTextSize(goal->display_name).x));
    }

    float padding = 50.0f, current_x = padding, row_max_height = 0.0f;
    const float horizontal_spacing = 32.0f, vertical_spacing = 48.0f;

    for (int i = 0; i < count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        bool is_done = goal && (goal->current_stage >= goal->stage_count - 1);

        // Filter out completed goals if the setting is enabled
        if (!goal || (is_done && settings->remove_completed_goals)) continue;

        SubGoal *active_stage = goal->stages[goal->current_stage];
        char stage_text[256];
        if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress > 0) {
            snprintf(stage_text, sizeof(stage_text), "%s (%d/%d)", active_stage->display_text,
                     active_stage->current_stat_progress, active_stage->required_progress);
        } else {
            strncpy(stage_text, active_stage->display_text, sizeof(stage_text));
        }

        ImVec2 text_size = ImGui::CalcTextSize(goal->display_name);
        ImVec2 stage_text_size = ImGui::CalcTextSize(stage_text);
        float item_height = 96.0f + text_size.y + 4.0f + stage_text_size.y + 4.0f;

        if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
            current_x = padding;
            current_y += row_max_height;
            row_max_height = 0.0f;
        }

        ImVec2 screen_pos = ImVec2((current_x * t->zoom_level) + t->camera_offset.x,
                                   (current_y * t->zoom_level) + t->camera_offset.y);
        ImVec2 bg_size = ImVec2(96.0f, 96.0f);

        SDL_Texture *bg_texture;
        if (goal->current_stage >= goal->stage_count - 1) bg_texture = t->adv_bg_done;
        else if (goal->current_stage > 0) bg_texture = t->adv_bg_half_done;
        else bg_texture = t->adv_bg;

        if (bg_texture)
            draw_list->AddImage((void *) bg_texture, screen_pos,
                                ImVec2(screen_pos.x + bg_size.x * t->zoom_level,
                                       screen_pos.y + bg_size.y * t->zoom_level));
        if (goal->texture)
            draw_list->AddImage((void *) goal->texture,
                                ImVec2(screen_pos.x + 16.0f * t->zoom_level,
                                       screen_pos.y + 16.0f * t->zoom_level),
                                ImVec2(screen_pos.x + 80.0f * t->zoom_level,
                                       screen_pos.y + 80.0f * t->zoom_level));

        draw_list->AddText(nullptr, 16.0f * t->zoom_level,
                           ImVec2(screen_pos.x + (bg_size.x * t->zoom_level - text_size.x * t->zoom_level) * 0.5f,
                                  screen_pos.y + bg_size.y * t->zoom_level), text_color, goal->display_name);
        draw_list->AddText(nullptr, 14.0f * t->zoom_level,
                           ImVec2(screen_pos.x + (bg_size.x * t->zoom_level - stage_text_size.x * t->zoom_level) * 0.5f,
                                  screen_pos.y + (bg_size.y + text_size.y + 4.0f) * t->zoom_level), text_color,
                           stage_text);

        current_x += uniform_item_width + horizontal_spacing;
        row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
    }
    current_y += row_max_height;
}


// Animate overlay, display more than just advancements
void tracker_render_gui(Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data) return;

    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize));
    ImGui::Begin("TrackerMap", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Pan and zoom logic
    if (ImGui::IsWindowHovered()) {
        if (io.MouseWheel != 0) {
            ImVec2 mouse_pos_in_window = ImGui::GetMousePos();
            ImVec2 mouse_pos_before_zoom = ImVec2((mouse_pos_in_window.x - t->camera_offset.x) / t->zoom_level,
                                                  (mouse_pos_in_window.y - t->camera_offset.y) / t->zoom_level);
            float old_zoom = t->zoom_level;
            t->zoom_level += io.MouseWheel * 0.1f * t->zoom_level;
            if (t->zoom_level < 0.1f) t->zoom_level = 0.1f;
            if (t->zoom_level > 10.0f) t->zoom_level = 10.0f;
            t->camera_offset.x += (mouse_pos_before_zoom.x * (old_zoom - t->zoom_level));
            t->camera_offset.y += (mouse_pos_before_zoom.y * (old_zoom - t->zoom_level));
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            t->camera_offset.x += io.MouseDelta.x;
            t->camera_offset.y += io.MouseDelta.y;
        }
    }

    // This is the starting Y position for all rendering.
    // Each section will render itself and update this value for the next section.
    float current_y = 50.0f;

    //  Render All Sections in Order
    render_trackable_category_section(t, settings, current_y, t->template_data->advancements,
                                      t->template_data->advancement_count, "Advancements", false);
    render_simple_item_section(t, settings, current_y, t->template_data->unlocks, t->template_data->unlock_count,
                               "Unlocks");
    render_trackable_category_section(t, settings, current_y, t->template_data->stats, t->template_data->stat_count,
                                      "Statistics", true);
    render_custom_goals_section(t, settings, current_y, "Custom Goals");
    render_multistage_goals_section(t, settings, current_y, "Multi-Stage Goals");

    // Info Bar

    // Push thhe user-defined text color before drawing the info bar
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4((float) settings->text_color.r / 255.f,
                                                (float) settings->text_color.g / 255.f,
                                                (float) settings->text_color.b / 255.f,
                                                (float) settings->text_color.a / 255.f));
    // You can remove ImGuiWindowFlags_AlwaysAutoResize if you want to be able to resize this window
    ImGui::Begin("Info | ESC: Settings | Pan: RMB/MMB Drag | Zoom: Wheel | Click: LMB", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::Separator(); // Draw a separator line
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    char info_buffer[512];
    char formatted_category[128];
    char formatted_time[64];
    char formatted_update_time[64];

    format_category_string(settings->category, formatted_category, sizeof(formatted_category));
    // Optional flag DOESN'T get formatted
    format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));
    MC_Version version = settings_get_version_from_string(settings->version_str);

    const char *adv_ach_label = (version >= MC_VERSION_1_12) ? "Adv" : "Ach";

    float last_update_time_5_seconds = floorf(t->time_since_last_update / 5.0f) * 5.0f; // Round to nearest 5 seconds
    format_time_since_update(last_update_time_5_seconds, formatted_update_time, sizeof(formatted_update_time));

    snprintf(info_buffer, sizeof(info_buffer),
             "%s  |  %s - %s%s%s  |  %s: %d/%d  -  Prog: %.2f%%  |  %s IGT  |  Upd: %s",
             t->world_name,
             settings->version_str,
             formatted_category,
             *settings->optional_flag ? " - " : "",
             settings->optional_flag,
             adv_ach_label,
             t->template_data->advancements_completed_count,
             t->template_data->advancement_count,
             t->template_data->overall_progress_percentage,
             formatted_time,
             formatted_update_time);

    ImGui::TextUnformatted(info_buffer);
    ImGui::End();

    // Pop the text color style so it no longer applies to subsequent UI elements
    ImGui::PopStyleColor();

    // Layout Control Buttons
    const float button_padding = 10.0f;
    ImVec2 lock_button_text_size = ImGui::CalcTextSize("Lock Layout");
    ImVec2 reset_button_text_size = ImGui::CalcTextSize("Reset Layout");

    // Add padding and space for the checkbox square
    ImVec2 lock_button_size = ImVec2(lock_button_text_size.x + 25.0f, lock_button_text_size.y + 8.0f);
    ImVec2 reset_button_size = ImVec2(reset_button_text_size.x + 25.0f, reset_button_text_size.y + 8.0f);
    float buttons_total_width = lock_button_size.x + reset_button_size.x + button_padding;
    // TODO: Adjust this for more buttons

    // Position a new transparent window in the bottom right to hold the buttons
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - buttons_total_width - button_padding,
                                   io.DisplaySize.y - lock_button_size.y - button_padding));
    ImGui::SetNextWindowSize(ImVec2(buttons_total_width, lock_button_size.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    // ImGuiWindowFlags_NoMove could be defined here
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

    // Style for transparent buttons and checkbox
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4((float) settings->text_color.r / 255.f,
                                                (float) settings->text_color.g / 255.f,
                                                (float) settings->text_color.b / 255.f,
                                                (float) settings->text_color.a / 255.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4((float) settings->text_color.r / 255.f,
                                                  (float) settings->text_color.g / 255.f,
                                                  (float) settings->text_color.b / 255.f,
                                                  (float) settings->text_color.a / 255.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, (float) ADVANCELY_FADED_ALPHA / 255.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.2f, 0.2f, 0.2f, (float) ADVANCELY_FADED_ALPHA / 255.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4((float) settings->text_color.r / 255.f,
                                                     (float) settings->text_color.g / 255.f,
                                                     (float) settings->text_color.b / 255.f, 1.0f));

    ImGui::SetWindowFontScale(0.9f); // Slightly smaller text for buttons

    // "Lock Layout" checkbox
    if (ImGui::Checkbox("Lock Layout", &t->layout_locked)) {
        if (t->layout_locked) {
            // When locking, store the current scroll position
            t->locked_layout_width = io.DisplaySize.x / t->zoom_level; // Store the current width
        }
    }

    ImGui::SameLine();

    // "Reset Layout" checkbox (acts as a button), it turns off the lock layout
    static bool reset_dummy = false;
    if (ImGui::Checkbox("Reset Layout", &reset_dummy)) {
        t->camera_offset = ImVec2(0.0f, 0.0f);
        t->zoom_level = 1.0f;
        t->layout_locked = false; // Unlocks the layout
        reset_dummy = false; // Ensure it's getting unchecked
    }

    ImGui::PopStyleColor(5); // Pop the style colors, there's 5 of them
    ImGui::End(); // End Layout Controls Window
    ImGui::End(); // End TrackerMap window
}


// -------------------------------------------- TRACKER RENDERING END --------------------------------------------


void tracker_reinit_template(Tracker *t, const AppSettings *settings) {
    if (!t) return;

    printf("[TRACKER] Re-initializing template...\n");

    // Update the paths from settings.json
    tracker_reinit_paths(t, settings);

    // Free all the old advancement, stat, etc. data
    if (t->template_data) {
        tracker_free_template_data(t->template_data);

        // Reset the entire to zero to clear dangling pointers and old counts.
        memset(t->template_data, 0, sizeof(TemplateData));

        // After clearing, ensure the snapshot name is also cleared to force a new snapshot
        t->template_data->snapshot_world_name[0] = '\0';
    }
    // Load and parse data from the new template files
    tracker_load_and_parse_data(t);
}

void tracker_reinit_paths(Tracker *t, const AppSettings *settings) {
    if (!t || !settings) return;

    // Copy the template and lang paths and snapshot path (for legacy snapshots if needed)
    strncpy(t->advancement_template_path, settings->template_path, MAX_PATH_LENGTH - 1);
    t->advancement_template_path[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(t->lang_path, settings->lang_path, MAX_PATH_LENGTH - 1);
    t->lang_path[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(t->snapshot_path, settings->snapshot_path, MAX_PATH_LENGTH - 1);
    t->snapshot_path[MAX_PATH_LENGTH - 1] = '\0';


    MC_Version version = settings_get_version_from_string(settings->version_str);

    if (get_saves_path(t->saves_path, MAX_PATH_LENGTH, settings->path_mode, settings->manual_saves_path)) {
        printf("[TRACKER] Using saves path: %s\n", t->saves_path);

        // Find the specific world files using the correct flag

        // If using StatsPerWorld Mod on a legacy version trick the path finder
        // into looking for the stats file per world, still looking for IDs though in .dat file
        find_player_data_files(
            t->saves_path,
            version,
            settings->using_stats_per_world_legacy,  // This toggles if StatsPerWorld mod is enabled, see above
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

void tracker_load_and_parse_data(Tracker *t) {
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
    tracker_parse_categories(t, advancements_json, t->template_data->lang_json, &t->template_data->advancements,
                             &t->template_data->advancement_count, &t->template_data->total_criteria_count,
                             "advancement.", false);

    // True as it's for stats
    tracker_parse_categories(t, stats_json, t->template_data->lang_json, &t->template_data->stats,
                             &t->template_data->stat_count, &t->template_data->stat_total_criteria_count, "stat.",
                             true);

    // Parse "unlock." prefix for unlocks
    tracker_parse_simple_trackables(t, unlocks_json, t->template_data->lang_json, &t->template_data->unlocks,
                                    &t->template_data->unlock_count, "unlock.");

    // Parse "custom." prefix for custom goals
    tracker_parse_simple_trackables(t, custom_json, t->template_data->lang_json, &t->template_data->custom_goals,
                                    &t->template_data->custom_goal_count, "custom.");

    tracker_parse_multi_stage_goals(t, multi_stage_goals_json, t->template_data->lang_json,
                                    &t->template_data->multi_stage_goals,
                                    &t->template_data->multi_stage_goal_count);

    // Detect and flag criteria that are shared between multiple advancements
    tracker_detect_shared_sub_items(t);

    printf("[TRACKER] Initial template parsing complete.\n");

    // LOADING SNAPSHOT FROM FILE - ONLY FOR VERSION 1.0-1.6.4 WITHOUT StatsPerWorld MOD
    AppSettings settings;
    settings_load(&settings);
    MC_Version version = settings_get_version_from_string(settings.version_str);

    if (version <= MC_VERSION_1_6_4 && !settings.using_stats_per_world_legacy) {
        tracker_load_snapshot_from_file(t);
    }
    cJSON_Delete(template_json);
    if (t->template_data->lang_json) {
        cJSON_Delete(t->template_data->lang_json);
        t->template_data->lang_json = nullptr;
    }
    // No need to delete settings_json, because it's not parsed, handled in tracker_update()
}


void tracker_free(Tracker **tracker) {
    if (tracker && *tracker) {
        Tracker *t = *tracker;

        if (t->minecraft_font) {
            TTF_CloseFont(t->minecraft_font);
        }

        if (t->adv_bg) SDL_DestroyTexture(t->adv_bg);
        if (t->adv_bg_half_done) SDL_DestroyTexture(t->adv_bg_half_done);
        if (t->adv_bg_done) SDL_DestroyTexture(t->adv_bg_done);

        if (t->template_data) {
            tracker_free_template_data(t->template_data); // This ONLY frees the CONTENT of the struct
            free(t->template_data); // This frees the struct
        }

        if (t->renderer) {
            SDL_DestroyRenderer(t->renderer);
            // We still have an address
            t->renderer = nullptr;
        }

        if (t->window) {
            SDL_DestroyWindow(t->window);
            // We still have an address
            t->window = nullptr;
        }

        // tracker is heap allocated so free it
        free(t);
        *tracker = nullptr;
        tracker = nullptr;
        printf("[TRACKER] Tracker freed!\n");
    }
}

void tracker_update_title(Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data || !settings) return;

    char title_buffer[512];
    char formatted_category[128];
    char formatted_time[64];

    // Format the category and optional flag strings
    format_category_string(settings->category, formatted_category, sizeof(formatted_category));
    // Optional flag doesn't get formatted
    format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));

    // Displaying Ach or Adv depending on the version
    // Get version from string
    MC_Version version = settings_get_version_from_string(settings->version_str);
    const char *adv_ach_label = (version >= MC_VERSION_1_12) ? "Adv" : "Ach";

    // Creating the title buffer
    // Displaying last update time doesn't make sense here, so only done in Tracker Info window in tracker_render_gui()
    snprintf(title_buffer, sizeof(title_buffer),
             "  Advancely  %s    |    %s    -    %s    -    %s%s%s    |    %s: %d/%d    -    Progress: %.2f%%    |    %s IGT",
             ADVANCELY_VERSION,
             t->world_name,
             settings->version_str,
             formatted_category,
             *settings->optional_flag ? " - " : "",
             settings->optional_flag,
             adv_ach_label,
             t->template_data->advancements_completed_count,
             t->template_data->advancement_count,
             t->template_data->overall_progress_percentage,
             formatted_time);


    // Putting buffer into Window title
    SDL_SetWindowTitle(t->window, title_buffer);
}

void tracker_print_debug_status(Tracker *t) {
    if (!t || !t->template_data) return;

    struct AppSettings settings;
    settings_load(&settings);
    cJSON *settings_json = cJSON_from_file(SETTINGS_FILE_PATH);
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : nullptr;

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
            cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : nullptr;
            if (stat_cat->done) {
                status_text = (stat_cat->is_manually_completed && cJSON_IsTrue(parent_override))
                                  ? "COMPLETED (MANUAL)"
                                  : "COMPLETED";
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
                    sub_status_text = (sub_stat->is_manually_completed && cJSON_IsTrue(parent_override))
                                          ? "DONE (MANUAL)"
                                          : "DONE";
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
                    snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                             sub_stat->root_name);
                    cJSON *sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : nullptr;
                    if (sub_stat->done) {
                        sub_status_text = (sub_stat->is_manually_completed && cJSON_IsTrue(sub_override))
                                              ? "DONE (MANUAL)"
                                              : "DONE";
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
                printf("[Custom Counter] %s: %d / %d - %s\n",
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
