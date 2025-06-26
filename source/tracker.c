//
// Created by Linus on 24.06.2025.
//

#include "tracker.h"
#include "init_sdl.h"

bool tracker_new(struct Tracker **tracker) {
    // dereference once and use calloc
    *tracker = calloc(1, sizeof(struct Tracker));
    // Check here if calloc failed
    if (*tracker == NULL) {
        fprintf(stderr, "Error allocating memory for tracker.\n");
        return false;
    }

    // temp variable to not dereference over and over again
    struct Tracker *t = *tracker;

    if (!tracker_init_sdl(t)) {
        return false;
    }
    return true;
}

void  tracker_events(struct Tracker *t, SDL_Event *event, bool *is_running, bool *settings_opened) {
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
                        printf("Escape key pressed in tracker: Opening settings window now.\n");
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
            printf("Mouse button pressed in tracker.\n");
            break;
        case SDL_EVENT_MOUSE_MOTION:
            printf("Mouse moved in tracker.\n");
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            printf("Mouse button released in tracker.\n");
            break;
        default:
            break;
    }
}

void tracker_update(struct Tracker *t, float *deltaTime) {
    // Use deltaTime for animations
    // game logic goes here
    (void) t;
    (void) deltaTime;
}

void tracker_render(struct Tracker *t) {
    // Set draw color and clear screen
    SDL_SetRenderDrawColor(t->renderer, TRACKER_BACKGROUND_COLOR.r, TRACKER_BACKGROUND_COLOR.g,
                           TRACKER_BACKGROUND_COLOR.b, TRACKER_BACKGROUND_COLOR.a);
    SDL_RenderClear(t->renderer);

    // Drawing happens here

    // present backbuffer
    SDL_RenderPresent(t->renderer);
}

void tracker_free(struct Tracker **tracker) {
    if (*tracker) {
        struct Tracker *t = *tracker;

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
        // t = NULL;
        *tracker = NULL;

        printf("Tracker freed!\n");
    }
}
