//
// Created by Linus on 26.06.2025.
//

#include "settings.h"
#include "init_sdl.h"

bool settings_new(struct Settings **settings, const AppSettings *app_settings, SDL_Window *parent) {
    // dereference once and use calloc
    *settings = calloc(1, sizeof(struct Settings));
    // Check here if calloc failed
    if (*settings == NULL) {
        fprintf(stderr, "[SETTINGS] Error allocating memory for settings.\n");
        return false;
    }

    // temp variable to dereference over and over again
    struct Settings *s = *settings;
    s->parent_window = parent; // Store the parent window

    if (!settings_init_sdl(s, app_settings)) {
        settings_free(settings);
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
                        printf("[SETTINGS] Settings Escape key pressed, closing settings.\n");
                        *settings_opened = false;
                        break;
                    default:
                        break;
                }
            }
            break;
        // TODO: Work with mouse events
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            // printf("[SETTINGS] Mouse button pressed in settings.\n");
            break;
        case SDL_EVENT_MOUSE_MOTION:
            // printf("[SETTINGS] Mouse moved in settings.\n");
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            // printf("[SETTINGS] Mouse button released in settings.\n");
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


void settings_render(struct Settings *s, const AppSettings *app_settings) {
    SDL_SetRenderDrawColor(s->renderer, app_settings->settings_bg_color.r, app_settings->settings_bg_color.g,
                           app_settings->settings_bg_color.b, app_settings->settings_bg_color.a);
    // Clear the screen
    SDL_RenderClear(s->renderer);

    // DRAWING HAPPENS HERE

    // present backbuffer
    SDL_RenderPresent(s->renderer);
}

void settings_free(struct Settings **settings) {
    if (settings && *settings) {
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

        // settings is heap allocated so free it
        free(s);
        s = NULL;
        *settings = NULL;

        printf("[SETTINGS] Settings freed!\n");
    }
}
