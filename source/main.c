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
#include "path_utils.h" // Include for find_player_data_files
#include "settings_utils.h" // Include for AppSettings and version checking

// Gloabal flag to signal that an update is needed
// Set to true initially to perform the first update

// global flag TODO: Should be set to true when custom goal is checked off (manual update) -> SDL_SetAtomicInt(&g_needs_update, 1);
// We make g_needs_update available to global_event_handler.h with external linkage
SDL_AtomicInt g_needs_update;
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
        if (ext && (strcmp(ext, ".json") == 0) | (strcmp(ext, ".dat") == 0)) {
            // Check for .json or .dat
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


// ------------------------------------ END OF STATIC FUNCTIONS ------------------------------------


int main(int argc, char *argv[]) {
    // Satisfying Werror
    (void) argc;
    (void) argv;

    // Expect the worst
    bool exit_status = EXIT_FAILURE;

    // Load settings ONCE at the start and check if file was incomplete to use default values
    // settings_load() returns true if the file was incomplete and used default values
    AppSettings app_settings;
    if (settings_load(&app_settings)) {
        printf("[MAIN] Settings file was incomplete or missing, saving with default values.\n");
        settings_save(&app_settings, NULL); // Save complete settings back to the file
    }


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

    if (tracker_new(&tracker, &app_settings) && overlay_new(&overlay, &app_settings)) {
        dmon_init();
        SDL_SetAtomicInt(&g_needs_update, 1);
        SDL_SetAtomicInt(&g_settings_changed, 0);

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

        bool is_running = true;
        bool settings_opened = false;
        Uint32 last_frame_time = SDL_GetTicks();
        float frame_target_time = 1000.0f / app_settings.fps;

        // Unified Main Loop at 60 FPS
        while (is_running) {
            Uint32 current_time = SDL_GetTicks();
            float deltaTime = (float) (current_time - last_frame_time) / 1000.0f;
            last_frame_time = current_time;

            // --- Per-Frame Logic ---

            // Lock mutex before touching watchers or paths
            SDL_LockMutex(g_watcher_mutex);

            handle_global_events(tracker, overlay, settings, &app_settings, &is_running, &settings_opened, &deltaTime);


            // Close immediately if app not running
            if (!is_running) break;

            // Check if settings.json has been modified
            if (SDL_SetAtomicInt(&g_settings_changed, 0) == 1) {
                printf("[MAIN] Settings changed. Re-initializing paths and file watcher.\n");

                // Store old critical settings for comparison
                // Making sure only NOT EVERY change in settings.json reloads game data (problem with legacy version
                // resetting difference to snapshot when tracker window was moved)
                char old_version[64];
                char old_category[MAX_PATH_LENGTH];
                strncpy(old_version, app_settings.version_str, 64);
                strncpy(old_category, app_settings.category, MAX_PATH_LENGTH);

                // Update hotkeys during runtime
                settings_load(&app_settings); // Reload settings

                // ONLY RE-INIT IF A CRITICAL SETTING HAS CHANGED
                // Not something like window position of the tracker
                if (strcmp(old_version, app_settings.version_str) != 0 ||
                    strcmp(old_category, app_settings.category) != 0) {
                    printf("[MAIN] Critical settings (saves path, version, category) changed. Re-initializing template.\n");

                    // Stop watching the old directory
                    dmon_unwatch(saves_watcher_id);

                    // Update the tracker with the new paths and template data
                    tracker_reinit_template(tracker, &app_settings);
                }

                // Start watching the new directory
                if (strlen(tracker->saves_path) > 0) {
                    printf("[MAIN] Now watching new saves directory: %s\n", tracker->saves_path);
                    saves_watcher_id = dmon_watch(tracker->saves_path, global_watch_callback, DMON_WATCHFLAGS_RECURSIVE,
                                                  NULL);
                }
                SDL_SetAtomicInt(&g_needs_update, 1);

                // ALWAYS apply non-critical changes
                frame_target_time = 1000.0f / app_settings.fps; // Update frame limiter if fps has changed in settings

                // Change always on top flag during runtime in settings.json
                if (!settings_opened) {
                    SDL_SetWindowAlwaysOnTop(tracker->window, app_settings.tracker_always_on_top);
                }
            }

            // Check if dmon (or manual update through custom goal) has requested an update
            // Atomically check if the flag is 1, and if so, set it to 0.
            if (SDL_SetAtomicInt(&g_needs_update, 0) == 1) {
                // Re-scan for the latest world to handle world switching
                AppSettings current_settings;
                settings_load(&current_settings); // Also constructs the template paths
                MC_Version version = settings_get_version_from_string(current_settings.version_str);
                find_player_data_files(
                    tracker->saves_path,
                    version,
                    tracker->world_name,
                    tracker->advancements_path,
                    tracker->stats_path,
                    tracker->unlocks_path,
                    MAX_PATH_LENGTH
                );

                // Now update progress with the correct paths
                tracker_update(tracker, &deltaTime);
                tracker_print_debug_status(tracker);

                // Update TITLE of the tracker window with some info, similar to the debug print
                tracker_update_title(tracker, &app_settings);
            }

            // Unlock mutex after all updates are done
            SDL_UnlockMutex(g_watcher_mutex);

            // Initialize settings when not opened
            if (settings_opened && settings == NULL) {
                // Disable always-on-top before opening settings
                SDL_SetWindowAlwaysOnTop(tracker->window, false);
                if (!settings_new(&settings, &app_settings, tracker->window)) settings_opened = false;
            } else if (!settings_opened && settings != NULL) {
                // Restore always-on-top statues based on settings when closing settings
                SDL_SetWindowAlwaysOnTop(tracker->window, app_settings.tracker_always_on_top);
                settings_free(&settings);
            }

            // Freeze other windows when settings are opened
            if (settings_opened && settings != NULL) {
                settings_update(settings, &deltaTime);
                settings_render(settings, &app_settings);
            } else {
                // Overlay animations should run every frame
                overlay_update(overlay, &deltaTime, &app_settings);
                tracker_render(tracker, &app_settings);
                overlay_render(overlay, &app_settings);
            }

            // --- Frame limiting ---
            const float frame_time = (float) SDL_GetTicks() - current_time;
            if (frame_time < frame_target_time) {
                SDL_Delay((Uint32) (frame_target_time - frame_time));
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
