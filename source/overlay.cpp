//
// Created by Linus on 24.06.2025.
//

#include "overlay.h"
#include "init_sdl.h"
#include "settings_utils.h"

#include <cstdio>
#include <cstdlib>
#include <cmath> // Required for roundf()

bool overlay_new(Overlay **overlay, const AppSettings *settings) {
    // dereference once and use calloc
    *overlay = (Overlay *) calloc(1, sizeof(Overlay));
    // Check here if calloc failed
    if (*overlay == nullptr) {
        fprintf(stderr, "[OVERLAY] Error allocating memory for overlay.\n");
        return false;
    }

    // temp variable to not dereference over and over again
    Overlay *o = *overlay;

    if (!overlay_init_sdl(o, settings)) {
        overlay_free(overlay, settings);
        return false;
    }

    // Make font HiDPI aware
    float scale = SDL_GetWindowDisplayScale(o->window);
    if (scale == 0.0f) { // Handle potential errors where scale is 0
        scale = 1.0f;
    }
    int font_size = (int)roundf(24.0f * scale); // Scale base font size

    o->font = TTF_OpenFont("resources/fonts/Minecraft.ttf", font_size);
    if (!o->font) {
        fprintf(stderr, "[OVERLAY] Failed to load font: %s\n", SDL_GetError());
        overlay_free(overlay, settings);
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
                    if (settings->print_debug_status) {
                        printf("[OVERLAY] Overlay Space key pressed, speeding up tracker.\n");
                    }
                    // speed up tracker
                    // The speedup is applied to deltaTime, which affect the update rate of the animation in overlay_update()
                    *deltaTime *= OVERLAY_SPEEDUP_FACTOR;
                    break;
                default:
                    break;
            }
            break;
        // TODO: Work with mouse events (HANDLED by ImGui)
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

    // TODO: Adjust the 50.0f, it's just an arbitrary multiplier
    // Can be turned into a setting later, scroll speed now has direction
    o->scroll_offset += scroll_speed * (*deltaTime) * 50.0f;

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
    // SDL_RenderPresent(o->renderer);

    // TODO: Expand this
    // Example drawing one item
    SDL_Color text_color = {
        settings->overlay_text_color.r, settings->overlay_text_color.g, settings->overlay_text_color.b, settings->overlay_text_color.a
    };
    SDL_Surface *text_surface = TTF_RenderText_Solid(o->font, "Example Advancement", 0, text_color);
    // 0 for null terminated text
    if (text_surface) {
        SDL_Texture *text_texture = SDL_CreateTextureFromSurface(o->renderer, text_surface);
        if (text_texture) {
            // Keep text sharp when scaled (on overlay window, probably won't be scaled)
            SDL_SetTextureScaleMode(text_texture, SDL_SCALEMODE_NEAREST);

            // TODO: Change size of advancement backgrounds and text for overlay here
            SDL_FRect dest_rect = {100.0f, 100.0f, (float) text_surface->w, (float) text_surface->h};
            SDL_RenderTexture(o->renderer, text_texture, nullptr, &dest_rect);
            SDL_DestroyTexture(text_texture);
        }
        SDL_DestroySurface(text_surface);
    }

    SDL_RenderPresent(o->renderer);
}

void overlay_free(Overlay **overlay, const AppSettings *settings) {
    if (overlay && *overlay) {
        Overlay *o = *overlay;

        if (o->font) {
            TTF_CloseFont(o->font);
            o->font = nullptr;
        }

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

        if (settings->print_debug_status) {
            printf("[OVERLAY] Overlay freed!\n");
        }
    }
}
