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

// HELPER FUNCTION FOR PARSING

/**
 * @brief Parses a cJSON object containing advancement categories into an array of TrackableCategory structs.
 *
 * This function iterates through a JSON object where each key is an advancement's root name.
 * It allocates memory for each category, populates it with data (root name, display name, icon),
 * and prepares it for having its criteria parsed.
 *
 * @param category_json The cJSON object for the "advancements" key from the template file.
 * @param lang_json The cJSON object from the language file to look up display names.
 * @param categories_array A pointer to the array of TrackableCategory pointers to be populated.
 * @param count A pointer to an integer that will store the number of categories parsed.
 */
static void parse_trackable_categories(cJSON *category_json, cJSON *lang_json, TrackableCategory ***categories_array,
                                       int *count) {
    if (!category_json) {
        printf("[TRACKER] parse_trackable_categories: category_json is NULL\n");
        return;
    }

    *count = 0;
    cJSON *temp = NULL;
    cJSON_ArrayForEach(temp, category_json) { (*count)++; } // count the number of categories
    if (*count == 0) {
        printf("[TRACKER] parse_trackable_categories: No categories found\n");
        return;
    }

    *categories_array = calloc(*count, sizeof(TrackableCategory *));
    if (!categories_array) return;

    cJSON *cat_json = category_json->child;
    int i = 0;
    while (cat_json) {
        TrackableCategory *new_cat = calloc(1, sizeof(TrackableCategory));
        if (new_cat) {
            strncpy(new_cat->root_name, cat_json->string, sizeof(new_cat->root_name) - 1);

            // Build the language key from the root_name, e.g., "minecraft:story/smelt_iron" -> "advancements.story.smelt_iron"
            char lang_key[256] = {0};
            const char *prefix = "minecraft:";
            if (strncmp(new_cat->root_name, prefix, strlen(prefix)) == 0) {
                snprintf(lang_key, sizeof(lang_key), "advancements.%s", new_cat->root_name + strlen(prefix));
                for (char *p = lang_key; *p; ++p) {
                    if (*p == '/') {
                        *p = '.';
                    }
                }
            } else {
                strncpy(lang_key, new_cat->root_name, sizeof(lang_key) - 1);
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
                // TODO: Parse nested criteria (Similar parsing logic for criteria would go here)
            }
            (*categories_array)[i++] = new_cat;
        }
        cat_json = cat_json->next;
    }
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

        // Parse parent goal properties
        cJSON *display_name = cJSON_GetObjectItem(goal_item_json, "display_name");
        cJSON *icon = cJSON_GetObjectItem(goal_item_json, "icon");

        if (cJSON_IsString(display_name))
            strncpy(new_goal->display_name, display_name->valuestring,
                    sizeof(new_goal->display_name) - 1);
        if (cJSON_IsString(icon)) strncpy(new_goal->icon_path, icon->valuestring, sizeof(new_goal->icon_path) - 1);

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

                cJSON *text = cJSON_GetObjectItem(stage_item_json, "display_text");
                cJSON *type = cJSON_GetObjectItem(stage_item_json, "type");
                cJSON *root = cJSON_GetObjectItem(stage_item_json, "root_name");
                cJSON *goal_val = cJSON_GetObjectItem(stage_item_json, "goal");

                if (cJSON_IsString(text))
                    strncpy(new_stage->display_text, text->valuestring,
                            sizeof(new_stage->display_text) - 1);
                if (cJSON_IsString(root))
                    strncpy(new_stage->root_name, root->valuestring,
                            sizeof(new_stage->root_name) - 1);
                if (cJSON_IsString(goal_val)) new_stage->required_progress = goal_val->valueint;

                // Parse type
                if (cJSON_IsString(type)) {
                    if (strcmp(type->valuestring, "stat") == 0) new_stage->type = SUBGOAL_STAT;
                    else if (strcmp(type->valuestring, "advancement") == 0) new_stage->type = SUBGOAL_ADVANCEMENT;
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
 * @brief Updates advancement progress from a pre-loaded cJSON object.
 * @param t A pointer to the Tracker struct.
 * @param player_adv_json The parsed player advancements JSON file.
 */
static void tracker_update_advancement_progress(struct Tracker *t, const cJSON *player_adv_json) {
    if (!player_adv_json) return;

    for (int i = 0; i < t->template_data->advancement_count; i++) {
    }

    printf("[TRACKER] Reading player advancements from: %s\n", t->advancements_path);

    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *adv = t->template_data->advancements[i]; // Get the current advancement
        cJSON *player_entry = cJSON_GetObjectItem(player_adv_json, adv->root_name);
        // Get the entry for this advancement

        if (player_entry) {
            cJSON *done_flag = cJSON_GetObjectItem(player_entry, "done");
            adv->done = cJSON_IsTrue(done_flag);
        } else {
            adv->done = false;
        }
    }
}

/**
 * @brief Updates unlock progress from a pre-loaded cJSON object and counts completed unlocks.
 * @param t A pointer to the Tracker struct.
 * @param player_unlocks_json The parsed player unlocks JSON file.
 */
static void tracker_update_unlock_progress(struct Tracker *t, const cJSON *player_unlocks_json) {
    if (!player_unlocks_json) return;

    printf("[TRACKER] Reading player unlocks from: %s\n", t->unlocks_path);

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
 * @brief Updates stat progress from a pre-loaded cJSON object.
 * @param t A pointer to the Tracker struct.
 * @param player_stats_json The parsed player stats JSON file.
 */
static void tracker_update_stat_progress(struct Tracker *t, const cJSON *player_stats_json) {
    if (!player_stats_json) return;

    printf("[TRACKER] Reading player stats from: %s\n", t->stats_path);


    cJSON *stats_obj = cJSON_GetObjectItem(player_stats_json, "stats");
    if (!stats_obj) {
        fprintf(stderr, "[TRACKER] Failed to find 'stats' object in player stats file.\n");
        return;
    }

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableItem *stat = t->template_data->stats[i];
        stat->progress = 0; // Default to 0

        // The root_name is in the format "category:key", e.g., "custom:jump"
        // The player stats file uses "minecraft:category" and "minecraft:key"
        // Prepending "minecraft:" to both parts
        char root_name_copy[192];
        strncpy(root_name_copy, stat->root_name, sizeof(root_name_copy) - 1);
        root_name_copy[sizeof(root_name_copy) - 1] = '\0'; // Ensure null termination

        char *item_str = strchr(root_name_copy, ':');
        if (item_str) {
            *item_str = '\0'; // Split the string by placing a null terminator
            item_str++; // Move pointer to the start of the key

            char *category_str = root_name_copy;

            char full_category_key[256];
            snprintf(full_category_key, sizeof(full_category_key), "minecraft:%s", category_str);

            cJSON *category_obj = cJSON_GetObjectItem(stats_obj, full_category_key);
            if (category_obj) {
                char full_stat_key[256];
                snprintf(full_stat_key, sizeof(full_stat_key), "minecraft:%s", item_str);

                // Check if the stat exists in the category
                cJSON *stat_value = cJSON_GetObjectItem(category_obj, full_stat_key);
                if (cJSON_IsNumber(stat_value)) {
                    stat->progress = stat_value->valueint;
                }
            }
        }

        // Check if the goal has been met and update the 'done' flag
        stat->done = (stat->goal > 0 && stat->progress >= stat->goal);
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
        cJSON *item_status = cJSON_GetObjectItem(progress_obj, item->root_name);
        item->done = cJSON_IsTrue(item_status);
    }
}

/**
 * @brief Updates multi-stage goal progress from preloaded cJSON objects.
 * @param t A pointer to the Tracker struct.
 * @param player_adv_json The parsed player advancements JSON file.
 * @param player_stats_json The parsed player stats JSON file.
 */
static void tracker_update_multi_stage_progress(struct Tracker *t, const cJSON *player_adv_json,
                                                const cJSON *player_stats_json) {
    if (t->template_data->multi_stage_goal_count == 0) return;

    if (!player_adv_json && !player_stats_json) {
        printf(
            "[TRACKER] Failed to load or parse player advancements or player stats file to update multi-stage goal progress.\n");
        return;
    }

    cJSON *stats_obj = player_stats_json ? cJSON_GetObjectItem(player_stats_json, "stats") : NULL;

    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        // Iterate through the multi-stage goals
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        if (goal->current_stage >= goal->stage_count - 1) continue; // Goal is already on its final stage

        SubGoal *active_stage = goal->stages[goal->current_stage];
        bool stage_completed = false;

        switch (active_stage->type) {
            case SUBGOAL_ADVANCEMENT:
                // We take the item with the root name then check if it is done then call this stage completed
                if (player_adv_json) {
                    cJSON *adv_entry = cJSON_GetObjectItem(player_adv_json, active_stage->root_name);
                    // Get the entry for this advancement
                    if (adv_entry && cJSON_IsTrue(cJSON_GetObjectItem(adv_entry, "done"))) {
                        stage_completed = true;
                    }
                }
                break;

            case SUBGOAL_STAT:
                if (stats_obj) {
                    // Handle shortend stat format e.g., "custom:jump"
                    char root_name_copy[192];
                    strncpy(root_name_copy, active_stage->root_name, sizeof(root_name_copy) - 1);
                    root_name_copy[sizeof(root_name_copy) - 1] = '\0'; // Ensure null termination

                    char *item_str = strchr(root_name_copy, ':'); // Find the first colon
                    if (item_str) {
                        *item_str = '\0';
                        item_str++;
                        char *category_str = root_name_copy;

                        // Construct the full JSON keys used in the stats file
                        char full_category_key[256];
                        snprintf(full_category_key, sizeof(full_category_key), "minecraft:%s", category_str);

                        printf("[MULTISTAGE DEBUG] Checking stat: category_key=[%s]\n", full_category_key);
                        cJSON *category_obj = cJSON_GetObjectItem(stats_obj, full_category_key);

                        if (category_obj) {
                            printf("[MULTISTAGE DEBUG] Found category object: %s\n", full_category_key);
                            char full_item_key[256];
                            snprintf(full_item_key, sizeof(full_item_key), "minecraft:%s", item_str);

                            printf("[MULTISTAGE DEBUG] Checking item_key=[%s]\n", full_item_key);
                            cJSON *stat_value = cJSON_GetObjectItem(category_obj, full_item_key);

                            // Check if the stat value is greater than or equal to the required progress
                            if (cJSON_IsNumber(stat_value)) {
                                printf("[MULTISTAGE DEBUG] Found stat value: %d\n", stat_value->valueint);
                                if (stat_value->valueint >= active_stage->required_progress) {
                                    stage_completed = true;
                                    printf("[MULTISTAGE DEBUG] Stage completed!\n");
                                }
                            } else { printf("[MULTISTAGE DEBUG] Stat value not found or not a number for key: %s\n", full_item_key); }
                        } else { printf("[MULTISTAGE DEBUG] Category object not found for key: %s\n", full_category_key); }
                    }
                }
                break;

            case SUBGOAL_MANUAL: // Not used
            default:
                break; // Manual stages are not updated here
        }

        if (stage_completed) {
            goal->current_stage++; // Advance to the next stage
        }
    }
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

    AppSettings settings; // struct used below, temporary
    settings_load(&settings); // load settings from file in settings_utils.c

    // Copy the template path into our tracker struct
    strncpy(t->advancement_template_path, settings.template_path, MAX_PATH_LENGTH - 1);
    t->advancement_template_path[MAX_PATH_LENGTH - 1] = '\0';

    strncpy(t->lang_path, settings.lang_path, MAX_PATH_LENGTH - 1);
    t->lang_path[MAX_PATH_LENGTH - 1] = '\0';


    MC_Version version = settings_get_version_from_string(settings.version_str);
    bool use_advancements = (version >= MC_VERSION_1_12);
    bool use_unlocks = (version == MC_VERSION_25W14CRAFTMINE);

    // Get the final, normalized saves path using the loaded settings
    if (get_saves_path(t->saves_path, MAX_PATH_LENGTH, settings.path_mode, settings.manual_saves_path)) {
        printf("[TRACKER] Using Minecraft saves folder: %s\n", t->saves_path);

        // Find the specific world files using the correct flags.
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
        fprintf(stderr, "[TRACKER] CRITICAL: Could not determine Minecraft saves folder.\n");

        // Ensure paths are empty, so no attempts are made to access them.
        t->saves_path[0] = '\0';
        t->advancements_path[0] = '\0';
        t->stats_path[0] = '\0';
        t->unlocks_path[0] = '\0';
    }
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
                        printf("[TRACKER] Escape key pressed in tracker: Opening settings window now.\n");
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

    static bool initial_load_done = false;
    if (!initial_load_done) {
        if (strlen(t->advancement_template_path) > 0) {
            tracker_load_and_parse_data(t);
            initial_load_done = true; // To only do this once
        }

        // Force one update immediately after loading
        if (initial_load_done) {
            // Fall through to the update logic on the same frame as the initial load
        } else {
            return; // Return if still not loaded
        }
    }

    // TODO: Call this block whenever custom goal is checked off or save files update (dmon.h) maybe

    // Load all necessary player files ONCE
    cJSON *player_adv_json = (strlen(t->advancements_path) > 0) ? cJSON_from_file(t->advancements_path) : NULL;
    cJSON *player_stats_json = (strlen(t->stats_path) > 0) ? cJSON_from_file(t->stats_path) : NULL;
    cJSON *player_unlocks_json = (strlen(t->unlocks_path) > 0) ? cJSON_from_file(t->unlocks_path) : NULL;
    cJSON *settings_json = cJSON_from_file(SETTINGS_FILE_PATH);

    // Pass the parsed data to the update functions
    tracker_update_advancement_progress(t, player_adv_json);
    tracker_update_unlock_progress(t, player_unlocks_json);
    tracker_update_stat_progress(t, player_stats_json);
    tracker_update_custom_progress(t, settings_json);
    tracker_update_multi_stage_progress(t, player_adv_json, player_stats_json);

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

    // --- NEW: Placeholder for rendering advancements ---
    // In the next step, we will iterate through t->advancements here and draw them.
    // For example:
    // for (int i = 0; i < t->advancement_count; ++i) {
    //     Advancement* adv = t->advancements[i];
    //     // ... draw icon and text ...
    //     if (adv->done) {
    //         // ... draw a checkmark or change color ...
    //     }
    // }

    // Drawing happens here

    // present backbuffer
    SDL_RenderPresent(t->renderer);
}

