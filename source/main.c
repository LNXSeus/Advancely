//
// Created by Linus on 24.06.2025.
//

#define DMON_IMPL // Required for dmon
#include "dmon.h"

#include <SDL3/SDL_main.h>
#include <SDL3/SDL_atomic.h>
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


/**
 * @brief Callback function for dmon file watcher.
 * This function is called by dmon in a separate thread whenever a file event occurs.
 * It sets a global flag (g_needs_update) to true if a file is modified.
 */
static void watch_callback(dmon_watch_id watch_id, dmon_action action, const char *rootdir, const char *filepath,
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


int main(int argc, char *argv[]) {
    // Satisfying Werror
    (void) argc;
    (void) argv;

    bool exit_status = EXIT_FAILURE;

    struct Tracker *tracker = NULL; // pass address to function
    struct Overlay *overlay = NULL;
    struct Settings *settings = NULL;

    if (tracker_new(&tracker) && overlay_new(&overlay)) {
        // --- DMON Setup ---

        dmon_init();
        if (strlen(tracker->saves_path) > 0) {
            printf("[DMON - MAIN] Watching saves directory: %s\n", tracker->saves_path);
            dmon_watch(tracker->saves_path, watch_callback, DMON_WATCHFLAGS_RECURSIVE, NULL);
            // Watch saves directory and monitor child diretories
        } else {
            fprintf(stderr, "[DMON - MAIN] Failed to watch saves directory as it's empty: %s\n", tracker->saves_path);
        }

        // Initialize the atomic flag to 1 to trigger an initial update.
        SDL_SetAtomicInt(&g_needs_update, 1);

        bool is_running = true;
        bool settings_opened = false;
        float last_frame_time = (float) SDL_GetTicks();

        // Unified Main Loop at 60 FPS
        while (is_running) {
            float current_time = (float) SDL_GetTicks();
            float deltaTime = (current_time - last_frame_time) / 1000.0f;
            last_frame_time = current_time;

            handle_global_events(tracker, overlay, settings, &is_running, &settings_opened, &deltaTime);

            // Close immediately if app not running
            if (!is_running) break;

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

            // --- Per-Frame Logic ---

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
    SDL_Quit(); // This is ONCE for all windows

    // One happy path
    return exit_status;
}
