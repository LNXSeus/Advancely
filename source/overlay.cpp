// Copyright (c) 2025 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 24.06.2025.
//

#include "overlay.h" // Has tracker.h
#include "init_sdl.h"
#include "settings_utils.h"
#include "format_utils.h"
#include "logger.h"

#include <cstdio>
#include <cstdlib>
#include <cmath> // Required for roundf()
#include <string>
#include <vector> // Required for collecting items to render
#include <algorithm> // Required for std::reverse

#define SOCIAL_CYCLE_SECONDS 15.0f

// TODO: Add more socials here
const char *SOCIALS[] = {
    "Advancely " ADVANCELY_VERSION "!",
    "Advance to the",
    "next level with",
    "Advancely!",
    "Download Advancely at",
    "github.com/LNXSeus/Advancely",
    "Support LNXS on",
    "youtube.com/@lnxs",
    "Support LNXSeus on",
    "twitch.tv/lnxseus",
    "Support LNXS on",
    "youtube.com/@lnxsarchive",
    "Support LNXSeus on",
    "discord.gg/TyNgXDz",
    "Donate to Advancely on",
    "streamlabs.com/lnxseus/tip",
};
const int NUM_SOCIALS = sizeof(SOCIALS) / sizeof(char *);

// --- Supporter Showcase for Completed Runs ---
// A list of all available icons for the supporter showcase.
const char *SUPPORTER_ICONS[] = {
    "emotes/glorpLove-4x.png",
    "emotes/Lnxseuheart.png",
    "emotes/Love_emote.png",
    "emotes/luvv-4x.png",
    "emotes/poggSpin-4x_unoptimized.gif",
    "emotes/peepoLove-4x.png",
    "emotes/catHeart-4x_unoptimized.gif",
    "emotes/doorLove-4x.gif"
};
const int NUM_SUPPORTER_ICONS = sizeof(SUPPORTER_ICONS) / sizeof(char *);

// A structure to hold supporter information.
typedef struct {
    const char *name;
    float amount;
} Supporter;

// The list of supporters and their donation amounts.
Supporter SUPPORTERS[] = {
    {"zurtleTif", 20.0f},
    {"ethansplace98", 30.0f},
    {"Totorewa", 31.0f}
    // {"LNXS", 20.0f},
    // {"Test", 2.0f},
    // {"Anonymous", 15.0f},
    // {"Another Supporter", 2.5f},
    // {"This could be you", 12.0f}
};
const int NUM_SUPPORTERS = sizeof(SUPPORTERS) / sizeof(Supporter);

// A structure to hold the pre-calculated rendering info for each supporter.
typedef struct {
    const Supporter *supporter;
    const char *icon_path;
    SDL_Texture *background_static;
    AnimatedTexture *background_anim;
} SupporterRenderInfo;

/**
 * @brief Helper function for text caching to improve performance.
 * @param o The Overlay instance
 * @param text The text to cache
 * @param color The color of the text
 * @return The SDL_Texture of the cached text
 */
static SDL_Texture *get_text_texture_from_cache(Overlay *o, const char *text, SDL_Color color) {
    if (!text || text[0] == '\0') {
        return nullptr;
    }

    // 1. Check if the texture is already in the cache
    for (int i = 0; i < o->text_cache_count; i++) {
        TextCacheEntry *entry = &o->text_cache[i];
        if (strcmp(entry->text, text) == 0 &&
            entry->color.r == color.r && entry->color.g == color.g &&
            entry->color.b == color.b && entry->color.a == color.a) {
            return entry->texture;
        }
    }

    // 2. If not in cache, create it and add it
    SDL_Surface *text_surface = TTF_RenderText_Blended(o->font, text, 0, color);
    if (!text_surface) {
        return nullptr;
    }
    SDL_Texture *text_texture = SDL_CreateTextureFromSurface(o->renderer, text_surface);
    SDL_DestroySurface(text_surface);
    if (!text_texture) {
        return nullptr;
    }
    SDL_SetTextureScaleMode(text_texture, SDL_SCALEMODE_NEAREST);

    // 3. Add to cache, resizing if necessary
    if (o->text_cache_count >= o->text_cache_capacity) {
        int new_capacity = o->text_cache_capacity == 0 ? 32 : o->text_cache_capacity * 2;
        auto *new_cache = (TextCacheEntry *) realloc(o->text_cache, new_capacity * sizeof(TextCacheEntry));
        if (!new_cache) {
            SDL_DestroyTexture(text_texture);
            return nullptr;
        }
        o->text_cache = new_cache;
        o->text_cache_capacity = new_capacity;
    }

    TextCacheEntry *new_entry = &o->text_cache[o->text_cache_count++];
    strncpy(new_entry->text, text, sizeof(new_entry->text) - 1);
    new_entry->text[sizeof(new_entry->text) - 1] = '\0';
    new_entry->color = color;
    new_entry->texture = text_texture;

    return new_entry->texture;
}

/** @brief Helper function to render a texture (static or animated) with alpha modulation
 * It also corrects the aspect ratio of the .png textures.
 *
 * @param renderer The SDL_Renderer to render the texture on.
 * @param texture The SDL_Texture to render.
 * @param anim_texture The AnimatedTexture to render.
 * @param dest The destination rectangle for the texture.
 * @param alpha The alpha value to apply to the texture.
 *
 */
static void render_texture_with_alpha(SDL_Renderer *renderer, SDL_Texture *texture, AnimatedTexture *anim_texture,
                                      const SDL_FRect *dest, Uint8 alpha) {
    SDL_Texture *texture_to_render = nullptr;
    bool is_animated = false;

    if (anim_texture && anim_texture->frame_count > 0) {
        is_animated = true;
        if (anim_texture->delays && anim_texture->total_duration > 0) {
            Uint32 elapsed_time = SDL_GetTicks() % anim_texture->total_duration;
            int current_frame = 0;
            Uint32 time_sum = 0;
            for (int frame_idx = 0; frame_idx < anim_texture->frame_count; ++frame_idx) {
                time_sum += anim_texture->delays[frame_idx];
                if (elapsed_time < time_sum) {
                    current_frame = frame_idx;
                    break;
                }
            }
            texture_to_render = anim_texture->frames[current_frame];
        } else {
            texture_to_render = anim_texture->frames[0];
        }
    } else if (texture) {
        texture_to_render = texture;
    }

    if (texture_to_render) {
        // Reset texture color modulation to white (no tint)
        SDL_SetTextureColorMod(texture_to_render, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture_to_render, alpha);

        // Aspect ratio correction for .png files
        if (!is_animated) {
            // This is a static texture (.png), so we correct its aspect ratio.
            // Animated textures (.gif) are already padded to be square at load time.
            float tex_w, tex_h;
            SDL_GetTextureSize(texture_to_render, &tex_w, &tex_h);

            // Calculate the best scale to fit the texture within the destination box
            float scale_factor = fminf(dest->w / tex_w, dest->h / tex_h);

            // Calculate the new dimensions of the texture
            float scaled_w = tex_w * scale_factor;
            float scaled_h = tex_h * scale_factor;

            // Calculate padding to center the texture inside the destination box
            float pad_x = (dest->w - scaled_w) / 2.0f;
            float pad_y = (dest->h - scaled_h) / 2.0f;

            // Create the final, centered, and correctly scaled destination rectangle
            SDL_FRect final_dest = {dest->x + pad_x, dest->y + pad_y, scaled_w, scaled_h};
            SDL_RenderTexture(renderer, texture_to_render, nullptr, &final_dest);
        } else {
            // For animated textures, render stretched as before, since they are pre-padded.
            SDL_RenderTexture(renderer, texture_to_render, nullptr, dest);
        }

        SDL_SetTextureAlphaMod(texture_to_render, 255); // Reset for other render calls
    }
}


