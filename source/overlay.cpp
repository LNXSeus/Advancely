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

// Helper function to render a texture (static or animated) with alpha modulation
static void render_texture_with_alpha(SDL_Renderer *renderer, SDL_Texture *texture, AnimatedTexture *anim_texture,
                                      const SDL_FRect *dest, Uint8 alpha) {
    SDL_Texture *texture_to_render = nullptr;

    if (anim_texture && anim_texture->frame_count > 0) {
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
        SDL_RenderTexture(renderer, texture_to_render, nullptr, dest);
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

    o->scroll_offset_row1 = 0.0f;
    o->scroll_offset_row2 = 0.0f;
    o->scroll_offset_row3 = 0.0f;
    o->social_media_timer = 0.0f;
    o->current_social_index = 0;
    o->text_engine = nullptr;

    if (!overlay_init_sdl(o, settings)) {
        overlay_free(overlay, settings);
        return false;
    }

    // Make font HiDPI aware
    float scale = SDL_GetWindowDisplayScale(o->window);
    if (scale == 0.0f) {
        // Handle potential errors where scale is 0
        scale = 1.0f;
    }
    int font_size = (int) roundf(24.0f * scale); // Scale base font size

    o->font = TTF_OpenFont("resources/fonts/Minecraft.ttf", (float) font_size);
    if (!o->font) {
        fprintf(stderr, "[OVERLAY] Failed to load font: %s\n", SDL_GetError());
        overlay_free(overlay, settings);
        return false;
    }

    o->text_engine = TTF_CreateRendererTextEngine(o->renderer);
    if (!o->text_engine) {
        fprintf(stderr, "[OVERLAY] Failed to create text engine: %s\n", SDL_GetError());
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

void overlay_update(Overlay *o, float *deltaTime, const AppSettings *settings) {
    // Base speed: 1440 pixels in 24 seconds = 60 pixels/second
    const float base_scroll_speed = 60.0f;
    float speed_multiplier = settings->overlay_scroll_speed;

    if (settings->overlay_animation_speedup) {
        speed_multiplier *= OVERLAY_SPEEDUP_FACTOR;
    }

    float scroll_delta = base_scroll_speed * speed_multiplier * (*deltaTime);

    o->scroll_offset_row1 += scroll_delta;
    o->scroll_offset_row2 += scroll_delta;
    o->scroll_offset_row3 += scroll_delta;

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
    SDL_RenderClear(o->renderer);

    // Render Progress Text (Top Bar) - NO CHANGES HERE
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
                SDL_SetTextureScaleMode(text_texture, SDL_SCALEMODE_NEAREST);
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
        const float ROW1_Y_POS = 52.0f; // Initially was 60.0f
        const float ROW1_ICON_SIZE = 48.0f;
        const float ROW1_SPACING = 8.0f;
        const float ROW1_SHARED_ICON_SIZE = 24.0f;

        std::vector<TrackableItem *> items;
        std::vector<TrackableCategory *> parents;

        for (int i = 0; i < t->template_data->advancement_count; i++) {
            TrackableCategory *cat = t->template_data->advancements[i];
            for (int j = 0; j < cat->criteria_count; j++) {
                if (cat->criteria[j]->alpha > 0.0f) {
                    items.push_back(cat->criteria[j]);
                    parents.push_back(cat);
                }
            }
        }
        for (int i = 0; i < t->template_data->stat_count; i++) {
            TrackableCategory *cat = t->template_data->stats[i];
            for (int j = 0; j < cat->criteria_count; j++) {
                if (cat->criteria[j]->alpha > 0.0f) {
                    items.push_back(cat->criteria[j]);
                    parents.push_back(cat);
                }
            }
        }

        if (!items.empty()) {
            float item_full_width = ROW1_ICON_SIZE + ROW1_SPACING;
            float total_row_width = items.size() * item_full_width;

            if (total_row_width > 0) {
                float start_pos = fmod(o->scroll_offset_row1, total_row_width);
                auto render_block = [&](float block_offset) {
                    for (size_t i = 0; i < items.size(); ++i) {
                        float current_x = block_offset + (i * item_full_width);
                        if (current_x + item_full_width < 0 || current_x > window_w) continue;

                        TrackableItem *item = items[i];
                        TrackableCategory *parent = parents[i];

                        Uint8 final_alpha = (Uint8) (item->alpha * 255);
                        if (final_alpha == 0) continue;

                        SDL_FRect dest_rect = {current_x, ROW1_Y_POS, ROW1_ICON_SIZE, ROW1_ICON_SIZE};

                        // TODO: Debug Placeholder Pink Squares for Criteria and Sub-Stats
                        SDL_SetRenderDrawColor(o->renderer, 255, 0, 255, 50); // Bright semi-transparent pink
                        SDL_RenderFillRect(o->renderer, &dest_rect);

                        render_texture_with_alpha(o->renderer, item->texture, item->anim_texture, &dest_rect,
                                                  final_alpha);

                        if (item->is_shared) {
                            SDL_FRect shared_dest_rect = {
                                current_x, ROW1_Y_POS, ROW1_SHARED_ICON_SIZE, ROW1_SHARED_ICON_SIZE
                            };
                            render_texture_with_alpha(o->renderer, parent->texture, parent->anim_texture,
                                                      &shared_dest_rect, final_alpha);
                        }
                    }
                };

                render_block(start_pos);
                render_block(start_pos - total_row_width);
                if (total_row_width < window_w) render_block(start_pos + total_row_width);
            }
        }
    }

    struct RenderItem {
        std::string display_name;
        std::string progress_text;
        SDL_Texture *texture = nullptr;
        AnimatedTexture *anim_texture = nullptr;
        SDL_Texture *bg_texture = nullptr;
        float alpha = 0.0f;
    };

    // --- ROW 2 & 3 (Generalized Logic) ---
    // --- GENERALIZED RENDER LOGIC FOR ROW 2 & 3 ---
    auto render_item_row = [&](float y_pos, float scroll_offset, const std::vector<RenderItem>& items) {
        if (items.empty()) return;

        const float ITEM_WIDTH = 96.0f;
        const float ITEM_SPACING = 16.0f;
        const float TEXT_Y_OFFSET = 4.0f;

        struct TextCacheEntry {
            TTF_Text* name_text = nullptr;
            TTF_Text* progress_text = nullptr;
            float name_w = 0, name_h = 0;
            float progress_w = 0, progress_h = 0;
        };
        std::vector<TextCacheEntry> text_cache;
        text_cache.reserve(items.size());

        float max_cell_width = ITEM_WIDTH;
        for (const auto& item : items) {
            TextCacheEntry entry;
            // TODO: Where is text_color handled?
            // SDL_Color text_color = { settings->overlay_text_color.r, settings->overlay_text_color.g, settings->overlay_text_color.b, 255 };


            entry.name_text = TTF_CreateText(o->text_engine, o->font, item.display_name.c_str(), 0);
            if (entry.name_text) {
                // Get size as int, but store as float
                int w, h;
                TTF_GetTextSize(entry.name_text, &w, &h);
                entry.name_w = (float)w;
                entry.name_h = (float)h;
                max_cell_width = fmaxf(max_cell_width, entry.name_w);
            }

            if (!item.progress_text.empty()) {
                entry.progress_text = TTF_CreateText(o->text_engine, o->font, item.progress_text.c_str(), 0);
                if (entry.progress_text) {
                    int w, h;
                    TTF_GetTextSize(entry.progress_text, &w, &h);
                    entry.progress_w = (float)w;
                    entry.progress_h = (float)h;
                    max_cell_width = fmaxf(max_cell_width, entry.progress_w);
                }
            }
            text_cache.push_back(entry);
        }

        float item_full_width = max_cell_width + ITEM_SPACING;
        float total_row_width = items.size() * item_full_width;

        if (total_row_width > 0) {
            float start_pos = fmod(scroll_offset, total_row_width);
            auto render_block = [&](float block_offset) {
                for (size_t i = 0; i < items.size(); ++i) {
                    const RenderItem& item = items[i];
                    const TextCacheEntry &cached_text = text_cache[i];
                    float current_x = block_offset + (i * item_full_width);

                    if (current_x + max_cell_width < 0 || current_x > window_w) continue;

                    Uint8 final_alpha_byte = (Uint8)(item.alpha * 255);
                    if (final_alpha_byte == 0) continue;
                    float final_alpha_float = item.alpha;

                    float bg_x_offset = (max_cell_width - ITEM_WIDTH) / 2.0f;

                    if (item.bg_texture) {
                        SDL_FRect bg_rect = { current_x + bg_x_offset, y_pos, ITEM_WIDTH, ITEM_WIDTH };
                        SDL_SetTextureAlphaMod(item.bg_texture, final_alpha_byte);
                        SDL_RenderTexture(o->renderer, item.bg_texture, nullptr, &bg_rect);
                        SDL_SetTextureAlphaMod(item.bg_texture, 255);
                    }

                    SDL_FRect icon_rect = { current_x + bg_x_offset + 16.0f, y_pos + 16.0f, 64.0f, 64.0f };

                    SDL_SetRenderDrawColor(o->renderer, 255, 0, 255, 50);
                    SDL_RenderFillRect(o->renderer, &icon_rect);

                    render_texture_with_alpha(o->renderer, item.texture, item.anim_texture, &icon_rect, final_alpha_byte);

                    if (cached_text.name_text) {
                        TTF_SetTextColorFloat(cached_text.name_text, (float)settings->overlay_text_color.r / 255.0f, (float)settings->overlay_text_color.g / 255.0f, (float)settings->overlay_text_color.b / 255.0f, final_alpha_float);
                        TTF_DrawRendererText(cached_text.name_text, current_x + (max_cell_width - cached_text.name_w) / 2.0f, y_pos + ITEM_WIDTH + TEXT_Y_OFFSET);

                        if (cached_text.progress_text) {
                            TTF_SetTextColorFloat(cached_text.progress_text, (float)settings->overlay_text_color.r / 255.0f, (float)settings->overlay_text_color.g / 255.0f, (float)settings->overlay_text_color.b / 255.0f, final_alpha_float);
                            TTF_DrawRendererText(cached_text.progress_text, current_x + (max_cell_width - cached_text.progress_w) / 2.0f, y_pos + ITEM_WIDTH + TEXT_Y_OFFSET + cached_text.name_h);
                        }
                    }
                }
            };
            render_block(start_pos);
            render_block(start_pos - total_row_width);
            if (total_row_width < window_w) render_block(start_pos + total_row_width);
        }

        for (auto& entry : text_cache) {
            if (entry.name_text) TTF_DestroyText(entry.name_text);
            if (entry.progress_text) TTF_DestroyText(entry.progress_text);
        }
    };

    // --- ROW 2 Data Collection ---
    {
        std::vector<RenderItem> items;
        MC_Version version = settings_get_version_from_string(settings->version_str);
        // Advancements
        for (int i = 0; i < t->template_data->advancement_count; ++i) {
            TrackableCategory *adv = t->template_data->advancements[i];
            if (adv->alpha > 0.0f && !((adv->criteria_count > 0 && adv->all_template_criteria_met) || (
                                           adv->criteria_count == 0 && adv->done))) {
                RenderItem item;
                item.display_name = adv->display_name;
                item.texture = adv->texture;
                item.anim_texture = adv->anim_texture;
                item.alpha = adv->alpha;
                if (adv->done) item.bg_texture = t->adv_bg_done;
                else if (adv->completed_criteria_count > 0) item.bg_texture = t->adv_bg_half_done;
                else item.bg_texture = t->adv_bg;
                if (adv->criteria_count > 0) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "(%d / %d)", adv->completed_criteria_count, adv->criteria_count);
                    item.progress_text = buf;
                }
                items.push_back(item);
            }
        }
        // Unlocks
        for (int i = 0; i < t->template_data->unlock_count; ++i) {
            TrackableItem *unlock = t->template_data->unlocks[i];
            if (unlock->alpha > 0.0f) {
                RenderItem item;
                item.display_name = unlock->display_name;
                item.texture = unlock->texture;
                item.anim_texture = unlock->anim_texture;
                item.alpha = unlock->alpha;
                item.bg_texture = unlock->done ? t->adv_bg_done : t->adv_bg;
                items.push_back(item);
            }
        }
        // Stat Categories
        for (int i = 0; i < t->template_data->stat_count; ++i) {
            TrackableCategory *stat = t->template_data->stats[i];
            bool is_hidden_legacy = (version <= MC_VERSION_1_6_4 && stat->criteria_count == 1 && stat->criteria[0]->goal
                                     <= 0);
            if (stat->alpha > 0.0f && !is_hidden_legacy) {
                RenderItem item;
                item.display_name = stat->display_name;
                item.texture = stat->texture;
                item.anim_texture = stat->anim_texture;
                item.alpha = stat->alpha;
                if (stat->done) item.bg_texture = t->adv_bg_done;
                else if (stat->completed_criteria_count > 0 || (
                             stat->criteria_count == 1 && stat->criteria[0]->progress > 0))
                    item.bg_texture = t->adv_bg_half_done;
                else item.bg_texture = t->adv_bg;
                if (stat->criteria_count > 1) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "(%d / %d)", stat->completed_criteria_count, stat->criteria_count);
                    item.progress_text = buf;
                } else if (stat->criteria_count == 1) {
                    TrackableItem *crit = stat->criteria[0];
                    if (crit->goal > 0) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "(%d / %d)", crit->progress, crit->goal);
                        item.progress_text = buf;
                    }
                }
                items.push_back(item);
            }
        }
        // Initially was 110.0f
        render_item_row(108.0f, o->scroll_offset_row2, items);
    }

    // --- ROW 3 Data Collection ---
    {
        std::vector<RenderItem> items;
        // Custom Goals
        for (int i = 0; i < t->template_data->custom_goal_count; ++i) {
            TrackableItem *goal = t->template_data->custom_goals[i];
            if (goal->alpha > 0.0f) {
                RenderItem item;
                item.display_name = goal->display_name;
                item.texture = goal->texture;
                item.anim_texture = goal->anim_texture;
                item.alpha = goal->alpha;
                if (goal->done) item.bg_texture = t->adv_bg_done;
                else if (goal->progress > 0) item.bg_texture = t->adv_bg_half_done;
                else item.bg_texture = t->adv_bg;
                char buf[32];
                if (goal->goal > 0) {
                    snprintf(buf, sizeof(buf), "(%d / %d)", goal->progress, goal->goal);
                    item.progress_text = buf;
                } else if (goal->goal == -1 && !goal->done) {
                    snprintf(buf, sizeof(buf), "(%d)", goal->progress);
                    item.progress_text = buf;
                }
                items.push_back(item);
            }
        }
        // Multi-Stage Goals
        for (int i = 0; i < t->template_data->multi_stage_goal_count; ++i) {
            MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
            if (goal->alpha > 0.0f) {
                RenderItem item;
                item.display_name = goal->display_name;
                item.texture = goal->texture;
                item.anim_texture = goal->anim_texture;
                item.alpha = goal->alpha;
                if (goal->current_stage >= goal->stage_count - 1) item.bg_texture = t->adv_bg_done;
                else if (goal->current_stage > 0) item.bg_texture = t->adv_bg_half_done;
                else item.bg_texture = t->adv_bg;
                if (goal->current_stage < goal->stage_count) {
                    SubGoal *active_stage = goal->stages[goal->current_stage];
                    char buf[256];
                    if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress > 0) {
                        snprintf(buf, sizeof(buf), "%s (%d/%d)", active_stage->display_text,
                                 active_stage->current_stat_progress, active_stage->required_progress);
                    } else { snprintf(buf, sizeof(buf), "%s", active_stage->display_text); }
                    item.progress_text = buf;
                }
                items.push_back(item);
            }
        }
        // Float value controls how much row is pushed down, initially was 270.0f
        render_item_row(260.0f, o->scroll_offset_row3, items);
    }

    SDL_RenderPresent(o->renderer);
}


void overlay_free(Overlay **overlay, const AppSettings *settings) {
    if (overlay && *overlay) {
        Overlay *o = *overlay;

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
