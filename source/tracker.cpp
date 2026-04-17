// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 24.06.2025.
//

#include <cstdio>
#include <map>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <unordered_map>
#include <cJSON.h>
#include <cmath>
#include <ctime>

#include "tracker.h"

#include <set>
#include <vector>
#include <unordered_set>

#include "init_sdl.h"
#include "path_utils.h"
#include "settings_utils.h" // For note related defaults as well
#include "file_utils.h" // has the cJSON_from_file function
#include "template_scanner.h" // For parse_manual_pos
#include "temp_creator_utils.h"
#include "coop_net.h"
#include "global_event_handler.h"
#include "format_utils.h"
#include "logger.h"
#include "main.h" // For show_error_message

#include "imgui_internal.h"

// Helper macros for font scaling
#define SET_FONT_SCALE(size, base_size) \
do { \
scale_factor = 1.0f; /* Assign, don't declare */ \
if (base_size > 0.0f) scale_factor = size / base_size; \
ImGui::SetWindowFontScale(scale_factor); \
} while(0) // Use do-while(0) for macro safety

#define RESET_FONT_SCALE() ImGui::SetWindowFontScale(1.0f)

// --- Visual Layout Multi-Select State ---
// Registered draggable items for selection rectangle hit-testing (rebuilt every frame)
struct VisualLayoutItem {
    ImVec2 screen_pos; // Top-left corner on screen
    ImVec2 size; // Size on screen
    ManualPos *pos; // Pointer to the ManualPos being controlled
};

static std::vector<VisualLayoutItem> s_visual_layout_items;
static std::vector<VisualLayoutItem> s_visual_layout_items_prev; // Previous frame snapshot for lookups during rendering
static std::unordered_set<ManualPos *> s_visual_selected_items;

// Search-linked sets: items that should remain visible because a matching counter/header links to them
static std::unordered_set<std::string> s_linked_top; // Top-level items (root_name, no parent_root)
static std::unordered_set<std::string> s_linked_sub; // Sub-items: composite key "parent_root\troot_name"

// Search helpers: match display_name, root_name, and icon_path against the search buffer
static bool item_matches_search(const TrackableItem *item, const char *search) {
    return str_contains_insensitive(item->display_name, search)
           || str_contains_insensitive(item->root_name, search)
           || str_contains_insensitive(item->icon_path, search);
}

static bool category_matches_search(const TrackableCategory *cat, const char *search) {
    return str_contains_insensitive(cat->display_name, search)
           || str_contains_insensitive(cat->root_name, search)
           || str_contains_insensitive(cat->icon_path, search);
}

static bool counter_matches_search(const CounterGoal *goal, const char *search) {
    return str_contains_insensitive(goal->display_name, search)
           || str_contains_insensitive(goal->root_name, search)
           || str_contains_insensitive(goal->icon_path, search);
}

static bool ms_goal_matches_search(const MultiStageGoal *goal, const char *search) {
    return str_contains_insensitive(goal->display_name, search)
           || str_contains_insensitive(goal->root_name, search)
           || str_contains_insensitive(goal->icon_path, search);
}

// For multi-stage goals: match the active stage's display_text, stage_id, and icon_path (only if stage icons are in use)
static bool stage_matches_search(const MultiStageGoal *goal, const SubGoal *stage, const char *search) {
    return str_contains_insensitive(stage->display_text, search)
           || str_contains_insensitive(stage->stage_id, search)
           || (goal->use_stage_icons && str_contains_insensitive(stage->icon_path, search));
}


// Simple string hashing function (djb2) to create a safe filename from a world path
static unsigned long hash_string(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

// Manages the per-world notes manifest and determines the correct notes file path
static void tracker_update_notes_path(Tracker *t, const AppSettings *settings) {
    if (settings->per_world_notes) {
        // Per World Mode
        if (t->world_name[0] == '\0' || t->saves_path[0] == '\0') {
            t->notes_path[0] = '\0';
            return;
        }

        // Create a unique identifier for the world
        char full_world_path[MAX_PATH_LENGTH * 2];
        snprintf(full_world_path, sizeof(full_world_path), "%s/%s", t->saves_path, t->world_name);

        // Hash the full path
        unsigned long world_hash = hash_string(full_world_path);
        snprintf(t->notes_path, MAX_PATH_LENGTH, "%s/%lu.txt", get_notes_dir_path(), world_hash);

        // Manifest Management
        fs_ensure_directory_exists(get_notes_dir_path());
        cJSON *manifest = cJSON_from_file(get_notes_manifest_path());
        if (!manifest) manifest = cJSON_CreateArray();

        cJSON *entry_to_update = nullptr;
        cJSON *entry = nullptr;
        cJSON_ArrayForEach(entry, manifest) {
            cJSON *hash_item = cJSON_GetObjectItem(entry, "hash");
            if (cJSON_IsNumber(hash_item) && (unsigned long) hash_item->valuedouble == world_hash) {
                entry_to_update = entry;
                break;
            }
        }

        bool save_needed = false; // Default to FALSE to prevent loops
        double current_time = (double) time(0);

        if (entry_to_update) {
            // World exists. Check timestamp before writing!
            cJSON *last_used_item = cJSON_GetObjectItem(entry_to_update, "last_used");
            double last_time = cJSON_IsNumber(last_used_item) ? last_used_item->valuedouble : 0;

            // Only write if more than 5 seconds have passed.
            if ((current_time - last_time) > 5.0) {
                cJSON_ReplaceItemInObject(entry_to_update, "last_used", cJSON_CreateNumber(current_time));
                save_needed = true;
            }
        } else {
            // New world, add a new entry (Always save new entries)
            cJSON *new_entry = cJSON_CreateObject();
            cJSON_AddNumberToObject(new_entry, "hash", (double) world_hash);
            cJSON_AddStringToObject(new_entry, "path", full_world_path);
            cJSON_AddNumberToObject(new_entry, "last_used", (double) time(0));
            cJSON_AddItemToArray(manifest, new_entry);
            save_needed = true;

            // Check if we need to prune old notes (LRU)
            if (cJSON_GetArraySize(manifest) > MAX_WORLD_NOTES) {
                cJSON *oldest_entry = nullptr;
                double oldest_time = -1;
                int oldest_index = -1;

                int current_index = 0;
                cJSON_ArrayForEach(entry, manifest) {
                    double last_used = cJSON_GetObjectItem(entry, "last_used")->valuedouble;
                    if (oldest_time == -1 || last_used < oldest_time) {
                        oldest_time = last_used;
                        oldest_entry = entry;
                        oldest_index = current_index;
                    }
                    current_index++;
                }

                if (oldest_entry && oldest_index != -1) {
                    unsigned long hash_to_delete = (unsigned long) cJSON_GetObjectItem(oldest_entry, "hash")->
                            valuedouble;
                    char path_to_delete[MAX_PATH_LENGTH];
                    snprintf(path_to_delete, sizeof(path_to_delete), "%s/%lu.txt", get_notes_dir_path(),
                             hash_to_delete);
                    if (remove(path_to_delete) == 0) {
                        log_message(LOG_INFO, "[NOTES] Pruned old notes file: %s\n", path_to_delete);
                    }
                    cJSON_DeleteItemFromArray(manifest, oldest_index);
                }
            }
        }

        // Save the updated manifest ONLY if necessary
        if (save_needed) {
            FILE *manifest_file = fopen(get_notes_manifest_path(), "w");
            if (manifest_file) {
                char *manifest_str = cJSON_Print(manifest);
                fputs(manifest_str, manifest_file);
                fclose(manifest_file);
                free(manifest_str);
            }
        }
        cJSON_Delete(manifest);
    } else {
        // Per template mode
        construct_template_paths((AppSettings *) settings);
        strncpy(t->notes_path, settings->notes_path, MAX_PATH_LENGTH - 1);
        t->notes_path[MAX_PATH_LENGTH - 1] = '\0';
    }
}


// NON STATIC FUNCTION -------------------------------------------------------------------

bool str_contains_insensitive(const char *haystack, const char *needle) {
    if (!needle || *needle == '\0') return true;
    if (!haystack) return false;

    // Optimized: No memory allocation
    const char *h = haystack;
    const char *n_start = needle;

    while (*h) {
        const char *h_iter = h;
        const char *n_iter = n_start;

        while (*h_iter && *n_iter &&
               (tolower((unsigned char) *h_iter) == tolower((unsigned char) *n_iter))) {
            h_iter++;
            n_iter++;
        }

        if (*n_iter == '\0') return true; // Found match
        h++;
    }
    return false;
}

// END OF NON STATIC FUNCTION -------------------------------------------------------------------

// Builds s_linked_top and s_linked_sub from counters and text headers that match the current search.
// One layer deep only (no transitivity). Exact matching (no parent/child propagation).
static void build_search_linked_sets(const Tracker *t) {
    s_linked_top.clear();
    s_linked_sub.clear();
    if (t->search_buffer[0] == '\0') return;

    const TemplateData *td = t->template_data;
    if (!td) return;

    auto add_linked = [td](const CounterLinkedGoal *goals, int count) {
        for (int j = 0; j < count; j++) {
            // If a specific multi-stage goal stage is linked, only show it when that stage is active
            if (goals[j].stage_id[0] != '\0') {
                bool stage_active = false;
                for (int k = 0; k < td->multi_stage_goal_count; k++) {
                    MultiStageGoal *msg = td->multi_stage_goals[k];
                    if (!msg || strcmp(msg->root_name, goals[j].root_name) != 0) continue;
                    SubGoal *active = msg->stages[msg->current_stage];
                    if (active && strcmp(active->stage_id, goals[j].stage_id) == 0) {
                        stage_active = true;
                    }
                    break;
                }
                if (!stage_active) continue; // Skip - linked stage is not the current one
            }

            if (goals[j].parent_root[0] != '\0') {
                std::string key = std::string(goals[j].parent_root) + "\t" + goals[j].root_name;
                s_linked_sub.insert(key);
            } else {
                s_linked_top.insert(goals[j].root_name);
            }
        }
    };

    // Counters: if counter matches search, add its linked goals
    for (int i = 0; i < td->counter_goal_count; i++) {
        CounterGoal *counter = td->counter_goals[i];
        if (counter_matches_search(counter, t->search_buffer)) {
            add_linked(counter->linked_goals, counter->linked_goal_count);
        }
    }

    // Text headers: if header matches search, add its linked items (no transitivity)
    for (int i = 0; i < td->decoration_count; i++) {
        DecorationElement *deco = td->decorations[i];
        if (deco->type != DECORATION_TEXT_HEADER) continue;
        if (str_contains_insensitive(deco->display_text, t->search_buffer) ||
            str_contains_insensitive(deco->id, t->search_buffer)) {
            add_linked(deco->linked_goals, deco->linked_goal_count);
        }
    }
}


/**
 * @brief Loads a GIF, converting its frames into an AnimatedTexture struct.
 * If the GIF has no frame timing information, a default delay is applied to each frame.
 * If the GIF isn't square-shaped, it is scaled to be square, that it renders properly.
 * @param renderer The SDL_Renderer to create textures with.
 * @param path The path to the .gif file.
 * @param scale_mode The SDL_ScaleMode to use when scaling the frames.
 * @return A pointer to a newly allocated AnimatedTexture, or nullptr on failure.
 */
static AnimatedTexture *load_animated_gif(SDL_Renderer *renderer, const char *path, SDL_ScaleMode scale_mode) {
    IMG_Animation *anim = IMG_LoadAnimation(path);
    if (!anim) {
        log_message(LOG_ERROR, "[TRACKER - GIF LOAD] Failed to load animation %s: %s\n", path, SDL_GetError());
        return nullptr;
    }

    AnimatedTexture *anim_texture = (AnimatedTexture *) calloc(1, sizeof(AnimatedTexture));
    if (!anim_texture) {
        IMG_FreeAnimation(anim);
        return nullptr;
    }

    anim_texture->frame_count = anim->count;
    anim_texture->frames = (SDL_Texture **) calloc(anim->count, sizeof(SDL_Texture *));
    anim_texture->delays = (int *) calloc(anim->count, sizeof(int));

    if (!anim_texture->frames || !anim_texture->delays) {
        // Allocation failed
        free(anim_texture->frames);
        anim_texture->frames = nullptr;
        free(anim_texture->delays);
        anim_texture->delays = nullptr;
        free(anim_texture);
        anim_texture = nullptr;
        IMG_FreeAnimation(anim);
        return nullptr;
    }

    Uint32 total_duration = 0;
    for (int i = 0; i < anim->count; i++) {
        SDL_Surface *frame_surface = anim->frames[i];
        SDL_Texture *final_frame_texture = nullptr;

        const int w = frame_surface->w;
        const int h = frame_surface->h;

        // If the frame is not square, use the renderer to create a new padded square texture.
        if (w != h) {
            const int side = (w > h) ? w : h;

            // 1. Create a temporary texture from the original non-square surface.
            SDL_Texture *temp_texture = SDL_CreateTextureFromSurface(renderer, frame_surface);
            if (!temp_texture) {
                SDL_Log("Failed to create temporary texture from GIF frame: %s", SDL_GetError());
                log_message(LOG_ERROR, "Failed to create temporary texture from GIF frame: %s", SDL_GetError());
                anim_texture->frames[i] = nullptr; // Mark as failed
                continue; // Skip to next frame
            }

            // 2. Create the new, blank square texture. It must be a "render target".
            final_frame_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, side,
                                                    side);
            if (final_frame_texture) {
                // Make the texture support transparency
                SDL_SetTextureBlendMode(final_frame_texture, SDL_BLENDMODE_BLEND);

                // 3. Set the renderer to draw onto our new texture instead of the window.
                SDL_SetRenderTarget(renderer, final_frame_texture);

                // 4. Clear the new texture with a fully transparent color.
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_RenderClear(renderer);

                // 5. Define where the original image will be drawn on the new texture.
                SDL_FRect dest_rect = {(float) (side - w) / 2.0f, (float) (side - h) / 2.0f, (float) w, (float) h};

                // 6. Render the temporary texture onto our new square texture.
                SDL_RenderTexture(renderer, temp_texture, nullptr, &dest_rect);

                // 7. Reset the renderer to draw back to the window.
                SDL_SetRenderTarget(renderer, nullptr);
            }

            // 8. Clean up the temporary texture.
            SDL_DestroyTexture(temp_texture);
        } else {
            // If the frame was already square, just create the texture directly.
            // Convert to a standard format first for robustness.
            SDL_Surface *formatted_surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
            if (formatted_surface) {
                SDL_BlitSurface(frame_surface, nullptr, formatted_surface, nullptr);
                final_frame_texture = SDL_CreateTextureFromSurface(renderer, formatted_surface);
                SDL_DestroySurface(formatted_surface);
            } else {
                final_frame_texture = SDL_CreateTextureFromSurface(renderer, frame_surface);
            }
        }

        anim_texture->frames[i] = final_frame_texture;

        if (!anim_texture->frames[i]) {
            log_message(LOG_ERROR, "[TRACKER - GIF LOAD] Failed to create texture for frame %d from %s\n", i, path);
            // Cleanup if a frame texture fails to create
            free(anim_texture->frames);
            anim_texture->frames = nullptr;
            free(anim_texture->delays);
            anim_texture->delays = nullptr;
            free(anim_texture);
            anim_texture = nullptr;
            IMG_FreeAnimation(anim);
            return nullptr;
        }

        SDL_SetTextureBlendMode(anim_texture->frames[i], SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(anim_texture->frames[i], scale_mode);
        anim_texture->delays[i] = anim->delays[i];
        total_duration += anim->delays[i];
    }
    anim_texture->total_duration = total_duration;

    // If the GIF has no timing info, calculate a default animation speed
    if (anim_texture->total_duration == 0 && anim_texture->frame_count > 0) {
        log_message(LOG_INFO, "[TRACKER - GIF LOAD] GIF at '%s' has no timing info. Applying default %dms delay.\n",
                    path,
                    DEFAULT_GIF_DELAY_MS);
        total_duration = 0; // Reset to recalculate
        for (int i = 0; i < anim_texture->frame_count; i++) {
            anim_texture->delays[i] = DEFAULT_GIF_DELAY_MS;
            total_duration += DEFAULT_GIF_DELAY_MS;
        }
        anim_texture->total_duration = total_duration;
    }

    IMG_FreeAnimation(anim);
    return anim_texture;
}

/**
 * @brief Reloads the global background textures based on current settings.
 * Uses the texture cache for efficiency. Handles missing files by attempting defaults.
 * @param t Tracker instance.
 * @param settings Current application settings.
 */
static void tracker_reload_background_textures(Tracker *t, const AppSettings *settings) {
    if (!t || !settings) return;

    log_message(LOG_INFO, "[TRACKER] Reloading background textures...\n");
    char full_path[MAX_PATH_LENGTH];

    // Helper lambda to load one background
    auto load_bg = [&](const char *setting_path, const char *default_path,
                       SDL_Texture **tex_target, AnimatedTexture **anim_target) {
        *tex_target = nullptr;
        *anim_target = nullptr;
        snprintf(full_path, sizeof(full_path), "%s/gui/%s", get_application_dir(), setting_path);

        if (strstr(full_path, ".gif")) {
            *anim_target = get_animated_texture_from_cache(t->renderer, &t->anim_cache, &t->anim_cache_count,
                                                           &t->anim_cache_capacity, full_path, SDL_SCALEMODE_NEAREST);
        } else {
            *tex_target = get_texture_from_cache(t->renderer, &t->texture_cache, &t->texture_cache_count,
                                                 &t->texture_cache_capacity, full_path, SDL_SCALEMODE_NEAREST);
        }

        // Fallback if loading failed
        if (!*tex_target && !*anim_target) {
            log_message(LOG_ERROR, "[TRACKER] Failed to load background: %s. Trying default...\n", setting_path);
            snprintf(full_path, sizeof(full_path), "%s/gui/%s", get_application_dir(), default_path);
            if (strstr(full_path, ".gif")) {
                *anim_target = get_animated_texture_from_cache(t->renderer, &t->anim_cache, &t->anim_cache_count,
                                                               &t->anim_cache_capacity, full_path,
                                                               SDL_SCALEMODE_NEAREST);
            } else {
                *tex_target = get_texture_from_cache(t->renderer, &t->texture_cache, &t->texture_cache_count,
                                                     &t->texture_cache_capacity, full_path, SDL_SCALEMODE_NEAREST);
            }
        }
    };

    load_bg(settings->adv_bg_path, DEFAULT_ADV_BG_PATH, &t->adv_bg, &t->adv_bg_anim);
    load_bg(settings->adv_bg_half_done_path, DEFAULT_ADV_BG_HALF_DONE_PATH, &t->adv_bg_half_done,
            &t->adv_bg_half_done_anim);
    load_bg(settings->adv_bg_done_path, DEFAULT_ADV_BG_DONE_PATH, &t->adv_bg_done, &t->adv_bg_done_anim);

    if ((!t->adv_bg && !t->adv_bg_anim) ||
        (!t->adv_bg_half_done && !t->adv_bg_half_done_anim) ||
        (!t->adv_bg_done && !t->adv_bg_done_anim)) {
        log_message(
            LOG_ERROR, "[TRACKER] CRITICAL: Failed to load one or more default background textures during reload.\n");
    }
}


// FOR VERSION-SPECIFIC PARSERS

/**
 * @brief (Era 1: 1.0-1.6.4) Save snapshot to file to simulate local stats.
 *
 * Thus remembers the snapshot even if the tracker is closed.
 *
 * @param t A pointer to the Tracker struct.
 */
static void tracker_save_snapshot_to_file(Tracker *t) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    // Save metadata
    cJSON_AddStringToObject(root, "snapshot_world_name", t->template_data->snapshot_world_name);
    cJSON_AddNumberToObject(root, "playtime_snapshot", t->template_data->playtime_snapshot);

    // Save achievement snapshot
    cJSON *ach_snapshot = cJSON_CreateObject();
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *ach = t->template_data->advancements[i];
        // Add the achievement to the snapshot with its status
        cJSON_AddBoolToObject(ach_snapshot, ach->root_name, ach->done_in_snapshot);
    }
    cJSON_AddItemToObject(root, "achievements", ach_snapshot);

    // Save stat snapshot
    cJSON *stat_snapshot = cJSON_CreateObject();
    for (int i = 0; i < t->template_data->stat_count; i++) {
        for (int j = 0; j < t->template_data->stats[i]->criteria_count; j++) {
            TrackableItem *sub_stat = t->template_data->stats[i]->criteria[j];

            // IMPORTANT Preventing duplicate entries in snapshot file
            if (!cJSON_HasObjectItem(stat_snapshot, sub_stat->root_name)) {
                cJSON_AddNumberToObject(stat_snapshot, sub_stat->root_name, sub_stat->initial_progress);
            }
        }
    }
    cJSON_AddItemToObject(root, "stats", stat_snapshot);

    // Write to file
    FILE *file = fopen(t->snapshot_path, "w");
    if (file) {
        char *json_str = cJSON_Print(root);
        fputs(json_str, file);
        fclose(file);
        free(json_str);
        json_str = nullptr;
        log_message(LOG_INFO, "[TRACKER] Snapshot saved to %s\n", t->snapshot_path);
    }
    cJSON_Delete(root); // Clean up
}

/**
 * @brief (Era 1: 1.0-1.6.4) Load snapshot from file to simulate local stats.
 *
 * This is important when the tracker gets re-opened so it can "remember" the snapshot.
 *
 * @param t A pointer to the Tracker struct.
 */
static void tracker_load_snapshot_from_file(Tracker *t, const AppSettings *settings) {
    (void) settings;

    cJSON *snapshot_json = cJSON_from_file(t->snapshot_path);
    if (!snapshot_json) {
        log_message(LOG_INFO, "[TRACKER] No existing snapshot file found for this configuration.\n");

        return;
    }

    // Load metadata
    // It should keep the snapshot as long as player is on the same world
    cJSON *world_name_json = cJSON_GetObjectItem(snapshot_json, "snapshot_world_name");
    if (cJSON_IsString(world_name_json)) {
        strncpy(t->template_data->snapshot_world_name, world_name_json->valuestring, MAX_PATH_LENGTH - 1);
        t->template_data->snapshot_world_name[MAX_PATH_LENGTH - 1] = '\0';
    }
    cJSON *playtime_json = cJSON_GetObjectItem(snapshot_json, "playtime_snapshot");
    if (cJSON_IsNumber(playtime_json)) {
        t->template_data->playtime_snapshot = playtime_json->valuedouble;
    }

    // Load achievement snapshot
    cJSON *ach_snapshot = cJSON_GetObjectItem(snapshot_json, "achievements");
    if (ach_snapshot) {
        for (int i = 0; i < t->template_data->advancement_count; i++) {
            TrackableCategory *ach = t->template_data->advancements[i];
            ach->done_in_snapshot = cJSON_IsTrue(cJSON_GetObjectItem(ach_snapshot, ach->root_name));
        }
    }

    // Load stat snapshot
    cJSON *stat_snapshot = cJSON_GetObjectItem(snapshot_json, "stats");
    if (stat_snapshot) {
        for (int i = 0; i < t->template_data->stat_count; i++) {
            for (int j = 0; j < t->template_data->stats[i]->criteria_count; j++) {
                TrackableItem *sub_stat = t->template_data->stats[i]->criteria[j];
                cJSON *stat_val = cJSON_GetObjectItem(stat_snapshot, sub_stat->root_name);
                if (cJSON_IsNumber(stat_val)) {
                    sub_stat->initial_progress = stat_val->valueint;
                }
            }
        }
    }
    cJSON_Delete(snapshot_json);
    log_message(LOG_INFO, "[TRACKER] Snapshot successfully loaded from %s\n", t->snapshot_path);
}

/**
 * @brief (Era 1: 1.0-1.6.4) Takes a snapshot of the current global stats including achievements.
 * This is called when a new world is loaded to establish a baseline for progress.
 */
static void tracker_snapshot_legacy_stats(Tracker *t, const AppSettings *settings) {
    (void) settings;
    cJSON *player_stats_json = cJSON_from_file(t->stats_path);
    if (!player_stats_json) {
        log_message(LOG_ERROR, "[TRACKER] Could not read stats file to create snapshot.\n");
        return;
    }

    // Get the stats-change array within the stats object
    cJSON *stats_change = cJSON_GetObjectItem(player_stats_json, "stats-change");
    if (!cJSON_IsArray(stats_change)) {
        cJSON_Delete(player_stats_json);
        return;
    }

    // Set the snapshot world name
    strncpy(t->template_data->snapshot_world_name, t->world_name, MAX_PATH_LENGTH - 1);
    t->template_data->snapshot_world_name[MAX_PATH_LENGTH - 1] = '\0';
    t->template_data->playtime_snapshot = 0;

    // Reset all initial progress fields first
    for (int i = 0; i < t->template_data->stat_count; i++) {
        for (int j = 0; j < t->template_data->stats[i]->criteria_count; j++) {
            t->template_data->stats[i]->criteria[j]->initial_progress = 0;
        }
    }
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        t->template_data->advancements[i]->done_in_snapshot = false;
    }


    // Iterate the live stats and store them as the initial values
    cJSON *stat_entry;
    cJSON_ArrayForEach(stat_entry, stats_change) {
        cJSON *item = stat_entry->child; // Then we go into the curly brackets
        if (item) {
            const char *key = item->string;
            int value = item->valueint;

            // Snapshot playtime
            if (strcmp(key, "1100") == 0) {
                t->template_data->playtime_snapshot = value;
            }

            // Snapshot other tracked stats (including playtime)
            for (int i = 0; i < t->template_data->stat_count; i++) {
                for (int j = 0; j < t->template_data->stats[i]->criteria_count; j++) {
                    TrackableItem *sub_stat = t->template_data->stats[i]->criteria[j];
                    if (strcmp(sub_stat->root_name, key) == 0) {
                        sub_stat->initial_progress = value;
                    }
                }
            }

            // Snapshot achievements
            for (int i = 0; i < t->template_data->advancement_count; i++) {
                TrackableCategory *ach = t->template_data->advancements[i];
                if (strcmp(ach->root_name, key) == 0 && value >= 1) {
                    ach->done_in_snapshot = true;
                }
            }
        }
    }
    cJSON_Delete(player_stats_json);

    // SAVE TO SNAPSHOT FILE
    tracker_save_snapshot_to_file(t);

    // ---- DEBUGGING CODE TO PRINT WHAT THE SNAPSHOT LOOKS LIKE ----
    log_message(LOG_INFO, "\n--- STARTING SNAPSHOT FOR WORLD: %s ---\n", t->template_data->snapshot_world_name);

    // Re-load the JSON file just for this debug print (this is inefficient but simple for debugging)
    cJSON *debug_json = cJSON_from_file(t->stats_path);
    if (debug_json) {
        cJSON *stats_change = cJSON_GetObjectItem(debug_json, "stats-change");
        if (cJSON_IsArray(stats_change)) {
            log_message(LOG_INFO, "\n--- LEGACY ACHIEVEMENT CHECK ---\n");
            // Loop through achievements from the template
            for (int i = 0; i < t->template_data->advancement_count; i++) {
                TrackableCategory *ach = t->template_data->advancements[i];
                bool found = false;
                int value = 0;

                // Search for this achievement in the player's stats data
                cJSON *stat_entry;
                cJSON_ArrayForEach(stat_entry, stats_change) {
                    cJSON *item = stat_entry->child;
                    if (item && strcmp(item->string, ach->root_name) == 0) {
                        found = true;
                        value = item->valueint;
                        break;
                    }
                }

                if (found) {
                    log_message(LOG_INFO, "  - Achievement '%s' (ID: %s): FOUND with value: %d\n", ach->display_name,
                                ach->root_name,
                                value);
                } else {
                    log_message(LOG_INFO, "  - Achievement '%s' (ID: %s): NOT FOUND in player data\n",
                                ach->display_name,
                                ach->root_name);
                }
            }
        }
        cJSON_Delete(debug_json);
    }


    log_message(LOG_INFO, "\n--- LEGACY STAT SNAPSHOT ---\n");
    log_message(LOG_INFO, "Playtime Snapshot: %lld ticks\n", t->template_data->playtime_snapshot);

    // Use a nested loop to print the stats
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        log_message(LOG_INFO, "  - Category '%s':\n", stat_cat->display_name);
        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];
            log_message(LOG_INFO, "    - Sub-Stat '%s' (ID: %s): Snapshot Value = %d\n",
                        sub_stat->display_name, sub_stat->root_name, sub_stat->initial_progress);
        }
    }
    log_message(LOG_INFO, "--- END OF SNAPSHOT ---\n\n");
}

/**
 * @brief Resets all manual progress when a world change is detected.
 * This includes custom goal progress and manual stat overrides. It then
 * saves these reset values back to settings.json.
 * @param t A pointer to the Tracker struct.
 * @param settings A pointer to the AppSettings struct.
 */
static void tracker_reset_progress_on_world_change(Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data) return;

    log_message(LOG_INFO, "[TRACKER] World change detected. Resetting custom progress and manual overrides.\n");

    // Reset custom goals
    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        TrackableItem *item = t->template_data->custom_goals[i];
        if (item) {
            item->progress = 0;
            item->done = false;
        }
    }

    // Reset stat overrides
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        if (stat_cat) {
            stat_cat->is_manually_completed = false;
            for (int j = 0; j < stat_cat->criteria_count; j++) {
                TrackableItem *sub_stat = stat_cat->criteria[j];
                if (sub_stat) {
                    sub_stat->is_manually_completed = false;
                }
            }
        }
    }

    // Save the reset progress back to the settings file
    settings_save(settings, t->template_data, SAVE_CONTEXT_ALL);
}


/**
 * @brief (Era 1: 1.0-1.6.4) Parses legacy .dat stats files.
 */
static void tracker_update_stats_legacy(Tracker *t, const cJSON *player_stats_json) {
    if (!player_stats_json) return;

    cJSON *stats_change = cJSON_GetObjectItem(player_stats_json, "stats-change");
    if (!cJSON_IsArray(stats_change)) return;

    // Reset achievement counts before updating
    t->template_data->advancements_completed_count = 0;

    // Loop to update achievement progress
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *ach = t->template_data->advancements[i];
        bool is_currently_done = false;

        cJSON *stat_entry;
        cJSON_ArrayForEach(stat_entry, stats_change) {
            cJSON *item = stat_entry->child;
            if (item && strcmp(item->string, ach->root_name) == 0 && item->valueint >= 1) {
                is_currently_done = true;
                break;
            }
        }
        ach->done = is_currently_done;

        // If the achievement is marked as done in the player's file,
        // it should always count towards the total, regardless of the snapshot.
        // Previously it was with !ach->done_in_snapshot
        if (ach->done) t->template_data->advancements_completed_count++;
    }

    cJSON *settings_json = cJSON_from_file(get_settings_file_path());
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : nullptr;

    t->template_data->play_time_ticks = 0;
    t->template_data->frozen_play_time_ticks = 0;
    t->template_data->run_completed = false;
    t->template_data->stats_completed_count = 0;
    t->template_data->stats_completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        stat_cat->completed_criteria_count = 0;

        // Check for parent override
        cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : nullptr;
        bool parent_forced_true = cJSON_IsBool(parent_override) && cJSON_IsTrue(parent_override);
        stat_cat->is_manually_completed = parent_forced_true;

        // Loop through each SUB-STAT
        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];

            // Always get live progress from file first
            sub_stat->progress = 0;
            cJSON *stat_entry;
            cJSON_ArrayForEach(stat_entry, stats_change) {
                cJSON *item = stat_entry->child;
                if (item && strcmp(item->string, sub_stat->root_name) == 0) {
                    int diff = item->valueint - sub_stat->initial_progress; // Difference to snapshot
                    sub_stat->progress = diff > 0 ? diff : 0;
                    break;
                }
            }

            // When completed through playing
            bool naturally_done = (sub_stat->goal > 0 && sub_stat->progress >= sub_stat->goal);

            // For single-criterion stats, the override key is the parent's.
            // For multi-criterion, it's the specific sub-stat key.
            // Creates ONE criteria behind the scenes, even if template doesn't have "criteria" field
            cJSON *sub_override;
            if (stat_cat->criteria_count == 1) {
                // WHEN IT HAS NO SUB-STATS
                sub_override = parent_override;
            } else {
                // Multi-criterion
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                         sub_stat->root_name);
                sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : nullptr;
            }

            bool sub_forced_true = cJSON_IsBool(sub_override) && cJSON_IsTrue(sub_override);
            sub_stat->is_manually_completed = sub_forced_true;


            // Sub-stat is done if it's naturally completed OR manually forced to be true (sub-stat or parent)
            sub_stat->done = naturally_done || sub_forced_true || parent_forced_true;

            // Increment category's completed count
            if (sub_stat->done) {
                stat_cat->completed_criteria_count++;
            }
        }

        // Determine final 'done' status for PARENT category
        bool all_children_done = (stat_cat->criteria_count > 0 && stat_cat->completed_criteria_count >= stat_cat->
                                  criteria_count);

        // A category is done if all its children are done OR it's manually forced to be true
        stat_cat->done = all_children_done || parent_forced_true;

        if (stat_cat->done) t->template_data->stats_completed_count++;
        t->template_data->stats_completed_criteria_count += stat_cat->completed_criteria_count;
    }

    // Handle playtime separately
    cJSON *stat_entry;
    cJSON_ArrayForEach(stat_entry, stats_change) {
        cJSON *item = stat_entry->child;
        if (item && strcmp(item->string, "1100") == 0) {
            long long diff = item->valueint - t->template_data->playtime_snapshot;
            t->template_data->play_time_ticks = (diff > 0) ? diff : 0;
            break;
        }
    }

    cJSON_Delete(settings_json);
}

/**
 * @brief (Era 2: 1.7.2-1.11.2) Parses unified JSON with achievements and stats.
 */
static void tracker_update_achievements_and_stats_mid(Tracker *t, const cJSON *player_stats_json) {
    if (!player_stats_json) return;

    t->template_data->advancements_completed_count = 0;
    t->template_data->completed_criteria_count = 0;
    t->template_data->play_time_ticks = 0;
    t->template_data->frozen_play_time_ticks = 0;
    t->template_data->run_completed = false;

    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *ach = t->template_data->advancements[i];
        ach->completed_criteria_count = 0;
        ach->done = false; // Reset done status
        ach->all_template_criteria_met = false;

        cJSON *ach_entry = cJSON_GetObjectItem(player_stats_json, ach->root_name);
        if (!ach_entry) continue;

        // First, update the progress of all criteria defined in the template
        if (ach->criteria_count > 0) {
            cJSON *progress_array = cJSON_GetObjectItem(ach_entry, "progress");
            if (cJSON_IsArray(progress_array)) {
                for (int j = 0; j < ach->criteria_count; j++) {
                    TrackableItem *crit = ach->criteria[j];
                    crit->done = false; // Reset criteria status
                    cJSON *progress_item;
                    cJSON_ArrayForEach(progress_item, progress_array) {
                        if (cJSON_IsString(progress_item) && strcmp(progress_item->valuestring, crit->root_name) == 0) {
                            crit->done = true;
                            ach->completed_criteria_count++;
                            break;
                        }
                    }
                }
            }
        }

        // Determine if the game file considers the achievement done
        bool game_is_done = false;
        if (cJSON_IsNumber(ach_entry)) {
            // Case for simple achievements, e.g. "achievement.buildHoe": 1
            game_is_done = (ach_entry->valueint >= 1);
        } else if (cJSON_IsObject(ach_entry)) {
            // Casse for complex achievements, e.g. "achievement.exploreAllBiomes": {"value": 1, "progress":["Forest", ...]}
            cJSON *value_item = cJSON_GetObjectItem(ach_entry, "value");
            game_is_done = (cJSON_IsNumber(value_item) && value_item->valueint >= 1);
        }

        // Determine final 'done' status either when all criteria from template are done or game says so
        if (ach->criteria_count > 0) {
            // Explicitly set the new flag for hiding logic
            ach->all_template_criteria_met = (ach->completed_criteria_count >= ach->criteria_count);
            ach->done = game_is_done || ach->all_template_criteria_met;
        } else {
            ach->all_template_criteria_met = game_is_done;
            ach->done = game_is_done;
        }

        // Increment completed achievement count
        if (ach->done) t->template_data->advancements_completed_count++;
        t->template_data->completed_criteria_count += ach->completed_criteria_count;
    }

    // Stats logic with sub-stats
    cJSON *settings_json = cJSON_from_file(get_settings_file_path());
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : nullptr;

    t->template_data->stats_completed_count = 0;
    t->template_data->stats_completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        // Iterate through stats
        TrackableCategory *stat_cat = t->template_data->stats[i];
        stat_cat->completed_criteria_count = 0;

        cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : nullptr;
        bool parent_forced_true = cJSON_IsBool(parent_override) && cJSON_IsTrue(parent_override);
        stat_cat->is_manually_completed = parent_forced_true;

        // Iterate through sub-stats
        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];

            cJSON *stat_entry = cJSON_GetObjectItem(player_stats_json, sub_stat->root_name);
            sub_stat->progress = cJSON_IsNumber(stat_entry) ? stat_entry->valueint : 0;

            // Determine natural completion
            bool naturally_done = (sub_stat->goal > 0 && sub_stat->progress >= sub_stat->goal);

            // Check for sub-stat override
            cJSON *sub_override;
            // If there are no sub-stats, use parent override (NO ".criteria." used for "regular" stats)
            // Creates ONE criteria behind the scenes, even if template doesn't have "criteria" field
            if (stat_cat->criteria_count == 1) {
                sub_override = parent_override;
            } else {
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                         sub_stat->root_name);
                sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : nullptr;
            }

            bool sub_forced_true = cJSON_IsBool(sub_override) && cJSON_IsTrue(sub_override);
            sub_stat->is_manually_completed = sub_forced_true;


            // Either naturally done OR manually overridden to be true  (itself or by parent)
            sub_stat->done = naturally_done || sub_forced_true || parent_forced_true;

            // Increment completed count
            if (sub_stat->done) stat_cat->completed_criteria_count++;
        }

        bool all_children_done = (stat_cat->criteria_count > 0 && stat_cat->completed_criteria_count >= stat_cat->
                                  criteria_count);

        // Either all children done OR parent manually overridden to be true
        stat_cat->done = all_children_done || parent_forced_true;

        if (stat_cat->done) t->template_data->stats_completed_count++;
        t->template_data->stats_completed_criteria_count += stat_cat->completed_criteria_count;
    }

    cJSON_Delete(settings_json);

    // Update mid-era playtime
    cJSON *play_time_entry = cJSON_GetObjectItem(player_stats_json, "stat.playOneMinute");
    if (cJSON_IsNumber(play_time_entry)) {
        t->template_data->play_time_ticks = (long long) play_time_entry->valuedouble;
    }
}

/**
 * @brief (Era 2/3: 1.7.2-1.12.2) Parses mid-era flat JSON stats files.
 * Specifically used for 1.12-1.12.2 as it has modern advancements, but mid-era stats formats.
 */
static void tracker_update_stats_mid(Tracker *t, const cJSON *player_stats_json, const cJSON *settings_json) {
    if (!player_stats_json) return;

    // Stats logic with sub-stats
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : nullptr;

    t->template_data->stats_completed_count = 0;
    t->template_data->stats_completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        // Iterate through stats
        TrackableCategory *stat_cat = t->template_data->stats[i];
        stat_cat->completed_criteria_count = 0;

        cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : nullptr;
        bool parent_forced_true = cJSON_IsBool(parent_override) && cJSON_IsTrue(parent_override);
        stat_cat->is_manually_completed = parent_forced_true;

        // Iterate through sub-stats
        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];

            cJSON *stat_entry = cJSON_GetObjectItem(player_stats_json, sub_stat->root_name);
            sub_stat->progress = cJSON_IsNumber(stat_entry) ? stat_entry->valueint : 0;

            // Determine natural completion
            bool naturally_done = (sub_stat->goal > 0 && sub_stat->progress >= sub_stat->goal);

            // Check for sub-stat override
            cJSON *sub_override;
            // If there are no sub-stats, use parent override (NO ".criteria." used for "regular" stats)
            // Creates ONE criteria behind the scenes, even if template doesn't have "criteria" field
            if (stat_cat->criteria_count == 1) {
                sub_override = parent_override;
            } else {
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                         sub_stat->root_name);
                sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : nullptr;
            }

            bool sub_forced_true = cJSON_IsBool(sub_override) && cJSON_IsTrue(sub_override);
            sub_stat->is_manually_completed = sub_forced_true;


            // Either naturally done OR manually overridden to be true  (itself or by parent)
            sub_stat->done = naturally_done || sub_forced_true || parent_forced_true;

            // Increment completed count
            if (sub_stat->done) stat_cat->completed_criteria_count++;
        }

        bool all_children_done = (stat_cat->criteria_count > 0 && stat_cat->completed_criteria_count >= stat_cat->
                                  criteria_count);

        // Either all children done OR parent manually overridden to be true
        stat_cat->done = all_children_done || parent_forced_true;

        if (stat_cat->done) t->template_data->stats_completed_count++;
        t->template_data->stats_completed_criteria_count += stat_cat->completed_criteria_count;
    }

    // Update mid-era playtime
    cJSON *play_time_entry = cJSON_GetObjectItem(player_stats_json, "stat.playOneMinute");
    if (cJSON_IsNumber(play_time_entry)) {
        t->template_data->play_time_ticks = (long long) play_time_entry->valuedouble;
    }
}

/**
 * @brief (Era 3: 1.12+) Updates advancement progress from modern JSON files.
 * It marks an advancement as completed if all of its criteria within the template are completed.
 * @param t Pointer to the tracker struct.
 * @param player_adv_json Pointer to the player advancements cJSON object.
 */
static void tracker_update_advancements_modern(Tracker *t, const cJSON *player_adv_json) {
    if (!player_adv_json) return;

    t->template_data->advancements_completed_count = 0;
    t->template_data->completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *adv = t->template_data->advancements[i];
        adv->completed_criteria_count = 0;
        adv->done = false; // Reset done status before re-evaluating
        adv->all_template_criteria_met = false;

        cJSON *player_entry = cJSON_GetObjectItem(player_adv_json, adv->root_name);
        // take root name (from template) from player advancements
        if (player_entry) {
            // Always update criteria progress first
            cJSON *player_criteria = cJSON_GetObjectItem(player_entry, "criteria");
            if (player_criteria && adv->criteria_count > 0) {
                // If the template has criteria, check them against player data
                for (int j = 0; j < adv->criteria_count; j++) {
                    TrackableItem *crit = adv->criteria[j];
                    if (cJSON_HasObjectItem(player_criteria, crit->root_name)) {
                        crit->done = true;
                        adv->completed_criteria_count++;
                    } else {
                        crit->done = false;
                    }
                }
            }

            // Determine 'done' status when all criteria within the template are done OR the game file marks it as done
            bool game_is_done = cJSON_IsTrue(cJSON_GetObjectItem(player_entry, "done"));
            if (adv->criteria_count > 0) {
                // If template has criteria, it's done if the game says so OR all criteria are done
                // Explicitly set the new flag for hiding logic
                adv->all_template_criteria_met = (adv->completed_criteria_count >= adv->criteria_count);
                adv->done = game_is_done || adv->all_template_criteria_met;
            } else {
                // If no criteria are in the template, fall back to the game file's "done" status
                adv->all_template_criteria_met = game_is_done;
                adv->done = game_is_done;
            }

            // Increment completed advancement count when it's NOT a recipe
            if (adv->done && !adv->is_recipe) t->template_data->advancements_completed_count++;
        } else {
            // If the advancement doesn't exist in the player file, it's not done and neither are its criteria
            adv->done = false;
            for (int j = 0; j < adv->criteria_count; j++) {
                adv->criteria[j]->done = false;
            }
        }
        // Add completed criteria count for progress calculation
        t->template_data->completed_criteria_count += adv->completed_criteria_count;
    }
}

/**
 * @brief (Era 3: 1.12+) Updates stat progress from modern JSON files.
 */
static void tracker_update_stats_modern(Tracker *t, const cJSON *player_stats_json, const cJSON *settings_json,
                                        MC_Version version) {
    if (!player_stats_json) return;

    cJSON *stats_obj = cJSON_GetObjectItem(player_stats_json, "stats");
    if (!stats_obj) return;

    // Get the playtime
    cJSON *custom_stats = cJSON_GetObjectItem(stats_obj, "minecraft:custom");
    if (custom_stats) {
        // Use play_one_minute for 1.12 -> 1.16.5 and play_time for 1.17+
        const char *playtime_key = (version >= MC_VERSION_1_17) ? "minecraft:play_time" : "minecraft:play_one_minute";
        cJSON *play_time = cJSON_GetObjectItem(custom_stats, playtime_key);
        if (cJSON_IsNumber(play_time)) {
            t->template_data->play_time_ticks = (long long) play_time->valuedouble;
        }
    }

    // Manually override the stat progress
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : nullptr;

    t->template_data->stats_completed_count = 0;
    t->template_data->stats_completed_criteria_count = 0;

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        stat_cat->completed_criteria_count = 0;

        // Override check
        cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : nullptr;
        bool parent_forced_true = cJSON_IsBool(parent_override) && cJSON_IsTrue(parent_override);
        stat_cat->is_manually_completed = parent_forced_true;

        // Loop through all sub stats
        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];
            sub_stat->progress = 0;

            char root_name_copy[192];
            strncpy(root_name_copy, sub_stat->root_name, sizeof(root_name_copy) - 1);
            root_name_copy[sizeof(root_name_copy) - 1] = '\0';

            // Use pre-parsed keys for lookup
            if (sub_stat->stat_category_key[0] != '\0') {
                cJSON *category_obj = cJSON_GetObjectItem(stats_obj, sub_stat->stat_category_key);
                if (category_obj) {
                    cJSON *stat_value = cJSON_GetObjectItem(category_obj, sub_stat->stat_item_key);
                    if (cJSON_IsNumber(stat_value)) {
                        sub_stat->progress = stat_value->valueint;
                    }
                }
            }

            // Determine natural completion
            bool naturally_done = (sub_stat->goal > 0 && sub_stat->progress >= sub_stat->goal);

            cJSON *sub_override;
            // If there are no sub stats, so "regular" stat doesn't use ".criteria.", then just counts as parent
            // Creates ONE criteria behind the scenes, even if template doesn't have "criteria" field
            if (stat_cat->criteria_count == 1) {
                sub_override = parent_override;
            } else {
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                         sub_stat->root_name);
                sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : nullptr;
            }
            bool sub_forced_true = cJSON_IsBool(sub_override) && cJSON_IsTrue(sub_override);
            sub_stat->is_manually_completed = sub_forced_true;

            // Either naturally done OR manually overridden to true (itself OR parent)
            sub_stat->done = naturally_done || sub_forced_true || parent_forced_true;

            if (sub_stat->done) stat_cat->completed_criteria_count++;
        }

        bool all_children_done = (stat_cat->criteria_count > 0 && stat_cat->completed_criteria_count >= stat_cat->
                                  criteria_count);

        stat_cat->done = all_children_done || parent_forced_true;

        // Update the percentage progress properly
        if (stat_cat->done) t->template_data->stats_completed_count++;
        t->template_data->stats_completed_criteria_count += stat_cat->completed_criteria_count;
    }
}

/**
 * @brief Parses linked goals and mode from a JSON object into C arrays.
 * Used for stat auto-completion. Allocates linked_goals array with calloc.
 */
static void parse_runtime_linked_goals(cJSON *json_obj, int *out_count, CounterLinkedGoal **out_goals,
                                       LinkedGoalMode *out_mode) {
    *out_count = 0;
    *out_goals = nullptr;
    *out_mode = LINKED_GOAL_AND;

    cJSON *linked_json = cJSON_GetObjectItem(json_obj, "linked_goals");
    if (!linked_json) return;

    int count = cJSON_GetArraySize(linked_json);
    if (count <= 0) return;

    *out_goals = (CounterLinkedGoal *) calloc(count, sizeof(CounterLinkedGoal));
    if (!*out_goals) return;
    *out_count = count;

    int idx = 0;
    cJSON *lg_json;
    cJSON_ArrayForEach(lg_json, linked_json) {
        CounterLinkedGoal *lg = &(*out_goals)[idx];
        cJSON *lr = cJSON_GetObjectItem(lg_json, "root_name");
        cJSON *ls = cJSON_GetObjectItem(lg_json, "stage_id");
        cJSON *lp = cJSON_GetObjectItem(lg_json, "parent_root");
        if (cJSON_IsString(lr)) {
            strncpy(lg->root_name, lr->valuestring, sizeof(lg->root_name) - 1);
            lg->root_name[sizeof(lg->root_name) - 1] = '\0';
        }
        if (cJSON_IsString(ls)) {
            strncpy(lg->stage_id, ls->valuestring, sizeof(lg->stage_id) - 1);
            lg->stage_id[sizeof(lg->stage_id) - 1] = '\0';
        }
        if (cJSON_IsString(lp)) {
            strncpy(lg->parent_root, lp->valuestring, sizeof(lg->parent_root) - 1);
            lg->parent_root[sizeof(lg->parent_root) - 1] = '\0';
        }
        idx++;
    }

    cJSON *mode_json = cJSON_GetObjectItem(json_obj, "linked_goal_mode");
    if (cJSON_IsString(mode_json) && strcmp(mode_json->valuestring, "or") == 0) {
        *out_mode = LINKED_GOAL_OR;
    }
}

// HELPER FUNCTION FOR PARSING
/**
 * @brief Parses advancement or stat categories and their criteria from the template, supporting modded advancements/stats.
 * It supports .gif and .png files.
 *
 * @param t A pointer to the Tracker object.
 * @param category_json The JSON object containing the categories.
 * @param lang_json The JSON object containing the language keys.
 * @param categories_array A pointer to an array of TrackableCategory pointers to store the parsed categories.
 * @param count A pointer to an integer to store the number of parsed categories.
 * @param total_criteria_count A pointer to an integer to store the total number of criteria across all categories.
 * @param lang_key_prefix The prefix for language keys. (e.g., "advancement." or "stat.")
 * @param is_stat_category A boolean indicating whether the categories are for stats. False means advancements.
 * @param version The Minecraft version.
 * @param settings A pointer to the AppSettings object.
 */
static void tracker_parse_categories(Tracker *t, cJSON *category_json, cJSON *lang_json,
                                     TrackableCategory ***categories_array,
                                     int *count, int *total_criteria_count, const char *lang_key_prefix,
                                     bool is_stat_category, MC_Version version, const AppSettings *settings) {
    (void) settings;
    if (!category_json) {
        log_message(LOG_INFO, "[TRACKER] tracker_parse_categories: category_json is nullptr\n");

        return;
    }

    *count = 0;
    for (cJSON *i = category_json->child; i != nullptr; i = i->next) (*count)++;
    if (*count == 0) return;

    *categories_array = (TrackableCategory **) calloc(*count, sizeof(TrackableCategory *));
    if (!*categories_array) return;

    cJSON *cat_json = category_json->child;
    int i = 0;
    *total_criteria_count = 0;

    while (cat_json) {
        TrackableCategory *new_cat = (TrackableCategory *) calloc(1, sizeof(TrackableCategory));
        if (!new_cat) {
            cat_json = cat_json->next;
            continue;
        }

        new_cat->alpha = 1.0f;
        new_cat->is_visible_on_overlay = true;
        new_cat->is_hidden = cJSON_IsTrue(cJSON_GetObjectItem(cat_json, "hidden")); // Hide if hidden in template
        new_cat->is_recipe = cJSON_IsTrue(cJSON_GetObjectItem(cat_json, "is_recipe"));
        new_cat->in_2nd_row = cJSON_IsTrue(cJSON_GetObjectItem(cat_json, "in_2nd_row")); // 2nd row of overlay
        new_cat->in_3rd_row = cJSON_IsTrue(cJSON_GetObjectItem(cat_json, "in_3rd_row")); // 3rd row of overlay

        parse_manual_pos(cat_json, "icon_pos", &new_cat->icon_pos);
        parse_manual_pos(cat_json, "text_pos", &new_cat->text_pos);
        parse_manual_pos(cat_json, "progress_pos", &new_cat->progress_pos);

        if (cat_json->string) {
            strncpy(new_cat->root_name, cat_json->string, sizeof(new_cat->root_name) - 1);
            new_cat->root_name[sizeof(new_cat->root_name) - 1] = '\0';
        } else {
            log_message(LOG_ERROR, "[TRACKER] PARSE ERROR: Found a JSON item with a nullptr key. Skipping.\n");
            free(new_cat);
            new_cat = nullptr;
            cat_json = cat_json->next;
            continue;
        }

        char cat_lang_key[256];
        // Correctly generate the parent language key
        if (!is_stat_category) {
            // Transform for advancements
            char temp_root_name[192];
            strncpy(temp_root_name, new_cat->root_name, sizeof(temp_root_name) - 1);
            temp_root_name[sizeof(temp_root_name) - 1] = '\0';
            char *path_part = strchr(temp_root_name, ':');
            if (path_part) *path_part = '.';
            snprintf(cat_lang_key, sizeof(cat_lang_key), "%s%s", lang_key_prefix, temp_root_name);
            for (char *p = cat_lang_key; *p; p++) if (*p == '/') *p = '.';
        } else {
            // Use raw name for stats
            snprintf(cat_lang_key, sizeof(cat_lang_key), "%s%s", lang_key_prefix, new_cat->root_name);
        }

        cJSON *lang_entry = cJSON_GetObjectItem(lang_json, cat_lang_key);
        if (cJSON_IsString(lang_entry)) {
            strncpy(new_cat->display_name, lang_entry->valuestring, sizeof(new_cat->display_name) - 1);
            new_cat->display_name[sizeof(new_cat->display_name) - 1] = '\0';
        } else {
            strncpy(new_cat->display_name, new_cat->root_name, sizeof(new_cat->display_name) - 1);
            new_cat->display_name[sizeof(new_cat->display_name) - 1] = '\0';
        }


        // Determine if this is a "hidden" legacy stat used only for multi-stage goal tracking
        bool is_hidden_legacy_stat = false;
        if (is_stat_category && version <= MC_VERSION_1_6_4) {
            cJSON *criteria_obj_check = cJSON_GetObjectItem(cat_json, "criteria");
            cJSON *target = cJSON_GetObjectItem(cat_json, "target");

            // A stat is hidden if it has no criteria and a target of 0 or not present
            if ((!criteria_obj_check || !criteria_obj_check->child) && (!target || target->valueint == 0)) {
                is_hidden_legacy_stat = true;
            }
        }

        cJSON *icon = cJSON_GetObjectItem(cat_json, "icon");
        // Only load an icon if one is defined AND it's not a hidden legacy stat
        // If hidden legacy stat, don't load icon as it's a mistake in the template file
        if (cJSON_IsString(icon) && !is_hidden_legacy_stat) {
            char full_icon_path[sizeof(new_cat->icon_path)];

            // Put whatever is in "icon" into "resources/icons/"
            snprintf(full_icon_path, sizeof(full_icon_path), "%s/icons/%s", get_application_dir(), icon->valuestring);
            strncpy(new_cat->icon_path, full_icon_path, sizeof(new_cat->icon_path) - 1);
            new_cat->icon_path[sizeof(new_cat->icon_path) - 1] = '\0';

            if (strstr(full_icon_path, ".gif")) {
                new_cat->anim_texture = get_animated_texture_from_cache(t->renderer, &t->anim_cache,
                                                                        &t->anim_cache_count, &t->anim_cache_capacity,
                                                                        new_cat->icon_path, SDL_SCALEMODE_NEAREST);
            } else {
                new_cat->texture = get_texture_from_cache(t->renderer, &t->texture_cache, &t->texture_cache_count,
                                                          &t->texture_cache_capacity, new_cat->icon_path,
                                                          SDL_SCALEMODE_NEAREST);
            }
        }

        cJSON *criteria_obj = cJSON_GetObjectItem(cat_json, "criteria");
        if (criteria_obj && criteria_obj->child != nullptr) {
            // CASE B: A "criteria" block exists (for advancements or multi-stats)
            new_cat->is_single_stat_category = false; // Multi-stats regardless of amount of criteria

            for (cJSON *c = criteria_obj->child; c != nullptr; c = c->next) new_cat->criteria_count++;
            if (new_cat->criteria_count > 0) {
                new_cat->criteria = (TrackableItem **) calloc(new_cat->criteria_count, sizeof(TrackableItem *));
                *total_criteria_count += new_cat->criteria_count;
                int k = 0;
                for (cJSON *crit_item = criteria_obj->child; crit_item != nullptr; crit_item = crit_item->next) {
                    TrackableItem *new_crit = (TrackableItem *) calloc(1, sizeof(TrackableItem));
                    if (new_crit) {
                        // Initialization for animation state
                        new_crit->alpha = 1.0f;
                        new_crit->is_visible_on_overlay = true;

                        // When hidden is true in template
                        new_crit->is_hidden = cJSON_IsTrue(cJSON_GetObjectItem(crit_item, "hidden"));
                        parse_manual_pos(crit_item, "icon_pos", &new_crit->icon_pos);
                        parse_manual_pos(crit_item, "text_pos", &new_crit->text_pos);
                        new_cat->is_recipe = cJSON_IsTrue(cJSON_GetObjectItem(cat_json, "is_recipe"));

                        strncpy(new_crit->root_name, crit_item->string, sizeof(new_crit->root_name) - 1);
                        new_crit->root_name[sizeof(new_crit->root_name) - 1] = '\0';

                        // Add pre-parsing for multi-criteria stats
                        if (is_stat_category) {
                            const char *slash = strchr(new_crit->root_name, '/');
                            if (slash) {
                                ptrdiff_t len = slash - new_crit->root_name;
                                strncpy(new_crit->stat_category_key, new_crit->root_name, len);
                                new_crit->stat_category_key[len] = '\0';
                                strncpy(new_crit->stat_item_key, slash + 1, sizeof(new_crit->stat_item_key) - 1);
                                new_crit->stat_item_key[sizeof(new_crit->stat_item_key) - 1] = '\0';
                            }
                        }

                        // Read the goal value
                        if (is_stat_category) {
                            cJSON *target = cJSON_GetObjectItem(crit_item, "target");
                            if (cJSON_IsNumber(target)) new_crit->goal = target->valueint;
                        }
                        char crit_lang_key[256];
                        snprintf(crit_lang_key, sizeof(crit_lang_key), "%s.criteria.%s", cat_lang_key,
                                 new_crit->root_name);
                        cJSON *crit_lang_entry = cJSON_GetObjectItem(lang_json, crit_lang_key);
                        if (cJSON_IsString(crit_lang_entry)) {
                            strncpy(new_crit->display_name, crit_lang_entry->valuestring,
                                    sizeof(new_crit->display_name) - 1);
                            new_crit->display_name[sizeof(new_crit->display_name) - 1] = '\0';
                        } else {
                            strncpy(new_crit->display_name, new_crit->root_name, sizeof(new_crit->display_name) - 1);
                            new_crit->display_name[sizeof(new_crit->display_name) - 1] = '\0';
                        }

                        cJSON *crit_icon = cJSON_GetObjectItem(crit_item, "icon");
                        if (cJSON_IsString(crit_icon) && crit_icon->valuestring[0] != '\0') {
                            char full_crit_icon_path[sizeof(new_crit->icon_path)];
                            snprintf(full_crit_icon_path, sizeof(full_crit_icon_path), "%s/icons/%s",
                                     get_application_dir(),
                                     crit_icon->valuestring);
                            strncpy(new_crit->icon_path, full_crit_icon_path, sizeof(new_crit->icon_path) - 1);
                            new_crit->icon_path[sizeof(new_crit->icon_path) - 1] = '\0';

                            if (strstr(full_crit_icon_path, ".gif")) {
                                new_crit->anim_texture = get_animated_texture_from_cache(
                                    t->renderer, &t->anim_cache, &t->anim_cache_count, &t->anim_cache_capacity,
                                    new_crit->icon_path, SDL_SCALEMODE_NEAREST);
                            } else {
                                new_crit->texture = get_texture_from_cache(
                                    t->renderer, &t->texture_cache, &t->texture_cache_count, &t->texture_cache_capacity,
                                    new_crit->icon_path, SDL_SCALEMODE_NEAREST);
                            }
                        }

                        // Parse sub-stat linked goals for auto-completion
                        if (is_stat_category) {
                            parse_runtime_linked_goals(crit_item, &new_crit->linked_goal_count,
                                                       &new_crit->linked_goals, &new_crit->linked_goal_mode);
                        }

                        new_cat->criteria[k++] = new_crit;
                    }
                }
            }
        } else if (is_stat_category && !criteria_obj) {
            // CASE A: It's a stat defined WITHOUT a "criteria" block (a true single-stat)
            new_cat->is_single_stat_category = true;
            new_cat->criteria_count = 1;
            *total_criteria_count += 1;
            new_cat->criteria = (TrackableItem **) calloc(new_cat->criteria_count, sizeof(TrackableItem *));
            if (new_cat->criteria_count) {
                TrackableItem *the_criterion = (TrackableItem *) calloc(1, sizeof(TrackableItem));
                if (the_criterion) {
                    // This single criterion inherits properties from its parent category
                    cJSON *crit_root_name_json = cJSON_GetObjectItem(cat_json, "root_name");
                    if (cJSON_IsString(crit_root_name_json)) {
                        strncpy(the_criterion->root_name, crit_root_name_json->valuestring,
                                sizeof(the_criterion->root_name) - 1);
                        the_criterion->root_name[sizeof(the_criterion->root_name) - 1] = '\0';

                        // Pre-parse stat keys
                        const char *slash = strchr(the_criterion->root_name, '/');
                        if (slash) {
                            ptrdiff_t len = slash - the_criterion->root_name;
                            strncpy(the_criterion->stat_category_key, the_criterion->root_name, len);
                            the_criterion->stat_category_key[len] = '\0';
                            strncpy(the_criterion->stat_item_key, slash + 1, sizeof(the_criterion->stat_item_key) - 1);
                            the_criterion->stat_item_key[sizeof(the_criterion->stat_item_key) - 1] = '\0';
                        }
                    }

                    strncpy(the_criterion->display_name, new_cat->display_name,
                            sizeof(the_criterion->display_name) - 1);
                    the_criterion->display_name[sizeof(the_criterion->display_name) - 1] = '\0';
                    strncpy(the_criterion->icon_path, new_cat->icon_path, sizeof(the_criterion->icon_path) - 1);
                    the_criterion->icon_path[sizeof(the_criterion->icon_path) - 1] = '\0';
                    the_criterion->is_shared = true; // Mark for overlay indicator

                    the_criterion->icon_pos = new_cat->icon_pos;
                    the_criterion->text_pos = new_cat->text_pos;

                    cJSON *target = cJSON_GetObjectItem(cat_json, "target");
                    if (cJSON_IsNumber(target)) the_criterion->goal = target->valueint;

                    new_cat->criteria[0] = the_criterion;
                }
            }
        }

        // Parse stat category linked goals for auto-completion
        if (is_stat_category) {
            parse_runtime_linked_goals(cat_json, &new_cat->linked_goal_count,
                                       &new_cat->linked_goals, &new_cat->linked_goal_mode);
        }

        // Implicitly, if criteria_obj exists but is empty, we do nothing.
        // This correctly leaves criteria_count at 0, making it a "simple" category.
        (*categories_array)[i++] = new_cat;
        cat_json = cat_json->next;
    }
}

// FNV-1a 64-bit hashing algorithm to hash file contents
static uint64_t compute_file_hash(const char *path) {
    if (!path || path[0] == '\0') return 0;

    // --- TODO: DEBUG PRINT ---
    // If you see this spamming in the console, your cache is being reset repeatedly.
    // log_message(LOG_INFO, "[DEBUG - TRACKER - FILE HASHING] Reading file from disk: %s\n", path);
    // ----------------------------

    FILE *f = fopen(path, "rb");
    if (!f) return 0; // If file can't be read, treat as unique/empty

    uint64_t hash = 0xcbf29ce484222325ULL;
    int c;

    // Read file byte by byte (sufficient for small icons)
    // For larger files, a buffer approach would be faster, but icons are tiny.
    while ((c = fgetc(f)) != EOF) {
        hash ^= (unsigned char) c;
        hash *= 0x100000001b3ULL;
    }

    fclose(f);
    return hash;
}

// Helper for counting based on Hash instead of Path
typedef struct {
    uint64_t hash;
    int count;
} IconHashCounter;

/**
 * @brief Helper to find a hash in the texture cache (Static or Animated) or compute it if missing.
 */
static uint64_t get_image_hash_optimized(Tracker *t, const char *path) {
    if (!path || path[0] == '\0') return 0;

    // 1. Try to find it in the STATIC cache first (Fast RAM lookup)
    for (int i = 0; i < t->texture_cache_count; i++) {
        if (strcmp(t->texture_cache[i].path, path) == 0) {
            return t->texture_cache[i].file_hash;
        }
    }

    // 2. Try to find it in the ANIMATED cache (Fast RAM lookup)
    for (int i = 0; i < t->anim_cache_count; i++) {
        if (strcmp(t->anim_cache[i].path, path) == 0) {
            return t->anim_cache[i].file_hash;
        }
    }

    // 3. If not in any cache, read from disk (Slow, but should rarely happen now)
    return compute_file_hash(path);
}

// Helper function to process and count all sub-items from a list of categories based on image HASH
static int count_all_icon_hashes(Tracker *t, IconHashCounter **counts, int capacity, int current_unique_count,
                                 TrackableCategory **categories, int cat_count) {
    if (!categories) return current_unique_count;

    for (int i = 0; i < cat_count; i++) {
        if (categories[i]->is_single_stat_category) continue;

        // If the parent category is hidden, children should not contribute to shared count (not visible)
        if (categories[i]->is_hidden) continue;

        for (int j = 0; j < categories[i]->criteria_count; j++) {
            TrackableItem *crit = categories[i]->criteria[j];

            if (crit->is_hidden || crit->icon_path[0] == '\0') continue;

            // Only calculate the hash if we haven't done it yet (Lazy Loading)
            if (crit->icon_hash == 0) {
                crit->icon_hash = get_image_hash_optimized(t, crit->icon_path);
            }

            if (crit->icon_hash == 0) continue;

            bool found = false;
            for (int k = 0; k < current_unique_count; k++) {
                if ((*counts)[k].hash == crit->icon_hash) {
                    (*counts)[k].count++;
                    found = true;
                    break;
                }
            }

            if (!found && current_unique_count < capacity) {
                (*counts)[current_unique_count].hash = crit->icon_hash;
                (*counts)[current_unique_count].count = 1;
                current_unique_count++;
            }
        }
    }
    return current_unique_count;
}

// Helper function to flag the items (With Caching)
static void flag_shared_icons_by_hash(IconHashCounter *counts, int unique_count, TrackableCategory **categories,
                                      int cat_count) {
    if (!categories) return;

    for (int i = 0; i < cat_count; i++) {
        if (categories[i]->is_single_stat_category) continue;

        // Skip processing hidden parents here as well, children are also hidden
        if (categories[i]->is_hidden) continue;

        for (int j = 0; j < categories[i]->criteria_count; j++) {
            TrackableItem *crit = categories[i]->criteria[j];
            crit->is_shared = false;

            if (crit->is_hidden || crit->icon_path[0] == '\0' || crit->icon_hash == 0) continue;

            for (int k = 0; k < unique_count; k++) {
                // Compare RAM integers instead of reading files
                if (counts[k].hash == crit->icon_hash && counts[k].count > 1) {
                    crit->is_shared = true;
                    break;
                }
            }
        }
    }
}

/**
 * @brief Detects criteria that share the same image content (Hash) across multiple advancements or stats.
 *
 * This function iterates through all parsed advancements or stats and their criteria.
 * It computes a hash of the image file. If distinct criteria point to images with
 * identical binary content (even if file paths differ), 'is_shared' is set to true.
 *
 * @param t The Tracker struct.
 */
static void tracker_detect_shared_icons(Tracker *t, const AppSettings *settings) {
    (void) settings;
    int total_criteria = t->template_data->total_criteria_count + t->template_data->stat_total_criteria_count;
    if (total_criteria == 0) return;

    IconHashCounter *counts = (IconHashCounter *) calloc(total_criteria, sizeof(IconHashCounter));
    if (!counts) return;

    int unique_count = 0;

    // 1. Count occurrences (Pass 't' to use the cache)
    unique_count = count_all_icon_hashes(t, &counts, total_criteria, unique_count, t->template_data->advancements,
                                         t->template_data->advancement_count);
    unique_count = count_all_icon_hashes(t, &counts, total_criteria, unique_count, t->template_data->stats,
                                         t->template_data->stat_count);

    // 2. Flag items
    flag_shared_icons_by_hash(counts, unique_count, t->template_data->advancements,
                              t->template_data->advancement_count);
    flag_shared_icons_by_hash(counts, unique_count, t->template_data->stats, t->template_data->stat_count);

    free(counts);
    counts = nullptr;
    log_message(LOG_INFO, "[TRACKER] Shared icon detection (Hash-based) complete.\n");
}


/**
 * @brief Parses a cJSON array of simple trackable items (like unlocks or custom goals) into an array of TrackableItem structs.
 *
 * This function iterates through a JSON array, allocating and populating a TrackableItem for each entry.
 * It extracts the root name, icon path, and goal value from the template and looks up the display name in the language file.
 * The language file uses "unlock." and "custom." prefixes now as well.
 * It supports .gif and .png files.
 *
 *  @param t Pointer to the Tracker struct.
 * @param category_json The cJSON array for the "stats" or "unlocks" key from the template file.
 * @param lang_json The cJSON object from the language file to look up display names.
 * @param items_array A pointer to the array of TrackableItem pointers to be populated.
 * @param count A pointer to an integer that will store the number of items parsed.
 * @param lang_key_prefix The prefix to use when looking up display names in the language file.
 * @param settings Pointer to the AppSettings struct.
 */
static void tracker_parse_simple_trackables(Tracker *t, cJSON *category_json, cJSON *lang_json,
                                            TrackableItem ***items_array,
                                            int *count, const char *lang_key_prefix, const AppSettings *settings,
                                            bool parse_linked) {
    (void) settings;
    (void) t;
    if (!category_json) {
        log_message(LOG_INFO, "[TRACKER] tracker_parse_simple_trackables: category_json is nullptr\n");

        return;
    }
    *count = cJSON_GetArraySize(category_json);
    if (*count == 0) {
        log_message(LOG_INFO, "[TRACKER] tracker_parse_simple_trackables: No items found\n");

        return;
    }

    *items_array = (TrackableItem **) calloc(*count, sizeof(TrackableItem *));
    if (!*items_array) return;

    cJSON *item_json = nullptr;
    int i = 0;
    cJSON_ArrayForEach(item_json, category_json) {
        TrackableItem *new_item = (TrackableItem *) calloc(1, sizeof(TrackableItem));
        if (new_item) {
            new_item->alpha = 1.0f;
            new_item->is_visible_on_overlay = true;

            // When hidden is true in template
            new_item->is_hidden = cJSON_IsTrue(cJSON_GetObjectItem(item_json, "hidden"));
            new_item->in_2nd_row = cJSON_IsTrue(cJSON_GetObjectItem(item_json, "in_2nd_row")); // 2nd row of overlay
            new_item->in_3rd_row = cJSON_IsTrue(cJSON_GetObjectItem(item_json, "in_3rd_row")); // 3rd row of overlay

            parse_manual_pos(item_json, "icon_pos", &new_item->icon_pos);
            parse_manual_pos(item_json, "text_pos", &new_item->text_pos);
            parse_manual_pos(item_json, "progress_pos", &new_item->progress_pos); // Only for custom goals

            cJSON *root_name_json = cJSON_GetObjectItem(item_json, "root_name");
            if (cJSON_IsString(root_name_json)) {
                strncpy(new_item->root_name, root_name_json->valuestring, sizeof(new_item->root_name) - 1);
                new_item->root_name[sizeof(new_item->root_name) - 1] = '\0';
            } else {
                // Skip this item if it has no root_name
                free(new_item);
                new_item = nullptr;
                continue;
            }

            // Conthe full language key with the prefix (for "stat." or "unlock.")
            char lang_key[256];
            snprintf(lang_key, sizeof(lang_key), "%s%s", lang_key_prefix, new_item->root_name);

            // Get display name from lang file using the new kay
            cJSON *lang_entry = cJSON_GetObjectItem(lang_json, lang_key);
            if (cJSON_IsString(lang_entry)) {
                // If string copy value as display name otherwise root name
                strncpy(new_item->display_name, lang_entry->valuestring, sizeof(new_item->display_name) - 1);
                new_item->display_name[sizeof(new_item->display_name) - 1] = '\0';
            } else {
                strncpy(new_item->display_name, new_item->root_name, sizeof(new_item->display_name) - 1);
                new_item->display_name[sizeof(new_item->display_name) - 1] = '\0';
            }

            // Get other properties from the template
            cJSON *icon = cJSON_GetObjectItem(item_json, "icon");
            if (cJSON_IsString(icon)) {
                // Append "icon" to "resources/icons/"
                char full_icon_path[sizeof(new_item->icon_path)];
                snprintf(full_icon_path, sizeof(full_icon_path), "%s/icons/%s", get_application_dir(),
                         icon->valuestring);
                strncpy(new_item->icon_path, full_icon_path, sizeof(new_item->icon_path) - 1);
                new_item->icon_path[sizeof(new_item->icon_path) - 1] = '\0';

                if (strstr(full_icon_path, ".gif")) {
                    new_item->anim_texture = get_animated_texture_from_cache(
                        t->renderer, &t->anim_cache, &t->anim_cache_count, &t->anim_cache_capacity, new_item->icon_path,
                        SDL_SCALEMODE_NEAREST);
                } else {
                    new_item->texture = get_texture_from_cache(t->renderer, &t->texture_cache, &t->texture_cache_count,
                                                               &t->texture_cache_capacity, new_item->icon_path,
                                                               SDL_SCALEMODE_NEAREST);
                }
            }

            cJSON *target = cJSON_GetObjectItem(item_json, "target");
            if (cJSON_IsNumber(target)) {
                new_item->goal = target->valueint;
            }

            // Parse linked goals for manual custom goals (goal <= 0)
            if (parse_linked && new_item->goal <= 0) {
                parse_runtime_linked_goals(item_json, &new_item->linked_goal_count,
                                           &new_item->linked_goals, &new_item->linked_goal_mode);
            }

            (*items_array)[i++] = new_item;
        }
    }
}

/**
 * @brief Parses the "multi_stage_goals" from the template JSON file.
 *
 * This function reads the multi_stage_goals array from the template, creating a
 * MultiStageGoal for each entry and parsing its corresponding sequence of sub-goal stages.
 * It supports .gif and .png files.
 *
 *@param t Pointer to the Tracker struct.
 * @param goals_json The cJSON object for the "multi_stage_goals" key from the template file.
 * @param lang_json The cJSON object from the language file (not used here but kept for consistency).
 * @param goals_array A pointer to the array of MultiStageGoal pointers to be populated.
 * @param count A pointer to an integer that will store the number of goals parsed.
 * @param settings Pointer to the AppSettings struct.
 */
static void tracker_parse_multi_stage_goals(Tracker *t, cJSON *goals_json, cJSON *lang_json,
                                            MultiStageGoal ***goals_array,
                                            int *count, const AppSettings *settings) {
    (void) t;
    (void) lang_json;
    (void) settings;
    if (!goals_json) {
        log_message(LOG_INFO, "[TRACKER] tracker_parse_multi_stage_goals: goals_json is nullptr\n");

        *count = 0;
        // goals_array = nullptr;
        return;
    }

    *count = cJSON_GetArraySize(goals_json);
    if (*count == 0) {
        log_message(LOG_INFO, "[TRACKER] tracker_parse_multi_stage_goals: No goals found\n");

        // goals_array = nullptr;
        return;
    }

    *goals_array = (MultiStageGoal **) calloc(*count, sizeof(MultiStageGoal *));
    if (!*goals_array) {
        log_message(LOG_ERROR, "[TRACKER] Failed to allocate memory for MultiStageGoal array.\n");
        *count = 0;
        return;
    }

    cJSON *goal_item_json = nullptr;
    int i = 0;
    cJSON_ArrayForEach(goal_item_json, goals_json) {
        // Iterate through each goal
        MultiStageGoal *new_goal = (MultiStageGoal *) calloc(1, sizeof(MultiStageGoal));
        if (!new_goal) continue;

        // Initialization for animation state
        new_goal->alpha = 1.0f;
        new_goal->is_visible_on_overlay = true;

        // Hide when hidden is true in template
        new_goal->is_hidden = cJSON_IsTrue(cJSON_GetObjectItem(goal_item_json, "hidden"));
        new_goal->in_2nd_row = cJSON_IsTrue(cJSON_GetObjectItem(goal_item_json, "in_2nd_row")); // 2nd row of overlay

        parse_manual_pos(goal_item_json, "icon_pos", &new_goal->icon_pos);
        parse_manual_pos(goal_item_json, "text_pos", &new_goal->text_pos);
        parse_manual_pos(goal_item_json, "progress_pos", &new_goal->progress_pos);

        // Parse root_name and icon
        cJSON *root_name = cJSON_GetObjectItem(goal_item_json, "root_name");
        cJSON *icon = cJSON_GetObjectItem(goal_item_json, "icon");

        if (cJSON_IsString(root_name)) {
            strncpy(new_goal->root_name, root_name->valuestring, sizeof(new_goal->root_name) - 1);
            new_goal->root_name[sizeof(new_goal->root_name) - 1] = '\0';
        }
        if (cJSON_IsString(icon)) {
            char full_icon_path[sizeof(new_goal->icon_path)];
            snprintf(full_icon_path, sizeof(full_icon_path), "%s/icons/%s", get_application_dir(), icon->valuestring);
            strncpy(new_goal->icon_path, full_icon_path, sizeof(new_goal->icon_path) - 1);
            new_goal->icon_path[sizeof(new_goal->icon_path) - 1] = '\0';

            if (strstr(full_icon_path, ".gif")) {
                new_goal->anim_texture = get_animated_texture_from_cache(
                    t->renderer, &t->anim_cache, &t->anim_cache_count, &t->anim_cache_capacity, new_goal->icon_path,
                    SDL_SCALEMODE_NEAREST);
            } else {
                new_goal->texture = get_texture_from_cache(t->renderer, &t->texture_cache, &t->texture_cache_count,
                                                           &t->texture_cache_capacity, new_goal->icon_path,
                                                           SDL_SCALEMODE_NEAREST);
            }
        }

        // Parse use_stage_icons flag
        new_goal->use_stage_icons = cJSON_IsTrue(cJSON_GetObjectItem(goal_item_json, "use_stage_icons"));

        // "multi_stage_goal.<root_name>.display_name"
        // Look up display name from lang file
        char goal_lang_key[256];
        snprintf(goal_lang_key, sizeof(goal_lang_key), "multi_stage_goal.%s.display_name", new_goal->root_name);
        cJSON *goal_lang_entry = cJSON_GetObjectItem(lang_json, goal_lang_key);

        // If the display name is not found in the lang file, use the root name
        if (cJSON_IsString(goal_lang_entry)) {
            strncpy(new_goal->display_name, goal_lang_entry->valuestring, sizeof(new_goal->display_name) - 1);
            new_goal->display_name[sizeof(new_goal->display_name) - 1] = '\0';
        } else {
            strncpy(new_goal->display_name, new_goal->root_name, sizeof(new_goal->display_name) - 1);
            new_goal->display_name[sizeof(new_goal->display_name) - 1] = '\0';
        }

        // Parse stages
        cJSON *stages_json = cJSON_GetObjectItem(goal_item_json, "stages");
        new_goal->stage_count = cJSON_GetArraySize(stages_json);
        if (new_goal->stage_count > 0) {
            // Allocate memory for the stages array
            new_goal->stages = (SubGoal **) calloc(new_goal->stage_count, sizeof(SubGoal *));
            if (!new_goal->stages) {
                free(new_goal);
                new_goal = nullptr;
                continue;
            }

            cJSON *stage_item_json = nullptr;
            int j = 0;
            cJSON_ArrayForEach(stage_item_json, stages_json) {
                SubGoal *new_stage = (SubGoal *) calloc(1, sizeof(SubGoal));
                if (!new_stage) continue;

                // parse stage_id and other properties
                cJSON *text = cJSON_GetObjectItem(stage_item_json, "display_text");
                cJSON *stage_id = cJSON_GetObjectItem(stage_item_json, "stage_id");
                cJSON *type = cJSON_GetObjectItem(stage_item_json, "type");
                cJSON *parent_adv = cJSON_GetObjectItem(stage_item_json, "parent_advancement");
                cJSON *root = cJSON_GetObjectItem(stage_item_json, "root_name");
                cJSON *target_val = cJSON_GetObjectItem(stage_item_json, "target");


                if (cJSON_IsString(text)) {
                    strncpy(new_stage->display_text, text->valuestring, sizeof(new_stage->display_text) - 1);
                    new_stage->display_text[sizeof(new_stage->display_text) - 1] = '\0';
                }
                if (cJSON_IsString(stage_id)) {
                    strncpy(new_stage->stage_id, stage_id->valuestring, sizeof(new_stage->stage_id) - 1);
                    new_stage->stage_id[sizeof(new_stage->stage_id) - 1] = '\0';
                }
                if (cJSON_IsString(parent_adv)) {
                    strncpy(new_stage->parent_advancement, parent_adv->valuestring,
                            sizeof(new_stage->parent_advancement) - 1);
                    new_stage->parent_advancement[sizeof(new_stage->parent_advancement) - 1] = '\0';
                }
                if (cJSON_IsString(root)) {
                    strncpy(new_stage->root_name, root->valuestring, sizeof(new_stage->root_name) - 1);
                    new_stage->root_name[sizeof(new_stage->root_name) - 1] = '\0';
                }
                if (cJSON_IsNumber(target_val)) new_stage->required_progress = target_val->valueint; // This is a number

                // Look up stage display name from lang file
                char stage_lang_key[256];
                snprintf(stage_lang_key, sizeof(stage_lang_key), "multi_stage_goal.%s.stage.%s", new_goal->root_name,
                         new_stage->stage_id);
                cJSON *stage_lang_entry = cJSON_GetObjectItem(lang_json, stage_lang_key);
                // take stage key and search in lang file

                // If the display name is not found in the lang file, use the stage ID
                if (cJSON_IsString(stage_lang_entry)) {
                    strncpy(new_stage->display_text, stage_lang_entry->valuestring,
                            sizeof(new_stage->display_text) - 1);
                    new_stage->display_text[sizeof(new_stage->display_text) - 1] = '\0';
                } else {
                    strncpy(new_stage->display_text, new_stage->stage_id,
                            sizeof(new_stage->display_text) - 1); // Fallback
                    new_stage->display_text[sizeof(new_stage->display_text) - 1] = '\0';
                }

                // Parse type
                if (cJSON_IsString(type)) {
                    if (strcmp(type->valuestring, "stat") == 0) new_stage->type = SUBGOAL_STAT;
                    else if (strcmp(type->valuestring, "advancement") == 0) new_stage->type = SUBGOAL_ADVANCEMENT;
                    else if (strcmp(type->valuestring, "unlock") == 0) new_stage->type = SUBGOAL_UNLOCK;
                    else if (strcmp(type->valuestring, "criterion") == 0) new_stage->type = SUBGOAL_CRITERION;
                    else new_stage->type = SUBGOAL_MANUAL; // "final" maps to manual/unused
                }

                // Parse Per-Stage Icon if enabled
                if (new_goal->use_stage_icons) {
                    cJSON *stage_icon = cJSON_GetObjectItem(stage_item_json, "icon");
                    if (cJSON_IsString(stage_icon)) {
                        char full_icon_path[sizeof(new_stage->icon_path)];
                        snprintf(full_icon_path, sizeof(full_icon_path), "%s/icons/%s", get_application_dir(),
                                 stage_icon->valuestring);
                        strncpy(new_stage->icon_path, full_icon_path, sizeof(new_stage->icon_path) - 1);
                        new_stage->icon_path[sizeof(new_stage->icon_path) - 1] = '\0';

                        if (strstr(full_icon_path, ".gif")) {
                            new_stage->anim_texture = get_animated_texture_from_cache(
                                t->renderer, &t->anim_cache, &t->anim_cache_count, &t->anim_cache_capacity,
                                new_stage->icon_path,
                                SDL_SCALEMODE_NEAREST);
                        } else {
                            new_stage->texture = get_texture_from_cache(t->renderer, &t->texture_cache,
                                                                        &t->texture_cache_count,
                                                                        &t->texture_cache_capacity,
                                                                        new_stage->icon_path,
                                                                        SDL_SCALEMODE_NEAREST);
                        }
                    }
                }
                // Add the stage to the goal
                new_goal->stages[j++] = new_stage;
            }
        }
        // Add the goal to the array
        (*goals_array)[i++] = new_goal;
    }
}


/**
 * @brief Parses counter goals from the template file.
 * Counter goals track how many of a set of linked goals are completed.
 */
static void tracker_parse_counter_goals(Tracker *t, cJSON *counters_json, cJSON *lang_json,
                                        CounterGoal ***goals_array, int *count,
                                        const AppSettings *settings) {
    (void) settings;
    if (!counters_json) {
        *count = 0;
        return;
    }

    *count = cJSON_GetArraySize(counters_json);
    if (*count == 0) return;

    *goals_array = (CounterGoal **) calloc(*count, sizeof(CounterGoal *));
    if (!*goals_array) {
        log_message(LOG_ERROR, "[TRACKER] Failed to allocate memory for CounterGoal array.\n");
        *count = 0;
        return;
    }

    cJSON *goal_item_json = nullptr;
    int i = 0;
    cJSON_ArrayForEach(goal_item_json, counters_json) {
        CounterGoal *new_goal = (CounterGoal *) calloc(1, sizeof(CounterGoal));
        if (!new_goal) continue;

        new_goal->alpha = 1.0f;
        new_goal->is_visible_on_overlay = true;

        new_goal->is_hidden = cJSON_IsTrue(cJSON_GetObjectItem(goal_item_json, "hidden"));
        new_goal->in_2nd_row = cJSON_IsTrue(cJSON_GetObjectItem(goal_item_json, "in_2nd_row"));

        parse_manual_pos(goal_item_json, "icon_pos", &new_goal->icon_pos);
        parse_manual_pos(goal_item_json, "text_pos", &new_goal->text_pos);
        parse_manual_pos(goal_item_json, "progress_pos", &new_goal->progress_pos);

        cJSON *root_name = cJSON_GetObjectItem(goal_item_json, "root_name");
        cJSON *icon = cJSON_GetObjectItem(goal_item_json, "icon");

        if (cJSON_IsString(root_name)) {
            strncpy(new_goal->root_name, root_name->valuestring, sizeof(new_goal->root_name) - 1);
            new_goal->root_name[sizeof(new_goal->root_name) - 1] = '\0';
        }
        if (cJSON_IsString(icon)) {
            char full_icon_path[sizeof(new_goal->icon_path)];
            snprintf(full_icon_path, sizeof(full_icon_path), "%s/icons/%s", get_application_dir(), icon->valuestring);
            strncpy(new_goal->icon_path, full_icon_path, sizeof(new_goal->icon_path) - 1);
            new_goal->icon_path[sizeof(new_goal->icon_path) - 1] = '\0';

            if (strstr(full_icon_path, ".gif")) {
                new_goal->anim_texture = get_animated_texture_from_cache(
                    t->renderer, &t->anim_cache, &t->anim_cache_count, &t->anim_cache_capacity, new_goal->icon_path,
                    SDL_SCALEMODE_NEAREST);
            } else {
                new_goal->texture = get_texture_from_cache(t->renderer, &t->texture_cache, &t->texture_cache_count,
                                                           &t->texture_cache_capacity, new_goal->icon_path,
                                                           SDL_SCALEMODE_NEAREST);
            }
        }

        // Look up display name from lang file: "counter.<root_name>"
        char goal_lang_key[256];
        snprintf(goal_lang_key, sizeof(goal_lang_key), "counter.%s", new_goal->root_name);
        cJSON *goal_lang_entry = cJSON_GetObjectItem(lang_json, goal_lang_key);
        if (cJSON_IsString(goal_lang_entry)) {
            strncpy(new_goal->display_name, goal_lang_entry->valuestring, sizeof(new_goal->display_name) - 1);
            new_goal->display_name[sizeof(new_goal->display_name) - 1] = '\0';
        } else {
            strncpy(new_goal->display_name, new_goal->root_name, sizeof(new_goal->display_name) - 1);
            new_goal->display_name[sizeof(new_goal->display_name) - 1] = '\0';
        }

        // Parse linked goals
        cJSON *linked_json = cJSON_GetObjectItem(goal_item_json, "linked_goals");
        int linked_count = cJSON_GetArraySize(linked_json);
        if (linked_count > 0) {
            new_goal->linked_goals = (CounterLinkedGoal *) calloc(linked_count, sizeof(CounterLinkedGoal));
            if (new_goal->linked_goals) {
                new_goal->linked_goal_count = linked_count;
                cJSON *link_json = nullptr;
                int li = 0;
                cJSON_ArrayForEach(link_json, linked_json) {
                    CounterLinkedGoal *lg = &new_goal->linked_goals[li];
                    cJSON *lr = cJSON_GetObjectItem(link_json, "root_name");
                    cJSON *ls = cJSON_GetObjectItem(link_json, "stage_id");
                    cJSON *lp = cJSON_GetObjectItem(link_json, "parent_root");
                    if (cJSON_IsString(lr)) {
                        strncpy(lg->root_name, lr->valuestring, sizeof(lg->root_name) - 1);
                        lg->root_name[sizeof(lg->root_name) - 1] = '\0';
                    }
                    if (cJSON_IsString(ls)) {
                        strncpy(lg->stage_id, ls->valuestring, sizeof(lg->stage_id) - 1);
                        lg->stage_id[sizeof(lg->stage_id) - 1] = '\0';
                    }
                    if (cJSON_IsString(lp)) {
                        strncpy(lg->parent_root, lp->valuestring, sizeof(lg->parent_root) - 1);
                        lg->parent_root[sizeof(lg->parent_root) - 1] = '\0';
                    }
                    li++;
                }
            }
        }

        (*goals_array)[i++] = new_goal;
    }
    *count = i;
}

/**
 * @brief Parses the "decorations" array from the template JSON file.
 *
 * Decorations are manual layout elements like text headers, lines, and arrows.
 * They are only rendered when manual layout mode is active.
 *
 * @param decorations_json The cJSON array for the "decorations" key from the template file.
 * @param lang_json The cJSON object from the language file.
 * @param decorations_array A pointer to the array of DecorationElement pointers to be populated.
 * @param count A pointer to an integer that will store the number of decorations parsed.
 */
static void tracker_parse_decorations(cJSON *decorations_json, cJSON *lang_json,
                                      DecorationElement ***decorations_array, int *count) {
    if (!decorations_json) {
        *count = 0;
        return;
    }

    *count = cJSON_GetArraySize(decorations_json);
    if (*count <= 0) {
        *count = 0;
        return;
    }

    *decorations_array = (DecorationElement **) calloc(*count, sizeof(DecorationElement *));
    int i = 0;
    cJSON *item_json = nullptr;
    cJSON_ArrayForEach(item_json, decorations_json) {
        auto *elem = (DecorationElement *) calloc(1, sizeof(DecorationElement));

        // Parse ID
        cJSON *id_json = cJSON_GetObjectItem(item_json, "id");
        if (cJSON_IsString(id_json)) {
            strncpy(elem->id, id_json->valuestring, sizeof(elem->id) - 1);
            elem->id[sizeof(elem->id) - 1] = '\0';
        }

        // Parse type
        cJSON *type_json = cJSON_GetObjectItem(item_json, "type");
        if (cJSON_IsString(type_json)) {
            if (strcmp(type_json->valuestring, "text_header") == 0) {
                elem->type = DECORATION_TEXT_HEADER;
            } else if (strcmp(type_json->valuestring, "line") == 0) {
                elem->type = DECORATION_LINE;
            } else if (strcmp(type_json->valuestring, "arrow") == 0) {
                elem->type = DECORATION_ARROW;
            }
        }

        // Parse display text from lang file, falling back to template
        if (elem->type == DECORATION_TEXT_HEADER) {
            // Try lang file first: "decoration.<id>"
            char lang_key[256];
            snprintf(lang_key, sizeof(lang_key), "decoration.%s", elem->id);
            cJSON *lang_val = lang_json ? cJSON_GetObjectItem(lang_json, lang_key) : nullptr;
            if (cJSON_IsString(lang_val)) {
                strncpy(elem->display_text, lang_val->valuestring, sizeof(elem->display_text) - 1);
            } else {
                cJSON *text_json = cJSON_GetObjectItem(item_json, "text");
                if (cJSON_IsString(text_json)) {
                    strncpy(elem->display_text, text_json->valuestring, sizeof(elem->display_text) - 1);
                } else {
                    // Fallback to ID so display text isn't empty
                    strncpy(elem->display_text, elem->id, sizeof(elem->display_text) - 1);
                }
            }
            elem->display_text[sizeof(elem->display_text) - 1] = '\0';

            // Parse linked goals for text headers
            cJSON *linked_json = cJSON_GetObjectItem(item_json, "linked_goals");
            int linked_count = cJSON_GetArraySize(linked_json);
            if (linked_count > 0) {
                elem->linked_goals = (CounterLinkedGoal *) calloc(linked_count, sizeof(CounterLinkedGoal));
                if (elem->linked_goals) {
                    elem->linked_goal_count = linked_count;
                    cJSON *lg_json = nullptr;
                    int li = 0;
                    cJSON_ArrayForEach(lg_json, linked_json) {
                        CounterLinkedGoal *lg = &elem->linked_goals[li];
                        cJSON *lr = cJSON_GetObjectItem(lg_json, "root_name");
                        cJSON *ls = cJSON_GetObjectItem(lg_json, "stage_id");
                        cJSON *lp = cJSON_GetObjectItem(lg_json, "parent_root");
                        if (cJSON_IsString(lr)) {
                            strncpy(lg->root_name, lr->valuestring, sizeof(lg->root_name) - 1);
                            lg->root_name[sizeof(lg->root_name) - 1] = '\0';
                        }
                        if (cJSON_IsString(ls)) {
                            strncpy(lg->stage_id, ls->valuestring, sizeof(lg->stage_id) - 1);
                            lg->stage_id[sizeof(lg->stage_id) - 1] = '\0';
                        }
                        if (cJSON_IsString(lp)) {
                            strncpy(lg->parent_root, lp->valuestring, sizeof(lg->parent_root) - 1);
                            lg->parent_root[sizeof(lg->parent_root) - 1] = '\0';
                        }
                        li++;
                    }
                }
            }
        }

        // Line fields
        if (elem->type == DECORATION_LINE) {
            cJSON *thickness_json = cJSON_GetObjectItem(item_json, "thickness");
            if (cJSON_IsNumber(thickness_json)) elem->thickness = (float) thickness_json->valuedouble;
            else elem->thickness = 2.0f;

            cJSON *opacity_json = cJSON_GetObjectItem(item_json, "opacity");
            if (cJSON_IsNumber(opacity_json)) elem->opacity = (float) opacity_json->valuedouble;
            else elem->opacity = 1.0f;

            parse_manual_pos(item_json, "pos2", &elem->pos2);
        }

        // Arrow fields
        if (elem->type == DECORATION_ARROW) {
            cJSON *thickness_json = cJSON_GetObjectItem(item_json, "thickness");
            if (cJSON_IsNumber(thickness_json)) elem->thickness = (float) thickness_json->valuedouble;
            else elem->thickness = 2.0f;

            cJSON *arrowhead_json = cJSON_GetObjectItem(item_json, "arrowhead_size");
            if (cJSON_IsNumber(arrowhead_json)) elem->arrowhead_size = (float) arrowhead_json->valuedouble;
            else elem->arrowhead_size = 12.0f;

            parse_manual_pos(item_json, "pos2", &elem->pos2);

            // Parse bends
            elem->bend_count = 0;
            cJSON *bends_json = cJSON_GetObjectItem(item_json, "bends");
            if (bends_json && cJSON_IsArray(bends_json)) {
                int bend_idx = 0;
                cJSON *bend_item = nullptr;
                cJSON_ArrayForEach(bend_item, bends_json) {
                    if (bend_idx >= MAX_ARROW_BENDS) break;
                    cJSON *bx = cJSON_GetObjectItem(bend_item, "x");
                    cJSON *by = cJSON_GetObjectItem(bend_item, "y");
                    if (cJSON_IsNumber(bx) && cJSON_IsNumber(by)) {
                        elem->bends[bend_idx].x =
                                fminf(fmaxf((float) bx->valuedouble, -MANUAL_POS_MAX), MANUAL_POS_MAX);
                        elem->bends[bend_idx].y =
                                fminf(fmaxf((float) by->valuedouble, -MANUAL_POS_MAX), MANUAL_POS_MAX);
                        elem->bends[bend_idx].is_set = true;
                        bend_idx++;
                    }
                }
                elem->bend_count = bend_idx;
            }

            // Goal linking
            cJSON *start_root_json = cJSON_GetObjectItem(item_json, "start_goal_root");
            if (cJSON_IsString(start_root_json)) {
                strncpy(elem->start_goal_root, start_root_json->valuestring, sizeof(elem->start_goal_root) - 1);
                elem->start_goal_root[sizeof(elem->start_goal_root) - 1] = '\0';
            }
            cJSON *start_stage_json = cJSON_GetObjectItem(item_json, "start_goal_stage");
            if (cJSON_IsString(start_stage_json)) {
                strncpy(elem->start_goal_stage, start_stage_json->valuestring, sizeof(elem->start_goal_stage) - 1);
                elem->start_goal_stage[sizeof(elem->start_goal_stage) - 1] = '\0';
            }
            cJSON *end_root_json = cJSON_GetObjectItem(item_json, "end_goal_root");
            if (cJSON_IsString(end_root_json)) {
                strncpy(elem->end_goal_root, end_root_json->valuestring, sizeof(elem->end_goal_root) - 1);
                elem->end_goal_root[sizeof(elem->end_goal_root) - 1] = '\0';
            }
            cJSON *end_stage_json = cJSON_GetObjectItem(item_json, "end_goal_stage");
            if (cJSON_IsString(end_stage_json)) {
                strncpy(elem->end_goal_stage, end_stage_json->valuestring, sizeof(elem->end_goal_stage) - 1);
                elem->end_goal_stage[sizeof(elem->end_goal_stage) - 1] = '\0';
            }

            cJSON *opacity_before_json = cJSON_GetObjectItem(item_json, "opacity_before");
            if (cJSON_IsNumber(opacity_before_json)) elem->opacity_before = (float) opacity_before_json->valuedouble;
            else elem->opacity_before = (float) ADVANCELY_FADED_ALPHA / 255.0f;

            cJSON *opacity_after_json = cJSON_GetObjectItem(item_json, "opacity_after");
            if (cJSON_IsNumber(opacity_after_json)) elem->opacity_after = (float) opacity_after_json->valuedouble;
            else elem->opacity_after = 1.0f;
        }

        // Parse position
        parse_manual_pos(item_json, "pos", &elem->pos);

        (*decorations_array)[i++] = elem;
    }
}

/**
 * @brief Updates unlock progress from a pre-loaded cJSON object and counts completed unlocks.
 * @param t A pointer to the Tracker struct.
 * @param player_unlocks_json The parsed player unlocks JSON file.
 */
static void tracker_update_unlock_progress(Tracker *t, const cJSON *player_unlocks_json) {
    if (!player_unlocks_json) return;

    // printf("[TRACKER] Reading player unlocks from: %s\n", t->unlocks_path);

    cJSON *obtained_obj = cJSON_GetObjectItem(player_unlocks_json, "obtained"); // Top level object in unlocks file
    if (!obtained_obj) {
        log_message(LOG_ERROR, "[TRACKER] Failed to find 'obtained' object in player unlocks file.\n");
        return;
    }

    t->template_data->unlocks_completed_count = 0;

    for (int i = 0; i < t->template_data->unlock_count; i++) {
        TrackableItem *unlock = t->template_data->unlocks[i];
        cJSON *unlock_status = cJSON_GetObjectItem(obtained_obj, unlock->root_name);

        // Check if the unlock is obtained
        if (cJSON_IsTrue(unlock_status)) {
            unlock->done = true;
            t->template_data->unlocks_completed_count++;
        } else {
            unlock->done = false;
        }
    }
}


/**
 * @brief Updates the 'done' status of custom goals by reading from the settings file.
 * @param t A pointer to the Tracker struct.
 * @param settings_json A pointer to the parsed settings.json cJSON object.
 */
static void tracker_update_custom_progress(Tracker *t, cJSON *settings_json, const AppSettings *settings) {
    (void) settings;
    if (!settings_json) {
        log_message(LOG_INFO, "[TRACKER] Failed to load or parse settings file.\n");

        return;
    }

    cJSON *progress_obj = cJSON_GetObjectItem(settings_json, "custom_progress");
    if (!cJSON_IsObject(progress_obj)) {
        return; // NO custom progress saved yet, which is fine
    }

    // Iterate through the custom goals
    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        TrackableItem *item = t->template_data->custom_goals[i];
        cJSON *item_progress_json = cJSON_GetObjectItem(progress_obj, item->root_name);

        // Reset state before each update
        item->done = false;
        item->is_manually_completed = false;
        item->progress = 0;

        if (item->goal == -1) {
            // INFINITE COUNTER WITH TARGET -1 and MANUAL OVERRIDE
            if (cJSON_IsTrue(item_progress_json)) {
                // Manually overridden to be complete
                item->done = true;
                item->is_manually_completed = true;
                item->progress = 1; // Set progress to 1 for consistency
            } else if (cJSON_IsNumber(item_progress_json)) {
                // It's a counter, but can't be completed manually
                item->progress = item_progress_json->valueint;
                item->done = false;
            }
        } else if (item->goal > 0) {
            // Normal counter
            if (cJSON_IsNumber(item_progress_json)) {
                item->progress = item_progress_json->valueint;
            }
            item->done = (item->progress >= item->goal);
        } else {
            // Simple toggle (target 0 or not set)
            if (cJSON_IsTrue(item_progress_json)) {
                item->done = true;
                item->is_manually_completed = true;
                item->progress = 1;
            }
        }
    }
}

/**
 * @brief Updates multi-stage goal progress from preloaded cJSON objects.
 * @param t A pointer to the Tracker struct.
 * @param player_adv_json The parsed player advancements JSON file.
 * @param player_stats_json The parsed player stats JSON file.
 * @param player_unlocks_json The parsed player unlocks JSON file.
 * @param version The game version from the MC_Version enum.
 */
static void tracker_update_multi_stage_progress(Tracker *t, const cJSON *player_adv_json,
                                                const cJSON *player_stats_json, const cJSON *player_unlocks_json,
                                                MC_Version version, const AppSettings *settings) {
    (void) settings;
    if (t->template_data->multi_stage_goal_count == 0) return;

    if (!player_adv_json && !player_stats_json) {
        log_message(LOG_INFO,
                    "[TRACKER] Failed to load or parse player advancements or player stats file to update multi-stage goal progress.\n");

        return;
    }

    // Iterate through the multi-stage goals
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];

        // Reset and re-evaluate progress from the start (important for world-changes when tracker is running)
        goal->current_stage = 0;

        // Loop through each stage sequentially to determine the current progress
        for (int j = 0; j < goal->stage_count; j++) {
            SubGoal *stage_to_check = goal->stages[j];
            bool stage_completed = false; // Default to false
            int current_progress = 0;
            bool stat_found = false;

            stage_to_check->current_stat_progress = 0;

            // Stop checking if we are on the final stage (SUBGOAL_MANUAL), anything that isn't "stat" or "advancement"
            if (stage_to_check->type == SUBGOAL_MANUAL) break;

            switch (stage_to_check->type) {
                case SUBGOAL_ADVANCEMENT:
                    // We take the item with the root name then check if it is done then call this stage completed
                    if (player_adv_json) {
                        cJSON *adv_entry = cJSON_GetObjectItem(player_adv_json, stage_to_check->root_name);
                        // Get the entry for this advancement
                        if (adv_entry && cJSON_IsTrue(cJSON_GetObjectItem(adv_entry, "done"))) {
                            stage_completed = true;
                        }
                    }
                    break;

                case SUBGOAL_STAT: {
                    // Braces to create a new scope
                    if (version <= MC_VERSION_1_6_4) {
                        // For Legacy, we MUST look up the pre-calculated, snapshot-aware progress
                        // First check stats list
                        for (int c_idx = 0; c_idx < t->template_data->stat_count && !stat_found; c_idx++) {
                            for (int s_idx = 0; s_idx < t->template_data->stats[c_idx]->criteria_count; s_idx++) {
                                TrackableItem *sub = t->template_data->stats[c_idx]->criteria[s_idx];
                                if (strcmp(sub->root_name, stage_to_check->root_name) == 0) {
                                    current_progress = sub->progress;
                                    stat_found = true;
                                    break;
                                }
                            }
                        }
                        // If not found, check achievements list
                        if (!stat_found) {
                            for (int a_idx = 0; a_idx < t->template_data->advancement_count; a_idx++) {
                                TrackableCategory *ach = t->template_data->advancements[a_idx];
                                if (strcmp(ach->root_name, stage_to_check->root_name) == 0) {
                                    // For achievements, progress is based on the raw value from the file
                                    cJSON *stats_change = cJSON_GetObjectItem(player_stats_json, "stats-change");
                                    cJSON *stat_entry;
                                    cJSON_ArrayForEach(stat_entry, stats_change) {
                                        cJSON *item = stat_entry->child;
                                        if (item && strcmp(item->string, ach->root_name) == 0) {
                                            current_progress = item->valueint;
                                            stat_found = true;
                                            break;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    } else if (version <= MC_VERSION_1_12_2) {
                        // MID ERA (and 1.12.x): Parse directly from the flat JSON structure
                        cJSON *stat_entry = cJSON_GetObjectItem(player_stats_json, stage_to_check->root_name);
                        if (cJSON_IsNumber(stat_entry)) {
                            current_progress = stat_entry->valueint;
                            stat_found = true;
                        }
                    } else {
                        // MODERN ERA: Parse directly from the nested "stats" object
                        cJSON *stats_obj = cJSON_GetObjectItem(player_stats_json, "stats");
                        if (stats_obj) {
                            char root_name_copy[192];
                            strncpy(root_name_copy, stage_to_check->root_name, sizeof(root_name_copy) - 1);
                            root_name_copy[sizeof(root_name_copy) - 1] = '\0';
                            char *item_key = strchr(root_name_copy, '/');
                            if (item_key) {
                                *item_key = '\0';
                                item_key++;
                                cJSON *category_obj = cJSON_GetObjectItem(stats_obj, root_name_copy);
                                if (category_obj) {
                                    cJSON *stat_value = cJSON_GetObjectItem(category_obj, item_key);
                                    if (cJSON_IsNumber(stat_value)) {
                                        current_progress = stat_value->valueint;
                                        stat_found = true;
                                    }
                                }
                            }
                        }
                    }

                    // Check for completion
                    if (stat_found && current_progress >= stage_to_check->required_progress) {
                        stage_completed = true;
                    }
                    stage_to_check->current_stat_progress = current_progress;
                    break;
                }

                // Can use advancement criteria for a stage
                case SUBGOAL_CRITERION: {
                    // Advancement Criteria are not possible for Legacy (<= 1.6.4)
                    if (version >= MC_VERSION_1_12) {
                        // Modern Era: Check for the criterion key in player_adv_json
                        if (player_adv_json) {
                            cJSON *adv_entry = cJSON_GetObjectItem(player_adv_json, stage_to_check->parent_advancement);
                            if (adv_entry) {
                                cJSON *criteria_obj = cJSON_GetObjectItem(adv_entry, "criteria");
                                if (criteria_obj && cJSON_HasObjectItem(criteria_obj, stage_to_check->root_name)) {
                                    stage_completed = true;
                                }
                            }
                        }
                    } else if (version >= MC_VERSION_1_7_2) {
                        // Mid Era: Check for the biome name in the 'progress' array of player_stats_json (player stats file)
                        if (player_stats_json) {
                            cJSON *ach_entry = cJSON_GetObjectItem(player_stats_json,
                                                                   stage_to_check->parent_advancement);
                            if (ach_entry) {
                                cJSON *progress_array = cJSON_GetObjectItem(ach_entry, "progress");
                                if (cJSON_IsArray(progress_array)) {
                                    cJSON *progress_item;
                                    cJSON_ArrayForEach(progress_item, progress_array) {
                                        if (cJSON_IsString(progress_item) && strcmp(
                                                progress_item->valuestring, stage_to_check->root_name) == 0) {
                                            stage_completed = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    break;
                }

                // Check if certain unlock is done within a stage (25w14craftmine)
                case SUBGOAL_UNLOCK:
                    if (player_unlocks_json) {
                        cJSON *obtained_obj = cJSON_GetObjectItem(player_unlocks_json, "obtained");
                        if (obtained_obj) {
                            cJSON *unlock_status = cJSON_GetObjectItem(obtained_obj, stage_to_check->root_name);
                            if (cJSON_IsTrue(unlock_status)) {
                                stage_completed = true;
                            }
                        }
                    }
                    break;
                case SUBGOAL_MANUAL: // Already handled above, when string in subgoal ISN'T "stat" or "advancement"
                default:
                    break; // Manual stages are not updated here
            }

            if (stage_completed) {
                goal->current_stage = j + 1; // Move to the next stage
            } else {
                break;
            }
        }
    }
}

/**
 * @brief Checks if a goal is completed by root_name, optional stage_id, and optional parent_root.
 * Used by arrow goal linking, counter goals and automatic stat completion (soon).
 */
static bool is_goal_completed_by_root(const TemplateData *td, const char *root_name,
                                      const char *stage_id, const char *parent_root) {
    if (!td || !root_name || root_name[0] == '\0') return false;

    // Check advancements
    for (int j = 0; j < td->advancement_count; j++) {
        if (!td->advancements[j]) continue;
        if (strcmp(td->advancements[j]->root_name, root_name) == 0 &&
            (!parent_root || parent_root[0] == '\0'))
            return td->advancements[j]->done;
        // Check criteria
        for (int k = 0; k < td->advancements[j]->criteria_count; k++) {
            if (td->advancements[j]->criteria[k] &&
                strcmp(td->advancements[j]->criteria[k]->root_name, root_name) == 0) {
                // If parent_root is specified, match the parent advancement
                if (parent_root && parent_root[0] != '\0') {
                    if (strcmp(td->advancements[j]->root_name, parent_root) != 0) continue;
                }
                return td->advancements[j]->criteria[k]->done;
            }
        }
    }
    // Check stats
    for (int j = 0; j < td->stat_count; j++) {
        if (!td->stats[j]) continue;
        if (strcmp(td->stats[j]->root_name, root_name) == 0 &&
            (!parent_root || parent_root[0] == '\0'))
            return td->stats[j]->done;
        for (int k = 0; k < td->stats[j]->criteria_count; k++) {
            if (td->stats[j]->criteria[k] &&
                strcmp(td->stats[j]->criteria[k]->root_name, root_name) == 0) {
                if (parent_root && parent_root[0] != '\0') {
                    if (strcmp(td->stats[j]->root_name, parent_root) != 0) continue;
                }
                return td->stats[j]->criteria[k]->done;
            }
        }
    }
    // Check unlocks
    for (int j = 0; j < td->unlock_count; j++) {
        if (td->unlocks[j] && strcmp(td->unlocks[j]->root_name, root_name) == 0)
            return td->unlocks[j]->done;
    }
    // Check custom goals
    for (int j = 0; j < td->custom_goal_count; j++) {
        if (td->custom_goals[j] && strcmp(td->custom_goals[j]->root_name, root_name) == 0)
            return td->custom_goals[j]->done;
    }
    // Check multi-stage goals
    for (int j = 0; j < td->multi_stage_goal_count; j++) {
        MultiStageGoal *msg = td->multi_stage_goals[j];
        if (!msg || strcmp(msg->root_name, root_name) != 0) continue;
        if (!stage_id || stage_id[0] == '\0') {
            return msg->current_stage >= msg->stage_count - 1;
        }
        for (int k = 0; k < msg->stage_count; k++) {
            if (msg->stages[k] && strcmp(msg->stages[k]->stage_id, stage_id) == 0) {
                return msg->current_stage > k;
            }
        }
    }
    // Check counter goals
    for (int j = 0; j < td->counter_goal_count; j++) {
        if (td->counter_goals[j] && strcmp(td->counter_goals[j]->root_name, root_name) == 0)
            return td->counter_goals[j]->done;
    }
    return false;
}

/**
 * @brief Checks if linked goals are satisfied based on mode (AND/OR).
 * Returns true if auto-completion should trigger.
 */
static bool check_linked_goals_satisfied(const TemplateData *td, const CounterLinkedGoal *linked_goals,
                                         int linked_goal_count, LinkedGoalMode mode) {
    if (linked_goal_count <= 0 || !linked_goals) return false;

    if (mode == LINKED_GOAL_AND) {
        // All linked goals must be completed
        for (int j = 0; j < linked_goal_count; j++) {
            if (!is_goal_completed_by_root(td, linked_goals[j].root_name,
                                           linked_goals[j].stage_id, linked_goals[j].parent_root)) {
                return false; // any goal not completed
            }
        }
        return true; // all completed
    } else {
        // At least one linked goal must be completed (OR mode)
        for (int j = 0; j < linked_goal_count; j++) {
            if (is_goal_completed_by_root(td, linked_goals[j].root_name,
                                          linked_goals[j].stage_id, linked_goals[j].parent_root)) {
                return true; // any goal completed
            }
        }
        return false; // none completed
    }
}

/**
 * @brief Updates completion of manual custom goals (goal <= 0) via linked goals.
 * Returns true if any goal was newly completed this call (used for fixed-point iteration).
 */
static bool tracker_update_custom_goal_linked_goals(Tracker *t) {
    if (!t || !t->template_data) return false;
    TemplateData *td = t->template_data;
    bool any_changed = false;

    for (int i = 0; i < td->custom_goal_count; i++) {
        TrackableItem *cg = td->custom_goals[i];
        if (!cg || cg->goal > 0) continue; // Only manual goals (toggle / infinite counter)
        if (cg->linked_goal_count > 0 && !cg->done) {
            if (check_linked_goals_satisfied(td, cg->linked_goals,
                                             cg->linked_goal_count, cg->linked_goal_mode)) {
                cg->done = true;
                any_changed = true;
            }
        }
    }
    return any_changed;
}

/**
 * @brief Updates completion state of all counter goals by checking their linked goals.
 * Returns true if any counter changed state (used for fixed-point iteration).
 * Must be called before tracker_calculate_overall_progress.
 */
static bool tracker_update_counter_goals(Tracker *t) {
    if (!t || !t->template_data) return false;
    TemplateData *td = t->template_data;
    bool any_changed = false;
    for (int i = 0; i < td->counter_goal_count; i++) {
        CounterGoal *counter = td->counter_goals[i];
        if (!counter) continue;
        int completed = 0;
        for (int j = 0; j < counter->linked_goal_count; j++) {
            CounterLinkedGoal *lg = &counter->linked_goals[j];
            if (is_goal_completed_by_root(td, lg->root_name, lg->stage_id, lg->parent_root)) {
                completed++;
            }
        }
        bool new_done = (counter->linked_goal_count > 0 && completed >= counter->linked_goal_count);
        if (counter->completed_count != completed || counter->done != new_done) any_changed = true;
        counter->completed_count = completed;
        counter->done = new_done;
    }
    return any_changed;
}

/**
 * @brief Updates stat completion based on linked goals (auto-completion).
 * Returns true if any stat or sub-stat was newly completed this call (used for fixed-point iteration).
 */
static bool tracker_update_stat_linked_goals(Tracker *t) {
    if (!t || !t->template_data) return false;
    TemplateData *td = t->template_data;
    bool any_changed = false;

    // Reset stat completion counts to recalculate with linked goals
    td->stats_completed_count = 0;
    td->stats_completed_criteria_count = 0;

    for (int i = 0; i < td->stat_count; i++) {
        TrackableCategory *stat_cat = td->stats[i];
        if (!stat_cat) continue;

        // Check sub-stat linked goals
        stat_cat->completed_criteria_count = 0;
        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];
            if (!sub_stat) continue;

            // If sub-stat has linked goals and they are satisfied, auto-complete it
            if (sub_stat->linked_goal_count > 0 && !sub_stat->done) {
                if (check_linked_goals_satisfied(td, sub_stat->linked_goals,
                                                 sub_stat->linked_goal_count, sub_stat->linked_goal_mode)) {
                    sub_stat->done = true;
                    any_changed = true;
                }
            }
            if (sub_stat->done) stat_cat->completed_criteria_count++;
        }

        // Check stat category linked goals
        if (stat_cat->linked_goal_count > 0 && !stat_cat->done) {
            if (check_linked_goals_satisfied(td, stat_cat->linked_goals,
                                             stat_cat->linked_goal_count, stat_cat->linked_goal_mode)) {
                stat_cat->done = true;
                any_changed = true;
                // When the category is auto-completed, mark all children done too
                for (int j = 0; j < stat_cat->criteria_count; j++) {
                    if (stat_cat->criteria[j] && !stat_cat->criteria[j]->done) {
                        stat_cat->criteria[j]->done = true;
                    }
                }
                stat_cat->completed_criteria_count = stat_cat->criteria_count;
            }
        }

        // Recalculate parent done if all children are now done (might have changed from sub-stat linked goals)
        bool all_children_done = (stat_cat->criteria_count > 0 &&
                                  stat_cat->completed_criteria_count >= stat_cat->criteria_count);
        if (all_children_done && !stat_cat->done) {
            stat_cat->done = true;
            any_changed = true;
        }

        if (stat_cat->done) td->stats_completed_count++;
        td->stats_completed_criteria_count += stat_cat->completed_criteria_count;
    }
    return any_changed;
}

void tracker_calculate_overall_progress(Tracker *t, MC_Version version, const AppSettings *settings) {
    (void) version;
    (void) settings;
    if (!t || !t->template_data) return; // || because we can't be sure if the template_data is initialized

    // calculate the total number of "steps"
    int total_steps = 0;
    int completed_steps = 0;

    // Advancements & Recipes
    // Every recipe is one step
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *adv = t->template_data->advancements[i];
        if (adv->is_recipe) {
            total_steps++; // A recipe is one step.
            if (adv->done) {
                completed_steps++;
            }
        } else {
            // A normal advancement's progress is based on its criteria.
            total_steps += adv->criteria_count;
            completed_steps += adv->completed_criteria_count;
        }
    }

    // Stats:
    // - For multi-criteria stats, each criterion is a step.
    // - For single-criterion stats, the parent category itself is one step.
    // - For hidden helper stats for legacy versions ms-goals, they DON'T count towards the progress at all
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        if (version <= MC_VERSION_1_6_4 && stat_cat->criteria_count == 1 && stat_cat->criteria[0]->goal == 0) {
            continue; // Skip hidden legacy stats
        }
        total_steps += stat_cat->criteria_count;
        completed_steps += stat_cat->completed_criteria_count;
    }

    // Unlocks
    total_steps += t->template_data->unlock_count;
    completed_steps += t->template_data->unlocks_completed_count;

    // Custom goals
    total_steps += t->template_data->custom_goal_count;
    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        if (t->template_data->custom_goals[i]->done) completed_steps++;
    }

    // Multi-Stage Goals
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        total_steps += (t->template_data->multi_stage_goals[i]->stage_count - 1); // Final stage is not counted
        completed_steps += t->template_data->multi_stage_goals[i]->current_stage;
    }

    // Counter Goals: only full completion (all linked goals done) counts as one step
    total_steps += t->template_data->counter_goal_count;
    for (int i = 0; i < t->template_data->counter_goal_count; i++) {
        if (t->template_data->counter_goals[i]->done) completed_steps++;
    }

    log_message(LOG_INFO, "Total steps: %d, completed steps: %d\n", total_steps, completed_steps);

    // Store total steps
    t->template_data->total_progress_steps = total_steps;


    // Set 100% if no steps are found
    if (total_steps > 0) {
        t->template_data->overall_progress_percentage = ((float) completed_steps / (float) total_steps) * 100.0f;
    } else {
        // Default to 100% if no criteria, stats, unlocks, custom goals or stages
        t->template_data->overall_progress_percentage = 100.0f;
    }

    // Freeze the IGT the first time the run reaches 100% completion
    bool is_complete = t->template_data->advancements_completed_count >= t->template_data->advancement_count &&
                       t->template_data->overall_progress_percentage >= 100.0f;
    if (is_complete && !t->template_data->run_completed) {
        t->template_data->run_completed = true;
        t->template_data->frozen_play_time_ticks = t->template_data->play_time_ticks;
    }
}

/**
 * @brief Frees an array of TrackableItem pointers.
 *
 * This is used in tracker_free_template_data(). Used for unlocks and custom goals.
 *
 * @param items The array of TrackableItem pointers to be freed.
 * @param count The number of elements in the array.
 */
static void free_trackable_items(TrackableItem **items, int count) {
    if (!items) return;
    for (int i = 0; i < count; i++) {
        if (items[i]) {
            // Free linked goals for stat auto-completion
            if (items[i]->linked_goals) {
                free(items[i]->linked_goals);
                items[i]->linked_goals = nullptr;
            }
            free(items[i]);
            items[i] = nullptr;
        }
    }
    free(items);
    items = nullptr;
}

/**
 * @brief Frees an array of TrackableCategory pointers. Frees advancements and stats.
 *
 * Used within tracker_free_template_data().
 *
 * @param categories TrackableCategory array
 * @param count Number of categories
 */
static void free_trackable_categories(TrackableCategory **categories, int count) {
    if (!categories) return;
    for (int i = 0; i < count; i++) {
        if (categories[i]) {
            // First, free the inner criteria array using the other helper
            free_trackable_items(categories[i]->criteria, categories[i]->criteria_count);

            // Free linked goals for stat auto-completion
            if (categories[i]->linked_goals) {
                free(categories[i]->linked_goals);
                categories[i]->linked_goals = nullptr;
            }

            // Then free the category itself
            free(categories[i]);
            categories[i] = nullptr;
        }
    }
    free(categories);
    categories = nullptr;
}

/**
 * @brief Frees an array of MultiStageGoal pointers, including their nested stages.
 * @param goals The array of MultiStageGoal pointers to be freed.
 * @param count The number of elements in the array.
 */
static void free_multi_stage_goals(MultiStageGoal **goals, int count) {
    if (!goals) return;
    for (int i = 0; i < count; i++) {
        MultiStageGoal *goal = goals[i];
        if (goal) {
            // Free the sub-goals first
            if (goal->stages) {
                for (int j = 0; j < goal->stage_count; j++) {
                    // Check if the pointer is valid before freeing
                    if (goal->stages[j]) {
                        free(goal->stages[j]);
                        goal->stages[j] = nullptr;
                    }
                }
                free(goal->stages); // Then free the array of pointers
                goal->stages = nullptr;
            }
            free(goal); // Finally, free the parent goal struct
            goals[i] = nullptr; // Nullify the pointer IN THE ARRAY
        }
    }
    free(goals); // Free the top-level array of goal pointers
    goals = nullptr;
}


/**
 * @brief Frees all dynamically allocated memory within a TemplateData struct. Multi-stage goals are freed here.
 *
 * To avoid memory leaks when switching templates during runtime.
 * It only frees the CONTENT of the TemplateData NOT the TemplateData itself.
 *
 * @param td A pointer to the TemplateData to be freed.
 */
static void tracker_free_template_data(TemplateData *td) {
    if (!td) return;


    // Free categories
    if (td->advancements) {
        free_trackable_categories(td->advancements, td->advancement_count);
    }

    if (td->stats) {
        free_trackable_categories(td->stats, td->stat_count);
    }

    if (td->unlocks) {
        free_trackable_items(td->unlocks, td->unlock_count);
    }

    // Free custom goal data
    if (td->custom_goals) {
        free_trackable_items(td->custom_goals, td->custom_goal_count);
    }

    // Free multi-stage goals data
    if (td->multi_stage_goals) {
        free_multi_stage_goals(td->multi_stage_goals, td->multi_stage_goal_count);
    }

    // Free counter goals data
    if (td->counter_goals) {
        for (int i = 0; i < td->counter_goal_count; i++) {
            if (td->counter_goals[i]) {
                free(td->counter_goals[i]->linked_goals);
                free(td->counter_goals[i]);
            }
        }
        free(td->counter_goals);
    }

    // Free decoration elements
    if (td->decorations) {
        for (int i = 0; i < td->decoration_count; i++) {
            if (td->decorations[i]) {
                free(td->decorations[i]->linked_goals);
            }
            free(td->decorations[i]);
        }
        free(td->decorations);
    }

    td->advancements = nullptr;
    td->stats = nullptr;
    td->unlocks = nullptr;
    td->custom_goals = nullptr;
    td->multi_stage_goals = nullptr;
    td->counter_goals = nullptr;
    td->decorations = nullptr;

    // Zero out the entire struct to reset all pointers and counts safely.
    memset(td, 0, sizeof(TemplateData));
}


// ----------------------------------------- END OF STATIC FUNCTIONS -----------------------------------------

// START OF NON-STATIC FUNCTIONS ------------------------------------

SDL_Texture *load_texture_with_scale_mode(SDL_Renderer *renderer, const char *path, SDL_ScaleMode scale_mode) {
    if (path == nullptr || path[0] == '\0') {
        log_message(LOG_ERROR, "[TRACKER - TEXTURE LOAD] Invalid path for texture: %s\n", path);
        return nullptr;
    }

    // Load original surfaces
    SDL_Surface *loaded_surface = IMG_Load(path);
    if (!loaded_surface) {
        log_message(LOG_ERROR, "[TRACKER - TEXTURE LOAD] Failed to load image %s: %s\n", path, SDL_GetError());
        return nullptr;
    }

    // Convert the surface to a consistent format that supports alpha blending
    // Create a new surface with a standard 32-bit RGBA pixel format
    SDL_Surface *formatted_surface = SDL_CreateSurface(loaded_surface->w, loaded_surface->h, SDL_PIXELFORMAT_RGBA32);
    if (formatted_surface) {
        // Copy the pixels from the loaded surface to the new, correctly formatted surface
        SDL_BlitSurface(loaded_surface, nullptr, formatted_surface, nullptr);
    }

    // We are done with the original surface, free it
    SDL_DestroySurface(loaded_surface);

    if (!formatted_surface) {
        log_message(LOG_ERROR, "[TRACKER - TEXTURE LOAD] Failed to create formatted surface for image %s: %s\n", path,
                    SDL_GetError());
        return nullptr;
    }

    SDL_Texture *new_texture = SDL_CreateTextureFromSurface(renderer, formatted_surface);
    SDL_DestroySurface(formatted_surface); // Clean up the surface after creating the texture
    if (!new_texture) {
        log_message(LOG_ERROR, "[TRACKER - TEXTURE LOAD] Failed to create texture from surface %s: %s\n", path,
                    SDL_GetError());
        return nullptr;
    }

    // Explicitly enable blending for the texture
    SDL_SetTextureBlendMode(new_texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(new_texture, scale_mode);
    return new_texture;
}

SDL_Texture *get_texture_from_cache(SDL_Renderer *renderer, TextureCacheEntry **cache, int *cache_count,
                                    int *cache_capacity, const char *path, SDL_ScaleMode scale_mode) {
    if (path == nullptr || path[0] == '\0') return nullptr;

    // Check if the texture is already in the cache
    for (int i = 0; i < *cache_count; i++) {
        if (strcmp((*cache)[i].path, path) == 0) {
            return (*cache)[i].texture;
        }
    }

    // If not in cache, load it
    SDL_Texture *new_texture = load_texture_with_scale_mode(renderer, path, scale_mode);
    if (!new_texture) {
        return nullptr; // Loading failed.
    }

    // Add the new texture to the cache
    if (*cache_count >= *cache_capacity) {
        int new_capacity = *cache_capacity == 0 ? 16 : *cache_capacity * 2;
        TextureCacheEntry *new_cache_ptr = (TextureCacheEntry *) realloc(
            *cache, new_capacity * sizeof(TextureCacheEntry));
        if (!new_cache_ptr) {
            log_message(LOG_ERROR, "[CACHE] Failed to reallocate texture cache!\n");
            SDL_DestroyTexture(new_texture);
            return nullptr;
        }
        *cache = new_cache_ptr;
        *cache_capacity = new_capacity;
    }

    // Add to cache
    TextureCacheEntry *entry = &(*cache)[*cache_count];
    strncpy(entry->path, path, MAX_PATH_LENGTH - 1);
    entry->path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null-termination
    entry->texture = new_texture;

    // OPTIMIZATION: Compute the hash now and store it, so we don't have to read the disk later
    entry->file_hash = compute_file_hash(path);

    (*cache_count)++;

    return new_texture;
}


void free_animated_texture(AnimatedTexture *anim) {
    if (!anim) return;
    for (int i = 0; i < anim->frame_count; i++) {
        if (anim->frames[i]) {
            SDL_DestroyTexture(anim->frames[i]);
        }
    }
    free(anim->frames);
    anim->frames = nullptr;
    free(anim->delays);
    anim->delays = nullptr;
    free(anim);
    anim = nullptr;
}

AnimatedTexture *get_animated_texture_from_cache(SDL_Renderer *renderer, AnimatedTextureCacheEntry **cache,
                                                 int *cache_count, int *cache_capacity, const char *path,
                                                 SDL_ScaleMode scale_mode) {
    if (path == nullptr || path[0] == '\0') return nullptr;

    // 1. Check if the animation is already in the cache.
    for (int i = 0; i < *cache_count; i++) {
        if (strcmp((*cache)[i].path, path) == 0) {
            return (*cache)[i].anim;
        }
    }

    // 2. If not in cache, load it.
    AnimatedTexture *new_anim = load_animated_gif(renderer, path, scale_mode);
    if (!new_anim) return nullptr;

    // 3. Add the new animation to the cache.
    if (*cache_count >= *cache_capacity) {
        int new_capacity = *cache_capacity == 0 ? 8 : *cache_capacity * 2;
        AnimatedTextureCacheEntry *new_cache_ptr = (AnimatedTextureCacheEntry *) realloc(
            *cache, new_capacity * sizeof(AnimatedTextureCacheEntry));
        if (!new_cache_ptr) {
            log_message(LOG_ERROR, "[CACHE] Failed to reallocate animation cache!\n");
            free_animated_texture(new_anim);
            return nullptr;
        }
        *cache = new_cache_ptr;
        *cache_capacity = new_capacity;
    }

    // Add to cache
    AnimatedTextureCacheEntry *entry = &(*cache)[*cache_count];
    strncpy(entry->path, path, MAX_PATH_LENGTH - 1);
    entry->path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null-termination
    entry->anim = new_anim;

    // OPTIMIZATION: Compute hash on load
    entry->file_hash = compute_file_hash(path);

    (*cache_count)++;

    return new_anim;
}

bool tracker_new(Tracker **tracker, AppSettings *settings) {
    // Allocate memory for the tracker itself
    // Calloc assures null initialization
    *tracker = (Tracker *) calloc(1, sizeof(Tracker));

    if (*tracker == nullptr) {
        log_message(LOG_ERROR, "[TRACKER] Failed to allocate memory for tracker.\n");
        return false;
    }

    Tracker *t = *tracker;

    // Explicitly construct HermesRotator (calloc doesn't call its constructor)
    new(&t->hermes_rotator) HermesRotator();
    t->hermes_play_log = nullptr;
    t->hermes_file_offset = 0;
    t->hermes_active = false;
    t->hermes_wants_ipc_flush = false;
    t->hermes_coop_stat_cache = new std::unordered_map<std::string, int>();

    // Initialize notes state
    t->notes_window_open = false;
    t->notes_buffer[0] = '\0';
    t->notes_path[0] = '\0'; // Initialize the new notes path
    t->search_buffer[0] = '\0';
    t->focus_search_box_requested = false;
    t->focus_tc_search_box = false; // CURRENTLY UNUSED
    t->selected_coop_player_idx = -1; // Default: "All Players" (merged view)
    t->is_temp_creator_focused = false;
    t->notes_widget_id_counter = 0;

    for (int i = 0; i < MAX_COOP_PLAYERS; i++) {
        t->coop_player_snapshots[i] = nullptr;
        t->coop_player_snapshot_sizes[i] = 0;
    }
    t->coop_merged_snapshot = nullptr;
    t->coop_merged_snapshot_size = 0;
    t->coop_view_dirty = 0;


    // Explicitly initialize all members
    t->window = nullptr;
    t->renderer = nullptr;
    t->template_data = nullptr;

    // Initialize texture cache
    t->texture_cache = nullptr;
    t->texture_cache_count = 0;
    t->texture_cache_capacity = 0;
    t->anim_cache = nullptr;
    t->anim_cache_count = 0;
    t->anim_cache_capacity = 0;

    // Initialize all string buffers to empty strings
    t->advancement_template_path[0] = '\0';
    t->lang_path[0] = '\0';
    t->saves_path[0] = '\0';
    t->world_name[0] = '\0';
    t->advancements_path[0] = '\0';
    t->unlocks_path[0] = '\0';
    t->stats_path[0] = '\0';
    t->snapshot_path[0] = '\0';

    // Initialize camera and zoom from settings.json
    t->camera_offset = ImVec2(settings->view_pan_x, settings->view_pan_y);
    t->zoom_level = (settings->view_zoom > 0.1f) ? settings->view_zoom : 1.0f;
    t->layout_locked = settings->view_locked;
    t->locked_layout_width = (settings->view_locked_width > 0.0f) ? settings->view_locked_width : 0.0f;

    // Initialize time since last update
    t->time_since_last_update = 0.0f;

    // Initialize SDL components for the tracker
    if (!tracker_init_sdl(t, settings)) {
        free(t);
        t = nullptr;
        *tracker = nullptr;
        tracker = nullptr;
        return false;
    }

    // Initialize SDL_ttf
    char font_path[MAX_PATH_LENGTH];
    snprintf(font_path, sizeof(font_path), "%s/fonts/Minecraft.ttf", get_application_dir());
    t->minecraft_font = TTF_OpenFont(font_path, 24);
    if (!t->minecraft_font) {
        log_message(
            LOG_ERROR,
            "[TRACKER] Failed to load Minecraft font: %s\n",
            SDL_GetError());
        tracker_free(tracker, settings);
        return false;
    }

    // Load global background textures
    tracker_reload_background_textures(t, settings);

    // Check if loading (including fallbacks) failed for any background
    if ((!t->adv_bg && !t->adv_bg_anim) ||
        (!t->adv_bg_half_done && !t->adv_bg_half_done_anim) ||
        (!t->adv_bg_done && !t->adv_bg_done_anim)) {
        log_message(LOG_ERROR, "[TRACKER] CRITICAL: Failed to load default background textures as fallback.\n");
        tracker_free(tracker, settings);
        return false; // Critical failure if defaults also fail
    }

    // Allocate the main data container
    t->template_data = (TemplateData *) calloc(1, sizeof(TemplateData));
    if (!t->template_data) {
        log_message(LOG_ERROR, "[TRACKER] Failed to allocate memory for template data.\n");
        tracker_free(tracker, settings);
        return false;
    }

    // Ensure snapshot world name is initially empty
    t->template_data->snapshot_world_name[0] = '\0';
    t->template_data->snapshot_world_name[0] = '\0';

    // Initialize paths (also during runtime)
    tracker_reinit_paths(t, settings);

    // Parse the advancement template JSON file and check for critical failure
    if (!tracker_load_and_parse_data(t, settings)) {
        tracker_free(tracker, settings);
        return false;
    }

    return true; // Success
}

void tracker_events(Tracker *t, SDL_Event *event, bool *is_running, bool *settings_opened) {
    switch (event->type) {
        // This should be handled in the global event handler
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
            // Check for unsaved changes or active lobby before quitting
            CoopNetState quit_net_state = g_coop_ctx ? coop_net_get_state(g_coop_ctx) : COOP_NET_IDLE;
            bool quit_lobby_active = (quit_net_state == COOP_NET_LISTENING || quit_net_state == COOP_NET_CONNECTED
                                      || quit_net_state == COOP_NET_CONNECTING);
            if (t && (t->settings_has_unsaved_changes || t->template_editor_has_unsaved_changes || quit_lobby_active)) {
                t->quit_requested = true;
            } else {
                *is_running = false;
            }
        }
        break;

        case SDL_EVENT_KEY_DOWN:
            if (event->key.repeat == 0) {
                // If any popup is open, block all subsequent hotkeys handled here.
                if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
                    break;
                }

                // Do not process tracker-specific hotkeys if an ImGui item is active.
                if (ImGui::IsAnyItemActive()) {
                    break;
                }
                switch (event->key.scancode) {
                    case SDL_SCANCODE_ESCAPE:
                        // printf("[TRACKER] Escape key pressed in tracker: Opening settings window now.\n");
                        // Open settings window, TOGGLE settings_opened
                        *settings_opened = !(*settings_opened);
                        break;
                    case SDL_SCANCODE_SPACE:
                        // Toggle the layout locked state
                        t->layout_locked = !t->layout_locked;

                        // If we just locked the layout, save the current width
                        if (t->layout_locked) {
                            // This logic mirrors what happens when the UI checkbox is clicked.
                            ImGuiIO &io = ImGui::GetIO();
                            t->locked_layout_width = io.DisplaySize.x / t->zoom_level;
                        }
                        break;
                    // Window move/resize events are handled in main.c
                    default:
                        break;
                }
            }
            break;
        default:
            break;
    }
}

// -------------------------------------------- CO-OP MERGE HELPERS --------------------------------------------

/**
 * @brief Resets all progress fields in TemplateData to zero/false before co-op merge.
 */
static void coop_reset_template_progress(TemplateData *td) {
    td->advancements_completed_count = 0;
    td->completed_criteria_count = 0;
    td->stats_completed_count = 0;
    td->stats_completed_criteria_count = 0;
    td->unlocks_completed_count = 0;
    td->play_time_ticks = 0;
    td->frozen_play_time_ticks = 0;
    td->run_completed = false;
    td->overall_progress_percentage = 0.0f;

    for (int i = 0; i < td->advancement_count; i++) {
        TrackableCategory *adv = td->advancements[i];
        adv->done = false;
        adv->all_template_criteria_met = false;
        adv->completed_criteria_count = 0;
        for (int j = 0; j < adv->criteria_count; j++) {
            adv->criteria[j]->done = false;
        }
    }

    for (int i = 0; i < td->stat_count; i++) {
        TrackableCategory *stat_cat = td->stats[i];
        stat_cat->done = false;
        stat_cat->completed_criteria_count = 0;
        for (int j = 0; j < stat_cat->criteria_count; j++) {
            stat_cat->criteria[j]->progress = 0;
            stat_cat->criteria[j]->done = false;
        }
    }

    for (int i = 0; i < td->multi_stage_goal_count; i++) {
        td->multi_stage_goals[i]->current_stage = 0;
        for (int j = 0; j < td->multi_stage_goals[i]->stage_count; j++) {
            td->multi_stage_goals[i]->stages[j]->current_stat_progress = 0;
            td->multi_stage_goals[i]->stages[j]->coop_completed = false;
        }
    }
}

/**
 * @brief Merges one player's advancement data into TemplateData (accumulative).
 *
 * - Simple advancements (no criteria): OR across players — done if any player completed
 * - Complex advancements (has criteria): track the player with the most criteria completed
 *
 * Works for both modern advancements (1.12+) and mid-era achievements (1.7-1.11.2).
 */
static void coop_merge_advancements_modern(TemplateData *td, const cJSON *player_adv_json) {
    if (!player_adv_json) return;

    for (int i = 0; i < td->advancement_count; i++) {
        TrackableCategory *adv = td->advancements[i];

        cJSON *player_entry = cJSON_GetObjectItem(player_adv_json, adv->root_name);
        if (!player_entry) continue;

        bool game_is_done = cJSON_IsTrue(cJSON_GetObjectItem(player_entry, "done"));

        if (adv->criteria_count == 0) {
            // Simple advancement: OR across players
            if (game_is_done) {
                adv->done = true;
                adv->all_template_criteria_met = true;
            }
        } else {
            // Complex advancement: track player with most criteria
            cJSON *player_criteria = cJSON_GetObjectItem(player_entry, "criteria");
            int this_player_count = 0;

            if (player_criteria) {
                for (int j = 0; j < adv->criteria_count; j++) {
                    if (cJSON_HasObjectItem(player_criteria, adv->criteria[j]->root_name)) {
                        this_player_count++;
                    }
                }
            }

            // If this player has more criteria than the current best, adopt their state
            if (this_player_count > adv->completed_criteria_count) {
                adv->completed_criteria_count = this_player_count;
                // Overwrite criteria done flags with this player's state
                for (int j = 0; j < adv->criteria_count; j++) {
                    adv->criteria[j]->done = (player_criteria &&
                                              cJSON_HasObjectItem(player_criteria, adv->criteria[j]->root_name));
                }
                adv->all_template_criteria_met = (adv->completed_criteria_count >= adv->criteria_count);
                adv->done = game_is_done || adv->all_template_criteria_met;
            } else if (game_is_done && !adv->done) {
                // Even if another player had more criteria, if this player fully completed it, mark done
                adv->done = true;
                adv->all_template_criteria_met = true;
            }
        }
    }
}

/**
 * @brief Merges one player's mid-era achievement data into TemplateData (accumulative).
 * Mid-era (1.7-1.11.2): achievements are in a flat JSON with value/progress fields.
 */
static void coop_merge_achievements_mid(TemplateData *td, const cJSON *player_stats_json) {
    if (!player_stats_json) return;

    for (int i = 0; i < td->advancement_count; i++) {
        TrackableCategory *ach = td->advancements[i];

        cJSON *ach_entry = cJSON_GetObjectItem(player_stats_json, ach->root_name);
        if (!ach_entry) continue;

        // Determine if this player has it done
        bool game_is_done = false;
        if (cJSON_IsNumber(ach_entry)) {
            game_is_done = (ach_entry->valueint >= 1);
        } else if (cJSON_IsObject(ach_entry)) {
            cJSON *value_item = cJSON_GetObjectItem(ach_entry, "value");
            game_is_done = (cJSON_IsNumber(value_item) && value_item->valueint >= 1);
        }

        if (ach->criteria_count == 0) {
            // Simple achievement: OR across players
            if (game_is_done) {
                ach->done = true;
                ach->all_template_criteria_met = true;
            }
        } else {
            // Complex achievement with criteria: track player with most
            cJSON *progress_array = cJSON_GetObjectItem(ach_entry, "progress");
            int this_player_count = 0;

            if (cJSON_IsArray(progress_array)) {
                for (int j = 0; j < ach->criteria_count; j++) {
                    cJSON *progress_item;
                    cJSON_ArrayForEach(progress_item, progress_array) {
                        if (cJSON_IsString(progress_item) &&
                            strcmp(progress_item->valuestring, ach->criteria[j]->root_name) == 0) {
                            this_player_count++;
                            break;
                        }
                    }
                }
            }

            if (this_player_count > ach->completed_criteria_count) {
                ach->completed_criteria_count = this_player_count;
                // Overwrite criteria done flags
                for (int j = 0; j < ach->criteria_count; j++) {
                    bool found = false;
                    if (cJSON_IsArray(progress_array)) {
                        cJSON *progress_item;
                        cJSON_ArrayForEach(progress_item, progress_array) {
                            if (cJSON_IsString(progress_item) &&
                                strcmp(progress_item->valuestring, ach->criteria[j]->root_name) == 0) {
                                found = true;
                                break;
                            }
                        }
                    }
                    ach->criteria[j]->done = found;
                }
                ach->all_template_criteria_met = (ach->completed_criteria_count >= ach->criteria_count);
                ach->done = game_is_done || ach->all_template_criteria_met;
            } else if (game_is_done && !ach->done) {
                ach->done = true;
                ach->all_template_criteria_met = true;
            }
        }
    }
}

/**
 * @brief Merges one player's legacy achievement data into TemplateData (accumulative).
 * Legacy (<=1.6.4): achievements are in stats-change array.
 */
static void coop_merge_achievements_legacy(TemplateData *td, const cJSON *player_stats_json) {
    if (!player_stats_json) return;

    cJSON *stats_change = cJSON_GetObjectItem(player_stats_json, "stats-change");
    if (!cJSON_IsArray(stats_change)) return;

    for (int i = 0; i < td->advancement_count; i++) {
        TrackableCategory *ach = td->advancements[i];

        cJSON *stat_entry;
        cJSON_ArrayForEach(stat_entry, stats_change) {
            cJSON *item = stat_entry->child;
            if (item && strcmp(item->string, ach->root_name) == 0 && item->valueint >= 1) {
                ach->done = true; // OR across players
                break;
            }
        }
    }
}

/**
 * @brief Merges one player's stat data into TemplateData using modern JSON format (1.13+).
 * Supports HIGHEST (max) or CUMULATIVE (sum) merge modes.
 */
static void coop_merge_stats_modern(TemplateData *td, const cJSON *player_stats_json,
                                    CoopStatMerge merge_mode, MC_Version version) {
    if (!player_stats_json) return;

    cJSON *stats_obj = cJSON_GetObjectItem(player_stats_json, "stats");
    if (!stats_obj) return;

    // Merge playtime (always use max across players for IGT display)
    cJSON *custom_stats = cJSON_GetObjectItem(stats_obj, "minecraft:custom");
    if (custom_stats) {
        const char *playtime_key = (version >= MC_VERSION_1_17) ? "minecraft:play_time" : "minecraft:play_one_minute";
        cJSON *play_time = cJSON_GetObjectItem(custom_stats, playtime_key);
        if (cJSON_IsNumber(play_time)) {
            long long player_time = (long long) play_time->valuedouble;
            if (player_time > td->play_time_ticks) {
                td->play_time_ticks = player_time;
            }
        }
    }

    for (int i = 0; i < td->stat_count; i++) {
        TrackableCategory *stat_cat = td->stats[i];

        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];

            if (sub_stat->stat_category_key[0] != '\0') {
                cJSON *category_obj = cJSON_GetObjectItem(stats_obj, sub_stat->stat_category_key);
                if (category_obj) {
                    cJSON *stat_value = cJSON_GetObjectItem(category_obj, sub_stat->stat_item_key);
                    if (cJSON_IsNumber(stat_value)) {
                        int player_value = stat_value->valueint;
                        if (merge_mode == COOP_STAT_CUMULATIVE) {
                            sub_stat->progress += player_value;
                        } else {
                            // COOP_STAT_HIGHEST
                            if (player_value > sub_stat->progress) {
                                sub_stat->progress = player_value;
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * @brief Merges one player's stat data into TemplateData using mid-era JSON format (1.7-1.12.2).
 * Flat JSON with stat keys at top level.
 */
static void coop_merge_stats_mid(TemplateData *td, const cJSON *player_stats_json,
                                 CoopStatMerge merge_mode) {
    if (!player_stats_json) return;

    // Merge mid-era playtime
    cJSON *play_time_entry = cJSON_GetObjectItem(player_stats_json, "stat.playOneMinute");
    if (cJSON_IsNumber(play_time_entry)) {
        long long player_time = (long long) play_time_entry->valuedouble;
        if (player_time > td->play_time_ticks) {
            td->play_time_ticks = player_time;
        }
    }

    for (int i = 0; i < td->stat_count; i++) {
        TrackableCategory *stat_cat = td->stats[i];

        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];

            cJSON *stat_entry = cJSON_GetObjectItem(player_stats_json, sub_stat->root_name);
            if (cJSON_IsNumber(stat_entry)) {
                int player_value = stat_entry->valueint;
                if (merge_mode == COOP_STAT_CUMULATIVE) {
                    sub_stat->progress += player_value;
                } else {
                    if (player_value > sub_stat->progress) {
                        sub_stat->progress = player_value;
                    }
                }
            }
        }
    }
}

/**
 * @brief Merges one player's stat data using legacy .dat format (<=1.6.4).
 * Legacy stats are in "stats-change" array with numeric IDs.
 */
static void coop_merge_stats_legacy(TemplateData *td, const cJSON *player_stats_json,
                                    CoopStatMerge merge_mode) {
    if (!player_stats_json) return;

    cJSON *stats_change = cJSON_GetObjectItem(player_stats_json, "stats-change");
    if (!cJSON_IsArray(stats_change)) return;

    // Merge legacy playtime (stat ID 1100)
    cJSON *stat_entry_iter;
    cJSON_ArrayForEach(stat_entry_iter, stats_change) {
        cJSON *item = stat_entry_iter->child;
        if (item && strcmp(item->string, "1100") == 0) {
            long long player_time = (long long) item->valueint;
            if (player_time > td->play_time_ticks) {
                td->play_time_ticks = player_time;
            }
            break;
        }
    }

    for (int i = 0; i < td->stat_count; i++) {
        TrackableCategory *stat_cat = td->stats[i];

        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];

            cJSON *stat_entry;
            cJSON_ArrayForEach(stat_entry, stats_change) {
                cJSON *item_inner = stat_entry->child;
                if (item_inner && strcmp(item_inner->string, sub_stat->root_name) == 0) {
                    int player_value = item_inner->valueint - sub_stat->initial_progress;
                    if (player_value < 0) player_value = 0;
                    if (merge_mode == COOP_STAT_CUMULATIVE) {
                        sub_stat->progress += player_value;
                    } else {
                        if (player_value > sub_stat->progress) {
                            sub_stat->progress = player_value;
                        }
                    }
                    break;
                }
            }
        }
    }
}

/**
 * @brief Merges one player's unlock data into TemplateData (OR across players).
 */
// Note: coop_merge_unlocks was removed — co-op is disabled for craftmine (the only
// version with unlocks), so unlock merging is not needed.
/**
 * @brief Accumulates one player's data into multi-stage goals for co-op merge.
 *
 * For stats, progress is always accumulated (summed) across players — this is NOT
 * affected by the highest/cumulative toggle. For advancements, criteria, and unlocks,
 * completion is OR'd across players. The actual stage progression is evaluated
 * globally in coop_finalize_multi_stage() after all players have been merged.
 */
static void coop_merge_multi_stage(TemplateData *td, const cJSON *player_adv_json,
                                   const cJSON *player_stats_json, const cJSON *player_unlocks_json,
                                   MC_Version version) {
    if (td->multi_stage_goal_count == 0) return;
    if (!player_adv_json && !player_stats_json) return;

    for (int i = 0; i < td->multi_stage_goal_count; i++) {
        MultiStageGoal *goal = td->multi_stage_goals[i];

        for (int j = 0; j < goal->stage_count; j++) {
            SubGoal *stage = goal->stages[j];

            if (stage->type == SUBGOAL_MANUAL) continue;

            switch (stage->type) {
                case SUBGOAL_ADVANCEMENT:
                    if (player_adv_json) {
                        cJSON *adv_entry = cJSON_GetObjectItem(player_adv_json, stage->root_name);
                        if (adv_entry && cJSON_IsTrue(cJSON_GetObjectItem(adv_entry, "done"))) {
                            stage->coop_completed = true;
                        }
                    }
                    break;

                case SUBGOAL_STAT: {
                    int player_progress = 0;
                    if (version <= MC_VERSION_1_6_4) {
                        // Legacy: look up from already-merged stats in TemplateData
                        for (int c_idx = 0; c_idx < td->stat_count; c_idx++) {
                            for (int s_idx = 0; s_idx < td->stats[c_idx]->criteria_count; s_idx++) {
                                TrackableItem *sub = td->stats[c_idx]->criteria[s_idx];
                                if (strcmp(sub->root_name, stage->root_name) == 0) {
                                    player_progress = sub->progress;
                                    goto stat_found_coop;
                                }
                            }
                        }
                    stat_found_coop:;
                    } else if (version <= MC_VERSION_1_12_2) {
                        cJSON *stat_entry = cJSON_GetObjectItem(player_stats_json, stage->root_name);
                        if (cJSON_IsNumber(stat_entry)) {
                            player_progress = stat_entry->valueint;
                        }
                    } else {
                        cJSON *stats_obj = cJSON_GetObjectItem(player_stats_json, "stats");
                        if (stats_obj) {
                            char root_copy[192];
                            strncpy(root_copy, stage->root_name, sizeof(root_copy) - 1);
                            root_copy[sizeof(root_copy) - 1] = '\0';
                            char *item_key = strchr(root_copy, '/');
                            if (item_key) {
                                *item_key = '\0';
                                item_key++;
                                cJSON *cat_obj = cJSON_GetObjectItem(stats_obj, root_copy);
                                if (cat_obj) {
                                    cJSON *val = cJSON_GetObjectItem(cat_obj, item_key);
                                    if (cJSON_IsNumber(val)) {
                                        player_progress = val->valueint;
                                    }
                                }
                            }
                        }
                    }

                    // Stats in multi-stage goals are ALWAYS cumulative (summed across players)
                    stage->current_stat_progress += player_progress;
                    break;
                }

                case SUBGOAL_CRITERION:
                    if (version >= MC_VERSION_1_12) {
                        if (player_adv_json) {
                            cJSON *adv_entry = cJSON_GetObjectItem(player_adv_json, stage->parent_advancement);
                            if (adv_entry) {
                                cJSON *criteria_obj = cJSON_GetObjectItem(adv_entry, "criteria");
                                if (criteria_obj && cJSON_HasObjectItem(criteria_obj, stage->root_name)) {
                                    stage->coop_completed = true;
                                }
                            }
                        }
                    } else if (version >= MC_VERSION_1_7_2) {
                        if (player_stats_json) {
                            cJSON *ach_entry = cJSON_GetObjectItem(player_stats_json, stage->parent_advancement);
                            if (ach_entry) {
                                cJSON *progress_array = cJSON_GetObjectItem(ach_entry, "progress");
                                if (cJSON_IsArray(progress_array)) {
                                    cJSON *p_item;
                                    cJSON_ArrayForEach(p_item, progress_array) {
                                        if (cJSON_IsString(p_item) &&
                                            strcmp(p_item->valuestring, stage->root_name) == 0) {
                                            stage->coop_completed = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    break;

                case SUBGOAL_UNLOCK:
                    if (player_unlocks_json) {
                        cJSON *obtained_obj = cJSON_GetObjectItem(player_unlocks_json, "obtained");
                        if (obtained_obj && cJSON_IsTrue(cJSON_GetObjectItem(obtained_obj, stage->root_name))) {
                            stage->coop_completed = true;
                        }
                    }
                    break;

                case SUBGOAL_MANUAL:
                default:
                    break;
            }
        }
    }
}

static void coop_finalize_multi_stage(TemplateData *td) {
    for (int i = 0; i < td->multi_stage_goal_count; i++) {
        MultiStageGoal *goal = td->multi_stage_goals[i];
        goal->current_stage = 0;

        for (int j = 0; j < goal->stage_count; j++) {
            SubGoal *stage = goal->stages[j];

            if (stage->type == SUBGOAL_MANUAL) break;

            bool stage_completed = false;
            if (stage->type == SUBGOAL_STAT) {
                stage_completed = (stage->current_stat_progress >= stage->required_progress);
            } else {
                stage_completed = stage->coop_completed;
            }

            if (stage_completed) {
                goal->current_stage = j + 1;
            } else {
                break; // Stages are sequential; stop at first incomplete
            }
        }
    }
}

/**
 * @brief After merging all players' stat data, finalize stat completion status.
 * Applies manual overrides and determines done flags based on merged progress values.
 */
static void coop_finalize_stats(TemplateData *td, const cJSON *settings_json) {
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : nullptr;

    td->stats_completed_count = 0;
    td->stats_completed_criteria_count = 0;

    for (int i = 0; i < td->stat_count; i++) {
        TrackableCategory *stat_cat = td->stats[i];
        stat_cat->completed_criteria_count = 0;

        cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : nullptr;
        bool parent_forced_true = cJSON_IsBool(parent_override) && cJSON_IsTrue(parent_override);
        stat_cat->is_manually_completed = parent_forced_true;

        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub_stat = stat_cat->criteria[j];
            bool naturally_done = (sub_stat->goal > 0 && sub_stat->progress >= sub_stat->goal);

            cJSON *sub_override;
            if (stat_cat->criteria_count == 1) {
                sub_override = parent_override;
            } else {
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s",
                         stat_cat->root_name, sub_stat->root_name);
                sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : nullptr;
            }
            bool sub_forced_true = cJSON_IsBool(sub_override) && cJSON_IsTrue(sub_override);
            sub_stat->is_manually_completed = sub_forced_true;

            sub_stat->done = naturally_done || sub_forced_true || parent_forced_true;
            if (sub_stat->done) stat_cat->completed_criteria_count++;
        }

        bool all_children_done = (stat_cat->criteria_count > 0 &&
                                  stat_cat->completed_criteria_count >= stat_cat->criteria_count);
        stat_cat->done = all_children_done || parent_forced_true;

        if (stat_cat->done) td->stats_completed_count++;
        td->stats_completed_criteria_count += stat_cat->completed_criteria_count;
    }
}

/**
 * @brief After merging all players' advancement data, finalize advancement completion counts.
 */
static void coop_finalize_advancements(TemplateData *td) {
    td->advancements_completed_count = 0;
    td->completed_criteria_count = 0;

    for (int i = 0; i < td->advancement_count; i++) {
        TrackableCategory *adv = td->advancements[i];
        if (adv->done && !adv->is_recipe) td->advancements_completed_count++;
        td->completed_criteria_count += adv->completed_criteria_count;
    }
}

// Periodically recheck file changes
void tracker_update(Tracker *t, const AppSettings *settings) {
    // Detect if the world has changed since the last update.
    if (t->template_data->last_known_world_name[0] == '\0' || // Handle first-time load
        strcmp(t->world_name, t->template_data->last_known_world_name) != 0) {
        // Save notes for the OLD world before doing anything else
        tracker_save_notes(t, settings);

        // Reset custom progress and manual stat overrides on world change
        tracker_reset_progress_on_world_change(t, settings);

        // Update the notes path to the new world and reload the notes content
        tracker_update_notes_path(t, settings);

        // Load from new file into buffer
        tracker_load_notes(t, settings);

        // Invalidate the old UI widget state by changing its future ID
        t->notes_widget_id_counter++;
    }
    // After the check, update the last known world name to the current one for the next cycle.
    strncpy(t->template_data->last_known_world_name, t->world_name,
            sizeof(t->template_data->last_known_world_name) - 1);
    t->template_data->last_known_world_name[sizeof(t->template_data->last_known_world_name) - 1] = '\0';


    MC_Version version = settings_get_version_from_string(settings->version_str);

    // Legacy Snapshot Logic
    // ONLY USING SNAPSHOTTING LOGIC IF *NOT* USING StatsPerWorld MOD
    // If StatsPerWorld is enabled, we don't need to take snapshots, but read directly from the
    // per-world stat files, like mid-era versions, but still reading with IDs and not strings
    // If the version is legacy and the current world name doesn't match the snapshot's world name,
    // it means we've loaded a new world and need to take a new snapshot of the global stats
    if (version <= MC_VERSION_1_6_4 && !settings->using_stats_per_world_legacy && strcmp(
            t->world_name, t->template_data->snapshot_world_name) != 0) {
        log_message(LOG_INFO, "[TRACKER] Legacy world change detected. Taking new stat snapshot for world: %s\n",
                    t->world_name);

        tracker_snapshot_legacy_stats(t, settings);
        // Take a new snapshot when StatsPerWorld is disabled in legacy version
    }

    // Load all necessary player files ONCE
    cJSON *player_adv_json = nullptr;
    // (strlen(t->advancements_path) > 0) ? cJSON_from_file(t->advancements_path) : nullptr;
    cJSON *player_stats_json = (strlen(t->stats_path) > 0) ? cJSON_from_file(t->stats_path) : nullptr;
    cJSON *player_unlocks_json = (strlen(t->unlocks_path) > 0) ? cJSON_from_file(t->unlocks_path) : nullptr;
    cJSON *settings_json = cJSON_from_file(get_settings_file_path());

    // Version-based Dispatch
    if (version <= MC_VERSION_1_6_4) {
        // If StatsPerWorld mod is enabled, stats file is per-world, still using IDs
        tracker_update_stats_legacy(t, player_stats_json);
    } else if (version >= MC_VERSION_1_7_2 && version <= MC_VERSION_1_11_2) {
        // Mid-Era: 1.7.2 through 1.11.2
        // This function handles both achievements and stats for this range.
        tracker_update_achievements_and_stats_mid(t, player_stats_json);
    } else if (version >= MC_VERSION_1_12 && version <= MC_VERSION_1_12_2) {
        // Hybrid Era: 1.12.x (Modern Advancements, Mid-era Stats)
        player_adv_json = (strlen(t->advancements_path) > 0) ? cJSON_from_file(t->advancements_path) : nullptr;
        tracker_update_advancements_modern(t, player_adv_json);
        tracker_update_stats_mid(t, player_stats_json, settings_json); // Use the new stats-only function
    } else if (version >= MC_VERSION_1_13) {
        // Modern Era: 1.13+
        player_adv_json = (strlen(t->advancements_path) > 0) ? cJSON_from_file(t->advancements_path) : nullptr;
        tracker_update_advancements_modern(t, player_adv_json);

        // Needs version for playtime as 1.17 renames minecraft:play_one_minute into minecraft:play_time
        tracker_update_stats_modern(t, player_stats_json, settings_json, version);
        tracker_update_unlock_progress(t, player_unlocks_json); // Just returns if unlocks don't exist
    }

    // Pass the parsed data to the update functions
    tracker_update_custom_progress(t, settings_json, settings);
    tracker_update_multi_stage_progress(t, player_adv_json, player_stats_json, player_unlocks_json, version, settings);
    // Fixed-point iteration: run until no new completions occur (handles arbitrary-depth chains).
    // Counters participate so that stats/custom goals/counters can all link to counters.
    // Safety cap of 32 iterations prevents infinite loops from unexpected circular references.
    {
        bool changed;
        int guard = 0;
        do {
            changed = tracker_update_custom_goal_linked_goals(t);
            changed |= tracker_update_stat_linked_goals(t);
            changed |= tracker_update_counter_goals(t);
        } while (changed && ++guard < 32);
    }
    tracker_calculate_overall_progress(t, version, settings); //THIS TRACKS SUB-ADVANCEMENTS AND EVERYTHING ELSE

    // Clean up the parsed JSON objects
    cJSON_Delete(player_adv_json);
    cJSON_Delete(player_stats_json);
    cJSON_Delete(player_unlocks_json);
    cJSON_Delete(settings_json);
}

void tracker_update_coop_merged(Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data || !settings) return;

    MC_Version version = settings_get_version_from_string(settings->version_str);

    // Detect world changes (same as tracker_update)
    if (t->template_data->last_known_world_name[0] == '\0' ||
        strcmp(t->world_name, t->template_data->last_known_world_name) != 0) {
        tracker_save_notes(t, settings);
        tracker_reset_progress_on_world_change(t, settings);
        tracker_update_notes_path(t, settings);
        tracker_load_notes(t, settings);
        t->notes_widget_id_counter++;
    }
    strncpy(t->template_data->last_known_world_name, t->world_name,
            sizeof(t->template_data->last_known_world_name) - 1);
    t->template_data->last_known_world_name[sizeof(t->template_data->last_known_world_name) - 1] = '\0';

    // Legacy snapshot logic (applies to host's own files for world-change detection)
    if (version <= MC_VERSION_1_6_4 && !settings->using_stats_per_world_legacy &&
        strcmp(t->world_name, t->template_data->snapshot_world_name) != 0) {
        log_message(LOG_INFO, "[COOP] Legacy world change detected. Taking new stat snapshot for world: %s\n",
                    t->world_name);
        tracker_snapshot_legacy_stats(t, settings);
    }

    // 1. Reset all progress to zero before merging
    coop_reset_template_progress(t->template_data);

    // Clear the Hermes per-player stat cache — the file-based merge is authoritative.
    // It will be re-seeded below with each player's values from their JSON files.
    auto *hermes_cache = t->hermes_coop_stat_cache
                             ? static_cast<std::unordered_map<std::string, int> *>(t->hermes_coop_stat_cache)
                             : nullptr;
    if (hermes_cache) hermes_cache->clear();

    // 2. Iterate over all players in the roster and merge their data
    for (int p = 0; p < settings->coop_player_count; p++) {
        const CoopPlayer *player = &settings->coop_players[p];

        // Find this player's data files by UUID
        char player_adv_path[MAX_PATH_LENGTH];
        char player_stats_path[MAX_PATH_LENGTH];
        char player_unlocks_path[MAX_PATH_LENGTH];

        find_player_data_files_for_uuid(
            t->saves_path, version, settings->using_stats_per_world_legacy,
            t->world_name, player->uuid, player->username,
            player_adv_path, player_stats_path, player_unlocks_path, MAX_PATH_LENGTH
        );

        // Parse the player's JSON files
        cJSON *player_adv_json = (player_adv_path[0] != '\0') ? cJSON_from_file(player_adv_path) : nullptr;
        cJSON *player_stats_json = (player_stats_path[0] != '\0') ? cJSON_from_file(player_stats_path) : nullptr;
        cJSON *player_unlocks_json = (player_unlocks_path[0] != '\0') ? cJSON_from_file(player_unlocks_path) : nullptr;

        // Merge advancements/achievements
        if (version <= MC_VERSION_1_6_4) {
            coop_merge_achievements_legacy(t->template_data, player_stats_json);
            coop_merge_stats_legacy(t->template_data, player_stats_json, settings->coop_stat_merge);
        } else if (version >= MC_VERSION_1_7_2 && version <= MC_VERSION_1_11_2) {
            coop_merge_achievements_mid(t->template_data, player_stats_json);
            coop_merge_stats_mid(t->template_data, player_stats_json, settings->coop_stat_merge);
        } else if (version >= MC_VERSION_1_12 && version <= MC_VERSION_1_12_2) {
            coop_merge_advancements_modern(t->template_data, player_adv_json);
            coop_merge_stats_mid(t->template_data, player_stats_json, settings->coop_stat_merge);
        } else if (version >= MC_VERSION_1_13) {
            coop_merge_advancements_modern(t->template_data, player_adv_json);
            coop_merge_stats_modern(t->template_data, player_stats_json, settings->coop_stat_merge, version);
        }

        // Merge multi-stage goals (global — any player any stage)
        coop_merge_multi_stage(t->template_data, player_adv_json, player_stats_json,
                               player_unlocks_json, version);

        // Seed the Hermes per-player stat cache with this player's current values.
        // This prevents double-counting when the first Hermes event arrives after
        // a file-based merge — without seeding, old_value would be 0 and the entire
        // file-based value would be added as a delta on top of the merged total.
        if (hermes_cache && settings->using_hermes &&
            settings->coop_stat_merge == COOP_STAT_CUMULATIVE &&
            player_stats_json && player->uuid[0] != '\0') {
            std::string uuid_prefix = player->uuid;
            uuid_prefix += ':';

            if (version >= MC_VERSION_1_13) {
                // Modern: stats → { "minecraft:custom": { "minecraft:play_time": 123, ... }, ... }
                cJSON *stats_obj = cJSON_GetObjectItem(player_stats_json, "stats");
                if (stats_obj) {
                    for (int i = 0; i < t->template_data->stat_count; i++) {
                        TrackableCategory *sc = t->template_data->stats[i];
                        for (int j = 0; j < sc->criteria_count; j++) {
                            TrackableItem *sub = sc->criteria[j];
                            if (sub->stat_category_key[0] == '\0') continue;
                            cJSON *cat_obj = cJSON_GetObjectItem(stats_obj, sub->stat_category_key);
                            if (!cat_obj) continue;
                            cJSON *val = cJSON_GetObjectItem(cat_obj, sub->stat_item_key);
                            if (cJSON_IsNumber(val)) {
                                // Build the Hermes-format key: "minecraft.picked_up:minecraft.oak_log"
                                // Hermes uses dots where the JSON has colons in the category/item prefix
                                char hermes_key[384];
                                char h_cat[192], h_item[192];
                                strncpy(h_cat, sub->stat_category_key, sizeof(h_cat) - 1);
                                h_cat[sizeof(h_cat) - 1] = '\0';
                                strncpy(h_item, sub->stat_item_key, sizeof(h_item) - 1);
                                h_item[sizeof(h_item) - 1] = '\0';
                                // Convert "minecraft:picked_up" → "minecraft.picked_up"
                                char *colon_pos = strchr(h_cat, ':');
                                if (colon_pos) *colon_pos = '.';
                                colon_pos = strchr(h_item, ':');
                                if (colon_pos) *colon_pos = '.';
                                snprintf(hermes_key, sizeof(hermes_key), "%s:%s", h_cat, h_item);
                                (*hermes_cache)[uuid_prefix + hermes_key] = val->valueint;
                            }
                        }
                    }
                }
            } else {
                // Legacy/mid-era: flat JSON with stat keys at top level
                for (int i = 0; i < t->template_data->stat_count; i++) {
                    TrackableCategory *sc = t->template_data->stats[i];
                    for (int j = 0; j < sc->criteria_count; j++) {
                        TrackableItem *sub = sc->criteria[j];
                        cJSON *val = cJSON_GetObjectItem(player_stats_json, sub->root_name);
                        if (cJSON_IsNumber(val)) {
                            (*hermes_cache)[uuid_prefix + sub->root_name] = val->valueint;
                        }
                    }
                }
            }
        }

        // Clean up this player's JSON
        cJSON_Delete(player_adv_json);
        cJSON_Delete(player_stats_json);
        cJSON_Delete(player_unlocks_json);
    }

    // 3. Finalize after all players are merged
    cJSON *settings_json = cJSON_from_file(get_settings_file_path());

    coop_finalize_advancements(t->template_data);
    coop_finalize_stats(t->template_data, settings_json);
    coop_finalize_multi_stage(t->template_data);

    // Custom goals: handled by host's settings.json (not merged from game files)
    tracker_update_custom_progress(t, settings_json, settings);

    // Fixed-point iteration for linked goals
    {
        bool changed;
        int guard = 0;
        do {
            changed = tracker_update_custom_goal_linked_goals(t);
            changed |= tracker_update_stat_linked_goals(t);
            changed |= tracker_update_counter_goals(t);
        } while (changed && ++guard < 32);
    }

    tracker_calculate_overall_progress(t, version, settings);

    cJSON_Delete(settings_json);
}

void tracker_update_coop_single_player(Tracker *t, const AppSettings *settings, int player_idx) {
    if (!t || !t->template_data || !settings) return;
    if (player_idx < 0 || player_idx >= settings->coop_player_count) return;

    MC_Version version = settings_get_version_from_string(settings->version_str);

    // Reset all progress to zero
    coop_reset_template_progress(t->template_data);

    // Merge only the selected player
    const CoopPlayer *player = &settings->coop_players[player_idx];

    char player_adv_path[MAX_PATH_LENGTH];
    char player_stats_path[MAX_PATH_LENGTH];
    char player_unlocks_path[MAX_PATH_LENGTH];

    find_player_data_files_for_uuid(
        t->saves_path, version, settings->using_stats_per_world_legacy,
        t->world_name, player->uuid, player->username,
        player_adv_path, player_stats_path, player_unlocks_path, MAX_PATH_LENGTH
    );

    cJSON *player_adv_json = (player_adv_path[0] != '\0') ? cJSON_from_file(player_adv_path) : nullptr;
    cJSON *player_stats_json = (player_stats_path[0] != '\0') ? cJSON_from_file(player_stats_path) : nullptr;
    cJSON *player_unlocks_json = (player_unlocks_path[0] != '\0') ? cJSON_from_file(player_unlocks_path) : nullptr;

    if (version <= MC_VERSION_1_6_4) {
        coop_merge_achievements_legacy(t->template_data, player_stats_json);
        coop_merge_stats_legacy(t->template_data, player_stats_json, settings->coop_stat_merge);
    } else if (version >= MC_VERSION_1_7_2 && version <= MC_VERSION_1_11_2) {
        coop_merge_achievements_mid(t->template_data, player_stats_json);
        coop_merge_stats_mid(t->template_data, player_stats_json, settings->coop_stat_merge);
    } else if (version >= MC_VERSION_1_12 && version <= MC_VERSION_1_12_2) {
        coop_merge_advancements_modern(t->template_data, player_adv_json);
        coop_merge_stats_mid(t->template_data, player_stats_json, settings->coop_stat_merge);
    } else if (version >= MC_VERSION_1_13) {
        coop_merge_advancements_modern(t->template_data, player_adv_json);
        coop_merge_stats_modern(t->template_data, player_stats_json, settings->coop_stat_merge, version);
    }

    coop_merge_multi_stage(t->template_data, player_adv_json, player_stats_json,
                           player_unlocks_json, version);

    cJSON_Delete(player_adv_json);
    cJSON_Delete(player_stats_json);
    cJSON_Delete(player_unlocks_json);

    // Finalize
    cJSON *settings_json = cJSON_from_file(get_settings_file_path());

    coop_finalize_advancements(t->template_data);
    coop_finalize_stats(t->template_data, settings_json);
    coop_finalize_multi_stage(t->template_data);

    tracker_update_custom_progress(t, settings_json, settings);

    {
        bool changed;
        int guard = 0;
        do {
            changed = tracker_update_custom_goal_linked_goals(t);
            changed |= tracker_update_stat_linked_goals(t);
            changed |= tracker_update_counter_goals(t);
        } while (changed && ++guard < 32);
    }

    tracker_calculate_overall_progress(t, version, settings);

    cJSON_Delete(settings_json);
}

void tracker_apply_coop_mods(Tracker *t, const AppSettings *settings,
                             const CoopCustomGoalModMsg *mods, int mod_count) {
    if (!t || !t->template_data || !settings || mod_count <= 0) return;

    TemplateData *td = t->template_data;
    bool any_applied = false;

    for (int m = 0; m < mod_count; m++) {
        const CoopCustomGoalModMsg *mod = &mods[m];

        // Try custom goals first (parent_root_name is empty for custom goals)
        if (mod->parent_root_name[0] == '\0') {
            // Check custom goals
            for (int i = 0; i < td->custom_goal_count; i++) {
                TrackableItem *item = td->custom_goals[i];
                if (!item || strcmp(item->root_name, mod->goal_root_name) != 0) continue;

                if (mod->action == COOP_MOD_TOGGLE) {
                    item->is_manually_completed = !item->is_manually_completed;
                    if (item->is_manually_completed) {
                        item->done = true;
                    } else {
                        item->done = (item->linked_goal_count > 0 &&
                                      check_linked_goals_satisfied(td, item->linked_goals,
                                                                   item->linked_goal_count, item->linked_goal_mode));
                    }
                    item->progress = item->done ? 1 : 0;
                    any_applied = true;
                } else if (mod->action == COOP_MOD_INCREMENT) {
                    item->progress++;
                    item->done = (item->goal > 0 && item->progress >= item->goal);
                    any_applied = true;
                } else if (mod->action == COOP_MOD_DECREMENT) {
                    item->progress--;
                    item->done = (item->goal > 0 && item->progress >= item->goal);
                    any_applied = true;
                }
                break;
            }

            // Check parent stat categories (toggle parent checkbox)
            for (int s = 0; s < td->stat_count; s++) {
                TrackableCategory *cat = td->stats[s];
                if (!cat || strcmp(cat->root_name, mod->goal_root_name) != 0) continue;

                if (mod->action == COOP_MOD_TOGGLE) {
                    cat->is_manually_completed = !cat->is_manually_completed;

                    // Recalculate children FIRST so completed_criteria_count is correct
                    for (int j = 0; j < cat->criteria_count; j++) {
                        TrackableItem *crit = cat->criteria[j];
                        bool crit_naturally_done = (crit->goal > 0 && crit->progress >= crit->goal);
                        crit->done = cat->is_manually_completed || crit->is_manually_completed || crit_naturally_done;
                    }
                    cat->completed_criteria_count = 0;
                    for (int k = 0; k < cat->criteria_count; k++) {
                        if (cat->criteria[k]->done) cat->completed_criteria_count++;
                    }

                    // Now calculate parent done based on updated children
                    bool all_children_done = (cat->criteria_count > 0 &&
                                              cat->completed_criteria_count >= cat->criteria_count);
                    cat->done = cat->is_manually_completed || all_children_done;
                    any_applied = true;
                }
                break;
            }
        } else {
            // Sub-stat checkbox toggle: find parent, then child
            for (int s = 0; s < td->stat_count; s++) {
                TrackableCategory *cat = td->stats[s];
                if (!cat || strcmp(cat->root_name, mod->parent_root_name) != 0) continue;

                for (int j = 0; j < cat->criteria_count; j++) {
                    TrackableItem *crit = cat->criteria[j];
                    if (!crit || strcmp(crit->root_name, mod->goal_root_name) != 0) continue;

                    if (mod->action == COOP_MOD_TOGGLE) {
                        crit->is_manually_completed = !crit->is_manually_completed;
                        bool crit_naturally_done = (crit->goal > 0 && crit->progress >= crit->goal);
                        crit->done = crit->is_manually_completed || crit_naturally_done;

                        cat->completed_criteria_count = 0;
                        for (int k = 0; k < cat->criteria_count; k++) {
                            if (cat->criteria[k]->done) cat->completed_criteria_count++;
                        }
                        bool all_children_done = (cat->criteria_count > 0 &&
                                                  cat->completed_criteria_count >= cat->criteria_count);
                        cat->done = cat->is_manually_completed || all_children_done;
                        any_applied = true;
                    }
                    break;
                }
                break;
            }
        }
    }

    if (any_applied) {
        SDL_SetAtomicInt(&g_suppress_settings_watch, 1);
        settings_save(settings, td, SAVE_CONTEXT_ALL);
    }
}

// UNUSED
void tracker_render(Tracker *t, const AppSettings *settings) {
    (void) t;
    (void) settings;
}

// END OF NON-STATIC FUNCTIONS ------------------------------------

// -------------------------------------------- TRACKER RENDERING START --------------------------------------------

// START OF STATIC FUNCTIONS ------------------------------------

// Computes the pixel offset to apply when rendering an element based on its anchor point.
// For TOP_LEFT (default), the offset is (0,0). For CENTER, it shifts by (-width/2, -height/2), etc.
// The element_width and element_height are in world coordinates (pre-zoom).
static ImVec2 get_anchor_offset(AnchorPoint anchor, float element_width, float element_height) {
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    switch (anchor) {
        case ANCHOR_TOP_LEFT: break;
        case ANCHOR_TOP_CENTER: offset_x = -element_width * 0.5f;
            break;
        case ANCHOR_TOP_RIGHT: offset_x = -element_width;
            break;
        case ANCHOR_CENTER_LEFT: offset_y = -element_height * 0.5f;
            break;
        case ANCHOR_CENTER: offset_x = -element_width * 0.5f;
            offset_y = -element_height * 0.5f;
            break;
        case ANCHOR_CENTER_RIGHT: offset_x = -element_width;
            offset_y = -element_height * 0.5f;
            break;
        case ANCHOR_BOTTOM_LEFT: offset_y = -element_height;
            break;
        case ANCHOR_BOTTOM_CENTER: offset_x = -element_width * 0.5f;
            offset_y = -element_height;
            break;
        case ANCHOR_BOTTOM_RIGHT: offset_x = -element_width;
            offset_y = -element_height;
            break;
    }

    return ImVec2(offset_x, offset_y);
}

// Initializes an unset ManualPos by reverse-engineering its world position from the registered screen rect.
static void init_unset_pos_from_screen(ManualPos *pos, float zoom_level, ImVec2 camera_offset) {
    if (pos->is_set) return;
    // Search the previous frame's registration list (complete), since the current frame's
    // list may be incomplete during rendering when items are registered incrementally.
    for (const auto &item: s_visual_layout_items_prev) {
        if (item.pos == pos) {
            pos->is_set = true;
            float world_top_left_x = (item.screen_pos.x - camera_offset.x) / zoom_level;
            float world_top_left_y = (item.screen_pos.y - camera_offset.y) / zoom_level;
            float element_w = item.size.x / zoom_level;
            float element_h = item.size.y / zoom_level;
            ImVec2 anchor_off = get_anchor_offset(pos->anchor, element_w, element_h);
            pos->x = roundf(world_top_left_x - anchor_off.x);
            pos->y = roundf(world_top_left_y - anchor_off.y);
            return;
        }
    }
    // Fallback if not found (shouldn't happen)
    pos->is_set = true;
    pos->x = 100.0f;
    pos->y = 100.0f;
}

// Draws a contrasting crosshair (black outline + white inner) at the given screen position.
static void draw_anchor_crosshair(ImDrawList *draw_list, ImVec2 pos, float size) {
    ImU32 outline_color = IM_COL32(0, 0, 0, 200);
    ImU32 inner_color = IM_COL32(255, 255, 255, 220);
    float half = size * 0.5f;

    // Horizontal line - black outline then white inner
    draw_list->AddLine(ImVec2(pos.x - half, pos.y), ImVec2(pos.x + half, pos.y), outline_color, 3.0f);
    draw_list->AddLine(ImVec2(pos.x - half, pos.y), ImVec2(pos.x + half, pos.y), inner_color, 1.0f);

    // Vertical line - black outline then white inner
    draw_list->AddLine(ImVec2(pos.x, pos.y - half), ImVec2(pos.x, pos.y + half), outline_color, 3.0f);
    draw_list->AddLine(ImVec2(pos.x, pos.y - half), ImVec2(pos.x, pos.y + half), inner_color, 1.0f);
}

// Computes the screen position of the anchor point within an element's bounding box.
// item_screen_pos is the top-left of the rendered element, hit_box_size is in screen pixels.
static ImVec2 get_anchor_screen_pos(AnchorPoint anchor, ImVec2 item_screen_pos, ImVec2 hit_box_size) {
    float ax = 0.0f, ay = 0.0f; // Fractional position within the bounding box
    switch (anchor) {
        case ANCHOR_TOP_LEFT: ax = 0.0f;
            ay = 0.0f;
            break;
        case ANCHOR_TOP_CENTER: ax = 0.5f;
            ay = 0.0f;
            break;
        case ANCHOR_TOP_RIGHT: ax = 1.0f;
            ay = 0.0f;
            break;
        case ANCHOR_CENTER_LEFT: ax = 0.0f;
            ay = 0.5f;
            break;
        case ANCHOR_CENTER: ax = 0.5f;
            ay = 0.5f;
            break;
        case ANCHOR_CENTER_RIGHT: ax = 1.0f;
            ay = 0.5f;
            break;
        case ANCHOR_BOTTOM_LEFT: ax = 0.0f;
            ay = 1.0f;
            break;
        case ANCHOR_BOTTOM_CENTER: ax = 0.5f;
            ay = 1.0f;
            break;
        case ANCHOR_BOTTOM_RIGHT: ax = 1.0f;
            ay = 1.0f;
            break;
    }
    return ImVec2(item_screen_pos.x + hit_box_size.x * ax, item_screen_pos.y + hit_box_size.y * ay);
}

// Helper function to handle Visual Layout Drag-and-Drop editing
static void handle_visual_layout_dragging(Tracker *t, const char *id, ImVec2 item_screen_pos, ImVec2 hit_box_size,
                                          ManualPos &target_pos, const char *goal_type,
                                          const char *display_name, const char *element_type,
                                          const char *root_name = nullptr,
                                          const char *parent_display_name = nullptr,
                                          const char *parent_root_name = nullptr) {
    if (!t->is_visual_layout_editing) return;

    ImGui::PushID(id);
    // Create an invisible button over the item so ImGui captures the mouse
    ImGui::SetCursorScreenPos(item_screen_pos);
    ImGui::InvisibleButton("##drag_handle", hit_box_size);

    bool is_dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    bool is_hovered = ImGui::IsItemHovered();
    bool is_just_clicked = ImGui::IsItemActivated();

    // Register this item for selection rectangle hit-testing
    s_visual_layout_items.push_back({item_screen_pos, hit_box_size, &target_pos});

    // Mark that an item was interacted with (prevents selection rectangle from starting)
    if (is_hovered || ImGui::IsItemActive()) {
        t->visual_item_interacted_this_frame = true;
    }

    // Handle click-to-select/deselect
    if (is_just_clicked) {
        ImGuiIO &click_io = ImGui::GetIO();
        bool ctrl_held = click_io.KeyCtrl; // Ctrl on Windows/Linux, Cmd on macOS
        bool is_selected = s_visual_selected_items.count(&target_pos) > 0;
        if (ctrl_held) {
            // Ctrl+Click: toggle this item in the selection
            if (is_selected) {
                s_visual_selected_items.erase(&target_pos);
            } else {
                s_visual_selected_items.insert(&target_pos);
            }
        } else if (!is_selected) {
            // Normal click on unselected item: clear selection, select only this one
            s_visual_selected_items.clear();
            s_visual_selected_items.insert(&target_pos);
        }
        // If already selected (without Ctrl), keep the selection (allows multi-drag)

        // Signal the editor to select this goal (without opening Layout Coordinates)
        if (parent_root_name && parent_root_name[0] != '\0') {
            strncpy(t->visual_drag_root_name, parent_root_name, sizeof(t->visual_drag_root_name) - 1);
            t->visual_drag_root_name[sizeof(t->visual_drag_root_name) - 1] = '\0';
            if (root_name && root_name[0] != '\0') {
                strncpy(t->visual_drag_child_root_name, root_name, sizeof(t->visual_drag_child_root_name) - 1);
                t->visual_drag_child_root_name[sizeof(t->visual_drag_child_root_name) - 1] = '\0';
            } else {
                t->visual_drag_child_root_name[0] = '\0';
            }
            if (strcmp(goal_type, "Sub-Stat") == 0) {
                strncpy(t->visual_drag_goal_type, "Stat", sizeof(t->visual_drag_goal_type) - 1);
            } else if (strcmp(goal_type, "Achievement") == 0) {
                strncpy(t->visual_drag_goal_type, "Achievement", sizeof(t->visual_drag_goal_type) - 1);
            } else {
                strncpy(t->visual_drag_goal_type, "Advancement", sizeof(t->visual_drag_goal_type) - 1);
            }
            t->visual_drag_goal_type[sizeof(t->visual_drag_goal_type) - 1] = '\0';
        } else if (root_name && root_name[0] != '\0') {
            strncpy(t->visual_drag_root_name, root_name, sizeof(t->visual_drag_root_name) - 1);
            t->visual_drag_root_name[sizeof(t->visual_drag_root_name) - 1] = '\0';
            strncpy(t->visual_drag_goal_type, goal_type, sizeof(t->visual_drag_goal_type) - 1);
            t->visual_drag_goal_type[sizeof(t->visual_drag_goal_type) - 1] = '\0';
            t->visual_drag_child_root_name[0] = '\0';
        }
        t->visual_layout_just_clicked = true;
    }

    if (is_dragging) {
        ImGuiIO &io = ImGui::GetIO();

        // Communicate the dragged goal's identity to the template editor
        // For criteria/sub-stats, select the parent goal instead
        if (parent_root_name && parent_root_name[0] != '\0') {
            strncpy(t->visual_drag_root_name, parent_root_name, sizeof(t->visual_drag_root_name) - 1);
            t->visual_drag_root_name[sizeof(t->visual_drag_root_name) - 1] = '\0';
            if (root_name && root_name[0] != '\0') {
                strncpy(t->visual_drag_child_root_name, root_name, sizeof(t->visual_drag_child_root_name) - 1);
                t->visual_drag_child_root_name[sizeof(t->visual_drag_child_root_name) - 1] = '\0';
            } else {
                t->visual_drag_child_root_name[0] = '\0';
            }
            if (strcmp(goal_type, "Sub-Stat") == 0) {
                strncpy(t->visual_drag_goal_type, "Stat", sizeof(t->visual_drag_goal_type) - 1);
            } else if (strcmp(goal_type, "Achievement") == 0) {
                strncpy(t->visual_drag_goal_type, "Achievement", sizeof(t->visual_drag_goal_type) - 1);
            } else {
                strncpy(t->visual_drag_goal_type, "Advancement", sizeof(t->visual_drag_goal_type) - 1);
            }
            t->visual_drag_goal_type[sizeof(t->visual_drag_goal_type) - 1] = '\0';
        } else if (root_name && root_name[0] != '\0') {
            strncpy(t->visual_drag_root_name, root_name, sizeof(t->visual_drag_root_name) - 1);
            t->visual_drag_root_name[sizeof(t->visual_drag_root_name) - 1] = '\0';
            strncpy(t->visual_drag_goal_type, goal_type, sizeof(t->visual_drag_goal_type) - 1);
            t->visual_drag_goal_type[sizeof(t->visual_drag_goal_type) - 1] = '\0';
            t->visual_drag_child_root_name[0] = '\0';
        }

        // If this is the very first time dragging it, reverse-engineer its current
        // procedural screen position back into World X/Y coordinates!
        if (!target_pos.is_set) {
            target_pos.is_set = true;
            float world_top_left_x = (item_screen_pos.x - t->camera_offset.x) / t->zoom_level;
            float world_top_left_y = (item_screen_pos.y - t->camera_offset.y) / t->zoom_level;
            float element_w = hit_box_size.x / t->zoom_level;
            float element_h = hit_box_size.y / t->zoom_level;
            ImVec2 anchor_off = get_anchor_offset(target_pos.anchor, element_w, element_h);
            target_pos.x = roundf(world_top_left_x - anchor_off.x);
            target_pos.y = roundf(world_top_left_y - anchor_off.y);
        }

        float dx = io.MouseDelta.x / t->zoom_level;
        float dy = io.MouseDelta.y / t->zoom_level;

        // Apply drag delta to this item
        target_pos.x = fminf(fmaxf(roundf(target_pos.x + dx), -MANUAL_POS_MAX), MANUAL_POS_MAX);
        target_pos.y = fminf(fmaxf(roundf(target_pos.y + dy), -MANUAL_POS_MAX), MANUAL_POS_MAX);

        // Multi-drag: also move all OTHER selected items by the same delta
        if (s_visual_selected_items.count(&target_pos) > 0) {
            for (ManualPos *sel_pos: s_visual_selected_items) {
                if (sel_pos == &target_pos) continue;
                init_unset_pos_from_screen(sel_pos, t->zoom_level, t->camera_offset);
                sel_pos->x = fminf(fmaxf(roundf(sel_pos->x + dx), -MANUAL_POS_MAX), MANUAL_POS_MAX);
                sel_pos->y = fminf(fmaxf(roundf(sel_pos->y + dy), -MANUAL_POS_MAX), MANUAL_POS_MAX);
            }
        }

        // Signal the Editor to sync this frame!
        t->visual_layout_just_dragged = true;
        SDL_SetAtomicInt(&g_templates_changed, 1);
    }

    // Draw anchor crosshair when dragging or hovering
    if (is_dragging || is_hovered) {
        ImDrawList *draw_list = ImGui::GetForegroundDrawList();
        ImVec2 anchor_screen = get_anchor_screen_pos(target_pos.anchor, item_screen_pos, hit_box_size);
        float crosshair_size = 12.0f;
        draw_anchor_crosshair(draw_list, anchor_screen, crosshair_size);
    }

    // Show tooltip on hover or while dragging
    if (is_hovered || is_dragging) {
        if (is_hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        char tooltip[512];
        if (is_dragging) {
            if (parent_display_name) {
                snprintf(tooltip, sizeof(tooltip),
                         "%s: \"%s\" - %s\nPart of \"%s\"\n\n"
                         "X: %.0f   Y: %.0f",
                         goal_type, display_name, element_type, parent_display_name,
                         target_pos.x, target_pos.y);
            } else {
                snprintf(tooltip, sizeof(tooltip),
                         "%s: \"%s\" - %s\n\n"
                         "X: %.0f   Y: %.0f",
                         goal_type, display_name, element_type,
                         target_pos.x, target_pos.y);
            }
        } else {
            float display_x, display_y;
            if (target_pos.is_set) {
                display_x = target_pos.x;
                display_y = target_pos.y;
            } else {
                float world_top_left_x = (item_screen_pos.x - t->camera_offset.x) / t->zoom_level;
                float world_top_left_y = (item_screen_pos.y - t->camera_offset.y) / t->zoom_level;
                float element_w = hit_box_size.x / t->zoom_level;
                float element_h = hit_box_size.y / t->zoom_level;
                ImVec2 anchor_off = get_anchor_offset(target_pos.anchor, element_w, element_h);
                display_x = roundf(world_top_left_x - anchor_off.x);
                display_y = roundf(world_top_left_y - anchor_off.y);
            }
            if (parent_display_name) {
                snprintf(tooltip, sizeof(tooltip),
                         "%s: \"%s\" - %s\nPart of \"%s\"\n\n"
                         "X: %.0f   Y: %.0f\n"
                         "Drag to reposition.",
                         goal_type, display_name, element_type, parent_display_name,
                         display_x, display_y);
            } else {
                snprintf(tooltip, sizeof(tooltip),
                         "%s: \"%s\" - %s\n\n"
                         "X: %.0f   Y: %.0f\n"
                         "Drag to reposition.",
                         goal_type, display_name, element_type,
                         display_x, display_y);
            }
        }
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::PopID();
}

/**
 * @brief Helper to draw a separator line with a title and completion counters for a new section.
 *
 * @param t The tracker instance.
 * @param settings The app settings.
 * @param current_y The current y position in the world.
 * @param title The title of the section.
 * @param text_color The text color for the title.
 * @param completed_count Number of completed main items in this section (respecting hiding mode).
 * @param total_visible_count Total number of main items considered for this section (respecting hiding mode).
 * @param completed_sub_count Number of completed sub-items (criteria/sub-stats/stages) (use -1 if not applicable).
 * @param total_visible_sub_count Total number of sub-items considered (use -1 if not applicable).
 */
static void render_section_separator(Tracker *t, const AppSettings *settings, float &current_y, const char *title,
                                     ImU32 text_color, int completed_count, int total_visible_count,
                                     int completed_sub_count, int total_visible_sub_count) {
    ImGuiIO &io = ImGui::GetIO();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    float zoom = t->zoom_level;
    float scale_factor;

    current_y += 12.0f; // Padding before the separator

    // Construct counter string based on hiding mode
    char counter_str[128] = "";
    if (total_visible_count > 0) {
        // Only show counters if there are items to count
        if (settings->goal_hiding_mode == HIDE_ALL_COMPLETED) {
            // --- HIDE_ALL_COMPLETED Mode: Show only totals ---
            if (completed_sub_count != -1 && total_visible_sub_count > 0) {
                // Sections with sub-items (Advancements/Stats with criteria, Multi-Stage)
                // Format: "Title (Total Main | Total Sub)"
                snprintf(counter_str, sizeof(counter_str), "  (%d  -  %d)",
                         total_visible_count, total_visible_sub_count);
            } else {
                // Sections without sub-items (Unlocks, Custom Goals, simple Adv/Stats)
                // Format: "Title (Total Main)"
                snprintf(counter_str, sizeof(counter_str), "  (%d)",
                         total_visible_count);
            }
        } else {
            // --- HIDE_ONLY_TEMPLATE_HIDDEN or SHOW_ALL Mode: Show completed/total ---
            if (completed_sub_count != -1 && total_visible_sub_count > 0) {
                // Sections with sub-items
                // Format: "Title (Completed Main / Total Main | Completed Sub / Total Sub)"
                snprintf(counter_str, sizeof(counter_str), "  (%d/%d  -  %d/%d)",
                         completed_count, total_visible_count,
                         completed_sub_count, total_visible_sub_count);
            } else {
                // Sections without sub-items
                // Format: "Title (Completed Main / Total Main)"
                snprintf(counter_str, sizeof(counter_str), "  (%d/%d)",
                         completed_count, total_visible_count);
            }
        }
    }


    // Combine title and counter string
    char full_title[512];
    snprintf(full_title, sizeof(full_title), "%s%s", title, counter_str);

    // Use the tracker font size from settings for the separator title
    float main_text_size = settings->tracker_font_size;

    // Measure the combined text
    SET_FONT_SCALE(main_text_size, t->tracker_font->LegacySize);
    ImVec2 full_text_size = ImGui::CalcTextSize(full_title);
    RESET_FONT_SCALE();

    float screen_width_in_world = io.DisplaySize.x / zoom;

    // Center text in screen space (ignore camera_offset.x so it doesn't shift with the world)
    float text_pos_x_in_world = (screen_width_in_world - full_text_size.x) / 2.0f;
    ImVec2 final_text_pos = ImVec2(text_pos_x_in_world * zoom,
                                   current_y * zoom + t->camera_offset.y);

    draw_list->AddText(nullptr, main_text_size * zoom, final_text_pos, text_color, full_title);

    // Draw a shorter, centered line (40% of the visible width)
    float line_width_in_world = screen_width_in_world * TRACKER_SEPARATOR_LINE_WIDTH;
    float line_start_x_in_world = (screen_width_in_world - line_width_in_world) / 2.0f;
    float line_end_x_in_world = line_start_x_in_world + line_width_in_world;

    // Calculate line Y position based on the dynamic text size
    float line_y_offset = main_text_size + 14.0f; // TODO: Adjust vertical spacing here, 30 total rn
    ImVec2 line_start = ImVec2(line_start_x_in_world * zoom,
                               (current_y + line_y_offset) * zoom + t->camera_offset.y);
    ImVec2 line_end = ImVec2(line_end_x_in_world * zoom,
                             (current_y + line_y_offset) * zoom + t->camera_offset.y);

    draw_list->AddLine(line_start, line_end,
                       IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                ADVANCELY_FADED_ALPHA), 1.0f * zoom);

    // Adjust padding after separator based on dynamic text size
    current_y += line_y_offset + 20.0f; // TODO: Adjust vertical spacing here, 50 total
}

// Helper to calculate the global safe starting X-coordinate for unplaced auto-layout items
// Shifting them to the right of all manually placed items that have coordinates within the template
// Helper to calculate the global safe starting X-coordinate for unplaced auto-layout items
static float get_global_safe_x(Tracker *t) {
    if (!t || !t->template_data) return 50.0f;

    float max_x = 0.0f;

    auto check_pos = [&](const ManualPos &pos, float assumed_width) {
        if (pos.is_set) {
            float right_edge = pos.x + assumed_width;
            if (right_edge > max_x) max_x = right_edge;
        }
    };

    auto check_item = [&](TrackableItem *item) {
        if (!item) return;
        check_pos(item->icon_pos, 120.0f);
        check_pos(item->text_pos, 180.0f);
        check_pos(item->progress_pos, 150.0f); // Custom goal progress, obv. not unlocks
    };

    TemplateData *td = t->template_data;
    for (int i = 0; i < td->advancement_count; i++) {
        if (td->advancements[i]) {
            TrackableCategory *cat = td->advancements[i];
            // If parent is placed, assume a much wider width if it has children!
            float baseline_width = (cat->criteria_count > 0) ? 300.0f : 120.0f;

            check_pos(cat->icon_pos, baseline_width);
            check_pos(cat->text_pos, 180.0f);
            check_pos(cat->progress_pos, 150.0f);
            for (int j = 0; j < cat->criteria_count; j++) {
                check_item(cat->criteria[j]);
            }
        }
    }
    for (int i = 0; i < td->stat_count; i++) {
        if (td->stats[i]) {
            TrackableCategory *cat = td->stats[i];
            float baseline_width = (cat->criteria_count > 0 && !cat->is_single_stat_category) ? 300.0f : 120.0f;

            check_pos(cat->icon_pos, baseline_width);
            check_pos(cat->text_pos, 180.0f);
            check_pos(cat->progress_pos, 150.0f);
            for (int j = 0; j < cat->criteria_count; j++) {
                check_item(cat->criteria[j]);
            }
        }
    }
    for (int i = 0; i < td->unlock_count; i++) check_item(td->unlocks[i]);
    for (int i = 0; i < td->custom_goal_count; i++) check_item(td->custom_goals[i]);
    for (int i = 0; i < td->multi_stage_goal_count; i++) {
        if (td->multi_stage_goals[i]) {
            check_pos(td->multi_stage_goals[i]->icon_pos, 120.0f);
            check_pos(td->multi_stage_goals[i]->text_pos, 180.0f);
            check_pos(td->multi_stage_goals[i]->progress_pos, 150.0f);
        }
    }
    for (int i = 0; i < td->decoration_count; i++) {
        if (td->decorations[i]) {
            DecorationElement *deco = td->decorations[i];
            if (deco->type == DECORATION_TEXT_HEADER) {
                check_pos(deco->pos, 200.0f);
            } else if (deco->type == DECORATION_LINE) {
                float line_margin = deco->thickness * 0.5f;
                check_pos(deco->pos, line_margin);
                check_pos(deco->pos2, line_margin);
            } else if (deco->type == DECORATION_ARROW) {
                float arrow_margin = deco->thickness * 0.5f;
                check_pos(deco->pos, arrow_margin);
                check_pos(deco->pos2, arrow_margin);
                for (int b = 0; b < deco->bend_count; b++) {
                    check_pos(deco->bends[b], arrow_margin);
                }
            }
        }
    }

    if (max_x > 0.0f) {
        return max_x + 50.0f; // Return absolute furthest point + 50px padding
    }
    return 50.0f;
}

/**
 * @brief Renders a section of items that are TrackableCategories (e.g., Advancements, Recipes, Stats).
 * This function handles the uniform grid layout and the two-pass rendering for simple vs. complex items.
 * It distinguishes between advancements and stats with the bool flag and manages all the checkbox logic,
 * communicating with the settings.json file. It also calculates and displays completion counters.
 *
 * Implements LOD (Level of Detail) to hide text and simplify icons when zoomed out.
 * @param t The tracker instance.
 * @param settings The app settings.
 * @param current_y The current y position in the world.
 * @param categories The array of TrackableCategory pointers.
 * @param count The number of TrackableCategory pointers in the array.
 * @param section_title The title of the section.
 * @param is_stat_section True if the section is for stats, false if it's for advancements.
 * @param version The game version (MC_Version).
 */
static void render_trackable_category_section(Tracker *t, const AppSettings *settings, float &current_y,
                                              TrackableCategory **categories, int count, const char *section_title,
                                              bool is_stat_section, MC_Version version) {
    // --- LOD THRESHOLDS ---
    // 1. Zoom < 0.60: Hide Sub-Item Text & Progress Text
    const float LOD_TEXT_SUB_THRESHOLD = settings->lod_text_sub_threshold;
    // 2. Zoom < 0.40: Hide Main Item Text, Parent Checkboxes & Sub-Item Checkboxes
    const float LOD_TEXT_MAIN_THRESHOLD = settings->lod_text_main_threshold;
    // 3. Zoom < 0.25: Simplify Sub-Item Icons to Squares
    const float LOD_ICON_DETAIL_THRESHOLD = settings->lod_icon_detail_threshold;


    // --- Pre-computation and Filtering for Counters ---
    int total_visible_count = 0;
    int completed_count = 0;
    int total_visible_sub_count = 0;
    int completed_sub_count = 0;
    bool section_has_sub_items = false; // Flag if any category in this section has criteria/sub-stats

    for (int i = 0; i < count; ++i) {
        TrackableCategory *cat = categories[i];
        if (!cat) continue;

        // Skip hidden legacy stats entirely from counting
        if (is_stat_section && version <= MC_VERSION_1_6_4 && cat->criteria_count == 1 && cat->criteria[0]->goal == 0) {
            continue;
        }

        // Determine completion status for counting purposes
        bool is_category_considered_complete = false;
        if (is_stat_section) {
            is_category_considered_complete = cat->done;
        } else {
            is_category_considered_complete = (cat->criteria_count > 0) ? cat->all_template_criteria_met : cat->done;
        }

        // Determine if parent should be hidden based *only* on hiding mode + template hidden status
        bool should_hide_parent_based_on_mode = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_parent_based_on_mode =
                        (!settings->use_manual_layout && cat->is_hidden) || is_category_considered_complete;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_parent_based_on_mode = !settings->use_manual_layout && cat->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_parent_based_on_mode = false;
                break;
        }

        // Skip entire category if hidden by mode
        if (should_hide_parent_based_on_mode) continue;

        // Apply Search Filter for counting
        bool parent_matches_search = category_matches_search(cat, t->search_buffer);
        bool parent_is_linked = s_linked_top.count(cat->root_name) > 0;
        bool any_visible_child_matches_search = false;
        bool any_child_is_linked = false;

        if (cat->criteria_count > 0 && !cat->is_single_stat_category) {
            // Only check children for complex types
            section_has_sub_items = true;
            for (int j = 0; j < cat->criteria_count; ++j) {
                TrackableItem *crit = cat->criteria[j];
                if (!crit) continue;

                // Determine if child should be hidden based *only* on hiding mode + template hidden status
                bool should_hide_child_based_on_mode = false;
                switch (settings->goal_hiding_mode) {
                    case HIDE_ALL_COMPLETED:
                        should_hide_child_based_on_mode =
                                (!settings->use_manual_layout && crit->is_hidden) || crit->done;
                        break;
                    case HIDE_ONLY_TEMPLATE_HIDDEN:
                        should_hide_child_based_on_mode = !settings->use_manual_layout && crit->is_hidden;
                        break;
                    case SHOW_ALL:
                        should_hide_child_based_on_mode = false;
                        break;
                }

                if (should_hide_child_based_on_mode) continue; // Skip hidden child

                // Check if this child is linked via counter/header
                std::string link_key = std::string(cat->root_name) + "\t" + crit->root_name;
                bool child_linked = s_linked_sub.count(link_key) > 0;
                if (child_linked) any_child_is_linked = true;

                // Now check search filter for this visible child
                if (item_matches_search(crit, t->search_buffer)) {
                    any_visible_child_matches_search = true;
                    // Count this child towards sub-totals if it matches search
                    total_visible_sub_count++;
                    if (crit->done) {
                        completed_sub_count++;
                    }
                } else if (parent_matches_search) {
                    // If parent matches, still count this visible (but non-matching) child towards sub-totals
                    total_visible_sub_count++;
                    if (crit->done) {
                        completed_sub_count++;
                    }
                } else if (child_linked) {
                    // Child is linked via counter/header search — count it
                    total_visible_sub_count++;
                    if (crit->done) {
                        completed_sub_count++;
                    }
                }
            }
        }

        // --- Count Main Item ---
        // Count if parent matches OR at least one visible child matches OR linked
        if (parent_matches_search || parent_is_linked || any_visible_child_matches_search || any_child_is_linked) {
            total_visible_count++;
            if (is_category_considered_complete) {
                completed_count++;
            }
        }
    }
    // --- End of Counter Calculation ---


    // --- Section Rendering ---

    // Check if *anything* should be rendered based on search + hiding (different from counting!)
    bool section_has_renderable_content = false;
    for (int i = 0; i < count; ++i) {
        TrackableCategory *cat = categories[i];
        if (!cat) continue;

        // Skip hidden legacy stats for rendering too
        if (is_stat_section && version <= MC_VERSION_1_6_4 && cat->criteria_count == 1 && cat->criteria[0]->goal == 0) {
            continue;
        }

        bool is_considered_complete_render = is_stat_section
                                                 ? cat->done
                                                 : ((cat->criteria_count > 0 && cat->all_template_criteria_met) || (
                                                        cat->criteria_count == 0 && cat->done));

        bool should_hide_parent_render = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_parent_render = (!settings->use_manual_layout && cat->is_hidden) ||
                                            is_considered_complete_render;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_parent_render = !settings->use_manual_layout && cat->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_parent_render = false;
                break;
        }

        if (should_hide_parent_render) continue; // Skip if parent hidden based on mode

        // Check search filter
        bool parent_matches = category_matches_search(cat, t->search_buffer);
        bool parent_is_linked = s_linked_top.count(cat->root_name) > 0;
        bool child_matches_render = false;
        bool child_is_linked_render = false;
        if (!parent_matches) {
            for (int j = 0; j < cat->criteria_count; j++) {
                TrackableItem *crit = cat->criteria[j];
                if (!crit) continue;

                bool should_hide_crit_render = false;
                switch (settings->goal_hiding_mode) {
                    case HIDE_ALL_COMPLETED: should_hide_crit_render =
                                             (!settings->use_manual_layout && crit->is_hidden) || crit->done;
                        break;
                    case HIDE_ONLY_TEMPLATE_HIDDEN:
                        should_hide_crit_render = !settings->use_manual_layout && crit->is_hidden;
                        break;
                    case SHOW_ALL: should_hide_crit_render = false;
                        break;
                }

                if (should_hide_crit_render) continue;

                if (item_matches_search(crit, t->search_buffer)) {
                    child_matches_render = true;
                    break;
                }
                std::string link_key = std::string(cat->root_name) + "\t" + crit->root_name;
                if (s_linked_sub.count(link_key)) {
                    child_is_linked_render = true;
                }
            }
        }

        if (parent_matches || parent_is_linked || child_matches_render || child_is_linked_render) {
            section_has_renderable_content = true;
            break; // Found at least one item to render, section is visible
        }
    }

    if (!section_has_renderable_content) return; // Hide section if no items match filters


    ImGuiIO &io = ImGui::GetIO();

    // Use the locked width if layout is locked
    float wrapping_width = t->layout_locked ? t->locked_layout_width : (io.DisplaySize.x / t->zoom_level);


    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 text_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                settings->text_color.a);
    ImU32 text_color_faded = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                      ADVANCELY_FADED_ALPHA);
    ImU32 icon_tint_faded = IM_COL32(255, 255, 255, ADVANCELY_FADED_ALPHA);
    float scale_factor; // Declare scale_factor here

    // Define checkbox colors from settings
    ImU32 checkmark_color = text_color;
    ImU32 checkbox_fill_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                         ADVANCELY_FADED_ALPHA);
    ImU32 checkbox_hover_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                          (int)fminf(255.0f, ADVANCELY_FADED_ALPHA + 60));


    // Call the modified separator function with calculated counts
    if (!settings->use_manual_layout) {
        // Only if not in manual mode
        render_section_separator(t, settings, current_y, section_title, text_color,
                                 completed_count, total_visible_count,
                                 section_has_sub_items ? completed_sub_count : -1, // Pass -1 if no sub-items
                                 section_has_sub_items ? total_visible_sub_count : -1);
    }


    // --- Calculate Uniform Item Width (based on items that will be rendered) ---
    // IMPORTANT: Width calculation logic must remain consistent regardless of LOD.
    // It calculates based on "what would be shown" to prevent layout jumps.
    float uniform_item_width = 0.0f;
    const float horizontal_spacing = 8.0f; // Define the default spacing

    // Check if custom width is enabled for THIS section
    TrackerSection section_id = is_stat_section
                                    ? SECTION_STATS
                                    : (strcmp(section_title, "Recipes") == 0 ? SECTION_RECIPES : SECTION_ADVANCEMENTS);
    if (settings->tracker_section_custom_width_enabled[section_id]) {
        // Use fixed width from settings
        uniform_item_width = settings->tracker_section_custom_item_width[section_id];
        // Ensure it's at least the icon width
        if (uniform_item_width < 96.0f) uniform_item_width = 96.0f;
    } else {
        for (int i = 0; i < count; i++) {
            TrackableCategory *cat = categories[i];

            if (!cat) continue;

            // Skip hidden legacy stats
            if (is_stat_section && version <= MC_VERSION_1_6_4 && cat->criteria_count == 1 && cat->criteria[0]->goal ==
                0) {
                continue;
            }

            // Parent Hiding Logic (for rendering visibility)
            bool is_considered_complete_render = is_stat_section
                                                     ? cat->done
                                                     : ((cat->criteria_count > 0 && cat->all_template_criteria_met) || (
                                                            cat->criteria_count == 0 && cat->done));

            bool parent_should_hide_render = false;
            switch (settings->goal_hiding_mode) {
                case HIDE_ALL_COMPLETED:
                    parent_should_hide_render = (!settings->use_manual_layout && cat->is_hidden) ||
                                                is_considered_complete_render;
                    break;
                case HIDE_ONLY_TEMPLATE_HIDDEN:
                    parent_should_hide_render = !settings->use_manual_layout && cat->is_hidden;
                    break;
                case SHOW_ALL:
                    parent_should_hide_render = false;
                    break;
            }
            if (parent_should_hide_render) continue;

            // Search Filter (for rendering visibility)
            bool parent_matches = category_matches_search(cat, t->search_buffer);
            bool parent_is_linked = s_linked_top.count(cat->root_name) > 0;
            bool child_matches_render = false;
            bool child_is_linked_width = false;
            if (!parent_matches) {
                for (int j = 0; j < cat->criteria_count; j++) {
                    TrackableItem *crit = cat->criteria[j];
                    if (!crit) continue;

                    // Apply the same hiding logic here as in the rendering pass
                    bool crit_should_hide_render = false;
                    switch (settings->goal_hiding_mode) {
                        case HIDE_ALL_COMPLETED:
                            crit_should_hide_render = (!settings->use_manual_layout && crit->is_hidden) || crit->done;
                            break;
                        case HIDE_ONLY_TEMPLATE_HIDDEN:
                            crit_should_hide_render = !settings->use_manual_layout && crit->is_hidden;
                            break;
                        case SHOW_ALL:
                            crit_should_hide_render = false;
                            break;
                    }

                    if (crit_should_hide_render) continue;

                    // Now, check both hiding status AND the search term
                    if (item_matches_search(crit, t->search_buffer)) {
                        child_matches_render = true;
                        break;
                    }
                    std::string link_key = std::string(cat->root_name) + "\t" + crit->root_name;
                    if (s_linked_sub.count(link_key)) {
                        child_is_linked_width = true;
                    }
                }
            }
            if (!parent_matches && !parent_is_linked && !child_matches_render && !child_is_linked_width)
                continue; // Skip if nothing matches

            // --- Calculate width needed by this item ---
            float required_width = 0.0f;

            // Determine if this is a simple stat category
            bool is_simple_stat_category = is_stat_section && cat->is_single_stat_category;

            // --- Calculate width needed for PARENT text (using main font size) ---
            float parent_text_required_width = 0.0f;
            SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize);
            parent_text_required_width = fmax(parent_text_required_width, ImGui::CalcTextSize(cat->display_name).x);
            RESET_FONT_SCALE();

            // Calculate width for the progress text below the parent
            char progress_text_width_calc[32] = "";
            if (is_simple_stat_category && cat->criteria_count == 1) {
                // Simple stat progress text calculation
                TrackableItem *crit = cat->criteria[0];
                if (crit->goal > 0) {
                    snprintf(progress_text_width_calc, sizeof(progress_text_width_calc), "(%d / %d)", crit->progress,
                             crit->goal);
                } else if (crit->goal == -1) {
                    snprintf(progress_text_width_calc, sizeof(progress_text_width_calc), "(%d)", crit->progress);
                }
            } else if (!is_simple_stat_category) {
                // Complex stat or Advancement
                if (cat->criteria_count > 0) {
                    snprintf(progress_text_width_calc, sizeof(progress_text_width_calc), "(%d / %d)",
                             cat->completed_criteria_count, cat->criteria_count);
                }
            }
            // Scale for progress text width calculation
            SET_FONT_SCALE(settings->tracker_sub_font_size, t->tracker_font->LegacySize);
            float progress_text_actual_width = ImGui::CalcTextSize(progress_text_width_calc).x;
            RESET_FONT_SCALE();
            parent_text_required_width = fmaxf(parent_text_required_width, progress_text_actual_width);
            // Ensure width accommodates progress text

            float children_max_required_width = 0.0f;

            // Check children widths ONLY for complex items
            if (!is_simple_stat_category && cat->criteria_count > 0) {
                for (int j = 0; j < cat->criteria_count; j++) {
                    TrackableItem *crit = cat->criteria[j];
                    // Child Hiding Logic (for width calculation)
                    bool crit_should_hide_width = false;
                    switch (settings->goal_hiding_mode) {
                        case HIDE_ALL_COMPLETED:
                            crit_should_hide_width = (!settings->use_manual_layout && crit->is_hidden) || crit->done;
                            break;
                        case HIDE_ONLY_TEMPLATE_HIDDEN:
                            crit_should_hide_width = !settings->use_manual_layout && crit->is_hidden;
                            break;
                        case SHOW_ALL:
                            crit_should_hide_width = false;
                            break;
                    }

                    // Also check if this specific child matches search if parent didn't
                    std::string crit_link_key = std::string(cat->root_name) + "\t" + crit->root_name;
                    bool crit_matches_search = parent_matches
                                               || (child_matches_render && item_matches_search(crit, t->search_buffer))
                                               || s_linked_sub.count(crit_link_key);

                    if (crit && !crit_should_hide_width && crit_matches_search) {
                        // Use sub_font_size for calculations here
                        float sub_font_size = settings->tracker_sub_font_size;
                        ImGui::PushFont(t->tracker_font); // Assume tracker font needed for CalcTextSize
                        ImGui::SetWindowFontScale(sub_font_size / t->tracker_font->LegacySize); // Scale to sub-size

                        char crit_progress_text_width[32] = "";
                        if (is_stat_section) {
                            // Only calculate progress width for sub-stats
                            if (crit->goal > 0) {
                                snprintf(crit_progress_text_width, sizeof(crit_progress_text_width), "(%d / %d)",
                                         crit->progress,
                                         crit->goal);
                            } else if (crit->goal == -1) {
                                snprintf(crit_progress_text_width, sizeof(crit_progress_text_width), "(%d)",
                                         crit->progress);
                            }
                        }
                        float crit_text_width = ImGui::CalcTextSize(crit->display_name).x;
                        float crit_progress_width = ImGui::CalcTextSize(crit_progress_text_width).x;
                        float checkbox_width = (is_stat_section && cat->criteria_count > 1) ? (20 + 4) : 0;
                        // Checkbox + padding
                        float total_crit_width = 32 + 4 + checkbox_width + crit_text_width + (
                                                     crit_progress_width > 0 ? 4 + crit_progress_width : 0);
                        // Icon + padding + checkbox + text + progress

                        // --- Pop font scaling ---
                        ImGui::SetWindowFontScale(1.0f);
                        ImGui::PopFont();

                        children_max_required_width = fmaxf(children_max_required_width, total_crit_width);
                    }
                }
            }
            // Determine final required width
            // It's the maximum of the parent's text needs OR the children's needs
            required_width = fmaxf(parent_text_required_width, children_max_required_width);

            // Ensure minimum width accommodates the 96px background for all items
            uniform_item_width = fmaxf(uniform_item_width, fmaxf(96.0f, required_width));
            // Ensure minimum width for icon bg
        }
        // Add default spacing ONLY in dynamic mode +8 pixels
        uniform_item_width += horizontal_spacing;
    }
    // --- End of Width Calculation ---

    // --- GLOBAL HYBRID SHIFT ---
    float padding = 50.0f;
    if (settings->use_manual_layout) {
        padding = get_global_safe_x(t);

        // Prevent auto-layout items from squeezing into a single column!
        // We force the wrapping width to allow at least 3 items to fit side-by-side
        float min_wrapping_width = padding + (uniform_item_width * 3.0f);
        if (wrapping_width < min_wrapping_width) {
            wrapping_width = min_wrapping_width;
        }
    }
    float current_x = padding, row_max_height = 0.0f;

    // Adjust vertical spacing -> need to do this for all render_*_section functions
    const float vertical_spacing = settings->tracker_vertical_spacing; // Changed from 16.0f

    // Pre-calculate line heights once per frame (Optimization)
    // Assuming single-line text, the height is simply the font size.
    const float main_text_line_height = settings->tracker_font_size;
    const float sub_text_line_height = settings->tracker_sub_font_size;

    // complex_pass = false -> Render all advancements/stats with no criteria or sub-stats (simple items)
    // complex_pass = true -> Render all advancements/stats with criteria or sub-stats (complex items)
    auto render_pass = [&](bool complex_pass) {
        for (int i = 0; i < count; i++) {
            TrackableCategory *cat = categories[i];

            if (!cat) continue;

            // Skip hidden legacy stats
            if (is_stat_section && version <= MC_VERSION_1_6_4 && cat->criteria_count == 1 && cat->criteria[0]->goal ==
                0) {
                continue;
            }

            bool is_complex = is_stat_section ? !cat->is_single_stat_category : (cat->criteria_count > 0);
            if (is_complex != complex_pass) continue;

            // --- Parent Hiding Logic (for rendering) ---
            bool is_considered_complete_render = is_stat_section
                                                     ? cat->done
                                                     : ((cat->criteria_count > 0 && cat->all_template_criteria_met) || (
                                                            cat->criteria_count == 0 && cat->done));


            bool should_hide_parent_render = false;
            switch (settings->goal_hiding_mode) {
                case HIDE_ALL_COMPLETED:
                    should_hide_parent_render = (!settings->use_manual_layout && cat->is_hidden) ||
                                                is_considered_complete_render;
                    break;
                case HIDE_ONLY_TEMPLATE_HIDDEN:
                    should_hide_parent_render = !settings->use_manual_layout && cat->is_hidden;
                    break;
                case SHOW_ALL:
                    should_hide_parent_render = false;
                    break;
            }
            if (should_hide_parent_render) continue;

            // --- Search Filtering (for rendering) ---
            bool parent_matches = category_matches_search(cat, t->search_buffer);
            bool parent_is_linked = s_linked_top.count(cat->root_name) > 0;
            std::vector<TrackableItem *> matching_children; // Children that match search or are linked
            bool child_matches_search = false; // Flag if any child matches search
            bool any_child_linked = false;
            if (!parent_matches) {
                for (int j = 0; j < cat->criteria_count; j++) {
                    TrackableItem *crit = cat->criteria[j];
                    if (!crit) continue;

                    bool should_hide_crit_render = false;
                    switch (settings->goal_hiding_mode) {
                        case HIDE_ALL_COMPLETED:
                            should_hide_crit_render = (!settings->use_manual_layout && crit->is_hidden) || crit->done;
                            break;
                        case HIDE_ONLY_TEMPLATE_HIDDEN:
                            should_hide_crit_render = !settings->use_manual_layout && crit->is_hidden;
                            break;
                        case SHOW_ALL: should_hide_crit_render = false;
                            break;
                    }

                    if (should_hide_crit_render) continue;

                    bool crit_search = item_matches_search(crit, t->search_buffer);
                    std::string crit_link_key = std::string(cat->root_name) + "\t" + crit->root_name;
                    bool crit_linked = s_linked_sub.count(crit_link_key) > 0;

                    if (crit_search || crit_linked) {
                        matching_children.push_back(crit);
                        if (crit_search) child_matches_search = true;
                        if (crit_linked) any_child_linked = true;
                    }
                }
            }
            if (!parent_matches && !parent_is_linked && !child_matches_search && !any_child_linked) continue;
            // Skip if neither parent nor any child matches search or is linked

            // --- PREPARE CHILDREN LIST ---
            std::vector<TrackableItem *> children_to_render;
            if (parent_matches) {
                // Text search matches parent → show ALL visible children (existing behavior)
                for (int j = 0; j < cat->criteria_count; ++j) {
                    TrackableItem *crit = cat->criteria[j];
                    if (!crit) continue;
                    bool should_hide_crit_render = false;
                    switch (settings->goal_hiding_mode) {
                        case HIDE_ALL_COMPLETED:
                            should_hide_crit_render = (!settings->use_manual_layout && crit->is_hidden) || crit->done;
                            break;
                        case HIDE_ONLY_TEMPLATE_HIDDEN:
                            should_hide_crit_render = !settings->use_manual_layout && crit->is_hidden;
                            break;
                        case SHOW_ALL: should_hide_crit_render = false;
                            break;
                    }
                    if (!should_hide_crit_render) children_to_render.push_back(crit);
                }
            } else {
                // Only matching/linked children (parent_is_linked adds NO children)
                children_to_render = matching_children;
            }

            // Count how many children are manually placed vs unplaced
            int unplaced_criteria_count = 0;
            bool has_manual_child = false;
            for (TrackableItem *crit: children_to_render) {
                // Disable scroll list if ANY part of the item is dragged
                if (settings->use_manual_layout && (crit->icon_pos.is_set || crit->text_pos.is_set)) {
                    has_manual_child = true;
                }

                // ONLY remove the item from the vertical list height if the ICON is placed
                if (settings->use_manual_layout && crit->icon_pos.is_set) {
                    // It's detached, don't count it
                } else {
                    unplaced_criteria_count++;
                }
            }


            // --- Item Height Calculation (OPTIMIZED: Math only, no ImGui calls) ---

            // 1. Snapshot Text check
            bool has_snapshot_text = false;
            if (!is_stat_section && version <= MC_VERSION_1_6_4 && !settings->using_stats_per_world_legacy) {
                if (cat->done) has_snapshot_text = true;
            }

            // 2. Progress Text check
            bool has_progress_text = false;
            if (is_stat_section) {
                if (is_complex) {
                    has_progress_text = true; // Multi-stat always has counter
                } else if (cat->criteria_count == 1) {
                    TrackableItem *crit = cat->criteria[0];
                    if (crit->goal > 0 || crit->goal == -1) has_progress_text = true;
                }
            } else {
                if (cat->criteria_count > 0) has_progress_text = true;
            }

            // Base icon + Main text + Padding
            float item_height = 96.0f + main_text_line_height + 4.0f;
            if (has_snapshot_text) item_height += sub_text_line_height + 4.0f;
            if (has_progress_text) item_height += sub_text_line_height + 4.0f;

            bool use_scrolling_list = false;
            float single_criterion_height = 36.0f; // Height for each criterion row
            float criteria_list_height = 0.0f; // The pixel height the list will take up

            // ONLY expand parent height based on UNPLACED criteria!
            // Skip for simple stat categories — their single criterion is rendered
            // as inline progress text, not as a sub-item row.
            bool is_simple_stat = is_stat_section && cat->is_single_stat_category;
            if (unplaced_criteria_count > 0 && !is_simple_stat) {
                item_height += 12.0f; // Initial padding before criteria list

                // Check if we exceed the threshold AND no children are manually placed
                if (is_complex && unplaced_criteria_count > settings->scrollable_list_threshold && !has_manual_child) {
                    use_scrolling_list = true;
                    criteria_list_height = (float) settings->scrollable_list_threshold * single_criterion_height;
                } else {
                    criteria_list_height = (float) unplaced_criteria_count * single_criterion_height;
                }
                item_height += criteria_list_height;
            }


            // --- Layout and Culling ---
            // Skip auto-layout items that are fully hidden in manual layout
            if (settings->use_manual_layout && !cat->icon_pos.is_set &&
                cat->icon_pos.is_hidden_in_layout && cat->text_pos.is_hidden_in_layout &&
                (!has_progress_text || cat->progress_pos.is_hidden_in_layout) && settings->goal_hiding_mode !=
                SHOW_ALL) {
                continue;
            }

            float item_x = current_x;
            float item_y = current_y;

            if (settings->use_manual_layout && cat->icon_pos.is_set) {
                ImVec2 anchor_off = get_anchor_offset(cat->icon_pos.anchor, 96.0f, 96.0f);
                item_x = cat->icon_pos.x + anchor_off.x;
                item_y = cat->icon_pos.y + anchor_off.y;
            } else {
                // Procedural Auto-Layout Wrapping
                if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
                    current_x = padding;
                    current_y += row_max_height;
                    row_max_height = 0.0f;
                    item_x = current_x;
                    item_y = current_y;
                }
                // Update procedural layout cursors
                current_x += uniform_item_width;
                row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
            }

            ImVec2 screen_pos = ImVec2((item_x * t->zoom_level) + t->camera_offset.x,
                                       (item_y * t->zoom_level) + t->camera_offset.y);

            // Culling logic
            ImVec2 item_size_on_screen = ImVec2(uniform_item_width * t->zoom_level, item_height * t->zoom_level);


            bool is_visible_on_screen = !(screen_pos.x > io.DisplaySize.x || (screen_pos.x + item_size_on_screen.x) < 0
                                          ||
                                          screen_pos.y > io.DisplaySize.y || (screen_pos.y + item_size_on_screen.y) <
                                          0);

            // Disable coarse parent culling in manual layout so detached text/criteria don't vanish
            if (settings->use_manual_layout) {
                is_visible_on_screen = true;
            }


            // Per-position hiding for manual layout
            bool hide_icon_in_layout = settings->use_manual_layout && cat->icon_pos.is_hidden_in_layout && settings->
                                       goal_hiding_mode != SHOW_ALL;
            bool hide_text_in_layout = settings->use_manual_layout && cat->text_pos.is_hidden_in_layout && settings->
                                       goal_hiding_mode != SHOW_ALL;
            bool hide_progress_in_layout = settings->use_manual_layout && cat->progress_pos.is_hidden_in_layout &&
                                           settings->goal_hiding_mode != SHOW_ALL;

            // --- Rendering Core Logic (Only if visible) ---
            if (is_visible_on_screen) {
                // --- Text String Construction (Only needed if visible) ---
                char snapshot_text[8] = "";
                if (has_snapshot_text) {
                    if (cat->done && !cat->done_in_snapshot) { strcpy(snapshot_text, "(New)"); } else if (cat->done) {
                        strcpy(snapshot_text, "(Old)");
                    }
                }

                char progress_text[32] = "";
                if (has_progress_text) {
                    if (is_stat_section) {
                        if (is_complex) {
                            snprintf(progress_text, sizeof(progress_text), "(%d / %d)", cat->completed_criteria_count,
                                     cat->criteria_count);
                        } else if (cat->criteria_count == 1) {
                            TrackableItem *crit = cat->criteria[0];
                            if (crit->goal > 0)
                                snprintf(progress_text, sizeof(progress_text), "(%d / %d)",
                                         crit->progress, crit->goal);
                            else if (crit->goal == -1)
                                snprintf(progress_text, sizeof(progress_text), "(%d)",
                                         crit->progress);
                        }
                    } else {
                        snprintf(progress_text, sizeof(progress_text), "(%d / %d)", cat->completed_criteria_count,
                                 cat->criteria_count);
                    }
                }

                // Get Text Sizes for Centering (Only if visible)
                SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize);
                ImVec2 text_size = ImGui::CalcTextSize(cat->display_name);
                RESET_FONT_SCALE();

                SET_FONT_SCALE(settings->tracker_sub_font_size, t->tracker_font->LegacySize);
                ImVec2 progress_text_size = ImGui::CalcTextSize(progress_text);
                ImVec2 snapshot_text_size = ImGui::CalcTextSize(snapshot_text);
                RESET_FONT_SCALE();

                ImVec2 bg_size = ImVec2(96.0f, 96.0f);

                // Select texture *pair* and render
                SDL_Texture *static_bg = t->adv_bg;
                AnimatedTexture *anim_bg = t->adv_bg_anim;

                if (is_considered_complete_render) {
                    static_bg = t->adv_bg_done;
                    anim_bg = t->adv_bg_done_anim;
                } else {
                    bool has_progress = false;
                    if (is_complex) {
                        has_progress = cat->completed_criteria_count > 0;
                    } else if (is_stat_section && cat->criteria_count == 1) {
                        has_progress = cat->criteria[0]->progress > 0;
                    }
                    if (has_progress) {
                        static_bg = t->adv_bg_half_done;
                        anim_bg = t->adv_bg_half_done_anim;
                    }
                }

                // Now render the correct one
                SDL_Texture *texture_to_draw = static_bg;
                if (anim_bg && anim_bg->frame_count > 0) {
                    // Standard GIF Frame Selection Logic
                    if (anim_bg->delays && anim_bg->total_duration > 0) {
                        Uint32 current_ticks = SDL_GetTicks();
                        Uint32 elapsed_time = current_ticks % anim_bg->total_duration;
                        int current_frame = 0;
                        Uint32 time_sum = 0;
                        for (int frame_idx = 0; frame_idx < anim_bg->frame_count; ++frame_idx) {
                            time_sum += anim_bg->delays[frame_idx];
                            if (elapsed_time < time_sum) {
                                current_frame = frame_idx;
                                break;
                            }
                        }
                        texture_to_draw = anim_bg->frames[current_frame];
                    } else {
                        texture_to_draw = anim_bg->frames[0];
                    }
                }

                // Render Background (Always Visible)
                if (!hide_icon_in_layout && texture_to_draw)
                    draw_list->AddImage((void *) texture_to_draw, screen_pos,
                                        ImVec2(screen_pos.x + bg_size.x * t->zoom_level,
                                               screen_pos.y + bg_size.y * t->zoom_level));

                // Render Main Icon (Animated or Static) - Always Visible
                // --- Start GIF Frame Selection Logic (Main Icon) ---
                if (cat->anim_texture && cat->anim_texture->frame_count > 0) {
                    if (cat->anim_texture->delays && cat->anim_texture->total_duration > 0) {
                        Uint32 current_ticks = SDL_GetTicks();
                        Uint32 elapsed_time = current_ticks % cat->anim_texture->total_duration;
                        int current_frame = 0;
                        Uint32 time_sum = 0;
                        for (int frame_idx = 0; frame_idx < cat->anim_texture->frame_count; ++frame_idx) {
                            time_sum += cat->anim_texture->delays[frame_idx];
                            if (elapsed_time < time_sum) {
                                current_frame = frame_idx;
                                break;
                            }
                        }
                        texture_to_draw = cat->anim_texture->frames[current_frame];
                    } else {
                        // Fallback if no timing info
                        texture_to_draw = cat->anim_texture->frames[0];
                    }
                } else if (cat->texture) {
                    // Static texture
                    texture_to_draw = cat->texture;
                }
                // --- End GIF Frame Selection Logic (Main Icon) ---

                if (texture_to_draw) {
                    // --- Start Icon Scaling and Centering Logic (Main Icon 64x64 box) ---
                    float tex_w = 0.0f, tex_h = 0.0f;
                    SDL_GetTextureSize(texture_to_draw, &tex_w, &tex_h);
                    ImVec2 target_box_size = ImVec2(64.0f * t->zoom_level, 64.0f * t->zoom_level);
                    // Target box size on screen
                    float scale_factor = 1.0f;
                    if (tex_w > 0 && tex_h > 0) {
                        // Avoid division by zero
                        scale_factor = fminf(target_box_size.x / tex_w, target_box_size.y / tex_h);
                    }
                    ImVec2 scaled_size = ImVec2(tex_w * scale_factor, tex_h * scale_factor); // Scaled size on screen
                    ImVec2 box_p_min = ImVec2(screen_pos.x + 16.0f * t->zoom_level, // 16.0f to CENTER the icons
                                              screen_pos.y + 16.0f * t->zoom_level); // Top-left of 64x64 box on screen
                    ImVec2 icon_padding = ImVec2((target_box_size.x - scaled_size.x) * 0.5f,
                                                 (target_box_size.y - scaled_size.y) * 0.5f); // Padding within the box
                    ImVec2 p_min = ImVec2(box_p_min.x + icon_padding.x, box_p_min.y + icon_padding.y);
                    // Final top-left for drawing
                    ImVec2 p_max = ImVec2(p_min.x + scaled_size.x, p_min.y + scaled_size.y);
                    // Final bottom-right for drawing
                    if (!hide_icon_in_layout)
                        draw_list->AddImage((void *) texture_to_draw, p_min, p_max);
                    // --- End Icon Scaling and Centering Logic (Main Icon) ---
                }

                // --- VISUAL LAYOUT DRAGGING (PARENT ICON) ---
                char drag_id[256];
                const char *cat_type = is_stat_section
                                           ? "Stat"
                                           : (version <= MC_VERSION_1_11_2 ? "Achievement" : "Advancement");
                const char *crit_type = is_stat_section
                                            ? "Sub-Stat"
                                            : (version <= MC_VERSION_1_11_2 ? "Achievement" : "Criterion");
                snprintf(drag_id, sizeof(drag_id), "drag_cat_icon_%s_%s", is_stat_section ? "stat" : "adv",
                         cat->root_name);
                handle_visual_layout_dragging(t, drag_id, screen_pos,
                                              ImVec2(bg_size.x * t->zoom_level, bg_size.y * t->zoom_level),
                                              cat->icon_pos, cat_type, cat->display_name, "Icon",
                                              cat->root_name);

                // --- TEXT CENTERING AND POSITIONING ---
                float text_x_center = screen_pos.x + (bg_size.x * t->zoom_level) * 0.5f;
                float current_text_y = screen_pos.y + bg_size.y * t->zoom_level + (4.0f * t->zoom_level);
                float main_font_size = settings->tracker_font_size;
                float sub_font_size = settings->tracker_sub_font_size;

                ImU32 current_text_color = is_considered_complete_render ? text_color_faded : text_color;

                if (settings->use_manual_layout && cat->text_pos.is_set) {
                    ImVec2 text_anchor_off = get_anchor_offset(cat->text_pos.anchor, text_size.x, text_size.y);
                    text_x_center = ((cat->text_pos.x + text_anchor_off.x) * t->zoom_level) + t->camera_offset.x + (
                                        text_size.x * t->zoom_level) * 0.5f;
                    current_text_y = ((cat->text_pos.y + text_anchor_off.y) * t->zoom_level) + t->camera_offset.y;
                }

                // Main Name
                if (t->zoom_level > LOD_TEXT_MAIN_THRESHOLD && !hide_text_in_layout) {
                    draw_list->AddText(nullptr, main_font_size * t->zoom_level,
                                       ImVec2(text_x_center - (text_size.x * t->zoom_level) * 0.5f, current_text_y),
                                       current_text_color, cat->display_name);

                    // --- VISUAL LAYOUT DRAGGING (PARENT TEXT) ---
                    snprintf(drag_id, sizeof(drag_id), "drag_cat_text_%s_%s", is_stat_section ? "stat" : "adv",
                             cat->root_name);
                    handle_visual_layout_dragging(t, drag_id,
                                                  ImVec2(text_x_center - (text_size.x * t->zoom_level) * 0.5f,
                                                         current_text_y),
                                                  ImVec2(text_size.x * t->zoom_level, text_size.y * t->zoom_level),
                                                  cat->text_pos, cat_type, cat->display_name, "Text",
                                                  cat->root_name);
                }
                current_text_y += text_size.y * t->zoom_level + 4.0f * t->zoom_level; // ADVANCE LAYOUT

                // Snapshot Text
                if (has_snapshot_text && !hide_text_in_layout) {
                    if (t->zoom_level > LOD_TEXT_MAIN_THRESHOLD) {
                        draw_list->AddText(nullptr, sub_font_size * t->zoom_level,
                                           ImVec2(text_x_center - (snapshot_text_size.x * t->zoom_level) * 0.5f,
                                                  current_text_y),
                                           text_color_faded, snapshot_text);
                    }
                    current_text_y += snapshot_text_size.y * t->zoom_level + 4.0f * t->zoom_level;
                }

                // Progress Text
                if (has_progress_text && !hide_progress_in_layout) {
                    float prog_x_center = text_x_center;
                    float prog_y = current_text_y;

                    // Apply manual progress_pos if set
                    if (settings->use_manual_layout && cat->progress_pos.is_set) {
                        ImVec2 prog_anchor_off = get_anchor_offset(cat->progress_pos.anchor, progress_text_size.x,
                                                                   progress_text_size.y);
                        prog_x_center = ((cat->progress_pos.x + prog_anchor_off.x) * t->zoom_level) + t->camera_offset.x
                                        + (
                                            progress_text_size.x * t->zoom_level) * 0.5f;
                        prog_y = ((cat->progress_pos.y + prog_anchor_off.y) * t->zoom_level) + t->camera_offset.y;
                    }

                    if (t->zoom_level > LOD_TEXT_SUB_THRESHOLD) {
                        draw_list->AddText(nullptr, sub_font_size * t->zoom_level,
                                           ImVec2(prog_x_center - (progress_text_size.x * t->zoom_level) * 0.5f,
                                                  prog_y),
                                           current_text_color, progress_text);

                        // --- VISUAL LAYOUT DRAGGING (PARENT PROGRESS) ---
                        snprintf(drag_id, sizeof(drag_id), "drag_cat_prog_%s_%s", is_stat_section ? "stat" : "adv",
                                 cat->root_name);
                        handle_visual_layout_dragging(t, drag_id,
                                                      ImVec2(
                                                          prog_x_center - (progress_text_size.x * t->zoom_level) * 0.5f,
                                                          prog_y),
                                                      ImVec2(progress_text_size.x * t->zoom_level,
                                                             progress_text_size.y * t->zoom_level),
                                                      cat->progress_pos, cat_type, cat->display_name, "Progress",
                                                      cat->root_name);
                    }
                    current_text_y = prog_y + progress_text_size.y * t->zoom_level + 4.0f * t->zoom_level;
                }

                // Render Criteria/Sub-Stats (if complex and visible)
                if (is_complex && !children_to_render.empty()) {
                    float sub_item_y_offset_world = item_y + (current_text_y - screen_pos.y) / t->zoom_level + 8.0f;

                    // --- SCROLLING SETUP ---
                    ImVec2 list_min_screen, list_max_screen;
                    float total_content_height_logical = (float) unplaced_criteria_count * single_criterion_height;
                    float visible_height_logical = criteria_list_height;
                    float max_scroll_logical = total_content_height_logical - visible_height_logical;
                    if (max_scroll_logical < 0) max_scroll_logical = 0;

                    if (use_scrolling_list) {
                        list_min_screen = ImVec2(screen_pos.x,
                                                 (sub_item_y_offset_world * t->zoom_level) + t->camera_offset.y);
                        list_max_screen = ImVec2(screen_pos.x + (uniform_item_width * t->zoom_level),
                                                 list_min_screen.y + (criteria_list_height * t->zoom_level));

                        if (ImGui::IsMouseHoveringRect(list_min_screen, list_max_screen) && max_scroll_logical > 0) {
                            t->is_hovering_scrollable_list = true;
                            if (io.MouseWheel != 0.0f)
                                cat->scroll_y -= io.MouseWheel * settings->tracker_list_scroll_speed;
                        }

                        if (cat->scroll_y < 0.0f) cat->scroll_y = 0.0f;
                        if (cat->scroll_y > max_scroll_logical) cat->scroll_y = max_scroll_logical;

                        draw_list->PushClipRect(list_min_screen, list_max_screen, true);
                    }

                    int render_index = 0; // Needed for relative positioning of UNPLACED items
                    for (TrackableItem *crit: children_to_render) {
                        if (!crit) continue;

                        bool is_manually_placed =
                                settings->use_manual_layout && crit->icon_pos.is_set;
                        bool hide_crit_icon_in_layout =
                                settings->use_manual_layout && crit->icon_pos.is_hidden_in_layout && settings->
                                goal_hiding_mode != SHOW_ALL;
                        bool hide_crit_text_in_layout =
                                settings->use_manual_layout && crit->text_pos.is_hidden_in_layout && settings->
                                goal_hiding_mode != SHOW_ALL;

                        float item_screen_y = 0.0f;
                        if (!is_manually_placed) {
                            float relative_y = (float) render_index * single_criterion_height;
                            item_screen_y = ((sub_item_y_offset_world + relative_y) * t->zoom_level) + t->camera_offset.
                                            y;

                            if (use_scrolling_list) {
                                item_screen_y -= (cat->scroll_y * t->zoom_level);
                                float item_h_screen = single_criterion_height * t->zoom_level;
                                if (item_screen_y + item_h_screen < list_min_screen.y || item_screen_y > list_max_screen
                                    .y) {
                                    render_index++; // Must tick up for culled items to maintain scroll position!
                                    continue;
                                }
                            }
                        }

                        ImVec2 crit_base_pos_screen;
                        if (is_manually_placed) {
                            ImVec2 crit_icon_anchor_off = get_anchor_offset(crit->icon_pos.anchor, 32.0f, 32.0f);
                            crit_base_pos_screen = ImVec2(
                                ((crit->icon_pos.x + crit_icon_anchor_off.x) * t->zoom_level) + t->camera_offset.x,
                                ((crit->icon_pos.y + crit_icon_anchor_off.y) * t->zoom_level) + t->camera_offset.y);
                        } else {
                            crit_base_pos_screen = ImVec2((item_x * t->zoom_level) + t->camera_offset.x, item_screen_y);
                        }

                        float current_element_x_screen = crit_base_pos_screen.x;

                        // LOD for Sub-Item Icon

                        // LOD for Sub-Item Icon
                        if (t->zoom_level > LOD_ICON_DETAIL_THRESHOLD && !hide_crit_icon_in_layout) {
                            // RENDER ACTUAL ICON
                            SDL_Texture *crit_texture_to_draw = nullptr;
                            // --- Start GIF Frame Selection Logic (Child Icon) ---
                            if (crit->anim_texture && crit->anim_texture->frame_count > 0) {
                                if (crit->anim_texture->delays && crit->anim_texture->total_duration > 0) {
                                    Uint32 current_ticks = SDL_GetTicks();
                                    Uint32 elapsed_time = current_ticks % crit->anim_texture->total_duration;
                                    int current_frame = 0;
                                    Uint32 time_sum = 0;
                                    for (int frame_idx = 0; frame_idx < crit->anim_texture->frame_count; ++frame_idx) {
                                        time_sum += crit->anim_texture->delays[frame_idx];
                                        if (elapsed_time < time_sum) {
                                            current_frame = frame_idx;
                                            break;
                                        }
                                    }
                                    crit_texture_to_draw = crit->anim_texture->frames[current_frame];
                                } else {
                                    // Fallback if no timing info
                                    crit_texture_to_draw = crit->anim_texture->frames[0];
                                }
                            } else if (crit->texture) {
                                // Static texture
                                crit_texture_to_draw = crit->texture;
                            }
                            // --- End GIF Frame Selection Logic (Child Icon) ---


                            if (crit_texture_to_draw) {
                                // --- Start Icon Scaling and Centering Logic (Child Icon 32x32 box) ---
                                float tex_w = 0.0f, tex_h = 0.0f;
                                SDL_GetTextureSize(crit_texture_to_draw, &tex_w, &tex_h);
                                ImVec2 target_box_size = ImVec2(32.0f * t->zoom_level, 32.0f * t->zoom_level);
                                // Target box size on screen
                                float scale_factor = 1.0f;
                                if (tex_w > 0 && tex_h > 0) {
                                    // Avoid division by zero
                                    scale_factor = fminf(target_box_size.x / tex_w, target_box_size.y / tex_h);
                                }
                                ImVec2 scaled_size = ImVec2(tex_w * scale_factor, tex_h * scale_factor);
                                // Scaled size on screen
                                // Padding is calculated relative to the 32x32 box, no offset needed here
                                ImVec2 icon_padding = ImVec2((target_box_size.x - scaled_size.x) * 0.5f,
                                                             (target_box_size.y - scaled_size.y) * 0.5f);
                                ImVec2 p_min = ImVec2(crit_base_pos_screen.x + icon_padding.x,
                                                      crit_base_pos_screen.y + icon_padding.y);
                                // Final top-left for drawing
                                ImVec2 p_max = ImVec2(p_min.x + scaled_size.x, p_min.y + scaled_size.y);
                                // Final bottom-right for drawing
                                ImU32 icon_tint = crit->done ? icon_tint_faded : IM_COL32_WHITE; // Apply fade if done
                                draw_list->AddImage((void *) crit_texture_to_draw, p_min, p_max, ImVec2(0, 0),
                                                    ImVec2(1, 1),
                                                    icon_tint);
                                // --- End Icon Scaling and Centering Logic (Child Icon) ---
                            }
                        } else if (!hide_crit_icon_in_layout) {
                            // RENDER SIMPLIFIED SQUARE (Average Color Placeholder)
                            // Since we don't have the exact average color calculated, use a generic color
                            // A faded text color works well to represent "something is here"
                            ImU32 avg_placeholder_color = IM_COL32(settings->text_color.r, settings->text_color.g,
                                                                   settings->text_color.b, 100);
                            // 32x32 box size
                            ImVec2 p_min = crit_base_pos_screen;
                            ImVec2 p_max = ImVec2(p_min.x + 32.0f * t->zoom_level, p_min.y + 32.0f * t->zoom_level);
                            draw_list->AddRectFilled(p_min, p_max, avg_placeholder_color);
                        }

                        // --- VISUAL LAYOUT DRAGGING (CRIT ICON) ---
                        snprintf(drag_id, sizeof(drag_id), "drag_crit_icon_%s_%s", cat->root_name, crit->root_name);
                        handle_visual_layout_dragging(t, drag_id, crit_base_pos_screen,
                                                      ImVec2(32.0f * t->zoom_level, 32.0f * t->zoom_level),
                                                      crit->icon_pos, crit_type, crit->display_name,
                                                      "Icon", crit->root_name,
                                                      cat->display_name, cat->root_name);

                        current_element_x_screen += 32.0f * t->zoom_level + 4.0f * t->zoom_level;
                        // Icon width + padding


                        // Render Checkbox for Multi-Criteria Stats
                        // LOD: Hide checkbox if zoomed out too far (tied to MAIN text threshold as requested)
                        if (is_stat_section && cat->criteria_count > 1 && t->zoom_level > LOD_TEXT_MAIN_THRESHOLD) {
                            ImVec2 check_pos = ImVec2(current_element_x_screen,
                                                      crit_base_pos_screen.y + 6 * t->zoom_level);
                            ImRect checkbox_rect(
                                check_pos, ImVec2(check_pos.x + 20 * t->zoom_level, check_pos.y + 20 * t->zoom_level));
                            bool is_hovered = ImGui::IsMouseHoveringRect(checkbox_rect.Min, checkbox_rect.Max);
                            ImU32 check_fill = is_hovered ? checkbox_hover_color : checkbox_fill_color;
                            draw_list->AddRectFilled(checkbox_rect.Min, checkbox_rect.Max, check_fill,
                                                     3.0f * t->zoom_level);
                            draw_list->AddRect(checkbox_rect.Min, checkbox_rect.Max, text_color, 3.0f * t->zoom_level);

                            if (crit->is_manually_completed) {
                                // Draw checkmark
                                ImVec2 p1 = ImVec2(check_pos.x + 5 * t->zoom_level, check_pos.y + 10 * t->zoom_level);
                                ImVec2 p2 = ImVec2(check_pos.x + 9 * t->zoom_level, check_pos.y + 15 * t->zoom_level);
                                ImVec2 p3 = ImVec2(check_pos.x + 15 * t->zoom_level, check_pos.y + 6 * t->zoom_level);
                                draw_list->AddLine(p1, p2, checkmark_color, 2.0f * t->zoom_level);
                                draw_list->AddLine(p2, p3, checkmark_color, 2.0f * t->zoom_level);
                            }

                            // Co-op: Show tooltip for host-only stat checkboxes
                            if (is_hovered && settings->network_mode == NETWORK_RECEIVER &&
                                settings->coop_stat_checkbox == COOP_STAT_CHECKBOX_HOST_ONLY) {
                                char tooltip_buf[128];
                                snprintf(tooltip_buf, sizeof(tooltip_buf),
                                         "Stat checkboxes are set to Host Only.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }

                            // Deactivating left click when in visual editing mode
                            // Co-op: Receivers respect stat checkbox permission
                            if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !t->
                                is_visual_layout_editing) {
                                bool rcv_in_lobby = (settings->network_mode == NETWORK_RECEIVER &&
                                    g_coop_ctx && coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED);
                                if (rcv_in_lobby &&
                                    settings->coop_stat_checkbox == COOP_STAT_CHECKBOX_HOST_ONLY) {
                                    // Host-only mode: clicking disabled for receivers
                                } else if (rcv_in_lobby &&
                                           settings->coop_stat_checkbox == COOP_STAT_CHECKBOX_ANY_PLAYER) {
                                    // Any-player mode: send toggle request to host
                                    CoopCustomGoalModMsg mod = {};
                                    snprintf(mod.goal_root_name, sizeof(mod.goal_root_name), "%s", crit->root_name);
                                    snprintf(mod.parent_root_name, sizeof(mod.parent_root_name), "%s", cat->root_name);
                                    mod.action = COOP_MOD_TOGGLE;
                                    coop_net_send_custom_goal_mod(g_coop_ctx, &mod);
                                } else {
                                    // Host or singleplayer: toggle locally
                                    crit->is_manually_completed = !crit->is_manually_completed;
                                    bool crit_naturally_done = (crit->goal > 0 && crit->progress >= crit->goal);
                                    crit->done = crit->is_manually_completed || crit_naturally_done;

                                    cat->completed_criteria_count = 0;
                                    for (int k = 0; k < cat->criteria_count; ++k) {
                                        if (cat->criteria[k]->done) cat->completed_criteria_count++;
                                    }
                                    bool all_children_done = (
                                        cat->criteria_count > 0 && cat->completed_criteria_count >= cat->
                                        criteria_count);
                                    cat->done = cat->is_manually_completed || all_children_done;

                                    SDL_SetAtomicInt(&g_suppress_settings_watch, 1);
                                    settings_save(settings, t->template_data, SAVE_CONTEXT_ALL);
                                    SDL_SetAtomicInt(&g_coop_broadcast_needed, 1);
                                    SDL_SetAtomicInt(&g_game_data_changed, 1);
                                }
                            }
                        }

                        // ADVANCE LAYOUT EVEN IF CHECKBOX NOT RENDERED
                        if (is_stat_section && cat->criteria_count > 1) {
                            current_element_x_screen += 20.0f * t->zoom_level + 4.0f * t->zoom_level;
                            // Checkbox width + padding
                        }


                        // Render Child Text and Progress
                        // LOD: Check if zoom level is sufficient for sub-text
                        // Render Child Text and Progress
                        if (t->zoom_level > LOD_TEXT_SUB_THRESHOLD && !hide_crit_text_in_layout) {
                            ImU32 current_child_text_color = crit->done ? text_color_faded : text_color;

                            float scale_factor = (settings->tracker_font_size > 0.0f)
                                                     ? (settings->tracker_sub_font_size / settings->tracker_font_size)
                                                     : 1.0f;
                            ImGui::PushFont(t->tracker_font);
                            ImGui::SetWindowFontScale(scale_factor);
                            ImVec2 child_text_size = ImGui::CalcTextSize(crit->display_name);
                            ImGui::SetWindowFontScale(1.0f);
                            ImGui::PopFont();

                            float text_y_pos = crit_base_pos_screen.y + (
                                                   (32.0f * t->zoom_level) - (child_text_size.y * t->zoom_level)) *
                                               0.5f;
                            ImVec2 child_text_pos = ImVec2(current_element_x_screen, text_y_pos);

                            if (settings->use_manual_layout && crit->text_pos.is_set) {
                                ImVec2 crit_text_anchor_off = get_anchor_offset(
                                    crit->text_pos.anchor, child_text_size.x, child_text_size.y);
                                child_text_pos = ImVec2(
                                    ((crit->text_pos.x + crit_text_anchor_off.x) * t->zoom_level) + t->camera_offset.x,
                                    ((crit->text_pos.y + crit_text_anchor_off.y) * t->zoom_level) + t->camera_offset.y);
                                current_element_x_screen = child_text_pos.x;
                                // Update cursor so progress text follows naturally
                            }

                            draw_list->AddText(nullptr, sub_font_size * t->zoom_level, child_text_pos,
                                               current_child_text_color, crit->display_name);

                            // --- VISUAL LAYOUT DRAGGING (CRIT TEXT) ---
                            snprintf(drag_id, sizeof(drag_id), "drag_crit_text_%s_%s", cat->root_name, crit->root_name);
                            handle_visual_layout_dragging(t, drag_id, child_text_pos,
                                                          ImVec2(child_text_size.x * t->zoom_level,
                                                                 child_text_size.y * t->zoom_level),
                                                          crit->text_pos, crit_type, crit->display_name,
                                                          "Text", crit->root_name,
                                                          cat->display_name, cat->root_name);

                            current_element_x_screen += (child_text_size.x * t->zoom_level) + (4.0f * t->zoom_level);

                            char crit_progress_text[32] = "";
                            if (is_stat_section) {
                                if (crit->goal > 0) {
                                    snprintf(crit_progress_text, sizeof(crit_progress_text), "(%d / %d)",
                                             crit->progress, crit->goal);
                                } else if (crit->goal == -1) {
                                    snprintf(crit_progress_text, sizeof(crit_progress_text), "(%d)", crit->progress);
                                }

                                if (crit_progress_text[0] != '\0') {
                                    ImVec2 crit_progress_pos = ImVec2(current_element_x_screen, child_text_pos.y);
                                    draw_list->AddText(nullptr, sub_font_size * t->zoom_level, crit_progress_pos,
                                                       current_child_text_color, crit_progress_text);
                                }
                            }
                        }

                        // Only advance the list gap for items actually sitting in the list
                        if (!is_manually_placed) {
                            render_index++;
                        }
                    } // End loop through children_to_render

                    // --- DRAW SCROLLBAR & POP CLIP ---
                    if (use_scrolling_list) {
                        draw_list->PopClipRect();

                        // Only draw if needed (and LOD allows)
                        if (total_content_height_logical > visible_height_logical && t->zoom_level > settings->
                            lod_icon_detail_threshold) {
                            // 1. Calculate Visual Dimensions (Screen Space)
                            float total_content_height_screen = total_content_height_logical * t->zoom_level;
                            float visible_height_screen = visible_height_logical * t->zoom_level;

                            float bar_width = 6.0f * t->zoom_level;
                            float bar_padding_right = 4.0f * t->zoom_level;
                            float bar_x = screen_pos.x + (uniform_item_width * t->zoom_level) - bar_width -
                                          bar_padding_right;
                            float bar_top_y = (sub_item_y_offset_world * t->zoom_level) + t->camera_offset.y;

                            // Calculate Handle Height
                            float handle_height_screen =
                                    visible_height_screen * (visible_height_screen / total_content_height_screen);
                            if (handle_height_screen < 10.0f * t->zoom_level)
                                handle_height_screen = 10.0f * t->zoom_level;

                            float available_scroll_track = visible_height_screen - handle_height_screen;

                            // 2. Interaction (Invisible Button)
                            // We place an invisible button over the ENTIRE scrollbar track to handle dragging robustly.
                            // SetCursorScreenPos tells ImGui where to place the next widget (our InvisibleButton).
                            ImGui::SetCursorScreenPos(ImVec2(bar_x - 2.0f * t->zoom_level, bar_top_y));
                            // Add padding for easier grabbing
                            ImGui::PushID(cat); // Ensure Unique ID per category

                            // Button covers the whole track height
                            ImGui::InvisibleButton("##scrollbar",
                                                   ImVec2(bar_width + 4.0f * t->zoom_level, visible_height_screen));

                            bool is_active = ImGui::IsItemActive(); // Held down
                            bool is_hovered = ImGui::IsItemHovered(); // Hovered

                            if (is_active) {
                                t->is_hovering_scrollable_list = true; // Block main map zoom while dragging

                                // Calculate ratio: How many logical pixels of content = 1 screen pixel of track movement?
                                // Max Scroll (Logical) / Track Space (Screen)
                                float max_scroll_logical = total_content_height_logical - visible_height_logical;

                                if (available_scroll_track > 0.1f) {
                                    float pixels_to_logical_scale = max_scroll_logical / available_scroll_track;

                                    // Apply Delta: This ensures smooth dragging relative to mouse movement
                                    cat->scroll_y += io.MouseDelta.y * pixels_to_logical_scale;
                                }
                            } else if (is_hovered) {
                                t->is_hovering_scrollable_list = true; // Block zoom if just hovering bar
                            }

                            ImGui::PopID();

                            // 3. Clamp Scroll (Ensure we stay in bounds after drag)
                            float max_scroll_logical = total_content_height_logical - visible_height_logical;
                            if (cat->scroll_y < 0.0f) cat->scroll_y = 0.0f;
                            if (cat->scroll_y > max_scroll_logical) cat->scroll_y = max_scroll_logical;

                            // 4. Draw Visuals
                            // Recalculate handle Y position based on the clamped scroll
                            float scroll_ratio = (max_scroll_logical > 0) ? (cat->scroll_y / max_scroll_logical) : 0.0f;
                            float handle_y = bar_top_y + (scroll_ratio * available_scroll_track);

                            // Colors based on State
                            ImU32 track_color = IM_COL32(settings->text_color.r, settings->text_color.g,
                                                         settings->text_color.b, 30); // Faint track
                            ImU32 handle_color;

                            if (is_active) {
                                // Active (Dragging): High opacity
                                handle_color = IM_COL32(settings->text_color.r, settings->text_color.g,
                                                        settings->text_color.b, 200);
                            } else if (is_hovered) {
                                // Hovered: Medium opacity
                                handle_color = IM_COL32(settings->text_color.r, settings->text_color.g,
                                                        settings->text_color.b, 150);
                            } else {
                                // Default: User-defined faded alpha (usually 100)
                                handle_color = IM_COL32(settings->text_color.r, settings->text_color.g,
                                                        settings->text_color.b, ADVANCELY_FADED_ALPHA);
                            }

                            // Draw Track Background
                            draw_list->AddRectFilled(
                                ImVec2(bar_x, bar_top_y),
                                ImVec2(bar_x + bar_width, bar_top_y + visible_height_screen),
                                track_color,
                                4.0f * t->zoom_level // Rounded
                            );

                            // Draw Handle
                            draw_list->AddRectFilled(
                                ImVec2(bar_x, handle_y),
                                ImVec2(bar_x + bar_width, handle_y + handle_height_screen),
                                handle_color,
                                4.0f * t->zoom_level
                            );
                        }
                    }
                } // End if (is_complex && visible_criteria_render_count > 0)


                // Render Parent Checkbox for Stats (single or multi)
                // LOD: Hide parent checkbox if zoomed out too far (tied to main text threshold)
                if (is_stat_section && t->zoom_level > LOD_TEXT_MAIN_THRESHOLD) {
                    ImVec2 check_pos_parent = ImVec2(screen_pos.x + 4 * t->zoom_level,
                                                     screen_pos.y + 4 * t->zoom_level); // Top-left corner
                    ImRect checkbox_rect_parent(check_pos_parent,
                                                ImVec2(check_pos_parent.x + 20 * t->zoom_level,
                                                       check_pos_parent.y + 20 * t->zoom_level));
                    bool is_hovered_parent = ImGui::IsMouseHoveringRect(checkbox_rect_parent.Min,
                                                                        checkbox_rect_parent.Max);
                    ImU32 check_fill_parent = is_hovered_parent ? checkbox_hover_color : checkbox_fill_color;
                    draw_list->AddRectFilled(checkbox_rect_parent.Min, checkbox_rect_parent.Max, check_fill_parent,
                                             3.0f * t->zoom_level);
                    draw_list->AddRect(checkbox_rect_parent.Min, checkbox_rect_parent.Max, text_color,
                                       3.0f * t->zoom_level);

                    if (cat->is_manually_completed) {
                        // Draw checkmark
                        ImVec2 p1 = ImVec2(check_pos_parent.x + 5 * t->zoom_level,
                                           check_pos_parent.y + 10 * t->zoom_level);
                        ImVec2 p2 = ImVec2(check_pos_parent.x + 9 * t->zoom_level,
                                           check_pos_parent.y + 15 * t->zoom_level);
                        ImVec2 p3 = ImVec2(check_pos_parent.x + 15 * t->zoom_level,
                                           check_pos_parent.y + 6 * t->zoom_level);
                        draw_list->AddLine(p1, p2, checkmark_color, 2.0f * t->zoom_level);
                        draw_list->AddLine(p2, p3, checkmark_color, 2.0f * t->zoom_level);
                    }

                    // Co-op: Show tooltip for host-only stat checkboxes
                    if (is_hovered_parent && settings->network_mode == NETWORK_RECEIVER &&
                        settings->coop_stat_checkbox == COOP_STAT_CHECKBOX_HOST_ONLY) {
                        char tooltip_buf[128];
                        snprintf(tooltip_buf, sizeof(tooltip_buf),
                                 "Stat checkboxes are set to Host Only.");
                        ImGui::SetTooltip("%s", tooltip_buf);
                    }

                    // Deactivating left click when in visual editing mode
                    // Co-op: Receivers respect stat checkbox permission
                    if (is_hovered_parent && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !t->
                        is_visual_layout_editing) {
                        bool rcv_in_lobby = (settings->network_mode == NETWORK_RECEIVER &&
                            g_coop_ctx && coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED);
                        if (rcv_in_lobby &&
                            settings->coop_stat_checkbox == COOP_STAT_CHECKBOX_HOST_ONLY) {
                            // Host-only mode: clicking disabled for receivers
                        } else if (rcv_in_lobby &&
                                   settings->coop_stat_checkbox == COOP_STAT_CHECKBOX_ANY_PLAYER) {
                            // Any-player mode: send toggle request to host (parent stat)
                            CoopCustomGoalModMsg mod = {};
                            snprintf(mod.goal_root_name, sizeof(mod.goal_root_name), "%s", cat->root_name);
                            mod.parent_root_name[0] = '\0';
                            mod.action = COOP_MOD_TOGGLE;
                            coop_net_send_custom_goal_mod(g_coop_ctx, &mod);
                        } else {
                            // Host or singleplayer: toggle locally
                            cat->is_manually_completed = !cat->is_manually_completed;

                            // Recalculate children FIRST so completed_criteria_count is correct
                            for (int j = 0; j < cat->criteria_count; ++j) {
                                TrackableItem *crit = cat->criteria[j];
                                bool crit_naturally_done = (crit->goal > 0 && crit->progress >= crit->goal);
                                crit->done = cat->is_manually_completed || crit->is_manually_completed ||
                                             crit_naturally_done;
                            }
                            cat->completed_criteria_count = 0;
                            for (int k = 0; k < cat->criteria_count; ++k) {
                                if (cat->criteria[k]->done) cat->completed_criteria_count++;
                            }

                            // Now calculate parent done based on updated children
                            bool all_children_done = (
                                cat->criteria_count > 0 && cat->completed_criteria_count >= cat->criteria_count);
                            cat->done = cat->is_manually_completed || all_children_done;

                            SDL_SetAtomicInt(&g_suppress_settings_watch, 1);
                            settings_save(settings, t->template_data, SAVE_CONTEXT_ALL);
                            SDL_SetAtomicInt(&g_coop_broadcast_needed, 1);
                            SDL_SetAtomicInt(&g_game_data_changed, 1);
                        }
                    }
                } // End if (is_stat_section) for parent checkbox
            } // End if (is_visible_on_screen)
        } // End loop through categories for this pass
    }; // End of render_pass lambda

    // Execute render passes
    render_pass(false); // Render simple items first
    render_pass(true); // Render complex items second

    // Update vertical position for the next section
    current_y += row_max_height;
}

/**
 * @brief Renders a section of items that are simple TrackableItems (e.g., Unlocks).
 */
static void render_simple_item_section(Tracker *t, const AppSettings *settings, float &current_y, TrackableItem **items,
                                       int count, const char *section_title) {
    // LOD Thresholds
    const float LOD_TEXT_MAIN_THRESHOLD = settings->lod_text_main_threshold; // Hide Main Name
    const float LOD_TEXT_SUB_THRESHOLD = settings->lod_text_sub_threshold; // Hide Progress Text

    // Pre-calculate line heights once per frame (Optimization)
    const float main_text_line_height = settings->tracker_font_size;
    const float sub_text_line_height = settings->tracker_sub_font_size;

    // --- Pre-computation and Filtering for Counters ---
    int total_visible_count = 0;
    int completed_count = 0;

    for (int i = 0; i < count; ++i) {
        TrackableItem *item = items[i];
        if (!item) continue;

        // Determine completion status (simple boolean 'done')
        bool is_item_considered_complete = item->done;

        // Determine visibility based on hiding mode
        bool should_hide_based_on_mode = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_based_on_mode = (!settings->use_manual_layout && item->is_hidden) ||
                                            is_item_considered_complete;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_based_on_mode = !settings->use_manual_layout && item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_based_on_mode = false;
                break;
        }

        // Apply Search Filter for counting
        bool matches_search = item_matches_search(item, t->search_buffer)
                              || s_linked_top.count(item->root_name);

        // Count items only if they are not hidden by mode AND match the search
        if (!should_hide_based_on_mode && matches_search) {
            total_visible_count++;
            if (is_item_considered_complete) {
                completed_count++;
            }
        }
    }
    // --- End of Counter Calculation ---


    // --- Section Rendering ---

    // Check if *anything* should be rendered based on search + hiding
    bool section_has_renderable_content = false;
    for (int i = 0; i < count; ++i) {
        TrackableItem *item = items[i];
        if (!item) continue;

        bool should_hide_render = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_render = (!settings->use_manual_layout && item->is_hidden) || item->done;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = !settings->use_manual_layout && item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Combine hiding and search filter
        if (!should_hide_render && (item_matches_search(item, t->search_buffer)
                                    || s_linked_top.count(item->root_name))) {
            section_has_renderable_content = true;
            break; // Found at least one item to render
        }
    }

    if (!section_has_renderable_content) return; // Hide section if no items match filters


    ImGuiIO &io = ImGui::GetIO();

    // Use locked width if layout is locked
    float wrapping_width = t->layout_locked ? t->locked_layout_width : (io.DisplaySize.x / t->zoom_level);


    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 text_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                settings->text_color.a);
    ImU32 text_color_faded = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                      ADVANCELY_FADED_ALPHA);
    float scale_factor; // Declare scale_factor here

    // Call separator with calculated counts (sub-counts are -1)
    if (!settings->use_manual_layout) {
        // Only if not in manual mode
        render_section_separator(t, settings, current_y, section_title, text_color,
                                 completed_count, total_visible_count, -1, -1);
    }


    // --- Calculate Uniform Item Width (based on items that will be rendered) ---
    // This calculation remains the same regardless of zoom to maintain consistent spacing logic
    float uniform_item_width = 0.0f;
    const float horizontal_spacing = 8.0f; // Define the default spacing

    // Check if custom width is enabled for THIS Section (Unlocks)
    TrackerSection section_id = SECTION_UNLOCKS;
    if (settings->tracker_section_custom_width_enabled[section_id]) {
        // Use fixed width from settings
        uniform_item_width = settings->tracker_section_custom_item_width[section_id];
        if (uniform_item_width < 96.0f) uniform_item_width = 96.0f;
    } else {
        // Dynamic Width
        for (int i = 0; i < count; i++) {
            TrackableItem *item = items[i];
            if (!item) continue;

            // Apply rendering hiding logic
            bool should_hide_width = false;
            switch (settings->goal_hiding_mode) {
                case HIDE_ALL_COMPLETED:
                    should_hide_width = (!settings->use_manual_layout && item->is_hidden) || item->done;
                    break;
                case HIDE_ONLY_TEMPLATE_HIDDEN:
                    should_hide_width = !settings->use_manual_layout && item->is_hidden;
                    break;
                case SHOW_ALL:
                    should_hide_width = false;
                    break;
            }

            // Apply search filter
            bool matches_search_width = item_matches_search(item, t->search_buffer)
                                        || s_linked_top.count(item->root_name);

            // Only consider items that will actually be rendered for width calculation
            if (!should_hide_width && matches_search_width) {
                // Calculate width needed for text
                SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize); // Use macro
                ImVec2 text_size_calc = ImGui::CalcTextSize(item->display_name);
                RESET_FONT_SCALE(); // Reset scale

                uniform_item_width = fmaxf(uniform_item_width, fmaxf(96.0f, text_size_calc.x));
            }
        }
        // Add default spacing ONLY in dynamic mode, 8 pixels
        uniform_item_width += horizontal_spacing;
    }

    // --- GLOBAL HYBRID SHIFT ---
    float padding = 50.0f;
    if (settings->use_manual_layout) {
        padding = get_global_safe_x(t);

        // Prevent auto-layout items from squeezing into a single column!
        // We force the wrapping width to allow at least 3 items to fit side-by-side
        float min_wrapping_width = padding + (uniform_item_width * 3.0f);
        if (wrapping_width < min_wrapping_width) {
            wrapping_width = min_wrapping_width;
        }
    }
    float current_x = padding, row_max_height = 0.0f;

    // Adjust vertical spacing
    const float vertical_spacing = settings->tracker_vertical_spacing; // Changed from 16.0f

    // --- Rendering Loop ---
    for (int i = 0; i < count; i++) {
        TrackableItem *item = items[i];
        if (!item) continue;

        // Apply rendering hiding logic
        bool should_hide_render = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_render = (!settings->use_manual_layout && item->is_hidden) || item->done;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = !settings->use_manual_layout && item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Apply search filter
        bool matches_search_render = item_matches_search(item, t->search_buffer)
                                     || s_linked_top.count(item->root_name);

        // Skip rendering if hidden or doesn't match search
        if (should_hide_render || !matches_search_render) {
            continue;
        }


        // --- Item Height and Layout (OPTIMIZED) ---
        // Construct progress text to determine if it exists, without calculating size yet
        char progress_text[32] = "";
        bool has_progress_text = false;
        if (item->goal > 0) {
            snprintf(progress_text, sizeof(progress_text), "(%d / %d)", item->progress, item->goal);
            has_progress_text = true;
        }
        // For infinite counters, only show progress if not manually overridden/done
        else if (item->goal == -1 && !item->done) {
            snprintf(progress_text, sizeof(progress_text), "(%d)", item->progress);
            has_progress_text = true;
        }

        // Calculate height using Math: Icon bg (96) + main text height + padding (4) + [progress text height + padding (4)]
        float item_height = 96.0f + main_text_line_height + 4.0f;
        if (has_progress_text) {
            item_height += sub_text_line_height + 4.0f;
        }

        // Skip auto-layout items that are fully hidden in manual layout
        if (settings->use_manual_layout && !item->icon_pos.is_set &&
            item->icon_pos.is_hidden_in_layout && item->text_pos.is_hidden_in_layout &&
            (!has_progress_text || item->progress_pos.is_hidden_in_layout) && settings->goal_hiding_mode != SHOW_ALL) {
            continue;
        }

        float item_x = current_x;
        float item_y = current_y;

        if (settings->use_manual_layout && item->icon_pos.is_set) {
            ImVec2 anchor_off = get_anchor_offset(item->icon_pos.anchor, 96.0f, 96.0f);
            item_x = item->icon_pos.x + anchor_off.x;
            item_y = item->icon_pos.y + anchor_off.y;
        } else {
            // Procedural Auto-Layout Wrapping
            if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
                current_x = padding;
                current_y += row_max_height;
                row_max_height = 0.0f;
                item_x = current_x;
                item_y = current_y;
            }
            // Update procedural layout cursors
            current_x += uniform_item_width;
            row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
        }

        ImVec2 screen_pos = ImVec2((item_x * t->zoom_level) + t->camera_offset.x,
                                   (item_y * t->zoom_level) + t->camera_offset.y);

        // --- Culling Logic ---
        ImVec2 item_size_on_screen = ImVec2(uniform_item_width * t->zoom_level, item_height * t->zoom_level);
        bool is_visible_on_screen = !(screen_pos.x > io.DisplaySize.x || (screen_pos.x + item_size_on_screen.x) < 0 ||
                                      screen_pos.y > io.DisplaySize.y || (screen_pos.y + item_size_on_screen.y) < 0);

        // Disable coarse parent culling in manual layout so detached text/criteria don't vanish
        if (settings->use_manual_layout) {
            is_visible_on_screen = true;
        }


        // Per-position hiding for manual layout
        bool hide_item_icon_in_layout = settings->use_manual_layout && item->icon_pos.is_hidden_in_layout && settings->
                                        goal_hiding_mode != SHOW_ALL;
        bool hide_item_text_in_layout = settings->use_manual_layout && item->text_pos.is_hidden_in_layout && settings->
                                        goal_hiding_mode != SHOW_ALL;
        bool hide_item_progress_in_layout = settings->use_manual_layout && item->progress_pos.is_hidden_in_layout &&
                                            settings->goal_hiding_mode != SHOW_ALL;

        // --- Rendering Core Logic ---
        if (is_visible_on_screen) {
            // --- Calculate Text Sizes for Centering (Only if visible) ---
            SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize);
            ImVec2 text_size = ImGui::CalcTextSize(item->display_name);
            RESET_FONT_SCALE();

            ImVec2 progress_text_size = ImVec2(0, 0);
            if (has_progress_text) {
                SET_FONT_SCALE(settings->tracker_sub_font_size, t->tracker_font->LegacySize);
                progress_text_size = ImGui::CalcTextSize(progress_text);
                RESET_FONT_SCALE();
            }

            ImVec2 bg_size = ImVec2(96.0f, 96.0f);

            // Select texture *pair* and render
            SDL_Texture *static_bg = item->done ? t->adv_bg_done : t->adv_bg;
            AnimatedTexture *anim_bg = item->done ? t->adv_bg_done_anim : t->adv_bg_anim;

            SDL_Texture *texture_to_draw = static_bg;
            if (anim_bg && anim_bg->frame_count > 0) {
                // Standard GIF Frame Selection Logic
                if (anim_bg->delays && anim_bg->total_duration > 0) {
                    Uint32 current_ticks = SDL_GetTicks();
                    Uint32 elapsed_time = current_ticks % anim_bg->total_duration;
                    int current_frame = 0;
                    Uint32 time_sum = 0;
                    for (int frame_idx = 0; frame_idx < anim_bg->frame_count; ++frame_idx) {
                        time_sum += anim_bg->delays[frame_idx];
                        if (elapsed_time < time_sum) {
                            current_frame = frame_idx;
                            break;
                        }
                    }
                    texture_to_draw = anim_bg->frames[current_frame];
                } else {
                    texture_to_draw = anim_bg->frames[0];
                }
            }

            // Render Background
            if (!hide_item_icon_in_layout && texture_to_draw)
                draw_list->AddImage((void *) texture_to_draw, screen_pos,
                                    ImVec2(screen_pos.x + bg_size.x * t->zoom_level,
                                           screen_pos.y + bg_size.y * t->zoom_level));

            // Render Icon (Animated or Static)
            // --- Start GIF Frame Selection Logic ---
            if (item->anim_texture && item->anim_texture->frame_count > 0) {
                if (item->anim_texture->delays && item->anim_texture->total_duration > 0) {
                    Uint32 current_ticks = SDL_GetTicks();
                    Uint32 elapsed_time = current_ticks % item->anim_texture->total_duration;
                    int current_frame = 0;
                    Uint32 time_sum = 0;
                    for (int frame_idx = 0; frame_idx < item->anim_texture->frame_count; ++frame_idx) {
                        time_sum += item->anim_texture->delays[frame_idx];
                        if (elapsed_time < time_sum) {
                            current_frame = frame_idx;
                            break;
                        }
                    }
                    texture_to_draw = item->anim_texture->frames[current_frame];
                } else {
                    // Fallback if no timing info
                    texture_to_draw = item->anim_texture->frames[0];
                }
            } else if (item->texture) {
                // Static texture
                texture_to_draw = item->texture;
            }
            // --- End GIF Frame Selection Logic ---

            if (texture_to_draw) {
                // --- Start Icon Scaling and Centering Logic (64x64 box) ---
                float tex_w = 0.0f, tex_h = 0.0f;
                SDL_GetTextureSize(texture_to_draw, &tex_w, &tex_h);
                ImVec2 target_box_size = ImVec2(64.0f * t->zoom_level, 64.0f * t->zoom_level);
                // Target box size on screen
                float scale_factor = 1.0f;
                if (tex_w > 0 && tex_h > 0) {
                    // Avoid division by zero
                    scale_factor = fminf(target_box_size.x / tex_w, target_box_size.y / tex_h);
                }
                ImVec2 scaled_size = ImVec2(tex_w * scale_factor, tex_h * scale_factor); // Scaled size on screen
                ImVec2 box_p_min = ImVec2(screen_pos.x + 16.0f * t->zoom_level, screen_pos.y + 16.0f * t->zoom_level);
                // 16.0f to CENTER the icons
                // Top-left of 64x64 box on screen
                ImVec2 icon_padding = ImVec2((target_box_size.x - scaled_size.x) * 0.5f,
                                             (target_box_size.y - scaled_size.y) * 0.5f); // Padding within the box
                ImVec2 p_min = ImVec2(box_p_min.x + icon_padding.x, box_p_min.y + icon_padding.y);
                // Final top-left for drawing
                ImVec2 p_max = ImVec2(p_min.x + scaled_size.x, p_min.y + scaled_size.y);
                // Final bottom-right for drawing
                if (!hide_item_icon_in_layout)
                    draw_list->AddImage((void *) texture_to_draw, p_min, p_max);
                // --- End Icon Scaling and Centering Logic ---
            }

            // --- VISUAL LAYOUT DRAGGING (ICON) ---
            char drag_id[256];
            snprintf(drag_id, sizeof(drag_id), "drag_unlock_icon_%s", item->root_name);
            handle_visual_layout_dragging(t, drag_id, screen_pos,
                                          ImVec2(bg_size.x * t->zoom_level, bg_size.y * t->zoom_level),
                                          item->icon_pos, "Unlock", item->display_name, "Icon",
                                          item->root_name);

            // Render Text
            // LOD: Check if zoom level is sufficient for text
            if (t->zoom_level > LOD_TEXT_MAIN_THRESHOLD && !hide_item_text_in_layout) {
                float main_text_size = settings->tracker_font_size;
                float sub_font_size = settings->tracker_sub_font_size;
                ImU32 current_text_color = item->done ? text_color_faded : text_color; // Fade if done

                // --- TEXT CENTERING AND POSITIONING ---
                float text_x_center = screen_pos.x + (bg_size.x * t->zoom_level) * 0.5f;
                float text_y_pos = screen_pos.y + bg_size.y * t->zoom_level + (4.0f * t->zoom_level);

                if (settings->use_manual_layout && item->text_pos.is_set) {
                    ImVec2 text_anchor_off = get_anchor_offset(item->text_pos.anchor, text_size.x, text_size.y);
                    text_x_center = ((item->text_pos.x + text_anchor_off.x) * t->zoom_level) + t->camera_offset.x + (
                                        text_size.x * t->zoom_level) * 0.5f;
                    text_y_pos = ((item->text_pos.y + text_anchor_off.y) * t->zoom_level) + t->camera_offset.y;
                }

                // Draw Main Name (centered)
                draw_list->AddText(nullptr, main_text_size * t->zoom_level,
                                   ImVec2(text_x_center - (text_size.x * t->zoom_level) * 0.5f, text_y_pos),
                                   current_text_color, item->display_name);

                // --- VISUAL LAYOUT DRAGGING (TEXT) ---
                snprintf(drag_id, sizeof(drag_id), "drag_unlock_text_%s", item->root_name);
                handle_visual_layout_dragging(t, drag_id,
                                              ImVec2(text_x_center - (text_size.x * t->zoom_level) * 0.5f, text_y_pos),
                                              ImVec2(text_size.x * t->zoom_level, text_size.y * t->zoom_level),
                                              item->text_pos, "Unlock", item->display_name, "Text",
                                              item->root_name);

                // Draw Progress Text below main name (if applicable, centered)
                if (has_progress_text && !hide_item_progress_in_layout) {
                    text_y_pos += text_size.y * t->zoom_level + 4.0f * t->zoom_level; // Move Y down

                    // LOD: Hide progress text if zoomed out
                    if (t->zoom_level > LOD_TEXT_SUB_THRESHOLD) {
                        draw_list->AddText(nullptr, sub_font_size * t->zoom_level,
                                           ImVec2(text_x_center - (progress_text_size.x * t->zoom_level) * 0.5f,
                                                  text_y_pos),
                                           current_text_color, progress_text);
                    }
                }
            }
            // NO Checkboxes rendered here
        } // End if (is_visible_on_screen)
    }
    // --- End Rendering Loop ---

    // Update vertical position for the next section
    current_y += row_max_height;
}

/**
 * @brief Renders the Custom Goals section with interactive checkboxes.
 * Calculates and displays completion counters based on visibility settings.
 */
static void render_custom_goals_section(Tracker *t, const AppSettings *settings, float &current_y,
                                        const char *section_title) {
    // LOD Thresholds
    const float LOD_TEXT_SUB_THRESHOLD = settings->lod_text_sub_threshold; // Hide Progress Text
    const float LOD_TEXT_MAIN_THRESHOLD = settings->lod_text_main_threshold; // Hide Main Name & Checkboxes

    // Pre-calculate line heights once per frame (Optimization)
    const float main_text_line_height = settings->tracker_font_size;
    const float sub_text_line_height = settings->tracker_sub_font_size;

    int count = t->template_data->custom_goal_count;

    // --- Pre-computation and Filtering for Counters ---
    int total_visible_count = 0;
    int completed_count = 0;

    for (int i = 0; i < count; ++i) {
        TrackableItem *item = t->template_data->custom_goals[i];
        if (!item) continue;

        // Determine completion status (simple boolean 'done')
        bool is_item_considered_complete = item->done;

        // Determine visibility based on hiding mode
        bool should_hide_based_on_mode = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_based_on_mode = (!settings->use_manual_layout && item->is_hidden) ||
                                            is_item_considered_complete;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_based_on_mode = !settings->use_manual_layout && item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_based_on_mode = false;
                break;
        }

        // Apply Search Filter for counting
        bool matches_search = item_matches_search(item, t->search_buffer)
                              || s_linked_top.count(item->root_name);

        // Count items only if they are not hidden by mode AND match the search
        if (!should_hide_based_on_mode && matches_search) {
            total_visible_count++;
            if (is_item_considered_complete) {
                completed_count++;
            }
        }
    }
    // --- End of Counter Calculation ---


    // --- Section Rendering ---

    // Check if *anything* should be rendered based on search + hiding
    bool section_has_renderable_content = false;
    for (int i = 0; i < count; ++i) {
        TrackableItem *item = t->template_data->custom_goals[i];
        if (!item) continue;

        bool should_hide_render = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_render = (!settings->use_manual_layout && item->is_hidden) || item->done;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = !settings->use_manual_layout && item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Combine hiding and search filter
        if (!should_hide_render && (item_matches_search(item, t->search_buffer)
                                    || s_linked_top.count(item->root_name))) {
            section_has_renderable_content = true;
            break; // Found at least one item to render
        }
    }

    if (!section_has_renderable_content) return; // Hide section if no items match filters


    ImGuiIO &io = ImGui::GetIO();

    // Use locked width if layout is locked
    float wrapping_width = t->layout_locked ? t->locked_layout_width : (io.DisplaySize.x / t->zoom_level);


    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 text_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                settings->text_color.a);
    ImU32 text_color_faded = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                      ADVANCELY_FADED_ALPHA); // Faded text color

    // Define checkbox colors from settings
    ImU32 checkmark_color = text_color;
    ImU32 checkbox_fill_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                         ADVANCELY_FADED_ALPHA);
    ImU32 checkbox_hover_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                          (int)fminf(255.0f, ADVANCELY_FADED_ALPHA + 60));
    float scale_factor; // Declare scale_factor here

    // Call separator with calculated counts (sub-counts are -1)
    if (!settings->use_manual_layout) {
        // Only if not in manual mode
        render_section_separator(t, settings, current_y, section_title, text_color,
                                 completed_count, total_visible_count, -1, -1);
    }


    // --- Calculate Uniform Item Width (based on items that will be rendered) ---
    // IMPORTANT: Width calculation logic must remain consistent regardless of LOD.
    float uniform_item_width = 0.0f;
    const float horizontal_spacing = 8.0f; // Define the default spacing

    // Check if custom width is enabled for THIS section (Custom)
    TrackerSection section_id = SECTION_CUSTOM;
    if (settings->tracker_section_custom_width_enabled[section_id]) {
        // Use fixed width from settings
        uniform_item_width = settings->tracker_section_custom_item_width[section_id];
        if (uniform_item_width < 96.0f) uniform_item_width = 96.0f;
    } else {
        // Dynamic width
        for (int i = 0; i < count; i++) {
            TrackableItem *item = t->template_data->custom_goals[i];
            if (!item) continue;

            // Apply rendering hiding logic
            bool should_hide_width = false;
            switch (settings->goal_hiding_mode) {
                case HIDE_ALL_COMPLETED:
                    should_hide_width = (!settings->use_manual_layout && item->is_hidden) || item->done;
                    break;
                case HIDE_ONLY_TEMPLATE_HIDDEN:
                    should_hide_width = !settings->use_manual_layout && item->is_hidden;
                    break;
                case SHOW_ALL:
                    should_hide_width = false;
                    break;
            }

            // Apply search filter
            bool matches_search_width = item_matches_search(item, t->search_buffer)
                                        || s_linked_top.count(item->root_name);

            // Only consider items that will actually be rendered for width calculation
            if (!should_hide_width && matches_search_width) {
                // Calculate width needed for text + progress text if applicable
                SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize);
                float text_width = ImGui::CalcTextSize(item->display_name).x;
                RESET_FONT_SCALE();

                char progress_text_width_calc[32] = "";
                if (item->goal > 0) {
                    snprintf(progress_text_width_calc, sizeof(progress_text_width_calc), "(%d / %d)", item->progress,
                             item->goal);
                } else if (item->goal == -1 && !item->done) {
                    snprintf(progress_text_width_calc, sizeof(progress_text_width_calc), "(%d)", item->progress);
                }

                // Scale for progress text width calculation
                SET_FONT_SCALE(settings->tracker_sub_font_size, t->tracker_font->LegacySize);
                float progress_width = ImGui::CalcTextSize(progress_text_width_calc).x;
                RESET_FONT_SCALE();

                // The required width is the max of the main text width and the progress text width (as they are on separate lines)
                float required_text_width = fmaxf(text_width, progress_width);
                // Ensure minimum width accommodates the 96px background
                uniform_item_width = fmaxf(uniform_item_width, fmaxf(96.0f, required_text_width));
            }
        }
        // Add default spacing ONLY in dynamic mode, 8 pixels
        uniform_item_width += horizontal_spacing;
    }
    // --- End of Width Calculation ---

    // --- GLOBAL HYBRID SHIFT ---
    float padding = 50.0f;
    if (settings->use_manual_layout) {
        padding = get_global_safe_x(t);

        // Prevent auto-layout items from squeezing into a single column!
        // We force the wrapping width to allow at least 3 items to fit side-by-side
        float min_wrapping_width = padding + (uniform_item_width * 3.0f);
        if (wrapping_width < min_wrapping_width) {
            wrapping_width = min_wrapping_width;
        }
    }
    float current_x = padding, row_max_height = 0.0f;

    // Adjust vertical spacing
    const float vertical_spacing = settings->tracker_vertical_spacing; // Changed from 16.0f


    // --- Rendering Loop ---
    for (int i = 0; i < count; i++) {
        TrackableItem *item = t->template_data->custom_goals[i];
        if (!item) continue;

        // Apply rendering hiding logic
        bool should_hide_render = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_render = (!settings->use_manual_layout && item->is_hidden) || item->done;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = !settings->use_manual_layout && item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Apply search filter
        bool matches_search_render = item_matches_search(item, t->search_buffer)
                                     || s_linked_top.count(item->root_name);

        // Skip rendering if hidden or doesn't match search
        if (should_hide_render || !matches_search_render) {
            continue;
        }


        // --- Item Height and Layout (OPTIMIZED) ---
        // Construct progress text string to determine if it exists
        char progress_text[32] = "";
        bool has_progress_text = false;
        if (item->goal > 0) {
            snprintf(progress_text, sizeof(progress_text), "(%d / %d)", item->progress, item->goal);
            has_progress_text = true;
        } else if (item->goal == -1 && !item->done) {
            snprintf(progress_text, sizeof(progress_text), "(%d)", item->progress);
            has_progress_text = true;
        }

        // Calculate height using Math: Icon bg (96) + main text height + padding (4) + [progress text height + padding (4)]
        float item_height = 96.0f + main_text_line_height + 4.0f;
        if (has_progress_text) {
            item_height += sub_text_line_height + 4.0f;
        }

        // Skip auto-layout items that are fully hidden in manual layout
        if (settings->use_manual_layout && !item->icon_pos.is_set &&
            item->icon_pos.is_hidden_in_layout && item->text_pos.is_hidden_in_layout &&
            (!has_progress_text || item->progress_pos.is_hidden_in_layout) && settings->goal_hiding_mode != SHOW_ALL) {
            continue;
        }

        float item_x = current_x;
        float item_y = current_y;

        if (settings->use_manual_layout && item->icon_pos.is_set) {
            ImVec2 anchor_off = get_anchor_offset(item->icon_pos.anchor, 96.0f, 96.0f);
            item_x = item->icon_pos.x + anchor_off.x;
            item_y = item->icon_pos.y + anchor_off.y;
        } else {
            // Procedural Auto-Layout Wrapping
            if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
                current_x = padding;
                current_y += row_max_height;
                row_max_height = 0.0f;
                item_x = current_x;
                item_y = current_y;
            }
            // Update procedural layout cursors
            current_x += uniform_item_width;
            row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
        }

        ImVec2 screen_pos = ImVec2((item_x * t->zoom_level) + t->camera_offset.x,
                                   (item_y * t->zoom_level) + t->camera_offset.y);

        // --- Culling Logic ---
        ImVec2 item_size_on_screen = ImVec2(uniform_item_width * t->zoom_level, item_height * t->zoom_level);
        bool is_visible_on_screen = !(screen_pos.x > io.DisplaySize.x || (screen_pos.x + item_size_on_screen.x) < 0 ||
                                      screen_pos.y > io.DisplaySize.y || (screen_pos.y + item_size_on_screen.y) < 0);

        // Disable coarse parent culling in manual layout so detached text/criteria don't vanish
        if (settings->use_manual_layout) {
            is_visible_on_screen = true;
        }


        // Per-position hiding for manual layout
        bool hide_item_icon_in_layout = settings->use_manual_layout && item->icon_pos.is_hidden_in_layout && settings->
                                        goal_hiding_mode != SHOW_ALL;
        bool hide_item_text_in_layout = settings->use_manual_layout && item->text_pos.is_hidden_in_layout && settings->
                                        goal_hiding_mode != SHOW_ALL;
        bool hide_item_progress_in_layout = settings->use_manual_layout && item->progress_pos.is_hidden_in_layout &&
                                            settings->goal_hiding_mode != SHOW_ALL;

        // --- Rendering Core Logic ---
        if (is_visible_on_screen) {
            // --- Calculate Text Sizes for Centering (Only if visible) ---
            SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize);
            ImVec2 text_size = ImGui::CalcTextSize(item->display_name);
            RESET_FONT_SCALE();

            ImVec2 progress_text_size = ImVec2(0, 0);
            if (has_progress_text) {
                SET_FONT_SCALE(settings->tracker_sub_font_size, t->tracker_font->LegacySize);
                progress_text_size = ImGui::CalcTextSize(progress_text);
                RESET_FONT_SCALE();
            }

            ImVec2 bg_size = ImVec2(96.0f, 96.0f);

            // Select texture *pair* and render
            SDL_Texture *static_bg = t->adv_bg;
            AnimatedTexture *anim_bg = t->adv_bg_anim;

            if (item->done) {
                static_bg = t->adv_bg_done;
                anim_bg = t->adv_bg_done_anim;
            } else if ((item->goal > 0 || item->goal == -1) && item->progress > 0) {
                static_bg = t->adv_bg_half_done;
                anim_bg = t->adv_bg_half_done_anim;
            }

            SDL_Texture *texture_to_draw = static_bg;
            if (anim_bg && anim_bg->frame_count > 0) {
                // Standard GIF Frame Selection Logic
                if (anim_bg->delays && anim_bg->total_duration > 0) {
                    Uint32 current_ticks = SDL_GetTicks();
                    Uint32 elapsed_time = current_ticks % anim_bg->total_duration;
                    int current_frame = 0;
                    Uint32 time_sum = 0;
                    for (int frame_idx = 0; frame_idx < anim_bg->frame_count; ++frame_idx) {
                        time_sum += anim_bg->delays[frame_idx];
                        if (elapsed_time < time_sum) {
                            current_frame = frame_idx;
                            break;
                        }
                    }
                    texture_to_draw = anim_bg->frames[current_frame];
                } else {
                    texture_to_draw = anim_bg->frames[0];
                }
            }

            // Render Background (Always Visible)
            if (!hide_item_icon_in_layout && texture_to_draw)
                draw_list->AddImage((void *) texture_to_draw, screen_pos,
                                    ImVec2(screen_pos.x + bg_size.x * t->zoom_level,
                                           screen_pos.y + bg_size.y * t->zoom_level));

            // Render Icon (Animated or Static) - Always Visible (Detailed)
            // --- Start GIF Frame Selection Logic ---
            if (item->anim_texture && item->anim_texture->frame_count > 0) {
                if (item->anim_texture->delays && item->anim_texture->total_duration > 0) {
                    Uint32 current_ticks = SDL_GetTicks();
                    Uint32 elapsed_time = current_ticks % item->anim_texture->total_duration;
                    int current_frame = 0;
                    Uint32 time_sum = 0;
                    for (int frame_idx = 0; frame_idx < item->anim_texture->frame_count; ++frame_idx) {
                        time_sum += item->anim_texture->delays[frame_idx];
                        if (elapsed_time < time_sum) {
                            current_frame = frame_idx;
                            break;
                        }
                    }
                    texture_to_draw = item->anim_texture->frames[current_frame];
                } else {
                    // Fallback if no timing info
                    texture_to_draw = item->anim_texture->frames[0];
                }
            } else if (item->texture) {
                // Static texture
                texture_to_draw = item->texture;
            }
            // --- End GIF Frame Selection Logic ---

            if (texture_to_draw) {
                // --- Start Icon Scaling and Centering Logic (64x64 box) ---
                float tex_w = 0.0f, tex_h = 0.0f;
                SDL_GetTextureSize(texture_to_draw, &tex_w, &tex_h);
                ImVec2 target_box_size = ImVec2(64.0f * t->zoom_level, 64.0f * t->zoom_level);
                // Target box size on screen
                float scale_factor = 1.0f;
                if (tex_w > 0 && tex_h > 0) {
                    // Avoid division by zero
                    scale_factor = fminf(target_box_size.x / tex_w, target_box_size.y / tex_h);
                }
                ImVec2 scaled_size = ImVec2(tex_w * scale_factor, tex_h * scale_factor); // Scaled size on screen
                ImVec2 box_p_min = ImVec2(screen_pos.x + 16.0f * t->zoom_level, screen_pos.y + 16.0f * t->zoom_level);
                // 16.0f to CENTER the icons
                // Top-left of 64x64 box on screen
                ImVec2 icon_padding = ImVec2((target_box_size.x - scaled_size.x) * 0.5f,
                                             (target_box_size.y - scaled_size.y) * 0.5f); // Padding within the box
                ImVec2 p_min = ImVec2(box_p_min.x + icon_padding.x, box_p_min.y + icon_padding.y);
                // Final top-left for drawing
                ImVec2 p_max = ImVec2(p_min.x + scaled_size.x, p_min.y + scaled_size.y);
                // Final bottom-right for drawing
                if (!hide_item_icon_in_layout)
                    draw_list->AddImage((void *) texture_to_draw, p_min, p_max);
                // --- End Icon Scaling and Centering Logic ---
            }

            // --- VISUAL LAYOUT DRAGGING (ICON) ---
            char drag_id[256];
            snprintf(drag_id, sizeof(drag_id), "drag_cg_icon_%s", item->root_name);
            handle_visual_layout_dragging(t, drag_id, screen_pos,
                                          ImVec2(bg_size.x * t->zoom_level, bg_size.y * t->zoom_level),
                                          item->icon_pos, "Custom Goal", item->display_name, "Icon",
                                          item->root_name);

            // Render Text
            // LOD: Check if zoom level is sufficient for text
            if (t->zoom_level > LOD_TEXT_MAIN_THRESHOLD && !hide_item_text_in_layout) {
                float main_text_size = settings->tracker_font_size;
                float sub_font_size = settings->tracker_sub_font_size;
                ImU32 current_text_color = item->done ? text_color_faded : text_color; // Fade if done

                // --- TEXT CENTERING AND POSITIONING ---
                float text_x_center = screen_pos.x + (bg_size.x * t->zoom_level) * 0.5f;
                float text_y_pos = screen_pos.y + bg_size.y * t->zoom_level + (4.0f * t->zoom_level);

                if (settings->use_manual_layout && item->text_pos.is_set) {
                    ImVec2 text_anchor_off = get_anchor_offset(item->text_pos.anchor, text_size.x, text_size.y);
                    text_x_center = ((item->text_pos.x + text_anchor_off.x) * t->zoom_level) + t->camera_offset.x + (
                                        text_size.x * t->zoom_level) * 0.5f;
                    text_y_pos = ((item->text_pos.y + text_anchor_off.y) * t->zoom_level) + t->camera_offset.y;
                }

                // Draw Main Name (centered)
                draw_list->AddText(nullptr, main_text_size * t->zoom_level,
                                   ImVec2(text_x_center - (text_size.x * t->zoom_level) * 0.5f, text_y_pos),
                                   current_text_color, item->display_name);

                // --- VISUAL LAYOUT DRAGGING (TEXT) ---
                snprintf(drag_id, sizeof(drag_id), "drag_cg_text_%s", item->root_name);
                handle_visual_layout_dragging(t, drag_id,
                                              ImVec2(text_x_center - (text_size.x * t->zoom_level) * 0.5f, text_y_pos),
                                              ImVec2(text_size.x * t->zoom_level, text_size.y * t->zoom_level),
                                              item->text_pos, "Custom Goal", item->display_name, "Text",
                                              item->root_name);

                // Draw Progress Text below main name (if applicable, centered)
                if (has_progress_text && !hide_item_progress_in_layout) {
                    float prog_x_center = text_x_center;
                    float prog_y = text_y_pos + text_size.y * t->zoom_level + 4.0f * t->zoom_level;

                    // Apply manual progress_pos if set
                    if (settings->use_manual_layout && item->progress_pos.is_set) {
                        ImVec2 prog_anchor_off = get_anchor_offset(item->progress_pos.anchor, progress_text_size.x,
                                                                   progress_text_size.y);
                        prog_x_center = ((item->progress_pos.x + prog_anchor_off.x) * t->zoom_level) + t->camera_offset.
                                        x + (
                                            progress_text_size.x * t->zoom_level) * 0.5f;
                        prog_y = ((item->progress_pos.y + prog_anchor_off.y) * t->zoom_level) + t->camera_offset.y;
                    }

                    // LOD: Hide progress text if zoomed out
                    if (t->zoom_level > LOD_TEXT_SUB_THRESHOLD) {
                        draw_list->AddText(nullptr, sub_font_size * t->zoom_level,
                                           ImVec2(prog_x_center - (progress_text_size.x * t->zoom_level) * 0.5f,
                                                  prog_y),
                                           current_text_color, progress_text);

                        // --- VISUAL LAYOUT DRAGGING (CG PROGRESS) ---
                        char drag_id[256];
                        snprintf(drag_id, sizeof(drag_id), "drag_cg_prog_%s", item->root_name);
                        handle_visual_layout_dragging(t, drag_id,
                                                      ImVec2(
                                                          prog_x_center - (progress_text_size.x * t->zoom_level) * 0.5f,
                                                          prog_y),
                                                      ImVec2(progress_text_size.x * t->zoom_level,
                                                             progress_text_size.y * t->zoom_level),
                                                      item->progress_pos, "Custom Goal", item->display_name, "Progress",
                                                      item->root_name);
                        // --------------------------------------------
                    }
                }
            }

            // --- Checkbox logic for manual override ---
            // Allow override for simple toggles (goal <= 0) OR infinite counters (goal == -1)
            bool can_be_overridden = (item->goal <= 0 || item->goal == -1);

            // LOD: Hide Checkbox if zoomed out too far (tied to MAIN text)
            if (can_be_overridden && t->zoom_level > LOD_TEXT_MAIN_THRESHOLD) {
                ImVec2 check_pos = ImVec2(screen_pos.x + 4 * t->zoom_level, screen_pos.y + 4 * t->zoom_level);
                // Top-right corner
                ImVec2 check_size = ImVec2(20 * t->zoom_level, 20 * t->zoom_level);
                ImRect checkbox_rect(check_pos, ImVec2(check_pos.x + check_size.x, check_pos.y + check_size.y));

                bool is_hovered = ImGui::IsMouseHoveringRect(checkbox_rect.Min, checkbox_rect.Max);
                ImU32 check_fill_color = is_hovered ? checkbox_hover_color : checkbox_fill_color;
                draw_list->AddRectFilled(checkbox_rect.Min, checkbox_rect.Max, check_fill_color, 3.0f * t->zoom_level);
                draw_list->AddRect(checkbox_rect.Min, checkbox_rect.Max, text_color,
                                   3.0f * t->zoom_level);

                if (item->is_manually_completed) {
                    // Draw checkmark only when manually completed; auto-completion via linked goals does not place it
                    ImVec2 p1 = ImVec2(check_pos.x + 5 * t->zoom_level, check_pos.y + 10 * t->zoom_level);
                    ImVec2 p2 = ImVec2(check_pos.x + 9 * t->zoom_level, check_pos.y + 15 * t->zoom_level);
                    ImVec2 p3 = ImVec2(check_pos.x + 15 * t->zoom_level, check_pos.y + 6 * t->zoom_level);
                    draw_list->AddLine(p1, p2, checkmark_color, 2.0f * t->zoom_level);
                    draw_list->AddLine(p2, p3, checkmark_color, 2.0f * t->zoom_level);
                }

                // Handle click interaction
                // Deactivating when in visual layout editor mode
                // Co-op: Receivers respect custom goal permission
                // Co-op: Show tooltip for host-only custom goals
                bool rcv_in_lobby = (settings->network_mode == NETWORK_RECEIVER &&
                    g_coop_ctx && coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED);
                if (is_hovered && rcv_in_lobby &&
                    settings->coop_custom_goal_mode == COOP_CUSTOM_HOST_ONLY) {
                    char tooltip_buf[128];
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Custom goals are set to Host Only.");
                    ImGui::SetTooltip("%s", tooltip_buf);
                }

                if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !t->is_visual_layout_editing) {
                    if (rcv_in_lobby &&
                        settings->coop_custom_goal_mode == COOP_CUSTOM_HOST_ONLY) {
                        // Host-only mode: clicking disabled for receivers
                    } else if (rcv_in_lobby &&
                               settings->coop_custom_goal_mode == COOP_CUSTOM_ANY_PLAYER) {
                        // Any-player mode: send toggle request to host
                        CoopCustomGoalModMsg mod = {};
                        snprintf(mod.goal_root_name, sizeof(mod.goal_root_name), "%s", item->root_name);
                        mod.parent_root_name[0] = '\0';
                        mod.action = COOP_MOD_TOGGLE;
                        coop_net_send_custom_goal_mod(g_coop_ctx, &mod);
                    } else {
                        // Host or singleplayer: toggle locally
                        item->is_manually_completed = !item->is_manually_completed;
                        if (item->is_manually_completed) {
                            item->done = true;
                        } else {
                            item->done = (item->linked_goal_count > 0 &&
                                          check_linked_goals_satisfied(t->template_data, item->linked_goals,
                                                                       item->linked_goal_count,
                                                                       item->linked_goal_mode));
                        }
                        item->progress = item->done ? 1 : 0;
                        SDL_SetAtomicInt(&g_suppress_settings_watch, 1);
                        settings_save(settings, t->template_data, SAVE_CONTEXT_ALL);
                        // Lightweight broadcast path: no full file re-merge needed for custom goals
                        SDL_SetAtomicInt(&g_coop_broadcast_needed, 1);
                        SDL_SetAtomicInt(&g_game_data_changed, 1);
                    }
                }
            } // End if (can_be_overridden && visible)
        } // End if (is_visible_on_screen)
    }
    // --- End Rendering Loop ---

    // Update vertical position for the next section
    current_y += row_max_height;
}

/**
 * @brief Renders the Counter Goals section.
 * Displays big icon, display name, and progress (completed/total) with bg textures based on state.
 */
static void render_counter_goals_section(Tracker *t, const AppSettings *settings, float &current_y,
                                         const char *section_title) {
    const float LOD_TEXT_SUB_THRESHOLD = settings->lod_text_sub_threshold;
    const float LOD_TEXT_MAIN_THRESHOLD = settings->lod_text_main_threshold;

    const float main_text_line_height = settings->tracker_font_size;
    const float sub_text_line_height = settings->tracker_sub_font_size;
    float scale_factor = 1.0f;

    int count = t->template_data->counter_goal_count;

    // --- Pre-computation ---
    int total_visible_count = 0;
    int completed_count = 0;

    for (int i = 0; i < count; ++i) {
        CounterGoal *goal = t->template_data->counter_goals[i];
        if (!goal) continue;

        bool should_hide_based_on_mode = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_based_on_mode = (!settings->use_manual_layout && goal->is_hidden) || goal->done;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_based_on_mode = !settings->use_manual_layout && goal->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_based_on_mode = false;
                break;
        }

        bool matches_search = counter_matches_search(goal, t->search_buffer)
                              || s_linked_top.count(goal->root_name);
        if (!should_hide_based_on_mode && matches_search) {
            total_visible_count++;
            if (goal->done) completed_count++;
        }
    }

    // Check if section has renderable content
    bool section_has_renderable_content = false;
    for (int i = 0; i < count; ++i) {
        CounterGoal *goal = t->template_data->counter_goals[i];
        if (!goal) continue;

        bool should_hide_render = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_render = (!settings->use_manual_layout && goal->is_hidden) || goal->done;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = !settings->use_manual_layout && goal->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        if (!should_hide_render && (counter_matches_search(goal, t->search_buffer)
                                    || s_linked_top.count(goal->root_name))) {
            section_has_renderable_content = true;
            break;
        }
    }

    if (!section_has_renderable_content) return;

    ImGuiIO &io = ImGui::GetIO();
    float wrapping_width = t->layout_locked ? t->locked_layout_width : (io.DisplaySize.x / t->zoom_level);

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 text_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                settings->text_color.a);
    ImU32 text_color_faded = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                      ADVANCELY_FADED_ALPHA);

    if (!settings->use_manual_layout) {
        render_section_separator(t, settings, current_y, section_title, text_color,
                                 completed_count, total_visible_count, -1, -1);
    }

    // --- Width Calculation ---
    float uniform_item_width = 0.0f;
    const float horizontal_spacing = 8.0f;

    TrackerSection section_id = SECTION_COUNTERS;
    if (settings->tracker_section_custom_width_enabled[section_id]) {
        uniform_item_width = settings->tracker_section_custom_item_width[section_id];
        if (uniform_item_width < 96.0f) uniform_item_width = 96.0f;
    } else {
        for (int i = 0; i < count; i++) {
            CounterGoal *goal = t->template_data->counter_goals[i];
            if (!goal) continue;

            bool should_hide_width = false;
            switch (settings->goal_hiding_mode) {
                case HIDE_ALL_COMPLETED:
                    should_hide_width = (!settings->use_manual_layout && goal->is_hidden) || goal->done;
                    break;
                case HIDE_ONLY_TEMPLATE_HIDDEN:
                    should_hide_width = !settings->use_manual_layout && goal->is_hidden;
                    break;
                case SHOW_ALL:
                    should_hide_width = false;
                    break;
            }
            if (should_hide_width || !(counter_matches_search(goal, t->search_buffer)
                                       || s_linked_top.count(goal->root_name)))
                continue;

            SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize);
            float text_width = ImGui::CalcTextSize(goal->display_name).x;
            RESET_FONT_SCALE();

            char progress_text_calc[32];
            snprintf(progress_text_calc, sizeof(progress_text_calc), "(%d / %d)",
                     goal->completed_count, goal->linked_goal_count);
            SET_FONT_SCALE(settings->tracker_sub_font_size, t->tracker_font->LegacySize);
            float progress_width = ImGui::CalcTextSize(progress_text_calc).x;
            RESET_FONT_SCALE();

            float required_text_width = fmaxf(text_width, progress_width);
            uniform_item_width = fmaxf(uniform_item_width, fmaxf(96.0f, required_text_width));
        }
        uniform_item_width += horizontal_spacing;
    }

    // --- Layout ---
    float padding = 50.0f;
    if (settings->use_manual_layout) {
        padding = get_global_safe_x(t);
        float min_wrapping_width = padding + (uniform_item_width * 3.0f);
        if (wrapping_width < min_wrapping_width) wrapping_width = min_wrapping_width;
    }
    float current_x = padding, row_max_height = 0.0f;
    const float vertical_spacing = settings->tracker_vertical_spacing;

    // --- Rendering Loop ---
    for (int i = 0; i < count; i++) {
        CounterGoal *goal = t->template_data->counter_goals[i];
        if (!goal) continue;

        bool should_hide_render = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_render = (!settings->use_manual_layout && goal->is_hidden) || goal->done;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = !settings->use_manual_layout && goal->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }
        if (should_hide_render || !(counter_matches_search(goal, t->search_buffer)
                                    || s_linked_top.count(goal->root_name)))
            continue;

        // Progress text
        char progress_text[32];
        snprintf(progress_text, sizeof(progress_text), "(%d / %d)",
                 goal->completed_count, goal->linked_goal_count);

        float item_height = 96.0f + main_text_line_height + 4.0f + sub_text_line_height + 4.0f;

        // Skip auto-layout items that are fully hidden in manual layout
        if (settings->use_manual_layout && !goal->icon_pos.is_set &&
            goal->icon_pos.is_hidden_in_layout && goal->text_pos.is_hidden_in_layout &&
            goal->progress_pos.is_hidden_in_layout && settings->goal_hiding_mode != SHOW_ALL) {
            continue;
        }

        float item_x = current_x;
        float item_y = current_y;

        if (settings->use_manual_layout && goal->icon_pos.is_set) {
            ImVec2 anchor_off = get_anchor_offset(goal->icon_pos.anchor, 96.0f, 96.0f);
            item_x = goal->icon_pos.x + anchor_off.x;
            item_y = goal->icon_pos.y + anchor_off.y;
        } else {
            if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
                current_x = padding;
                current_y += row_max_height;
                row_max_height = 0.0f;
                item_x = current_x;
                item_y = current_y;
            }
            current_x += uniform_item_width;
            row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
        }

        ImVec2 screen_pos = ImVec2((item_x * t->zoom_level) + t->camera_offset.x,
                                   (item_y * t->zoom_level) + t->camera_offset.y);

        ImVec2 item_size_on_screen = ImVec2(uniform_item_width * t->zoom_level, item_height * t->zoom_level);
        bool is_visible_on_screen = !(screen_pos.x > io.DisplaySize.x || (screen_pos.x + item_size_on_screen.x) < 0 ||
                                      screen_pos.y > io.DisplaySize.y || (screen_pos.y + item_size_on_screen.y) < 0);
        if (settings->use_manual_layout) is_visible_on_screen = true;

        // Per-position hiding for manual layout (multi-stage goals)
        bool hide_goal_icon_in_layout = settings->use_manual_layout && goal->icon_pos.is_hidden_in_layout && settings->
                                        goal_hiding_mode != SHOW_ALL;
        bool hide_goal_text_in_layout = settings->use_manual_layout && goal->text_pos.is_hidden_in_layout && settings->
                                        goal_hiding_mode != SHOW_ALL;
        bool hide_goal_progress_in_layout = settings->use_manual_layout && goal->progress_pos.is_hidden_in_layout &&
                                            settings->goal_hiding_mode != SHOW_ALL;

        if (is_visible_on_screen) {
            SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize);
            ImVec2 text_size = ImGui::CalcTextSize(goal->display_name);
            RESET_FONT_SCALE();

            SET_FONT_SCALE(settings->tracker_sub_font_size, t->tracker_font->LegacySize);
            ImVec2 progress_text_size = ImGui::CalcTextSize(progress_text);
            RESET_FONT_SCALE();

            ImVec2 bg_size = ImVec2(96.0f, 96.0f);

            // Select background texture based on completion state:
            // - Default (no progress): adv_bg
            // - Partially done (some linked goals completed): adv_bg_half_done
            // - Fully done (all linked goals completed): adv_bg_done
            SDL_Texture *static_bg = t->adv_bg;
            AnimatedTexture *anim_bg = t->adv_bg_anim;

            if (goal->done) {
                static_bg = t->adv_bg_done;
                anim_bg = t->adv_bg_done_anim;
            } else if (goal->completed_count > 0) {
                static_bg = t->adv_bg_half_done;
                anim_bg = t->adv_bg_half_done_anim;
            }

            SDL_Texture *texture_to_draw = static_bg;
            if (anim_bg && anim_bg->frame_count > 0) {
                if (anim_bg->delays && anim_bg->total_duration > 0) {
                    Uint32 current_ticks = SDL_GetTicks();
                    Uint32 elapsed_time = current_ticks % anim_bg->total_duration;
                    int current_frame = 0;
                    Uint32 time_sum = 0;
                    for (int frame_idx = 0; frame_idx < anim_bg->frame_count; ++frame_idx) {
                        time_sum += anim_bg->delays[frame_idx];
                        if (elapsed_time < time_sum) {
                            current_frame = frame_idx;
                            break;
                        }
                    }
                    texture_to_draw = anim_bg->frames[current_frame];
                } else {
                    texture_to_draw = anim_bg->frames[0];
                }
            }

            // Render Background
            if (!hide_goal_icon_in_layout && texture_to_draw)
                draw_list->AddImage((void *) texture_to_draw, screen_pos,
                                    ImVec2(screen_pos.x + bg_size.x * t->zoom_level,
                                           screen_pos.y + bg_size.y * t->zoom_level));

            // Render Icon (Animated or Static)
            SDL_Texture *icon_texture = nullptr;
            if (goal->anim_texture && goal->anim_texture->frame_count > 0) {
                if (goal->anim_texture->delays && goal->anim_texture->total_duration > 0) {
                    Uint32 current_ticks = SDL_GetTicks();
                    Uint32 elapsed_time = current_ticks % goal->anim_texture->total_duration;
                    int current_frame = 0;
                    Uint32 time_sum = 0;
                    for (int frame_idx = 0; frame_idx < goal->anim_texture->frame_count; ++frame_idx) {
                        time_sum += goal->anim_texture->delays[frame_idx];
                        if (elapsed_time < time_sum) {
                            current_frame = frame_idx;
                            break;
                        }
                    }
                    icon_texture = goal->anim_texture->frames[current_frame];
                } else {
                    icon_texture = goal->anim_texture->frames[0];
                }
            } else if (goal->texture) {
                icon_texture = goal->texture;
            }

            if (icon_texture) {
                float tex_w = 0.0f, tex_h = 0.0f;
                SDL_GetTextureSize(icon_texture, &tex_w, &tex_h);
                ImVec2 target_box_size = ImVec2(64.0f * t->zoom_level, 64.0f * t->zoom_level);
                float scale_factor = 1.0f;
                if (tex_w > 0 && tex_h > 0) {
                    scale_factor = fminf(target_box_size.x / tex_w, target_box_size.y / tex_h);
                }
                ImVec2 scaled_size = ImVec2(tex_w * scale_factor, tex_h * scale_factor);
                ImVec2 box_p_min = ImVec2(screen_pos.x + 16.0f * t->zoom_level, screen_pos.y + 16.0f * t->zoom_level);
                ImVec2 icon_padding = ImVec2((target_box_size.x - scaled_size.x) * 0.5f,
                                             (target_box_size.y - scaled_size.y) * 0.5f);
                ImVec2 p_min = ImVec2(box_p_min.x + icon_padding.x, box_p_min.y + icon_padding.y);
                ImVec2 p_max = ImVec2(p_min.x + scaled_size.x, p_min.y + scaled_size.y);
                if (!hide_goal_icon_in_layout)
                    draw_list->AddImage((void *) icon_texture, p_min, p_max);
            }

            // --- VISUAL LAYOUT DRAGGING (ICON) ---
            char drag_id[256];
            snprintf(drag_id, sizeof(drag_id), "drag_counter_icon_%s", goal->root_name);
            handle_visual_layout_dragging(t, drag_id, screen_pos,
                                          ImVec2(bg_size.x * t->zoom_level, bg_size.y * t->zoom_level),
                                          goal->icon_pos, "Counter", goal->display_name, "Icon",
                                          goal->root_name);

            // Render Text
            if (t->zoom_level > LOD_TEXT_MAIN_THRESHOLD && !hide_goal_text_in_layout) {
                float main_text_size = settings->tracker_font_size;
                float sub_font_size = settings->tracker_sub_font_size;
                ImU32 current_text_color = goal->done ? text_color_faded : text_color;

                float text_x_center = screen_pos.x + (bg_size.x * t->zoom_level) * 0.5f;
                float text_y_pos = screen_pos.y + bg_size.y * t->zoom_level + (4.0f * t->zoom_level);

                if (settings->use_manual_layout && goal->text_pos.is_set) {
                    ImVec2 text_anchor_off = get_anchor_offset(goal->text_pos.anchor, text_size.x, text_size.y);
                    text_x_center = ((goal->text_pos.x + text_anchor_off.x) * t->zoom_level) + t->camera_offset.x + (
                                        text_size.x * t->zoom_level) * 0.5f;
                    text_y_pos = ((goal->text_pos.y + text_anchor_off.y) * t->zoom_level) + t->camera_offset.y;
                }

                // Draw Main Name (centered)
                draw_list->AddText(nullptr, main_text_size * t->zoom_level,
                                   ImVec2(text_x_center - (text_size.x * t->zoom_level) * 0.5f, text_y_pos),
                                   current_text_color, goal->display_name);

                // --- VISUAL LAYOUT DRAGGING (TEXT) ---
                snprintf(drag_id, sizeof(drag_id), "drag_counter_text_%s", goal->root_name);
                handle_visual_layout_dragging(t, drag_id,
                                              ImVec2(text_x_center - (text_size.x * t->zoom_level) * 0.5f, text_y_pos),
                                              ImVec2(text_size.x * t->zoom_level, text_size.y * t->zoom_level),
                                              goal->text_pos, "Counter", goal->display_name, "Text",
                                              goal->root_name);

                // Draw Progress Text
                if (!hide_goal_progress_in_layout) {
                    float prog_x_center = text_x_center;
                    float prog_y = text_y_pos + text_size.y * t->zoom_level + 4.0f * t->zoom_level;

                    if (settings->use_manual_layout && goal->progress_pos.is_set) {
                        ImVec2 prog_anchor_off = get_anchor_offset(goal->progress_pos.anchor, progress_text_size.x,
                                                                   progress_text_size.y);
                        prog_x_center = ((goal->progress_pos.x + prog_anchor_off.x) * t->zoom_level) + t->camera_offset.
                                        x + (
                                            progress_text_size.x * t->zoom_level) * 0.5f;
                        prog_y = ((goal->progress_pos.y + prog_anchor_off.y) * t->zoom_level) + t->camera_offset.y;
                    }

                    if (t->zoom_level > LOD_TEXT_SUB_THRESHOLD) {
                        draw_list->AddText(nullptr, sub_font_size * t->zoom_level,
                                           ImVec2(prog_x_center - (progress_text_size.x * t->zoom_level) * 0.5f,
                                                  prog_y),
                                           current_text_color, progress_text);

                        // --- VISUAL LAYOUT DRAGGING (PROGRESS) ---
                        snprintf(drag_id, sizeof(drag_id), "drag_counter_prog_%s", goal->root_name);
                        handle_visual_layout_dragging(t, drag_id,
                                                      ImVec2(
                                                          prog_x_center - (progress_text_size.x * t->zoom_level) * 0.5f,
                                                          prog_y),
                                                      ImVec2(progress_text_size.x * t->zoom_level,
                                                             progress_text_size.y * t->zoom_level),
                                                      goal->progress_pos, "Counter", goal->display_name, "Progress",
                                                      goal->root_name);
                    }
                } // End hide_goal_progress_in_layout
            }
        } // End if (is_visible_on_screen)
    }

    current_y += row_max_height;
}

/**
 * @brief Renders the Multi-Stage Goals section.
 * Calculates and displays completion counters based on visibility settings.
 */
static void render_multistage_goals_section(Tracker *t, const AppSettings *settings, float &current_y,
                                            const char *section_title) {
    // LOD Thresholds
    const float LOD_TEXT_SUB_THRESHOLD = settings->lod_text_sub_threshold; // Hide Stage Text
    const float LOD_TEXT_MAIN_THRESHOLD = settings->lod_text_main_threshold; // Hide Main Goal Name

    // Pre-calculate line heights once per frame (Optimization)
    const float main_text_line_height = settings->tracker_font_size;
    const float sub_text_line_height = settings->tracker_sub_font_size;

    int count = t->template_data->multi_stage_goal_count;

    // --- Pre-computation and Filtering for Counters ---
    int total_visible_count = 0; // Count of main multi-stage goals visible based on mode
    int completed_count = 0; // Count of completed main multi-stage goals visible based on mode
    int total_visible_sub_count = 0; // Count of *completable stages* across all relevant goals
    int completed_sub_count = 0; // Count of *completed stages* across all relevant goals

    for (int i = 0; i < count; ++i) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        if (!goal || goal->stage_count <= 0) continue; // Skip invalid goals

        // Determine completion status of the main goal
        bool is_goal_considered_complete = (goal->current_stage >= goal->stage_count - 1);

        // Determine visibility of the main goal based on hiding mode
        bool should_hide_based_on_mode = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_based_on_mode = (!settings->use_manual_layout && goal->is_hidden) ||
                                            is_goal_considered_complete;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_based_on_mode = !settings->use_manual_layout && goal->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_based_on_mode = false;
                break;
        }

        // Apply Search Filter for counting (check main name AND active stage)
        SubGoal *active_stage_count = goal->stages[goal->current_stage];
        bool name_matches = ms_goal_matches_search(goal, t->search_buffer)
                            || s_linked_top.count(goal->root_name);
        bool stage_matches = stage_matches_search(goal, active_stage_count, t->search_buffer);
        bool goal_matches_search = name_matches || stage_matches;

        // Skip entire goal if hidden by mode OR doesn't match search
        if (should_hide_based_on_mode || !goal_matches_search) continue;

        // --- Count Main Item (Multi-Stage Goal) ---
        total_visible_count++;
        if (is_goal_considered_complete) {
            completed_count++;
        }

        // --- Count Sub-Items (Stages) ---
        // Only count stages if the goal itself isn't template-hidden (already checked above)
        // and has more than one stage (meaning it has completable stages)
        if (goal->stage_count > 1) {
            // Iterate through *completable* stages (all except the last one)
            for (int j = 0; j < goal->stage_count - 1; ++j) {
                // Determine if the *stage* is considered complete
                bool is_stage_complete = (goal->current_stage > j);

                // Count this stage towards totals as the parent goal is visible
                total_visible_sub_count++;
                if (is_stage_complete) {
                    completed_sub_count++;
                }
            }
        }
    }
    // --- End of Counter Calculation ---


    // --- Section Rendering ---

    // Check if *anything* should be rendered based on search + hiding
    bool section_has_renderable_content = false;
    for (int i = 0; i < count; ++i) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        if (!goal || goal->stage_count <= 0) continue;

        bool is_done_render = (goal->current_stage >= goal->stage_count - 1);

        // Apply rendering hiding logic
        bool should_hide_render = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_render = (!settings->use_manual_layout && goal->is_hidden) || is_done_render;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = !settings->use_manual_layout && goal->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Apply search filter (check main name and current stage text)
        SubGoal *active_stage_render = goal->stages[goal->current_stage];
        bool name_matches_render = ms_goal_matches_search(goal, t->search_buffer)
                                   || s_linked_top.count(goal->root_name);
        bool stage_matches_render = stage_matches_search(goal, active_stage_render, t->search_buffer);

        // Combine hiding and search filter
        if (!should_hide_render && (name_matches_render || stage_matches_render)) {
            section_has_renderable_content = true;
            break; // Found at least one item to render
        }
    }

    if (!section_has_renderable_content) return; // Hide section if no items match filters


    ImGuiIO &io = ImGui::GetIO();

    // Use locked width if layout is locked
    float wrapping_width = t->layout_locked ? t->locked_layout_width : (io.DisplaySize.x / t->zoom_level);


    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 text_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                settings->text_color.a);
    ImU32 text_color_faded = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                      ADVANCELY_FADED_ALPHA); // Faded text color
    float scale_factor; // Declare scale_factor here

    // Call separator with calculated counts
    if (!settings->use_manual_layout) {
        // Only if not in manual mode
        render_section_separator(t, settings, current_y, section_title, text_color,
                                 completed_count, total_visible_count,
                                 completed_sub_count, total_visible_sub_count); // Pass stage counts
    }


    // --- Calculate Uniform Item Width (based on items that will be rendered) ---
    // IMPORTANT: Width calculation logic must remain consistent regardless of LOD.
    float uniform_item_width = 0.0f;
    const float horizontal_spacing = 8.0f; // Define the default spacing

    // Check if custom width is enabled for THIS section (Multi-Stage)
    TrackerSection section_id = SECTION_MULTISTAGE;
    if (settings->tracker_section_custom_width_enabled[section_id]) {
        // use fixed width from settings
        uniform_item_width = settings->tracker_section_custom_item_width[section_id];
        if (uniform_item_width < 96.0f) uniform_item_width = 96.0f;
    } else {
        // Dynamic width
        for (int i = 0; i < count; i++) {
            MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
            if (!goal || goal->stage_count <= 0) continue;

            bool is_done_width = (goal->current_stage >= goal->stage_count - 1);

            // Apply rendering hiding logic for width calculation
            bool should_hide_width = false;
            switch (settings->goal_hiding_mode) {
                case HIDE_ALL_COMPLETED:
                    should_hide_width = (!settings->use_manual_layout && goal->is_hidden) || is_done_width;
                    break;
                case HIDE_ONLY_TEMPLATE_HIDDEN:
                    should_hide_width = !settings->use_manual_layout && goal->is_hidden;
                    break;
                case SHOW_ALL:
                    should_hide_width = false;
                    break;
            }

            // Apply search filter for width calculation
            SubGoal *active_stage_width = goal->stages[goal->current_stage];
            bool name_matches_width = ms_goal_matches_search(goal, t->search_buffer)
                                      || s_linked_top.count(goal->root_name);
            bool stage_matches_width = stage_matches_search(goal, active_stage_width, t->search_buffer);

            // Only consider items that will actually be rendered for width calculation
            if (!should_hide_width && (name_matches_width || stage_matches_width)) {
                // Calculate width needed for text (main name and current stage text)
                SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize);
                float name_width = ImGui::CalcTextSize(goal->display_name).x;
                RESET_FONT_SCALE();

                // Format stage text including progress if applicable
                char stage_text_width_calc[256];
                if (active_stage_width->type == SUBGOAL_STAT && active_stage_width->required_progress > 0) {
                    snprintf(stage_text_width_calc, sizeof(stage_text_width_calc), "%s (%d/%d)",
                             active_stage_width->display_text,
                             active_stage_width->current_stat_progress, active_stage_width->required_progress);
                } else {
                    strncpy(stage_text_width_calc, active_stage_width->display_text, sizeof(stage_text_width_calc) - 1);
                    stage_text_width_calc[sizeof(stage_text_width_calc) - 1] = '\0';
                }
                // Scale for stage text width calculation
                SET_FONT_SCALE(settings->tracker_sub_font_size, t->tracker_font->LegacySize);
                float stage_width = ImGui::CalcTextSize(stage_text_width_calc).x;
                RESET_FONT_SCALE();

                // Required width is the max needed for either line
                float required_text_width = fmaxf(name_width, stage_width);
                // Ensure minimum width accommodates the 96px background
                uniform_item_width = fmaxf(uniform_item_width, fmaxf(96.0f, required_text_width));
            }
        }
        // Add default spacing ONLY in dynamic mode
        uniform_item_width += horizontal_spacing;
    }
    // --- End of Width Calculation ---

    // --- GLOBAL HYBRID SHIFT ---
    float padding = 50.0f;
    if (settings->use_manual_layout) {
        padding = get_global_safe_x(t);

        // Prevent auto-layout items from squeezing into a single column!
        // We force the wrapping width to allow at least 3 items to fit side-by-side
        float min_wrapping_width = padding + (uniform_item_width * 3.0f);
        if (wrapping_width < min_wrapping_width) {
            wrapping_width = min_wrapping_width;
        }
    }
    float current_x = padding, row_max_height = 0.0f;

    // Adjust vertical spacing
    const float vertical_spacing = settings->tracker_vertical_spacing; // Changed from 16.0f


    // --- Rendering Loop ---
    for (int i = 0; i < count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        if (!goal || goal->stage_count <= 0) continue;

        bool is_done_render = (goal->current_stage >= goal->stage_count - 1);

        // Apply rendering hiding logic
        bool should_hide_render = false;
        switch (settings->goal_hiding_mode) {
            case HIDE_ALL_COMPLETED:
                should_hide_render = (!settings->use_manual_layout && goal->is_hidden) || is_done_render;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = !settings->use_manual_layout && goal->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Apply search filter
        SubGoal *active_stage_render = goal->stages[goal->current_stage];
        bool name_matches_render = ms_goal_matches_search(goal, t->search_buffer)
                                   || s_linked_top.count(goal->root_name);
        bool stage_matches_render = stage_matches_search(goal, active_stage_render, t->search_buffer);

        // Skip rendering if hidden or doesn't match search
        if (should_hide_render || (!name_matches_render && !stage_matches_render)) {
            continue;
        }


        // --- Item Height and Layout (OPTIMIZED) ---
        // Calculate height using Math: Icon bg (96) + main name height + padding (4) + stage text height + padding (4)
        // Multi-Stage goals always have both lines of text
        float item_height = 96.0f + main_text_line_height + 4.0f + sub_text_line_height + 4.0f;

        // Skip auto-layout items that are fully hidden in manual layout
        if (settings->use_manual_layout && !goal->icon_pos.is_set &&
            goal->icon_pos.is_hidden_in_layout && goal->text_pos.is_hidden_in_layout &&
            goal->progress_pos.is_hidden_in_layout && settings->goal_hiding_mode != SHOW_ALL) {
            continue;
        }

        float item_x = current_x;
        float item_y = current_y;

        if (settings->use_manual_layout && goal->icon_pos.is_set) {
            ImVec2 anchor_off = get_anchor_offset(goal->icon_pos.anchor, 96.0f, 96.0f);
            item_x = goal->icon_pos.x + anchor_off.x;
            item_y = goal->icon_pos.y + anchor_off.y;
        } else {
            // Procedural Auto-Layout Wrapping
            if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
                current_x = padding;
                current_y += row_max_height;
                row_max_height = 0.0f;
                item_x = current_x;
                item_y = current_y;
            }
        }

        ImVec2 screen_pos = ImVec2((item_x * t->zoom_level) + t->camera_offset.x,
                                   (item_y * t->zoom_level) + t->camera_offset.y);

        // --- Culling Logic ---
        ImVec2 item_size_on_screen = ImVec2(uniform_item_width * t->zoom_level, item_height * t->zoom_level);
        bool is_visible_on_screen = !(screen_pos.x > io.DisplaySize.x || (screen_pos.x + item_size_on_screen.x) < 0 ||
                                      screen_pos.y > io.DisplaySize.y || (screen_pos.y + item_size_on_screen.y) < 0);

        // Disable coarse parent culling in manual layout so detached text/criteria don't vanish
        if (settings->use_manual_layout) {
            is_visible_on_screen = true;
        }

        // Per-position hiding for manual layout (multi-stage goals)
        bool hide_goal_icon_in_layout = settings->use_manual_layout && goal->icon_pos.is_hidden_in_layout && settings->
                                        goal_hiding_mode != SHOW_ALL;
        bool hide_goal_text_in_layout = settings->use_manual_layout && goal->text_pos.is_hidden_in_layout && settings->
                                        goal_hiding_mode != SHOW_ALL;
        bool hide_goal_progress_in_layout = settings->use_manual_layout && goal->progress_pos.is_hidden_in_layout &&
                                            settings->goal_hiding_mode != SHOW_ALL;

        // --- Rendering Core Logic ---
        if (is_visible_on_screen) {
            // --- String Formatting and Text Sizing (Only if visible) ---
            char stage_text[256];
            if (active_stage_render->type == SUBGOAL_STAT && active_stage_render->required_progress > 0) {
                snprintf(stage_text, sizeof(stage_text), "%s (%d/%d)", active_stage_render->display_text,
                         active_stage_render->current_stat_progress, active_stage_render->required_progress);
            } else {
                strncpy(stage_text, active_stage_render->display_text, sizeof(stage_text) - 1);
                stage_text[sizeof(stage_text) - 1] = '\0';
            }

            // Scale font to sub-size for stage text measurement
            SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize);
            ImVec2 text_size = ImGui::CalcTextSize(goal->display_name); // Uses main tracker_font_size
            RESET_FONT_SCALE();

            // Scale font to sub-size for stage text measurement
            SET_FONT_SCALE(settings->tracker_sub_font_size, t->tracker_font->LegacySize);
            ImVec2 stage_text_size = ImGui::CalcTextSize(stage_text);
            RESET_FONT_SCALE();

            ImVec2 bg_size = ImVec2(96.0f, 96.0f);

            // Select texture *pair* and render
            SDL_Texture *static_bg = t->adv_bg;
            AnimatedTexture *anim_bg = t->adv_bg_anim;

            if (goal->current_stage >= goal->stage_count - 1) {
                static_bg = t->adv_bg_done;
                anim_bg = t->adv_bg_done_anim;
            } else if (goal->current_stage > 0) {
                static_bg = t->adv_bg_half_done;
                anim_bg = t->adv_bg_half_done_anim;
            }

            SDL_Texture *texture_to_draw = static_bg;
            if (anim_bg && anim_bg->frame_count > 0) {
                // --- Standard GIF Frame Selection Logic ---
                if (anim_bg->delays && anim_bg->total_duration > 0) {
                    Uint32 current_ticks = SDL_GetTicks();
                    Uint32 elapsed_time = current_ticks % anim_bg->total_duration;
                    int current_frame = 0;
                    Uint32 time_sum = 0;
                    for (int frame_idx = 0; frame_idx < anim_bg->frame_count; ++frame_idx) {
                        time_sum += anim_bg->delays[frame_idx];
                        if (elapsed_time < time_sum) {
                            current_frame = frame_idx;
                            break;
                        }
                    }
                    texture_to_draw = anim_bg->frames[current_frame];
                } else {
                    texture_to_draw = anim_bg->frames[0];
                }
            }

            // Render Background
            if (!hide_goal_icon_in_layout && texture_to_draw)
                draw_list->AddImage((void *) texture_to_draw, screen_pos,
                                    ImVec2(screen_pos.x + bg_size.x * t->zoom_level,
                                           screen_pos.y + bg_size.y * t->zoom_level));

            // Render Icon (Animated or Static)
            // --- Start GIF Frame Selection Logic ---

            // Determine which texture source to use
            AnimatedTexture *anim_src = goal->anim_texture;
            SDL_Texture *static_src = goal->texture;

            if (goal->use_stage_icons && goal->stage_count > 0) {
                // Use the icon of the current stage
                int stage_idx = goal->current_stage;
                // Safety clamp (though current_stage should rarely exceed bounds)
                if (stage_idx >= goal->stage_count) stage_idx = goal->stage_count - 1;

                // If flag is set, stage icon takes precedence
                if (goal->stages[stage_idx]->anim_texture || goal->stages[stage_idx]->texture) {
                    anim_src = goal->stages[stage_idx]->anim_texture;
                    static_src = goal->stages[stage_idx]->texture;
                }
            }

            if (anim_src && anim_src->frame_count > 0) {
                if (anim_src->delays && anim_src->total_duration > 0) {
                    Uint32 current_ticks = SDL_GetTicks();
                    Uint32 elapsed_time = current_ticks % anim_src->total_duration;
                    int current_frame = 0;
                    Uint32 time_sum = 0;
                    for (int frame_idx = 0; frame_idx < anim_src->frame_count; ++frame_idx) {
                        time_sum += anim_src->delays[frame_idx];
                        if (elapsed_time < time_sum) {
                            current_frame = frame_idx;
                            break;
                        }
                    }
                    texture_to_draw = anim_src->frames[current_frame];
                } else {
                    // Fallback if no timing info
                    texture_to_draw = anim_src->frames[0];
                }
            } else if (static_src) {
                // Static texture
                texture_to_draw = static_src;
            }
            // --- End GIF Frame Selection Logic ---

            if (texture_to_draw) {
                // --- Start Icon Scaling and Centering Logic (64x64 box) ---
                float tex_w = 0.0f, tex_h = 0.0f;
                SDL_GetTextureSize(texture_to_draw, &tex_w, &tex_h);
                ImVec2 target_box_size = ImVec2(64.0f * t->zoom_level, 64.0f * t->zoom_level);
                // Target box size on screen
                float scale_factor = 1.0f;
                if (tex_w > 0 && tex_h > 0) {
                    // Avoid division by zero
                    scale_factor = fminf(target_box_size.x / tex_w, target_box_size.y / tex_h);
                }
                ImVec2 scaled_size = ImVec2(tex_w * scale_factor, tex_h * scale_factor); // Scaled size on screen
                ImVec2 box_p_min = ImVec2(screen_pos.x + 16.0f * t->zoom_level, screen_pos.y + 16.0f * t->zoom_level);
                // 16.0f to CENTER the icons
                // Top-left of 64x64 box on screen
                ImVec2 icon_padding = ImVec2((target_box_size.x - scaled_size.x) * 0.5f,
                                             (target_box_size.y - scaled_size.y) * 0.5f); // Padding within the box
                ImVec2 p_min = ImVec2(box_p_min.x + icon_padding.x, box_p_min.y + icon_padding.y);
                // Final top-left for drawing
                ImVec2 p_max = ImVec2(p_min.x + scaled_size.x, p_min.y + scaled_size.y);
                // Final bottom-right for drawing
                if (!hide_goal_icon_in_layout)
                    draw_list->AddImage((void *) texture_to_draw, p_min, p_max);
                // --- End Icon Scaling and Centering Logic ---
            }

            // --- VISUAL LAYOUT DRAGGING (ICON) ---
            char drag_id[256];
            snprintf(drag_id, sizeof(drag_id), "drag_ms_icon_%s", goal->root_name);
            handle_visual_layout_dragging(t, drag_id, screen_pos,
                                          ImVec2(bg_size.x * t->zoom_level, bg_size.y * t->zoom_level),
                                          goal->icon_pos, "Multi-Stage Goal", goal->display_name, "Icon",
                                          goal->root_name);

            // Render Text (Main Name and Current Stage Text)
            float main_font_size = settings->tracker_font_size;
            float sub_font_size = settings->tracker_sub_font_size;
            ImU32 current_text_color = is_done_render ? text_color_faded : text_color; // Fade if done

            // --- TEXT CENTERING AND POSITIONING ---
            float text_x_center = screen_pos.x + (bg_size.x * t->zoom_level) * 0.5f;
            float text_y_pos = screen_pos.y + bg_size.y * t->zoom_level + (4.0f * t->zoom_level);

            if (settings->use_manual_layout && goal->text_pos.is_set) {
                ImVec2 text_anchor_off = get_anchor_offset(goal->text_pos.anchor, text_size.x, text_size.y);
                text_x_center = ((goal->text_pos.x + text_anchor_off.x) * t->zoom_level) + t->camera_offset.x + (
                                    text_size.x * t->zoom_level)
                                * 0.5f;
                text_y_pos = ((goal->text_pos.y + text_anchor_off.y) * t->zoom_level) + t->camera_offset.y;
            }

            // Draw Main Name (centered)
            // LOD: Hide main name if zoomed out too far
            if (t->zoom_level > LOD_TEXT_MAIN_THRESHOLD) {
                if (!hide_goal_text_in_layout)
                    draw_list->AddText(nullptr, main_font_size * t->zoom_level,
                                       ImVec2(text_x_center - (text_size.x * t->zoom_level) * 0.5f, text_y_pos),
                                       current_text_color, goal->display_name);

                // --- VISUAL LAYOUT DRAGGING (TEXT) ---
                snprintf(drag_id, sizeof(drag_id), "drag_ms_text_%s", goal->root_name);
                handle_visual_layout_dragging(t, drag_id,
                                              ImVec2(text_x_center - (text_size.x * t->zoom_level) * 0.5f, text_y_pos),
                                              ImVec2(text_size.x * t->zoom_level, text_size.y * t->zoom_level),
                                              goal->text_pos, "Multi-Stage Goal", goal->display_name, "Text",
                                              goal->root_name);
            }

            // Draw Current Stage Text (uses sub_font_size)
            float stage_text_y = text_y_pos + text_size.y * t->zoom_level + 4.0f * t->zoom_level;
            float stage_text_x_center = text_x_center;

            // Apply manual progress_pos if set
            if (settings->use_manual_layout && goal->progress_pos.is_set) {
                ImVec2 prog_anchor_off = get_anchor_offset(goal->progress_pos.anchor, stage_text_size.x,
                                                           stage_text_size.y);
                stage_text_x_center = ((goal->progress_pos.x + prog_anchor_off.x) * t->zoom_level) + t->camera_offset.x
                                      + (
                                          stage_text_size.x * t->zoom_level) * 0.5f;
                stage_text_y = ((goal->progress_pos.y + prog_anchor_off.y) * t->zoom_level) + t->camera_offset.y;
            }

            // LOD: Hide stage text if zoomed out
            if (t->zoom_level > LOD_TEXT_SUB_THRESHOLD) {
                if (!hide_goal_progress_in_layout)
                    draw_list->AddText(nullptr, sub_font_size * t->zoom_level,
                                       ImVec2(stage_text_x_center - (stage_text_size.x * t->zoom_level) * 0.5f,
                                              stage_text_y),
                                       current_text_color, stage_text); // Use formatted stage text

                // --- VISUAL LAYOUT DRAGGING (PROGRESS) ---
                snprintf(drag_id, sizeof(drag_id), "drag_ms_prog_%s", goal->root_name);
                handle_visual_layout_dragging(t, drag_id,
                                              ImVec2(stage_text_x_center - (stage_text_size.x * t->zoom_level) * 0.5f,
                                                     stage_text_y),
                                              ImVec2(stage_text_size.x * t->zoom_level,
                                                     stage_text_size.y * t->zoom_level),
                                              goal->progress_pos, "Multi-Stage Goal", goal->display_name, "Progress",
                                              goal->root_name);
            }
        } // End if (is_visible_on_screen)

        // --- Update Layout Position ---
        current_x += uniform_item_width; // Use the final uniform_item_width directly
        row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
    }
    // --- End Rendering Loop ---

    // Update vertical position for the next section
    current_y += row_max_height;
}


// Helper struct to pass necessary data to the ImGui callback
typedef struct {
    Tracker *t;
    const AppSettings *settings;
} NotesCallbackData;

// This is the callback function that ImGui will call on every edit of the notes window
static int NotesEditCallback(ImGuiInputTextCallbackData *data) {
    // The 'UserData' field contains the pointer we passed to InputTextMultiline.
    // We cast it back to our struct type to get access to our tracker and settings.
    auto *callback_data = (NotesCallbackData *) data->UserData;

    // The buffer has been edited, so we save the notes immediately.
    tracker_save_notes(callback_data->t, callback_data->settings);

    return 0;
}


/**
 * @brief Renders decoration elements (text headers, lines, arrows) on the tracker map.
 * Only renders when manual layout mode is active.
 */
static void render_decorations(Tracker *t, const AppSettings *settings) {
    if (!settings->use_manual_layout) return;
    if (!t->template_data || t->template_data->decoration_count <= 0) return;

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 text_color = IM_COL32(settings->text_color.r, settings->text_color.g, settings->text_color.b,
                                settings->text_color.a);
    float main_font_size = settings->tracker_font_size;

    for (int i = 0; i < t->template_data->decoration_count; i++) {
        DecorationElement *elem = t->template_data->decorations[i];
        if (!elem) continue;

        switch (elem->type) {
            case DECORATION_TEXT_HEADER: {
                if (elem->display_text[0] == '\0') continue;

                // Check if hidden in manual layout
                if (elem->pos.is_hidden_in_layout && settings->goal_hiding_mode != SHOW_ALL) continue;

                // Measure text
                float scale_factor;
                SET_FONT_SCALE(main_font_size, t->tracker_font->LegacySize);
                ImVec2 text_size = ImGui::CalcTextSize(elem->display_text);
                RESET_FONT_SCALE();

                // Position
                float text_x, text_y;
                if (elem->pos.is_set) {
                    ImVec2 anchor_off = get_anchor_offset(elem->pos.anchor, text_size.x, text_size.y);
                    text_x = ((elem->pos.x + anchor_off.x) * t->zoom_level) + t->camera_offset.x;
                    text_y = ((elem->pos.y + anchor_off.y) * t->zoom_level) + t->camera_offset.y;
                } else {
                    // Fallback: render at a default position (shouldn't normally happen)
                    text_x = (100.0f * t->zoom_level) + t->camera_offset.x;
                    text_y = (100.0f * t->zoom_level) + t->camera_offset.y;
                }

                // LOD: Use main text threshold
                if (t->zoom_level > settings->lod_text_main_threshold) {
                    draw_list->AddText(nullptr, main_font_size * t->zoom_level,
                                       ImVec2(text_x, text_y),
                                       text_color, elem->display_text);

                    // Visual layout dragging
                    char drag_id[128];
                    snprintf(drag_id, sizeof(drag_id), "drag_deco_%s", elem->id);
                    handle_visual_layout_dragging(t, drag_id,
                                                  ImVec2(text_x, text_y),
                                                  ImVec2(text_size.x * t->zoom_level, text_size.y * t->zoom_level),
                                                  elem->pos, "Decoration", elem->display_text, "Text",
                                                  elem->id);
                }
                break;
            }

            case DECORATION_LINE: {
                // Compute screen positions for both endpoints
                float x1, y1, x2, y2;
                if (elem->pos.is_set) {
                    x1 = (elem->pos.x * t->zoom_level) + t->camera_offset.x;
                    y1 = (elem->pos.y * t->zoom_level) + t->camera_offset.y;
                } else {
                    x1 = (100.0f * t->zoom_level) + t->camera_offset.x;
                    y1 = (100.0f * t->zoom_level) + t->camera_offset.y;
                }
                if (elem->pos2.is_set) {
                    x2 = (elem->pos2.x * t->zoom_level) + t->camera_offset.x;
                    y2 = (elem->pos2.y * t->zoom_level) + t->camera_offset.y;
                } else {
                    x2 = (200.0f * t->zoom_level) + t->camera_offset.x;
                    y2 = (100.0f * t->zoom_level) + t->camera_offset.y;
                }

                // Apply opacity to the tracker font color
                ImU32 line_color = IM_COL32(settings->text_color.r, settings->text_color.g,
                                            settings->text_color.b,
                                            (int) (settings->text_color.a * elem->opacity));

                float scaled_thickness = elem->thickness * t->zoom_level;
                draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), line_color, scaled_thickness);

                // Visual layout dragging for the whole line (drag from the midpoint)
                if (t->is_visual_layout_editing) {
                    // Endpoint 1 drag handle
                    float handle_size = fmaxf(8.0f, scaled_thickness * 2.0f);
                    char drag_id_p1[128];
                    snprintf(drag_id_p1, sizeof(drag_id_p1), "drag_deco_%s_p1", elem->id);
                    handle_visual_layout_dragging(t, drag_id_p1,
                                                  ImVec2(x1 - handle_size * 0.5f, y1 - handle_size * 0.5f),
                                                  ImVec2(handle_size, handle_size),
                                                  elem->pos, "Decoration", elem->id, "Endpoint 1",
                                                  elem->id);

                    // Endpoint 2 drag handle
                    char drag_id_p2[128];
                    snprintf(drag_id_p2, sizeof(drag_id_p2), "drag_deco_%s_p2", elem->id);
                    handle_visual_layout_dragging(t, drag_id_p2,
                                                  ImVec2(x2 - handle_size * 0.5f, y2 - handle_size * 0.5f),
                                                  ImVec2(handle_size, handle_size),
                                                  elem->pos2, "Decoration", elem->id, "Endpoint 2",
                                                  elem->id);

                    // Whole-line drag handle at center of the line
                    float mid_x = (x1 + x2) * 0.5f;
                    float mid_y = (y1 + y2) * 0.5f;
                    float line_handle_size = fmaxf(handle_size, scaled_thickness * 3.0f);

                    // Draw a midpoint marker so users know where to grab
                    float marker_radius = fmaxf(4.0f, scaled_thickness * 0.8f);
                    draw_list->AddCircleFilled(ImVec2(mid_x, mid_y), marker_radius,
                                               IM_COL32(255, 255, 255, 180));
                    draw_list->AddCircle(ImVec2(mid_x, mid_y), marker_radius,
                                         IM_COL32(0, 0, 0, 200), 0, 1.5f);

                    char drag_id_line[128];
                    snprintf(drag_id_line, sizeof(drag_id_line), "drag_deco_%s_line", elem->id);

                    // Custom dual-endpoint dragging from the line's center
                    ImGui::PushID(drag_id_line);
                    ImVec2 line_handle_pos = ImVec2(mid_x - line_handle_size * 0.5f,
                                                    mid_y - line_handle_size * 0.5f);
                    ImGui::SetCursorScreenPos(line_handle_pos);
                    ImGui::InvisibleButton("##drag_handle", ImVec2(line_handle_size, line_handle_size));

                    bool is_line_dragging = ImGui::IsItemActive() &&
                                            ImGui::IsMouseDragging(ImGuiMouseButton_Left);
                    bool is_line_hovered = ImGui::IsItemHovered();
                    bool is_line_just_clicked = ImGui::IsItemActivated();

                    // Register both endpoints for selection hit-testing
                    s_visual_layout_items.push_back({
                        line_handle_pos,
                        ImVec2(line_handle_size, line_handle_size),
                        &elem->pos
                    });
                    s_visual_layout_items.push_back({
                        line_handle_pos,
                        ImVec2(line_handle_size, line_handle_size),
                        &elem->pos2
                    });

                    if (is_line_hovered || ImGui::IsItemActive()) {
                        t->visual_item_interacted_this_frame = true;
                    }

                    // Handle click-to-select for whole-line drag
                    if (is_line_just_clicked) {
                        ImGuiIO &click_io = ImGui::GetIO();
                        bool ctrl_held = click_io.KeyCtrl;
                        bool either_selected = s_visual_selected_items.count(&elem->pos) > 0 ||
                                               s_visual_selected_items.count(&elem->pos2) > 0;
                        if (ctrl_held) {
                            // Ctrl+Click: toggle both endpoints in the selection
                            if (either_selected) {
                                s_visual_selected_items.erase(&elem->pos);
                                s_visual_selected_items.erase(&elem->pos2);
                            } else {
                                s_visual_selected_items.insert(&elem->pos);
                                s_visual_selected_items.insert(&elem->pos2);
                            }
                        } else if (!either_selected) {
                            s_visual_selected_items.clear();
                            s_visual_selected_items.insert(&elem->pos);
                            s_visual_selected_items.insert(&elem->pos2);
                        }

                        // Signal the editor to select this decoration
                        strncpy(t->visual_drag_root_name, elem->id, sizeof(t->visual_drag_root_name) - 1);
                        t->visual_drag_root_name[sizeof(t->visual_drag_root_name) - 1] = '\0';
                        strncpy(t->visual_drag_goal_type, "Decoration", sizeof(t->visual_drag_goal_type) - 1);
                        t->visual_drag_goal_type[sizeof(t->visual_drag_goal_type) - 1] = '\0';
                        t->visual_drag_child_root_name[0] = '\0';
                        t->visual_layout_just_clicked = true;
                    }

                    if (is_line_dragging) {
                        ImGuiIO &io = ImGui::GetIO();

                        strncpy(t->visual_drag_root_name, elem->id,
                                sizeof(t->visual_drag_root_name) - 1);
                        t->visual_drag_root_name[sizeof(t->visual_drag_root_name) - 1] = '\0';
                        strncpy(t->visual_drag_goal_type, "Decoration",
                                sizeof(t->visual_drag_goal_type) - 1);
                        t->visual_drag_goal_type[sizeof(t->visual_drag_goal_type) - 1] = '\0';
                        t->visual_drag_child_root_name[0] = '\0';

                        if (!elem->pos.is_set) {
                            elem->pos.is_set = true;
                            elem->pos.x = roundf((x1 - t->camera_offset.x) / t->zoom_level);
                            elem->pos.y = roundf((y1 - t->camera_offset.y) / t->zoom_level);
                        }
                        if (!elem->pos2.is_set) {
                            elem->pos2.is_set = true;
                            elem->pos2.x = roundf((x2 - t->camera_offset.x) / t->zoom_level);
                            elem->pos2.y = roundf((y2 - t->camera_offset.y) / t->zoom_level);
                        }

                        float dx = io.MouseDelta.x / t->zoom_level;
                        float dy = io.MouseDelta.y / t->zoom_level;

                        // Move both line endpoints
                        elem->pos.x = fminf(fmaxf(roundf(elem->pos.x + dx), -MANUAL_POS_MAX),
                                            MANUAL_POS_MAX);
                        elem->pos.y = fminf(fmaxf(roundf(elem->pos.y + dy), -MANUAL_POS_MAX),
                                            MANUAL_POS_MAX);
                        elem->pos2.x = fminf(fmaxf(roundf(elem->pos2.x + dx), -MANUAL_POS_MAX),
                                             MANUAL_POS_MAX);
                        elem->pos2.y = fminf(fmaxf(roundf(elem->pos2.y + dy), -MANUAL_POS_MAX),
                                             MANUAL_POS_MAX);

                        // Multi-drag: move all other selected items
                        bool line_is_selected = s_visual_selected_items.count(&elem->pos) > 0 ||
                                                s_visual_selected_items.count(&elem->pos2) > 0;
                        if (line_is_selected) {
                            for (ManualPos *sel_pos: s_visual_selected_items) {
                                if (sel_pos == &elem->pos || sel_pos == &elem->pos2) continue;
                                init_unset_pos_from_screen(sel_pos, t->zoom_level, t->camera_offset);
                                sel_pos->x = fminf(fmaxf(roundf(sel_pos->x + dx), -MANUAL_POS_MAX),
                                                   MANUAL_POS_MAX);
                                sel_pos->y = fminf(fmaxf(roundf(sel_pos->y + dy), -MANUAL_POS_MAX),
                                                   MANUAL_POS_MAX);
                            }
                        }

                        t->visual_layout_just_dragged = true;
                        SDL_SetAtomicInt(&g_templates_changed, 1);
                    }

                    // Tooltip showing both endpoints
                    if (is_line_hovered || is_line_dragging) {
                        if (is_line_hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                        char tooltip[512];
                        if (is_line_dragging) {
                            snprintf(tooltip, sizeof(tooltip),
                                     "Decoration: \"%s\" - Line\n\n"
                                     "Endpoint 1:  X: %.0f   Y: %.0f\n"
                                     "Endpoint 2:  X: %.0f   Y: %.0f",
                                     elem->id,
                                     elem->pos.x, elem->pos.y,
                                     elem->pos2.x, elem->pos2.y);
                        } else {
                            snprintf(tooltip, sizeof(tooltip),
                                     "Decoration: \"%s\" - Line\n\n"
                                     "Endpoint 1:  X: %.0f   Y: %.0f\n"
                                     "Endpoint 2:  X: %.0f   Y: %.0f\n"
                                     "Drag to reposition entire line.",
                                     elem->id,
                                     elem->pos.x, elem->pos.y,
                                     elem->pos2.x, elem->pos2.y);
                        }
                        ImGui::SetTooltip("%s", tooltip);
                    }
                    ImGui::PopID();
                }
                break;
            }
            case DECORATION_ARROW: {
                // --- Determine arrow opacity based on linked goal completion ---
                bool start_completed = is_goal_completed_by_root(t->template_data, elem->start_goal_root,
                                                                 elem->start_goal_stage, nullptr);
                bool end_completed = is_goal_completed_by_root(t->template_data, elem->end_goal_root,
                                                               elem->end_goal_stage, nullptr);

                // Hide arrow if end goal is completed and hiding mode is HIDE_ALL_COMPLETED
                if (end_completed && elem->end_goal_root[0] != '\0' &&
                    settings->goal_hiding_mode == HIDE_ALL_COMPLETED) {
                    break;
                }

                // Determine effective opacity
                float effective_opacity;
                if (elem->start_goal_root[0] == '\0') {
                    // No start goal linked - use opacity_after (fully visible)
                    effective_opacity = elem->opacity_after;
                } else {
                    effective_opacity = start_completed ? elem->opacity_after : elem->opacity_before;
                }

                ImU32 arrow_color = IM_COL32(settings->text_color.r, settings->text_color.g,
                                             settings->text_color.b,
                                             (int) (settings->text_color.a * effective_opacity));

                float scaled_thickness = elem->thickness * t->zoom_level;

                // Build the full path: tail -> bends -> tip
                // Max points = 2 (tail + tip) + MAX_ARROW_BENDS
                float pts_x[MAX_ARROW_BENDS + 2];
                float pts_y[MAX_ARROW_BENDS + 2];
                int pt_count = 0;

                // Tail (pos)
                if (elem->pos.is_set) {
                    pts_x[pt_count] = (elem->pos.x * t->zoom_level) + t->camera_offset.x;
                    pts_y[pt_count] = (elem->pos.y * t->zoom_level) + t->camera_offset.y;
                } else {
                    pts_x[pt_count] = (100.0f * t->zoom_level) + t->camera_offset.x;
                    pts_y[pt_count] = (100.0f * t->zoom_level) + t->camera_offset.y;
                }
                pt_count++;

                // Bends
                for (int b = 0; b < elem->bend_count; b++) {
                    if (elem->bends[b].is_set) {
                        pts_x[pt_count] = (elem->bends[b].x * t->zoom_level) + t->camera_offset.x;
                        pts_y[pt_count] = (elem->bends[b].y * t->zoom_level) + t->camera_offset.y;
                    } else {
                        pts_x[pt_count] = (150.0f * t->zoom_level) + t->camera_offset.x;
                        pts_y[pt_count] = (100.0f * t->zoom_level) + t->camera_offset.y;
                    }
                    pt_count++;
                }

                // Tip (pos2)
                if (elem->pos2.is_set) {
                    pts_x[pt_count] = (elem->pos2.x * t->zoom_level) + t->camera_offset.x;
                    pts_y[pt_count] = (elem->pos2.y * t->zoom_level) + t->camera_offset.y;
                } else {
                    pts_x[pt_count] = (200.0f * t->zoom_level) + t->camera_offset.x;
                    pts_y[pt_count] = (100.0f * t->zoom_level) + t->camera_offset.y;
                }
                pt_count++;

                // Compute arrowhead geometry: the line ends at the arrowhead base,
                // and the triangle extends beyond to the tip (drag point).
                float scaled_head = elem->arrowhead_size * t->zoom_level;
                float ndx_tip = 0.0f, ndy_tip = 0.0f; // Normalized direction of last segment
                bool has_arrowhead = false;

                if (pt_count >= 2) {
                    float tip_x = pts_x[pt_count - 1];
                    float tip_y = pts_y[pt_count - 1];
                    float prev_x = pts_x[pt_count - 2];
                    float prev_y = pts_y[pt_count - 2];

                    float dx = tip_x - prev_x;
                    float dy = tip_y - prev_y;
                    float seg_len = sqrtf(dx * dx + dy * dy);
                    if (seg_len > 0.001f) {
                        ndx_tip = dx / seg_len;
                        ndy_tip = dy / seg_len;
                        has_arrowhead = true;

                        // Shorten the last point so the line ends at the arrowhead base,
                        // but extend slightly into the triangle to prevent an anti-aliasing seam
                        float shorten = fminf(scaled_head, seg_len * 0.9f); // Don't overshoot past the previous point
                        float overlap = fminf(0.5f, shorten * 0.1f);
                        pts_x[pt_count - 1] = tip_x - ndx_tip * (shorten - overlap);
                        pts_y[pt_count - 1] = tip_y - ndy_tip * (shorten - overlap);
                    }
                }

                // Draw polyline (seamless joins, no overlap artifacts at bends)
                if (pt_count >= 2) {
                    ImVec2 polyline_pts[MAX_ARROW_BENDS + 2];
                    for (int p = 0; p < pt_count; p++) {
                        polyline_pts[p] = ImVec2(pts_x[p], pts_y[p]);
                    }
                    draw_list->AddPolyline(polyline_pts, pt_count, arrow_color, ImDrawFlags_None, scaled_thickness);
                }

                // Draw arrowhead triangle extending from the line end to the actual tip
                if (has_arrowhead && pt_count >= 2) {
                    // Restore the actual tip position (the drag point)
                    float actual_tip_x, actual_tip_y;
                    if (elem->pos2.is_set) {
                        actual_tip_x = (elem->pos2.x * t->zoom_level) + t->camera_offset.x;
                        actual_tip_y = (elem->pos2.y * t->zoom_level) + t->camera_offset.y;
                    } else {
                        actual_tip_x = (200.0f * t->zoom_level) + t->camera_offset.x;
                        actual_tip_y = (100.0f * t->zoom_level) + t->camera_offset.y;
                    }

                    // Perpendicular direction for arrowhead width
                    float pdx = -ndy_tip;
                    float pdy = ndx_tip;

                    // Triangle: tip point, and two base corners
                    float base_x = actual_tip_x - ndx_tip * scaled_head;
                    float base_y = actual_tip_y - ndy_tip * scaled_head;
                    ImVec2 p1 = ImVec2(actual_tip_x, actual_tip_y);
                    ImVec2 p2 = ImVec2(base_x + pdx * scaled_head * 0.4f,
                                       base_y + pdy * scaled_head * 0.4f);
                    ImVec2 p3 = ImVec2(base_x - pdx * scaled_head * 0.4f,
                                       base_y - pdy * scaled_head * 0.4f);
                    draw_list->AddTriangleFilled(p1, p2, p3, arrow_color);
                }

                // --- Visual layout dragging ---
                if (t->is_visual_layout_editing) {
                    float handle_size = fmaxf(8.0f, scaled_thickness * 2.0f);

                    // Tail drag handle
                    char drag_id_tail[128];
                    snprintf(drag_id_tail, sizeof(drag_id_tail), "drag_deco_%s_tail", elem->id);
                    handle_visual_layout_dragging(t, drag_id_tail,
                                                  ImVec2(pts_x[0] - handle_size * 0.5f, pts_y[0] - handle_size * 0.5f),
                                                  ImVec2(handle_size, handle_size),
                                                  elem->pos, "Decoration", elem->id, "Tail",
                                                  elem->id);

                    // Bend drag handles
                    for (int b = 0; b < elem->bend_count; b++) {
                        char drag_id_bend[128];
                        snprintf(drag_id_bend, sizeof(drag_id_bend), "drag_deco_%s_bend%d", elem->id, b);
                        char bend_label[32];
                        snprintf(bend_label, sizeof(bend_label), "Bend %d", b + 1);
                        handle_visual_layout_dragging(t, drag_id_bend,
                                                      ImVec2(pts_x[1 + b] - handle_size * 0.5f,
                                                             pts_y[1 + b] - handle_size * 0.5f),
                                                      ImVec2(handle_size, handle_size),
                                                      elem->bends[b], "Decoration", elem->id, bend_label,
                                                      elem->id);
                    }

                    // Tip drag handle (at the actual triangle tip, not the shortened line end)
                    float actual_tip_drag_x = elem->pos2.is_set
                                                  ? (elem->pos2.x * t->zoom_level) + t->camera_offset.x
                                                  : (200.0f * t->zoom_level) + t->camera_offset.x;
                    float actual_tip_drag_y = elem->pos2.is_set
                                                  ? (elem->pos2.y * t->zoom_level) + t->camera_offset.y
                                                  : (100.0f * t->zoom_level) + t->camera_offset.y;
                    char drag_id_tip[128];
                    snprintf(drag_id_tip, sizeof(drag_id_tip), "drag_deco_%s_tip", elem->id);
                    handle_visual_layout_dragging(t, drag_id_tip,
                                                  ImVec2(actual_tip_drag_x - handle_size * 0.5f,
                                                         actual_tip_drag_y - handle_size * 0.5f),
                                                  ImVec2(handle_size, handle_size),
                                                  elem->pos2, "Decoration", elem->id, "Tip",
                                                  elem->id);

                    // Whole-arrow drag handle at center between tail and actual tip
                    float mid_x = (pts_x[0] + actual_tip_drag_x) * 0.5f;
                    float mid_y = (pts_y[0] + actual_tip_drag_y) * 0.5f;
                    float arrow_handle_size = fmaxf(handle_size, scaled_thickness * 3.0f);

                    // Draw midpoint marker
                    float marker_radius = fmaxf(4.0f, scaled_thickness * 0.8f);
                    draw_list->AddCircleFilled(ImVec2(mid_x, mid_y), marker_radius,
                                               IM_COL32(255, 255, 255, 180));
                    draw_list->AddCircle(ImVec2(mid_x, mid_y), marker_radius,
                                         IM_COL32(0, 0, 0, 200), 0, 1.5f);

                    char drag_id_arrow[128];
                    snprintf(drag_id_arrow, sizeof(drag_id_arrow), "drag_deco_%s_arrow", elem->id);

                    ImGui::PushID(drag_id_arrow);
                    ImVec2 arrow_handle_pos = ImVec2(mid_x - arrow_handle_size * 0.5f,
                                                     mid_y - arrow_handle_size * 0.5f);
                    ImGui::SetCursorScreenPos(arrow_handle_pos);
                    ImGui::InvisibleButton("##drag_handle", ImVec2(arrow_handle_size, arrow_handle_size));

                    bool is_arrow_dragging = ImGui::IsItemActive() &&
                                             ImGui::IsMouseDragging(ImGuiMouseButton_Left);
                    bool is_arrow_hovered = ImGui::IsItemHovered();
                    bool is_arrow_just_clicked = ImGui::IsItemActivated();

                    // Register all points for selection
                    s_visual_layout_items.push_back({
                        arrow_handle_pos, ImVec2(arrow_handle_size, arrow_handle_size), &elem->pos
                    });
                    s_visual_layout_items.push_back({
                        arrow_handle_pos, ImVec2(arrow_handle_size, arrow_handle_size), &elem->pos2
                    });
                    for (int b = 0; b < elem->bend_count; b++) {
                        s_visual_layout_items.push_back({
                            arrow_handle_pos, ImVec2(arrow_handle_size, arrow_handle_size), &elem->bends[b]
                        });
                    }

                    if (is_arrow_hovered || ImGui::IsItemActive()) {
                        t->visual_item_interacted_this_frame = true;
                    }

                    // Click-to-select for whole-arrow drag
                    if (is_arrow_just_clicked) {
                        ImGuiIO &click_io = ImGui::GetIO();
                        bool ctrl_held = click_io.KeyCtrl;
                        bool any_point_selected = s_visual_selected_items.count(&elem->pos) > 0 ||
                                                  s_visual_selected_items.count(&elem->pos2) > 0;
                        if (ctrl_held) {
                            // Ctrl+Click: toggle all arrow points in the selection
                            if (any_point_selected) {
                                s_visual_selected_items.erase(&elem->pos);
                                s_visual_selected_items.erase(&elem->pos2);
                                for (int b = 0; b < elem->bend_count; b++) {
                                    s_visual_selected_items.erase(&elem->bends[b]);
                                }
                            } else {
                                s_visual_selected_items.insert(&elem->pos);
                                s_visual_selected_items.insert(&elem->pos2);
                                for (int b = 0; b < elem->bend_count; b++) {
                                    s_visual_selected_items.insert(&elem->bends[b]);
                                }
                            }
                        } else if (!any_point_selected) {
                            s_visual_selected_items.clear();
                            s_visual_selected_items.insert(&elem->pos);
                            s_visual_selected_items.insert(&elem->pos2);
                            for (int b = 0; b < elem->bend_count; b++) {
                                s_visual_selected_items.insert(&elem->bends[b]);
                            }
                        }

                        // Signal the editor to select this decoration
                        strncpy(t->visual_drag_root_name, elem->id, sizeof(t->visual_drag_root_name) - 1);
                        t->visual_drag_root_name[sizeof(t->visual_drag_root_name) - 1] = '\0';
                        strncpy(t->visual_drag_goal_type, "Decoration", sizeof(t->visual_drag_goal_type) - 1);
                        t->visual_drag_goal_type[sizeof(t->visual_drag_goal_type) - 1] = '\0';
                        t->visual_drag_child_root_name[0] = '\0';
                        t->visual_layout_just_clicked = true;
                    }

                    if (is_arrow_dragging) {
                        ImGuiIO &io = ImGui::GetIO();

                        strncpy(t->visual_drag_root_name, elem->id, sizeof(t->visual_drag_root_name) - 1);
                        t->visual_drag_root_name[sizeof(t->visual_drag_root_name) - 1] = '\0';
                        strncpy(t->visual_drag_goal_type, "Decoration", sizeof(t->visual_drag_goal_type) - 1);
                        t->visual_drag_goal_type[sizeof(t->visual_drag_goal_type) - 1] = '\0';
                        t->visual_drag_child_root_name[0] = '\0';

                        // Init any unset positions
                        if (!elem->pos.is_set) {
                            elem->pos.is_set = true;
                            elem->pos.x = roundf((pts_x[0] - t->camera_offset.x) / t->zoom_level);
                            elem->pos.y = roundf((pts_y[0] - t->camera_offset.y) / t->zoom_level);
                        }
                        if (!elem->pos2.is_set) {
                            elem->pos2.is_set = true;
                            elem->pos2.x = roundf((pts_x[pt_count - 1] - t->camera_offset.x) / t->zoom_level);
                            elem->pos2.y = roundf((pts_y[pt_count - 1] - t->camera_offset.y) / t->zoom_level);
                        }

                        float dxm = io.MouseDelta.x / t->zoom_level;
                        float dym = io.MouseDelta.y / t->zoom_level;

                        // Move all arrow points
                        elem->pos.x = fminf(fmaxf(roundf(elem->pos.x + dxm), -MANUAL_POS_MAX), MANUAL_POS_MAX);
                        elem->pos.y = fminf(fmaxf(roundf(elem->pos.y + dym), -MANUAL_POS_MAX), MANUAL_POS_MAX);
                        elem->pos2.x = fminf(fmaxf(roundf(elem->pos2.x + dxm), -MANUAL_POS_MAX), MANUAL_POS_MAX);
                        elem->pos2.y = fminf(fmaxf(roundf(elem->pos2.y + dym), -MANUAL_POS_MAX), MANUAL_POS_MAX);
                        for (int b = 0; b < elem->bend_count; b++) {
                            if (!elem->bends[b].is_set) {
                                elem->bends[b].is_set = true;
                            }
                            elem->bends[b].x = fminf(fmaxf(roundf(elem->bends[b].x + dxm), -MANUAL_POS_MAX),
                                                     MANUAL_POS_MAX);
                            elem->bends[b].y = fminf(fmaxf(roundf(elem->bends[b].y + dym), -MANUAL_POS_MAX),
                                                     MANUAL_POS_MAX);
                        }

                        // Multi-drag: move all other selected items
                        bool arrow_is_selected = s_visual_selected_items.count(&elem->pos) > 0 ||
                                                 s_visual_selected_items.count(&elem->pos2) > 0;
                        if (arrow_is_selected) {
                            for (ManualPos *sel_pos: s_visual_selected_items) {
                                if (sel_pos == &elem->pos || sel_pos == &elem->pos2) continue;
                                bool is_own_bend = false;
                                for (int b = 0; b < elem->bend_count; b++) {
                                    if (sel_pos == &elem->bends[b]) {
                                        is_own_bend = true;
                                        break;
                                    }
                                }
                                if (is_own_bend) continue;
                                init_unset_pos_from_screen(sel_pos, t->zoom_level, t->camera_offset);
                                sel_pos->x = fminf(fmaxf(roundf(sel_pos->x + dxm), -MANUAL_POS_MAX), MANUAL_POS_MAX);
                                sel_pos->y = fminf(fmaxf(roundf(sel_pos->y + dym), -MANUAL_POS_MAX), MANUAL_POS_MAX);
                            }
                        }

                        t->visual_layout_just_dragged = true;
                        SDL_SetAtomicInt(&g_templates_changed, 1);
                    }

                    // Tooltip
                    if (is_arrow_hovered || is_arrow_dragging) {
                        if (is_arrow_hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                        char tooltip[512];
                        if (is_arrow_dragging) {
                            snprintf(tooltip, sizeof(tooltip),
                                     "Decoration: \"%s\" - Arrow\n\n"
                                     "Tail:  X: %.0f   Y: %.0f\n"
                                     "Tip:   X: %.0f   Y: %.0f",
                                     elem->id,
                                     elem->pos.x, elem->pos.y,
                                     elem->pos2.x, elem->pos2.y);
                        } else {
                            snprintf(tooltip, sizeof(tooltip),
                                     "Decoration: \"%s\" - Arrow\n\n"
                                     "Tail:  X: %.0f   Y: %.0f\n"
                                     "Tip:   X: %.0f   Y: %.0f\n"
                                     "Drag to reposition entire arrow.",
                                     elem->id,
                                     elem->pos.x, elem->pos.y,
                                     elem->pos2.x, elem->pos2.y);
                        }
                        ImGui::SetTooltip("%s", tooltip);
                    }
                    ImGui::PopID();
                }
                break;
            }
        }
    }
}

// END OF STATIC FUNCTIONS ------------------------------------

// Animate overlay, display more than just advancements
void tracker_render_gui(Tracker *t, AppSettings *settings) {
    if (!t || !t->template_data) return;

    if (settings->print_debug_status) {
        // Add this line to show the ImGui Metrics Window
        ImGui::ShowMetricsWindow();
    }

    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize));
    ImGui::Begin("TrackerMap", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Push the tracker font
    if (t->tracker_font) {
        ImGui::PushFont(t->tracker_font);
    }

    // Reset the hover flag at the START of the frame, used for scrollable lists
    t->is_hovering_scrollable_list = false;

    // Snapshot the previous frame's item list (complete) for lookups during multi-drag,
    // then clear the current list so it can be rebuilt as items render this frame.
    s_visual_layout_items_prev.swap(s_visual_layout_items);
    s_visual_layout_items.clear();
    t->visual_item_interacted_this_frame = false;

    // This is the starting Y position for all rendering.
    // Each section will render itself and update this value for the next section.
    float current_y = 50.0f;

    // Get the current game version
    MC_Version version = settings_get_version_from_string(settings->version_str);

    // Create separate vectors for advancements and recipes
    std::vector<TrackableCategory *> advancements_only;
    std::vector<TrackableCategory *> recipes_only;
    if (t->template_data) {
        for (int i = 0; i < t->template_data->advancement_count; ++i) {
            TrackableCategory *item = t->template_data->advancements[i];
            if (item) {
                if (item->is_recipe) {
                    recipes_only.push_back(item);
                } else {
                    advancements_only.push_back(item);
                }
            }
        }
    }

    // Build search-linked sets for counter/header search propagation
    build_search_linked_sets(t);

    //  Render All Sections in User-Defined Order
    for (int i = 0; i < SECTION_COUNT; ++i) {
        auto section_id = (TrackerSection) settings->section_order[i];
        switch (section_id) {
            case SECTION_COUNTERS:
                render_counter_goals_section(t, settings, current_y, "Counters");
                break;
            case SECTION_ADVANCEMENTS: {
                const char *title = (version <= MC_VERSION_1_11_2) ? "Achievements" : "Advancements";
                render_trackable_category_section(t, settings, current_y, advancements_only.data(),
                                                  advancements_only.size(), title, false, version);
                break;
            }
            case SECTION_RECIPES:
                // Only render if the version is modern and there are recipes to show
                if (version >= MC_VERSION_1_12 && !recipes_only.empty()) {
                    render_trackable_category_section(t, settings, current_y, recipes_only.data(),
                                                      recipes_only.size(), "Recipes", false, version);
                }
                break;
            case SECTION_UNLOCKS:
                render_simple_item_section(t, settings, current_y, t->template_data->unlocks,
                                           t->template_data->unlock_count,
                                           "Unlocks");
                break;
            case SECTION_STATS:
                render_trackable_category_section(t, settings, current_y, t->template_data->stats,
                                                  t->template_data->stat_count,
                                                  "Statistics", true, version);
                break;
            case SECTION_CUSTOM:
                render_custom_goals_section(t, settings, current_y, "Custom Goals");
                break;
            case SECTION_MULTISTAGE:
                render_multistage_goals_section(t, settings, current_y, "Multi-Stage Goals");
                break;
            case SECTION_COUNT:
                // Should not happen
                break;
        }
    }

    // Render decorations (text headers, lines, arrows) - only visible in manual layout mode
    render_decorations(t, settings);

    // --- Visual Layout Multi-Select ---
    if (t->is_visual_layout_editing && settings->use_manual_layout) {
        ImDrawList *fg_draw_list = ImGui::GetForegroundDrawList();

        // Draw highlight around selected items
        for (const auto &item: s_visual_layout_items) {
            if (s_visual_selected_items.count(item.pos) > 0) {
                ImVec2 p_min = item.screen_pos;
                ImVec2 p_max = ImVec2(p_min.x + item.size.x, p_min.y + item.size.y);
                fg_draw_list->AddRect(p_min, p_max,
                                      IM_COL32(settings->text_color.r, settings->text_color.g,
                                               settings->text_color.b, 180),
                                      2.0f, 0, 2.0f);
            }
        }

        // Snapshot items that were selected before the rectangle started (for Ctrl+Drag additive mode)
        static std::unordered_set<ManualPos *> s_pre_rect_selected_items;

        // Selection rectangle logic
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_None)) {
            // Start selection rectangle on left-click if no item was interacted with
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !t->visual_item_interacted_this_frame) {
                t->visual_select_rect_active = true;
                t->visual_select_rect_start = ImGui::GetMousePos();
                // Ctrl+Drag: keep existing selection to add to it; normal drag: clear
                if (!ImGui::GetIO().KeyCtrl) {
                    s_visual_selected_items.clear();
                }
                s_pre_rect_selected_items = s_visual_selected_items;
            }
        }

        // Draw and finalize selection rectangle
        if (t->visual_select_rect_active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                ImVec2 mouse_pos = ImGui::GetMousePos();
                ImVec2 rect_min = ImVec2(fminf(t->visual_select_rect_start.x, mouse_pos.x),
                                         fminf(t->visual_select_rect_start.y, mouse_pos.y));
                ImVec2 rect_max = ImVec2(fmaxf(t->visual_select_rect_start.x, mouse_pos.x),
                                         fmaxf(t->visual_select_rect_start.y, mouse_pos.y));

                // Draw the selection rectangle
                fg_draw_list->AddRectFilled(rect_min, rect_max,
                                            IM_COL32(settings->text_color.r, settings->text_color.g,
                                                     settings->text_color.b, 40));
                fg_draw_list->AddRect(rect_min, rect_max,
                                      IM_COL32(settings->text_color.r, settings->text_color.g,
                                               settings->text_color.b, 150),
                                      0.0f, 0, 1.5f);

                // Live-update selection as rectangle is drawn
                // Start from the pre-rect snapshot (preserves Ctrl+Drag additive selection)
                s_visual_selected_items = s_pre_rect_selected_items;
                for (const auto &item: s_visual_layout_items) {
                    ImVec2 item_center = ImVec2(item.screen_pos.x + item.size.x * 0.5f,
                                                item.screen_pos.y + item.size.y * 0.5f);
                    if (item_center.x >= rect_min.x && item_center.x <= rect_max.x &&
                        item_center.y >= rect_min.y && item_center.y <= rect_max.y) {
                        s_visual_selected_items.insert(item.pos);
                    }
                }
            } else {
                // Mouse released — finalize selection
                t->visual_select_rect_active = false;
            }
        }

        // Clear selection when visual editing is disabled
    } else if (!t->is_visual_layout_editing && !s_visual_selected_items.empty()) {
        s_visual_selected_items.clear();
        t->visual_select_rect_active = false;
    }

    // --- Info Bar ---

    // Set the background color to match the tracker, with slight opacity.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4((float) settings->tracker_bg_color.r / 255.f,
                                                    (float) settings->tracker_bg_color.g / 255.f,
                                                    (float) settings->tracker_bg_color.b / 255.f,
                                                    230.0f / 255.f));

    // Determine the color for the WINDOW TITLE BAR text using the luminance check.
    ImVec4 title_text_color = ImVec4((float) settings->text_color.r / 255.f,
                                     (float) settings->text_color.g / 255.f,
                                     (float) settings->text_color.b / 255.f,
                                     (float) settings->text_color.a / 255.f);
    float luminance = (0.299f * settings->text_color.r + 0.587f * settings->text_color.g + 0.114f * settings->text_color
                       .b);
    if (luminance < 50) {
        // If the color is very dark, override with white for the title.
        title_text_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, title_text_color);

    // Enforce minimum width for the title bar
    const char *info_window_title =
            "Info | ESC: Settings | SPACE: Lock | Pan: RMB/MMB Drag | Zoom: Wheel | Click: LMB | Move Win: LMB Drag";
    ImVec2 title_size = ImGui::CalcTextSize(info_window_title);
    // Add padding (WindowPadding * 2 + extra for safety/frame borders)
    float min_info_width = title_size.x + (ImGui::GetStyle().WindowPadding.x * 2.0f) + 40.0f;

    // Constraint: Minimum width = title width, Maximum width = unlimited
    ImGui::SetNextWindowSizeConstraints(ImVec2(min_info_width, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

    // This Begin() call draws the window frame and title bar using the styles we just pushed.
    ImGui::Begin(info_window_title, nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    // Set Font Scale for Info Bar CONTENT
    float scale_factor_info = 1.0f; // Declare local scale factor
    if (t->tracker_font && t->tracker_font->LegacySize > 0.0f) {
        scale_factor_info = settings->tracker_ui_font_size / t->tracker_font->LegacySize;
    }
    ImGui::SetWindowFontScale(scale_factor_info); // Apply scale HERE, after Begin

    // Now that the title bar is drawn, we pop its text color and push the user's
    // original text color to use for the content inside the window.
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4((float) settings->text_color.r / 255.f,
                                                (float) settings->text_color.g / 255.f,
                                                (float) settings->text_color.b / 255.f,
                                                (float) settings->text_color.a / 255.f));

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    char info_buffer[512];
    char formatted_time[64];

    // Check if the run is 100% complete.
    bool is_run_complete = t->template_data->advancements_completed_count >= t->template_data->advancement_count &&
                           t->template_data->overall_progress_percentage >= 100.0f;

    // Use frozen IGT for the info window when the run is completed
    long long display_ticks = is_run_complete
                                  ? t->template_data->frozen_play_time_ticks
                                  : t->template_data->play_time_ticks;
    format_time(display_ticks, formatted_time, sizeof(formatted_time));

    if (is_run_complete) {
        snprintf(info_buffer, sizeof(info_buffer),
                 "*** RUN COMPLETED! *** |   Final Time: %s",
                 formatted_time);
    } else {
        // This is the original info string for when the run is in progress.
        char formatted_update_time[64];
        const char *adv_ach_label = (version >= MC_VERSION_1_12) ? "Adv" : "Ach";
        float last_update_time_5_seconds = floorf(t->time_since_last_update / 5.0f) * 5.0f;
        format_time_since_update(last_update_time_5_seconds, formatted_update_time, sizeof(formatted_update_time));

        char temp_chunk[256];
        bool show_adv_counter = (t->template_data->advancement_goal_count > 0);
        bool show_prog_percent = (t->template_data->total_progress_steps > 0);

        // For receivers, show "Syncing with <Host>" instead of world name
        const char *info_world = t->world_name;
        char info_sync_buf[MAX_PATH_LENGTH];
        bool info_rcv_connected = (settings->network_mode == NETWORK_RECEIVER &&
                                   g_coop_ctx && coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED);
        if (info_rcv_connected) {
            CoopLobbyPlayer info_lobby[COOP_MAX_LOBBY];
            int info_lobby_count = coop_net_get_lobby_players(g_coop_ctx, info_lobby, COOP_MAX_LOBBY);
            const char *info_host_name = "Host";
            for (int i = 0; i < info_lobby_count; i++) {
                if (info_lobby[i].is_host) {
                    info_host_name = info_lobby[i].display_name[0] != '\0'
                                         ? info_lobby[i].display_name
                                         : info_lobby[i].username;
                    break;
                }
            }
            snprintf(info_sync_buf, sizeof(info_sync_buf), "Syncing with %s", info_host_name);
            info_world = info_sync_buf;
        }

        // Start with the world name and run details
        if (settings->category_display_name[0] != '\0') {
            snprintf(info_buffer, sizeof(info_buffer), "%s  |  %s - %s",
                     info_world,
                     settings->display_version_str,
                     settings->category_display_name);
        } else {
            snprintf(info_buffer, sizeof(info_buffer), "%s  |  %s",
                     info_world,
                     settings->display_version_str);
        }

        // Conditionally add the progress part
        if (show_adv_counter && show_prog_percent) {
            snprintf(temp_chunk, sizeof(temp_chunk), "  |  %s: %d/%d  -  Prog: %.2f%%",
                     adv_ach_label, t->template_data->advancements_completed_count,
                     t->template_data->advancement_goal_count, t->template_data->overall_progress_percentage);
            strncat(info_buffer, temp_chunk, sizeof(info_buffer) - strlen(info_buffer) - 1);
        } else if (show_adv_counter) {
            snprintf(temp_chunk, sizeof(temp_chunk), "  |  %s: %d/%d",
                     adv_ach_label, t->template_data->advancements_completed_count,
                     t->template_data->advancement_goal_count);
            strncat(info_buffer, temp_chunk, sizeof(info_buffer) - strlen(info_buffer) - 1);
        } else if (show_prog_percent) {
            snprintf(temp_chunk, sizeof(temp_chunk), "  |  Prog: %.2f%%",
                     t->template_data->overall_progress_percentage);
            strncat(info_buffer, temp_chunk, sizeof(info_buffer) - strlen(info_buffer) - 1);
        }

        // Add the IGT and Update Timer
        snprintf(temp_chunk, sizeof(temp_chunk), "  |  %s IGT  |  %s %s",
                 formatted_time,
                 settings->using_hermes ? "Synced:" : "Upd:",
                 formatted_update_time);
        strncat(info_buffer, temp_chunk, sizeof(info_buffer) - strlen(info_buffer) - 1);
    }

    // This text will now be drawn using the user's selected color.
    ImGui::TextUnformatted(info_buffer);

    // Add a tooltip to the info window to explain the progress metrics.
    if (ImGui::IsWindowHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);

        ImGui::TextUnformatted("Progress Text");

        ImGui::BulletText("World: Shows the current world name and 'Syncing with <Host>' if in receiver mode.");
        ImGui::BulletText("Run Details: Shows the Display Version & Display Category.");
        ImGui::BulletText("Progress: Shows the main adv/ach counter and overall percentage.");
        ImGui::BulletText("IGT: Displays the in-game time from the stats file (ticks).");
        ImGui::BulletText("Update Timer: Shows the time since the last game file update.");

        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Progress Breakdown");

        if (version <= MC_VERSION_1_11_2) {
            // Achievements
            ImGui::BulletText(
                "The Achievements counter tracks only the main goals defined in the \"advancements\" section of your template file.");
        } else {
            ImGui::BulletText(
                "The Advancements counter tracks only the main goals defined in the \"advancements\" section of your template file.");
        }

        ImGui::BulletText(
            "The Progress %% shows your total completion across all individual sub-tasks from all categories.\n"
            "Each of the following tasks has an equal weight in the calculation:");
        ImGui::Indent();
        ImGui::BulletText("Counter Goals");
        if (version > MC_VERSION_1_6_4 && version <= MC_VERSION_1_11_2) {
            // Achievement Criteria
            ImGui::BulletText("Achievement Criteria");
        } else {
            // Advancement Criteria
            ImGui::BulletText("Advancement Criteria");
            ImGui::BulletText("Recipes");
        }

        if (version == MC_VERSION_25W14CRAFTMINE) {
            ImGui::BulletText("Unlocks");
        }

        ImGui::BulletText("Individual Sub-Stats");
        ImGui::BulletText("Custom Goals");
        ImGui::BulletText("Multi-Stage Goal Stages");
        ImGui::Unindent();

        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    ImGui::End();

    // Pop the two style colors still on the stack (WindowBg and the content's Text color).
    ImGui::PopStyleColor(2);

    // Reset Font Scale after Info Bar
    ImGui::SetWindowFontScale(1.0f);

    // Layout Control Buttons

    // --- Apply Font Scale for Control Bar Calculations AND Rendering ---
    float scale_factor_controls = 1.0f;
    if (t->tracker_font && t->tracker_font->LegacySize > 0.0f) {
        scale_factor_controls = settings->tracker_ui_font_size / t->tracker_font->LegacySize;
    }
    ImGui::SetWindowFontScale(scale_factor_controls); // Apply scale FIRST

    // --- Calculate control sizes AFTER setting the scale, using ImGui styles ---
    ImGuiStyle &style = ImGui::GetStyle();
    const float button_padding_x = style.ItemSpacing.x; // Use ImGui's item spacing
    const float frame_padding_x = style.FramePadding.x;
    // const float frame_padding_y = style.FramePadding.y;
    const float frame_height = ImGui::GetFrameHeight(); // Scaled height for buttons/checkboxes

    const float clear_button_width = frame_height; // Square button
    const float search_box_width = 250.0f; // Keep this fixed for now

    // Calculate sizes for Checkboxes (Square + Spacing + Label + Padding)
    // Checkbox square is roughly frame_height wide.
    ImVec2 lock_text_size = ImGui::CalcTextSize("Lock Layout");
    float lock_checkbox_width = frame_height + style.ItemInnerSpacing.x + lock_text_size.x + frame_padding_x * 0.5f;
    // Approx checkbox width calculation

    ImVec2 reset_text_size = ImGui::CalcTextSize("Reset Camera");
    float reset_checkbox_width = frame_height + style.ItemInnerSpacing.x + reset_text_size.x + frame_padding_x * 0.5f;

    ImVec2 manual_layout_text_size = ImGui::CalcTextSize("Manual Layout");
    float manual_layout_checkbox_width = frame_height + style.ItemInnerSpacing.x + manual_layout_text_size.x +
                                         frame_padding_x * 0.5f;

    ImVec2 notes_text_size = ImGui::CalcTextSize("Notes");
    float notes_checkbox_width = frame_height + style.ItemInnerSpacing.x + notes_text_size.x + frame_padding_x * 0.5f;

    // Calculate total width using ImGui's ItemSpacing
    // 6 elements = 5 gaps between them
    float controls_total_width = clear_button_width + button_padding_x +
                                 search_box_width + button_padding_x +
                                 lock_checkbox_width + button_padding_x +
                                 reset_checkbox_width + button_padding_x +
                                 manual_layout_checkbox_width + button_padding_x +
                                 notes_checkbox_width;

    // Calculate button height consistently
    float control_height = frame_height; // All controls should share the same height

    // --- Position the Controls window ---
    ImVec2 controls_window_pos = ImVec2(io.DisplaySize.x - controls_total_width - style.WindowPadding.x,
                                        // Use window padding for edge spacing
                                        io.DisplaySize.y - control_height - style.WindowPadding.y);
    ImVec2 controls_window_size = ImVec2(controls_total_width, control_height);

    ImGui::SetNextWindowPos(controls_window_pos);
    ImGui::SetNextWindowSize(controls_window_size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // No internal padding needed for this wrapper

    ImGui::Begin("Controls", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();

    ImGui::SetWindowFontScale(scale_factor_controls);

    // Style for transparent buttons and checkbox
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4((float) settings->text_color.r / 255.f,
                                                (float) settings->text_color.g / 255.f,
                                                (float) settings->text_color.b / 255.f,
                                                (float) settings->text_color.a / 255.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4((float) settings->text_color.r / 255.f,
                                                  (float) settings->text_color.g / 255.f,
                                                  (float) settings->text_color.b / 255.f,
                                                  (float) settings->text_color.a / 255.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, (float) ADVANCELY_FADED_ALPHA / 255.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.2f, 0.2f, 0.2f, (float) ADVANCELY_FADED_ALPHA / 255.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4((float) settings->text_color.r / 255.f,
                                                     (float) settings->text_color.g / 255.f,
                                                     (float) settings->text_color.b / 255.f, 1.0f));

    // We use the tracker's main text color but with the faded alpha value.
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4((float) settings->text_color.r / 255.f,
                                                        (float) settings->text_color.g / 255.f,
                                                        (float) settings->text_color.b / 255.f,
                                                        (float) ADVANCELY_FADED_ALPHA / 255.0f)); //

    // Render the "X" button OR an invisible dummy widget to hold the space
    if (t->search_buffer[0] != '\0') {
        if (ImGui::Button("X##ClearSearch", ImVec2(clear_button_width, 0))) {
            t->search_buffer[0] = '\0'; // Clear the buffer
        }
        if (ImGui::IsItemHovered()) {
            char clear_search_tooltip_buffer[1024];
            snprintf(clear_search_tooltip_buffer, sizeof(clear_search_tooltip_buffer),
                     "Clear Search");
            ImGui::SetTooltip("%s", clear_search_tooltip_buffer);
        }
    } else {
        // Render an invisible widget of the same size to hold the space
        ImGui::Dummy(ImVec2(clear_button_width, 0));
    }

    ImGui::SameLine();

    // Render the search box
    // Apply styles for placeholder and border
    bool search_is_active = (t->search_buffer[0] != '\0');
    if (search_is_active) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4((float) settings->text_color.r / 255.f,
                                                      (float) settings->text_color.g / 255.f,
                                                      (float) settings->text_color.b / 255.f, 0.8f));
    }

    // Ctrl + F or Cmd + F to focus the search box
    if (t->focus_search_box_requested) {
        ImGui::SetKeyboardFocusHere();
        t->focus_search_box_requested = false; // Reset the flag immediately after use
    }

    // Search box
    ImGui::SetNextItemWidth(search_box_width);
    ImGui::InputTextWithHint("##SearchBox", "Search...", t->search_buffer, sizeof(t->search_buffer));

    // Pop color
    ImGui::PopStyleColor(1);
    if (search_is_active) ImGui::PopStyleColor(); // Pop border color

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 50.0f); // Helps the text wrap nicely

        ImGui::TextUnformatted(
            "Search for goals by name, root name, or icon path (case-insensitive).\n"
            "You can also use Ctrl + F (or Cmd + F on macOS).\n"
            "Using the search filter also dynamically updates the completion counters in the section headers.");
        ImGui::Separator();
        ImGui::TextUnformatted("It applies the filter to anything currently visible in the following way:");

        if (version <= MC_VERSION_1_6_4) {
            // Achievements, no advancement criteria
            ImGui::BulletText(
                "Achievements: Shows a category if its title matches.");
            ImGui::BulletText(
                "Statistics: Shows a category if its title or any of its sub-stats match.\n"
                "If only a sub-stat matches, only that specific one will be shown under its parent.");
        } else if (version <= MC_VERSION_1_11_2) {
            // Achievements and critiera
            ImGui::BulletText(
                "Achievements & Statistics: Shows a category if its title or any of its sub-criteria match.\n"
                "If only a sub-criterion matches, only that specific one will be shown under its parent.");
        } else {
            // Modern
            ImGui::BulletText(
                "Advancements, Recipes & Statistics: Shows a category if its title or any of its sub-criteria match.\n"
                "If only a sub-criterion matches, only that specific one will be shown under its parent.");
        }

        if (version == MC_VERSION_25W14CRAFTMINE) {
            // Unlocks
            ImGui::BulletText("Unlocks & Custom Goals: Shows the goal if its name matches the search term.");
        } else {
            ImGui::BulletText("Custom Goals: Shows the goal if its name matches the search term.");
        }

        ImGui::BulletText(
            "Multi-Stage Goals: Shows the goal if its main title, root name, icon path, or the text,\n"
            "stage ID, or icon path of its currently active stage matches the search term.");

        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    // Player dropdown — only visible in coop
    {
        CoopNetState player_dd_state = g_coop_ctx ? coop_net_get_state(g_coop_ctx) : COOP_NET_IDLE;
        bool in_coop = (player_dd_state == COOP_NET_LISTENING || player_dd_state == COOP_NET_CONNECTED);
        if (in_coop && settings->coop_player_count > 0) {
            ImGui::SameLine();

            // Build preview label
            const char *preview = "All Players";
            if (t->selected_coop_player_idx >= 0 && t->selected_coop_player_idx < settings->coop_player_count) {
                const CoopPlayer *sel = &settings->coop_players[t->selected_coop_player_idx];
                preview = sel->display_name[0] != '\0' ? sel->display_name : sel->username;
            }

            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::BeginCombo("##PlayerView", preview, ImGuiComboFlags_None)) {
                // "All Players" entry
                bool is_all = (t->selected_coop_player_idx == -1);
                if (ImGui::Selectable("All Players", is_all)) {
                    if (!is_all) {
                        t->selected_coop_player_idx = -1;
                        // Apply the cached snapshot on the next frame instead of
                        // triggering a full disk reload — remote players may not
                        // have saved their files for several minutes.
                        t->coop_view_dirty = 1;
                    }
                }
                if (is_all) ImGui::SetItemDefaultFocus();

                // Individual players
                for (int pi = 0; pi < settings->coop_player_count; pi++) {
                    const CoopPlayer *p = &settings->coop_players[pi];
                    const char *label = p->display_name[0] != '\0' ? p->display_name : p->username;
                    char player_label[128];
                    snprintf(player_label, sizeof(player_label), "%s##player_%d", label, pi);
                    bool is_selected = (t->selected_coop_player_idx == pi);
                    if (ImGui::Selectable(player_label, is_selected)) {
                        if (!is_selected) {
                            t->selected_coop_player_idx = pi;
                            t->coop_view_dirty = 1;
                        }
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buf[256];
                snprintf(tooltip_buf, sizeof(tooltip_buf),
                         "View progress for a specific player or all players combined.\n"
                         "'All Players' shows the merged Co-op progress.");
                ImGui::SetTooltip("%s", tooltip_buf);
            }
        } else {
            // Reset to All Players when not in coop
            t->selected_coop_player_idx = -1;
        }
    }

    ImGui::SameLine();

    // "Lock Layout" checkbox
    if (ImGui::Checkbox("Lock Layout", &t->layout_locked)) {
        if (t->layout_locked) {
            // When locking, store the current scroll position
            t->locked_layout_width = io.DisplaySize.x / t->zoom_level; // Store the current width
        }
    }

    if (ImGui::IsItemHovered()) {
        char lock_layout_tooltip_buffer[1024];
        snprintf(lock_layout_tooltip_buffer, sizeof(lock_layout_tooltip_buffer),
                 "Also toggled by pressing SPACE.\n"
                 "Prevents the layout from rearranging when zooming or resizing the window.\n"
                 "Adjusting the window width gives more control over\n"
                 "the exact amount of goals displayed per row.");
        ImGui::SetTooltip("%s", lock_layout_tooltip_buffer);
    }

    ImGui::SameLine();

    // "Reset Camera" checkbox (acts as a button), it turns off the lock layout
    static bool reset_dummy = false;
    if (ImGui::Checkbox("Reset Camera", &reset_dummy)) {
        t->camera_offset = ImVec2(0.0f, 0.0f);
        t->zoom_level = 1.0f;
        t->layout_locked = false; // Unlocks the layout
        reset_dummy = false; // Ensure it's getting unchecked
    }

    if (ImGui::IsItemHovered()) {
        char reset_layout_tooltip_buffer[1024];
        snprintf(reset_layout_tooltip_buffer, sizeof(reset_layout_tooltip_buffer),
                 "Resets camera position and zoom level to their defaults.");
        ImGui::SetTooltip("%s", reset_layout_tooltip_buffer);
    }

    ImGui::SameLine();

    ImGui::BeginDisabled(!t->template_data); // Prevent crashing if template_data is null
    bool temp_manual = settings->use_manual_layout;
    if (ImGui::Checkbox("Manual Layout", &temp_manual)) {
        settings->use_manual_layout = temp_manual;
        settings_save(settings, t->template_data, SAVE_CONTEXT_ALL); // Save the preference instantly
    }
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[256];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Toggles between procedural 'Auto Layout' and 'Manual Layout'.\n"
                 "Any non-manually placed goals get pushed to the right.");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Add the "Notes" checkbox
    ImGui::Checkbox("Notes", &t->notes_window_open);
    if (ImGui::IsItemHovered()) {
        char notes_tooltip_buffer[1024];
        snprintf(notes_tooltip_buffer, sizeof(notes_tooltip_buffer),
                 "Notes Window\n"
                 "--------------------------------\n"
                 "Toggles a persistent text editor for keeping notes.\n"
                 "The system has two modes, configurable inside the window:\n\n"
                 " - Per-World (Default): Notes are saved for each world individually.\n"
                 "   The last 32 worlds are remembered.\n"
                 " - Per-Template: Notes are shared for the currently loaded template permanently.\n\n"
                 "--------------------------------\n"
                 "The window's size and position are remembered across sessions.\n"
                 "Anything you type is immediately saved.\n"
                 "Hotkeys are disabled while typing in the notes window.\n"
                 "The maximum note size is 64KB.");
        ImGui::SetTooltip("%s", notes_tooltip_buffer);
    }

    ImGui::PopStyleColor(5); // Pop the style colors, there's 5 of them

    // Reset Font Scale after Control Bar content
    ImGui::SetWindowFontScale(1.0f); // Reset scale HERE, before End

    ImGui::End(); // End Layout Controls Window

    // Reset Font Scale after Control Bar
    ImGui::SetWindowFontScale(1.0f);


    // --- Render Notes Window ---

    if (t->notes_window_open) {
        // A static title with a hidden ID (##AdvancelyNotes) ensures the window's size
        // and position are always remembered by ImGui.
        const char *notes_window_title = "Notes##AdvancelyNotes";

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(notes_window_title, &t->notes_window_open)) {
            // Display the dynamic context (world or template name) inside the window.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f)); // Dim the context text
            if (settings->per_world_notes) {
                ImGui::Text("World: %s", t->world_name[0] != '\0' ? t->world_name : "No World Loaded");
            } else {
                char formatted_category[128];
                format_category_string(settings->category, formatted_category, sizeof(formatted_category));
                char formatted_flag[128];
                format_category_string(settings->optional_flag, formatted_flag, sizeof(formatted_flag));
                ImGui::Text("Template: %s - %s%s%s",
                            settings->version_str,
                            formatted_category,
                            *settings->optional_flag ? " - " : "",
                            *settings->optional_flag ? formatted_flag : "");
            }
            ImGui::PopStyleColor();
            ImGui::Separator();

            // If the "Use Settings Font" option is enabled, push the UI font.
            bool roboto_font_pushed = false;
            if (settings->notes_use_roboto_font && t->roboto_font) {
                ImGui::PushFont(t->roboto_font);
                roboto_font_pushed = true; // 2. Set the flag to true only if we push the font.
            }

            // Calculate the editor size to leave just enough space for the checkbox at the bottom.
            float bottom_widget_height = ImGui::GetFrameHeightWithSpacing();
            ImVec2 editor_size = ImVec2(-FLT_MIN, -bottom_widget_height);

            // Draw the text editor and generate dynamic IDs for widgets
            char widget_id[64];
            snprintf(widget_id, sizeof(widget_id), "##NotesEditor%d", t->notes_widget_id_counter);

            // Prepare the data packet to pass to our callback function.
            NotesCallbackData callback_data = {t, settings};

            // The ImGuiInputTextFlags_CallbackEdit flag tells ImGui to call our function on every modification.
            ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackEdit;

            ImGui::InputTextMultiline(widget_id, t->notes_buffer, sizeof(t->notes_buffer),
                                      editor_size, flags, NotesEditCallback, &callback_data);


            // --- Controls at the bottom of the window ---
            // Per-World Toggle — disabled for receivers in coop (no world file available)
            bool rcv_notes_locked = (settings->network_mode == NETWORK_RECEIVER &&
                                     g_coop_ctx && coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED);
            if (rcv_notes_locked) {
                // Force per-template for receivers and reload if it was per-world
                if (settings->per_world_notes) {
                    settings->per_world_notes = false;
                    tracker_update_notes_path(t, settings);
                    tracker_load_notes(t, settings);
                    settings_save(settings, nullptr, SAVE_CONTEXT_ALL);
                }
                ImGui::BeginDisabled();
            }
            if (ImGui::Checkbox("Per-World Notes", &settings->per_world_notes)) {
                // When toggled, immediately update the path and reload the notes
                tracker_update_notes_path(t, settings);
                tracker_load_notes(t, settings);
                settings_save(settings, nullptr, SAVE_CONTEXT_ALL); // Save the setting change
            }
            if (rcv_notes_locked)
                ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                char per_world_notes_tooltip_buffer[512];
                if (rcv_notes_locked) {
                    snprintf(per_world_notes_tooltip_buffer, sizeof(per_world_notes_tooltip_buffer),
                             "Per-World notes are not available in Co-op as a receiver.\n"
                             "Notes are saved per template while connected.");
                } else {
                    snprintf(per_world_notes_tooltip_buffer, sizeof(per_world_notes_tooltip_buffer),
                             "When enabled, notes are saved for each world individually.\n"
                             "When disabled, notes are shared for the current template.");
                }
                ImGui::SetTooltip("%s", per_world_notes_tooltip_buffer);
            }

            ImGui::SameLine();

            // Font Toggle (aligned to the right)
            const char *checkbox_label = "Use Settings/UI Font";
            float checkbox_width = ImGui::CalcTextSize(checkbox_label).x + ImGui::GetFrameHeightWithSpacing();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - checkbox_width - ImGui::GetStyle().WindowPadding.x);
            if (ImGui::Checkbox(checkbox_label, &settings->notes_use_roboto_font)) {
                settings_save(settings, nullptr, SAVE_CONTEXT_ALL);
            }
            if (ImGui::IsItemHovered()) {
                char use_settings_font_tooltip_buffer[512];
                snprintf(use_settings_font_tooltip_buffer, sizeof(use_settings_font_tooltip_buffer),
                         "Toggle whether to use the settings font and -size for the notes editor (better readability).");
                ImGui::SetTooltip(
                    "%s", use_settings_font_tooltip_buffer);
            }

            // Pop the UI font only if we pushed it in this frame.
            if (roboto_font_pushed) {
                // 3. Base the pop operation on the local flag, not the setting.
                ImGui::PopFont();
            }
        }
        // End() must always be called to match a Begin(), regardless of its return value.
        ImGui::End();
    }

    // Pan and zoom logic (Moved to bottom to respect is_hovering_scrollable_list flag set during render)
    if (ImGui::IsWindowHovered()) {
        if (!t->is_hovering_scrollable_list && io.MouseWheel != 0) {
            ImVec2 mouse_pos_in_window = ImGui::GetMousePos();
            ImVec2 mouse_pos_before_zoom = ImVec2((mouse_pos_in_window.x - t->camera_offset.x) / t->zoom_level,
                                                  (mouse_pos_in_window.y - t->camera_offset.y) / t->zoom_level);
            float old_zoom = t->zoom_level;
            t->zoom_level += io.MouseWheel * 0.1f * t->zoom_level;
            if (t->zoom_level < 0.1f) t->zoom_level = 0.1f;
            if (t->zoom_level > 10.0f) t->zoom_level = 10.0f;
            t->camera_offset.x += (mouse_pos_before_zoom.x * (old_zoom - t->zoom_level));
            t->camera_offset.y += (mouse_pos_before_zoom.y * (old_zoom - t->zoom_level));
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            t->camera_offset.x += io.MouseDelta.x;
            t->camera_offset.y += io.MouseDelta.y;
        }
    }

    // Pop the font at the very end, so everything inside TrackerMap uses it.
    if (t->tracker_font) {
        ImGui::PopFont();
    }


    ImGui::End(); // End TrackerMap window
}


// -------------------------------------------- TRACKER RENDERING END --------------------------------------------


void tracker_clear_coop_snapshot_cache(Tracker *t) {
    if (!t) return;
    for (int i = 0; i < MAX_COOP_PLAYERS; i++) {
        if (t->coop_player_snapshots[i]) {
            free(t->coop_player_snapshots[i]);
            t->coop_player_snapshots[i] = nullptr;
        }
        t->coop_player_snapshot_sizes[i] = 0;
    }
    if (t->coop_merged_snapshot) {
        free(t->coop_merged_snapshot);
        t->coop_merged_snapshot = nullptr;
    }
    t->coop_merged_snapshot_size = 0;
    t->coop_view_dirty = 0;
}

void tracker_reinit_template(Tracker *t, AppSettings *settings) {
    if (!t) return;

    log_message(LOG_INFO, "[TRACKER] Re-initializing template...\n");

    // Invalidate coop snapshot cache: serialized buffers are tied to the OLD
    // template layout (goal counts / order). Applying them after a template
    // swap would misalign fields or drop goals.
    tracker_clear_coop_snapshot_cache(t);

    // --- BACKUP SCROLL STATE ---
    // We use a map to store the scroll_y value keyed by the category's root_name.
    // This ensures we restore the correct scroll position to the correct category
    // even if the order changes or items are added/removed.
    std::map<std::string, float> scroll_backup;

    if (t->template_data) {
        // Backup Advancements
        if (t->template_data->advancements) {
            for (int i = 0; i < t->template_data->advancement_count; i++) {
                if (t->template_data->advancements[i] && t->template_data->advancements[i]->scroll_y > 0.0f) {
                    scroll_backup[t->template_data->advancements[i]->root_name] = t->template_data->advancements[i]->
                            scroll_y;
                }
            }
        }
        // Backup Stats
        if (t->template_data->stats) {
            for (int i = 0; i < t->template_data->stat_count; i++) {
                if (t->template_data->stats[i] && t->template_data->stats[i]->scroll_y > 0.0f) {
                    scroll_backup[t->template_data->stats[i]->root_name] = t->template_data->stats[i]->scroll_y;
                }
            }
        }
    }

    // Update the paths from settings.json
    tracker_reinit_paths(t, settings);

    // Reload background textures after paths are updated
    tracker_reload_background_textures(t, settings);

    // Free all the old advancement, stat, etc. data
    if (t->template_data) {
        tracker_free_template_data(t->template_data);

        // After clearing, ensure the snapshot name is also cleared to force a new snapshot
        t->template_data->snapshot_world_name[0] = '\0';
    }
    // Load and parse data from the new template files
    tracker_load_and_parse_data(t, settings);

    // --- RESTORE SCROLL STATE ---
    if (t->template_data && !scroll_backup.empty()) {
        // Restore Advancements
        if (t->template_data->advancements) {
            for (int i = 0; i < t->template_data->advancement_count; i++) {
                if (t->template_data->advancements[i]) {
                    auto it = scroll_backup.find(t->template_data->advancements[i]->root_name);
                    if (it != scroll_backup.end()) {
                        t->template_data->advancements[i]->scroll_y = it->second;
                    }
                }
            }
        }
        // Restore Stats
        if (t->template_data->stats) {
            for (int i = 0; i < t->template_data->stat_count; i++) {
                if (t->template_data->stats[i]) {
                    auto it = scroll_backup.find(t->template_data->stats[i]->root_name);
                    if (it != scroll_backup.end()) {
                        t->template_data->stats[i]->scroll_y = it->second;
                    }
                }
            }
        }
    }
}

void tracker_reinit_paths(Tracker *t, const AppSettings *settings) {
    if (!t || !settings) return;

    // Copy the template and lang paths and snapshot path (for legacy snapshots if needed)
    strncpy(t->advancement_template_path, settings->template_path, MAX_PATH_LENGTH - 1);
    t->advancement_template_path[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(t->lang_path, settings->lang_path, MAX_PATH_LENGTH - 1);
    t->lang_path[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(t->snapshot_path, settings->snapshot_path, MAX_PATH_LENGTH - 1);
    t->snapshot_path[MAX_PATH_LENGTH - 1] = '\0';


    MC_Version version = settings_get_version_from_string(settings->version_str);

    // Saves path should either be fixed_world_path or manual_saves_path
    const char *path_arg = (settings->path_mode == PATH_MODE_FIXED_WORLD)
                               ? settings->fixed_world_path
                               : settings->manual_saves_path;
    if (get_saves_path(t->saves_path, MAX_PATH_LENGTH, settings->path_mode, path_arg)) {
        log_message(LOG_INFO, "[TRACKER] Using saves path: %s\n", t->saves_path);


        // Find the specific world files using the correct flag

        // If using StatsPerWorld Mod on a legacy version trick the path finder
        // into looking for the stats file per world, still looking for IDs though in .dat file
        find_player_data_files(
            t->saves_path,
            version,
            settings->using_stats_per_world_legacy, // This toggles if StatsPerWorld mod is enabled, see above
            settings,
            t->world_name,
            t->advancements_path,
            t->stats_path,
            t->unlocks_path,
            MAX_PATH_LENGTH);

        // Hermes Live-Update Detection ---
        // Close any previously open handle first
        if (t->hermes_play_log) {
            fclose(t->hermes_play_log);
            t->hermes_play_log = nullptr;
            t->hermes_file_offset = 0;
            t->hermes_active = false;
        }

        if (t->world_name[0] != '\0' && t->saves_path[0] != '\0') {
            char hermes_log_path[MAX_PATH_LENGTH];
            snprintf(hermes_log_path, sizeof(hermes_log_path),
                     "%s/%s/hermes/restricted/play.log.enc", // Encrypted log file
                     t->saves_path, t->world_name);
            FILE *f = fopen(hermes_log_path, "rb");
            if (f) {
                t->hermes_play_log = f;
                t->hermes_file_offset = 0; // start from beginning on new world, then it appends
                t->hermes_active = true;
                log_message(LOG_INFO, "[TRACKER - HERMES] Detected play.log.enc at: %s\n", hermes_log_path);
            }
        }
    } else {
        // Only log at ERROR level once; use INFO for the common "MC not running" case
        // to avoid spam when PATH_MODE_INSTANCE is active with no game open.
        if (settings->path_mode == PATH_MODE_INSTANCE) {
            log_message(LOG_INFO, "[TRACKER] No instance saves path found (Minecraft not running?).\n");
        } else {
            log_message(LOG_ERROR, "[TRACKER] CRITICAL: Failed to get saves path.\n");
        }

        // Ensure paths are empty
        t->saves_path[0] = '\0';
        t->world_name[0] = '\0';
        t->advancements_path[0] = '\0';
        t->stats_path[0] = '\0';
        t->unlocks_path[0] = '\0';
    }
}

// --- Helper for compare ---
static bool json_objects_differ(cJSON *obj1, cJSON *obj2) {
    if (obj1 == obj2) return false;
    if (!obj1 || !obj2) return true; // One exists, the other doesn't

    char *s1 = cJSON_PrintUnformatted(obj1);
    char *s2 = cJSON_PrintUnformatted(obj2);

    bool diff = true;
    if (s1 && s2) {
        diff = (strcmp(s1, s2) != 0);
    }

    if (s1) free(s1);
    if (s2) free(s2);
    return diff;
}


// =============================================================================
//  HERMES MOD LIVE-UPDATE SUPPORT FUNCTIONS
// =============================================================================

/**
 * Converts a modern Hermes stat key to category + item keys.
 *
 * Hermes format:  "minecraft.picked_up:minecraft.oak_log"
 * Output:          h_cat  = "minecraft:picked_up"
 *                  h_item = "minecraft:oak_log"
 *
 * These match TrackableItem::stat_category_key / stat_item_key, which are
 * populated at load time by splitting root_name on '/'.
 *
 * Returns false if there is no ':' — indicating a legacy or mid-era key
 * that must be compared directly against root_name instead.
 */
static bool hermes_parse_stat_key(const char *hermes_key,
                                  char *h_cat, char *h_item,
                                  size_t buf_size) {
    const char *colon = strchr(hermes_key, ':');
    if (!colon) return false;

    size_t cat_len = (size_t) (colon - hermes_key);
    if (cat_len == 0 || cat_len >= buf_size) return false;
    strncpy(h_cat, hermes_key, cat_len);
    h_cat[cat_len] = '\0';
    char *dot = strchr(h_cat, '.');
    if (dot) *dot = ':'; // "minecraft.picked_up" → "minecraft:picked_up"

    const char *item_start = colon + 1;
    if (*item_start == '\0') return false;
    strncpy(h_item, item_start, buf_size - 1);
    h_item[buf_size - 1] = '\0';
    dot = strchr(h_item, '.');
    if (dot) *dot = ':'; // "minecraft.oak_log" → "minecraft:oak_log"

    return true;
}


/**
 * Applies a single Hermes "stat" event to in-memory template data.
 *
 * Key format detection:
 *   Modern  (≥1.13): has ':', parsed into category/item, matched via
 *                    stat_category_key / stat_item_key on TrackableItem.
 *   Mid-era / Legacy: no ':', matched by direct strcmp against root_name.
 *
 * After updating criteria, recalculates category-level completion counters.
 * Also updates the active SUBGOAL_STAT stage in any multi-stage goal.
 *
 * Returns true if at least one in-memory value changed.
 */
static bool hermes_apply_stat_event(Tracker *t, const cJSON *data, bool skip_multi_stage = false) {
    cJSON *stat_key_json = cJSON_GetObjectItem(data, "stat");
    cJSON *value_json = cJSON_GetObjectItem(data, "value");

    if (!cJSON_IsString(stat_key_json) || !cJSON_IsNumber(value_json))
        return false;

    const char *hermes_key = stat_key_json->valuestring;
    int new_value = (int) value_json->valuedouble;

    char h_cat[192], h_item[192];
    bool is_modern = hermes_parse_stat_key(hermes_key, h_cat, h_item, sizeof(h_cat));

    bool changed = false;

    // --- Stat categories / criteria ---
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        if (!stat_cat) continue;

        bool cat_changed = false;

        for (int j = 0; j < stat_cat->criteria_count; j++) {
            TrackableItem *sub = stat_cat->criteria[j];
            if (!sub) continue;

            bool matches = false;
            if (is_modern) {
                if (sub->stat_category_key[0] == '\0') continue;
                matches = (strcmp(sub->stat_category_key, h_cat) == 0 &&
                           strcmp(sub->stat_item_key, h_item) == 0);
            } else {
                // Legacy: "5242881", mid-era: "stat.pickup.minecraft.gold_block"
                // Both stored verbatim in root_name.
                matches = (strcmp(sub->root_name, hermes_key) == 0);
            }

            if (!matches) continue;

            // Hermes value is always the cumulative total for one player.
            // In singleplayer: overwrite directly (only one player).
            // In coop HIGHEST mode: only apply if higher (preserves max across players).
            if (new_value <= sub->progress) continue;
            sub->progress = new_value;

            if (!sub->is_manually_completed) {
                if (sub->goal > 0) sub->done = (sub->progress >= sub->goal);
                else if (sub->goal == -1) sub->done = false; // infinite counter
            }

            cat_changed = true;
            changed = true;
        }

        if (cat_changed) {
            int completed = 0;
            for (int j = 0; j < stat_cat->criteria_count; j++) {
                if (stat_cat->criteria[j] && stat_cat->criteria[j]->done)
                    completed++;
            }
            stat_cat->completed_criteria_count = completed;

            if (!stat_cat->is_manually_completed) {
                stat_cat->done = (stat_cat->criteria_count > 0 &&
                                  completed >= stat_cat->criteria_count);
            }
        }
    }

    // --- Active SUBGOAL_STAT stage in multi-stage goals ---
    // In coop HOST mode, multi-stage stages are handled by the cumulative function
    // (always summed across players, independent of the stat merge setting).
    if (!skip_multi_stage) {
        for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
            MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
            if (!goal) continue;
            if (goal->current_stage >= goal->stage_count) continue;

            SubGoal *stage = goal->stages[goal->current_stage];
            if (!stage || stage->type != SUBGOAL_STAT) continue;

            bool matches = false;
            if (is_modern) {
                // Modern template format: "minecraft:picked_up/minecraft:wither_skeleton_skull"
                const char *slash = strchr(stage->root_name, '/');
                if (!slash) continue;

                char s_cat[192], s_item[192];
                size_t cat_len = (size_t) (slash - stage->root_name);
                if (cat_len == 0 || cat_len >= sizeof(s_cat)) continue;

                strncpy(s_cat, stage->root_name, cat_len);
                s_cat[cat_len] = '\0';
                strncpy(s_item, slash + 1, sizeof(s_item) - 1);
                s_item[sizeof(s_item) - 1] = '\0';

                matches = (strcmp(s_cat, h_cat) == 0 &&
                           strcmp(s_item, h_item) == 0);
            } else {
                // Legacy/mid-era: direct compare, e.g. "5242881" or "stat.pickup.minecraft.skull"
                matches = (strcmp(stage->root_name, hermes_key) == 0);
            }

            if (!matches) continue;

            if (new_value > stage->current_stat_progress) {
                stage->current_stat_progress = new_value;
                changed = true;
            }

            if (stage->required_progress > 0 &&
                stage->current_stat_progress >= stage->required_progress) {
                if (goal->current_stage + 1 < goal->stage_count) {
                    goal->current_stage++;
                    log_message(LOG_INFO,
                                "[TRACKER - HERMES] Multi-stage goal '%s' advanced to stage %d.\n",
                                goal->root_name, goal->current_stage);
                }
            }
        }
    }

    return changed;
}


/**
 * Applies a single Hermes "stat" event using CUMULATIVE (sum) merge logic.
 * Instead of setting the stat value directly, this tracks each player's last-known
 * value and applies only the delta to the merged template_data.
 *
 * Example: Player A had 45, now reports 50. Delta = +5. Merged total increases by 5.
 * This preserves the sum across all players even though events arrive one at a time.
 *
 * The per-player cache key is "uuid:stat_key" (or "?:stat_key" if UUID unavailable).
 * The cache is cleared each time the file-based merge runs (authoritative reset).
 */
static bool hermes_apply_stat_event_cumulative(Tracker *t, const cJSON *data,
                                               const char *player_uuid,
                                               bool multi_stage_only = false) {
    cJSON *stat_key_json = cJSON_GetObjectItem(data, "stat");
    cJSON *value_json = cJSON_GetObjectItem(data, "value");

    if (!cJSON_IsString(stat_key_json) || !cJSON_IsNumber(value_json))
        return false;

    const char *hermes_key = stat_key_json->valuestring;
    int new_value = (int) value_json->valuedouble;

    auto *cache = static_cast<std::unordered_map<std::string, int> *>(t->hermes_coop_stat_cache);
    if (!cache) return false;

    // Build the cache key: "uuid:stat_key"
    std::string cache_key = (player_uuid ? player_uuid : "?");
    cache_key += ':';
    cache_key += hermes_key;

    // Look up old value and compute delta
    int old_value = 0;
    auto it = cache->find(cache_key);
    if (it != cache->end()) {
        old_value = it->second;
    }
    int delta = new_value - old_value;
    (*cache)[cache_key] = new_value;

    if (delta == 0) return false;

    char h_cat[192], h_item[192];
    bool is_modern = hermes_parse_stat_key(hermes_key, h_cat, h_item, sizeof(h_cat));

    bool changed = false;

    // Regular stat categories (skip when only processing multi-stage stages)
    if (!multi_stage_only) {
        for (int i = 0; i < t->template_data->stat_count; i++) {
            TrackableCategory *stat_cat = t->template_data->stats[i];
            if (!stat_cat) continue;

            bool cat_changed = false;

            for (int j = 0; j < stat_cat->criteria_count; j++) {
                TrackableItem *sub = stat_cat->criteria[j];
                if (!sub) continue;

                bool matches = false;
                if (is_modern) {
                    if (sub->stat_category_key[0] == '\0') continue;
                    matches = (strcmp(sub->stat_category_key, h_cat) == 0 &&
                               strcmp(sub->stat_item_key, h_item) == 0);
                } else {
                    matches = (strcmp(sub->root_name, hermes_key) == 0);
                }

                if (!matches) continue;

                // Add the delta to the merged cumulative total
                sub->progress += delta;

                if (!sub->is_manually_completed) {
                    if (sub->goal > 0) sub->done = (sub->progress >= sub->goal);
                    else if (sub->goal == -1) sub->done = false;
                }

                cat_changed = true;
                changed = true;
            }

            if (cat_changed) {
                int completed = 0;
                for (int j = 0; j < stat_cat->criteria_count; j++) {
                    if (stat_cat->criteria[j] && stat_cat->criteria[j]->done)
                        completed++;
                }
                stat_cat->completed_criteria_count = completed;

                if (!stat_cat->is_manually_completed) {
                    stat_cat->done = (stat_cat->criteria_count > 0 &&
                                      completed >= stat_cat->criteria_count);
                }
            }
        }
    } // end if (!multi_stage_only)

    // Multi-stage goals with CUMULATIVE: also use delta
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        if (!goal) continue;
        if (goal->current_stage >= goal->stage_count) continue;

        SubGoal *stage = goal->stages[goal->current_stage];
        if (!stage || stage->type != SUBGOAL_STAT) continue;

        bool matches = false;
        if (is_modern) {
            const char *slash = strchr(stage->root_name, '/');
            if (!slash) continue;

            char s_cat[192], s_item[192];
            size_t cat_len = (size_t) (slash - stage->root_name);
            if (cat_len == 0 || cat_len >= sizeof(s_cat)) continue;

            strncpy(s_cat, stage->root_name, cat_len);
            s_cat[cat_len] = '\0';
            strncpy(s_item, slash + 1, sizeof(s_item) - 1);
            s_item[sizeof(s_item) - 1] = '\0';

            matches = (strcmp(s_cat, h_cat) == 0 && strcmp(s_item, h_item) == 0);
        } else {
            matches = (strcmp(stage->root_name, hermes_key) == 0);
        }

        if (!matches) continue;

        stage->current_stat_progress += delta;
        changed = true;

        if (stage->required_progress > 0 &&
            stage->current_stat_progress >= stage->required_progress) {
            if (goal->current_stage + 1 < goal->stage_count) {
                goal->current_stage++;
                log_message(LOG_INFO,
                            "[TRACKER - HERMES] Multi-stage goal '%s' advanced to stage %d (cumulative).\n",
                            goal->root_name, goal->current_stage);
            }
        }
    }

    return changed;
}


/**
 * Applies a single Hermes "advancement" event to in-memory template data.
 *
 * The Hermes event contains:
 *   "id"            - the advancement/achievement root_name
 *   "criterion_name"- the specific criterion that was earned
 *   "completed"     - whether the entire advancement is now complete
 *
 * We mark the criterion done and, if completed == true, mark the whole
 * category done and update the completion counter.
 *
 * We deliberately do NOT unmark anything here. Revocations (/advancement revoke)
 * are not applied in-memory from Hermes — they self-correct on the next
 * dmon-triggered game save when tracker_update() does a full re-read from disk.
 *
 * Returns true if at least one in-memory value changed.
 */
static bool hermes_apply_advancement_event(Tracker *t, const cJSON *data) {
    cJSON *id_json = cJSON_GetObjectItem(data, "id");
    cJSON *criterion_json = cJSON_GetObjectItem(data, "criterion_name");
    cJSON *completed_json = cJSON_GetObjectItem(data, "completed");

    if (!cJSON_IsString(id_json)) return false;

    const char *adv_id = id_json->valuestring;
    const char *criterion_name = cJSON_IsString(criterion_json) ? criterion_json->valuestring : nullptr;
    bool adv_completed = cJSON_IsTrue(completed_json);

    bool changed = false;

    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *adv = t->template_data->advancements[i];
        if (!adv) continue;
        if (strcmp(adv->root_name, adv_id) != 0) continue;

        // Found the matching advancement/achievement.

        // Mark the specific criterion done if one was reported and we have criteria.
        if (criterion_name && adv->criteria_count > 0) {
            for (int j = 0; j < adv->criteria_count; j++) {
                TrackableItem *crit = adv->criteria[j];
                if (!crit) continue;
                if (strcmp(crit->root_name, criterion_name) != 0) continue;

                if (!crit->done) {
                    crit->done = true;
                    adv->completed_criteria_count++;
                    changed = true;
                }
                break;
            }
        }

        // If Hermes says the whole advancement is now complete, mark it so.
        if (adv_completed && !adv->done && !adv->is_manually_completed) {
            adv->done = true;

            // Only count non-recipe advancements toward the completion counter.
            if (!adv->is_recipe) {
                t->template_data->advancements_completed_count++;
            }
            changed = true;
        }

        // Mark all_template_criteria_met if every criterion is done.
        if (adv->criteria_count > 0 &&
            adv->completed_criteria_count >= adv->criteria_count) {
            adv->all_template_criteria_met = true;
        }

        break; // root_name is unique, no need to keep searching.
    }


    // -----------------------------------------------------------------------
    // Multi-stage goals: advance any active SUBGOAL_ADVANCEMENT or
    // SUBGOAL_CRITERION stage that matches this event.
    //
    // SUBGOAL_ADVANCEMENT: stage->root_name is the advancement id.
    //   Completes when Hermes reports that advancement as completed.
    //
    // SUBGOAL_CRITERION: stage->parent_advancement is the advancement id,
    //   stage->root_name is the criterion name.
    //   Completes when Hermes reports that specific criterion being earned.
    //   Note: criterion events always arrive before the parent advancement
    //   completion event, so we handle them here independently.
    // -----------------------------------------------------------------------
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        if (!goal) continue;
        if (goal->current_stage >= goal->stage_count) continue;

        SubGoal *stage = goal->stages[goal->current_stage];
        if (!stage) continue;

        bool stage_completed = false;

        if (stage->type == SUBGOAL_ADVANCEMENT) {
            // The stage root_name is the advancement id we are waiting for.
            if (adv_completed && strcmp(stage->root_name, adv_id) == 0)
                stage_completed = true;
        } else if (stage->type == SUBGOAL_CRITERION && criterion_name) {
            // Both the parent advancement and the specific criterion must match.
            if (strcmp(stage->parent_advancement, adv_id) == 0 &&
                strcmp(stage->root_name, criterion_name) == 0)
                stage_completed = true;
        }

        if (stage_completed && goal->current_stage + 1 < goal->stage_count) {
            goal->current_stage++;
            changed = true;
            log_message(LOG_INFO,
                        "[TRACKER - HERMES] Multi-stage goal '%s' advanced to stage %d via advancement event.\n",
                        goal->root_name, goal->current_stage);
        }
    }

    return changed;
}


void tracker_recalculate_progress(Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data) return;
    MC_Version version = settings_get_version_from_string(settings->version_str); {
        bool changed;
        int guard = 0;
        do {
            changed = tracker_update_custom_goal_linked_goals(t);
            changed |= tracker_update_stat_linked_goals(t);
            changed |= tracker_update_counter_goals(t);
        } while (changed && ++guard < 32);
    }
    tracker_calculate_overall_progress(t, version, settings);
}


void tracker_poll_hermes_log(Tracker *t, const AppSettings *settings) {
    if (!settings->using_hermes || !t->hermes_active || !t->hermes_play_log)
        return;

    if (fseek(t->hermes_play_log, t->hermes_file_offset, SEEK_SET) != 0)
        return;

    bool any_changed = false;

    while (true) {
        long start_offset = ftell(t->hermes_play_log);
        std::string line_buf;
        char chunk[8192];
        bool newline_found = false;

        // Read chunks until we hit the end of the line or EOF
        while (fgets(chunk, sizeof(chunk), t->hermes_play_log)) {
            line_buf += chunk;
            if (!line_buf.empty() && line_buf.back() == '\n') {
                newline_found = true;
                break;
            }
        }

        if (!newline_found) {
            // Incomplete line — Minecraft is still writing or we reached EOF. Retry next frame.
            // Reset to start of this line so we read it completely next time.
            fseek(t->hermes_play_log, start_offset, SEEK_SET);
            break;
        }

        // Commit read position past this complete line.
        t->hermes_file_offset = ftell(t->hermes_play_log);

        // Strip trailing CR/LF.
        while (!line_buf.empty() && (line_buf.back() == '\n' || line_buf.back() == '\r')) {
            line_buf.pop_back();
        }

        if (line_buf.empty()) continue;

        // Decrypt.
        std::string decrypted = t->hermes_rotator.processLine(line_buf);

        cJSON *event = cJSON_ParseWithLength(decrypted.c_str(), decrypted.size());
        if (!event) {
            log_message(LOG_ERROR, "[TRACKER - HERMES] Failed to parse decrypted line (len=%zu)\n", line_buf.size());
            continue;
        }

        cJSON *type_json = cJSON_GetObjectItem(event, "type");
        cJSON *data = cJSON_GetObjectItem(event, "data");

        if (!cJSON_IsString(type_json) || !data) {
            cJSON_Delete(event);
            continue;
        }

        const char *type = type_json->valuestring;

        // --- Player identity filter ---
        // Hermes events include a "player" object with "name" and "uuid".
        // In coop HOST mode: accept events from any player in the lobby roster.
        // In singleplayer/receiver: accept only the local player's events.
        // Match by UUID first (authoritative, case-insensitive hex), then
        // fall back to case-insensitive username (Hermes "name" may differ
        // in case; legacy stats files are fully lowercase).
        const char *event_player_uuid = nullptr; // Used for stat cache key
        cJSON *player_json = cJSON_GetObjectItem(data, "player");
        if (cJSON_IsObject(player_json)) {
            cJSON *uuid_json = cJSON_GetObjectItem(player_json, "uuid");
            cJSON *name_json = cJSON_GetObjectItem(player_json, "name");
            const char *ev_uuid = cJSON_IsString(uuid_json) ? uuid_json->valuestring : nullptr;
            const char *ev_name = cJSON_IsString(name_json) ? name_json->valuestring : nullptr;

            bool player_match = false;

            if (settings->network_mode == NETWORK_HOST && settings->coop_player_count > 0) {
                // Coop host: match against any player in the roster
                for (int p = 0; p < settings->coop_player_count; p++) {
                    const CoopPlayer *rp = &settings->coop_players[p];
                    if (ev_uuid && rp->uuid[0] != '\0' &&
                        strcasecmp(ev_uuid, rp->uuid) == 0) {
                        player_match = true;
                        event_player_uuid = ev_uuid;
                        break;
                    }
                    if (!player_match && ev_name && rp->username[0] != '\0' &&
                        strcasecmp(ev_name, rp->username) == 0) {
                        player_match = true;
                        event_player_uuid = ev_uuid; // may be null, that's OK
                        break;
                    }
                }
            } else {
                // Singleplayer or receiver: match local player only
                if (settings->local_player.uuid[0] != '\0' && ev_uuid) {
                    player_match = (strcasecmp(ev_uuid, settings->local_player.uuid) == 0);
                }
                if (!player_match && settings->local_player.username[0] != '\0' && ev_name) {
                    player_match = (strcasecmp(ev_name, settings->local_player.username) == 0);
                }
                if (player_match) event_player_uuid = ev_uuid;
            }

            if (!player_match) {
                cJSON_Delete(event);
                continue; // Skip events from players not in roster / not local
            }
        }
        // If no player object in event, allow it through (older Hermes versions)

        // --- Dispatch event ---
        if (strcmp(type, "stat") == 0) {
            bool is_coop_host = (settings->network_mode == NETWORK_HOST &&
                                 t->hermes_coop_stat_cache);
            if (is_coop_host && settings->coop_stat_merge == COOP_STAT_CUMULATIVE) {
                // CUMULATIVE mode: track per-player deltas for everything
                if (hermes_apply_stat_event_cumulative(t, data, event_player_uuid))
                    any_changed = true;
            } else if (is_coop_host) {
                // HIGHEST mode: use higher-wins for regular stats, but multi-stage
                // stages must always be cumulative (summed across all players)
                if (hermes_apply_stat_event(t, data, true))  // skip_multi_stage
                    any_changed = true;
                if (hermes_apply_stat_event_cumulative(t, data, event_player_uuid, true))  // multi_stage_only
                    any_changed = true;
            } else {
                // Singleplayer: direct apply (only one player, highest == actual)
                if (hermes_apply_stat_event(t, data))
                    any_changed = true;
            }
        } else if (strcmp(type, "advancement") == 0) {
            // Advancements are always OR-merged: any player completing = done
            if (hermes_apply_advancement_event(t, data))
                any_changed = true;
        }
        // ALL OTHER EVENT TYPES ARE IGNORED!!! - Fully speedrun legal this way

        cJSON_Delete(event);
    }

    if (any_changed) {
        tracker_recalculate_progress(t, settings);
        t->hermes_wants_ipc_flush = true;
    }
}

// =============================================================================
//  END OF HERMES MOD LIVE-UPDATE SUPPORT FUNCTIONS
// =============================================================================


bool tracker_load_and_parse_data(Tracker *t, AppSettings *settings) {
    log_message(LOG_INFO, "[TRACKER] Loading advancement template from: %s\n", t->advancement_template_path);

    cJSON *template_json = cJSON_from_file(t->advancement_template_path);

    if (!template_json) {
        // --- TEMPLATE NOT FOUND & RECOVERY LOGIC ---
        log_message(LOG_ERROR, "[TRACKER] CRITICAL: Template file not found: %s\n", t->advancement_template_path);

        // 1. Temporarily disable 'Always On Top' to ensure the popup is visible
        bool was_on_top = (SDL_GetWindowFlags(t->window) & SDL_WINDOW_ALWAYS_ON_TOP);
        if (was_on_top) {
            SDL_SetWindowAlwaysOnTop(t->window, false);
        }

        // 2. Show a detailed error message
        char error_msg[MAX_PATH_LENGTH + 256];
        snprintf(error_msg, sizeof(error_msg),
                 "The selected template could not be found:\n%s\n\nAdvancely will now reset to the default template.",
                 t->advancement_template_path);
        show_error_message("Template Not Found", error_msg);

        // 3. Restore 'Always On Top' state after the user dismisses the popup
        if (was_on_top) {
            SDL_SetWindowAlwaysOnTop(t->window, true);
        }

        // 4. Reset the settings in memory to the application defaults
        strncpy(settings->version_str, DEFAULT_VERSION, sizeof(settings->version_str) - 1);
        settings->version_str[sizeof(settings->version_str) - 1] = '\0';

        strncpy(settings->category, DEFAULT_CATEGORY, sizeof(settings->category) - 1);
        settings->category[sizeof(settings->category) - 1] = '\0';

        settings->optional_flag[0] = '\0';
        settings->lang_flag[0] = '\0';

        // 5. Save these new default settings back to the settings.json file
        settings_save(settings, nullptr, SAVE_CONTEXT_ALL);

        // 6. Attempt to reload the data with the new default paths immediately
        construct_template_paths(settings);
        strncpy(t->advancement_template_path, settings->template_path, MAX_PATH_LENGTH - 1);
        t->advancement_template_path[MAX_PATH_LENGTH - 1] = '\0';

        strncpy(t->lang_path, settings->lang_path, MAX_PATH_LENGTH - 1);
        t->lang_path[MAX_PATH_LENGTH - 1] = '\0';

        strncpy(t->snapshot_path, settings->snapshot_path, MAX_PATH_LENGTH - 1);
        t->snapshot_path[MAX_PATH_LENGTH - 1] = '\0';

        strncpy(t->notes_path, settings->notes_path, MAX_PATH_LENGTH - 1);
        t->notes_path[MAX_PATH_LENGTH - 1] = '\0';

        template_json = cJSON_from_file(t->advancement_template_path);
        if (!template_json) {
            // If it fails even with the default, something is critically wrong.
            // Temporarily disable 'Always On Top' to ensure the popup is visible
            if (was_on_top) {
                SDL_SetWindowAlwaysOnTop(t->window, false);
            }
            show_error_message("Critical Error",
                               "The default template is missing or corrupted. Please reinstall Advancely.");

            // 3. Restore 'Always On Top' state after the user dismisses the popup
            if (was_on_top) {
                SDL_SetWindowAlwaysOnTop(t->window, true);
            }

            return false; // This is a fatal error
        }
    }

    // Declare lang_json as a local variable, this prevents memory leaks
    cJSON *lang_json = cJSON_from_file(t->lang_path);
    if (!lang_json) {
        // Handle case where lang file might still be missing for some reason
        lang_json = cJSON_CreateObject();
    }

    // Load settings.json to check for custom progress
    cJSON *settings_json = cJSON_from_file(get_settings_file_path());
    if (!settings_json) {
        log_message(LOG_ERROR, "[TRACKER] Failed to load or parse settings file.\n");
        return false;
    }

    // Parse the main categories from the template
    cJSON *advancements_json = cJSON_GetObjectItem(template_json, "advancements");
    cJSON *stats_json = cJSON_GetObjectItem(template_json, "stats");
    cJSON *unlocks_json = cJSON_GetObjectItem(template_json, "unlocks");
    cJSON *custom_json = cJSON_GetObjectItem(template_json, "custom"); // Custom goals, manually checked of by user
    cJSON *multi_stage_goals_json = cJSON_GetObjectItem(template_json, "multi_stage_goals");
    cJSON *counter_goals_json = cJSON_GetObjectItem(template_json, "counter_goals");
    cJSON *decorations_json = cJSON_GetObjectItem(template_json, "decorations");


    MC_Version version = settings_get_version_from_string(settings->version_str);

    // Parse the main categories
    // False as it's for advancements
    tracker_parse_categories(t, advancements_json, lang_json, &t->template_data->advancements,
                             &t->template_data->advancement_count, &t->template_data->total_criteria_count,
                             "advancement.", false, version, settings);

    // After parsing, calculate the count of actual advancements (excluding recipes)
    t->template_data->advancement_goal_count = 0;
    if (t->template_data->advancements) {
        for (int i = 0; i < t->template_data->advancement_count; i++) {
            // When it's NOT a recipe
            // Recipes count towards percentage progress
            if (t->template_data->advancements[i] && !t->template_data->advancements[i]->is_recipe) {
                t->template_data->advancement_goal_count++;
            }
        }
    }

    // True as it's for stats
    tracker_parse_categories(t, stats_json, lang_json, &t->template_data->stats,
                             &t->template_data->stat_count, &t->template_data->stat_total_criteria_count, "stat.",
                             true, version, settings);

    // Parse "unlock." prefix for unlocks
    tracker_parse_simple_trackables(t, unlocks_json, lang_json, &t->template_data->unlocks,
                                    &t->template_data->unlock_count, "unlock.", settings, false);

    // Parse "custom." prefix for custom goals
    tracker_parse_simple_trackables(t, custom_json, lang_json, &t->template_data->custom_goals,
                                    &t->template_data->custom_goal_count, "custom.", settings, true);

    tracker_parse_multi_stage_goals(t, multi_stage_goals_json, lang_json,
                                    &t->template_data->multi_stage_goals,
                                    &t->template_data->multi_stage_goal_count, settings);

    tracker_parse_counter_goals(t, counter_goals_json, lang_json,
                                &t->template_data->counter_goals,
                                &t->template_data->counter_goal_count, settings);

    tracker_parse_decorations(decorations_json, lang_json,
                              &t->template_data->decorations,
                              &t->template_data->decoration_count);


    // Detect and flag criteria that are shared between multiple advancements
    tracker_detect_shared_icons(t, settings);

    // Automatically synchronize settings.json with the newly loaded template
    cJSON *settings_root = cJSON_from_file(get_settings_file_path());
    if (!settings_root) settings_root = cJSON_CreateObject();

    bool save_needed = false; // FLAG: Only write to disk if data ACTUALLY changed

    // Sync custom_progress
    cJSON *old_custom_progress = cJSON_GetObjectItem(settings_root, "custom_progress");
    cJSON *new_custom_progress = cJSON_CreateObject();
    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        TrackableItem *item = t->template_data->custom_goals[i];
        cJSON *old_item = old_custom_progress ? cJSON_GetObjectItem(old_custom_progress, item->root_name) : nullptr;
        if (old_item) {
            // Preserve old value if it exists
            cJSON_AddItemToObject(new_custom_progress, item->root_name, cJSON_Duplicate(old_item, 1));
        } else {
            // Add new item with default value
            if (item->goal > 0 || item->goal == -1) cJSON_AddNumberToObject(new_custom_progress, item->root_name, 0);
            else cJSON_AddBoolToObject(new_custom_progress, item->root_name, false);
        }
    }

    // Compare before replacing
    if (json_objects_differ(old_custom_progress, new_custom_progress)) {
        cJSON_ReplaceItemInObject(settings_root, "custom_progress", new_custom_progress);
        save_needed = true;
    } else {
        cJSON_Delete(new_custom_progress); // Clean up unused new object
    }

    // Sync stat_progress_override
    cJSON *old_stat_override = cJSON_GetObjectItem(settings_root, "stat_progress_override");
    cJSON *new_stat_override = cJSON_CreateObject();
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *stat_cat = t->template_data->stats[i];
        cJSON *old_cat_item = old_stat_override ? cJSON_GetObjectItem(old_stat_override, stat_cat->root_name) : nullptr;

        // Always add the parent entry (e.g., "stat_cat:mine_more_sand")
        if (old_cat_item)
            cJSON_AddItemToObject(new_stat_override, stat_cat->root_name,
                                  cJSON_Duplicate(old_cat_item, 1));
        else cJSON_AddBoolToObject(new_stat_override, stat_cat->root_name, false);

        // Only add ".criteria." entries if the template defines multiple sub-stats for this category.
        if (stat_cat->criteria_count > 1) {
            for (int j = 0; j < stat_cat->criteria_count; j++) {
                TrackableItem *sub_stat = stat_cat->criteria[j];
                char sub_stat_key[512];
                // Use the sub-stat's actual root_name for the key
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                         sub_stat->root_name);
                cJSON *old_sub_item = old_stat_override
                                          ? cJSON_GetObjectItem(old_stat_override, sub_stat_key)
                                          : nullptr;
                if (old_sub_item)
                    cJSON_AddItemToObject(new_stat_override, sub_stat_key,
                                          cJSON_Duplicate(old_sub_item, 1));
                else cJSON_AddBoolToObject(new_stat_override, sub_stat_key, false);
            }
        }
    }

    // Compare before replacing
    if (json_objects_differ(old_stat_override, new_stat_override)) {
        cJSON_ReplaceItemInObject(settings_root, "stat_progress_override", new_stat_override);
        save_needed = true;
    } else {
        cJSON_Delete(new_stat_override);
    }

    // Sync hotkeys, matching by target_goal (root_name). The on-disk array is
    // sparse (only counters with bindings are saved), so index-based matching
    // would scramble bindings when users assign hotkeys to non-leading counters.
    cJSON *old_hotkeys_array = cJSON_GetObjectItem(settings_root, "hotkeys");
    cJSON *new_hotkeys_array = cJSON_CreateArray();

    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        TrackableItem *item = t->template_data->custom_goals[i];
        if (item && (item->goal > 0 || item->goal == -1)) {
            // It's a counter
            cJSON *new_hotkey_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(new_hotkey_obj, "target_goal", item->root_name);

            const char *inc_key = "None";
            const char *dec_key = "None";

            // Find the old binding whose target_goal matches this counter's root_name.
            if (cJSON_IsArray(old_hotkeys_array)) {
                cJSON *old_hotkey_item = nullptr;
                cJSON_ArrayForEach(old_hotkey_item, old_hotkeys_array) {
                    cJSON *old_target = cJSON_GetObjectItem(old_hotkey_item, "target_goal");
                    if (cJSON_IsString(old_target) && old_target->valuestring &&
                        strcmp(old_target->valuestring, item->root_name) == 0) {
                        cJSON *old_inc_key = cJSON_GetObjectItem(old_hotkey_item, "increment_key");
                        cJSON *old_dec_key = cJSON_GetObjectItem(old_hotkey_item, "decrement_key");
                        if (cJSON_IsString(old_inc_key) && old_inc_key->valuestring) inc_key = old_inc_key->valuestring;
                        if (cJSON_IsString(old_dec_key) && old_dec_key->valuestring) dec_key = old_dec_key->valuestring;
                        break;
                    }
                }
            }

            cJSON_AddStringToObject(new_hotkey_obj, "increment_key", inc_key);
            cJSON_AddStringToObject(new_hotkey_obj, "decrement_key", dec_key);
            cJSON_AddItemToArray(new_hotkeys_array, new_hotkey_obj);
        }
    }

    // Compare before replacing
    if (json_objects_differ(old_hotkeys_array, new_hotkeys_array)) {
        cJSON_ReplaceItemInObject(settings_root, "hotkeys", new_hotkeys_array);
        save_needed = true;
    } else {
        cJSON_Delete(new_hotkeys_array);
    }

    if (save_needed) {
        log_message(LOG_INFO, "[TRACKER] Updating settings.json with new template data...\n");
        // Write the synchronized settings back to the file
        FILE *file = fopen(get_settings_file_path(), "w");
        if (file) {
            char *json_str = cJSON_Print(settings_root);
            fputs(json_str, file);
            fclose(file);
            free(json_str);
            json_str = nullptr;
        }
    } else {
        // TODO: Debug
        log_message(LOG_INFO, "[TRACKER] Settings.json is up to date. Skipping write.\n");
    }
    cJSON_Delete(settings_root);

    // LOADING SNAPSHOT FROM FILE - ONLY FOR VERSION 1.0-1.6.4 WITHOUT StatsPerWorld MOD
    if (version <= MC_VERSION_1_6_4 && !settings->using_stats_per_world_legacy) {
        tracker_load_snapshot_from_file(t, settings);
    }

    cJSON_Delete(template_json);
    if (lang_json) {
        cJSON_Delete(lang_json);
    }

    // After parsing everything, determine the correct notes path and do an initial load.
    tracker_update_notes_path(t, settings);
    tracker_load_notes(t, settings);

    // Prime the 'last_known_world_name' with the initial world on first load.
    strncpy(t->template_data->last_known_world_name, t->world_name,
            sizeof(t->template_data->last_known_world_name) - 1);
    t->template_data->last_known_world_name[sizeof(t->template_data->last_known_world_name) - 1] = '\0';

    log_message(LOG_INFO, "[TRACKER] Initial template parsing complete.\n");

    return true; // Success
    // No need to delete settings_json, because it's not parsed, handled in tracker_update()
}


void tracker_free(Tracker **tracker, AppSettings *settings) {
    if (tracker && *tracker) {
        Tracker *t = *tracker;

        // Save view state to settings and write to disk before destroying data
        if (settings) {
            settings->view_pan_x = t->camera_offset.x;
            settings->view_pan_y = t->camera_offset.y;
            settings->view_zoom = t->zoom_level;
            settings->view_locked = t->layout_locked;
            settings->view_locked_width = t->locked_layout_width;

            // Save settings immediately
            settings_save(settings, t->template_data, SAVE_CONTEXT_ALL);
        }

        // Free all textures in the cache
        if (t->texture_cache) {
            for (int i = 0; i < t->texture_cache_count; i++) {
                if (t->texture_cache[i].texture) {
                    SDL_DestroyTexture(t->texture_cache[i].texture);
                }
            }
            free(t->texture_cache);
            t->texture_cache = nullptr;
        }

        // Free all animations in the cache
        if (t->anim_cache) {
            for (int i = 0; i < t->anim_cache_count; i++) {
                if (t->anim_cache[i].anim) {
                    // THE ONLY PLACE WHERE ANIMATIONS ARE FREED
                    free_animated_texture(t->anim_cache[i].anim);
                }
            }
            free(t->anim_cache);
            t->anim_cache = nullptr;
        }

        if (t->minecraft_font) {
            TTF_CloseFont(t->minecraft_font);
        }

        if (t->template_data) {
            tracker_free_template_data(t->template_data);
            // This ONLY frees the CONTENT of the struct, not the struct itself
            free(t->template_data); // This frees the struct, STRUCT FREED HERE
            t->template_data = nullptr;
        }

        if (t->renderer) {
            SDL_DestroyRenderer(t->renderer);
            // We still have an address
            t->renderer = nullptr;
        }

        if (t->window) {
            SDL_DestroyWindow(t->window);
            // We still have an address
            t->window = nullptr;
        }

        // Close Hermes log if open
        if (t->hermes_play_log) {
            fclose(t->hermes_play_log);
            t->hermes_play_log = nullptr;
        }

        // Free Hermes coop stat cache
        if (t->hermes_coop_stat_cache) {
            delete static_cast<std::unordered_map<std::string, int> *>(t->hermes_coop_stat_cache);
            t->hermes_coop_stat_cache = nullptr;
        }

        // Free coop snapshot caches
        tracker_clear_coop_snapshot_cache(t);

        // tracker is heap allocated so free it
        free(t);
        t = nullptr;
        *tracker = nullptr;
        tracker = nullptr;
        log_message(LOG_INFO, "[TRACKER] Tracker freed!\n");
    }
}

void tracker_update_title(Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data || !settings) return;

    char title_buffer[512];
    char formatted_time[64];
    char formatted_update_time[64];

    format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));

    // Format the time since last update (snapped to 5s intervals like the overlay)
    float last_update_time_5_seconds = floorf(t->time_since_last_update / 5.0f) * 5.0f;
    format_time_since_update(last_update_time_5_seconds, formatted_update_time, sizeof(formatted_update_time));

    // Displaying Ach or Adv depending on the version
    // Get version from string
    MC_Version version = settings_get_version_from_string(settings->version_str);
    const char *adv_ach_label = (version >= MC_VERSION_1_12) ? "Adv" : "Ach";

    char progress_chunk[128] = "";
    bool show_adv_counter = (t->template_data->advancement_goal_count > 0);
    bool show_prog_percent = (t->template_data->total_progress_steps > 0);

    if (show_adv_counter && show_prog_percent) {
        snprintf(progress_chunk, sizeof(progress_chunk), "    |    %s: %d/%d    -    Progress: %.2f%%",
                 adv_ach_label, t->template_data->advancements_completed_count,
                 t->template_data->advancement_goal_count, t->template_data->overall_progress_percentage);
    } else if (show_adv_counter) {
        snprintf(progress_chunk, sizeof(progress_chunk), "    |    %s: %d/%d",
                 adv_ach_label, t->template_data->advancements_completed_count,
                 t->template_data->advancement_goal_count);
    } else if (show_prog_percent) {
        snprintf(progress_chunk, sizeof(progress_chunk), "    |    Progress: %.2f%%",
                 t->template_data->overall_progress_percentage);
    }

    char category_chunk[MAX_PATH_LENGTH + 10] = "";
    if (settings->category_display_name[0] != '\0') {
        snprintf(category_chunk, sizeof(category_chunk), "    -    %s", settings->category_display_name);
    }

    // For receivers, show "Syncing with <Host>" instead of world name
    bool is_receiver_connected = (settings->network_mode == NETWORK_RECEIVER &&
                                  g_coop_ctx && coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED);
    char world_display[MAX_PATH_LENGTH];
    if (is_receiver_connected) {
        CoopLobbyPlayer lobby[COOP_MAX_LOBBY];
        int lobby_count = coop_net_get_lobby_players(g_coop_ctx, lobby, COOP_MAX_LOBBY);
        const char *host_name = "Host";
        for (int i = 0; i < lobby_count; i++) {
            if (lobby[i].is_host) {
                host_name = lobby[i].display_name[0] != '\0' ? lobby[i].display_name : lobby[i].username;
                break;
            }
        }
        snprintf(world_display, sizeof(world_display), "Syncing with %s", host_name);
    } else {
        strncpy(world_display, t->world_name, sizeof(world_display) - 1);
        world_display[sizeof(world_display) - 1] = '\0';
    }

    snprintf(title_buffer, sizeof(title_buffer),
             "  Advancely  %s    |    %s    -    %s%s%s    |    %s IGT    |    %s %s",
             ADVANCELY_VERSION, world_display,
             settings->display_version_str,
             category_chunk,
             progress_chunk, formatted_time,
             settings->using_hermes ? "Synced:" : "Upd:",
             formatted_update_time);

    SDL_SetWindowTitle(t->window, title_buffer);
}

void tracker_load_notes(Tracker *t, const AppSettings *settings) {
    (void) settings; // Settings parameter is no longer used for the path
    if (!t || t->notes_path[0] == '\0') {
        if (t) t->notes_buffer[0] = '\0';
        return;
    }

    FILE *file = fopen(t->notes_path, "r");
    if (!file) {
        // File doesn't exist, which is fine for a new world. Ensure buffer is empty.
        t->notes_buffer[0] = '\0';
        return;
    }

    // Read the entire file into the buffer
    size_t bytes_read = fread(t->notes_buffer, 1, sizeof(t->notes_buffer) - 1, file);
    t->notes_buffer[bytes_read] = '\0'; // Ensure null-termination
    fclose(file);
}

void tracker_save_notes(const Tracker *t, const AppSettings *settings) {
    (void) settings; // Settings parameter is no longer used for the path
    if (!t || t->notes_path[0] == '\0') {
        return;
    }

    FILE *file = fopen(t->notes_path, "w");
    if (file) {
        fputs(t->notes_buffer, file);
        fclose(file);
    } else {
        log_message(LOG_ERROR, "[TRACKER] Failed to open notes file for writing: %s\n", t->notes_path);
    }
}

void tracker_print_debug_status(Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data) return;

    cJSON *settings_json = cJSON_from_file(get_settings_file_path());
    cJSON *override_obj = settings_json ? cJSON_GetObjectItem(settings_json, "stat_progress_override") : nullptr;

    // Also load the current game version used
    MC_Version version = settings_get_version_from_string(settings->version_str);

    // Check if the run is completed, check both advancement and overall progress
    bool is_run_complete = t->template_data->advancements_completed_count >= t->template_data->advancement_count &&
                           t->template_data->overall_progress_percentage >= 100.0f;

    // Format the time to DD:HH:MM:SS.MS — use frozen time when the run is completed
    char formatted_time[128];
    long long display_ticks = is_run_complete
                                  ? t->template_data->frozen_play_time_ticks
                                  : t->template_data->play_time_ticks;
    format_time(display_ticks, formatted_time, sizeof(formatted_time));


    log_message(LOG_INFO, "============================================================\n");
    // For receivers, show "Syncing with <Host>" instead of world name
    bool dbg_receiver_connected = (settings->network_mode == NETWORK_RECEIVER &&
                                   g_coop_ctx && coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED);
    if (dbg_receiver_connected) {
        CoopLobbyPlayer dbg_lobby[COOP_MAX_LOBBY];
        int dbg_lobby_count = coop_net_get_lobby_players(g_coop_ctx, dbg_lobby, COOP_MAX_LOBBY);
        const char *dbg_host_name = "Host";
        for (int i = 0; i < dbg_lobby_count; i++) {
            if (dbg_lobby[i].is_host) {
                dbg_host_name = dbg_lobby[i].display_name[0] != '\0'
                                    ? dbg_lobby[i].display_name
                                    : dbg_lobby[i].username;
                break;
            }
        }
        log_message(LOG_INFO, " World:      Syncing with %s\n", dbg_host_name);
    } else {
        log_message(LOG_INFO, " World:      %s\n", t->world_name);
    }
    log_message(LOG_INFO, " Version:    %s\n", settings->display_version_str);

    // When category display name isn't empty
    if (settings->category_display_name[0] != '\0') {
        log_message(LOG_INFO, " Category:   %s\n", settings->category_display_name);
    }

    log_message(LOG_INFO, " Play Time:  %s\n", formatted_time);
    log_message(LOG_INFO, "============================================================\n");


    if (is_run_complete) {
        log_message(LOG_INFO, "\n                  *** RUN COMPLETED! ***\n\n");
        log_message(LOG_INFO, "                  Final Time: %s\n\n", formatted_time);
        log_message(LOG_INFO, "============================================================\n");
    } else {
        // Advancements or Achievements
        if (version >= MC_VERSION_1_12) {
            log_message(LOG_INFO, "[Advancements] %d / %d completed\n", t->template_data->advancements_completed_count,
                        t->template_data->advancement_goal_count); // Excluding recipes
        } else {
            log_message(LOG_INFO, "[Achievements] %d / %d completed\n", t->template_data->advancements_completed_count,
                        t->template_data->advancement_goal_count);
        }
        for (int i = 0; i < t->template_data->advancement_count; i++) {
            TrackableCategory *adv = t->template_data->advancements[i];

            const char *status_text;
            // For Legacy only show completed if earned in this session's snapshot
            if (version <= MC_VERSION_1_6_4) {
                bool earned_this_session = adv->done && !adv->done_in_snapshot;
                // status_text = earned_this_session ? "COMPLETED" : "INCOMPLETE";
                // To see pre-existing ones:
                status_text = earned_this_session ? "COMPLETED (New)" : (adv->done ? "COMPLETED (Old)" : "INCOMPLETE");
            } else {
                // For Modern, the 'done' flag is always correct within the local stats file
                status_text = adv->done ? "COMPLETED" : "INCOMPLETE";
            }

            // Only print criteria count if there are more than 1
            if (adv->criteria_count > 1) {
                log_message(LOG_INFO, "  - %s (%d/%d criteria): %s\n", adv->display_name, adv->completed_criteria_count,
                            adv->criteria_count,
                            status_text);
            } else {
                log_message(LOG_INFO, "  - %s: %s\n", adv->display_name, status_text);
            }


            for (int j = 0; j < adv->criteria_count; j++) {
                TrackableItem *crit = adv->criteria[j];
                // takes translation from the language file otherwise root_name
                // Print if criteria is shared
                log_message(LOG_INFO, "    - %s: %s%s\n", crit->display_name, crit->is_shared ? "SHARED - " : "",
                            crit->done ? "DONE" : "NOT DONE");
            }
        }

        // Other categories...

        // Now supporting stats with criteria
        for (int i = 0; i < t->template_data->stat_count; i++) {
            TrackableCategory *stat_cat = t->template_data->stats[i];

            // Hide a no-icon stat ONLY if the version is pre-1.7.2 (legacy)
            // With target value being 0 or NOT EXISTENT and legacy version it should act as hidden stat for multi-stage
            // It's a "tracker-only" stat with no goal, USED WITHIN MULTI-STAGE FOR LEGACY VERSIONS
            if (version < MC_VERSION_1_6_4 && stat_cat->icon_path[0] == '\0') continue;

            const char *status_text;
            cJSON *parent_override = override_obj ? cJSON_GetObjectItem(override_obj, stat_cat->root_name) : nullptr;
            if (stat_cat->done) {
                status_text = (stat_cat->is_manually_completed && cJSON_IsTrue(parent_override))
                                  ? "COMPLETED (MANUAL)"
                                  : "COMPLETED";
            } else {
                status_text = "INCOMPLETE";
            }

            // Check if this is a single-criterion stat
            if (stat_cat->criteria_count == 1) {
                // --- SINGLE-CRITERION PRINT FORMAT ---
                TrackableItem *sub_stat = stat_cat->criteria[0];

                // Status of the single criterion
                const char *sub_status_text;
                if (sub_stat->done) {
                    sub_status_text = (sub_stat->is_manually_completed && cJSON_IsTrue(parent_override))
                                          ? "DONE (MANUAL)"
                                          : "DONE";
                } else {
                    sub_status_text = "NOT DONE";
                }

                // Check if the single criterion has a target greater 0 or -1
                if (sub_stat->goal > 0) {
                    // It's a completable single-criterion stat
                    log_message(LOG_INFO, "[Stat] %s: %d / %d - %s\n",
                                stat_cat->display_name,
                                sub_stat->progress,
                                sub_stat->goal,
                                sub_status_text);
                } else if (sub_stat->goal == -1) {
                    // When target is -1 it acts as infinte counter, then goal doesn't get printed
                    // It's a completable single-criterion stat
                    log_message(LOG_INFO, "[Stat] %s: %d - %s\n",
                                stat_cat->display_name,
                                sub_stat->progress,
                                sub_status_text);
                } else if (sub_stat->goal == 0 && version <= MC_VERSION_1_6_4) {
                    // When target value would be 0 or NOT EXISTENT, but "icon" key exists, then "icon" key should be removed
                    // NO STAT SHOULD EVER HAVE A GOAL OF 0
                    // HERE WE'RE HANDLING GOAL OF 0 AND NO ICON KEY
                    // THEN it's a hidden stat for multi-stage for legacy
                    log_message(LOG_INFO, "[Stat Tracker] %s: %d\n",
                                stat_cat->display_name,
                                sub_stat->progress);
                } else {
                    // When target value is 0 or NOT EXISTENT for mid-era and modern versions
                    // Then it's a MISTAKE IN THE TEMPLATE FILE
                    log_message(
                        LOG_INFO,
                        "[Stat] %s: %d\n - HAS GOAL OF %d, which it shouldn't have. This stat can't be completed.\n",
                        stat_cat->display_name,
                        sub_stat->progress,
                        sub_stat->goal);
                }
            } else {
                // Full stat category uses the status of the category, others use sub_status above
                log_message(LOG_INFO, "[Stat Category] %s (%d/%d): %s\n",
                            stat_cat->display_name,
                            stat_cat->completed_criteria_count,
                            stat_cat->criteria_count,
                            status_text);

                // Print each sub-stat (criterion)
                for (int j = 0; j < stat_cat->criteria_count; j++) {
                    TrackableItem *sub_stat = stat_cat->criteria[j];
                    const char *sub_status_text;
                    char sub_stat_key[512];
                    snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                             sub_stat->root_name);
                    cJSON *sub_override = override_obj ? cJSON_GetObjectItem(override_obj, sub_stat_key) : nullptr;
                    if (sub_stat->done) {
                        sub_status_text = (sub_stat->is_manually_completed && cJSON_IsTrue(sub_override))
                                              ? "DONE (MANUAL)"
                                              : "DONE";
                    } else {
                        sub_status_text = "NOT DONE";
                    }
                    // You could add a check here for sub_stat->goal != -1 if you don't want to print progress for untracked stats
                    log_message(LOG_INFO, "  - %s: %s%d / %d - %s\n",
                                sub_stat->display_name,
                                sub_stat->is_shared ? "SHARED - " : "",
                                sub_stat->progress,
                                sub_stat->goal,
                                sub_status_text);
                }
            }
        }


        // Only print unlocks if they exist
        if (t->template_data->unlock_count > 0) {
            log_message(LOG_INFO, "[Unlocks] %d / %d completed\n", t->template_data->unlocks_completed_count,
                        t->template_data->unlock_count);
        }

        // Loop to print each unlock individually
        for (int i = 0; i < t->template_data->unlock_count; i++) {
            TrackableItem *unlock = t->template_data->unlocks[i];
            if (!unlock) continue;
            log_message(LOG_INFO, "  - %s: %s\n", unlock->display_name, unlock->done ? "UNLOCKED" : "LOCKED");
        }


        for (int i = 0; i < t->template_data->custom_goal_count; i++) {
            TrackableItem *custom_goal = t->template_data->custom_goals[i];

            // Check if the custom goal is a counter (has a target > 0)
            if (custom_goal->goal == -1) {
                // Case 1: Infinite counter. Show the count, or "COMPLETED" if overridden.
                if (custom_goal->done) {
                    log_message(LOG_INFO, "[Custom Counter] %s: COMPLETED (MANUAL)\n", custom_goal->display_name);
                } else {
                    log_message(LOG_INFO, "[Custom Counter] %s: %d\n", custom_goal->display_name,
                                custom_goal->progress);
                }
            } else if (custom_goal->goal > 0) {
                // Case 2: Normal counter with a target goal.
                log_message(LOG_INFO, "[Custom Counter] %s: %d / %d - %s\n",
                            custom_goal->display_name,
                            custom_goal->progress,
                            custom_goal->goal,
                            custom_goal->done ? "COMPLETED" : "INCOMPLETE");
            } else {
                // Case 3: Simple on/off toggle when goal == 0 or not set
                log_message(LOG_INFO, "[Custom Goal] %s: %s\n",
                            custom_goal->display_name,
                            custom_goal->done ? "COMPLETED" : "INCOMPLETE");
            }
        }

        for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
            MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
            if (goal && goal->stages && goal->current_stage < goal->stage_count) {
                SubGoal *active_stage = goal->stages[goal->current_stage];

                // Check if the active stage is a stat and print its progress
                if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress > 0) {
                    log_message(LOG_INFO, "[Multi-Stage Goal] %s: %s (%d/%d)\n",
                                goal->display_name,
                                active_stage->display_text,
                                active_stage->current_stat_progress,
                                active_stage->required_progress);
                } else {
                    // If it's not "stat" print this
                    log_message(LOG_INFO, "[Multi-Stage Goal] %s: %s\n", goal->display_name,
                                active_stage->display_text);
                }
            }
        }

        for (int i = 0; i < t->template_data->counter_goal_count; i++) {
            CounterGoal *counter = t->template_data->counter_goals[i];
            if (!counter) continue;
            log_message(LOG_INFO, "[Counter] %s: %d / %d - %s\n",
                        counter->display_name,
                        counter->completed_count,
                        counter->linked_goal_count,
                        counter->done ? "COMPLETED" : "INCOMPLETE");
        }

        // Print if advancements are more than zero or progress isn't empty
        if (t->template_data->advancement_goal_count > 0) {
            if (version >= MC_VERSION_1_12) {
                log_message(LOG_INFO, "[Advancements] %d / %d completed\n",
                            t->template_data->advancements_completed_count,
                            t->template_data->advancement_goal_count); // Excluding recipes
            } else {
                log_message(LOG_INFO, "[Achievements] %d / %d completed\n",
                            t->template_data->advancements_completed_count,
                            t->template_data->advancement_goal_count);
            }
        }

        if (t->template_data->total_progress_steps > 0) {
            log_message(LOG_INFO, "[Overall Progress] %.2f%%\n", t->template_data->overall_progress_percentage);
        }

        log_message(LOG_INFO, "============================================================\n");
    }
    // Force the output buffer to write to the console immediately
    fflush(stdout);
}
