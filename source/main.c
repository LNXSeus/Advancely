//
// Created by Linus on 24.06.2025.
//

#include <SDL3/SDL_main.h>
#include "tracker.h" // includes main.h
#include "overlay.h"
#include "settings.h"
#include "global_event_handler.h"


int main(int argc, char *argv[]) {
    // Satisfying Werror
    (void) argc;
    (void) argv;

    bool exit_status = EXIT_FAILURE;

    struct Tracker *tracker = NULL; // pass address to function
    struct Overlay *overlay = NULL;
    struct Settings *settings = NULL;

    if (tracker_new(&tracker) && overlay_new(&overlay)) {
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
            if (!is_running) {
                break;
            }

            // Initialize settings when not opened
            if (settings_opened && settings == NULL) {
                // Showing settings if window is closed
                if (!settings_new(&settings)) {
                    settings_opened = false;
                }
            } else if (!settings_opened && settings != NULL) {
                // Free it
                settings_free(&settings);
            }

            // Freeze other windows when settings are opened
            if (settings_opened && settings != NULL) {
                settings_update(settings, &deltaTime);
                settings_render(settings);
            } else {
                tracker_update(tracker, &deltaTime);
                overlay_update(overlay, &deltaTime);
            }

            tracker_render(tracker);
            overlay_render(overlay);

            // --- Frame limiting ---
            const float frame_time = (float) SDL_GetTicks() - current_time;
            if (frame_time < FRAME_TARGET_TIME) {
                SDL_Delay(FRAME_TARGET_TIME - frame_time);
            }
        }
        exit_status = EXIT_SUCCESS;
    }

    tracker_free(&tracker);
    overlay_free(&overlay);
    settings_free(&settings);

    SDL_Quit(); // This is ONCE for all windows

    // One happy path
    return exit_status;
}
