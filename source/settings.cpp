//
// Created by Linus on 26.06.2025.
//


#include "settings.h"
#include "settings_utils.h"
#include "init_sdl.h"

#include <cstdio>
#include <cstdlib>

bool settings_new(Settings **settings, const AppSettings *app_settings, SDL_Window *parent) {
    // dereference once and use calloc
    *settings = (Settings *)calloc(1, sizeof(Settings));
    // Check here if calloc failed
    if (*settings == nullptr) {
        fprintf(stderr, "[SETTINGS] Error allocating memory for settings.\n");
        return false;
    }

    // temp variable to dereference over and over again
    Settings *s = *settings;
    s->parent_window = parent; // Store the parent window

    if (!settings_init_sdl(s, app_settings)) {
        settings_free(settings);
        return false;
    }
    return true;
}


void settings_events(Settings *s, SDL_Event *event, bool *is_running, bool *settings_opened) {
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

void settings_update(Settings *s, float *deltaTime) {
    // Game logic here
    (void) s;
    (void) deltaTime;
}


void settings_render(Settings *s, const AppSettings *app_settings) {
    SDL_SetRenderDrawColor(s->renderer, app_settings->settings_bg_color.r, app_settings->settings_bg_color.g,
                           app_settings->settings_bg_color.b, app_settings->settings_bg_color.a);
    // Clear the screen
    SDL_RenderClear(s->renderer);

    // DRAWING HAPPENS HERE

    // present backbuffer
    SDL_RenderPresent(s->renderer);
}

void settings_free(Settings **settings) {
    if (settings && *settings) {
        Settings *s = *settings;

        if (s->renderer) {
            SDL_DestroyRenderer(s->renderer);

            // We still have an address
            s->renderer = nullptr;
        }

        if (s->window) {
            SDL_DestroyWindow(s->window);

            // We still have an address
            s->window = nullptr;
        }

        // settings is heap allocated so free it
        free(s);
        s = nullptr;
        *settings = nullptr;

        printf("[SETTINGS] Settings freed!\n");
    }
}