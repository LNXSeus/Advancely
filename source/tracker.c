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
 * @brief (Era 1: 1.0-1.6.4) Takes a snapshot of the current global stats.
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

    // Reset all initial progress fields first
    for (int i = 0; i < t->template_data->stat_count; i++) {
        // TODO: Stat + advancements right??
        t->template_data->stats[i]->initial_progress = 0;
    }
    t->template_data->playtime_snapshot = 0;

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
                // TODO: Stat + advancements right??
                TrackableItem *stat = t->template_data->stats[i];
                if (strcmp(stat->root_name, key) == 0) {
                    stat->initial_progress = value;
                    break;
                }
            }
        }
    }
    cJSON_Delete(player_stats_json);

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

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableItem *stat = t->template_data->stats[i];
        printf("  - Stat '%s' (ID: %s): Snapshot Value = %d\n", stat->display_name, stat->root_name,
               stat->initial_progress);
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

    // Reset progress to 0 before recalculating
    for (int i = 0; i < t->template_data->stat_count; i++) {
        t->template_data->stats[i]->progress = 0;
    }
    t->template_data->play_time_ticks = 0;

    cJSON *stat_entry;
    // stat_change is an array
    cJSON_ArrayForEach(stat_entry, stats_change) {
        cJSON *item = stat_entry->child; // Then we go into the curly brackets

        if (item) {
            const char *key = item->string;
            int current_value = item->valueint;

            // Update playtime relative to snapshot
            if (strcmp(key, "1100") == 0) {
                long long diff = current_value - t->template_data->playtime_snapshot;
                // Subtract the snapshot to be left with the actual playtime
                t->template_data->play_time_ticks = (diff > 0) ? diff : 0;

                // PRINT DIFFERENCE TO SNAPSHOT
                printf("[TRACKER] PLAYTIME | Snapshot: %-6d | Current: %-6lld | Tracker Diff: %lld\n", current_value,
                       t->template_data->playtime_snapshot, t->template_data->play_time_ticks);
            }

            // Update other stats relative to snapshot
            for (int i = 0; i < t->template_data->stat_count; i++) {
                TrackableItem *stat = t->template_data->stats[i];
                if (strcmp(stat->root_name, key) == 0) {
                    int diff = current_value - stat->initial_progress;
                    stat->progress = diff > 0 ? diff : 0;
                    stat->done = (stat->goal > 0 && stat->progress >= stat->goal);
                    printf("STAT: %-28s | Current: %-6d | Snapshot: %-6d | Tracked Diff: %d\n",
                           stat->display_name, current_value, stat->initial_progress, stat->progress);
                    break;
                }
            }
        }
    }
}

// TODO: Delete once possible
// /**
//  * @brief (Era 2: 1.7.2-1.11.2) Parses unified JSON with achievements and stats.
//  */
// static void tracker_update_achievements_and_stats_mid(struct Tracker *t, const cJSON *player_stats_json) {
//     if (!player_stats_json) return;
//
//     t->template_data->advancement_count = 0;
//     t->template_data->completed_criteria_count = 0;
//     t->template_data->play_time_ticks = 0;
//
//     // Update Achievements
//     for (int i = 0; i < t->template_data->advancement_count; i++) {
//         TrackableCategory *ach = t->template_data->advancements[i];
//         ach->completed_criteria_count = 0;
//         ach->done = false;
//
//         cJSON *ach_entry = cJSON_GetObjectItem(player_stats_json, ach->root_name);
//         if (!ach_entry) continue;
//
//         if (cJSON_IsNumber(ach_entry)) {
//             // Simple achievement
//             ach->done = (ach_entry->valueint > 0);
//         } else if (cJSON_IsObject(ach_entry)) {
//             // Achievement with criteria
//             cJSON *progress_array = cJSON_GetObjectItem(ach_entry, "progress");
//             if (cJSON_IsArray(progress_array)) {
//                 for (int j = 0; j < ach->criteria_count; j++) {
//                     TrackableItem *crit = ach->criteria[j];
//                     crit->done = false;
//                     cJSON *progress_item;
//                     cJSON_ArrayForEach(progress_item, progress_array) {
//                         if (cJSON_IsString(progress_item) && strcmp(progress_item->valuestring, crit->root_name) == 0) {
//                             crit->done = true;
//                             ach->completed_criteria_count++;
//                             break;
//                         }
//                     }
//                 }
//             }
//             // Achievement is done if all criteria are done
//             if (ach->criteria_count > 0 && ach->completed_criteria_count >= ach->criteria_count) {
//                 ach->done = true;
//             }
//         }
//         if (ach->done) {
//             t->template_data->advancements_completed_count++;
//         }
//         t->template_data->completed_criteria_count += ach->completed_criteria_count;
//     }
//
//     // Update Stats
//     for (int i = 0; i < t->template_data->stat_count; i++) {
//         TrackableItem *stat = t->template_data->stats[i];
//         cJSON *stat_entry = cJSON_GetObjectItem(player_stats_json, stat->root_name);
//         // take root name (from template) from player advancements
//         stat->progress = cJSON_IsNumber(stat_entry) ? stat_entry->valueint : 0;
//         stat->done = (stat->goal > 0 && stat->progress >= stat->goal);
//     }
//
//     // Update Playtime
//     cJSON *play_time_entry = cJSON_GetObjectItem(player_stats_json, "stat.playOneMinute");
//     if (cJSON_IsNumber(play_time_entry)) {
//         t->template_data->play_time_ticks = (long long) play_time_entry->valuedouble;
//     }
// }

