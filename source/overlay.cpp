//
// Created by Linus on 24.06.2025.
//

#include "overlay.h" // Has tracker.h
#include "init_sdl.h"
#include "settings_utils.h"
#include "format_utils.h"

#include <cstdio>
#include <cstdlib>
#include <cmath> // Required for roundf()
#include <string>
#include <vector> // Required for collecting items to render

#define SOCIAL_CYCLE_SECONDS 20.0f

// TODO: Add more socials here
const char *SOCIALS[] = {
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
    "streamlabs.com/lnxseus/tip"
};
const int NUM_SOCIALS = sizeof(SOCIALS) / sizeof(char *);

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
        fprintf(stderr, "[OVERLAY] Error allocating memory for overlay.\n");
        return false;
    }

    // temp variable to not dereference over and over again
    Overlay *o = *overlay;


    o->social_media_timer = 0.0f;
    o->current_social_index = 0;
    o->text_engine = nullptr;

    o->scroll_offset_row1 = 0.0f;
    o->scroll_offset_row2 = 0.0f;
    o->scroll_offset_row3 = 0.0f;
    o->start_index_row1 = 0;
    o->start_index_row2 = 0;
    o->start_index_row3 = 0;

    // TODO: Remove
    // o->row1_total_width = 0.0f;
    // o->row2_total_width = 0.0f;
    // o->row3_total_width = 0.0f;
    //
    // // Initialize new state for Row 1
    // o->row1_items = nullptr;
    // o->row1_item_count = 0;
    // o->row1_item_capacity = 0;
    //
    // // Initialize new state for Row 2
    // o->row2_items = nullptr;
    // o->row2_item_count = 0;
    // o->row2_item_capacity = 0;
    //
    // // Initialize new state for Row 3
    // o->row3_items = nullptr;
    // o->row3_item_count = 0;
    // o->row3_item_capacity = 0;

    o->texture_cache = nullptr;
    o->texture_cache_count = 0;
    o->texture_cache_capacity = 0;
    o->anim_cache = nullptr;
    o->anim_cache_count = 0;
    o->anim_cache_capacity = 0;

    // Create the SDL window and renderer
    if (!overlay_init_sdl(o, settings)) {
        overlay_free(overlay, settings);
        return false;
    }

    o->text_engine = TTF_CreateRendererTextEngine(o->renderer);
    if (!o->text_engine) {
        fprintf(stderr, "[OVERLAY] Failed to create text engine: %s\n", SDL_GetError());
        overlay_free(overlay, settings);
        return false;
    }

    // Load global background textures using the overlay's renderer
    o->adv_bg = load_texture_with_scale_mode(o->renderer, "resources/gui/advancement_background.png",
                                             SDL_SCALEMODE_NEAREST);
    o->adv_bg_half_done = load_texture_with_scale_mode(o->renderer,
                                                       "resources/gui/advancement_background_half_done.png",
                                                       SDL_SCALEMODE_NEAREST);
    o->adv_bg_done = load_texture_with_scale_mode(o->renderer, "resources/gui/advancement_background_done.png",
                                                  SDL_SCALEMODE_NEAREST);
    if (!o->adv_bg || !o->adv_bg_half_done || !o->adv_bg_done) {
        fprintf(stderr, "[OVERLAY] Failed to load advancement background textures.\n");
        overlay_free(overlay, settings);
        return false;
    }

    // Make font HiDPI aware by loading it at a base point size (e.g., 24).
    // SDL_ttf will automatically scale it correctly on any monitor at render time.
    const float base_font_size = 24.0f;
    o->font = TTF_OpenFont("resources/fonts/Minecraft.ttf", base_font_size);
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
    (void) deltaTime;

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


// Helper to check the 'done' status of any displayable item type
static bool is_display_item_done(const OverlayDisplayItem &display_item, const AppSettings *settings) {
    if (!display_item.item_ptr) return true;

    // If "Remove Completed Goals" is off, nothing is ever considered "done" for hiding purposes.
    if (!settings->remove_completed_goals) return false;

    switch (display_item.type) {
        case OverlayDisplayItem::ADVANCEMENT: {
            auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
            return (adv->criteria_count > 0 && adv->all_template_criteria_met) || (
                       adv->criteria_count == 0 && adv->done);
        }
        case OverlayDisplayItem::UNLOCK:
            return static_cast<TrackableItem *>(display_item.item_ptr)->done;
        case OverlayDisplayItem::STAT:
            return static_cast<TrackableCategory *>(display_item.item_ptr)->done;
        case OverlayDisplayItem::CUSTOM:
            return static_cast<TrackableItem *>(display_item.item_ptr)->done;
        case OverlayDisplayItem::MULTISTAGE: {
            auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
            return goal->current_stage >= goal->stage_count - 1; // Final stage not counted (done-stage)
        }
    }
    return true;
}