void tracker_load_and_parse_data(struct Tracker *t) {
    printf("[TRACKER] Loading advancement template from: %s\n", t->advancement_template_path);
    cJSON *template_json = cJSON_from_file(t->advancement_template_path);
    if (!template_json) {
        fprintf(stderr, "[TRACKER] Failed to load or parse advancement template file.\n");
        fprintf(stderr, "[TRACKER] Please check the 'version', 'category', and 'optional_flag' in settings.json.\n");
        return;
    }

    t->template_data->lang_json = cJSON_from_file(t->lang_path);
    if (!t->template_data->lang_json) {
        fprintf(stderr, "[TRACKER] Failed to load or parse language file.\n");
        return;
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
    parse_trackable_categories(advancements_json, t->template_data->lang_json, &t->template_data->advancements,
                               &t->template_data->advancement_count);
    parse_simple_trackables(stats_json, t->template_data->lang_json, &t->template_data->stats,
                            &t->template_data->stat_count);
    parse_simple_trackables(unlocks_json, t->template_data->lang_json, &t->template_data->unlocks,
                            &t->template_data->unlock_count);
    parse_simple_trackables(custom_json, t->template_data->lang_json, &t->template_data->custom_goals,
                            &t->template_data->custom_goal_count); // Update progress from player files
    parse_multi_stage_goals(multi_stage_goals_json, t->template_data->lang_json, &t->template_data->multi_stage_goals,
                            &t->template_data->multi_stage_goal_count);


    // ---------------- Printing for debugging ----------------
    printf("[TRACKER] Parsed %d advancements.\n", t->template_data->advancement_count);

    // print found advancements
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *adv = t->template_data->advancements[i];
        printf("[TRACKER] Advancement %d: %s\n", i, adv->root_name);
    }

    printf("[TRACKER] Parsed %d stats.\n", t->template_data->stat_count);

    // print found stats
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableItem *stat = t->template_data->stats[i];
        printf("[TRACKER] Stat %d: %s\n", i, stat->root_name);
    }


    printf("[TRACKER] Parsed %d unlocks.\n", t->template_data->unlock_count);

    // print found unlocks
    for (int i = 0; i < t->template_data->unlock_count; i++) {
        TrackableItem *unlock = t->template_data->unlocks[i];
        printf("[TRACKER] Unlock %d: %s\n", i, unlock->root_name);
    }


    printf("[TRACKER] Parsed %d custom goals.\n", t->template_data->custom_goal_count);

    // print found custom goals
    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        TrackableItem *custom = t->template_data->custom_goals[i];
        printf("[TRACKER] Custom Goal %d: %s\n", i, custom->root_name);
    }

    // Print for multi-stage goals

    printf("[MULTISTAGE] Parsed %d multi-stage goals.\n", t->template_data->multi_stage_goal_count);

    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        printf("[MULTISTAGE] Multi-Stage Goal %d: %s\n", i, goal->display_name);
    }

    // Print the final status of all tracked items
    printf("\n--- Player Progress Status ---\n");
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *adv = t->template_data->advancements[i];
        printf("[Advancement] %s: %s\n", adv->display_name, adv->done ? "COMPLETED" : "INCOMPLETE");
    }

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableItem *stat = t->template_data->stats[i];
        printf("[Stat] %s: %d / %d - %s\n", stat->display_name, stat->progress, stat->goal,
               stat->done ? "COMPLETE" : "INCOMPLETE");
    }

    for (int i = 0; i < t->template_data->unlock_count; i++) {
        TrackableItem *unlock = t->template_data->unlocks[i];
        printf("[Unlock] %s: %s\n", unlock->display_name, unlock->done ? "UNLOCKED" : "LOCKED");
    }

    printf("[Unlocks] %d / %d completed\n", t->template_data->unlocks_completed_count, t->template_data->unlock_count);

    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        TrackableItem *custom_goal = t->template_data->custom_goals[i];
        printf("[Custom Goal] %s: %s\n", custom_goal->display_name, custom_goal->done ? "COMPLETED" : "INCOMPLETE");
    }

    // Print for multi-stage goals
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) { // For each multi-stage goal
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        // Check if the goal and its stages exist and the index is valid
        if (goal && goal->stages && goal->current_stage < goal->stage_count) {
            SubGoal *active_stage = goal->stages[goal->current_stage];
            printf("[Multi-Stage Goal] %s: %s\n", goal->display_name, active_stage->display_text);
        }
    }

    printf("----------------------------\n\n");

    printf("[TRACKER] Initial template parsing complete.\n");

    // ---------------- End of Printing for debugging ----------------

    cJSON_Delete(template_json);
    // No need to delete settings_json, because it's not parsed, handled in tracker_update()
}


