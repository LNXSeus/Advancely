//
// Created by Linus on 24.06.2025.
//

#include "overlay.h"
#include "init_sdl.h"
#include "settings_utils.h"
#include "format_utils.h"

#include <cstdio>
#include <cstdlib>
#include <cmath> // Required for roundf()

#define SOCIAL_CYCLE_SECONDS 30.0f

// TODO: Add more socials here
const char *SOCIALS[] = {
    "github.com/LNXSeus/Advancely",
    "youtube.com/@lnxs",
    "twitch.tv/lnxseus",
    "youtube.com/@lnxsarchive",
    "discord.gg/TyNgXDz",
    "streamlabs.com/lnxseus/tip"
};
const int NUM_SOCIALS = sizeof(SOCIALS) / sizeof(char *);


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

    o->scroll_offset = 0.0f;
    o->social_media_timer = 0.0f;
    o->current_social_index = 0;

    o->font = TTF_OpenFont("resources/fonts/Minecraft.ttf", (float)font_size);
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

    // Cycle through social media text
    o->social_media_timer += *deltaTime;
    if (o->social_media_timer >= SOCIAL_CYCLE_SECONDS) {
        o->social_media_timer -= SOCIAL_CYCLE_SECONDS;
        o->current_social_index = (o->current_social_index + 1) % NUM_SOCIALS;
    }

    // TODO: Wrap scroll_offset based on the total width of rendered overlay content.
    // For example: if (o->scroll_offset > total_content_width) { o->scroll_offset = 0; }
}


void overlay_render(Overlay *o, const Tracker *t, const AppSettings *settings) {
    SDL_SetRenderDrawColor(o->renderer, settings->overlay_bg_color.r, settings->overlay_bg_color.g,
                           settings->overlay_bg_color.b, settings->overlay_bg_color.a);
    // Clear the screen
    SDL_RenderClear(o->renderer);

    // Render Progress Text
    if (t && t->template_data) {
        char info_buffer[512];
        char final_buffer[1024];
        char formatted_time[64];
        char formatted_update_time[64];
        char formatted_category[128];

        format_category_string(settings->category, formatted_category, sizeof(formatted_category));
        format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));
        float last_update_time_5_seconds = floorf(t->time_since_last_update / 5.0f) * 5.0f;
        format_time_since_update(last_update_time_5_seconds, formatted_update_time, sizeof(formatted_update_time));

        MC_Version version = settings_get_version_from_string(settings->version_str);
        const char *adv_ach_label = (version >= MC_VERSION_1_12) ? "Adv" : "Ach";

        snprintf(info_buffer, sizeof(info_buffer),
                 "%s | %s - %s%s%s | %s: %d/%d - Prog: %.2f%% | %s IGT | Upd: %s",
                 t->world_name,
                 settings->version_str,
                 formatted_category,
                 *settings->optional_flag ? " - " : "",
                 settings->optional_flag,
                 adv_ach_label,
                 t->template_data->advancements_completed_count,
                 t->template_data->advancement_count,
                 t->template_data->overall_progress_percentage,
                 formatted_time,
                 formatted_update_time);

        // Append the cycling social media text
        snprintf(final_buffer, sizeof(final_buffer), "%s | %s", info_buffer, SOCIALS[o->current_social_index]);

        SDL_Color text_color = { settings->overlay_text_color.r, settings->overlay_text_color.g, settings->overlay_text_color.b, settings->overlay_text_color.a };
        SDL_Surface *text_surface = TTF_RenderText_Solid(o->font, final_buffer, 0, text_color);
        if (text_surface) {
            SDL_Texture *text_texture = SDL_CreateTextureFromSurface(o->renderer, text_surface);
            if (text_texture) {
                SDL_SetTextureScaleMode(text_texture, SDL_SCALEMODE_NEAREST);

                int overlay_w;
                SDL_GetWindowSize(o->window, &overlay_w, nullptr);
                const float padding = 10.0f;
                SDL_FRect dest_rect = { padding, padding, (float)text_surface->w, (float)text_surface->h };

                switch (settings->overlay_progress_text_align) {
                    case OVERLAY_PROGRESS_TEXT_ALIGN_CENTER:
                        dest_rect.x = ( (float)overlay_w - (float)text_surface->w ) / 2.0f;
                        break;
                    case OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT:
                        dest_rect.x = (float)overlay_w - (float)text_surface->w - padding;
                        break;
                    case OVERLAY_PROGRESS_TEXT_ALIGN_LEFT:
                    default:
                        dest_rect.x = padding;
                        break;
                }

                SDL_RenderTexture(o->renderer, text_texture, nullptr, &dest_rect);
                SDL_DestroyTexture(text_texture);
            }
            SDL_DestroySurface(text_surface);
        }
    }

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
