//
// Created by Linus on 26.06.2025.
//

#include "settings.h"
#include "init_sdl.h"

bool settings_new(struct Settings **settings) {
    // dereference once and use calloc
    *settings = calloc(1, sizeof(struct Settings));
    // Check here if calloc failed
    if (*settings == NULL) {
        fprintf(stderr, "Error allocating memory for settings.\n");
        return false;
    }

    // temp variable to dereference over and over again
    struct Settings *s = *settings;

    if (!settings_init_sdl(s)) {
        return false;
    }
    return true;
}


void settings_events(struct Settings *s, SDL_Event *event, bool *is_running, bool *settings_opened) {
    (void) s;
    (void) is_running;

    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
            if (event->key.repeat == 0) {
                switch (event->key.scancode) {
                    case SDL_SCANCODE_ESCAPE:
                        // Close settings
                        printf("Settings Space key pressed, closing settings.\n");
                        *settings_opened = false;
                        break;
                    default:
                        break;
                }
            }
            break;
        // TODO: Work with mouse events
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            printf("Mouse button pressed in settings.\n");
            break;
        case SDL_EVENT_MOUSE_MOTION:
            printf("Mouse moved in settings.\n");
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            printf("Mouse button released in settings.\n");
            break;
        default:
            break;
    }
}

void settings_update(struct Settings *s, float *deltaTime) {
    // Game logic here
    (void) s;
    (void) deltaTime;
}


void settings_render(struct Settings *s) {
    SDL_SetRenderDrawColor(s->renderer, SETTINGS_BACKGROUND_COLOR.r, SETTINGS_BACKGROUND_COLOR.g,
                           SETTINGS_BACKGROUND_COLOR.b, SETTINGS_BACKGROUND_COLOR.a);
    // Clear the screen
    SDL_RenderClear(s->renderer);

    // DRAWING HAPPENS HERE

    // present backbuffer
    SDL_RenderPresent(s->renderer);
}

void settings_free(struct Settings **settings) {
    if (*settings) {
        struct Settings *s = *settings;

        if (s->renderer) {
            SDL_DestroyRenderer(s->renderer);

            // We still have an address
            s->renderer = NULL;
        }

        if (s->window) {
            SDL_DestroyWindow(s->window);

            // We still have an address
            s->window = NULL;
        }

        // SDL_Quit(); // This is ONCE for all windows in the main loop

        // settings is heap allocated so free it
        free(s);
        s = NULL;
        *settings = NULL;

        printf("Settings freed!\n");
    }
}

