//
// Created by Linus on 24.06.2025.
//

#include <stdio.h>
#include <cJSON.h>

#include "tracker.h"
#include "init_sdl.h"
#include "file_utils.h" // has the cJSON_from_file function
#include "path_utils.h"
#include "settings_utils.h"

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

    // Load all settings from the JSON file into a temporary settings struct
    // CENTRAL POINT FOR ALL CONFIGURATION

    AppSettings settings; // struct used below, temporary
    settings_load(&settings); // load settings from file in settings_utils.c

    // Copy the template path into our tracker struct
    strncpy(t->advancement_template_path, settings.advancement_template_path, MAX_PATH_LENGTH - 1); // copy from settings to trakcer
    // Assure null termination
    t->advancement_template_path[MAX_PATH_LENGTH - 1] = '\0';

    // Determine path-finding flags based on loaded version setting
    bool use_advancements = (settings.version >= MC_VERSION_1_12);
    bool use_unlocks = (settings.version == MC_VERSION_25W14CRAFTMINE);

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
    // --- TODO: Update parsing logic based on flags ---
    // This logic should probably be moved into tracker_load_and_parse_advancements
    // if (use_unlocks) { ... }

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
            printf("[TRACKER] Mouse button pressed in tracker.\n");
            break;
        case SDL_EVENT_MOUSE_MOTION:
            printf("[TRACKER] Mouse moved in tracker.\n");
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            printf("[TRACKER] Mouse button released in tracker.\n");
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
    if (!initial_load_done && strlen(t->advancements_path) > 0) {
        tracker_load_and_parse_advancements(t);
        initial_load_done = true;
    }
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

void tracker_free(struct Tracker **tracker) {
    if (tracker && *tracker) {
        struct Tracker *t = *tracker;

        // Free advancements data
        if (t->advancements) {
            for (int i = 0; i < t->advancement_count; i++) {
                if (t->advancements[i]) {
                    // Free criteria if they exist
                    if (t->advancements[i]->criteria) {
                        for (int j = 0; j < t->advancements[i]->criteria_count; j++) {
                            free(t->advancements[i]->criteria[j]);
                        }
                        // Free the array of criterion pointers
                        free(t->advancements[i]->criteria);
                    }
                    // Free the advancement struct itself
                    free(t->advancements[i]);
                }
            }
            // Free the array of advancement pointers
            free(t->advancements);
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

        // SDL_Quit(); // This is ONCE for all windows in the main loop

        // tracker is heap allocated so free it
        free(t);
        *tracker = NULL;
        tracker = NULL;
        printf("[TRACKER] Tracker freed!\n");
    }
}

void tracker_load_and_parse_advancements(struct Tracker *t) {
    printf("[TRACKER] Loading advancement template from: %s\n", t->advancement_template_path);
    cJSON *template_json = cJSON_from_file(t->advancement_template_path);
    if (!template_json) {
        fprintf(stderr, "[TRACKER] Failed to load or parse advancement template file.\n");
        return;
    }

    // Count advancements in the object
    int count = 0;
    cJSON *adv_json_iterator = NULL;
    cJSON_ArrayForEach(adv_json_iterator, template_json) {
        count++;
    }
    t->advancement_count = count;

    t->advancements = calloc(t->advancement_count, sizeof(Advancement*));
    if (!t->advancements) {
        fprintf(stderr, "[TRACKER] Failed to allocate memory for advancements array.\n");
        cJSON_Delete(template_json);
        return;
    }

    cJSON *adv_json = template_json->child;
    int i = 0;
    while(adv_json) {
        Advancement *new_adv = calloc(1, sizeof(Advancement));
        if (!new_adv) {
             adv_json = adv_json->next;
             continue; // Skip if allocation fails
        }

        // The key of the object is the root name
        strncpy(new_adv->root_name, adv_json->string, sizeof(new_adv->root_name) - 1);

        cJSON *displayName = cJSON_GetObjectItem(adv_json, "displayName");
        if (cJSON_IsString(displayName)) strncpy(new_adv->name, displayName->valuestring, sizeof(new_adv->name) - 1);

        new_adv->done = false;

        cJSON *criteria_obj = cJSON_GetObjectItem(adv_json, "criteria");
        if (criteria_obj) {
            int crit_count = 0;
            cJSON *crit_iterator = NULL;
            cJSON_ArrayForEach(crit_iterator, criteria_obj) { crit_count++; }
            new_adv->criteria_count = crit_count;

            if (new_adv->criteria_count > 0) {
                new_adv->criteria = calloc(new_adv->criteria_count, sizeof(Criterion*));
                if (new_adv->criteria) {
                    cJSON *crit_json = criteria_obj->child;
                    int j = 0;
                    while(crit_json) {
                        Criterion *new_crit = calloc(1, sizeof(Criterion));
                        if(!new_crit) {
                            crit_json = crit_json->next;
                            continue;
                        }

                        strncpy(new_crit->root_name, crit_json->string, sizeof(new_crit->root_name) - 1);
                        cJSON *crit_name = cJSON_GetObjectItem(crit_json, "name");
                        if(cJSON_IsString(crit_name)) strncpy(new_crit->name, crit_name->valuestring, sizeof(new_crit->name) - 1);
                        new_crit->done = false;

                        new_adv->criteria[j++] = new_crit;
                        crit_json = crit_json->next;
                    }
                }
            }
        }
        t->advancements[i++] = new_adv;
        adv_json = adv_json->next;
    }
    printf("[TRACKER] Successfully parsed %d advancements from template.\n", t->advancement_count);

    // Now, check against the player's actual file
    if (strlen(t->advancements_path) > 0) {
        cJSON *player_adv_json = cJSON_from_file(t->advancements_path);
        if(player_adv_json) {
            for(int k=0; k < t->advancement_count; ++k) {
                Advancement* adv = t->advancements[k];
                cJSON* player_entry = cJSON_GetObjectItem(player_adv_json, adv->root_name);
                if(player_entry) {
                    cJSON* done_flag = cJSON_GetObjectItem(player_entry, "done");
                    if(cJSON_IsTrue(done_flag)) {
                        adv->done = true;
                    }

                    // Check criteria
                    cJSON* player_criteria = cJSON_GetObjectItem(player_entry, "criteria");
                    if(player_criteria) {
                        for(int l=0; l < adv->criteria_count; ++l) {
                             if(cJSON_HasObjectItem(player_criteria, adv->criteria[l]->root_name)) {
                                 adv->criteria[l]->done = true;
                             }
                        }
                    }
                }
            }
            printf("[TRACKER] Updated completion status from player file.\n");
            cJSON_Delete(player_adv_json);
        }
    }

    cJSON_Delete(template_json);
}