bool overlay_new(Overlay **overlay, const AppSettings *settings) {
    // dereference once and use calloc
    *overlay = (Overlay *) calloc(1, sizeof(Overlay));
    // Check here if calloc failed
    if (*overlay == nullptr) {
        log_message(LOG_ERROR, "[OVERLAY] Error allocating memory for overlay.\n");
        return false;
    }

    // temp variable to not dereference over and over again
    Overlay *o = *overlay;

    // Caches are zero initialized by calloc

    // Create the SDL window and renderer
    if (!overlay_init_sdl(o, settings)) {
        overlay_free(overlay, settings);
        return false;
    }

    o->text_engine = TTF_CreateRendererTextEngine(o->renderer);
    if (!o->text_engine) {
        log_message(LOG_ERROR, "[OVERLAY] Failed to create text engine: %s\n", SDL_GetError());
        overlay_free(overlay, settings);
        return false;
    }

    // Load global background textures using settings and cache
    char full_path[MAX_PATH_LENGTH]; // Buffer for constructing full path

    // Helper lambda to load one background
    auto load_bg = [&](const char *setting_path, const char *default_path,
                       SDL_Texture **tex_target, AnimatedTexture **anim_target) {
        *tex_target = nullptr;
        *anim_target = nullptr;
        snprintf(full_path, sizeof(full_path), "%s/gui/%s", get_resources_path(), setting_path);

        if (strstr(full_path, ".gif")) {
            *anim_target = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
                                                           &o->anim_cache_capacity, full_path, SDL_SCALEMODE_NEAREST);
        } else {
            *tex_target = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                 &o->texture_cache_capacity, full_path, SDL_SCALEMODE_NEAREST);
        }

        // Fallback if loading failed
        if (!*tex_target && !*anim_target) {
            log_message(LOG_ERROR, "[OVERLAY] Failed to load background: %s. Trying default...\n", setting_path);
            snprintf(full_path, sizeof(full_path), "%s/gui/%s", get_resources_path(), default_path);
            if (strstr(full_path, ".gif")) {
                *anim_target = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
                                                               &o->anim_cache_capacity, full_path,
                                                               SDL_SCALEMODE_NEAREST);
            } else {
                *tex_target = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                     &o->texture_cache_capacity, full_path, SDL_SCALEMODE_NEAREST);
            }
        }
    };

    load_bg(settings->adv_bg_path, DEFAULT_ADV_BG_PATH, &o->adv_bg, &o->adv_bg_anim);
    load_bg(settings->adv_bg_half_done_path, DEFAULT_ADV_BG_HALF_DONE_PATH, &o->adv_bg_half_done,
            &o->adv_bg_half_done_anim);
    load_bg(settings->adv_bg_done_path, DEFAULT_ADV_BG_DONE_PATH, &o->adv_bg_done, &o->adv_bg_done_anim);

    if ((!o->adv_bg && !o->adv_bg_anim) ||
        (!o->adv_bg_half_done && !o->adv_bg_half_done_anim) ||
        (!o->adv_bg_done && !o->adv_bg_done_anim)) {
        log_message(LOG_ERROR, "[OVERLAY] CRITICAL: Failed to load default background textures as fallback.\n");
        overlay_free(overlay, settings);
        return false; // Critical failure if defaults also fail
    }

    // Make font HiDPI aware by loading it at a base point size (e.g., 24).
    // SDL_ttf will automatically scale it correctly on any monitor at render time.
    const float base_font_size = DEFAULT_OVERLAY_FONT_SIZE;
    char overlay_font_path[1024];
    snprintf(overlay_font_path, sizeof(overlay_font_path), "%s/fonts/%s", get_resources_path(),
             settings->overlay_font_name);

    if (!path_exists(overlay_font_path)) {
        log_message(
            LOG_ERROR, "[OVERLAY] Tracker/Overlay Font '%s' not found. Falling back to default Minecraft font.\n",
            settings->overlay_font_name);
        snprintf(overlay_font_path, sizeof(overlay_font_path), "%s/fonts/Minecraft.ttf", get_resources_path());
    }

    o->font = TTF_OpenFont(overlay_font_path, base_font_size);
    if (!o->font) {
        log_message(LOG_ERROR, "[OVERLAY] Failed to load font: %s\n", SDL_GetError());
        overlay_free(overlay, settings);
        return false;
    }
    return true;
}


void overlay_events(Overlay *o, SDL_Event *event, bool *is_running, float *deltaTime, AppSettings *settings) {
    (void) o;
    (void) settings;
    (void) deltaTime;
    switch (event->type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: *is_running = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event->key.scancode == SDL_SCANCODE_SPACE) {
                *deltaTime *= OVERLAY_SPEEDUP_FACTOR;
            }
            break;

        // Properly save the window position and size
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_RESIZED: {
            // Get the current window position and size
            SDL_GetWindowPosition(o->window, &settings->overlay_window.x, &settings->overlay_window.y);
            int w, h;
            SDL_GetWindowSize(o->window, &w, &h);

            // Always save the current width and the required fixed height
            settings->overlay_window.w = w;
            settings->overlay_window.h = OVERLAY_FIXED_HEIGHT;
            settings_save(settings, nullptr, SAVE_CONTEXT_OVERLAY_GEOM); // Save the updated settings

            // If the resize event resulted in a different height, force it back.
            // This creates a "sticky" height that can't be changed by the user.
            if (event->type == SDL_EVENT_WINDOW_RESIZED && h != OVERLAY_FIXED_HEIGHT) {
                SDL_SetWindowSize(o->window, w, OVERLAY_FIXED_HEIGHT);
            }
            break;
        }
        default: break;
    }
}

// Helper to check the 'done' status of any displayable item type
static bool is_display_item_done(const OverlayDisplayItem &display_item, const AppSettings *settings) {
    // --- Step 1 & 2: Incorporate the "hidden" check from the new code ---
    bool is_hidden = false;
    bool in_2nd_row = false; // Track if this item is forced to row 2

    switch (display_item.type) {
        case OverlayDisplayItem::ADVANCEMENT:
        case OverlayDisplayItem::STAT: {
            auto *cat = static_cast<TrackableCategory *>(display_item.item_ptr);
            is_hidden = cat->is_hidden;
            in_2nd_row = cat->in_2nd_row;
            break;
        }
        case OverlayDisplayItem::UNLOCK:
        case OverlayDisplayItem::CUSTOM: {
            auto *item = static_cast<TrackableItem *>(display_item.item_ptr);
            is_hidden = item->is_hidden;
            in_2nd_row = item->in_2nd_row;
            break;
        }
        case OverlayDisplayItem::MULTISTAGE: {
            auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
            is_hidden = goal->is_hidden;
            in_2nd_row = goal->in_2nd_row;
            break;
        }
    }

    // If the item is marked as hidden in the template, always hide it from the overlay.
    if (is_hidden) {
        return true;
    }

    // --- Step 3: Use the exact logic from the old code ---
    bool should_hide_when_done;

    // Determine which setting controls the hiding behavior based on the item type
    switch (display_item.type) {
        case OverlayDisplayItem::STAT:
        case OverlayDisplayItem::CUSTOM:
        case OverlayDisplayItem::MULTISTAGE:
            // If forced to Row 2, treat as "always hide" (like Advancements).
            if (in_2nd_row) {
                should_hide_when_done = true;
            } else {
                // Otherwise, respect the Row 3 setting.
                should_hide_when_done = settings->overlay_row3_remove_completed;
            }
            break;

        case OverlayDisplayItem::ADVANCEMENT:
        case OverlayDisplayItem::UNLOCK:
        default:
            // These types belong to Row 2, ALWAYS HIDE THEM
            should_hide_when_done = true;
            break;
    }

    // If hiding is disabled for this item's row, it's never considered "done" for removal purposes.
    if (!should_hide_when_done) return false;

    // If hiding is enabled, check the actual completion status of the item
    switch (display_item.type) {
        case OverlayDisplayItem::ADVANCEMENT: {
            auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
            return adv->done;
        }
        case OverlayDisplayItem::UNLOCK:
            return static_cast<TrackableItem *>(display_item.item_ptr)->done;
        case OverlayDisplayItem::STAT: {
            auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
            // Hide legacy helper stats
            MC_Version version = settings_get_version_from_string(settings->version_str);
            bool is_hidden_legacy = (version <= MC_VERSION_1_6_4 && stat->is_single_stat_category && stat->criteria[0]->
                                     goal <= 0);
            return stat->done || is_hidden_legacy;
        }
        case OverlayDisplayItem::CUSTOM:
            return static_cast<TrackableItem *>(display_item.item_ptr)->done;
        case OverlayDisplayItem::MULTISTAGE: {
            auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
            return goal->current_stage >= goal->stage_count - 1;
        }
    }
    return true;
}

void overlay_update(Overlay *o, float *deltaTime, const Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data) return;

    // Store the current delta time so we can display it in the render function.
    o->last_delta_time = *deltaTime;

    // --- Gather Items for Each Row ---
    std::vector<std::pair<TrackableItem *, TrackableCategory *> > row1_items;
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *cat = t->template_data->advancements[i];
        for (int j = 0; j < cat->criteria_count; j++) row1_items.push_back({cat->criteria[j], cat});
    }

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *cat = t->template_data->stats[i];

        // If the stat category is a simple stat (defined without a "criteria" block in the template),
        // do not add its auto-generated criterion to Row 1.
        if (cat->is_single_stat_category) {
            continue;
        }

        // For complex stats, add all of their defined criteria to Row 1.
        for (int j = 0; j < cat->criteria_count; j++) {
            row1_items.push_back({cat->criteria[j], cat});
        }
    }

    std::vector<OverlayDisplayItem> row2_items; // Advancements & Unlocks
    for (int i = 0; i < t->template_data->advancement_count; ++i)
        row2_items.push_back({
            t->template_data->advancements[i], OverlayDisplayItem::ADVANCEMENT
        });
    for (int i = 0; i < t->template_data->unlock_count; ++i)
        row2_items.push_back({
            t->template_data->unlocks[i], OverlayDisplayItem::UNLOCK
        });

    // Add forced items to Row 2 (custom goals, ms goals, and stats can be forced using "in_2nd_row")
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *cat = t->template_data->stats[i];
        if (cat->in_2nd_row) {
            // Skip hidden helper stats even if forced (shouldn't happen but safe to check)
            if (cat->is_single_stat_category && cat->criteria_count > 0 && cat->criteria[0]->goal <= 0 &&
                cat->icon_path[0] == '\0') {
                continue;
            }
            row2_items.push_back({cat, OverlayDisplayItem::STAT});
        }
    }
    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        TrackableItem *item = t->template_data->custom_goals[i];
        if (item->in_2nd_row) {
            // Add to row 2
            row2_items.push_back({item, OverlayDisplayItem::CUSTOM});
        }
    }
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        if (goal->in_2nd_row) {
            // Add to row 2
            row2_items.push_back({goal, OverlayDisplayItem::MULTISTAGE});
        }
    }

    // --- Update Animation State ---
    const float base_scroll_speed = 60.0f;
    float speed_multiplier = settings->overlay_scroll_speed;
    // Speedup from holding SPACE is handled in overlay_events directly, mutliplying by deltatime
    float scroll_delta = -(base_scroll_speed * speed_multiplier * (*deltaTime));


    // --- Row 1 Update Logic (Dynamic Width) ---
    if (!row1_items.empty()) {
        // scroll_delta is negative by default. Adding it makes the offset
        // increasingly negative, which moves items from Right to Left.
        // Do NOT reset the offset to 0. Let it accumulate.
        // The render function handles the wrapping via fmod().
        o->scroll_offset_row1 -= scroll_delta;
    } else {
        o->scroll_offset_row1 = 0;
    }

    // --- Row 2 Update Logic (Dynamic Width) ---
    if (!row2_items.empty()) {
        // Unified Logic: Just accumulate the scroll delta.
        // We no longer need to calculate widths or swap indices here.
        // The Block-Based renderer handles the wrapping automatically.
        o->scroll_offset_row2 -= scroll_delta;
    } else {
        o->scroll_offset_row2 = 0;
    }

    // Row 3 doesn't disappear by default (only with setting)
    o->scroll_offset_row3 -= scroll_delta;

    // The timing logic for complex stat categories lives in overlay_render().


    // --- Cycle through social media text ---
    o->social_media_timer += *deltaTime;
    if (o->social_media_timer >= SOCIAL_CYCLE_SECONDS) {
        o->social_media_timer -= SOCIAL_CYCLE_SECONDS;
        o->current_social_index = (o->current_social_index + 1) % NUM_SOCIALS;
    }
}

