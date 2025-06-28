//
// Created by Linus on 24.06.2025.
//

#include "overlay.h"
#include "init_sdl.h"

bool overlay_new(struct Overlay **overlay) {
    // dereference once and use calloc
    *overlay = calloc(1, sizeof(struct Overlay));
    // Check here if calloc failed
    if (*overlay == NULL) {
        fprintf(stderr, "[OVERLAY] Error allocating memory for overlay.\n");
        return false;
    }

    // temp variable to not dereference over and over again
    struct Overlay *o = *overlay;

    if (!overlay_init_sdl(o)) {
        return false;
    }
    return true;
}


void overlay_events(struct Overlay *o, SDL_Event *event, bool *is_running, float *deltaTime) {
    (void) o;
    (void) is_running;

    switch (event->type) {

        case SDL_EVENT_KEY_DOWN:
            // Allowing repeats here
            switch (event->key.scancode) {
                case SDL_SCANCODE_SPACE:
                    printf("[OVERLAY] Overlay Space key pressed, speeding up tracker.\n");
                    // speed up tracker
                    *deltaTime *= OVERLAY_SPEEDUP_FACTOR;
                    break;
                default:
                    break;
            }
            break;
        // TODO: Work with mouse events
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            printf("[OVERLAY] Mouse button pressed in overlay.\n");
            break;
        case SDL_EVENT_MOUSE_MOTION:
            printf("[OVERLAY] Mouse moved in overlay.\n");
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            printf("[OVERLAY] Mouse button released in overlay.\n");
            break;
        default:
            break;
    }
}

void overlay_update(struct Overlay *o, float *deltaTime) {
    // Game logic here
    (void) o;
    (void) deltaTime;
}


void overlay_render(struct Overlay *o) {
    SDL_SetRenderDrawColor(o->renderer, OVERLAY_BACKGROUND_COLOR.r, OVERLAY_BACKGROUND_COLOR.g,
                           OVERLAY_BACKGROUND_COLOR.b, OVERLAY_BACKGROUND_COLOR.a);
    // Clear the screen
    SDL_RenderClear(o->renderer);

    // DRAWING HAPPENS HERE

    // Define title bar
    const SDL_FRect title_bar_rect = {0, 0, OVERLAY_WIDTH, OVERLAY_TITLE_BAR_HEIGHT};
    const SDL_Color title_bar_color = {43, 43, 43, 255}; // RGBA

    SDL_SetRenderDrawColor(o->renderer, title_bar_color.r, title_bar_color.g, title_bar_color.b,
                           title_bar_color.a);
    SDL_RenderFillRect(o->renderer, &title_bar_rect);

    // TODO: Draw the title text. This requires the SDL_ttf library,
    // which you already have included in main.h. You would load a font,
    // render text to a surface, create a texture, and then render the texture.
    // For now, this just draws the bar.

    // present backbuffer
    SDL_RenderPresent(o->renderer);
}

void overlay_free(struct Overlay **overlay) {
    if (*overlay) {
        struct Overlay *o = *overlay;

        if (o->renderer) {
            SDL_DestroyRenderer(o->renderer);

            // We still have an address
            o->renderer = NULL;
        }

        if (o->window) {
            SDL_DestroyWindow(o->window);

            // We still have an address
            o->window = NULL;
        }

        // SDL_Quit(); // This is ONCE for all windows in the main loop

        // tracker is heap allocated so free it
        free(o);
        o = NULL;
        *overlay = NULL;

        printf("[OVERLAY] Overlay freed!\n");
    }
}