/**
 * @brief (Era 2: 1.7.2-1.11.2) Parses unified JSON with achievements and stats.
 */
static void tracker_update_achievements_and_stats_mid(struct Tracker *t, const cJSON *player_stats_json) {
    if (!player_stats_json) return;

    t->template_data->advancements_completed_count = 0; // Corrected from advancement_count
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

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableItem *stat = t->template_data->stats[i];
        cJSON *stat_entry = cJSON_GetObjectItem(player_stats_json, stat->root_name);
        stat->progress = cJSON_IsNumber(stat_entry) ? stat_entry->valueint : 0;
        stat->done = (stat->goal > 0 && stat->progress >= stat->goal);
    }

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

    // Get the playtime for display, when 1.17+ its minecraft:play_time below, below it's minecraft:play_one_minute
    cJSON *custom_stats = cJSON_GetObjectItem(stats_obj, "minecraft:custom");
    if (custom_stats) {
        // 1.17+
        cJSON *play_time = NULL;
        if (version >= MC_VERSION_1_17) {
            play_time = cJSON_GetObjectItem(custom_stats, "minecraft:play_time");
        } else {
            // 1.12 -> 1.16.5
            play_time = cJSON_GetObjectItem(custom_stats, "minecraft:play_one_minute");
        }

        if (cJSON_IsNumber(play_time)) {
            t->template_data->play_time_ticks = (long long) play_time->valuedouble;
        }
    }

    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : NULL;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableItem *stat = t->template_data->stats[i];
        stat->progress = 0;
        stat->is_manually_completed = false;

        char root_name_copy[192];
        strncpy(root_name_copy, stat->root_name, sizeof(root_name_copy) - 1);
        root_name_copy[sizeof(root_name_copy) - 1] = '\0';

        char *item_key = strchr(root_name_copy, '/');
        if (item_key) {
            *item_key = '\0'; // Null-terminate any existing '/' in the root_name
            item_key++;
            char *category_key = root_name_copy;
            cJSON *category_obj = cJSON_GetObjectItem(stats_obj, category_key);
            if (category_obj) {
                cJSON *stat_value = cJSON_GetObjectItem(category_obj, item_key);
                if (cJSON_IsNumber(stat_value)) {
                    stat->progress = stat_value->valueint;
                }
            }
        }

        if (override_obj) {
            cJSON *override_status = cJSON_GetObjectItem(override_obj, stat->root_name);
            if (cJSON_IsTrue(override_status)) {
                stat->done = true;
                stat->is_manually_completed = true;
            }
        }

        if (!stat->is_manually_completed) {
            stat->done = (stat->goal > 0 && stat->progress >= stat->goal);
        }
    }
}

