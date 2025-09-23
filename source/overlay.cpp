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

    // Load global background textures using the overlay's renderer
    char adv_bg_path[MAX_PATH_LENGTH];
    snprintf(adv_bg_path, sizeof(adv_bg_path), "%s/gui/advancement_background.png", get_resources_path());
    o->adv_bg = load_texture_with_scale_mode(o->renderer, adv_bg_path, SDL_SCALEMODE_NEAREST);

    snprintf(adv_bg_path, sizeof(adv_bg_path), "%s/gui/advancement_background_half_done.png", get_resources_path());
    o->adv_bg_half_done = load_texture_with_scale_mode(o->renderer, adv_bg_path, SDL_SCALEMODE_NEAREST);

    snprintf(adv_bg_path, sizeof(adv_bg_path), "%s/gui/advancement_background_done.png", get_resources_path());
    o->adv_bg_done = load_texture_with_scale_mode(o->renderer, adv_bg_path, SDL_SCALEMODE_NEAREST);
    if (!o->adv_bg || !o->adv_bg_half_done || !o->adv_bg_done) {
        log_message(LOG_ERROR, "[OVERLAY] Failed to load advancement background textures.\n");
        overlay_free(overlay, settings);
        return false;
    }

    // Make font HiDPI aware by loading it at a base point size (e.g., 24).
    // SDL_ttf will automatically scale it correctly on any monitor at render time.
    const float base_font_size = DEFAULT_OVERLAY_FONT_SIZE;
    char overlay_font_path[1024];
    snprintf(overlay_font_path, sizeof(overlay_font_path), "%s/fonts/%s", get_resources_path(), settings->overlay_font_name);

    if (!path_exists(overlay_font_path)) {
        log_message(LOG_ERROR, "[OVERLAY] Tracker/Overlay Font '%s' not found. Falling back to default Minecraft font.\n", settings->overlay_font_name);
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
    switch (display_item.type) {
        case OverlayDisplayItem::ADVANCEMENT:
        case OverlayDisplayItem::STAT: {
            auto *cat = static_cast<TrackableCategory *>(display_item.item_ptr);
            is_hidden = cat->is_hidden;
            break;
        }
        case OverlayDisplayItem::UNLOCK:
        case OverlayDisplayItem::CUSTOM: {
            auto *item = static_cast<TrackableItem *>(display_item.item_ptr);
            is_hidden = item->is_hidden;
            break;
        }
        case OverlayDisplayItem::MULTISTAGE: {
            auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
            is_hidden = goal->is_hidden;
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
            // These types belong to Row 3
            should_hide_when_done = settings->overlay_row3_remove_completed;
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

    // --- Update Animation State ---
    const float base_scroll_speed = 60.0f;
    float speed_multiplier = settings->overlay_scroll_speed;
    if (settings->overlay_animation_speedup) speed_multiplier *= OVERLAY_SPEEDUP_FACTOR;
    float scroll_delta = -(base_scroll_speed * speed_multiplier * (*deltaTime));

    // --- Row 1 Update Logic ---
    if (!row1_items.empty()) {
        // VALIDATE INDEX FIRST to prevent out-of-bounds access after a template reload.
        if (static_cast<size_t>(o->start_index_row1) >= row1_items.size()) {
            o->start_index_row1 = 0;
        }

        // Count visible items to prevent lag when the row is completed
        size_t visible_item_count = 0;
        for (const auto &item_pair: row1_items) {
            // A criterion is only visible if it is not done AND its parent category is not hidden.
            if (!item_pair.first->done && !item_pair.second->is_hidden) {
                visible_item_count++;
            }
        }

        if (visible_item_count > 0) {
            const float item_full_width = 48.0f + 8.0f; // ROW1_ICON_SIZE + ROW1_SPACING
            o->scroll_offset_row1 += scroll_delta;
            int items_scrolled = floor(fabs(o->scroll_offset_row1) / item_full_width);
            if (items_scrolled > 0) {
                o->scroll_offset_row1 = fmod(o->scroll_offset_row1, item_full_width);
                if (scroll_delta > 0) {
                    for (int i = 0; i < items_scrolled; ++i) {
                        size_t loop_guard = 0;
                        do {
                            // Only rendering item if it is not done AND its parent category is not hidden.
                            o->start_index_row1 = (o->start_index_row1 + 1) % row1_items.size();
                            loop_guard++;
                        } while ((row1_items[o->start_index_row1].first->done || row1_items[o->start_index_row1].second
                                  ->is_hidden) && loop_guard < row1_items.size() * 2);
                    }
                } else {
                    for (int i = 0; i < items_scrolled; ++i) {
                        size_t loop_guard = 0;
                        do {
                            o->start_index_row1 = (o->start_index_row1 - 1 + row1_items.size()) % row1_items.size();
                            loop_guard++;
                        } while ((row1_items[o->start_index_row1].first->done || row1_items[o->start_index_row1].second
                                  ->is_hidden) && loop_guard < row1_items.size() * 2);
                    }
                }
            }
        }
    }

    // --- Row 2 Update Logic (Dynamic Width) ---
    if (!row2_items.empty()) {
        // VALIDATE INDEX FIRST to prevent out-of-bounds access after a template reload.
        if (static_cast<size_t>(o->start_index_row2) >= row2_items.size()) {
            o->start_index_row2 = 0;
        }

        size_t visible_item_count = 0;
        float max_text_width = 0.0f;

        // Process all visible items in a single pass to get their count and max width
        for (const auto &display_item: row2_items) {
            if (is_display_item_done(display_item, settings)) {
                continue; // Skip hidden items
            }
            visible_item_count++;

            // --- Calculate text width for this visible item ---
            char name_buf[256] = {0};
            char progress_buf[64] = {0};

            if (display_item.type == OverlayDisplayItem::ADVANCEMENT) {
                auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
                strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                if (adv->criteria_count > 0)
                    snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                             adv->completed_criteria_count, adv->criteria_count);
            } else if (display_item.type == OverlayDisplayItem::UNLOCK) {
                auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
                strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
            }

            // --- OPTIMIZATION: Use TTF_MeasureString for Row 3 as well ---
            int w;
            if (name_buf[0] != '\0') {
                TTF_MeasureString(o->font, name_buf, 0, 0, &w, nullptr);
                max_text_width = fmaxf(max_text_width, (float) w);
            }
            if (progress_buf[0] != '\0') {
                TTF_MeasureString(o->font, progress_buf, 0, 0, &w, nullptr);
                max_text_width = fmaxf(max_text_width, (float) w);
            }
        }

        // Only run the animation logic if there is something to show
        if (visible_item_count > 0) {
            const float cell_width = fmaxf(96.0f, max_text_width);
            const float item_full_width = cell_width + 16.0f;

            o->scroll_offset_row2 += scroll_delta;
            int items_scrolled = floor(fabs(o->scroll_offset_row2) / item_full_width);
            if (items_scrolled > 0) {
                o->scroll_offset_row2 = fmod(o->scroll_offset_row2, item_full_width);
                if (scroll_delta > 0) {
                    // Moving RTL
                    for (int i = 0; i < items_scrolled; ++i) {
                        size_t loop_guard = 0;
                        do {
                            o->start_index_row2 = (o->start_index_row2 + 1) % row2_items.size();
                            loop_guard++;
                        } while (is_display_item_done(row2_items[o->start_index_row2], settings) && loop_guard <
                                 row2_items.size() * 2);
                    }
                } else {
                    // Moving LTR
                    for (int i = 0; i < items_scrolled; ++i) {
                        size_t loop_guard = 0;
                        do {
                            o->start_index_row2 = (o->start_index_row2 - 1 + row2_items.size()) % row2_items.size();
                            loop_guard++;
                        } while (is_display_item_done(row2_items[o->start_index_row2], settings) && loop_guard <
                                 row2_items.size() * 2);
                    }
                }
            }
        }
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
            snprintf(info_buffer, sizeof(info_buffer), "*** RUN COMPLETE! *** | Final Time: %s", formatted_time);
        } else {
            // Conditionally build the progress string section by section
            if (settings->overlay_show_world && t->world_name[0] != '\0') {
                add_component(t->world_name);
            }

            if (settings->overlay_show_run_details) {
                char formatted_category[128];
                format_category_string(settings->category, formatted_category, sizeof(formatted_category));
                char formatted_flag[128];
                format_category_string(settings->optional_flag, formatted_flag, sizeof(formatted_flag));
                snprintf(temp_chunk, sizeof(temp_chunk), "%s - %s%s%s",
                         settings->version_str,
                         formatted_category,
                         *settings->optional_flag ? " - " : "",
                         formatted_flag);
                add_component(temp_chunk);
            }

            if (settings->overlay_show_progress) {
                MC_Version version = settings_get_version_from_string(settings->version_str);
                const char *adv_ach_label = (version >= MC_VERSION_1_12) ? "Adv" : "Ach";
                snprintf(temp_chunk, sizeof(temp_chunk), "%s: %d/%d - Prog: %.2f%%",
                         adv_ach_label, t->template_data->advancements_completed_count,
                         t->template_data->advancement_goal_count, // Excluding recipes
                         t->template_data->overall_progress_percentage);
                add_component(temp_chunk);
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
        const float ROW1_Y_POS = 48.0f; // Initially 52
        const float ROW1_ICON_SIZE = 48.0f;
        const float ROW1_SPACING = 8.0f;
        const float ROW1_SHARED_ICON_SIZE = 30.0f;
        const float item_full_width = ROW1_ICON_SIZE + ROW1_SPACING;

        // Gather items to determine the list we are animating
        std::vector<std::pair<TrackableItem *, TrackableCategory *> > row1_items;
        for (int i = 0; i < t->template_data->advancement_count; i++) {
            TrackableCategory *cat = t->template_data->advancements[i];
            for (int j = 0; j < cat->criteria_count; j++) {
                row1_items.push_back({cat->criteria[j], cat});
            }
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


        if (!row1_items.empty()) {
            // Determine how many slots to draw based on screen width.
            int items_to_draw = ceil((float) window_w / item_full_width) + 2; // To render items off screen
            // Only draw as many items as are visible, but repeat them if needed to fill the screen (marquee effect).

            // Draw either enough slots to fill the screen OR only the visible items, whichever is smaller.
            // This prevents drawing empty, superfluous slots.

            // int items_to_draw = std::min(geometric_slots_needed, (int)visible_item_count);
            // int items_to_draw = geometric_slots_needed;
            int current_item_idx = o->start_index_row1;


            for (int k = 0; k < items_to_draw; k++) {
                // -1.0f to offset rendering one icon width to the left (make it hidden)
                float x_pos = ((k - 1.0f) * item_full_width) - o->scroll_offset_row1;

                // Find the next visible item, skipping it if it's done OR its parent is hidden.
                size_t loop_guard = 0;
                while (row1_items[current_item_idx].first->done || row1_items[current_item_idx].second->is_hidden) {
                    current_item_idx = (current_item_idx + 1) % row1_items.size();
                    loop_guard++;
                    if (loop_guard > row1_items.size()) {
                        goto end_row1_render; // All visible items are done/hidden
                    }
                }

                TrackableItem *item_to_render = row1_items[current_item_idx].first;
                TrackableCategory *parent = row1_items[current_item_idx].second;

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
                render_texture_with_alpha(o->renderer, tex, anim_tex, &dest_rect, 255);

                // Safeguard Check, if item has no texture
                if (!tex && !anim_tex) {
                    // This item has no valid texture, print a warning and draw a placeholder
                    log_message(
                        LOG_ERROR,
                        "[Overlay Render WARNING] Item '%s' (index %d) has no texture. Check icon path in template.\n",
                        item_to_render->root_name, current_item_idx);
                    SDL_SetRenderDrawColor(o->renderer, 255, 0, 255, 100); // Pink placeholder
                    SDL_RenderFillRect(o->renderer, &dest_rect);
                } else {
                    render_texture_with_alpha(o->renderer, tex, anim_tex, &dest_rect, 255);
                }

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

                // Advance to the next item for the next screen slot
                current_item_idx = (current_item_idx + 1) % row1_items.size();
            }
        }
    end_row1_render:;
    }

    // --- ROW 2: Advancements & Unlocks ---
    {
        const float ROW2_Y_POS = 108.0f;
        const float ITEM_WIDTH = 96.0f;
        const float ITEM_SPACING = 16.0f;
        const float TEXT_Y_OFFSET = 4.0f;

        std::vector<OverlayDisplayItem> row2_items;
        for (int i = 0; i < t->template_data->advancement_count; ++i)
            row2_items.push_back({
                t->template_data->advancements[i], OverlayDisplayItem::ADVANCEMENT
            });
        for (int i = 0; i < t->template_data->unlock_count; ++i)
            row2_items.push_back({
                t->template_data->unlocks[i], OverlayDisplayItem::UNLOCK
            });

        size_t visible_item_count = 0;
        for (const auto &item: row2_items) {
            if (!is_display_item_done(item, settings)) {
                visible_item_count++;
            }
        }

        if (visible_item_count > 0) {
            float max_text_width = 0.0f;
            // First pass: calculate the max width required by any visible item in the row
            for (const auto &display_item: row2_items) {
                if (is_display_item_done(display_item, settings)) continue;
                char name_buf[256] = {0}, progress_buf[64] = {0};

                if (display_item.type == OverlayDisplayItem::ADVANCEMENT) {
                    auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
                    strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                    if (adv->criteria_count > 0)
                        snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                 adv->completed_criteria_count, adv->criteria_count);
                } else if (display_item.type == OverlayDisplayItem::UNLOCK) {
                    auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
                    strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
                }

                // Use TTF_MeasureString for Row 3 as well
                int w;
                if (name_buf[0] != '\0') {
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w, nullptr);
                    max_text_width = fmaxf(max_text_width, (float) w);
                }
                if (progress_buf[0] != '\0') {
                    TTF_MeasureString(o->font, progress_buf, 0, 0, &w, nullptr);
                    max_text_width = fmaxf(max_text_width, (float) w);
                }
            }

            const float cell_width = fmaxf(ITEM_WIDTH, max_text_width);
            const float item_full_width = cell_width + ITEM_SPACING;

            int items_to_draw = (item_full_width > 0) ? ceil((float) window_w / item_full_width) + 2 : 0;
            int current_item_idx = o->start_index_row2;

            for (int k = 0; k < items_to_draw; k++) {
                size_t loop_guard = 0;
                while (is_display_item_done(row2_items[current_item_idx], settings)) {
                    current_item_idx = (current_item_idx + 1) % row2_items.size();
                    loop_guard++;
                    if (loop_guard > row2_items.size()) goto end_row2_render;
                }

                const auto &display_item = row2_items[current_item_idx];
                float x_pos = ((k - 1) * item_full_width) - o->scroll_offset_row2;

                // --- Render the item ---
                SDL_Texture *bg_texture = o->adv_bg;
                std::string icon_path;
                char name_buf[256] = {0}, progress_buf[64] = {0};

                switch (display_item.type) {
                    case OverlayDisplayItem::ADVANCEMENT: {
                        auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
                        if (adv->done) bg_texture = o->adv_bg_done;
                        else if (adv->completed_criteria_count > 0) bg_texture = o->adv_bg_half_done;
                        icon_path = adv->icon_path;
                        strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                        if (adv->criteria_count > 0) {
                            snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", adv->completed_criteria_count,
                                     adv->criteria_count);
                        }
                        break;
                    }
                    case OverlayDisplayItem::UNLOCK: {
                        auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
                        if (unlock->done) bg_texture = o->adv_bg_done;
                        icon_path = unlock->icon_path;
                        strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
                        break;
                    }
                    default: break;
                }

                float bg_x_offset = (cell_width - ITEM_WIDTH) / 2.0f;
                SDL_FRect bg_rect = {x_pos + bg_x_offset, ROW2_Y_POS, ITEM_WIDTH, ITEM_WIDTH};
                if (bg_texture) SDL_RenderTexture(o->renderer, bg_texture, nullptr, &bg_rect);

                SDL_FRect icon_rect = {bg_rect.x + 16.0f, bg_rect.y + 16.0f, 64.0f, 64.0f};
                SDL_Texture *tex = nullptr;
                AnimatedTexture *anim_tex = nullptr;
                if (!icon_path.empty() && strstr(icon_path.c_str(), ".gif")) {
                    anim_tex = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
                                                               &o->anim_cache_capacity, icon_path.c_str(),
                                                               SDL_SCALEMODE_NEAREST);
                } else if (!icon_path.empty()) {
                    tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                 &o->texture_cache_capacity, icon_path.c_str(), SDL_SCALEMODE_NEAREST);
                }
                render_texture_with_alpha(o->renderer, tex, anim_tex, &icon_rect, 255);


                // Text rendering now uses the cache
                SDL_Texture *name_texture = get_text_texture_from_cache(o, name_buf, text_color);
                if (name_texture) {
                    float w, h;
                    SDL_GetTextureSize(name_texture, &w, &h);
                    float text_x = x_pos + (cell_width - w) / 2.0f;
                    SDL_FRect dest_rect = {text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET, w, h};
                    SDL_RenderTexture(o->renderer, name_texture, nullptr, &dest_rect);

                    if (progress_buf[0] != '\0') {
                        SDL_Texture *progress_texture = get_text_texture_from_cache(o, progress_buf, text_color);
                        if (progress_texture) {
                            float pw, ph;
                            SDL_GetTextureSize(progress_texture, &pw, &ph);
                            float p_text_x = x_pos + (cell_width - pw) / 2.0f;
                            SDL_FRect p_dest_rect = {p_text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + h, pw, ph};
                            SDL_RenderTexture(o->renderer, progress_texture, nullptr, &p_dest_rect);
                        }
                    }
                }

                // This is the correct place to advance to the next item for the next screen slot.
                current_item_idx = (current_item_idx + 1) % row2_items.size();
            }
        }
    end_row2_render:; // FIX: Corrected goto label name
    }

    // --- ROW 3: Stats & Goals ---
    {
        const float ROW3_Y_POS = 260.0f; // Configure height of row, more pushes down
        const float ITEM_WIDTH = 96.0f;
        const float ITEM_SPACING = 16.0f;
        const float TEXT_Y_OFFSET = 4.0f;

        // Gather items for this row
        std::vector<OverlayDisplayItem> row3_items;
        for (int i = 0; i < t->template_data->stat_count; ++i) {
            TrackableCategory *stat_cat = t->template_data->stats[i];
            // Skip hidden helper stats that are not meant to be displayed
            if (stat_cat->is_single_stat_category && stat_cat->criteria_count > 0 && stat_cat->criteria[0]->goal <= 0 &&
                stat_cat->icon_path[0] == '\0') {
                continue;
            }
            row3_items.push_back({stat_cat, OverlayDisplayItem::STAT});
        }
        for (int i = 0; i < t->template_data->custom_goal_count; ++i)
            row3_items.push_back({
                t->template_data->custom_goals[i], OverlayDisplayItem::CUSTOM
            });
        for (int i = 0; i < t->template_data->multi_stage_goal_count; ++i)
            row3_items.push_back({
                t->template_data->multi_stage_goals[i], OverlayDisplayItem::MULTISTAGE
            });

        size_t visible_item_count = 0;
        for (const auto &item: row3_items) {
            if (!is_display_item_done(item, settings)) {
                visible_item_count++;
            }
        }

        if (visible_item_count > 0) {
            float max_text_width = 0.0f;
            // First pass: calculate the max width required by any visible item in the row
            for (const auto &display_item: row3_items) {
                // This is the layout calculation loop
                if (is_display_item_done(display_item, settings)) continue;

                char name_buf[256] = {0};
                char progress_buf[256] = {0};

                switch (display_item.type) {
                    case OverlayDisplayItem::STAT: {
                        auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);

                        if (stat->criteria_count > 1) {
                            // For multi-stats, find the max width between the title and the longest sub-stat.
                            char title_line[256];
                            snprintf(title_line, sizeof(title_line), "%s (%d / %d)", stat->display_name,
                                     stat->completed_criteria_count, stat->criteria_count);

                            char longest_sub_stat_line[256] = {0};
                            for (int j = 0; j < stat->criteria_count; ++j) {
                                TrackableItem *crit = stat->criteria[j];
                                char temp_sub_stat_buf[256] = {0};
                                if (crit->goal > 0) {
                                    snprintf(temp_sub_stat_buf, sizeof(temp_sub_stat_buf), "%d. %s (%d / %d)",
                                             j + 1, crit->display_name, crit->progress, crit->goal);
                                } else if (crit->goal == -1) {
                                    snprintf(temp_sub_stat_buf, sizeof(temp_sub_stat_buf), "%d. %s (%d)",
                                             j + 1, crit->display_name, crit->progress);
                                }
                                if (strlen(temp_sub_stat_buf) > strlen(longest_sub_stat_line)) {
                                    strcpy(longest_sub_stat_line, temp_sub_stat_buf);
                                }
                            }
                            strncpy(name_buf, title_line, sizeof(name_buf) - 1);
                            strncpy(progress_buf, longest_sub_stat_line, sizeof(progress_buf) - 1);
                        } else {
                            // Otherwise, use the original static format for single-criterion stats.
                            strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
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
                        strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                        if (goal->goal > 0) {
                            snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", goal->progress, goal->goal);
                        } else if (goal->goal == -1 && !goal->done) {
                            snprintf(progress_buf, sizeof(progress_buf), "(%d)", goal->progress);
                        }
                        break;
                    }
                    case OverlayDisplayItem::MULTISTAGE: {
                        auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
                        strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                        if (goal->current_stage < goal->stage_count) {
                            SubGoal *active_stage = goal->stages[goal->current_stage];
                            if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress > 0) {
                                snprintf(progress_buf, sizeof(progress_buf), "%s (%d/%d)", active_stage->display_text,
                                         active_stage->current_stat_progress, active_stage->required_progress);
                            } else {
                                snprintf(progress_buf, sizeof(progress_buf), "%s", active_stage->display_text);
                            }
                        }
                        break;
                    }
                    default: break;
                }

                // Use TTF_MeasureString for Row 3 as well
                int w;
                if (name_buf[0] != '\0') {
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w, nullptr);
                    max_text_width = fmaxf(max_text_width, (float) w);
                }
                if (progress_buf[0] != '\0') {
                    TTF_MeasureString(o->font, progress_buf, 0, 0, &w, nullptr);
                    max_text_width = fmaxf(max_text_width, (float) w);
                }
            }

            const float cell_width = fmaxf(ITEM_WIDTH, max_text_width);
            const float item_full_width = cell_width + ITEM_SPACING;

            // Render pass
            float total_row_width = visible_item_count * item_full_width;
            float start_pos = fmod(o->scroll_offset_row3, total_row_width);
            int blocks_to_draw = (total_row_width > 0) ? (int) ceil((float) window_w / total_row_width) + 2 : 0;

            for (int block = -blocks_to_draw; block <= blocks_to_draw; ++block) {
                float block_offset = start_pos + (block * total_row_width);
                int visible_item_index = 0;

                for (const auto &display_item: row3_items) {
                    if (is_display_item_done(display_item, settings)) continue;

                    float current_x = block_offset + (visible_item_index * item_full_width);
                    if (current_x + item_full_width < 0 || current_x > window_w) {
                        visible_item_index++;
                        continue;
                    }

                    // --- Render the item ---
                    SDL_Texture *bg_texture = o->adv_bg;
                    std::string icon_path;
                    char name_buf[256] = {0};
                    char progress_buf[256] = {0};

                    switch (display_item.type) {
                        case OverlayDisplayItem::STAT: {
                            auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
                            // Check for completion first, then for the "half-done" state
                            if (stat->done) {
                                bg_texture = o->adv_bg_done;
                            }
                            // A stat is "half-done" if:
                            // 1. It's a complex category with at least one criterion met.
                            // OR
                            // 2. It's a simple category and its progress is greater than zero.
                            else if ((!stat->is_single_stat_category && stat->completed_criteria_count > 0) ||
                                     (stat->is_single_stat_category && stat->criteria_count > 0 && stat->criteria[0]->
                                      progress > 0)) {
                                bg_texture = o->adv_bg_half_done;
                            }
                            icon_path = stat->icon_path;

                            if (stat->criteria_count > 1) {
                                snprintf(name_buf, sizeof(name_buf), "%s (%d / %d)", stat->display_name,
                                         stat->completed_criteria_count, stat->criteria_count);

                                std::vector<int> incomplete_indices;
                                for (int j = 0; j < stat->criteria_count; ++j) {
                                    if (!stat->criteria[j]->done) {
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
                                strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
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
                            if (goal->done) bg_texture = o->adv_bg_done;
                            else if (goal->progress > 0) bg_texture = o->adv_bg_half_done;
                            icon_path = goal->icon_path;
                            strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                            if (goal->goal > 0) {
                                snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", goal->progress, goal->goal);
                            } else if (goal->goal == -1 && !goal->done) {
                                snprintf(progress_buf, sizeof(progress_buf), "(%d)", goal->progress);
                            }
                            break;
                        }
                        case OverlayDisplayItem::MULTISTAGE: {
                            auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
                            if (goal->current_stage >= goal->stage_count - 1) bg_texture = o->adv_bg_done;
                            else if (goal->current_stage > 0) bg_texture = o->adv_bg_half_done;
                            icon_path = goal->icon_path;
                            strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
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

                    float bg_x_offset = (cell_width - ITEM_WIDTH) / 2.0f;
                    SDL_FRect bg_rect = {current_x + bg_x_offset, ROW3_Y_POS, ITEM_WIDTH, ITEM_WIDTH};
                    if (bg_texture) SDL_RenderTexture(o->renderer, bg_texture, nullptr, &bg_rect);

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


                    // Optimized text rendering using cache

                    // --- OPTIMIZATION: Text rendering now uses the cache ---
                    SDL_Texture *name_texture = get_text_texture_from_cache(o, name_buf, text_color);
                    if (name_texture) {
                        float w, h;
                        SDL_GetTextureSize(name_texture, &w, &h);
                        float text_x = current_x + (cell_width - w) / 2.0f;
                        SDL_FRect dest_rect = {text_x, ROW3_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET, w, h};
                        SDL_RenderTexture(o->renderer, name_texture, nullptr, &dest_rect);

                        if (progress_buf[0] != '\0') {
                            SDL_Texture *progress_texture = get_text_texture_from_cache(o, progress_buf, text_color);
                            if (progress_texture) {
                                float pw, ph;
                                SDL_GetTextureSize(progress_texture, &pw, &ph);
                                float p_text_x = current_x + (cell_width - pw) / 2.0f;
                                SDL_FRect p_dest_rect = {p_text_x, ROW3_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + h, pw, ph};
                                SDL_RenderTexture(o->renderer, progress_texture, nullptr, &p_dest_rect);
                            }
                        }
                    }

                    visible_item_index++;
                }
            }
        }
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

        if (o->adv_bg) SDL_DestroyTexture(o->adv_bg);
        if (o->adv_bg_half_done) SDL_DestroyTexture(o->adv_bg_half_done);
        if (o->adv_bg_done) SDL_DestroyTexture(o->adv_bg_done);

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
