//
// Created by Linus on 26.06.2025.
//

#include "global_event_handler.h"


void handle_global_events(struct Tracker *t, struct Overlay *o, struct Settings *s, bool *is_running, bool *settings_opened, float *deltaTime) {
    // create one event out of tracker->event and overlay->event
    // Both structs are unused currently
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        // TOP LEVEL QUIT when it's not the X on the settings window
        if (event.type == SDL_EVENT_QUIT) {
            *is_running = false;
            break;
        }
        // Important saveguard to check for s to be not NULL, then settings were opened and can be closed
        if (s != NULL && event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.key.windowID == SDL_GetWindowID(s->window)) {
            *settings_opened = false;
            continue; // Skip next event
        }

        // --- Dispatch keyboard/mouse events to the correct window handler ---
        if (event.type >= SDL_EVENT_KEY_DOWN && event.type <= SDL_EVENT_MOUSE_WHEEL) {
            if (event.key.windowID == SDL_GetWindowID(t->window)) {
                tracker_events(t, &event, is_running, settings_opened);
            } else if (event.key.windowID == SDL_GetWindowID(o->window)) {
                overlay_events(o, &event, is_running, deltaTime);
            } else if (s != NULL && event.key.windowID == SDL_GetWindowID(s->window)) {
                settings_events(s, &event, is_running, settings_opened);
            }
        }
        // --- Dispatch other window-specific events (e.g., focus, resize) ---
        else if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
            if (event.window.windowID == SDL_GetWindowID(t->window)) {
                tracker_events(t, &event, is_running, settings_opened);
            } else if (event.window.windowID == SDL_GetWindowID(o->window)) {
                overlay_events(o, &event, is_running, deltaTime);
            } else if (s != NULL && event.window.windowID == SDL_GetWindowID(s->window)) {
                settings_events(s, &event, is_running, settings_opened);
            }
        }
    }
}