void overlay_render(Overlay *o, const Tracker *t, const AppSettings *settings) {
    SDL_SetRenderDrawColor(o->renderer, settings->overlay_bg_color.r, settings->overlay_bg_color.g,
                           settings->overlay_bg_color.b, settings->overlay_bg_color.a);
    SDL_RenderClear(o->renderer);

    // Get version
    MC_Version version = settings_get_version_from_string(settings->version_str);

    // Render Progress Text (Top Bar)
    if (t && t->template_data) {
        char info_buffer[512] = {0};
        char final_buffer[1024];
        char temp_chunk[256];
        bool first_item_added = false;

        auto add_component = [&](const char *component_str) {
            if (first_item_added) {
                strcat(info_buffer, " | ");
            }
            strcat(info_buffer, component_str);
            first_item_added = true;
        };

        // Check if the run is 100% complete
        bool is_run_complete = t->template_data->advancements_completed_count >= t->template_data->advancement_count &&
                               t->template_data->overall_progress_percentage >= 100.0f;

        if (is_run_complete) {
            char formatted_time[64];
            format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));
            snprintf(info_buffer, sizeof(info_buffer),
                     "*** RUN COMPLETE! *** | Final Time: %s | Donate (mentioning 'Advancely') to be featured!",
                     formatted_time);
        } else {
            // Conditionally build the progress string section by section
            if (settings->overlay_show_world && t->world_name[0] != '\0') {
                add_component(t->world_name);
            }

            if (settings->overlay_show_run_details) {
                if (settings->category_display_name[0] != '\0') {
                    snprintf(temp_chunk, sizeof(temp_chunk), "%s - %s", // Version - Category
                             settings->display_version_str,
                             settings->category_display_name);
                } else {
                    snprintf(temp_chunk, sizeof(temp_chunk), "%s", // Display Category Empty
                             settings->display_version_str);
                }
                add_component(temp_chunk);
            }

            // Show progress sections if they have something
            if (settings->overlay_show_progress) {
                bool show_adv_counter = (t->template_data->advancement_goal_count > 0);
                bool show_prog_percent = (t->template_data->total_progress_steps > 0);
                const char *adv_ach_label = (version >= MC_VERSION_1_12) ? "Adv" : "Ach";

                if (show_adv_counter && show_prog_percent) {
                    snprintf(temp_chunk, sizeof(temp_chunk), "%s: %d/%d - Prog: %.2f%%",
                             adv_ach_label, t->template_data->advancements_completed_count,
                             t->template_data->advancement_goal_count, t->template_data->overall_progress_percentage);
                    add_component(temp_chunk);
                } else if (show_adv_counter) {
                    snprintf(temp_chunk, sizeof(temp_chunk), "%s: %d/%d",
                             adv_ach_label, t->template_data->advancements_completed_count,
                             t->template_data->advancement_goal_count);
                    add_component(temp_chunk);
                } else if (show_prog_percent) {
                    snprintf(temp_chunk, sizeof(temp_chunk), "Prog: %.2f%%",
                             t->template_data->overall_progress_percentage);
                    add_component(temp_chunk);
                }
            }

            if (settings->overlay_show_igt) {
                char formatted_time[64];
                format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));
                snprintf(temp_chunk, sizeof(temp_chunk), "%s IGT", formatted_time);
                add_component(temp_chunk);
            }

            if (settings->overlay_show_update_timer) {
                char formatted_update_time[64];
                float last_update_time_5_seconds = floorf(t->time_since_last_update / 5.0f) * 5.0f;
                format_time_since_update(last_update_time_5_seconds, formatted_update_time,
                                         sizeof(formatted_update_time));
                snprintf(temp_chunk, sizeof(temp_chunk), "Upd: %s", formatted_update_time);
                add_component(temp_chunk);
            }
        }

        // Always append the rotating social media text to the prepared message
        if (info_buffer[0] != '\0') {
            snprintf(final_buffer, sizeof(final_buffer), "%s | %s", info_buffer, SOCIALS[o->current_social_index]);
        } else {
            // If all sections are turned off, just show the socials
            strncpy(final_buffer, SOCIALS[o->current_social_index], sizeof(final_buffer) - 1);
            final_buffer[sizeof(final_buffer) - 1] = '\0';
        }


        SDL_Color text_color = {
            settings->overlay_text_color.r, settings->overlay_text_color.g, settings->overlay_text_color.b,
            settings->overlay_text_color.a
        };

        // Use text cache for top info bar
        SDL_Texture *text_texture = get_text_texture_from_cache(o, final_buffer, text_color);
        if (text_texture) {
            float w, h;
            SDL_GetTextureSize(text_texture, &w, &h);
            int overlay_w;
            SDL_GetWindowSize(o->window, &overlay_w, nullptr);
            const float padding = 10.0f;
            SDL_FRect dest_rect = {padding, padding, w, h};
            if (settings->overlay_progress_text_align == OVERLAY_PROGRESS_TEXT_ALIGN_CENTER)
                dest_rect.x = ((float) overlay_w - w) / 2.0f;
            else if (settings->overlay_progress_text_align == OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT)
                dest_rect.x = (float) overlay_w - w - padding;
            SDL_RenderTexture(o->renderer, text_texture, nullptr, &dest_rect);
        }
    }

    if (!t || !t->template_data) {
        SDL_RenderPresent(o->renderer);
        return;
    }

    int window_w;
    SDL_GetWindowSizeInPixels(o->window, &window_w, nullptr);
    SDL_Color text_color = {
        settings->overlay_text_color.r, settings->overlay_text_color.g, settings->overlay_text_color.b, 255
    };

    // --- ROW 1: Criteria & Sub-stats Icons ---
    {
        const float ROW1_Y_POS = 48.0f;
        const float ROW1_ICON_SIZE = 48.0f;
        const float ROW1_SHARED_ICON_SIZE = settings->overlay_row1_shared_icon_size; // Originally 30.0f
        const float item_full_width = ROW1_ICON_SIZE + settings->overlay_row1_spacing;

        // Gather items
        std::vector<std::pair<TrackableItem *, TrackableCategory *> > row1_items;
        for (int i = 0; i < t->template_data->advancement_count; i++) {
            TrackableCategory *cat = t->template_data->advancements[i];
            for (int j = 0; j < cat->criteria_count; j++) {
                row1_items.push_back({cat->criteria[j], cat});
            }
        }

        for (int i = 0; i < t->template_data->stat_count; i++) {
            TrackableCategory *cat = t->template_data->stats[i];
            if (cat->is_single_stat_category) continue;
            for (int j = 0; j < cat->criteria_count; j++) {
                row1_items.push_back({cat->criteria[j], cat});
            }
        }

        // Count visible items
        size_t visible_item_count = 0;
        for (const auto &pair: row1_items) {
            if (!pair.first->done && !pair.second->is_hidden && !pair.first->is_hidden) {
                visible_item_count++;
            }
        }

        if (visible_item_count > 0 && item_full_width > 0) {
            // --- Block-Based Rendering ---
            float total_row_width = visible_item_count * item_full_width;

            // fmod allows the infinite scrolling
            float start_pos = fmod(o->scroll_offset_row1, total_row_width);

            // Calculate blocks needed to cover screen + buffer
            int blocks_to_draw = (int) ceil((float) window_w / total_row_width) + 2;

            for (int block = -blocks_to_draw; block <= blocks_to_draw; ++block) {
                float block_offset = start_pos + (block * total_row_width);
                int visible_item_index = 0;

                for (const auto &pair: row1_items) {
                    TrackableItem *item_to_render = pair.first;
                    TrackableCategory *parent = pair.second;

                    // Skip hidden/done items
                    if (item_to_render->done || parent->is_hidden || item_to_render->is_hidden) continue;

                    // Calculate absolute position
                    float x_pos;
                    if (settings->overlay_scroll_speed > 0) {
                        // Positive Scroll (L->R): Anchor to the Right (End)
                        // This puts Index 0 on the Right (Leading), and Index N on the Left (Trailing).
                        // This ensures Item 0 appears first.
                        x_pos = block_offset - (visible_item_index * item_full_width);
                    } else {
                        // Negative Scroll (R->L): Anchor to the Left (Start) - Default Behavior
                        x_pos = block_offset + (visible_item_index * item_full_width);
                    }

                    // --- Simple Culling ---
                    // Icons are uniform size, so simple culling is safe here
                    if (x_pos + item_full_width < 0 || x_pos > window_w) {
                        visible_item_index++;
                        continue;
                    }

                    // --- Render Icon ---
                    SDL_FRect dest_rect = {x_pos, ROW1_Y_POS, ROW1_ICON_SIZE, ROW1_ICON_SIZE};

                    SDL_Texture *tex = nullptr;
                    AnimatedTexture *anim_tex = nullptr;
                    if (strstr(item_to_render->icon_path, ".gif")) {
                        anim_tex = get_animated_texture_from_cache(
                            o->renderer, &o->anim_cache, &o->anim_cache_count, &o->anim_cache_capacity,
                            item_to_render->icon_path, SDL_SCALEMODE_NEAREST);
                    } else {
                        tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                     &o->texture_cache_capacity, item_to_render->icon_path,
                                                     SDL_SCALEMODE_NEAREST);
                    }

                    if (!tex && !anim_tex) {
                        SDL_SetRenderDrawColor(o->renderer, 255, 0, 255, 100);
                        SDL_RenderFillRect(o->renderer, &dest_rect);
                    } else {
                        render_texture_with_alpha(o->renderer, tex, anim_tex, &dest_rect, 255);
                    }

                    // --- Render Shared Parent Icon Overlay ---
                    if (item_to_render->is_shared && parent) {
                        SDL_Texture *parent_tex = nullptr;
                        AnimatedTexture *parent_anim_tex = nullptr;
                        if (strstr(parent->icon_path, ".gif")) {
                            parent_anim_tex = get_animated_texture_from_cache(
                                o->renderer, &o->anim_cache, &o->anim_cache_count, &o->anim_cache_capacity,
                                parent->icon_path, SDL_SCALEMODE_NEAREST);
                        } else {
                            parent_tex = get_texture_from_cache(o->renderer, &o->texture_cache,
                                                                &o->texture_cache_count,
                                                                &o->texture_cache_capacity, parent->icon_path,
                                                                SDL_SCALEMODE_NEAREST);
                        }

                        SDL_FRect shared_dest_rect = {
                            x_pos, ROW1_Y_POS, ROW1_SHARED_ICON_SIZE, ROW1_SHARED_ICON_SIZE
                        };
                        render_texture_with_alpha(o->renderer, parent_tex, parent_anim_tex, &shared_dest_rect, 255);
                    }

                    visible_item_index++;
                }
            }
        }
    }

    // --- ROW 2: Advancements & Unlocks (AND forced items) ---
    // ROW 2 ALSO SHOWS SUPPORTERS WHEN RUN IS COMPLETED
    {
        const float ROW2_Y_POS = 108.0f;
        const float ITEM_WIDTH = 96.0f; // Minimum Width based on icon bg
        const float ITEM_SPACING = 16.0f;
        const float TEXT_Y_OFFSET = 4.0f;

        // Static variables to store the randomized supporter list and track completion state
        static std::vector<SupporterRenderInfo> static_supporter_render_list;
        static bool run_was_complete_last_frame = false;

        bool is_run_complete = t->template_data->advancements_completed_count >= t->template_data->
                               advancement_goal_count &&
                               t->template_data->overall_progress_percentage >= 100.0f;

        // If the run has just been completed, generate the randomized list ONCE.
        if (is_run_complete && !run_was_complete_last_frame) {
            static_supporter_render_list.clear(); // Clear any previous run's data

            // 1. Create a sortable list of pointers to the original supporters.
            std::vector<const Supporter *> sorted_supporters;
            for (int i = 0; i < NUM_SUPPORTERS; ++i) {
                sorted_supporters.push_back(&SUPPORTERS[i]);
            }

            // 2. Sort the pointers in descending order based on donation amount.
            std::sort(sorted_supporters.begin(), sorted_supporters.end(), [](const Supporter *a, const Supporter *b) {
                return a->amount > b->amount;
            });

            // 3. Calculate the index cutoffs for the top and middle thirds.
            const int top_third_count = (NUM_SUPPORTERS + 2) / 3; // Ensures a fair split, e.g., 5 supporters -> top 2
            const int middle_third_count = (NUM_SUPPORTERS * 2 + 2) / 3;

            // Prepare the persistent render info list
            for (int i = 0; i < NUM_SUPPORTERS; ++i) {
                SupporterRenderInfo info = {};
                info.supporter = sorted_supporters[i];
                // Assign a RANDOM icon and store it
                info.icon_path = SUPPORTER_ICONS[rand() % NUM_SUPPORTER_ICONS];

                // 4. Assign background texture based on the supporter's rank.
                if (i < top_third_count) {
                    info.background_static = o->adv_bg_done;
                    info.background_anim = o->adv_bg_done_anim;
                } else if (i < middle_third_count) {
                    info.background_static = o->adv_bg_half_done;
                    info.background_anim = o->adv_bg_half_done_anim;
                } else {
                    info.background_static = o->adv_bg;
                    info.background_anim = o->adv_bg_anim;
                }
                static_supporter_render_list.push_back(info);
            }
        }

        if (is_run_complete && NUM_SUPPORTERS > 0) {
            // Completed Run: Render Supporter Showcase
            float max_text_width = 0.0f;

            // First pass: Prepare render info and calculate max width from the static list
            for (const auto &render_info: static_supporter_render_list) {
                // Measure text widths for layout calculation
                char amount_buf[64];
                snprintf(amount_buf, sizeof(amount_buf), "$%.2f", render_info.supporter->amount);
                int name_w = 0, amount_w = 0;
                TTF_MeasureString(o->font, render_info.supporter->name, 0, 0, &name_w, nullptr);
                TTF_MeasureString(o->font, amount_buf, 0, 0, &amount_w, nullptr);
                max_text_width = fmaxf(max_text_width, (float) fmax(name_w, amount_w));
            }

            const float cell_width = fmaxf(ITEM_WIDTH, max_text_width);
            const float item_full_width = cell_width + ITEM_SPACING;

            float total_row_width = NUM_SUPPORTERS * item_full_width;
            float start_pos = fmod(o->scroll_offset_row3, total_row_width); // Sync with row 3's speed
            int blocks_to_draw = (total_row_width > 0) ? (int) ceil((float) window_w / total_row_width) + 2 : 0;

            for (int block = -blocks_to_draw; block <= blocks_to_draw; ++block) {
                float block_offset = start_pos + (block * total_row_width);
                for (size_t i = 0; i < static_supporter_render_list.size(); ++i) {
                    float current_x = block_offset + (i * item_full_width);
                    if (current_x + item_full_width < 0 || current_x > window_w) continue;

                    const auto &render_info = static_supporter_render_list[i];

                    // Render background
                    float bg_x_offset = (cell_width - ITEM_WIDTH) / 2.0f;
                    SDL_FRect bg_rect = {current_x + bg_x_offset, ROW2_Y_POS, ITEM_WIDTH, ITEM_WIDTH};
                    render_texture_with_alpha(o->renderer, render_info.background_static, render_info.background_anim,
                                              &bg_rect, 255);

                    // Render icon
                    SDL_FRect icon_rect = {bg_rect.x + 16.0f, bg_rect.y + 16.0f, 64.0f, 64.0f};

                    // Also support .gif icons
                    SDL_Texture *tex = nullptr;
                    AnimatedTexture *anim_tex = nullptr;
                    char full_icon_path[MAX_PATH_LENGTH];
                    snprintf(full_icon_path, sizeof(full_icon_path), "%s/icons/%s", get_resources_path(),
                             render_info.icon_path);

                    // Check the file extension to decide which cache function to use
                    if (strstr(full_icon_path, ".gif")) {
                        anim_tex = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
                                                                   &o->anim_cache_capacity, full_icon_path,
                                                                   SDL_SCALEMODE_NEAREST);
                    } else {
                        tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                     &o->texture_cache_capacity, full_icon_path, SDL_SCALEMODE_NEAREST);
                    }

                    if (tex || anim_tex) {
                        // Pass both pointers; the function will correctly choose which one to use
                        render_texture_with_alpha(o->renderer, tex, anim_tex, &icon_rect, 255);
                    } else {
                        // If texture loading fails for any reason, draw a placeholder
                        SDL_SetRenderDrawColor(o->renderer, 255, 0, 255, 255); // Bright Pink
                        SDL_RenderFillRect(o->renderer, &icon_rect);
                    }

                    // Render name
                    SDL_Texture *name_tex = get_text_texture_from_cache(o, render_info.supporter->name, text_color);
                    if (name_tex) {
                        float w, h;
                        SDL_GetTextureSize(name_tex, &w, &h);
                        float text_x = current_x + (cell_width - w) / 2.0f;
                        SDL_FRect dest_rect = {text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET, w, h};
                        SDL_RenderTexture(o->renderer, name_tex, nullptr, &dest_rect);

                        // Render amount
                        char amount_buf[64];
                        snprintf(amount_buf, sizeof(amount_buf), "$%.2f", render_info.supporter->amount);
                        SDL_Texture *amount_tex = get_text_texture_from_cache(o, amount_buf, text_color);
                        if (amount_tex) {
                            float pw, ph;
                            SDL_GetTextureSize(amount_tex, &pw, &ph);
                            float p_text_x = current_x + (cell_width - pw) / 2.0f;
                            SDL_FRect p_dest_rect = {p_text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + h, pw, ph};
                            SDL_RenderTexture(o->renderer, amount_tex, nullptr, &p_dest_rect);
                        }
                    }
                }
            }
        } else {
            // If run ISN'T complete
            // --- Default Behavior: Render Advancements & Unlocks ---
            SDL_Texture *static_bg = nullptr;
            AnimatedTexture *anim_bg = nullptr;

            std::vector<OverlayDisplayItem> row2_items;
            for (int i = 0; i < t->template_data->advancement_count; ++i)
                row2_items.push_back({
                    t->template_data->advancements[i], OverlayDisplayItem::ADVANCEMENT
                });
            for (int i = 0; i < t->template_data->unlock_count; ++i)
                row2_items.push_back({
                    t->template_data->unlocks[i], OverlayDisplayItem::UNLOCK
                });

            // Add forced items to Row 2
            for (int i = 0; i < t->template_data->stat_count; i++) {
                TrackableCategory *cat = t->template_data->stats[i];
                if (cat->in_2nd_row) {
                    if (cat->is_single_stat_category && cat->criteria_count > 0 && cat->criteria[0]->goal <= 0 &&
                        cat->icon_path[0] == '\0') {
                        continue;
                    }
                    row2_items.push_back({cat, OverlayDisplayItem::STAT});
                }
            }
            for (int i = 0; i < t->template_data->custom_goal_count; i++) {
                TrackableItem *item = t->template_data->custom_goals[i];
                if (item->in_2nd_row) {
                    row2_items.push_back({item, OverlayDisplayItem::CUSTOM});
                }
            }
            for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
                MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
                if (goal->in_2nd_row) {
                    row2_items.push_back({goal, OverlayDisplayItem::MULTISTAGE});
                }
            }

            // --- 1. Calculate Max Text Width ---
            float max_text_width_row2 = 0.0f;
            for (const auto &display_item: row2_items) {
                // Skip template-hidden items
                bool is_template_hidden = false;
                if (display_item.type == OverlayDisplayItem::ADVANCEMENT)
                    is_template_hidden = static_cast<TrackableCategory *>(display_item.item_ptr)->is_hidden;
                else if (display_item.type == OverlayDisplayItem::UNLOCK)
                    is_template_hidden = static_cast<TrackableItem *>(display_item.item_ptr)->is_hidden;
                    // Check hidden status for forced types
                else if (display_item.type == OverlayDisplayItem::STAT)
                    is_template_hidden = static_cast<TrackableCategory *>(display_item.item_ptr)->is_hidden;
                else if (display_item.type == OverlayDisplayItem::CUSTOM)
                    is_template_hidden = static_cast<TrackableItem *>(display_item.item_ptr)->is_hidden;
                else if (display_item.type == OverlayDisplayItem::MULTISTAGE)
                    is_template_hidden = static_cast<MultiStageGoal *>(display_item.item_ptr)->is_hidden;

                if (is_template_hidden) continue;

                char name_buf[256] = {0};
                char potential_progress_buf[64] = {0};
                char longest_criterion_buf[256] = {0};
                int w_name = 0, w_progress = 0, w_criterion = 0;

                if (display_item.type == OverlayDisplayItem::ADVANCEMENT) {
                    auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
                    strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);

                    if (adv->criteria_count > 0) {
                        snprintf(potential_progress_buf, sizeof(potential_progress_buf), "(%d / %d)",
                                 adv->criteria_count, adv->criteria_count);
                        TTF_MeasureString(o->font, potential_progress_buf, 0, 0, &w_progress, nullptr);

                        for (int j = 0; j < adv->criteria_count; ++j) {
                            if (adv->criteria[j] && strlen(adv->criteria[j]->display_name) > strlen(
                                    longest_criterion_buf)) {
                                strncpy(longest_criterion_buf, adv->criteria[j]->display_name,
                                        sizeof(longest_criterion_buf) - 1);
                                longest_criterion_buf[sizeof(longest_criterion_buf) - 1] = '\0';
                            }
                        }
                        if (longest_criterion_buf[0] != '\0') {
                            TTF_MeasureString(o->font, longest_criterion_buf, 0, 0, &w_criterion, nullptr);
                        }
                    }
                } else if (display_item.type == OverlayDisplayItem::UNLOCK) {
                    auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
                    strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                }

                // Handle width calculation for forced items (same logic as Row 3)
                else if (display_item.type == OverlayDisplayItem::STAT) {
                    auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
                    strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);

                    if (!stat->is_single_stat_category) {
                        // Complex stat (even if one sub-stat)
                        snprintf(name_buf, sizeof(name_buf), "%s (%d / %d)", stat->display_name,
                                 stat->completed_criteria_count, stat->criteria_count);
                        TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);

                        for (int j = 0; j < stat->criteria_count; ++j) {
                            TrackableItem *crit = stat->criteria[j];
                            char temp_sub_stat_buf[256] = {0};
                            if (crit->goal > 0) {
                                snprintf(temp_sub_stat_buf, sizeof(temp_sub_stat_buf), "%d. %s (%d / %d)", j + 1,
                                         crit->display_name, crit->goal, crit->goal);
                            } else if (crit->goal == -1) {
                                snprintf(temp_sub_stat_buf, sizeof(temp_sub_stat_buf), "%d. %s (999)", j + 1,
                                         crit->display_name);
                            }
                            if (strlen(temp_sub_stat_buf) > strlen(longest_criterion_buf)) {
                                // Reuse longest_criterion_buf
                                strcpy(longest_criterion_buf, temp_sub_stat_buf);
                            }
                        }
                    } else if (stat->criteria_count == 1) {
                        TrackableItem *crit = stat->criteria[0];
                        if (crit->goal > 0) {
                            snprintf(potential_progress_buf, sizeof(potential_progress_buf), "(%d / %d)", crit->goal,
                                     crit->goal);
                        } else if (crit->goal == -1) {
                            snprintf(potential_progress_buf, sizeof(potential_progress_buf), "(999)");
                        }
                    }
                } else if (display_item.type == OverlayDisplayItem::CUSTOM) {
                    auto *goal = static_cast<TrackableItem *>(display_item.item_ptr);
                    strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    if (goal->goal > 0) {
                        snprintf(potential_progress_buf, sizeof(potential_progress_buf), "(%d / %d)", goal->goal,
                                 goal->goal);
                    } else if (goal->goal == -1) {
                        snprintf(potential_progress_buf, sizeof(potential_progress_buf), "(999)");
                    }
                } else if (display_item.type == OverlayDisplayItem::MULTISTAGE) {
                    auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
                    strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    for (int j = 0; j < goal->stage_count; ++j) {
                        SubGoal *stage = goal->stages[j];
                        char temp_stage_buf[256];
                        if (stage->type == SUBGOAL_STAT && stage->required_progress > 0) {
                            snprintf(temp_stage_buf, sizeof(temp_stage_buf), "%s (%d/%d)", stage->display_text,
                                     stage->required_progress, stage->required_progress);
                        } else {
                            strncpy(temp_stage_buf, stage->display_text, sizeof(temp_stage_buf) - 1);
                            temp_stage_buf[sizeof(temp_stage_buf) - 1] = '\0';
                        }
                        if (strlen(temp_stage_buf) > strlen(longest_criterion_buf)) {
                            strcpy(longest_criterion_buf, temp_stage_buf);
                        }
                    }
                }

                if (potential_progress_buf[0] != '\0')
                    TTF_MeasureString(
                        o->font, potential_progress_buf, 0, 0, &w_progress, nullptr);
                if (longest_criterion_buf[0] != '\0')
                    TTF_MeasureString(o->font, longest_criterion_buf, 0, 0,
                                      &w_criterion, nullptr);

                float item_max_text_width = fmaxf((float) w_name, fmaxf((float) w_progress, (float) w_criterion));
                max_text_width_row2 = fmaxf(max_text_width_row2, item_max_text_width);
            }

            // --- 2. Apply Spacing Settings ---
            float cell_width_row2;
            float item_full_width_row2;

            if (settings->overlay_row2_custom_spacing_enabled) {
                item_full_width_row2 = settings->overlay_row2_custom_spacing;
                cell_width_row2 = item_full_width_row2 - ITEM_SPACING;
            } else {
                cell_width_row2 = fmaxf(ITEM_WIDTH, max_text_width_row2);
                item_full_width_row2 = cell_width_row2 + ITEM_SPACING;
            }
            o->calculated_row2_item_width = item_full_width_row2;

            size_t visible_item_count = 0;
            for (const auto &item: row2_items) {
                if (!is_display_item_done(item, settings)) visible_item_count++;
            }

            if (visible_item_count > 0) {
                // --- 3. Block-Based Render Loop ---
                float total_row_width = visible_item_count * item_full_width_row2;

                // Use raw scroll offset (no negation) to match Row 1 and 3 direction
                float start_pos = fmod(o->scroll_offset_row2, total_row_width);

                // Calculate blocks to draw, adding buffer for wide text bleeding in
                int blocks_to_draw = (total_row_width > 0) ? (int) ceil((float) window_w / total_row_width) + 2 : 0;
                if (total_row_width > 0 && max_text_width_row2 > total_row_width) {
                    blocks_to_draw += (int) ceilf(max_text_width_row2 / total_row_width);
                }

                for (int block = -blocks_to_draw; block <= blocks_to_draw; ++block) {
                    float block_offset = start_pos + (block * total_row_width);
                    int visible_item_index = 0;

                    for (const auto &display_item: row2_items) {
                        if (is_display_item_done(display_item, settings)) continue;

                        // --- Directional Gap Filling ---
                        float current_x;
                        if (settings->overlay_scroll_speed > 0) {
                            // Positive Scroll (L->R): Anchor to the Right
                            current_x = block_offset - (visible_item_index * item_full_width_row2);
                        } else {
                            // Negative Scroll (R->L): Anchor to the Left
                            current_x = block_offset + (visible_item_index * item_full_width_row2);
                        }

                        // --- Dynamic Culling ---
                        float bg_x_offset = (cell_width_row2 - ITEM_WIDTH) / 2.0f;
                        float icon_visual_x = current_x + bg_x_offset;
                        float dynamic_cull_margin = max_text_width_row2 + 50.0f;

                        if (icon_visual_x + ITEM_WIDTH + dynamic_cull_margin < 0 || icon_visual_x - dynamic_cull_margin
                            > window_w) {
                            visible_item_index++;
                            continue;
                        }

                        // --- Render Logic ---
                        std::string icon_path;
                        char name_buf[256] = {0};
                        char progress_buf[256] = {0};

                        switch (display_item.type) {
                            case OverlayDisplayItem::ADVANCEMENT: {
                                auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
                                static_bg = o->adv_bg;
                                anim_bg = o->adv_bg_anim;
                                if (adv->done) {
                                    static_bg = o->adv_bg_done;
                                    anim_bg = o->adv_bg_done_anim;
                                } else if (adv->completed_criteria_count > 0) {
                                    static_bg = o->adv_bg_half_done;
                                    anim_bg = o->adv_bg_half_done_anim;
                                }
                                icon_path = adv->icon_path;
                                strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                                name_buf[sizeof(name_buf) - 1] = '\0';

                                if (adv->criteria_count > 0 && adv->completed_criteria_count == adv->criteria_count -
                                    1) {
                                    for (int j = 0; j < adv->criteria_count; ++j) {
                                        if (!adv->criteria[j]->done) {
                                            strncpy(progress_buf, adv->criteria[j]->display_name,
                                                    sizeof(progress_buf) - 1);
                                            progress_buf[sizeof(progress_buf) - 1] = '\0';
                                            break;
                                        }
                                    }
                                } else if (adv->criteria_count > 0) {
                                    snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                             adv->completed_criteria_count,
                                             adv->criteria_count);
                                }
                                break;
                            }
                            case OverlayDisplayItem::UNLOCK: {
                                auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
                                static_bg = o->adv_bg;
                                anim_bg = o->adv_bg_anim;
                                if (unlock->done) {
                                    static_bg = o->adv_bg_done;
                                    anim_bg = o->adv_bg_done_anim;
                                }
                                icon_path = unlock->icon_path;
                                strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
                                name_buf[sizeof(name_buf) - 1] = '\0';
                                break;
                            }
                            // Render logic for forced items (same as Row 3)
                            case OverlayDisplayItem::STAT: {
                                auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
                                static_bg = o->adv_bg;
                                anim_bg = o->adv_bg_anim;
                                if (stat->done) {
                                    static_bg = o->adv_bg_done;
                                    anim_bg = o->adv_bg_done_anim;
                                } else if ((!stat->is_single_stat_category && stat->completed_criteria_count > 0) ||
                                           (stat->is_single_stat_category && stat->criteria_count > 0 && stat->criteria[
                                                0]->progress > 0)) {
                                    static_bg = o->adv_bg_half_done;
                                    anim_bg = o->adv_bg_half_done_anim;
                                }
                                icon_path = stat->icon_path;

                                if (!stat->is_single_stat_category) {
                                    // If complex stat it cycles (even if just one sub-stat)
                                    // Multi-stat / Complex Stat Logic
                                    snprintf(name_buf, sizeof(name_buf), "%s (%d / %d)", stat->display_name,
                                             stat->completed_criteria_count, stat->criteria_count);
                                    // Cycle logic for multi-stat
                                    std::vector<int> incomplete_indices;
                                    for (int j = 0; j < stat->criteria_count; ++j) {
                                        if (!stat->criteria[j]->done && !stat->criteria[j]->is_hidden) {
                                            incomplete_indices.push_back(j);
                                        }
                                    }
                                    if (!incomplete_indices.empty()) {
                                        int cycle_duration_ms = (int) (settings->overlay_stat_cycle_speed * 1000.0f);
                                        if (cycle_duration_ms <= 0) cycle_duration_ms = 1000;
                                        Uint32 current_ticks = SDL_GetTicks();
                                        int num_incomplete = incomplete_indices.size();
                                        int list_index_to_show = (current_ticks / cycle_duration_ms) % num_incomplete;
                                        int original_crit_index = incomplete_indices[list_index_to_show];
                                        TrackableItem *crit = stat->criteria[original_crit_index];
                                        if (crit->goal > 0) {
                                            snprintf(progress_buf, sizeof(progress_buf), "%d. %s (%d / %d)",
                                                     original_crit_index + 1, crit->display_name, crit->progress,
                                                     crit->goal);
                                        } else if (crit->goal == -1) {
                                            snprintf(progress_buf, sizeof(progress_buf), "%d. %s (%d)",
                                                     original_crit_index + 1, crit->display_name, crit->progress);
                                        }
                                    }
                                } else {
                                    strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
                                    name_buf[sizeof(name_buf) - 1] = '\0';
                                    if (stat->criteria_count == 1) {
                                        TrackableItem *crit = stat->criteria[0];
                                        if (crit->goal > 0)
                                            snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                                     crit->progress, crit->goal);
                                        else if (crit->goal == -1)
                                            snprintf(
                                                progress_buf, sizeof(progress_buf), "(%d)", crit->progress);
                                    }
                                }
                                break;
                            }
                            case OverlayDisplayItem::CUSTOM: {
                                auto *goal = static_cast<TrackableItem *>(display_item.item_ptr);
                                static_bg = o->adv_bg;
                                anim_bg = o->adv_bg_anim;
                                if (goal->done) {
                                    static_bg = o->adv_bg_done;
                                    anim_bg = o->adv_bg_done_anim;
                                } else if (goal->progress > 0) {
                                    static_bg = o->adv_bg_half_done;
                                    anim_bg = o->adv_bg_half_done_anim;
                                }
                                icon_path = goal->icon_path;
                                strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                                name_buf[sizeof(name_buf) - 1] = '\0';
                                if (goal->goal > 0)
                                    snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                             goal->progress, goal->goal);
                                else if (goal->goal == -1 && !goal->done)
                                    snprintf(
                                        progress_buf, sizeof(progress_buf), "(%d)", goal->progress);
                                break;
                            }
                            case OverlayDisplayItem::MULTISTAGE: {
                                auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
                                static_bg = o->adv_bg;
                                anim_bg = o->adv_bg_anim;
                                if (goal->current_stage >= goal->stage_count - 1) {
                                    static_bg = o->adv_bg_done;
                                    anim_bg = o->adv_bg_done_anim;
                                } else if (goal->current_stage > 0) {
                                    static_bg = o->adv_bg_half_done;
                                    anim_bg = o->adv_bg_half_done_anim;
                                }
                                icon_path = goal->icon_path;
                                strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                                name_buf[sizeof(name_buf) - 1] = '\0';
                                if (goal->current_stage < goal->stage_count) {
                                    SubGoal *active_stage = goal->stages[goal->current_stage];
                                    if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress > 0) {
                                        snprintf(progress_buf, sizeof(progress_buf), "%s (%d/%d)",
                                                 active_stage->display_text, active_stage->current_stat_progress,
                                                 active_stage->required_progress);
                                    } else {
                                        snprintf(progress_buf, sizeof(progress_buf), "%s", active_stage->display_text);
                                    }
                                }
                                break;
                            }

                            default: break;
                        }

                        SDL_FRect bg_rect = {current_x + bg_x_offset, ROW2_Y_POS, ITEM_WIDTH, ITEM_WIDTH};
                        render_texture_with_alpha(o->renderer, static_bg, anim_bg, &bg_rect, 255);

                        SDL_FRect icon_rect = {bg_rect.x + 16.0f, bg_rect.y + 16.0f, 64.0f, 64.0f};
                        SDL_Texture *tex = nullptr;
                        AnimatedTexture *anim_tex = nullptr;
                        if (!icon_path.empty() && strstr(icon_path.c_str(), ".gif")) {
                            anim_tex = get_animated_texture_from_cache(o->renderer, &o->anim_cache,
                                                                       &o->anim_cache_count,
                                                                       &o->anim_cache_capacity, icon_path.c_str(),
                                                                       SDL_SCALEMODE_NEAREST);
                        } else if (!icon_path.empty()) {
                            tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                         &o->texture_cache_capacity, icon_path.c_str(),
                                                         SDL_SCALEMODE_NEAREST);
                        }
                        render_texture_with_alpha(o->renderer, tex, anim_tex, &icon_rect, 255);

                        SDL_Texture *name_texture = get_text_texture_from_cache(o, name_buf, text_color);
                        if (name_texture) {
                            float w, h;
                            SDL_GetTextureSize(name_texture, &w, &h);
                            float text_x = current_x + (cell_width_row2 - w) / 2.0f;
                            SDL_FRect dest_rect = {text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET, w, h};
                            SDL_RenderTexture(o->renderer, name_texture, nullptr, &dest_rect);

                            if (progress_buf[0] != '\0') {
                                SDL_Texture *progress_texture =
                                        get_text_texture_from_cache(o, progress_buf, text_color);
                                if (progress_texture) {
                                    float pw, ph;
                                    SDL_GetTextureSize(progress_texture, &pw, &ph);
                                    float p_text_x = current_x + (cell_width_row2 - pw) / 2.0f;
                                    SDL_FRect p_dest_rect = {
                                        p_text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + h, pw, ph
                                    };
                                    SDL_RenderTexture(o->renderer, progress_texture, nullptr, &p_dest_rect);
                                }
                            }
                        }

                        visible_item_index++;
                    }
                }
            }
        }

        // Update the state for the next frame
        run_was_complete_last_frame = is_run_complete;
    }

    // --- ROW 3: Stats & Goals (excluding forced items with "in_2nd_row" flag) ---
    {
        const float ROW3_Y_POS = 260.0f; // Configure height of row, more pushes down
        const float ITEM_WIDTH = 96.0f; // Minimum width based on icon bg
        const float ITEM_SPACING = 16.0f;
        const float TEXT_Y_OFFSET = 4.0f;

        // Gather items for this row
        std::vector<OverlayDisplayItem> row3_items;
        for (int i = 0; i < t->template_data->stat_count; ++i) {
            TrackableCategory *stat_cat = t->template_data->stats[i];
            if (stat_cat->in_2nd_row) continue; // SKIP if forced to Row 2 ("in_2nd_row")
            // Skip hidden helper stats that are not meant to be displayed
            if (stat_cat->is_single_stat_category && stat_cat->criteria_count > 0 && stat_cat->criteria[0]->goal <= 0 &&
                stat_cat->icon_path[0] == '\0') {
                continue;
            }
            row3_items.push_back({stat_cat, OverlayDisplayItem::STAT});
        }
        for (int i = 0; i < t->template_data->custom_goal_count; ++i) {
            if (t->template_data->custom_goals[i]->in_2nd_row) continue; // SKIP if forced to Row 2 ("in_2nd_row")
            row3_items.push_back({
                t->template_data->custom_goals[i], OverlayDisplayItem::CUSTOM
            });
        }
        for (int i = 0; i < t->template_data->multi_stage_goal_count; ++i) {
            if (t->template_data->multi_stage_goals[i]->in_2nd_row) continue; // SKIP if forced to Row 2 ("in_2nd_row")
            row3_items.push_back({
                t->template_data->multi_stage_goals[i], OverlayDisplayItem::MULTISTAGE
            });
        }

        float max_text_width_row3 = 0.0f;

        // Calculate max text width for row 3
        for (const auto &display_item: row3_items) {
            // Skip items hidden *in the template*
            bool is_template_hidden = false;
            if (display_item.type == OverlayDisplayItem::STAT) {
                TrackableCategory *stat_cat = static_cast<TrackableCategory *>(display_item.item_ptr);
                // Skip hidden legacy helper stats during width calculation
                if (version <= MC_VERSION_1_6_4 && stat_cat->is_single_stat_category && stat_cat->criteria_count > 0
                    &&
                    stat_cat->criteria[0]->goal <= 0 && stat_cat->icon_path[0] == '\0') {
                    continue;
                }
                is_template_hidden = stat_cat->is_hidden;
            } else if (display_item.type == OverlayDisplayItem::CUSTOM) {
                is_template_hidden = static_cast<TrackableItem *>(display_item.item_ptr)->is_hidden;
            } else if (display_item.type == OverlayDisplayItem::MULTISTAGE) {
                is_template_hidden = static_cast<MultiStageGoal *>(display_item.item_ptr)->is_hidden;
            }
            if (is_template_hidden) continue;

            char name_buf[256] = {0};
            char longest_progress_buf[256] = {0}; // To store the potentially longest progress/stage text
            int w_name = 0, w_progress = 0;

            switch (display_item.type) {
                case OverlayDisplayItem::STAT: {
                    auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
                    strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);

                    if (!stat->is_single_stat_category) {
                        // Complex Stat
                        // Find longest sub-stat line (e.g., "1. Name (X / Y)")
                        snprintf(name_buf, sizeof(name_buf), "%s (%d / %d)", stat->display_name,
                                 stat->completed_criteria_count, stat->criteria_count);
                        TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);

                        for (int j = 0; j < stat->criteria_count; ++j) {
                            TrackableItem *crit = stat->criteria[j];
                            char temp_sub_stat_buf[256] = {0};
                            if (crit->goal > 0) {
                                snprintf(temp_sub_stat_buf, sizeof(temp_sub_stat_buf), "%d. %s (%d / %d)", j + 1,
                                         crit->display_name, crit->goal, crit->goal); // Use max progress for width
                            } else if (crit->goal == -1) {
                                snprintf(temp_sub_stat_buf, sizeof(temp_sub_stat_buf), "%d. %s (999)", j + 1,
                                         crit->display_name); // Assume 3 digits for width
                            }
                            if (strlen(temp_sub_stat_buf) > strlen(longest_progress_buf)) {
                                strcpy(longest_progress_buf, temp_sub_stat_buf);
                            }
                        }
                    } else if (stat->criteria_count == 1) {
                        // Simple stat
                        TrackableItem *crit = stat->criteria[0];
                        if (crit->goal > 0) {
                            snprintf(longest_progress_buf, sizeof(longest_progress_buf), "(%d / %d)", crit->goal,
                                     crit->goal); // Use max progress
                        } else if (crit->goal == -1) {
                            snprintf(longest_progress_buf, sizeof(longest_progress_buf), "(999)");
                            // Assume 3 digits
                        }
                    }
                    break;
                }
                case OverlayDisplayItem::CUSTOM: {
                    auto *goal = static_cast<TrackableItem *>(display_item.item_ptr);
                    strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    if (goal->goal > 0) {
                        snprintf(longest_progress_buf, sizeof(longest_progress_buf), "(%d / %d)", goal->goal,
                                 goal->goal);
                    } else if (goal->goal == -1) {
                        snprintf(longest_progress_buf, sizeof(longest_progress_buf), "(999)");
                    }
                    break;
                }
                case OverlayDisplayItem::MULTISTAGE: {
                    auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
                    strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    // Find longest stage text
                    for (int j = 0; j < goal->stage_count; ++j) {
                        SubGoal *stage = goal->stages[j];
                        char temp_stage_buf[256];
                        if (stage->type == SUBGOAL_STAT && stage->required_progress > 0) {
                            snprintf(temp_stage_buf, sizeof(temp_stage_buf), "%s (%d/%d)", stage->display_text,
                                     stage->required_progress, stage->required_progress);
                        } else {
                            strncpy(temp_stage_buf, stage->display_text, sizeof(temp_stage_buf) - 1);
                            temp_stage_buf[sizeof(temp_stage_buf) - 1] = '\0';
                        }
                        if (strlen(temp_stage_buf) > strlen(longest_progress_buf)) {
                            strcpy(longest_progress_buf, temp_stage_buf);
                        }
                    }
                    break;
                }
                default: break;
            }

            if (longest_progress_buf[0] != '\0') {
                TTF_MeasureString(o->font, longest_progress_buf, 0, 0, &w_progress, nullptr);
            }

            float item_max_text_width = fmaxf((float) w_name, (float) w_progress);
            max_text_width_row3 = fmaxf(max_text_width_row3, item_max_text_width);
        }

        // Apply spacing settings
        float cell_width_row3;
        float item_full_width_row3;

        if (settings->overlay_row3_custom_spacing_enabled) {
            // Use fixed width from setting
            item_full_width_row3 = settings->overlay_row3_custom_spacing;
            cell_width_row3 = item_full_width_row3 - ITEM_SPACING;
        } else {
            cell_width_row3 = fmaxf(ITEM_WIDTH, max_text_width_row3);
            item_full_width_row3 = cell_width_row3 + ITEM_SPACING;
        }

        o->calculated_row3_item_width = item_full_width_row3; // Store for next update cycle

        size_t visible_item_count = 0;
        for (const auto &item: row3_items) {
            if (!is_display_item_done(item, settings)) {
                visible_item_count++;
            }
        }

        if (visible_item_count > 0) {
            // Render pass uses the calculated item_full_width_row3
            float total_row_width = visible_item_count * item_full_width_row3;
            // Use current visible count for total width
            float start_pos = fmod(o->scroll_offset_row3, total_row_width);
            int blocks_to_draw = (total_row_width > 0) ? (int) ceil((float) window_w / total_row_width) + 2 : 0;

            for (int block = -blocks_to_draw; block <= blocks_to_draw; ++block) {
                float block_offset = start_pos + (block * total_row_width);
                int visible_item_index = 0;

                for (const auto &display_item: row3_items) {
                    if (is_display_item_done(display_item, settings)) continue; // Skip currently invisible

                    // --- Directional Gap Filling ---
                    float current_x;
                    if (settings->overlay_scroll_speed > 0) {
                        // Positive Scroll (L->R): Anchor to the Right
                        current_x = block_offset - (visible_item_index * item_full_width_row3);
                    } else {
                        // Negative Scroll (R->L): Anchor to the Left
                        current_x = block_offset + (visible_item_index * item_full_width_row3);
                    }

                    // Strict Culling based on Icon Visibility
                    // Calculate where the 96px icon background will sit
                    float bg_x_offset = (cell_width_row3 - ITEM_WIDTH) / 2.0f;
                    float icon_visual_x = current_x + bg_x_offset;

                    // Use the calculated max text width as the safety margin
                    float dynamic_cull_margin = max_text_width_row3 + 50.0f;

                    // Check if the item is completely off-screen
                    if (icon_visual_x + ITEM_WIDTH + dynamic_cull_margin < 0 || icon_visual_x - dynamic_cull_margin >
                        window_w) {
                        visible_item_index++;
                        continue;
                    }

                    // --- Render the item ---
                    SDL_Texture *static_bg = nullptr;
                    AnimatedTexture *anim_bg = nullptr;

                    std::string icon_path;
                    char name_buf[256] = {0}; // Renamed
                    char progress_buf[256] = {0}; // Renamed and increased size

                    switch (display_item.type) {
                        case OverlayDisplayItem::STAT: {
                            auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
                            static_bg = o->adv_bg;
                            anim_bg = o->adv_bg_anim;
                            if (stat->done) {
                                static_bg = o->adv_bg_done;
                                anim_bg = o->adv_bg_done_anim;
                            } else if ((!stat->is_single_stat_category && stat->completed_criteria_count > 0) ||
                                       (stat->is_single_stat_category && stat->criteria_count > 0 && stat->criteria[0]->
                                        progress > 0)) {
                                static_bg = o->adv_bg_half_done;
                                anim_bg = o->adv_bg_half_done_anim;
                            }
                            icon_path = stat->icon_path;

                            if (!stat->is_single_stat_category) {
                                // If it's a complex stat (even if just one sub-stat)
                                // Multi-stat / Complex stat logic
                                snprintf(name_buf, sizeof(name_buf), "%s (%d / %d)", stat->display_name,
                                         stat->completed_criteria_count, stat->criteria_count);

                                std::vector<int> incomplete_indices;
                                for (int j = 0; j < stat->criteria_count; ++j) {
                                    if (!stat->criteria[j]->done && !stat->criteria[j]->is_hidden) {
                                        incomplete_indices.push_back(j);
                                    }
                                }

                                if (!incomplete_indices.empty()) {
                                    int cycle_duration_ms = (int) (settings->overlay_stat_cycle_speed * 1000.0f);
                                    if (cycle_duration_ms <= 0) cycle_duration_ms = 1000;

                                    Uint32 current_ticks = SDL_GetTicks();
                                    int num_incomplete = incomplete_indices.size();
                                    int list_index_to_show = (current_ticks / cycle_duration_ms) % num_incomplete;
                                    int original_crit_index = incomplete_indices[list_index_to_show];
                                    TrackableItem *crit = stat->criteria[original_crit_index];

                                    if (crit->goal > 0) {
                                        snprintf(progress_buf, sizeof(progress_buf), "%d. %s (%d / %d)",
                                                 original_crit_index + 1, crit->display_name, crit->progress,
                                                 crit->goal);
                                    } else if (crit->goal == -1) {
                                        snprintf(progress_buf, sizeof(progress_buf), "%d. %s (%d)",
                                                 original_crit_index + 1, crit->display_name, crit->progress);
                                    }
                                } else {
                                    progress_buf[0] = '\0';
                                }
                            } else {
                                // Simple stat
                                strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
                                name_buf[sizeof(name_buf) - 1] = '\0';
                                if (stat->criteria_count == 1) {
                                    TrackableItem *crit = stat->criteria[0];
                                    if (crit->goal > 0) {
                                        snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                                 crit->progress, crit->goal);
                                    } else if (crit->goal == -1) {
                                        snprintf(progress_buf, sizeof(progress_buf), "(%d)", crit->progress);
                                    }
                                }
                            }
                            break;
                        }
                        case OverlayDisplayItem::CUSTOM: {
                            auto *goal = static_cast<TrackableItem *>(display_item.item_ptr);
                            static_bg = o->adv_bg;
                            anim_bg = o->adv_bg_anim;
                            if (goal->done) {
                                static_bg = o->adv_bg_done;
                                anim_bg = o->adv_bg_done_anim;
                            } else if (goal->progress > 0) {
                                static_bg = o->adv_bg_half_done;
                                anim_bg = o->adv_bg_half_done_anim;
                            }
                            icon_path = goal->icon_path;
                            strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                            name_buf[sizeof(name_buf) - 1] = '\0';
                            if (goal->goal > 0) {
                                snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", goal->progress, goal->goal);
                            } else if (goal->goal == -1 && !goal->done) {
                                snprintf(progress_buf, sizeof(progress_buf), "(%d)", goal->progress);
                            }
                            break;
                        }
                        case OverlayDisplayItem::MULTISTAGE: {
                            auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
                            static_bg = o->adv_bg;
                            anim_bg = o->adv_bg_anim;
                            if (goal->current_stage >= goal->stage_count - 1) {
                                static_bg = o->adv_bg_done;
                                anim_bg = o->adv_bg_done_anim;
                            } else if (goal->current_stage > 0) {
                                static_bg = o->adv_bg_half_done;
                                anim_bg = o->adv_bg_half_done_anim;
                            }
                            icon_path = goal->icon_path;
                            strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                            name_buf[sizeof(name_buf) - 1] = '\0';
                            if (goal->current_stage < goal->stage_count) {
                                SubGoal *active_stage = goal->stages[goal->current_stage];
                                if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress > 0) {
                                    snprintf(progress_buf, sizeof(progress_buf), "%s (%d/%d)",
                                             active_stage->display_text, active_stage->current_stat_progress,
                                             active_stage->required_progress);
                                } else {
                                    snprintf(progress_buf, sizeof(progress_buf), "%s", active_stage->display_text);
                                }
                            }
                            break;
                        }
                        default: break;
                    }

                    // --- Make sure text positioning uses cell_width_row3 for centering ---
                    SDL_FRect bg_rect = {current_x + bg_x_offset, ROW3_Y_POS, ITEM_WIDTH, ITEM_WIDTH};
                    render_texture_with_alpha(o->renderer, static_bg, anim_bg, &bg_rect, 255);

                    SDL_FRect icon_rect = {bg_rect.x + 16.0f, bg_rect.y + 16.0f, 64.0f, 64.0f};
                    SDL_Texture *tex = nullptr;
                    AnimatedTexture *anim_tex = nullptr;
                    if (!icon_path.empty() && strstr(icon_path.c_str(), ".gif")) {
                        anim_tex = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
                                                                   &o->anim_cache_capacity, icon_path.c_str(),
                                                                   SDL_SCALEMODE_NEAREST);
                    } else if (!icon_path.empty()) {
                        tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                     &o->texture_cache_capacity, icon_path.c_str(),
                                                     SDL_SCALEMODE_NEAREST);
                    }
                    render_texture_with_alpha(o->renderer, tex, anim_tex, &icon_rect, 255);


                    // Text rendering uses cell_width_row3 for centering
                    SDL_Texture *name_texture = get_text_texture_from_cache(o, name_buf, text_color);
                    // Use name_buf calculated earlier
                    if (name_texture) {
                        float w, h;
                        SDL_GetTextureSize(name_texture, &w, &h);
                        float text_x = current_x + (cell_width_row3 - w) / 2.0f; // Center using cell_width_row3
                        SDL_FRect dest_rect = {text_x, ROW3_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET, w, h};
                        SDL_RenderTexture(o->renderer, name_texture, nullptr, &dest_rect);

                        if (progress_buf[0] != '\0') {
                            // Use progress_buf which holds current text
                            SDL_Texture *progress_texture = get_text_texture_from_cache(o, progress_buf, text_color);
                            if (progress_texture) {
                                float pw, ph;
                                SDL_GetTextureSize(progress_texture, &pw, &ph);
                                float p_text_x = current_x + (cell_width_row3 - pw) / 2.0f;
                                // Center using cell_width_row3
                                SDL_FRect p_dest_rect = {p_text_x, ROW3_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + h, pw, ph};
                                SDL_RenderTexture(o->renderer, progress_texture, nullptr, &p_dest_rect);
                            }
                        }
                    }

                    visible_item_index++;
                } // End for display_item
            } // End for block
        } // End if visible_item_count > 0
    }

    // --- DEBUG: Performance Display ---
    if (settings->print_debug_status) {
        // Static variables to track frame rate
        static Uint32 frame_count = 0;
        static Uint32 last_fps_update_time = 0;
        static float current_fps = 0.0f;

        frame_count++;
        Uint32 current_ticks = SDL_GetTicks();

        // Calculate FPS once every second
        if ((current_ticks - last_fps_update_time) >= 1000) {
            current_fps = (float) frame_count;
            frame_count = 0;
            last_fps_update_time = current_ticks;
        }

        // Format the debug string
        char debug_buffer[128];
        snprintf(debug_buffer, sizeof(debug_buffer), "FPS: %.1f | dT: %.1f ms",
                 current_fps, o->last_delta_time * 1000.0f);

        // Render the text using the cache
        SDL_Color text_color = {255, 0, 255, 255}; // Purple for visibility
        SDL_Texture *text_texture = get_text_texture_from_cache(o, debug_buffer, text_color);
        if (text_texture) {
            float w, h;
            SDL_GetTextureSize(text_texture, &w, &h);
            SDL_FRect dest_rect = {5.0f, 5.0f, w, h};
            SDL_RenderTexture(o->renderer, text_texture, nullptr, &dest_rect);
        }
    }

    // END OF DEBUG -------------------------------------------------

    SDL_RenderPresent(o->renderer);
}


