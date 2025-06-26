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
        fprintf(stderr, "Error allocating memory for overlay.\n");
        return false;
    }

    // temp variable to not dereference over and over again
    struct Overlay *o = *overlay;

    if (!overlay_init_sdl(o)) {
        return false;
    }
    return true;
}

// void overlay_events(struct Overlay *o, bool *is_running, float *deltaTime) {
//     while (SDL_PollEvent(&o->event)) {
//         // only care about events for this specific window
//         if (o->event.window.windowID == SDL_GetWindowID(o->window)) {
//             switch (o->event.type) {
//                 case SDL_EVENT_QUIT: // X button or Alt+F4
//                     *is_running = false;
//                     break;
//
//                 case SDL_EVENT_KEY_DOWN: // SPACE SHOULD SPEED UP TRACKER
//                     switch (o->event.key.scancode) {
//                         case SDL_SCANCODE_SPACE: // Can add some functionality to this
//                             printf("Overlay Space key pressed, speeding up tracker.\n");
//                             // speed up tracker
//                             *deltaTime *= OVERLAY_SPEEDUP_FACTOR;
//                             break;
//                         default:
//                             break;
//                     }
//                     break;
//                 default:
//                     break;
//             }
//         }
//     }
// }

void overlay_events(struct Overlay *o, SDL_Event *event, bool *is_running, float *deltaTime) {
    (void) o;
    (void) is_running;

    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
            if (event->key.repeat == 0) {
                switch (event->key.scancode) {
                    case SDL_SCANCODE_SPACE:
                        printf("Overlay Space key pressed, speeding up tracker.\n");
                        // speed up tracker
                        *deltaTime *= OVERLAY_SPEEDUP_FACTOR;
                        break;
                    default:
                        break;
                }

            }
        default:
            break;
    }
}

void overlay_update(struct Overlay *o, float *deltaTime) {
    // Game logic here
    (void) o;
    (void)deltaTime;
}


void overlay_render(struct Overlay *o) {
    SDL_SetRenderDrawColor(o->renderer, OVERLAY_BACKGROUND_COLOR.r, OVERLAY_BACKGROUND_COLOR.g,
                           OVERLAY_BACKGROUND_COLOR.b, OVERLAY_BACKGROUND_COLOR.a);
    // Clear the screen
    SDL_RenderClear(o->renderer);

    // DRAWING HAPPENS HERE

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

        printf("Overlay freed!\n");
    }
}
