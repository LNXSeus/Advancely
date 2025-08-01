//
// Created by Linus on 24.06.2025.
//

#include "overlay.h"
#include "init_sdl.h"
#include "settings_utils.h"

#include <cstdio>
#include <cstdlib>

bool overlay_new(Overlay **overlay, const AppSettings *settings) {
    // dereference once and use calloc
    *overlay = (Overlay *)calloc(1, sizeof(Overlay));
    // Check here if calloc failed
    if (*overlay == nullptr) {
        fprintf(stderr, "[OVERLAY] Error allocating memory for overlay.\n");
        return false;
    }

    // temp variable to not dereference over and over again
    Overlay *o = *overlay;

    if (!overlay_init_sdl(o, settings)) {
        overlay_free(overlay);
        return false;
    }
    return true;
}


void overlay_events(Overlay *o, SDL_Event *event, bool *is_running, float *deltaTime,
                    const AppSettings *settings) {
    (void) o;
    (void) settings;

    switch (event->type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            *is_running = false;
            break;

        case SDL_EVENT_KEY_DOWN:
            // Allowing repeats here
            switch (event->key.scancode) {
                case SDL_SCANCODE_SPACE:
                    printf("[OVERLAY] Overlay Space key pressed, speeding up tracker.\n");
                    // speed up tracker
                    // The speedup is applied to deltaTime, which affect the update rate of the animation in overlay_update()
                    *deltaTime *= OVERLAY_SPEEDUP_FACTOR;
                    break;
                default:
                    break;
            }
            break;
        // TODO: Work with mouse events
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            // printf("[OVERLAY] Mouse button pressed in overlay.\n");
            break;
        case SDL_EVENT_MOUSE_MOTION:
            // printf("[OVERLAY] Mouse moved in overlay.\n");
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            // printf("[OVERLAY] Mouse button released in overlay.\n");
            break;
        default:
            break;
    }
}

void overlay_update(Overlay *o, float *deltaTime, const AppSettings *settings) {
    // Game logic here
    // Animate the scroll offset
    float scroll_speed = settings->overlay_scroll_speed;
    float direction = settings->overlay_scroll_left_to_right ? 1.0f : -1.0f;

    // TODO: Adjust the 50.0f is just arbitrary multiplier
    // Can be turned into a setting later
    o->scroll_offset += scroll_speed * direction * (*deltaTime) * 50.0f;

    // TODO: Wrap scroll_offset based on the total width of rendered overlay content.
    // For example: if (o->scroll_offset > total_content_width) { o->scroll_offset = 0; }
}


void overlay_render(Overlay *o, const AppSettings *settings) {
    SDL_SetRenderDrawColor(o->renderer, settings->overlay_bg_color.r, settings->overlay_bg_color.g,
                           settings->overlay_bg_color.b, settings->overlay_bg_color.a);
    // Clear the screen
    SDL_RenderClear(o->renderer);

    // DRAWING HAPPENS HERE

    // TODO: When rendering items, use o->scroll_offset and settings->goal_align_left
    // to determine their position on the screen.

    // TODO: Draw the title text. This requires the SDL_ttf library,
    // which you already have included in main.h. You would load a font,
    // render text to a surface, create a texture, and then render the texture.
    // For now, this just draws the bar.

    // present backbuffer
    SDL_RenderPresent(o->renderer);
}

void overlay_free(Overlay **overlay) {
    if (overlay && *overlay) {
        Overlay *o = *overlay;

        if (o->renderer) {
            SDL_DestroyRenderer(o->renderer);

            // We still have an address
            o->renderer = nullptr;
        }

        if (o->window) {
            SDL_DestroyWindow(o->window);

            // We still have an address
            o->window = nullptr;
        }

        // SDL_Quit(); // This is ONCE for all windows in the main loop

        // tracker is heap allocated so free it
        free(o);
        o = nullptr;
        *overlay = nullptr;

        printf("[OVERLAY] Overlay freed!\n");
    }
}
