//
// Created by Linus on 24.06.2025.
//

#define DMON_IMPL // Required for dmon
#include "dmon.h"

#include <SDL3/SDL_main.h>
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_mutex.h>
#include "tracker.h" // includes main.h
#include "overlay.h"
#include "settings.h"
#include "global_event_handler.h"
#include "path_utils.h" // Include for find_latest_world_files
#include "settings_utils.h" // Include for AppSettings and version checking

// Gloabal flag to signal that an update is needed
// Set to true initially to perform the first update

// global flag TODO: Should be set to true when custom goal is checked off (manual update) -> SDL_SetAtomicInt(&g_needs_update, 1);
// TODO: Currently tracker needs to be restarted when saves path is changed in settings.json file
static SDL_AtomicInt g_needs_update;
static SDL_AtomicInt g_settings_changed; // Watching when settings.json is modified to re-init paths

// Global mutex to protect the watcher and paths (see they don't break when called in close succession)
static SDL_Mutex *g_watcher_mutex = NULL;



/**
 * @brief Callback function for dmon file watcher.
 * This function is called by dmon in a separate thread whenever a file event occurs.
 * It sets a global flag (g_needs_update) to true if a file is modified.
 */
static void global_watch_callback(dmon_watch_id watch_id, dmon_action action, const char *rootdir, const char *filepath,
                                  const char *oldfilepath, void *user) {
    // Satisfying Werror - not used
    (void) watch_id;
    (void) rootdir;
    (void) oldfilepath;
    (void) user;

    // We only care about file modifications to existing files
    if (action == DMON_ACTION_MODIFY) {
        const char *ext = strrchr(filepath, '.'); // Locate last '.' in string
        if (ext && strcmp(ext, ".json") == 0) {
            // Check if file extension is .json, IMPORTANT: This triggers for useless .json files as well
            printf("[DMON - MAIN] File modified: %s. Triggering update.\n", filepath);
            // Automatically set the flag to 1. Safe to call from any thread.
            SDL_SetAtomicInt(&g_needs_update, 1);
        }
    }
}

/**
 * @brief Callback for dmon watching the CONFIG directory.
 */
static void settings_watch_callback(dmon_watch_id watch_id, dmon_action action, const char *rootdir,
                                    const char *filepath, const char *oldfilepath, void *user) {
    (void) watch_id;
    (void) rootdir;
    (void) oldfilepath;
    (void) user;

    if (action == DMON_ACTION_MODIFY) {
        // Check if the modified file is our settings file
        if (strcmp(filepath, "settings.json") == 0) {
            printf("[DMON - MAIN] settings.json modified. Triggering update.\n");
            SDL_SetAtomicInt(&g_settings_changed, 1);
        }
    }
}