// HELPER FUNCTION FOR PARSING
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
                if (cJSON_IsString(stage_id))
                    strncpy(new_stage->stage_id, stage_id->valuestring,
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


// /**
//  * @brief Updates stat progress from a pre-loaded cJSON object. Supports modded stats. No hardcoded "minecraft:".
//  * It also looks specifically for and stores the minecraft:play_time statistic for finished runs.
//  * @param t A pointer to the Tracker struct.
//  * @param player_stats_json The parsed player stats JSON file.
//  */
// static void tracker_update_stat_progress(struct Tracker *t, const cJSON *player_stats_json,
//                                          const cJSON *settings_json) {
//     if (!player_stats_json) return;
//
//     // printf("[TRACKER] Reading player stats from: %s\n", t->stats_path);
//
//
//     cJSON *stats_obj = cJSON_GetObjectItem(player_stats_json, "stats");
//     if (!stats_obj) {
//         fprintf(stderr, "[TRACKER] Failed to find 'stats' object in player stats file.\n");
//         return;
//     }
//
//     cJSON *custom_stats = cJSON_GetObjectItem(stats_obj, "minecraft:custom");
//     if (custom_stats) {
//         cJSON *play_time = cJSON_GetObjectItem(custom_stats, "minecraft:play_time");
//         if (cJSON_IsNumber(play_time)) {
//             t->template_data->play_time_ticks = (long long) play_time->valuedouble;
//         }
//     }
//
//     // MANUAL OVERRIDE FOR STATS GOAL
//     cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : NULL;
//
//     for (int i = 0; i < t->template_data->stat_count; i++) {
//         TrackableItem *stat = t->template_data->stats[i];
//
//         stat->progress = 0; // Default to 0
//         stat->is_manually_completed = false;
//
//         // The root_name is now in the format "full:category/full:item", e.g., "minecraft:mined/minecraft:dirt"
//         char root_name_copy[192];
//         strncpy(root_name_copy, stat->root_name, sizeof(root_name_copy) - 1);
//         root_name_copy[sizeof(root_name_copy) - 1] = '\0'; // Ensure null termination
//
//         // Find the separator '/' to distinguish category and item
//
//         char *item_key = strchr(root_name_copy, '/');
//         if (item_key) {
//             *item_key = '\0'; // Split the string with null terminator
//             item_key++; // Move pointer to the start of the item key
//
//             char *category_key = root_name_copy;
//
//             // Look up the category (e.g., "minecraft:mined")
//             cJSON *category_obj = cJSON_GetObjectItem(stats_obj, category_key);
//             if (category_obj) {
//                 // Look up the item within that category (e.g., "minecraft:dirt")
//
//                 // Check if the stat exists in the category
//                 cJSON *stat_value = cJSON_GetObjectItem(category_obj, item_key);
//                 if (cJSON_IsNumber(stat_value)) {
//                     stat->progress = stat_value->valueint;
//                 }
//             }
//         }
//
//         if (override_obj) {
//             cJSON *override_status = cJSON_GetObjectItem(override_obj, stat->root_name);
//             if (cJSON_IsBool(override_status)) {
//                 if (cJSON_IsTrue(override_status)) {
//                     stat->done = true;
//                     stat->is_manually_completed = true;
//                 }
//             }
//         }
//
//         // If not manually completed, determine 'done' status by progress
//         if (!stat->is_manually_completed) {
//             stat->done = (stat->goal > 0 && stat->progress >= stat->goal);
//         }
//     }
// }

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
        t->template_data->overall_progress_percentage = 100.0f; // Display 100% if there are no steps
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
    free_trackable_items(td->stats, td->stat_count);
    free_trackable_items(td->unlocks, td->unlock_count);

    // Free custom goal data
    if (td->custom_goals) {
        free_trackable_items(td->custom_goals, td->custom_goal_count);
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
    tracker_update_multi_stage_progress(t, player_adv_json, player_stats_json, player_unlocks_json);
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

    // Copy the template and lang paths
    strncpy(t->advancement_template_path, settings->template_path, MAX_PATH_LENGTH - 1);
    t->advancement_template_path[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(t->lang_path, settings->lang_path, MAX_PATH_LENGTH - 1);
    t->lang_path[MAX_PATH_LENGTH - 1] = '\0';

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

void tracker_update_title(struct Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data || !settings) return;

    char title_buffer[512];
    char formatted_category[128];
    char formatted_time[64];

    format_category_string(settings->category, formatted_category, sizeof(formatted_category));
    format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));

    // Creating the title buffer
    snprintf(title_buffer, sizeof(title_buffer),
             "Advancely    -    %s    -    %s    -    %s    |    Adv: %d/%d    |    Progress: %.2f%%    |    %s",
             t->world_name,
             settings->version_str,
             formatted_category,
             t->template_data->advancements_completed_count,
             t->template_data->advancement_count,
             t->template_data->overall_progress_percentage,
             formatted_time);

    // Putting buffer into Window title
    SDL_SetWindowTitle(t->window, title_buffer);
}

void tracker_print_debug_status(struct Tracker *t) {
    if (!t || !t->template_data) return;

    AppSettings settings;
    settings_load(&settings);

    char formatted_category[128];
    format_category_string(settings.category, formatted_category, sizeof(formatted_category));

    // Format the time to DD:HH:MM:SS.MS
    char formatted_time[128];
    format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));

    printf("\n============================================================\n");
    printf(" World:      %s\n", t->world_name);
    printf(" Version:    %s\n", settings.version_str);
    printf(" Category:   %s\n", formatted_category);
    printf(" Flag:       %s\n", settings.optional_flag);
    printf(" Play Time:  %s\n", formatted_time);
    printf("============================================================\n");


    // Check if the run is completed, check both advancement and overall progress
    if (t->template_data->advancements_completed_count >= t->template_data->advancement_count && t->template_data->
        overall_progress_percentage >= 100.0f) {
        printf("\n                  *** RUN COMPLETE! ***\n\n");
        printf("                  Final Time: %s\n\n", formatted_time);
        printf("============================================================\n\n");
    } else {
        printf("\n--- Player Progress Status ---\n\n");

        // Advancements and Criteria
        printf("[Advancements] %d / %d completed\n", t->template_data->advancements_completed_count,
               t->template_data->advancement_count);
        for (int i = 0; i < t->template_data->advancement_count; i++) {
            TrackableCategory *adv = t->template_data->advancements[i];
            printf("  - %s (%d/%d criteria): %s\n", adv->display_name, adv->completed_criteria_count,
                   adv->criteria_count,
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
            if (custom_goal->goal > 0) {
                printf("[Custom Goal] %s: %d / %d - %s\n",
                       custom_goal->display_name,
                       custom_goal->progress,
                       custom_goal->goal,
                       custom_goal->done ? "COMPLETED" : "INCOMPLETE");
            } else {
                // Otherwise it's a simple toggle (no target entry needs to be specified)
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
        printf("============================================================\n\n");
    }
    // Force the output buffer to write to the console immediately
    fflush(stdout);
}