void overlay_update(Overlay *o, float *deltaTime, const Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data) return;

    // --- Gather Items for Each Row ---
    std::vector<std::pair<TrackableItem *, TrackableCategory *> > row1_items;
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *cat = t->template_data->advancements[i];
        for (int j = 0; j < cat->criteria_count; j++) row1_items.push_back({cat->criteria[j], cat});
    }
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *cat = t->template_data->stats[i];
        for (int j = 0; j < cat->criteria_count; j++) row1_items.push_back({cat->criteria[j], cat});
    }

    std::vector<OverlayDisplayItem> row2_items; // Advancements & Unlocks
    for (int i = 0; i < t->template_data->advancement_count; ++i) row2_items.push_back({
        t->template_data->advancements[i], OverlayDisplayItem::ADVANCEMENT
    });
    for (int i = 0; i < t->template_data->unlock_count; ++i) row2_items.push_back({
        t->template_data->unlocks[i], OverlayDisplayItem::UNLOCK
    });

    // --- Update Animation State ---
    const float base_scroll_speed = 60.0f;
    float speed_multiplier = settings->overlay_scroll_speed;
    if (settings->overlay_animation_speedup) speed_multiplier *= OVERLAY_SPEEDUP_FACTOR;
    float scroll_delta = -(base_scroll_speed * speed_multiplier * (*deltaTime));

    // --- Validate Indices ---
    if (!row1_items.empty() && static_cast<size_t>(o->start_index_row1) >= row1_items.size()) o->start_index_row1 = 0;
    if (!row2_items.empty() && static_cast<size_t>(o->start_index_row2) >= row2_items.size()) o->start_index_row2 = 0;

    // --- Row 1 Update Logic ---
    if (!row1_items.empty()) {
        const float item_full_width = 48.0f + 8.0f; // ROW1_ICON_SIZE + ROW1_SPACING
        o->scroll_offset_row1 += scroll_delta;
        int items_scrolled = floor(fabs(o->scroll_offset_row1) / item_full_width);
        if (items_scrolled > 0) {
            o->scroll_offset_row1 = fmod(o->scroll_offset_row1, item_full_width);
            if (scroll_delta > 0) {
                for (int i = 0; i < items_scrolled; ++i) {
                    size_t loop_guard = 0;
                    do {
                        o->start_index_row1 = (o->start_index_row1 + 1) % row1_items.size();
                        loop_guard++;
                    } while (row1_items[o->start_index_row1].first->done && loop_guard < row1_items.size() * 2);
                }
            } else {
                for (int i = 0; i < items_scrolled; ++i) {
                    size_t loop_guard = 0;
                    do {
                        o->start_index_row1 = (o->start_index_row1 - 1 + row1_items.size()) % row1_items.size();
                        loop_guard++;
                    } while (row1_items[o->start_index_row1].first->done && loop_guard < row1_items.size() * 2);
                }
            }
        }
    }

        // --- Row 2 Update Logic (Dynamic Width) ---
    if (!row2_items.empty()) {
        float max_text_width = 0.0f;
        // First, calculate the max width required by any visible item in the row.
        // This is necessary to know how far to scroll before advancing the index.
        for (const auto& display_item : row2_items) {
            if (is_display_item_done(display_item, settings)) continue;

            char name_buf[256] = {0};
            char progress_buf[64] = {0};

            if (display_item.type == OverlayDisplayItem::ADVANCEMENT) {
                auto* adv = static_cast<TrackableCategory*>(display_item.item_ptr);
                strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                if (adv->criteria_count > 0) snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", adv->completed_criteria_count, adv->criteria_count);
            } else if (display_item.type == OverlayDisplayItem::UNLOCK) {
                auto* unlock = static_cast<TrackableItem*>(display_item.item_ptr);
                strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
            }

            TTF_Text* temp_text = TTF_CreateText(o->text_engine, o->font, name_buf, 0);
            if (temp_text) {
                int w; TTF_GetTextSize(temp_text, &w, nullptr); max_text_width = fmaxf(max_text_width, (float)w); TTF_DestroyText(temp_text);
            }
            if (progress_buf[0] != '\0') {
                temp_text = TTF_CreateText(o->text_engine, o->font, progress_buf, 0);
                if (temp_text) {
                    int w; TTF_GetTextSize(temp_text, &w, nullptr); max_text_width = fmaxf(max_text_width, (float)w); TTF_DestroyText(temp_text);
                }
            }
        }

        const float cell_width = fmaxf(96.0f, max_text_width);
        const float item_full_width = cell_width + 16.0f;

        o->scroll_offset_row2 += scroll_delta;
        int items_scrolled = floor(fabs(o->scroll_offset_row2) / item_full_width);
        if (items_scrolled > 0) {
            o->scroll_offset_row2 = fmod(o->scroll_offset_row2, item_full_width);
            if (scroll_delta > 0) { // Moving RTL
                for (int i = 0; i < items_scrolled; ++i) {
                    size_t loop_guard = 0;
                    do {
                        o->start_index_row2 = (o->start_index_row2 + 1) % row2_items.size();
                        loop_guard++;
                    } while (is_display_item_done(row2_items[o->start_index_row2], settings) && loop_guard < row2_items.size() * 2);
                }
            } else { // Moving LTR
                for (int i = 0; i < items_scrolled; ++i) {
                    size_t loop_guard = 0;
                    do {
                        o->start_index_row2 = (o->start_index_row2 - 1 + row2_items.size()) % row2_items.size();
                        loop_guard++;
                    } while (is_display_item_done(row2_items[o->start_index_row2], settings) && loop_guard < row2_items.size() * 2);
                }
            }
        }
    }



    // if (!row2_items.empty()) {
    //     // We must calculate the row's current uniform width to know how far to scroll before advancing the index.
    //     float max_text_width = 0.0f;
    //     for (const auto& display_item : row2_items) {
    //         if (is_display_item_done(display_item, settings)) continue;
    //         // ... (code to calculate max_text_width, same as in render function) ...
    //     }
    //     const float cell_width = fmaxf(96.0f, max_text_width);
    //     const float item_full_width = cell_width + 16.0f;
    //
    //     o->scroll_offset_row2 += scroll_delta;
    //     int items_scrolled = floor(fabs(o->scroll_offset_row2) / item_full_width);
    //     if (items_scrolled > 0) {
    //         o->scroll_offset_row2 = fmod(o->scroll_offset_row2, item_full_width);
    //         if (scroll_delta < 0) { // Moving RTL
    //             for (int i = 0; i < items_scrolled; ++i) {
    //                 size_t loop_guard = 0;
    //                 do {
    //                     o->start_index_row2 = (o->start_index_row2 + 1) % row2_items.size();
    //                     loop_guard++;
    //                 } while (is_display_item_done(row2_items[o->start_index_row2], settings) && loop_guard < row2_items.size() * 2);
    //             }
    //         } else { // Moving LTR
    //             for (int i = 0; i < items_scrolled; ++i) {
    //                 size_t loop_guard = 0;
    //                 do {
    //                     o->start_index_row2 = (o->start_index_row2 - 1 + row2_items.size()) % row2_items.size();
    //                     loop_guard++;
    //                 } while (is_display_item_done(row2_items[o->start_index_row2], settings) && loop_guard < row2_items.size() * 2);
    //             }
    //         }
    //     }
    // }

    // TODO: Remove
    // if (!t || !t->template_data) return;
    //
    // // --- Gather Items for Each Row ---
    // std::vector<std::pair<TrackableItem*, TrackableCategory*>> row1_items;
    // for (int i = 0; i < t->template_data->advancement_count; i++) {
    //     TrackableCategory *cat = t->template_data->advancements[i];
    //     for (int j = 0; j < cat->criteria_count; j++) {
    //         row1_items.push_back({cat->criteria[j], cat});
    //     }
    // }
    // for (int i = 0; i < t->template_data->stat_count; i++) {
    //     TrackableCategory *cat = t->template_data->stats[i];
    //     for (int j = 0; j < cat->criteria_count; j++) {
    //         row1_items.push_back({cat->criteria[j], cat});
    //     }
    // }
    //
    // // Note: Rows 2 & 3 item lists are gathered in the render function to calculate dynamic widths.
    //
    // // --- Update Animation State ---
    // const float base_scroll_speed = 60.0f;
    // float speed_multiplier = settings->overlay_scroll_speed;
    // if (settings->overlay_animation_speedup) speed_multiplier *= OVERLAY_SPEEDUP_FACTOR;
    // float scroll_delta = -(base_scroll_speed * speed_multiplier * (*deltaTime));
    //
    // // --- Validate Indices to prevent crashes when templates change ---
    // if (!row1_items.empty() && static_cast<size_t>(o->start_index_row1) >= row1_items.size()) {
    //     o->start_index_row1 = 0;
    // }
    // // Validation for rows 2 & 3 will happen in the render function after their item lists are built.
    //
    // // --- Row 1 Update Logic (fixed item width) ---
    // if (!row1_items.empty()) {
    //     const float item_full_width = 48.0f + 8.0f; // ROW1_ICON_SIZE + ROW1_SPACING
    //     o->scroll_offset_row1 += scroll_delta;
    //
    //     int items_scrolled = floor(fabs(o->scroll_offset_row1) / item_full_width);
    //     if (items_scrolled > 0) {
    //         o->scroll_offset_row1 = fmod(o->scroll_offset_row1, item_full_width);
    //
    //         if (scroll_delta > 0) { // Moving RTL
    //             for (int i = 0; i < items_scrolled; ++i) {
    //                 size_t loop_guard = 0;
    //                 do {
    //                     o->start_index_row1 = (o->start_index_row1 + 1) % row1_items.size();
    //                     loop_guard++;
    //                 } while (row1_items[o->start_index_row1].first->done && loop_guard < row1_items.size() * 2);
    //             }
    //         } else { // Moving LTR
    //             for (int i = 0; i < items_scrolled; ++i) {
    //                 size_t loop_guard = 0;
    //                 do {
    //                     o->start_index_row1 = (o->start_index_row1 - 1 + row1_items.size()) % row1_items.size();
    //                     loop_guard++;
    //                 } while (row1_items[o->start_index_row1].first->done && loop_guard < row1_items.size() * 2);
    //             }
    //         }
    //     }
    // }

    // Row 3 doesn't disappear
    o->scroll_offset_row3 -= scroll_delta;

    // --- Cycle through social media text ---
    o->social_media_timer += *deltaTime;
    if (o->social_media_timer >= SOCIAL_CYCLE_SECONDS) {
        o->social_media_timer -= SOCIAL_CYCLE_SECONDS;
        o->current_social_index = (o->current_social_index + 1) % NUM_SOCIALS;
    }
}