int main(int argc, char *argv[]) {
    // Satisfying Werror
    (void) argc;
    (void) argv;

    bool exit_status = EXIT_FAILURE;

    struct Tracker *tracker = NULL; // pass address to function
    struct Overlay *overlay = NULL;
    struct Settings *settings = NULL;

    // Variable to hold the ID of our saves watcher
    dmon_watch_id saves_watcher_id;
    g_watcher_mutex = SDL_CreateMutex();

    if (!g_watcher_mutex) {
        fprintf(stderr, "[MAIN] Failed to create mutex: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    if (tracker_new(&tracker) && overlay_new(&overlay)) {

        dmon_init();

        // HARDCODED SETTINGS DIRECTORY
        printf("[DMON - MAIN] Watching config directory: resources/config/\n");
        dmon_watch("resources/config/", settings_watch_callback, 0, NULL);

        // Watch saves directory and store the watcher ID
        if (strlen(tracker->saves_path) > 0) {
            printf("[DMON - MAIN] Watching saves directory: %s\n", tracker->saves_path);
            // Watch saves directory and monitor child diretories
            saves_watcher_id = dmon_watch(tracker->saves_path, global_watch_callback, DMON_WATCHFLAGS_RECURSIVE, NULL);
        } else {
            fprintf(stderr, "[DMON - MAIN] Failed to watch saves directory as it's empty: %s\n", tracker->saves_path);
        }

        // Initialize the atomic flag to 1 to trigger an initial update.
        SDL_SetAtomicInt(&g_needs_update, 1);
        SDL_SetAtomicInt(&g_settings_changed, 0);

        bool is_running = true;
        bool settings_opened = false;
        float last_frame_time = (float) SDL_GetTicks();

        // Unified Main Loop at 60 FPS
        while (is_running) {
            float current_time = (float) SDL_GetTicks();
            float deltaTime = (current_time - last_frame_time) / 1000.0f;
            last_frame_time = current_time;

            // --- Per-Frame Logic ---

            handle_global_events(tracker, overlay, settings, &is_running, &settings_opened, &deltaTime);
            const Uint8 *key_state = (const Uint8 *)SDL_GetKeyboardState(NULL);
            static Uint32 last_hotkey_time = 0; // Prevent rapid-fire counting

            // HOTKEY LOGIC

            if (SDL_GetTicks() - last_hotkey_time > 200) {
                AppSettings app_settings;
                settings_load(&app_settings); // Load settings to get current hotkey bindings, TODO: List all possible hotkeys
                bool changed = false;

                for (int i = 0; i < app_settings.hotkey_count; i++) {
                    HotkeyBinding *hb = &app_settings.hotkeys[i];
                    TrackableItem *target_goal = NULL;

                    // Find the goal this hotkey is bound to
                    for (int j = 0; j < tracker->template_data->custom_goal_count; j++) {
                        if (strcmp(tracker->template_data->custom_goals[j]->root_name, hb->target_goal) == 0) {
                            target_goal = tracker->template_data->custom_goals[j];
                            break;
                        }
                    }
                    if (!target_goal) continue;

                    // Check if increment or decrement key is pressed
                    if (key_state[hb->increment_scancode]) {
                        target_goal->progress++;
                        changed = true;
                    } else if (key_state[hb->decrement_scancode]) {
                        target_goal->progress--;
                        changed = true;
                    }
                }

                if (changed) {
                    last_hotkey_time = SDL_GetTicks(); // Update timestamp

                    // Use the flexible save system
                    cJSON *settings = settings_read_full();
                    if (settings) {
                        cJSON *progress_obj = cJSON_GetObjectItem(settings, "custom_progress");
                        if (!progress_obj) {
                            // If it doesn't exist, create it
                            progress_obj = cJSON_AddObjectToObject(settings, "custom_progress");
                        }

                        // Save all custom goals back to the object
                        for (int i = 0; i < tracker->template_data->custom_goal_count; i++) {
                            TrackableItem *item = tracker->template_data->custom_goals[i];
                            if (item->goal > 0) { // Save numbers for counters
                                cJSON_ReplaceItemInObject(progress_obj, item->root_name, cJSON_CreateNumber(item->progress));
                            } else { // Save booleans for toggles
                                cJSON_ReplaceItemInObject(progress_obj, item->root_name, cJSON_CreateBool(item->done));
                            }
                        }
                        settings_write_full(settings);
                        cJSON_Delete(settings);
                    }
                    SDL_SetAtomicInt(&g_needs_update, 1); // Trigger UI update
                }
            }

            // Close immediately if app not running
            if (!is_running) break;

            // Lock mutex before touching watchers or paths
            SDL_LockMutex(g_watcher_mutex);

            // Check if settings.json has been modified
            if (SDL_SetAtomicInt(&g_settings_changed, 0) == 1) {
                printf("[MAIN] Settings changed. Re-initializing paths and file watcher.\n");

                // Stop watching the old directory
                dmon_unwatch(saves_watcher_id);

                // Update the tracker with the new paths
                tracker_reinit_paths(tracker); // changes tracker->saves_path

                // Start watching the new directory
                if (strlen(tracker->saves_path) > 0) {
                    printf("[MAIN] Now watching new saves directory: %s\n", tracker->saves_path);
                    saves_watcher_id = dmon_watch(tracker->saves_path, global_watch_callback, DMON_WATCHFLAGS_RECURSIVE, NULL);
                } else {
                    fprintf(stderr, "[MAIN] Failed to watch new saves directory as it's empty: %s\n", tracker->saves_path);
                }

                // Force an update from the new path
                SDL_SetAtomicInt(&g_needs_update, 1);
            }

            // Check if dmon (or manual update through custom goal) has requested an update
            // Atomically check if the flag is 1, and if so, set it to 0.
            if (SDL_SetAtomicInt(&g_needs_update, 0) == 1) {
                // Re-scan for the latest world to handle world switching
                AppSettings app_settings;
                settings_load(&app_settings);
                MC_Version version = settings_get_version_from_string(app_settings.version_str);
                find_latest_world_files(
                    tracker->saves_path,
                    tracker->advancements_path,
                    tracker->stats_path,
                    tracker->unlocks_path,
                    MAX_PATH_LENGTH,
                    (version >= MC_VERSION_1_12),
                    (version >= MC_VERSION_25W14CRAFTMINE)
                );

                // Now update progress with the correct paths
                tracker_update(tracker, &deltaTime);
                tracker_print_debug_status(tracker);
            }

            // Unlock mutex after all updates are done
            SDL_UnlockMutex(g_watcher_mutex);

            // Initialize settings when not opened
            if (settings_opened && settings == NULL) {
                // Showing settings if window is closed
                if (!settings_new(&settings)) settings_opened = false;
            } else if (!settings_opened && settings != NULL) {
                // Free it
                settings_free(&settings);
            }

            // Freeze other windows when settings are opened
            if (settings_opened && settings != NULL) {
                settings_update(settings, &deltaTime);
                settings_render(settings);
            } else {
                // Overlay animations should run every frame
                overlay_update(overlay, &deltaTime);
                tracker_render(tracker);
                overlay_render(overlay);
            }

            // --- Frame limiting ---
            const float frame_time = (float) SDL_GetTicks() - current_time;
            if (frame_time < FRAME_TARGET_TIME) {
                SDL_Delay(FRAME_TARGET_TIME - frame_time);
            }
        }
        exit_status = EXIT_SUCCESS;
    }

    dmon_deinit();
    tracker_free(&tracker);
    overlay_free(&overlay);
    settings_free(&settings);
    SDL_DestroyMutex(g_watcher_mutex); // Destroy the mutex
    SDL_Quit(); // This is ONCE for all windows

    // One happy path
    return exit_status;
}