void tracker_free(struct Tracker **tracker) {
    if (tracker && *tracker) {
        struct Tracker *t = *tracker;

        if (t->template_data) {
            // Free advancements data
            for (int i = 0; i < t->template_data->advancement_count; i++) {
                // For each advancement
                if (t->template_data->advancements[i]) {
                    for (int j = 0; j < t->template_data->advancements[i]->criteria_count; j++) {
                        // For each criterion
                        free(t->template_data->advancements[i]->criteria[j]);
                    }
                    free(t->template_data->advancements[i]->criteria);
                    free(t->template_data->advancements[i]);
                }
            }
            free(t->template_data->advancements);

            // Free stats data
            for (int i = 0; i < t->template_data->stat_count; i++) {
                free(t->template_data->stats[i]);
            }
            free(t->template_data->stats);

            // Free unlocks data
            for (int i = 0; i < t->template_data->unlock_count; i++) {
                free(t->template_data->unlocks[i]);
            }
            free(t->template_data->unlocks);

            // Free the language file
            if (t->template_data->lang_json) {
                cJSON_Delete(t->template_data->lang_json);
            }

            // Free custom goals data
            if (t->template_data->custom_goals) {
                for (int i = 0; i < t->template_data->custom_goal_count; i++) {
                    free(t->template_data->custom_goals[i]);
                }
                free(t->template_data->custom_goals);
            }

            // Free multi stage goals data
            if (t->template_data->multi_stage_goals) {
                for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
                    MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
                    if (goal) {
                        // Free all sub-stages within the goal
                        for (int j = 0; j < goal->stage_count; j++) {
                            free(goal->stages[j]);
                        }
                        free(goal->stages);
                        free(goal);
                    }
                }
                free(t->template_data->multi_stage_goals);
            }

            // Free the array of advancement pointers
            free(t->template_data);
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
