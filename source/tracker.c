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

    t->is_running = true;

    return true;
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

        SDL_Quit();

        // tracker is heap allocated so free it
        free(t);
        t = NULL;
        *tracker = NULL;

        printf("All freed!\n");
    }
}

void tracker_events(struct Tracker *t) {
    while (SDL_PollEvent(&t->event)) {

        switch (t->event.type) {
            case SDL_EVENT_QUIT: // X button or Alt+F4
                t->is_running = false;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: // This is right OR left click, CAN'T HOLD IT
                if (SDL_EVENT_MOUSE_BUTTON_UP) { // Make sure only single click
                    printf("Mouse button %d pressed %d.\n", t->event.mdevice.type, t->event.button.clicks);
                    break;
                }

            case SDL_EVENT_KEY_DOWN:
                // Make sure Esc does not repeat
                 if (t->event.key.repeat == 0) {
                    switch (t->event.key.scancode) {
                        case SDL_SCANCODE_ESCAPE:
                            printf("Escape key pressed.\n"); // This should open up another window
                            break;
                        case SDL_SCANCODE_SPACE: // Can add some functionality to this
                            printf("Space key pressed.\n");
                            break;
                        default:
                            break;
                     }
                }

            default:
                break;
        }
    }
}

void tracker_run(struct Tracker *t) {
    float last_frame_time = 0;
    float deltaTime = 0.0f; // TODO: Used for calculating Speed later
    // Tracker loop running as long as is_running is true
    while (t->is_running) {
        float current_time = (float)SDL_GetTicks();
        deltaTime = (current_time - last_frame_time) / 1000.0f;
        last_frame_time = current_time;

        // 60 FPS GAME LOOP START ------------------------------------------------
        // Call tracker_events
        tracker_events(t);

        SDL_SetRenderDrawColor(t->renderer, TRACKER_BACKGROUND_COLOR.r, TRACKER_BACKGROUND_COLOR.g, TRACKER_BACKGROUND_COLOR.b, TRACKER_BACKGROUND_COLOR.a);

        // Clear the screen
        SDL_RenderClear(t->renderer);

        // DRAWING HAPPENS HERE

        SDL_RenderPresent(t->renderer);

        // 60 FPS GAME LOOP END ------------------------------------------------
        const float frame_time = (float)SDL_GetTicks() - current_time;
        if (frame_time < FRAME_TARGET_TIME) {
            SDL_Delay(FRAME_TARGET_TIME - frame_time);
        }
    }
}