void overlay_free(Overlay **overlay, const AppSettings *settings) {
    (void) settings;
    if (overlay && *overlay) {
        Overlay *o = *overlay;

        // Free the caches
        if (o->texture_cache) {
            for (int i = 0; i < o->texture_cache_count; i++) {
                if (o->texture_cache[i].texture) {
                    SDL_DestroyTexture(o->texture_cache[i].texture);
                }
            }
            free(o->texture_cache);
            o->texture_cache = nullptr;
        }
        if (o->anim_cache) {
            for (int i = 0; i < o->anim_cache_count; i++) {
                if (o->anim_cache[i].anim) {
                    free_animated_texture(o->anim_cache[i].anim);
                }
            }
            free(o->anim_cache);
            o->anim_cache = nullptr;
        }

        // Free the new text cache
        if (o->text_cache) {
            for (int i = 0; i < o->text_cache_count; i++) {
                if (o->text_cache[i].texture) {
                    SDL_DestroyTexture(o->text_cache[i].texture);
                }
            }
            free(o->text_cache);
            o->text_cache = nullptr;
        }

        if (o->text_engine) {
            TTF_DestroyRendererTextEngine(o->text_engine);
            o->text_engine = nullptr;
        }

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

        log_message(LOG_INFO, "[OVERLAY] Overlay freed!\n");
    }
}
