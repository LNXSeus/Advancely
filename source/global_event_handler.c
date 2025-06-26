//
// Created by Linus on 26.06.2025.
//

#include "global_event_handler.h"


void handle_global_events(struct Tracker *tracker, struct Overlay *overlay, bool *is_running, float *deltaTime) {
    // create one event out of tracker->event and overlay->event
    // Both structs are unused currently
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        // TOP LEVEL QUIT
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            *is_running = false;
            break;
        }

        // switch (event.type) {
        //     case SDL_EVENT_KEY_DOWN:
        //         switch (event.key.scancode) {
        //             case SDL_SCANCODE_ESCAPE:
        //                 printf("Escape key pressed.\n"); // This should open up another window
        //                 break;
        //             case SDL_SCANCODE_SPACE: // Can add some functionality to this
        //                 printf("Overlay Space key pressed, speeding up tracker.\n");
        //                 // speed up tracker
        //                 *deltaTime *= OVERLAY_SPEEDUP_FACTOR;
        //                 break;
        //             default:
        //                 break;
        //         }
        //         break;
        //     default:
        //         break;
        // }
        // --- Dispatch keyboard/mouse events to the correct window handler ---
        if (event.type >= SDL_EVENT_KEY_DOWN && event.type <= SDL_EVENT_MOUSE_WHEEL) {
            if (event.key.windowID == SDL_GetWindowID(tracker->window)) {
                tracker_events(tracker, &event, is_running);
            } else if (event.key.windowID == SDL_GetWindowID(overlay->window)) {
                overlay_events(overlay, &event, is_running, deltaTime);
            }
        }
        // --- Dispatch other window-specific events (e.g., focus, resize) ---
        else if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
            if (event.window.windowID == SDL_GetWindowID(tracker->window)) {
                tracker_events(tracker, &event, is_running);
            } else if (event.window.windowID == SDL_GetWindowID(overlay->window)) {
                overlay_events(overlay, &event, is_running, deltaTime);
            }
        }
    }
}