// TODO: Remove
// void overlay_update(Overlay *o, float *deltaTime, const Tracker *t, const AppSettings *settings) {
//     if (!t || !t->template_data) return;
//
//
//     // --- Gather Items for Each Row ---
//     // We rebuild these lists each update to react to game progress.
//
//     // Row 1: All individual criteria and sub-stats
//     std::vector<std::pair<TrackableItem *, TrackableCategory *> > row1_items;
//     for (int i = 0; i < t->template_data->advancement_count; i++) {
//         TrackableCategory *cat = t->template_data->advancements[i];
//         for (int j = 0; j < cat->criteria_count; j++) {
//             row1_items.push_back({cat->criteria[j], cat});
//         }
//     }
//     for (int i = 0; i < t->template_data->stat_count; i++) {
//         TrackableCategory *cat = t->template_data->stats[i];
//         for (int j = 0; j < cat->criteria_count; j++) {
//             row1_items.push_back({cat->criteria[j], cat});
//         }
//     }
//
//     // // Row 2: Parent advancements, stats, and unlocks
//     // std::vector<void *> row2_items; // Using void* for heterogeneous types
//     // for (int i = 0; i < t->template_data->advancement_count; ++i)
//     //     row2_items.push_back(
//     //         t->template_data->advancements[i]);
//     // for (int i = 0; i < t->template_data->unlock_count; ++i) row2_items.push_back(t->template_data->unlocks[i]);
//     // for (int i = 0; i < t->template_data->stat_count; ++i) row2_items.push_back(t->template_data->stats[i]);
//     //
//     // // Row 3: Custom and Multi-Stage Goals
//     // std::vector<void *> row3_items;
//     // for (int i = 0; i < t->template_data->custom_goal_count; ++i)
//     //     row3_items.push_back(
//     //         t->template_data->custom_goals[i]);
//     // for (int i = 0; i < t->template_data->multi_stage_goal_count; ++i)
//     //     row3_items.push_back(
//     //         t->template_data->multi_stage_goals[i]);
//
//     std::vector<OverlayDisplayItem> row2_items; // Advancements & Unlocks
//     for (int i = 0; i < t->template_data->advancement_count; ++i) row2_items.push_back({t->template_data->advancements[i], OverlayDisplayItem::ADVANCEMENT});
//     for (int i = 0; i < t->template_data->unlock_count; ++i) row2_items.push_back({t->template_data->unlocks[i], OverlayDisplayItem::UNLOCK});
//
//     std::vector<OverlayDisplayItem> row3_items; // Stats, Custom, Multi-Stage
//     for (int i = 0; i < t->template_data->stat_count; ++i) row3_items.push_back({t->template_data->stats[i], OverlayDisplayItem::STAT});
//     for (int i = 0; i < t->template_data->custom_goal_count; ++i) row3_items.push_back({t->template_data->custom_goals[i], OverlayDisplayItem::CUSTOM});
//     for (int i = 0; i < t->template_data->multi_stage_goal_count; ++i) row3_items.push_back({t->template_data->multi_stage_goals[i], OverlayDisplayItem::MULTISTAGE});
//
//     // --- Update Animation State ---
//     const float base_scroll_speed = 60.0f;
//     float speed_multiplier = settings->overlay_scroll_speed;
//     if (settings->overlay_animation_speedup) speed_multiplier *= OVERLAY_SPEEDUP_FACTOR;
//     float scroll_delta = -(base_scroll_speed * speed_multiplier * (*deltaTime)); // minus is reversing the scroll
//
//     // --- Validate Indices to prevent crashes ---
//     if (!row1_items.empty() && static_cast<size_t>(o->start_index_row1) >= row1_items.size()) o->start_index_row1 = 0;
//     if (!row2_items.empty() && static_cast<size_t>(o->start_index_row2) >= row2_items.size()) o->start_index_row2 = 0;
//     if (!row3_items.empty() && static_cast<size_t>(o->start_index_row3) >= row3_items.size()) o->start_index_row3 = 0;
//
//     // For simplicity, all rows now use a basic scroll. The rendering logic will handle the seamless loop.
//     o->scroll_offset_row1 += scroll_delta;
//     o->scroll_offset_row2 += scroll_delta;
//     o->scroll_offset_row3 += scroll_delta;
//
//     // Cycle through social media text
//     o->social_media_timer += *deltaTime;
//     if (o->social_media_timer >= SOCIAL_CYCLE_SECONDS) {
//         o->social_media_timer -= SOCIAL_CYCLE_SECONDS;
//         o->current_social_index = (o->current_social_index + 1) % NUM_SOCIALS;
//     }
//
//
//     // Reset start index if it becomes invalid after the item list is rebuilt (changing template (with fewer items))
//     if (!row1_items.empty() && static_cast<size_t>(o->start_index_row1) >= row1_items.size()) {
//         o->start_index_row1 = 0; // Reset index if it's out of bounds
//     }
//
//     // Row 1 Update Logic
//
//     if (!row1_items.empty()) {
//         const float item_full_width = 48.0f + 8.0f; // ROW1_ICON_SIZE + ROW1_SPACING
//         o->scroll_offset_row1 += scroll_delta;
//
//         int items_scrolled = floor(fabs(o->scroll_offset_row1) / item_full_width);
//         if (items_scrolled > 0) {
//             o->scroll_offset_row1 = fmod(o->scroll_offset_row1, item_full_width);
//
//             // --- FIX: Handle index update based on scroll direction ---
//             if (scroll_delta > 0) { // Moving Right-to-Left, find NEXT item
//                 for (int i = 0; i < items_scrolled; ++i) {
//                     size_t loop_guard = 0;
//                     do {
//                         o->start_index_row1 = (o->start_index_row1 + 1) % row1_items.size();
//                         loop_guard++;
//                     } while (row1_items[o->start_index_row1].first->done && loop_guard < row1_items.size() * 2);
//                 }
//             } else { // Moving Left-to-Right, find PREVIOUS item
//                 for (int i = 0; i < items_scrolled; ++i) {
//                     size_t loop_guard = 0;
//                     do {
//                         // Correct way to decrement with wrapping for unsigned and negative results
//                         o->start_index_row1 = (o->start_index_row1 - 1 + row1_items.size()) % row1_items.size();
//                         loop_guard++;
//                     } while (row1_items[o->start_index_row1].first->done && loop_guard < row1_items.size() * 2);
//                 }
//             }
//         }
//     }
//
//     // TODO: Remove
//     // if (!row1_items.empty()) {
//     //     const float item_full_width = 48.0f + 8.0f; // ROW1_ICON_SIZE + ROW1_SPACING
//     //     o->scroll_offset_row1 += scroll_delta;
//     //
//     //     int items_scrolled = floor(fabs(o->scroll_offset_row1) / item_full_width);
//     //     if (items_scrolled > 0) {
//     //         o->scroll_offset_row1 = fmod(o->scroll_offset_row1, item_full_width);
//     //         for (int i = 0; i < items_scrolled; ++i) {
//     //             size_t loop_guard = 0;
//     //             do {
//     //                 o->start_index_row1 = (o->start_index_row1 + 1) % row1_items.size();
//     //                 loop_guard++;
//     //             } while (row1_items[o->start_index_row1].first->done && loop_guard < row1_items.size() * 2);
//     //         }
//     //     }
//     // }
//
//
//     // TODO: Remove
//     //
//     //
//     // // Check for items whose gaps have scrolled off-screen and mark them for removal
//     // int window_w;
//     // SDL_GetWindowSizeInPixels(o->window, &window_w, nullptr);
//     // const float ROW1_ICON_SIZE = 48.0f;
//     // const float ROW1_SPACING = 8.0f;
//     // float item_full_width = ROW1_ICON_SIZE + ROW1_SPACING;
//     // float current_x_pos = -o->scroll_offset_row1;
//     //
//     // for (int i = 0; i < o->row1_item_count; i++) {
//     //     Row1Item *item = &o->row1_items[i];
//     //
//     //     // We only care about items that are leaving a gap
//     //     if (item->state != ITEM_COMPLETED) {
//     //         current_x_pos += item_full_width;
//     //         continue;
//     //     }
//     //
//     //     bool is_off_screen = false;
//     //     if (scroll_delta > 0) {
//     //         // Moving Left to Right (positive speed)
//     //         // Off-screen if its right edge has passed the left side of the window
//     //         is_off_screen = (current_x_pos + item_full_width) < 0;
//     //     } else if (scroll_delta < 0) {
//     //         // Moving Right to Left (negative speed)
//     //         // Off-screen if its left edge has passed the right side of the window
//     //         is_off_screen = current_x_pos > window_w;
//     //     }
//     //
//     //     if (is_off_screen) {
//     //         item->state = ITEM_REMOVED;
//     //     }
//     //     current_x_pos += item_full_width;
//     // }
// }

void overlay_render(Overlay *o, const Tracker *t, const AppSettings *settings) {
    SDL_SetRenderDrawColor(o->renderer, settings->overlay_bg_color.r, settings->overlay_bg_color.g,
                           settings->overlay_bg_color.b, settings->overlay_bg_color.a);
    SDL_RenderClear(o->renderer);

    // Render Progress Text (Top Bar)
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

        snprintf(final_buffer, sizeof(final_buffer), "%s | %s", info_buffer, SOCIALS[o->current_social_index]);

        SDL_Color text_color = {
            settings->overlay_text_color.r, settings->overlay_text_color.g, settings->overlay_text_color.b,
            settings->overlay_text_color.a
        };
        // Blended is higher quality than Solid
        SDL_Surface *text_surface = TTF_RenderText_Blended(o->font, final_buffer, 0, text_color);
        if (text_surface) {
            SDL_Texture *text_texture = SDL_CreateTextureFromSurface(o->renderer, text_surface);
            if (text_texture) {
                SDL_SetTextureScaleMode(text_texture, SDL_SCALEMODE_NEAREST); // Linear scaling for overlay
                int overlay_w;
                SDL_GetWindowSize(o->window, &overlay_w, nullptr);
                const float padding = 10.0f;
                SDL_FRect dest_rect = {padding, padding, (float) text_surface->w, (float) text_surface->h};
                switch (settings->overlay_progress_text_align) {
                    case OVERLAY_PROGRESS_TEXT_ALIGN_CENTER:
                        dest_rect.x = ((float) overlay_w - (float) text_surface->w) / 2.0f;
                        break;
                    case OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT:
                        dest_rect.x = (float) overlay_w - (float) text_surface->w - padding;
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

    if (!t || !t->template_data) {
        SDL_RenderPresent(o->renderer);
        return;
    }

    int window_w;
    SDL_GetWindowSizeInPixels(o->window, &window_w, nullptr);

    // --- ROW 1: Criteria & Sub-stats Icons ---
    {
        const float ROW1_Y_POS = 52.0f;
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
            for (int j = 0; j < cat->criteria_count; j++) {
                row1_items.push_back({cat->criteria[j], cat});
            }
        }

        // TODO: Remove
        // // Explicitly count visible items and only draw them
        // size_t visible_item_count = 0;
        // for (const auto& pair : row1_items) {
        //     if (!pair.first->done) {
        //         visible_item_count++;
        //     }
        // }


        if (!row1_items.empty()) {
            // Determine how many slots to draw based on screen width.
            int items_to_draw = ceil((float) window_w / item_full_width) + 2; // To render items off screen
            // Only draw as many items as are visible, but repeat them if needed to fill the screen (marquee effect).

            // Draw either enough slots to fill the screen OR only the visible items, whichever is smaller.
            // This prevents drawing empty, superfluous slots.

            // int items_to_draw = std::min(geometric_slots_needed, (int)visible_item_count);
            // int items_to_draw = geometric_slots_needed;
            int current_item_idx = o->start_index_row1;

            // printf("\n--- NEW RENDER FRAME ---\n"); // DEBUG: Mark the start of a new render pass

            for (int k = 0; k < items_to_draw; k++) {
                // -1.0f to offset rendering one icon width to the left (make it hidden)
                float x_pos = ((k - 1.0f) * item_full_width) - o->scroll_offset_row1;

                // --- DEBUG: Print current state for this slot ---
                // printf("--- k = %d ---\n", k);

                // TODO: Visual debugger (keep this under debug print) later
                // // Draw a colored box for each slot to visualize the grid
                // SDL_SetRenderDrawBlendMode(o->renderer, SDL_BLENDMODE_BLEND);
                // if (k % 2 == 0) {
                //     SDL_SetRenderDrawColor(o->renderer, 255, 0, 0, 50); // Red for even slots
                // } else {
                //     SDL_SetRenderDrawColor(o->renderer, 0, 0, 255, 50); // Blue for odd slots
                // }
                // SDL_FRect debug_rect = {x_pos, ROW1_Y_POS, ROW1_ICON_SIZE, ROW1_ICON_SIZE};
                // SDL_RenderFillRect(o->renderer, &debug_rect);
                // // --- END DEBUG DRAWING ---

                // TODO: Remove
                // float x_pos;
                // if (settings->overlay_scroll_speed >= 0) { // Left to Right
                //     x_pos = window_w - ((k + 1) * item_full_width) + o->scroll_offset_row1;
                // } else { // Right to left
                //     x_pos = (k * item_full_width) - o->scroll_offset_row1;
                // }

                // Find the next visible item
                size_t loop_guard = 0;
                while (row1_items[current_item_idx].first->done) {
                    // printf("  - Skipping idx %d (done=true)\n", current_item_idx);
                    current_item_idx = (current_item_idx + 1) % row1_items.size();
                    loop_guard++;
                    if (loop_guard > row1_items.size()) {
                        // printf("  - SAFETY BREAK HIT: All items appear to be done.\n");
                        goto end_row1_render; // All items are done
                    }
                }

                // --- DEBUG: Print the result of the search ---
                // printf("  - Settled on idx %d to draw in slot %d\n", current_item_idx, k);

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
                    fprintf(
                        stderr,
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

                // TODO: debug --- START DEBUG DRAWING ---
                // // --- DEBUG: Draw the index and status on the icon ---
                // char status_buf[16];
                // snprintf(status_buf, sizeof(status_buf), "%d%c", current_item_idx, item_to_render->done ? 'D' : 'V');
                // TTF_Text* status_text = TTF_CreateText(o->text_engine, o->font, status_buf, 0);
                // if (status_text) {
                //     TTF_SetTextColor(status_text, 255, 255, 0, 255); // Yellow
                //     TTF_DrawRendererText(status_text, x_pos + 5, ROW1_Y_POS + 5);
                //     TTF_DestroyText(status_text);
                // }
                // // --- END DEBUG ---

                // Advance to the next item for the next screen slot
                current_item_idx = (current_item_idx + 1) % row1_items.size();
            }
        }
    end_row1_render:;

        // TODO: debug --- START DEBUG DRAWING ---
        // // Display current state variables on screen
        // char state_buf[128];
        // snprintf(state_buf, sizeof(state_buf), "Start Index: %d | Scroll Offset: %.2f", o->start_index_row1, o->scroll_offset_row1);
        // TTF_Text* state_text = TTF_CreateText(o->text_engine, o->font, state_buf, 0);
        // if (state_text) {
        //     int window_w, window_h;
        //     SDL_GetWindowSize(o->window, &window_w, &window_h);
        //     TTF_SetTextColor(state_text, 255, 255, 255, 255);
        //     TTF_DrawRendererText(state_text, 10, window_h - 40.0f); // Draw at bottom-left
        //     TTF_DestroyText(state_text);
        // }
        // // --- END DEBUG DRAWING ---
    }

    // --- ROW 2: Advancements & Unlocks ---
    {
        const float ROW2_Y_POS = 108.0f;
        const float ITEM_WIDTH = 96.0f;
        const float ITEM_SPACING = 16.0f;
        const float TEXT_Y_OFFSET = 4.0f;

        std::vector<OverlayDisplayItem> row2_items;
        for (int i = 0; i < t->template_data->advancement_count; ++i) row2_items.push_back({t->template_data->advancements[i], OverlayDisplayItem::ADVANCEMENT});
        for (int i = 0; i < t->template_data->unlock_count; ++i) row2_items.push_back({t->template_data->unlocks[i], OverlayDisplayItem::UNLOCK});

        size_t visible_item_count = 0;
        for (const auto& item : row2_items) {
            if (!is_display_item_done(item, settings)) {
                visible_item_count++;
            }
        }

        if (visible_item_count > 0) {
            float max_text_width = 0.0f;
            // First pass: calculate the max width required by any visible item in the row
            for (const auto& display_item : row2_items) {
                if (is_display_item_done(display_item, settings)) continue;
                char name_buf[256] = {0}, progress_buf[64] = {0};

                if (display_item.type == OverlayDisplayItem::ADVANCEMENT) {
                    auto* adv = static_cast<TrackableCategory*>(display_item.item_ptr);
                    strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                    if (adv->criteria_count > 0) snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", adv->completed_criteria_count, adv->criteria_count);
                } else if (display_item.type == OverlayDisplayItem::UNLOCK) {
                    auto* unlock = static_cast<TrackableItem*>(display_item.item_ptr);
                    strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
                }

                TTF_Text* temp_text = TTF_CreateText(o->text_engine, o->font, name_buf, 0);
                if (temp_text) {
                    int w; TTF_GetTextSize(temp_text, &w, nullptr); max_text_width = fmaxf(max_text_width, (float)w); TTF_DestroyText(temp_text);
                }
                if (progress_buf[0] != '\0') {
                    temp_text = TTF_CreateText(o->text_engine, o->font, progress_buf, 0);
                    if (temp_text) {
                        int w; TTF_GetTextSize(temp_text, &w, nullptr); max_text_width = fmaxf(max_text_width, (float)w); TTF_DestroyText(temp_text);
                    }
                }
            }

            const float cell_width = fmaxf(ITEM_WIDTH, max_text_width);
            const float item_full_width = cell_width + ITEM_SPACING;

            int items_to_draw = (item_full_width > 0) ? ceil((float)window_w / item_full_width) + 2 : 0;
            int current_item_idx = o->start_index_row2;

            for (int k = 0; k < items_to_draw; k++) {
                size_t loop_guard = 0;
                while (is_display_item_done(row2_items[current_item_idx], settings)) {
                    current_item_idx = (current_item_idx + 1) % row2_items.size();
                    loop_guard++;
                    if (loop_guard > row2_items.size()) goto end_row2_render;
                }

                const auto& display_item = row2_items[current_item_idx];
                float x_pos = ((k - 1) * item_full_width) - o->scroll_offset_row2;

                // --- Render the item ---
                SDL_Texture* bg_texture = o->adv_bg;
                std::string icon_path;
                char name_buf[256] = {0}, progress_buf[64] = {0};

                switch (display_item.type) {
                    case OverlayDisplayItem::ADVANCEMENT: {
                        auto* adv = static_cast<TrackableCategory*>(display_item.item_ptr);
                        if (adv->done) bg_texture = o->adv_bg_done;
                        else if (adv->completed_criteria_count > 0) bg_texture = o->adv_bg_half_done;
                        icon_path = adv->icon_path;
                        strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                        if (adv->criteria_count > 0) {
                            snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", adv->completed_criteria_count, adv->criteria_count);
                        }
                        break;
                    }
                    case OverlayDisplayItem::UNLOCK: {
                        auto* unlock = static_cast<TrackableItem*>(display_item.item_ptr);
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
                SDL_Texture* tex = nullptr;
                AnimatedTexture* anim_tex = nullptr;
                if (!icon_path.empty() && strstr(icon_path.c_str(), ".gif")) {
                    anim_tex = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count, &o->anim_cache_capacity, icon_path.c_str(), SDL_SCALEMODE_NEAREST);
                } else if (!icon_path.empty()) {
                    tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count, &o->texture_cache_capacity, icon_path.c_str(), SDL_SCALEMODE_NEAREST);
                }
                render_texture_with_alpha(o->renderer, tex, anim_tex, &icon_rect, 255);

                TTF_Text* name_text = TTF_CreateText(o->text_engine, o->font, name_buf, 0);
                if (name_text) {
                    int w, h;
                    TTF_GetTextSize(name_text, &w, &h);
                    float text_x = x_pos + (cell_width - (float)w) / 2.0f;
                    TTF_SetTextColor(name_text, settings->overlay_text_color.r, settings->overlay_text_color.g, settings->overlay_text_color.b, 255);
                    TTF_DrawRendererText(name_text, text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET);

                    if (progress_buf[0] != '\0') {
                        TTF_Text* progress_text = TTF_CreateText(o->text_engine, o->font, progress_buf, 0);
                        if (progress_text) {
                            TTF_GetTextSize(progress_text, &w, nullptr);
                            text_x = x_pos + (cell_width - (float)w) / 2.0f;
                            TTF_SetTextColor(progress_text, settings->overlay_text_color.r, settings->overlay_text_color.g, settings->overlay_text_color.b, 255);
                            TTF_DrawRendererText(progress_text, text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + (float)h);
                            TTF_DestroyText(progress_text);
                        }
                    }
                    TTF_DestroyText(name_text);
                }

                // FIX: This is the correct place to advance to the next item for the next screen slot.
                current_item_idx = (current_item_idx + 1) % row2_items.size();
            }
        }
        end_row2_render:; // FIX: Corrected goto label name
    }

    // TODO: Remove
    // // --- ROW 2: Advancements & Unlocks ---
    // {
    //     const float ROW2_Y_POS = 108.0f;
    //     const float ITEM_WIDTH = 96.0f;
    //     const float ITEM_SPACING = 16.0f;
    //     const float TEXT_Y_OFFSET = 4.0f;
    //
    //     std::vector<OverlayDisplayItem> row2_items;
    //     for (int i = 0; i < t->template_data->advancement_count; ++i) row2_items.push_back({
    //         t->template_data->advancements[i], OverlayDisplayItem::ADVANCEMENT
    //     });
    //     for (int i = 0; i < t->template_data->unlock_count; ++i) row2_items.push_back({
    //         t->template_data->unlocks[i], OverlayDisplayItem::UNLOCK
    //     });
    //
    //     size_t visible_item_count = 0;
    //     for (const auto &item: row2_items) {
    //         if (!is_display_item_done(item, settings)) {
    //             visible_item_count++;
    //         }
    //     }
    //
    //     if (visible_item_count > 0) {
    //         float max_text_width = 0.0f;
    //         // First pass: calculate the max width required by any visible item in the row
    //         for (const auto &display_item: row2_items) {
    //             if (is_display_item_done(display_item, settings)) continue;
    //
    //             char name_buf[256] = {0};
    //             char progress_buf[64] = {0};
    //
    //             // TODO: Remove?
    //             // switch (display_item.type) {
    //             //     case OverlayDisplayItem::ADVANCEMENT: {
    //             //         auto* adv = static_cast<TrackableCategory*>(display_item.item_ptr);
    //             //         strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
    //             //         if (adv->criteria_count > 0) {
    //             //             snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", adv->completed_criteria_count, adv->criteria_count);
    //             //         }
    //             //         break;
    //             //     }
    //             //     case OverlayDisplayItem::UNLOCK: {
    //             //         auto* unlock = static_cast<TrackableItem*>(display_item.item_ptr);
    //             //         strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
    //             //         break;
    //             //     }
    //             //     default: break;
    //             // }
    //             //
    //             // TTF_Text* temp_text = TTF_CreateText(o->text_engine, o->font, name_buf, 0);
    //             // if (temp_text) {
    //             //     int w, h;
    //             //     TTF_GetTextSize(temp_text, &w, &h);
    //             //     max_text_width = fmaxf(max_text_width, (float)w);
    //             //     TTF_DestroyText(temp_text);
    //             // }
    //             // if (progress_buf[0] != '\0') {
    //             //     temp_text = TTF_CreateText(o->text_engine, o->font, progress_buf, 0);
    //             //     if (temp_text) {
    //             //         int w, h;
    //             //         TTF_GetTextSize(temp_text, &w, &h);
    //             //         max_text_width = fmaxf(max_text_width, (float)w);
    //             //         TTF_DestroyText(temp_text);
    //             //     }
    //             // }
    //
    //
    //             if (display_item.type == OverlayDisplayItem::ADVANCEMENT) {
    //                 auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
    //                 strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
    //                 if (adv->criteria_count > 0) snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
    //                                                       adv->completed_criteria_count, adv->criteria_count);
    //             } else if (display_item.type == OverlayDisplayItem::UNLOCK) {
    //                 auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
    //                 strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
    //             }
    //
    //             TTF_Text *temp_text = TTF_CreateText(o->text_engine, o->font, name_buf, 0);
    //             if (temp_text) {
    //                 int w;
    //                 TTF_GetTextSize(temp_text, &w, nullptr);
    //                 max_text_width = fmaxf(max_text_width, (float) w);
    //                 TTF_DestroyText(temp_text);
    //             }
    //             if (progress_buf[0] != '\0') {
    //                 temp_text = TTF_CreateText(o->text_engine, o->font, progress_buf, 0);
    //                 if (temp_text) {
    //                     int w;
    //                     TTF_GetTextSize(temp_text, &w, nullptr);
    //                     max_text_width = fmaxf(max_text_width, (float) w);
    //                     TTF_DestroyText(temp_text);
    //                 }
    //             }
    //         }
    //
    //         const float cell_width = fmaxf(ITEM_WIDTH, max_text_width);
    //         const float item_full_width = cell_width + ITEM_SPACING;
    //         float total_row_width = visible_item_count * item_full_width;
    //
    //         // Simple scroll wrapping for dynamic width rows
    //         if (total_row_width > 0) o->scroll_offset_row2 = fmod(o->scroll_offset_row2, total_row_width);
    //
    //         int items_to_draw = (item_full_width > 0) ? ceil((float) window_w / item_full_width) + 2 : 0;
    //         int current_item_idx = o->start_index_row2;
    //
    //         for (int k = 0; k < items_to_draw; k++) {
    //             size_t loop_guard = 0;
    //             while (is_display_item_done(row2_items[current_item_idx], settings)) {
    //                 current_item_idx = (current_item_idx + 1) % row2_items.size();
    //                 loop_guard++;
    //                 if (loop_guard > row2_items.size()) goto end_row2_render;
    //             }
    //
    //             const auto &display_item = row2_items[current_item_idx];
    //             float current_x = ((k - 1) * item_full_width) - o->scroll_offset_row2;
    //
    //             // --- Render the item ---
    //             SDL_Texture *bg_texture = o->adv_bg;
    //             std::string icon_path;
    //             char name_buf[256] = {0};
    //             char progress_buf[64] = {0};
    //
    //             switch (display_item.type) {
    //                 case OverlayDisplayItem::ADVANCEMENT: {
    //                     auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
    //                     if (adv->done) bg_texture = o->adv_bg_done;
    //                     else if (adv->completed_criteria_count > 0) bg_texture = o->adv_bg_half_done;
    //                     icon_path = adv->icon_path;
    //                     strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
    //                     if (adv->criteria_count > 0) {
    //                         snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", adv->completed_criteria_count,
    //                                  adv->criteria_count);
    //                     }
    //                     break;
    //                 }
    //                 case OverlayDisplayItem::UNLOCK: {
    //                     auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
    //                     if (unlock->done) bg_texture = o->adv_bg_done;
    //                     icon_path = unlock->icon_path;
    //                     strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
    //                     break;
    //                 }
    //                 default:
    //                     break; // Should not happen in this row
    //             }
    //
    //             // Center the 96x96 background within the calculated cell width
    //             float bg_x_offset = (cell_width - ITEM_WIDTH) / 2.0f;
    //             SDL_FRect bg_rect = {current_x + bg_x_offset, ROW2_Y_POS, ITEM_WIDTH, ITEM_WIDTH};
    //             if (bg_texture) {
    //                 SDL_RenderTexture(o->renderer, bg_texture, nullptr, &bg_rect);
    //             }
    //
    //             // Render the main icon (64x64) inside the background
    //             SDL_FRect icon_rect = {bg_rect.x + 16.0f, bg_rect.y + 16.0f, 64.0f, 64.0f};
    //             SDL_Texture *tex = nullptr;
    //             AnimatedTexture *anim_tex = nullptr;
    //             if (strstr(icon_path.c_str(), ".gif")) {
    //                 anim_tex = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
    //                                                            &o->anim_cache_capacity, icon_path.c_str(),
    //                                                            SDL_SCALEMODE_NEAREST);
    //             } else {
    //                 tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
    //                                              &o->texture_cache_capacity, icon_path.c_str(), SDL_SCALEMODE_NEAREST);
    //             }
    //             render_texture_with_alpha(o->renderer, tex, anim_tex, &icon_rect, 255);
    //
    //             // Render the text lines, centered within the cell
    //             TTF_Text *name_text = TTF_CreateText(o->text_engine, o->font, name_buf, 0);
    //             if (name_text) {
    //                 int w, h;
    //                 TTF_GetTextSize(name_text, &w, &h);
    //                 float text_x = current_x + (cell_width - (float) w) / 2.0f;
    //                 TTF_SetTextColor(name_text, settings->overlay_text_color.r, settings->overlay_text_color.g,
    //                                  settings->overlay_text_color.b, 255);
    //                 TTF_DrawRendererText(name_text, text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET);
    //
    //                 if (progress_buf[0] != '\0') {
    //                     TTF_Text *progress_text = TTF_CreateText(o->text_engine, o->font, progress_buf, 0);
    //                     if (progress_text) {
    //                         TTF_GetTextSize(progress_text, &w, nullptr);
    //                         text_x = current_x + (cell_width - (float) w) / 2.0f;
    //                         TTF_SetTextColor(progress_text, settings->overlay_text_color.r,
    //                                          settings->overlay_text_color.g, settings->overlay_text_color.b, 255);
    //                         TTF_DrawRendererText(progress_text, text_x,
    //                                              ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + (float) h);
    //                         TTF_DestroyText(progress_text);
    //                     }
    //                 }
    //                 TTF_DestroyText(name_text);
    //             }
    //         }
    //
    //
    //         float start_pos = fmod(o->scroll_offset_row2, total_row_width);
    //         int blocks_to_draw = (int) ceil((float) window_w / total_row_width) + 2;
    //
    //         for (int block = -blocks_to_draw; block <= blocks_to_draw; ++block) {
    //             float block_offset = start_pos + (block * total_row_width);
    //             int visible_item_index = 0;
    //
    //             for (const auto &display_item: row2_items) {
    //                 if (is_display_item_done(display_item, settings)) continue;
    //
    //                 float current_x = block_offset + (visible_item_index * item_full_width);
    //                 if (current_x + item_full_width < 0 || current_x > window_w) {
    //                     visible_item_index++;
    //                     continue;
    //                 }
    //
    //                 SDL_Texture *bg_texture = o->adv_bg;
    //                 std::string icon_path;
    //                 char name_buf[256] = {0};
    //                 char progress_buf[64] = {0};
    //
    //                 switch (display_item.type) {
    //                     case OverlayDisplayItem::ADVANCEMENT: {
    //                         auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
    //                         if (adv->done) bg_texture = o->adv_bg_done;
    //                         else if (adv->completed_criteria_count > 0) bg_texture = o->adv_bg_half_done;
    //                         icon_path = adv->icon_path;
    //                         strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
    //                         if (adv->criteria_count > 0) {
    //                             snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", adv->completed_criteria_count,
    //                                      adv->criteria_count);
    //                         }
    //                         break;
    //                     }
    //                     case OverlayDisplayItem::UNLOCK: {
    //                         auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
    //                         if (unlock->done) bg_texture = o->adv_bg_done;
    //                         icon_path = unlock->icon_path;
    //                         strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
    //                         break;
    //                     }
    //                     default:
    //                         break; // Should not happen in this row
    //                 }
    //
    //                 // Center the 96x96 background within the calculated cell width
    //                 float bg_x_offset = (cell_width - ITEM_WIDTH) / 2.0f;
    //                 SDL_FRect bg_rect = {current_x + bg_x_offset, ROW2_Y_POS, ITEM_WIDTH, ITEM_WIDTH};
    //                 if (bg_texture) {
    //                     SDL_RenderTexture(o->renderer, bg_texture, nullptr, &bg_rect);
    //                 }
    //
    //                 // Render the main icon (64x64) inside the background
    //                 SDL_FRect icon_rect = {bg_rect.x + 16.0f, bg_rect.y + 16.0f, 64.0f, 64.0f};
    //                 SDL_Texture *tex = nullptr;
    //                 AnimatedTexture *anim_tex = nullptr;
    //                 if (strstr(icon_path.c_str(), ".gif")) {
    //                     anim_tex = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
    //                                                                &o->anim_cache_capacity, icon_path.c_str(),
    //                                                                SDL_SCALEMODE_NEAREST);
    //                 } else {
    //                     tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
    //                                                  &o->texture_cache_capacity, icon_path.c_str(),
    //                                                  SDL_SCALEMODE_NEAREST);
    //                 }
    //                 render_texture_with_alpha(o->renderer, tex, anim_tex, &icon_rect, 255);
    //
    //                 // Render the text lines, centered within the cell
    //                 TTF_Text *name_text = TTF_CreateText(o->text_engine, o->font, name_buf, 0);
    //                 if (name_text) {
    //                     int w, h;
    //                     TTF_GetTextSize(name_text, &w, &h);
    //                     float text_x = current_x + (cell_width - (float) w) / 2.0f;
    //                     TTF_SetTextColor(name_text, settings->overlay_text_color.r, settings->overlay_text_color.g,
    //                                      settings->overlay_text_color.b, 255);
    //                     TTF_DrawRendererText(name_text, text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET);
    //
    //                     if (progress_buf[0] != '\0') {
    //                         TTF_Text *progress_text = TTF_CreateText(o->text_engine, o->font, progress_buf, 0);
    //                         if (progress_text) {
    //                             TTF_GetTextSize(progress_text, &w, nullptr);
    //                             text_x = current_x + (cell_width - (float) w) / 2.0f;
    //                             TTF_SetTextColor(progress_text, settings->overlay_text_color.r,
    //                                              settings->overlay_text_color.g, settings->overlay_text_color.b, 255);
    //                             TTF_DrawRendererText(progress_text, text_x,
    //                                                  ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + (float) h);
    //                             TTF_DestroyText(progress_text);
    //                         }
    //                     }
    //                     TTF_DestroyText(name_text);
    //                 }
    //
    //                 visible_item_index++;
    //             }
    //             current_item_idx = (current_item_idx + 1) % row2_items.size();
    //         }
    //     }
    //     end_row_2_renderer:;
    // }
    // --- ROW 3: Stats & Goals ---
    {
        const float ROW3_Y_POS = 260.0f; // Configure height of row, more pushes down
        const float ITEM_WIDTH = 96.0f;
        const float ITEM_SPACING = 16.0f;
        const float TEXT_Y_OFFSET = 4.0f;

        // Gather items for this row
        std::vector<OverlayDisplayItem> row3_items;
        for (int i = 0; i < t->template_data->stat_count; ++i) row3_items.push_back({
            t->template_data->stats[i], OverlayDisplayItem::STAT
        });
        for (int i = 0; i < t->template_data->custom_goal_count; ++i) row3_items.push_back({
            t->template_data->custom_goals[i], OverlayDisplayItem::CUSTOM
        });
        for (int i = 0; i < t->template_data->multi_stage_goal_count; ++i) row3_items.push_back({
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
                if (is_display_item_done(display_item, settings)) continue;

                char name_buf[256] = {0};
                char progress_buf[256] = {0};

                switch (display_item.type) {
                    case OverlayDisplayItem::STAT: {
                        auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
                        strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
                        if (stat->criteria_count > 1) {
                            snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", stat->completed_criteria_count,
                                     stat->criteria_count);
                        } else if (stat->criteria_count == 1) {
                            TrackableItem *crit = stat->criteria[0];
                            if (crit->goal > 0) snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                                         crit->progress, crit->goal);
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

                TTF_Text *temp_text = TTF_CreateText(o->text_engine, o->font, name_buf, 0);
                if (temp_text) {
                    int w;
                    TTF_GetTextSize(temp_text, &w, nullptr);
                    max_text_width = fmaxf(max_text_width, (float) w);
                    TTF_DestroyText(temp_text);
                }
                if (progress_buf[0] != '\0') {
                    temp_text = TTF_CreateText(o->text_engine, o->font, progress_buf, 0);
                    if (temp_text) {
                        int w;
                        TTF_GetTextSize(temp_text, &w, nullptr);
                        max_text_width = fmaxf(max_text_width, (float) w);
                        TTF_DestroyText(temp_text);
                    }
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
                            if (stat->done) bg_texture = o->adv_bg_done;
                            else if (stat->completed_criteria_count > 0 || (
                                         stat->criteria_count == 1 && stat->criteria[0]->progress > 0))
                                bg_texture = o->adv_bg_half_done;
                            icon_path = stat->icon_path;
                            strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
                            if (stat->criteria_count > 1) {
                                snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                         stat->completed_criteria_count, stat->criteria_count);
                            } else if (stat->criteria_count == 1) {
                                TrackableItem *crit = stat->criteria[0];
                                if (crit->goal > 0) snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                                             crit->progress, crit->goal);
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

                    TTF_Text *name_text = TTF_CreateText(o->text_engine, o->font, name_buf, 0);
                    if (name_text) {
                        int w, h;
                        TTF_GetTextSize(name_text, &w, &h);
                        float text_x = current_x + (cell_width - (float) w) / 2.0f;
                        TTF_SetTextColor(name_text, settings->overlay_text_color.r, settings->overlay_text_color.g,
                                         settings->overlay_text_color.b, 255);
                        TTF_DrawRendererText(name_text, text_x, ROW3_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET);

                        if (progress_buf[0] != '\0') {
                            TTF_Text *progress_text = TTF_CreateText(o->text_engine, o->font, progress_buf, 0);
                            if (progress_text) {
                                TTF_GetTextSize(progress_text, &w, nullptr);
                                text_x = current_x + (cell_width - (float) w) / 2.0f;
                                TTF_SetTextColor(progress_text, settings->overlay_text_color.r,
                                                 settings->overlay_text_color.g, settings->overlay_text_color.b, 255);
                                TTF_DrawRendererText(progress_text, text_x,
                                                     ROW3_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + (float) h);
                                TTF_DestroyText(progress_text);
                            }
                        }
                        TTF_DestroyText(name_text);
                    }

                    visible_item_index++;
                }
            }
        }
    }


    // TODO: Remove??
    // // --- GENERALIZED RENDER LOGIC FOR ROW 2 & 3 ---
    // auto render_item_row = [&](float y_pos, float scroll_offset, const std::vector<RenderItem> &items) {
    //     if (items.empty()) return;
    //
    //     const float ITEM_WIDTH = 96.0f;
    //     const float ITEM_SPACING = 16.0f;
    //     const float TEXT_Y_OFFSET = 4.0f;
    //
    //     struct TextCacheEntry {
    //         TTF_Text *name_text = nullptr;
    //         TTF_Text *progress_text = nullptr;
    //         float name_w = 0, name_h = 0;
    //         float progress_w = 0, progress_h = 0;
    //     };
    //     std::vector<TextCacheEntry> text_cache;
    //     text_cache.reserve(items.size());
    //
    //     float max_cell_width = ITEM_WIDTH;
    //     for (const auto &item: items) {
    //         TextCacheEntry entry;
    //
    //         entry.name_text = TTF_CreateText(o->text_engine, o->font, item.display_name.c_str(), 0);
    //         if (entry.name_text) {
    //             // Get size as int, but store as float
    //             int w, h;
    //             TTF_GetTextSize(entry.name_text, &w, &h);
    //             entry.name_w = (float) w;
    //             entry.name_h = (float) h;
    //             max_cell_width = fmaxf(max_cell_width, entry.name_w);
    //         }
    //
    //         if (!item.progress_text.empty()) {
    //             entry.progress_text = TTF_CreateText(o->text_engine, o->font, item.progress_text.c_str(), 0);
    //             if (entry.progress_text) {
    //                 int w, h;
    //                 TTF_GetTextSize(entry.progress_text, &w, &h);
    //                 entry.progress_w = (float) w;
    //                 entry.progress_h = (float) h;
    //                 max_cell_width = fmaxf(max_cell_width, entry.progress_w);
    //             }
    //         }
    //         text_cache.push_back(entry);
    //     }
    //
    //     float item_full_width = max_cell_width + ITEM_SPACING;
    //     float total_row_width = items.size() * item_full_width;
    //
    //     if (total_row_width > 0) {
    //         float start_pos = fmod(scroll_offset, total_row_width);
    //
    //         if (start_pos < 0) {
    //             start_pos += total_row_width;
    //         }
    //
    //         auto render_block = [&](float block_offset) {
    //             for (size_t i = 0; i < items.size(); ++i) {
    //                 const RenderItem &item = items[i];
    //                 const TextCacheEntry &cached_text = text_cache[i];
    //                 float current_x = block_offset + (i * item_full_width);
    //
    //                 if (current_x + max_cell_width < 0 || current_x > window_w) continue;
    //
    //                 Uint8 final_alpha_byte = (Uint8) (item.alpha * 255);
    //                 if (final_alpha_byte == 0) continue;
    //                 float final_alpha_float = item.alpha;
    //
    //                 float bg_x_offset = (max_cell_width - ITEM_WIDTH) / 2.0f;
    //
    //                 if (item.bg_texture) {
    //                     SDL_FRect bg_rect = {current_x + bg_x_offset, y_pos, ITEM_WIDTH, ITEM_WIDTH};
    //                     SDL_SetTextureAlphaMod(item.bg_texture, final_alpha_byte);
    //                     SDL_RenderTexture(o->renderer, item.bg_texture, nullptr, &bg_rect);
    //                     SDL_SetTextureAlphaMod(item.bg_texture, 255);
    //                 }
    //
    //                 SDL_FRect icon_rect = {current_x + bg_x_offset + 16.0f, y_pos + 16.0f, 64.0f, 64.0f};
    //
    //                 // TODO: DEBUG PLACEHOLDER
    //                 // SDL_SetRenderDrawColor(o->renderer, 255, 0, 255, 50);
    //                 // SDL_RenderFillRect(o->renderer, &icon_rect);
    //
    //
    //                 // Separate .gif and .png texture loading
    //                 SDL_Texture *tex = nullptr;
    //                 AnimatedTexture *anim_tex = nullptr;
    //                 if (strstr(item.icon_path.c_str(), ".gif")) {
    //                     anim_tex = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
    //                                                                &o->anim_cache_capacity, item.icon_path.c_str(),
    //                                                                SDL_SCALEMODE_NEAREST);
    //                 } else {
    //                     tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
    //                                                  &o->texture_cache_capacity, item.icon_path.c_str(),
    //                                                  SDL_SCALEMODE_NEAREST);
    //                 }
    //                 render_texture_with_alpha(o->renderer, tex, anim_tex, &icon_rect, final_alpha_byte);
    //
    //                 if (cached_text.name_text) {
    //                     TTF_SetTextColorFloat(cached_text.name_text, (float) settings->overlay_text_color.r / 255.0f,
    //                                           (float) settings->overlay_text_color.g / 255.0f,
    //                                           (float) settings->overlay_text_color.b / 255.0f, final_alpha_float);
    //                     TTF_DrawRendererText(cached_text.name_text,
    //                                          current_x + (max_cell_width - cached_text.name_w) / 2.0f,
    //                                          y_pos + ITEM_WIDTH + TEXT_Y_OFFSET);
    //
    //                     if (cached_text.progress_text) {
    //                         TTF_SetTextColorFloat(cached_text.progress_text,
    //                                               (float) settings->overlay_text_color.r / 255.0f,
    //                                               (float) settings->overlay_text_color.g / 255.0f,
    //                                               (float) settings->overlay_text_color.b / 255.0f, final_alpha_float);
    //                         TTF_DrawRendererText(cached_text.progress_text,
    //                                              current_x + (max_cell_width - cached_text.progress_w) / 2.0f,
    //                                              y_pos + ITEM_WIDTH + TEXT_Y_OFFSET + cached_text.name_h);
    //                     }
    //                 }
    //             }
    //         };
    //         int blocks_to_draw = (int) ceil((float) window_w / total_row_width) + 1;
    //         for (int i = 0; i < blocks_to_draw; ++i) {
    //             render_block(start_pos + (i * total_row_width));
    //             render_block(start_pos - ((i + 1) * total_row_width));
    //         }
    //     }
    //
    //     for (auto &entry: text_cache) {
    //         if (entry.name_text) TTF_DestroyText(entry.name_text);
    //         if (entry.progress_text) TTF_DestroyText(entry.progress_text);
    //     }
    // };

    // TODO: Remove??
    // struct RenderItem {
    //     std::string display_name;
    //     std::string progress_text;
    //     std::string icon_path;
    //     SDL_Texture *texture = nullptr;
    //     AnimatedTexture *anim_texture = nullptr;
    //     SDL_Texture *bg_texture = nullptr;
    //     float alpha = 0.0f;
    // };
    //
    //
    // // --- ROW 2 Data Collection ---
    // {
    //     std::vector<RenderItem> items;
    //     // Advancements
    //     for (int i = 0; i < t->template_data->advancement_count; ++i) {
    //         TrackableCategory *adv = t->template_data->advancements[i];
    //         if (adv->alpha > 0.0f && !((adv->criteria_count > 0 && adv->all_template_criteria_met) || (
    //                                        adv->criteria_count == 0 && adv->done))) {
    //             RenderItem item;
    //             item.display_name = adv->display_name;
    //             item.icon_path = adv->icon_path;
    //             item.alpha = adv->alpha;
    //             // Use overlay background textures
    //             if (adv->done) item.bg_texture = o->adv_bg_done;
    //             else if (adv->completed_criteria_count > 0) item.bg_texture = o->adv_bg_half_done;
    //             else item.bg_texture = o->adv_bg;
    //             if (adv->criteria_count > 0) {
    //                 char buf[32];
    //                 snprintf(buf, sizeof(buf), "(%d / %d)", adv->completed_criteria_count, adv->criteria_count);
    //                 item.progress_text = buf;
    //             }
    //             items.push_back(item);
    //         }
    //     }
    //     // Unlocks
    //     for (int i = 0; i < t->template_data->unlock_count; ++i) {
    //         TrackableItem *unlock = t->template_data->unlocks[i];
    //         if (unlock->alpha > 0.0f) {
    //             RenderItem item;
    //             item.display_name = unlock->display_name;
    //             item.icon_path = unlock->icon_path;
    //             item.alpha = unlock->alpha;
    //             item.bg_texture = unlock->done ? o->adv_bg_done : o->adv_bg;
    //             items.push_back(item);
    //         }
    //     }
    //
    //     // Initially was 110.0f
    //     render_item_row(108.0f, o->scroll_offset_row2, items);
    // }
    //
    // // --- ROW 3 Data Collection ---
    // {
    //     std::vector<RenderItem> items;
    //     MC_Version version = settings_get_version_from_string(settings->version_str);
    //     // Stat Categories
    //     for (int i = 0; i < t->template_data->stat_count; ++i) {
    //         TrackableCategory *stat = t->template_data->stats[i];
    //         bool is_hidden_legacy = (version <= MC_VERSION_1_6_4 && stat->criteria_count == 1 && stat->criteria[0]->goal
    //                                  <= 0);
    //         if (stat->alpha > 0.0f && !is_hidden_legacy && !stat->done) {
    //             RenderItem item;
    //             item.display_name = stat->display_name;
    //             item.icon_path = stat->icon_path;
    //             item.alpha = stat->alpha;
    //
    //             if (stat->done) item.bg_texture = o->adv_bg_done;
    //             else if (stat->completed_criteria_count > 0 || (
    //                          stat->criteria_count == 1 && stat->criteria[0]->progress > 0))
    //                 item.bg_texture = o->adv_bg_half_done;
    //             else item.bg_texture = o->adv_bg;
    //
    //             // This logic correctly creates the progress text, same as the tracker
    //             if (stat->criteria_count > 1) {
    //                 char buf[32];
    //                 snprintf(buf, sizeof(buf), "(%d / %d)", stat->completed_criteria_count, stat->criteria_count);
    //                 item.progress_text = buf;
    //             } else if (stat->criteria_count == 1) {
    //                 TrackableItem *crit = stat->criteria[0];
    //                 if (crit->goal > 0) {
    //                     char buf[32];
    //                     snprintf(buf, sizeof(buf), "(%d / %d)", crit->progress, crit->goal);
    //                     item.progress_text = buf;
    //                 }
    //             }
    //             items.push_back(item);
    //         }
    //     }
    //
    //     // Custom Goals
    //     for (int i = 0; i < t->template_data->custom_goal_count; ++i) {
    //         TrackableItem *goal = t->template_data->custom_goals[i];
    //         if (goal->alpha > 0.0f) {
    //             RenderItem item;
    //             item.display_name = goal->display_name;
    //             item.icon_path = goal->icon_path;
    //             // TODO: Remove
    //             // item.texture = goal->texture;
    //             // item.anim_texture = goal->anim_texture;
    //             item.alpha = goal->alpha;
    //             if (goal->done) item.bg_texture = o->adv_bg_done;
    //             else if (goal->progress > 0) item.bg_texture = o->adv_bg_half_done;
    //             else item.bg_texture = o->adv_bg;
    //             char buf[32];
    //             if (goal->goal > 0) {
    //                 snprintf(buf, sizeof(buf), "(%d / %d)", goal->progress, goal->goal);
    //                 item.progress_text = buf;
    //             } else if (goal->goal == -1 && !goal->done) {
    //                 snprintf(buf, sizeof(buf), "(%d)", goal->progress);
    //                 item.progress_text = buf;
    //             }
    //             items.push_back(item);
    //         }
    //     }
    //     // Multi-Stage Goals
    //     for (int i = 0; i < t->template_data->multi_stage_goal_count; ++i) {
    //         MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
    //         if (goal->alpha > 0.0f) {
    //             RenderItem item;
    //             item.display_name = goal->display_name;
    //             item.icon_path = goal->icon_path;
    //             // TODO: Remove
    //             // item.texture = goal->texture;
    //             // item.anim_texture = goal->anim_texture;
    //             item.alpha = goal->alpha;
    //             if (goal->current_stage >= goal->stage_count - 1) item.bg_texture = o->adv_bg_done;
    //             else if (goal->current_stage > 0) item.bg_texture = o->adv_bg_half_done;
    //             else item.bg_texture = o->adv_bg;
    //             if (goal->current_stage < goal->stage_count) {
    //                 SubGoal *active_stage = goal->stages[goal->current_stage];
    //                 char buf[256];
    //                 if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress > 0) {
    //                     snprintf(buf, sizeof(buf), "%s (%d/%d)", active_stage->display_text,
    //                              active_stage->current_stat_progress, active_stage->required_progress);
    //                 } else { snprintf(buf, sizeof(buf), "%s", active_stage->display_text); }
    //                 item.progress_text = buf;
    //             }
    //             items.push_back(item);
    //         }
    //     }
    //     // Float value controls how much row is pushed down, initially was 270.0f
    //     render_item_row(260.0f, o->scroll_offset_row3, items);
    // }

    SDL_RenderPresent(o->renderer);
}


void overlay_free(Overlay **overlay, const AppSettings *settings) {
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
        }
        if (o->anim_cache) {
            for (int i = 0; i < o->anim_cache_count; i++) {
                if (o->anim_cache[i].anim) {
                    free_animated_texture(o->anim_cache[i].anim);
                }
            }
            free(o->anim_cache);
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

        if (settings->print_debug_status) {
            printf("[OVERLAY] Overlay freed!\n");
        }
    }
}
