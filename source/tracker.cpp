// Copyright (c) 2025 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 24.06.2025.
//

#include <cstdio>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cJSON.h>
#include <cmath>
#include <ctime>

#include "tracker.h"

#include <set>
#include <vector>

#include "init_sdl.h"
#include "path_utils.h"
#include "settings_utils.h" // For note related defaults as well
#include "file_utils.h" // has the cJSON_from_file function
#include "temp_creator_utils.h"
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
            t->notes_path[0] = '\0'; // Cannot determine püath if world/saves path is unknown
            return;
        }

        // Create a unique identifier for the world from the saves path and world name
        char full_world_path[MAX_PATH_LENGTH * 2];
        snprintf(full_world_path, sizeof(full_world_path), "%s/%s", t->saves_path, t->world_name);

        // Hash the full path to create a safe filename
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

        if (entry_to_update) {
            // World exists, update its timestamp
            cJSON_ReplaceItemInObject(entry_to_update, "last_used", cJSON_CreateNumber((double) time(0)));
        } else {
            // new world, add a new entry
            cJSON *new_entry = cJSON_CreateObject();
            cJSON_AddNumberToObject(new_entry, "hash", (double) world_hash);
            cJSON_AddStringToObject(new_entry, "path", full_world_path);
            cJSON_AddNumberToObject(new_entry, "last_used", (double) time(0));
            cJSON_AddItemToArray(manifest, new_entry);

            // Check if we need to prune old notes (LRU - least recently used)
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

        // Save the updated manifest
        FILE *manifest_file = fopen(get_notes_manifest_path(), "w");
        if (manifest_file) {
            char *manifest_str = cJSON_Print(manifest);
            fputs(manifest_str, manifest_file);
            fclose(manifest_file);
            free(manifest_str);
        }
        cJSON_Delete(manifest);
    } else {
        // Per template mode
        construct_template_paths((AppSettings *) settings); // This will update the template paths
        strncpy(t->notes_path, settings->notes_path, MAX_PATH_LENGTH - 1);
        t->notes_path[MAX_PATH_LENGTH - 1] = '\0';
    }
}


// NON STATIC FUNCTION -------------------------------------------------------------------

bool str_contains_insensitive(const char *haystack, const char *needle) {
    if (!needle || *needle == '\0') return true; // An empty search query matches everything
    if (!haystack) return false;

    std::string haystack_lower = haystack;
    std::transform(haystack_lower.begin(), haystack_lower.end(), haystack_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::string needle_lower = needle;
    std::transform(needle_lower.begin(), needle_lower.end(), needle_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return haystack_lower.find(needle_lower) != std::string::npos;
}

// END OF NON STATIC FUNCTION -------------------------------------------------------------------


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
        snprintf(full_path, sizeof(full_path), "%s/gui/%s", get_resources_path(), setting_path);

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
            snprintf(full_path, sizeof(full_path), "%s/gui/%s", get_resources_path(), default_path);
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
            snprintf(full_icon_path, sizeof(full_icon_path), "%s/icons/%s", get_resources_path(), icon->valuestring);
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
                                     get_resources_path(),
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

                    cJSON *target = cJSON_GetObjectItem(cat_json, "target");
                    if (cJSON_IsNumber(target)) the_criterion->goal = target->valueint;

                    new_cat->criteria[0] = the_criterion;
                }
            }
        }

        // Implicitly, if criteria_obj exists but is empty, we do nothing.
        // This correctly leaves criteria_count at 0, making it a "simple" category.
        (*categories_array)[i++] = new_cat;
        cat_json = cat_json->next;
    }
}

// Helper for counting
typedef struct {
    char icon_path[256];
    int count;
} IconPathCounter;

// helper function to process and count all sub-items from a list of categories, simple stats are excluded
static int count_all_icon_paths(IconPathCounter **counts, int capacity, int current_unique_count,
                                TrackableCategory **categories, int cat_count) {
    if (!categories) return current_unique_count;

    for (int i = 0; i < cat_count; i++) {
        // Skip simple stat categories, as their criteria are not rendered individually on the overlay
        if (categories[i]->is_single_stat_category) {
            continue;
        }
        for (int j = 0; j < categories[i]->criteria_count; j++) {
            TrackableItem *crit = categories[i]->criteria[j];
            // Only count items that have a valid icon path AND ARE NOT HIDDEN
            if (crit->is_hidden || crit->icon_path[0] == '\0') {
                continue;
            }

            bool found = false;
            for (int k = 0; k < current_unique_count; k++) {
                if (strcmp((*counts)[k].icon_path, crit->icon_path) == 0) {
                    (*counts)[k].count++;
                    found = true;
                    break;
                }
            }

            // If the criterion is not found in the counts array, add it
            if (!found && current_unique_count < capacity) {
                strncpy((*counts)[current_unique_count].icon_path, crit->icon_path, 255);
                (*counts)[current_unique_count].icon_path[255] = '\0';
                (*counts)[current_unique_count].count = 1;
                current_unique_count++;
            }
        }
    }
    return current_unique_count;
}

// Helper function to flag the items that are shared, simple stats are excluded
static void flag_shared_icons(IconPathCounter *counts, int unique_count, TrackableCategory **categories,
                              int cat_count) {
    if (!categories) return;

    for (int i = 0; i < cat_count; i++) {
        // Skip simple stat categories, as their criteria are not rendered individually on the overlay
        if (categories[i]->is_single_stat_category) {
            continue;
        }
        for (int j = 0; j < categories[i]->criteria_count; j++) {
            TrackableItem *crit = categories[i]->criteria[j];
            crit->is_shared = false; // Reset first

            // A hidden criterion cannot be part of a shared group
            if (crit->is_hidden || crit->icon_path[0] == '\0') {
                continue;
            }

            for (int k = 0; k < unique_count; k++) {
                // If the criterion's icon path is found in the list and is used more than once
                if (strcmp(counts[k].icon_path, crit->icon_path) == 0 && counts[k].count > 1) {
                    crit->is_shared = true;
                    break;
                }
            }
        }
    }
}

/**
 * @brief Detects criteria that share the same icon path across multiple advancements or stats and flags them.
 *
 * This function iterates through all parsed advancements or stats and their criteria to identify
 * criteria that have the same icon_path. If a criterion's icon is found in more than one
 * place, its 'is_shared' flag is set to true. This allows the rendering
 * logic to visually distinguish them.
 *
 * @param t The Tracker struct.
 */
static void tracker_detect_shared_icons(Tracker *t, const AppSettings *settings) {
    (void) settings;
    int total_criteria = t->template_data->total_criteria_count + t->template_data->stat_total_criteria_count;
    if (total_criteria == 0) return;

    IconPathCounter *counts = (IconPathCounter *) calloc(total_criteria, sizeof(IconPathCounter));
    if (!counts) return;

    int unique_count = 0;
    unique_count = count_all_icon_paths(&counts, total_criteria, unique_count, t->template_data->advancements,
                                        t->template_data->advancement_count);
    unique_count = count_all_icon_paths(&counts, total_criteria, unique_count, t->template_data->stats,
                                        t->template_data->stat_count);

    flag_shared_icons(counts, unique_count, t->template_data->advancements, t->template_data->advancement_count);
    flag_shared_icons(counts, unique_count, t->template_data->stats, t->template_data->stat_count);

    free(counts);
    counts = nullptr;
    log_message(LOG_INFO, "[TRACKER] Shared icon detection complete.\n");
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
                                            int *count, const char *lang_key_prefix, const AppSettings *settings) {
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
                snprintf(full_icon_path, sizeof(full_icon_path), "%s/icons/%s", get_resources_path(),
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

        // Parse root_name and icon
        cJSON *root_name = cJSON_GetObjectItem(goal_item_json, "root_name");
        cJSON *icon = cJSON_GetObjectItem(goal_item_json, "icon");

        if (cJSON_IsString(root_name)) {
            strncpy(new_goal->root_name, root_name->valuestring, sizeof(new_goal->root_name) - 1);
            new_goal->root_name[sizeof(new_goal->root_name) - 1] = '\0';
        }
        if (cJSON_IsString(icon)) {
            char full_icon_path[sizeof(new_goal->icon_path)];
            snprintf(full_icon_path, sizeof(full_icon_path), "%s/icons/%s", get_resources_path(), icon->valuestring);
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
                // Add the stage to the goal
                new_goal->stages[j++] = new_stage;
            }
        }
        // Add the goal to the array
        (*goals_array)[i++] = new_goal;
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
        item->progress = 0;

        if (item->goal == -1) {
            // INFINITE COUNTER WITH TARGET -1 and MANUAL OVERRIDE
            if (cJSON_IsTrue(item_progress_json)) {
                // Manually overridden to be complete
                item->done = true;
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
 * @brief Calculates the overall progress percentage based on all tracked items. Advancements are separately!!
 * Advancements with is_recipe set to true count towards the progress here. (modern versions)
 *
 * It first calculates the total number of "steps" (e.g., Recipes, criteria, sub-stats or stat if no sub-stats, unlocks,
 * custom goals, and multi-stage goals),
 * then the number of completed "steps", and finally calculates the overall progress percentage.
 *
 * @param t A pointer to the Tracker struct.
 * @param version The version of the game.
 * @param settings A pointer to the AppSettings struct.
 *
 */
static void tracker_calculate_overall_progress(Tracker *t, MC_Version version, const AppSettings *settings) {
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
        // Cleanup for animated textures
        if (items[i]) {
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

    td->advancements = nullptr;
    td->stats = nullptr;
    td->unlocks = nullptr;
    td->custom_goals = nullptr;
    td->multi_stage_goals = nullptr;

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
    strncpy((*cache)[*cache_count].path, path, MAX_PATH_LENGTH - 1);
    (*cache)[*cache_count].path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null-termination

    (*cache)[*cache_count].texture = new_texture;
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
    strncpy((*cache)[*cache_count].path, path, MAX_PATH_LENGTH - 1);
    (*cache)[*cache_count].path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null-termination

    (*cache)[*cache_count].anim = new_anim;
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

    // Initialize notes state
    t->notes_window_open = false;
    t->notes_buffer[0] = '\0';
    t->notes_path[0] = '\0'; // Initialize the new notes path
    t->search_buffer[0] = '\0';
    t->focus_search_box_requested = false;
    t->focus_tc_search_box = false; // CURRENTLY UNUSED
    t->is_temp_creator_focused = false;
    t->notes_widget_id_counter = 0;


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

    // Initialize camera and zoom
    t->camera_offset = ImVec2(0.0f, 0.0f);
    t->zoom_level = 1.0f;
    t->layout_locked = false;
    t->locked_layout_width = 0.0f;

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
    snprintf(font_path, sizeof(font_path), "%s/fonts/Minecraft.ttf", get_resources_path());
    t->minecraft_font = TTF_OpenFont(font_path, 24);
    if (!t->minecraft_font) {
        log_message(
            LOG_ERROR,
            "[TRACKER] Failed to load Minecraft font (asure path contains only standard English (ASCII) characters): %s\n",
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
    (void) t; // Not directly used, but kept for consistency

    switch (event->type) {
        // This should be handled in the global event handler
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            *is_running = false;
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

// Periodically recheck file changes
void tracker_update(Tracker *t, float *deltaTime, const AppSettings *settings) {
    (void) deltaTime;

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
    tracker_calculate_overall_progress(t, version, settings); //THIS TRACKS SUB-ADVANCEMENTS AND EVERYTHING ELSE

    // Clean up the parsed JSON objects
    cJSON_Delete(player_adv_json);
    cJSON_Delete(player_stats_json);
    cJSON_Delete(player_unlocks_json);
    cJSON_Delete(settings_json);
}

// UNUSED
void tracker_render(Tracker *t, const AppSettings *settings) {
    (void) t;
    (void) settings;
}

// END OF NON-STATIC FUNCTIONS ------------------------------------

// -------------------------------------------- TRACKER RENDERING START --------------------------------------------

// START OF STATIC FUNCTIONS ------------------------------------
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

/**
 * @brief Renders a section of items that are TrackableCategories (e.g., Advancements, Stats).
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
    const float LOD_TEXT_SUB_THRESHOLD = 0.60f;
    // 2. Zoom < 0.40: Hide Main Item Text, Parent Checkboxes & Sub-Item Checkboxes
    const float LOD_TEXT_MAIN_THRESHOLD = 0.40f;
    // 3. Zoom < 0.25: Simplify Sub-Item Icons to Squares
    const float LOD_ICON_DETAIL_THRESHOLD = 0.25f;


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
                should_hide_parent_based_on_mode = cat->is_hidden || is_category_considered_complete;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_parent_based_on_mode = cat->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_parent_based_on_mode = false;
                break;
        }

        // Skip entire category if hidden by mode
        if (should_hide_parent_based_on_mode) continue;

        // Apply Search Filter for counting
        bool parent_matches_search = str_contains_insensitive(cat->display_name, t->search_buffer);
        bool any_visible_child_matches_search = false;

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
                        should_hide_child_based_on_mode = crit->is_hidden || crit->done;
                        break;
                    case HIDE_ONLY_TEMPLATE_HIDDEN:
                        should_hide_child_based_on_mode = crit->is_hidden;
                        break;
                    case SHOW_ALL:
                        should_hide_child_based_on_mode = false;
                        break;
                }

                if (should_hide_child_based_on_mode) continue; // Skip hidden child

                // Now check search filter for this visible child
                if (str_contains_insensitive(crit->display_name, t->search_buffer)) {
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
                }
            }
        }

        // --- Count Main Item ---
        // Count if parent matches OR at least one visible child matches
        if (parent_matches_search || any_visible_child_matches_search) {
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
                should_hide_parent_render = cat->is_hidden || is_considered_complete_render;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_parent_render = cat->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_parent_render = false;
                break;
        }

        if (should_hide_parent_render) continue; // Skip if parent hidden based on mode

        // Check search filter
        bool parent_matches = str_contains_insensitive(cat->display_name, t->search_buffer);
        bool child_matches_render = false;
        if (!parent_matches) {
            for (int j = 0; j < cat->criteria_count; j++) {
                TrackableItem *crit = cat->criteria[j];
                if (!crit) continue;

                bool should_hide_crit_render = false;
                switch (settings->goal_hiding_mode) {
                    case HIDE_ALL_COMPLETED: should_hide_crit_render = crit->is_hidden || crit->done;
                        break;
                    case HIDE_ONLY_TEMPLATE_HIDDEN: should_hide_crit_render = crit->is_hidden;
                        break;
                    case SHOW_ALL: should_hide_crit_render = false;
                        break;
                }

                if (!should_hide_crit_render && str_contains_insensitive(crit->display_name, t->search_buffer)) {
                    child_matches_render = true;
                    break;
                }
            }
        }

        if (parent_matches || child_matches_render) {
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
    render_section_separator(t, settings, current_y, section_title, text_color,
                             completed_count, total_visible_count,
                             section_has_sub_items ? completed_sub_count : -1, // Pass -1 if no sub-items
                             section_has_sub_items ? total_visible_sub_count : -1);


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
                    parent_should_hide_render = cat->is_hidden || is_considered_complete_render;
                    break;
                case HIDE_ONLY_TEMPLATE_HIDDEN:
                    parent_should_hide_render = cat->is_hidden;
                    break;
                case SHOW_ALL:
                    parent_should_hide_render = false;
                    break;
            }
            if (parent_should_hide_render) continue;

            // Search Filter (for rendering visibility)
            bool parent_matches = str_contains_insensitive(cat->display_name, t->search_buffer);
            bool child_matches_render = false;
            if (!parent_matches) {
                for (int j = 0; j < cat->criteria_count; j++) {
                    TrackableItem *crit = cat->criteria[j];
                    if (!crit) continue;

                    // Apply the same hiding logic here as in the rendering pass
                    bool crit_should_hide_render = false;
                    switch (settings->goal_hiding_mode) {
                        case HIDE_ALL_COMPLETED:
                            crit_should_hide_render = crit->is_hidden || crit->done;
                            break;
                        case HIDE_ONLY_TEMPLATE_HIDDEN:
                            crit_should_hide_render = crit->is_hidden;
                            break;
                        case SHOW_ALL:
                            crit_should_hide_render = false;
                            break;
                    }

                    // Now, check both hiding status AND the search term
                    if (!crit_should_hide_render && str_contains_insensitive(crit->display_name, t->search_buffer)) {
                        child_matches_render = true;
                        break;
                    }
                }
            }
            if (!parent_matches && !child_matches_render) continue; // Skip if neither parent nor child matches search

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
                            crit_should_hide_width = crit->is_hidden || crit->done;
                            break;
                        case HIDE_ONLY_TEMPLATE_HIDDEN:
                            crit_should_hide_width = crit->is_hidden;
                            break;
                        case SHOW_ALL:
                            crit_should_hide_width = false;
                            break;
                    }

                    // Also check if this specific child matches search if parent didn't
                    bool crit_matches_search = parent_matches || (child_matches_render && str_contains_insensitive(
                                                                      crit->display_name, t->search_buffer));

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


    float padding = 50.0f, current_x = padding, row_max_height = 0.0f;

    // Adjust vertical spacing -> need to do this for all render_*_section functions
    const float vertical_spacing = settings->tracker_vertical_spacing; // Changed from 16.0f

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
                    should_hide_parent_render = cat->is_hidden || is_considered_complete_render;
                    break;
                case HIDE_ONLY_TEMPLATE_HIDDEN:
                    should_hide_parent_render = cat->is_hidden;
                    break;
                case SHOW_ALL:
                    should_hide_parent_render = false;
                    break;
            }
            if (should_hide_parent_render) continue;

            // --- Search Filtering (for rendering) ---
            bool parent_matches = str_contains_insensitive(cat->display_name, t->search_buffer);
            std::vector<TrackableItem *> matching_children; // Store pointers to children that match search
            bool child_matches_search = false; // Flag if any child matches search
            if (!parent_matches) {
                for (int j = 0; j < cat->criteria_count; j++) {
                    TrackableItem *crit = cat->criteria[j];
                    if (!crit) continue;

                    bool should_hide_crit_render = false;
                    switch (settings->goal_hiding_mode) {
                        case HIDE_ALL_COMPLETED: should_hide_crit_render = crit->is_hidden || crit->done;
                            break;
                        case HIDE_ONLY_TEMPLATE_HIDDEN: should_hide_crit_render = crit->is_hidden;
                            break;
                        case SHOW_ALL: should_hide_crit_render = false;
                            break;
                    }

                    if (!should_hide_crit_render && str_contains_insensitive(crit->display_name, t->search_buffer)) {
                        matching_children.push_back(crit); // Add matching child to list
                        child_matches_search = true; // Set flag
                    }
                }
            }
            if (!parent_matches && !child_matches_search) continue;
            // Skip if neither parent nor any child matches search


            // --- Item Height Calculation ---
            // Must calculate exact height even if text is invisible to maintain layout
            char snapshot_text[8] = "";
            if (!is_stat_section && version <= MC_VERSION_1_6_4 && !settings->using_stats_per_world_legacy) {
                if (cat->done && !cat->done_in_snapshot) { strcpy(snapshot_text, "(New)"); } else if (cat->done) {
                    strcpy(snapshot_text, "(Old)");
                }
            }

            char progress_text[32] = "";
            if (is_stat_section) {
                if (is_complex) {
                    // Multi-stat
                    snprintf(progress_text, sizeof(progress_text), "(%d / %d)", cat->completed_criteria_count,
                             cat->criteria_count);
                } else if (cat->criteria_count == 1) {
                    // Single-stat
                    TrackableItem *crit = cat->criteria[0];
                    if (crit->goal > 0) {
                        snprintf(progress_text, sizeof(progress_text), "(%d / %d)", crit->progress, crit->goal);
                    } else if (crit->goal == -1) {
                        snprintf(progress_text, sizeof(progress_text), "(%d)", crit->progress);
                    }
                }
            } else {
                // Advancement
                if (cat->criteria_count > 0) {
                    snprintf(progress_text, sizeof(progress_text), "(%d / %d)", cat->completed_criteria_count,
                             cat->criteria_count);
                }
            }


            // Calculate text sizes with correct font scaling
            SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize);
            ImVec2 text_size = ImGui::CalcTextSize(cat->display_name); // Uses main tracker_font_size
            RESET_FONT_SCALE();

            // Scale font to sub-size for progress/snapshot text measurement
            SET_FONT_SCALE(settings->tracker_sub_font_size, t->tracker_font->LegacySize);
            ImVec2 progress_text_size = ImGui::CalcTextSize(progress_text);
            ImVec2 snapshot_text_size = ImGui::CalcTextSize(snapshot_text);
            RESET_FONT_SCALE();

            int visible_criteria_render_count = 0; // How many criteria *will be rendered* for height calc

            if (is_complex) {
                if (parent_matches) {
                    // If parent matches, count all children that pass the standard hide filter for rendering.
                    for (int j = 0; j < cat->criteria_count; j++) {
                        TrackableItem *crit = cat->criteria[j];
                        if (!crit) continue;

                        bool should_hide_crit_render = false;
                        switch (settings->goal_hiding_mode) {
                            case HIDE_ALL_COMPLETED: should_hide_crit_render = crit->is_hidden || crit->done;
                                break;
                            case HIDE_ONLY_TEMPLATE_HIDDEN: should_hide_crit_render = crit->is_hidden;
                                break;
                            case SHOW_ALL: should_hide_crit_render = false;
                                break;
                        }
                        if (!should_hide_crit_render) {
                            // Count if it passes the hiding filter
                            visible_criteria_render_count++;
                        }
                    }
                } else {
                    // If only children match, the count is the size of our pre-filtered list.
                    visible_criteria_render_count = matching_children.size();
                }
            }

            // Calculate height: Base icon + main text + snapshot text (if any) + progress text (if any) + space for each visible child + padding
            float item_height = 96.0f; // Base icon background size
            item_height += text_size.y + 4.0f; // Main display name + padding
            if (snapshot_text[0] != '\0') item_height += snapshot_text_size.y + 4.0f; // Snapshot text + padding
            if (progress_text[0] != '\0') item_height += progress_text_size.y + 4.0f; // Progress text + padding
            if (visible_criteria_render_count > 0) {
                item_height += 12.0f; // Initial padding before criteria list
                item_height += (float) visible_criteria_render_count * 36.0f; // Height for each criterion row
            }


            // --- Layout and Culling ---
            if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
                current_x = padding;
                current_y += row_max_height;
                row_max_height = 0.0f;
            }

            ImVec2 screen_pos = ImVec2((current_x * t->zoom_level) + t->camera_offset.x,
                                       (current_y * t->zoom_level) + t->camera_offset.y);

            // Culling logic
            ImVec2 item_size_on_screen = ImVec2(uniform_item_width * t->zoom_level, item_height * t->zoom_level);
            bool is_visible_on_screen = !(screen_pos.x > io.DisplaySize.x || (screen_pos.x + item_size_on_screen.x) < 0
                                          ||
                                          screen_pos.y > io.DisplaySize.y || (screen_pos.y + item_size_on_screen.y) <
                                          0);


            // --- Rendering Core Logic ---
            if (is_visible_on_screen) {
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
                if (texture_to_draw)
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
                    draw_list->AddImage((void *) texture_to_draw, p_min, p_max);
                    // --- End Icon Scaling and Centering Logic (Main Icon) ---
                }


                // Render Text (Main Name, Snapshot, Progress)
                float current_text_y = screen_pos.y + bg_size.y * t->zoom_level + (4.0f * t->zoom_level);
                float main_font_size = settings->tracker_font_size;
                float sub_font_size = settings->tracker_sub_font_size; // Max 32 to prevent shifting downwards

                // LOD: Only render Main Text if zoom is sufficient
                if (t->zoom_level > LOD_TEXT_MAIN_THRESHOLD) {
                    // Main Name
                    draw_list->AddText(nullptr, main_font_size * t->zoom_level,
                                       ImVec2(
                                           screen_pos.x + (bg_size.x * t->zoom_level - text_size.x * t->zoom_level) *
                                           0.5f,
                                           current_text_y), text_color, cat->display_name);
                }
                // ADVANCE LAYOUT EVEN IF NOT RENDERED
                current_text_y += text_size.y * t->zoom_level + 4.0f * t->zoom_level;

                // Snapshot Text (if applicable)
                if (snapshot_text[0] != '\0') {
                    // LOD: Tie snapshot text to main text visibility
                    if (t->zoom_level > LOD_TEXT_MAIN_THRESHOLD) {
                        draw_list->AddText(nullptr, sub_font_size * t->zoom_level,
                                           ImVec2(
                                               screen_pos.x + (
                                                   bg_size.x * t->zoom_level - snapshot_text_size.x * t->zoom_level) *
                                               0.5f,
                                               current_text_y), text_color_faded, snapshot_text); // Use faded color
                    }
                    // ADVANCE LAYOUT EVEN IF NOT RENDERED
                    current_text_y += snapshot_text_size.y * t->zoom_level + 4.0f * t->zoom_level;
                }

                // Progress Text (if applicable)
                if (progress_text[0] != '\0') {
                    // LOD: Tie progress text to SUB text visibility (as requested)
                    if (t->zoom_level > LOD_TEXT_SUB_THRESHOLD) {
                        draw_list->AddText(nullptr, sub_font_size * t->zoom_level,
                                           ImVec2(
                                               screen_pos.x + (
                                                   bg_size.x * t->zoom_level - progress_text_size.x * t->zoom_level) *
                                               0.5f,
                                               current_text_y), text_color, progress_text);
                    }
                    // ADVANCE LAYOUT EVEN IF NOT RENDERED
                    current_text_y += progress_text_size.y * t->zoom_level + 4.0f * t->zoom_level;
                    // Add padding even after last text line before criteria
                }

                // Render Criteria/Sub-Stats (if complex and visible)
                if (is_complex && visible_criteria_render_count > 0) {
                    float sub_item_y_offset_world = current_y + (current_text_y - screen_pos.y) / t->zoom_level + 8.0f;
                    // Start below last text + padding


                    // Decide which list of children to iterate over based on search results
                    std::vector<TrackableItem *> children_to_render;
                    if (parent_matches) {
                        // If parent matched, render all non-hidden children
                        for (int j = 0; j < cat->criteria_count; ++j) {
                            TrackableItem *crit = cat->criteria[j];
                            if (!crit) continue;
                            bool should_hide_crit_render = false;
                            switch (settings->goal_hiding_mode) {
                                case HIDE_ALL_COMPLETED: should_hide_crit_render = crit->is_hidden || crit->done;
                                    break;
                                case HIDE_ONLY_TEMPLATE_HIDDEN: should_hide_crit_render = crit->is_hidden;
                                    break;
                                case SHOW_ALL: should_hide_crit_render = false;
                                    break;
                            }
                            if (!should_hide_crit_render) {
                                children_to_render.push_back(crit);
                            }
                        }
                    } else {
                        // If only children matched, render only the ones from the matching list
                        children_to_render = matching_children;
                    }


                    // Render each visible child
                    for (TrackableItem *crit: children_to_render) {
                        if (!crit) continue; // Should not happen, but safety check

                        ImVec2 crit_base_pos_screen = ImVec2((current_x * t->zoom_level) + t->camera_offset.x,
                                                             (sub_item_y_offset_world * t->zoom_level) + t->
                                                             camera_offset.y);
                        float current_element_x_screen = crit_base_pos_screen.x;

                        // LOD for Sub-Item Icon
                        if (t->zoom_level > LOD_ICON_DETAIL_THRESHOLD) {
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
                        } else {
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

                            if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                // Toggle manual completion and update 'done' status
                                crit->is_manually_completed = !crit->is_manually_completed;
                                bool crit_naturally_done = (crit->goal > 0 && crit->progress >= crit->goal);
                                // A criterion is done if forced OR naturally complete
                                crit->done = crit->is_manually_completed || crit_naturally_done;

                                // Recalculate parent completion counts and status
                                cat->completed_criteria_count = 0;
                                for (int k = 0; k < cat->criteria_count; ++k) {
                                    if (cat->criteria[k]->done) cat->completed_criteria_count++;
                                }
                                bool all_children_done = (
                                    cat->criteria_count > 0 && cat->completed_criteria_count >= cat->criteria_count);
                                cat->done = cat->is_manually_completed || all_children_done;
                                // Parent done if forced OR all children done

                                settings_save(settings, t->template_data, SAVE_CONTEXT_ALL);
                                SDL_SetAtomicInt(&g_needs_update, 1);
                                SDL_SetAtomicInt(&g_game_data_changed, 1);
                            }
                        }

                        // ADVANCE LAYOUT EVEN IF CHECKBOX NOT RENDERED
                        if (is_stat_section && cat->criteria_count > 1) {
                            current_element_x_screen += 20.0f * t->zoom_level + 4.0f * t->zoom_level;
                            // Checkbox width + padding
                        }


                        // Render Child Text and Progress
                        // LOD: Check if zoom level is sufficient for sub-text
                        if (t->zoom_level > LOD_TEXT_SUB_THRESHOLD) {
                            ImU32 current_child_text_color = crit->done ? text_color_faded : text_color;

                            // Calculate text height using the correct font scale
                            float scale_factor = 1.0f;
                            if (settings->tracker_font_size > 0.0f) {
                                // Prevent division by zero
                                scale_factor = settings->tracker_sub_font_size / settings->tracker_font_size;
                            }

                            ImGui::PushFont(t->tracker_font); // Ensure we're scaling from the correct base font
                            ImGui::SetWindowFontScale(scale_factor);

                            // Calculate the height of the text. (Width is also needed for the X advance)
                            ImVec2 child_text_size = ImGui::CalcTextSize(crit->display_name);

                            ImGui::SetWindowFontScale(1.0f); // Restore scale
                            ImGui::PopFont();

                            // Calculate the vertically centered Y position
                            // (BoxTop) + ( (BoxHeight - TextHeight) / 2 )
                            // Both the box height AND the text height must be scaled by zoom.
                            float text_y_pos = crit_base_pos_screen.y + (
                                                   (32.0f * t->zoom_level) - (child_text_size.y * t->zoom_level)) *
                                               0.5f;

                            // Draw the text
                            ImVec2 child_text_pos = ImVec2(current_element_x_screen, text_y_pos);
                            draw_list->AddText(nullptr, sub_font_size * t->zoom_level, child_text_pos,
                                               current_child_text_color, crit->display_name);

                            // Advance X position using the width we just calculated
                            current_element_x_screen += (child_text_size.x * t->zoom_level) + (4.0f * t->zoom_level);

                            char crit_progress_text[32] = "";
                            if (is_stat_section) {
                                if (crit->goal > 0) {
                                    snprintf(crit_progress_text, sizeof(crit_progress_text), "(%d / %d)",
                                             crit->progress,
                                             crit->goal);
                                } else if (crit->goal == -1) {
                                    snprintf(crit_progress_text, sizeof(crit_progress_text), "(%d)", crit->progress);
                                }

                                if (crit_progress_text[0] != '\0') {
                                    ImVec2 crit_progress_pos = ImVec2(current_element_x_screen, text_y_pos);
                                    draw_list->AddText(nullptr, sub_font_size * t->zoom_level, crit_progress_pos,
                                                       current_child_text_color, crit_progress_text);
                                }
                            }
                        }

                        sub_item_y_offset_world += 36.0f; // Move down for the next criterion
                    } // End loop through children_to_render
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

                    if (is_hovered_parent && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        cat->is_manually_completed = !cat->is_manually_completed;
                        // Update parent 'done' status
                        bool all_children_naturally_done = (
                            cat->criteria_count > 0 && cat->completed_criteria_count >= cat->criteria_count);
                        cat->done = cat->is_manually_completed || all_children_naturally_done;

                        // Update all children 'done' status based on parent override
                        for (int j = 0; j < cat->criteria_count; ++j) {
                            TrackableItem *crit = cat->criteria[j];
                            bool crit_naturally_done = (crit->goal > 0 && crit->progress >= crit->goal);
                            // Child is done if parent forces it OR child forces it OR naturally done
                            crit->done = cat->is_manually_completed || crit->is_manually_completed ||
                                         crit_naturally_done;
                        }
                        // Recalculate completed count AFTER potentially updating children
                        cat->completed_criteria_count = 0;
                        for (int k = 0; k < cat->criteria_count; ++k) {
                            if (cat->criteria[k]->done) cat->completed_criteria_count++;
                        }


                        settings_save(settings, t->template_data, SAVE_CONTEXT_ALL);
                        SDL_SetAtomicInt(&g_needs_update, 1);
                        SDL_SetAtomicInt(&g_game_data_changed, 1);
                    }
                } // End if (is_stat_section) for parent checkbox
            } // End if (is_visible_on_screen)

            // --- Update Layout Position ---
            current_x += uniform_item_width; // Use the final uniform_item_width directly
            row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
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
    const float LOD_TEXT_THRESHOLD = 0.5f; // Text disappears below 50% zoom

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
                should_hide_based_on_mode = item->is_hidden || is_item_considered_complete;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_based_on_mode = item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_based_on_mode = false;
                break;
        }

        // Apply Search Filter for counting
        bool matches_search = str_contains_insensitive(item->display_name, t->search_buffer);

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
                should_hide_render = item->is_hidden || item->done;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Combine hiding and search filter
        if (!should_hide_render && str_contains_insensitive(item->display_name, t->search_buffer)) {
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
    render_section_separator(t, settings, current_y, section_title, text_color,
                             completed_count, total_visible_count, -1, -1);


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
                    should_hide_width = item->is_hidden || item->done;
                    break;
                case HIDE_ONLY_TEMPLATE_HIDDEN:
                    should_hide_width = item->is_hidden;
                    break;
                case SHOW_ALL:
                    should_hide_width = false;
                    break;
            }

            // Apply search filter
            bool matches_search_width = str_contains_insensitive(item->display_name, t->search_buffer);

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

    float padding = 50.0f, current_x = padding, row_max_height = 0.0f;

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
                should_hide_render = item->is_hidden || item->done;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Apply search filter
        bool matches_search_render = str_contains_insensitive(item->display_name, t->search_buffer);

        // Skip rendering if hidden or doesn't match search
        if (should_hide_render || !matches_search_render) {
            continue;
        }


        // --- Item Height and Layout ---
        // IMPORTANT: These calculations MUST run every frame regardless of zoom
        // to ensure the grid layout remains stable even when text is hidden.

        // Scale font to sub-size for text measurement
        SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize); // Use macro for CalcTextSize
        ImVec2 text_size = ImGui::CalcTextSize(item->display_name);
        RESET_FONT_SCALE(); // Reset scale

        // Calculate height: Icon bg + main text + padding
        float item_height = 96.0f + text_size.y + 4.0f;

        if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
            current_x = padding;
            current_y += row_max_height;
            row_max_height = 0.0f;
        }

        ImVec2 screen_pos = ImVec2((current_x * t->zoom_level) + t->camera_offset.x,
                                   (current_y * t->zoom_level) + t->camera_offset.y);

        // --- Culling Logic ---
        ImVec2 item_size_on_screen = ImVec2(uniform_item_width * t->zoom_level, item_height * t->zoom_level);
        bool is_visible_on_screen = !(screen_pos.x > io.DisplaySize.x || (screen_pos.x + item_size_on_screen.x) < 0 ||
                                      screen_pos.y > io.DisplaySize.y || (screen_pos.y + item_size_on_screen.y) < 0);


        // --- Rendering Core Logic ---
        if (is_visible_on_screen) {
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
            if (texture_to_draw)
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
                draw_list->AddImage((void *) texture_to_draw, p_min, p_max);
                // --- End Icon Scaling and Centering Logic ---
            }

            // Render Text
            // LOD: Check if zoom level is sufficient for text
            if (t->zoom_level > LOD_TEXT_THRESHOLD) {
                float main_text_size = settings->tracker_font_size;
                ImU32 current_text_color = item->done ? text_color_faded : text_color; // Fade text if done

                // Calculate Y position for the first line of text
                float text_y_pos = screen_pos.y + bg_size.y * t->zoom_level + (4.0f * t->zoom_level);

                // Draw Main Name (centered)
                draw_list->AddText(nullptr, main_text_size * t->zoom_level,
                                   ImVec2(
                                       screen_pos.x + (bg_size.x * t->zoom_level - text_size.x * t->zoom_level) * 0.5f,
                                       text_y_pos), current_text_color,
                                   item->display_name);
            }
            // NO Checkboxes rendered here
        } // End if (is_visible_on_screen)

        // --- Update Layout Position ---
        current_x += uniform_item_width; // Use the final uniform_item_width directly
        row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
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
                should_hide_based_on_mode = item->is_hidden || is_item_considered_complete;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_based_on_mode = item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_based_on_mode = false;
                break;
        }

        // Apply Search Filter for counting
        bool matches_search = str_contains_insensitive(item->display_name, t->search_buffer);

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
                should_hide_render = item->is_hidden || item->done;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Combine hiding and search filter
        if (!should_hide_render && str_contains_insensitive(item->display_name, t->search_buffer)) {
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
    render_section_separator(t, settings, current_y, section_title, text_color,
                             completed_count, total_visible_count, -1, -1);


    // --- Calculate Uniform Item Width (based on items that will be rendered) ---
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
                    should_hide_width = item->is_hidden || item->done;
                    break;
                case HIDE_ONLY_TEMPLATE_HIDDEN:
                    should_hide_width = item->is_hidden;
                    break;
                case SHOW_ALL:
                    should_hide_width = false;
                    break;
            }

            // Apply search filter
            bool matches_search_width = str_contains_insensitive(item->display_name, t->search_buffer);

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


    float padding = 50.0f, current_x = padding, row_max_height = 0.0f;

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
                should_hide_render = item->is_hidden || item->done;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = item->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Apply search filter
        bool matches_search_render = str_contains_insensitive(item->display_name, t->search_buffer);

        // Skip rendering if hidden or doesn't match search
        if (should_hide_render || !matches_search_render) {
            continue;
        }


        // --- Item Height and Layout ---
        char progress_text[32] = "";
        if (item->goal > 0) snprintf(progress_text, sizeof(progress_text), "(%d / %d)", item->progress, item->goal);
            // For infinite counters, only show progress if not manually overridden/done
        else if (item->goal == -1 && !item->done)
            snprintf(progress_text, sizeof(progress_text), "(%d)", item->progress);

        // Scale font to sub-size for progress text measurement
        SET_FONT_SCALE(settings->tracker_font_size, t->tracker_font->LegacySize);
        ImVec2 text_size = ImGui::CalcTextSize(item->display_name); // Uses main tracker_font_size
        RESET_FONT_SCALE();

        // Scale font to sub-size for progress text measurement
        SET_FONT_SCALE(settings->tracker_sub_font_size, t->tracker_font->LegacySize);
        ImVec2 progress_text_size = ImGui::CalcTextSize(progress_text);
        RESET_FONT_SCALE();

        // Calculate height: Icon bg + main text + progress text (if any) + padding
        float item_height = 96.0f + text_size.y + 4.0f + (progress_text[0] != '\0' ? progress_text_size.y + 4.0f : 0);

        if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
            current_x = padding;
            current_y += row_max_height;
            row_max_height = 0.0f;
        }

        ImVec2 screen_pos = ImVec2((current_x * t->zoom_level) + t->camera_offset.x,
                                   (current_y * t->zoom_level) + t->camera_offset.y);

        // --- Culling Logic ---
        ImVec2 item_size_on_screen = ImVec2(uniform_item_width * t->zoom_level, item_height * t->zoom_level);
        bool is_visible_on_screen = !(screen_pos.x > io.DisplaySize.x || (screen_pos.x + item_size_on_screen.x) < 0 ||
                                      screen_pos.y > io.DisplaySize.y || (screen_pos.y + item_size_on_screen.y) < 0);


        // --- Rendering Core Logic ---
        if (is_visible_on_screen) {
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

            // Render Background
            if (texture_to_draw)
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
                draw_list->AddImage((void *) texture_to_draw, p_min, p_max);
                // --- End Icon Scaling and Centering Logic ---
            }

            // Render Text (Name and Progress)
            float main_font_size = settings->tracker_font_size;
            float sub_font_size = settings->tracker_sub_font_size; // Max 32 pt
            ImU32 current_text_color = item->done ? text_color_faded : text_color; // Fade if done

            // Calculate Y position for the first line of text
            float text_y_pos = screen_pos.y + bg_size.y * t->zoom_level + (4.0f * t->zoom_level);

            // Draw Main Name (centered)
            draw_list->AddText(nullptr, main_font_size * t->zoom_level,
                               ImVec2(screen_pos.x + (bg_size.x * t->zoom_level - text_size.x * t->zoom_level) * 0.5f,
                                      text_y_pos), current_text_color,
                               item->display_name);

            // Draw Progress Text below main name (if applicable, centered)
            if (progress_text[0] != '\0') {
                text_y_pos += text_size.y * t->zoom_level + 4.0f * t->zoom_level; // Move Y down
                draw_list->AddText(nullptr, sub_font_size * t->zoom_level,
                                   ImVec2(
                                       screen_pos.x + (bg_size.x * t->zoom_level - progress_text_size.x * t->zoom_level)
                                       * 0.5f,
                                       text_y_pos), current_text_color,
                                   progress_text);
            }

            // --- Checkbox logic for manual override ---
            // Allow override for simple toggles (goal <= 0) OR infinite counters (goal == -1)
            bool can_be_overridden = (item->goal <= 0 || item->goal == -1);
            if (can_be_overridden) {
                ImVec2 check_pos = ImVec2(screen_pos.x + 4 * t->zoom_level, screen_pos.y + 4 * t->zoom_level);
                // Top-right corner
                ImVec2 check_size = ImVec2(20 * t->zoom_level, 20 * t->zoom_level);
                ImRect checkbox_rect(check_pos, ImVec2(check_pos.x + check_size.x, check_pos.y + check_size.y));

                bool is_hovered = ImGui::IsMouseHoveringRect(checkbox_rect.Min, checkbox_rect.Max);
                ImU32 check_fill_color = is_hovered ? checkbox_hover_color : checkbox_fill_color;
                draw_list->AddRectFilled(checkbox_rect.Min, checkbox_rect.Max, check_fill_color, 3.0f * t->zoom_level);
                draw_list->AddRect(checkbox_rect.Min, checkbox_rect.Max, text_color,
                                   3.0f * t->zoom_level);

                if (item->done) {
                    // Draw checkmark if done
                    ImVec2 p1 = ImVec2(check_pos.x + 5 * t->zoom_level, check_pos.y + 10 * t->zoom_level);
                    ImVec2 p2 = ImVec2(check_pos.x + 9 * t->zoom_level, check_pos.y + 15 * t->zoom_level);
                    ImVec2 p3 = ImVec2(check_pos.x + 15 * t->zoom_level, check_pos.y + 6 * t->zoom_level);
                    draw_list->AddLine(p1, p2, checkmark_color, 2.0f * t->zoom_level);
                    draw_list->AddLine(p2, p3, checkmark_color, 2.0f * t->zoom_level);
                }

                // Handle click interaction
                if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    item->done = !item->done; // Toggle done state
                    // Update progress for consistency (1 if done, 0 otherwise) for simple toggles/overridden counters
                    if (item->goal <= 0 || item->goal == -1) {
                        item->progress = item->done ? 1 : 0;
                    }
                    item->is_manually_completed = true; // Mark as manually changed (though less relevant here)
                    settings_save(settings, t->template_data, SAVE_CONTEXT_ALL); // Save change immediately
                    SDL_SetAtomicInt(&g_needs_update, 1); // Trigger data refresh
                    SDL_SetAtomicInt(&g_game_data_changed, 1); // Indicate change wasn't from game file
                }
            } // End if (can_be_overridden)
        } // End if (is_visible_on_screen)

        // --- Update Layout Position ---
        current_x += uniform_item_width; // Use the final uniform_item_width directly
        row_max_height = fmaxf(row_max_height, item_height + vertical_spacing);
    }
    // --- End Rendering Loop ---

    // Update vertical position for the next section
    current_y += row_max_height;
}

/**
 * @brief Renders the Multi-Stage Goals section.
 * Calculates and displays completion counters based on visibility settings.
 */
static void render_multistage_goals_section(Tracker *t, const AppSettings *settings, float &current_y,
                                            const char *section_title) {
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
                should_hide_based_on_mode = goal->is_hidden || is_goal_considered_complete;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_based_on_mode = goal->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_based_on_mode = false;
                break;
        }

        // Apply Search Filter for counting (check main name AND active stage)
        SubGoal *active_stage_count = goal->stages[goal->current_stage];
        bool name_matches_search = str_contains_insensitive(goal->display_name, t->search_buffer);
        bool stage_matches_search = str_contains_insensitive(active_stage_count->display_text, t->search_buffer);
        bool goal_matches_search = name_matches_search || stage_matches_search;

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
                should_hide_render = goal->is_hidden || is_done_render;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = goal->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Apply search filter (check main name and current stage text)
        SubGoal *active_stage_render = goal->stages[goal->current_stage];
        bool name_matches_render = str_contains_insensitive(goal->display_name, t->search_buffer);
        bool stage_matches_render = str_contains_insensitive(active_stage_render->display_text, t->search_buffer);

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
    render_section_separator(t, settings, current_y, section_title, text_color,
                             completed_count, total_visible_count,
                             completed_sub_count, total_visible_sub_count); // Pass stage counts


    // --- Calculate Uniform Item Width (based on items that will be rendered) ---
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
                    should_hide_width = goal->is_hidden || is_done_width;
                    break;
                case HIDE_ONLY_TEMPLATE_HIDDEN:
                    should_hide_width = goal->is_hidden;
                    break;
                case SHOW_ALL:
                    should_hide_width = false;
                    break;
            }

            // Apply search filter for width calculation
            SubGoal *active_stage_width = goal->stages[goal->current_stage];
            bool name_matches_width = str_contains_insensitive(goal->display_name, t->search_buffer);
            bool stage_matches_width = str_contains_insensitive(active_stage_width->display_text, t->search_buffer);

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


    float padding = 50.0f, current_x = padding, row_max_height = 0.0f;
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
                should_hide_render = goal->is_hidden || is_done_render;
                break;
            case HIDE_ONLY_TEMPLATE_HIDDEN:
                should_hide_render = goal->is_hidden;
                break;
            case SHOW_ALL:
                should_hide_render = false;
                break;
        }

        // Apply search filter
        SubGoal *active_stage_render = goal->stages[goal->current_stage];
        bool name_matches_render = str_contains_insensitive(goal->display_name, t->search_buffer);
        bool stage_matches_render = str_contains_insensitive(active_stage_render->display_text, t->search_buffer);

        // Skip rendering if hidden or doesn't match search
        if (should_hide_render || (!name_matches_render && !stage_matches_render)) {
            continue;
        }


        // --- Item Height and Layout ---
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

        // Calculate height: Icon bg + main name + stage text + padding
        float item_height = 96.0f + text_size.y + 4.0f + stage_text_size.y + 4.0f;

        if (current_x > padding && (current_x + uniform_item_width) > wrapping_width - padding) {
            current_x = padding;
            current_y += row_max_height;
            row_max_height = 0.0f;
        }

        ImVec2 screen_pos = ImVec2((current_x * t->zoom_level) + t->camera_offset.x,
                                   (current_y * t->zoom_level) + t->camera_offset.y);

        // --- Culling Logic ---
        ImVec2 item_size_on_screen = ImVec2(uniform_item_width * t->zoom_level, item_height * t->zoom_level);
        bool is_visible_on_screen = !(screen_pos.x > io.DisplaySize.x || (screen_pos.x + item_size_on_screen.x) < 0 ||
                                      screen_pos.y > io.DisplaySize.y || (screen_pos.y + item_size_on_screen.y) < 0);


        // --- Rendering Core Logic ---
        if (is_visible_on_screen) {
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
            if (texture_to_draw)
                draw_list->AddImage((void *) texture_to_draw, screen_pos,
                                    ImVec2(screen_pos.x + bg_size.x * t->zoom_level,
                                           screen_pos.y + bg_size.y * t->zoom_level));

            // Render Icon (Animated or Static)
            // --- Start GIF Frame Selection Logic ---
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
                    texture_to_draw = goal->anim_texture->frames[current_frame];
                } else {
                    // Fallback if no timing info
                    texture_to_draw = goal->anim_texture->frames[0];
                }
            } else if (goal->texture) {
                // Static texture
                texture_to_draw = goal->texture;
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
                draw_list->AddImage((void *) texture_to_draw, p_min, p_max);
                // --- End Icon Scaling and Centering Logic ---
            }

            // Render Text (Main Name and Current Stage Text)
            float main_font_size = settings->tracker_font_size;
            float sub_font_size = settings->tracker_sub_font_size;
            ImU32 current_text_color = is_done_render ? text_color_faded : text_color; // Fade if done

            // Calculate Y position for the first line of text
            float text_y_pos = screen_pos.y + bg_size.y * t->zoom_level + (4.0f * t->zoom_level);

            // Draw Main Name (centered)
            draw_list->AddText(nullptr, main_font_size * t->zoom_level,
                               ImVec2(screen_pos.x + (bg_size.x * t->zoom_level - text_size.x * t->zoom_level) * 0.5f,
                                      text_y_pos), current_text_color,
                               goal->display_name);

            // Draw Current Stage Text (uses sub_font_size)
            text_y_pos += text_size.y * t->zoom_level + 4.0f * t->zoom_level; // Move Y down
            draw_list->AddText(nullptr, sub_font_size * t->zoom_level,
                               ImVec2(
                                   screen_pos.x + (bg_size.x * t->zoom_level - stage_text_size.x * t->zoom_level) *
                                   0.5f,
                                   text_y_pos), current_text_color,
                               stage_text); // Use formatted stage text
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

    // Pan and zoom logic
    if (ImGui::IsWindowHovered()) {
        if (io.MouseWheel != 0) {
            ImVec2 mouse_pos_in_window = ImGui::GetMousePos();
            ImVec2 mouse_pos_before_zoom = ImVec2((mouse_pos_in_window.x - t->camera_offset.x) / t->zoom_level,
                                                  (mouse_pos_in_window.y - t->camera_offset.y) / t->zoom_level);
            float old_zoom = t->zoom_level;
            t->zoom_level += io.MouseWheel * 0.1f * t->zoom_level;
            if (t->zoom_level < 0.1f) t->zoom_level = 0.1f; // Adjust max zoom out, originally 0.1f
            if (t->zoom_level > 10.0f) t->zoom_level = 10.0f; // Adjust max zoom in
            t->camera_offset.x += (mouse_pos_before_zoom.x * (old_zoom - t->zoom_level));
            t->camera_offset.y += (mouse_pos_before_zoom.y * (old_zoom - t->zoom_level));
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            t->camera_offset.x += io.MouseDelta.x;
            t->camera_offset.y += io.MouseDelta.y;
        }
    }

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

    //  Render All Sections in User-Defined Order
    for (int i = 0; i < SECTION_COUNT; ++i) {
        auto section_id = (TrackerSection) settings->section_order[i];
        switch (section_id) {
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

    // This Begin() call draws the window frame and title bar using the styles we just pushed.
    ImGui::Begin("Info | ESC: Settings | Pan: RMB/MMB Drag | Zoom: Wheel | Click: LMB | Move Win: LMB Drag", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    // --- Set Font Scale for Info Bar CONTENT ---
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
    format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));

    // Check if the run is 100% complete.
    bool is_run_complete = t->template_data->advancements_completed_count >= t->template_data->advancement_count &&
                           t->template_data->overall_progress_percentage >= 100.0f;

    if (is_run_complete) {
        snprintf(info_buffer, sizeof(info_buffer),
                 "*** RUN COMPLETE! *** |   Final Time: %s",
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

        // Start with the world name and run details
        if (settings->category_display_name[0] != '\0') {
            snprintf(info_buffer, sizeof(info_buffer), "%s  |  %s - %s",
                     t->world_name,
                     settings->display_version_str,
                     settings->category_display_name);
        } else {
            snprintf(info_buffer, sizeof(info_buffer), "%s  |  %s",
                     t->world_name,
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
        snprintf(temp_chunk, sizeof(temp_chunk), "  |  %s IGT  |  Upd: %s", formatted_time, formatted_update_time);
        strncat(info_buffer, temp_chunk, sizeof(info_buffer) - strlen(info_buffer) - 1);
    }

    // This text will now be drawn using the user's selected color.
    ImGui::TextUnformatted(info_buffer);

    // Add a tooltip to the info window to explain the progress metrics.
    if (ImGui::IsWindowHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);

        ImGui::TextUnformatted("Progress Text");

        ImGui::BulletText("World: Shows the current world name.");
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

    ImVec2 reset_text_size = ImGui::CalcTextSize("Reset Layout");
    float reset_checkbox_width = frame_height + style.ItemInnerSpacing.x + reset_text_size.x + frame_padding_x * 0.5f;

    ImVec2 notes_text_size = ImGui::CalcTextSize("Notes");
    float notes_checkbox_width = frame_height + style.ItemInnerSpacing.x + notes_text_size.x + frame_padding_x * 0.5f;

    // Calculate total width using ImGui's ItemSpacing
    // 6 elements = 5 gaps between them
    float controls_total_width = clear_button_width + button_padding_x +
                                 search_box_width + button_padding_x +
                                 lock_checkbox_width + button_padding_x +
                                 reset_checkbox_width + button_padding_x +
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
            "Search for goals by name (case-insensitive). You can also use Ctrl + F (or Cmd + F on macOS).\n"
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
            "Multi-Stage Goals: Shows the goal if its main title or the text of its currently\n"
            "active stage matches the search term.");

        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
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

    // "Reset Layout" checkbox (acts as a button), it turns off the lock layout
    static bool reset_dummy = false;
    if (ImGui::Checkbox("Reset Layout", &reset_dummy)) {
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

    // Add the "Notes" checkbox
    ImGui::Checkbox("Notes", &t->notes_window_open);
    if (ImGui::IsItemHovered()) {
        char notes_tooltip_buffer[1024];
        snprintf(notes_tooltip_buffer, sizeof(notes_tooltip_buffer),
                 "Notes Window\n"
                 "--------------------------------\n"
                 "Toggles a persistent text editor for keeping notes. The system has two modes, configurable inside the window:\n\n"
                 " - Per-World (Default): Notes are saved for each world individually. The last 32 worlds are remembered.\n"
                 " - Per-Template: Notes are shared for the currently loaded template permanently.\n\n"
                 "--------------------------------\n"
                 "The window's size and position are remembered across sessions.\n"
                 "Anything you type is immediately saved.\n"
                 "Hotkeys are disabled while typing in the notes window. The maximum note size is 64KB.");
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
            // Per-World Toggle
            if (ImGui::Checkbox("Per-World Notes", &settings->per_world_notes)) {
                // When toggled, immediately update the path and reload the notes
                tracker_update_notes_path(t, settings);
                tracker_load_notes(t, settings);
                settings_save(settings, nullptr, SAVE_CONTEXT_ALL); // Save the setting change
            }
            if (ImGui::IsItemHovered()) {
                char per_world_notes_tooltip_buffer[512];
                snprintf(per_world_notes_tooltip_buffer, sizeof(per_world_notes_tooltip_buffer),
                         "When enabled, notes are saved for each world individually.\n"
                         "When disabled, notes are shared for the current template.");
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

    // Pop the font at the very end, so everything inside TrackerMap uses it.
    if (t->tracker_font) {
        ImGui::PopFont();
    }


    ImGui::End(); // End TrackerMap window
}


// -------------------------------------------- TRACKER RENDERING END --------------------------------------------


void tracker_reinit_template(Tracker *t, AppSettings *settings) {
    if (!t) return;

    log_message(LOG_INFO, "[TRACKER] Re-initializing template...\n");


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

    if (get_saves_path(t->saves_path, MAX_PATH_LENGTH, settings->path_mode, settings->manual_saves_path)) {
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
    } else {
        log_message(LOG_ERROR, "[TRACKER] CRITICAL: Failed to get saves path.\n");

        // Ensure paths are empty
        t->saves_path[0] = '\0';
        t->world_name[0] = '\0';
        t->advancements_path[0] = '\0';
        t->stats_path[0] = '\0';
        t->unlocks_path[0] = '\0';
    }
}

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

    // Parse the 3 main categories from the template
    cJSON *advancements_json = cJSON_GetObjectItem(template_json, "advancements");
    cJSON *stats_json = cJSON_GetObjectItem(template_json, "stats");
    cJSON *unlocks_json = cJSON_GetObjectItem(template_json, "unlocks");
    cJSON *custom_json = cJSON_GetObjectItem(template_json, "custom"); // Custom goals, manually checked of by user
    cJSON *multi_stage_goals_json = cJSON_GetObjectItem(template_json, "multi_stage_goals");


    MC_Version version = settings_get_version_from_string(settings->version_str);
    // Parse the 5 main categories
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
                                    &t->template_data->unlock_count, "unlock.", settings);

    // Parse "custom." prefix for custom goals
    tracker_parse_simple_trackables(t, custom_json, lang_json, &t->template_data->custom_goals,
                                    &t->template_data->custom_goal_count, "custom.", settings);

    tracker_parse_multi_stage_goals(t, multi_stage_goals_json, lang_json,
                                    &t->template_data->multi_stage_goals,
                                    &t->template_data->multi_stage_goal_count, settings);


    // Detect and flag criteria that are shared between multiple advancements
    tracker_detect_shared_icons(t, settings);

    // Automatically synchronize settings.json with the newly loaded template
    cJSON *settings_root = cJSON_from_file(get_settings_file_path());
    if (!settings_root) settings_root = cJSON_CreateObject();

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
    cJSON_ReplaceItemInObject(settings_root, "custom_progress", new_custom_progress);

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
    cJSON_ReplaceItemInObject(settings_root, "stat_progress_override", new_stat_override);

    // Sync hotkeys based on their order in the list, not by name
    cJSON *old_hotkeys_array = cJSON_GetObjectItem(settings_root, "hotkeys");
    cJSON *new_hotkeys_array = cJSON_CreateArray();

    int counter_index = 0; // This will track their order in the list
    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        TrackableItem *item = t->template_data->custom_goals[i];
        if (item && (item->goal > 0 || item->goal == -1)) {
            // It's a counter
            cJSON *new_hotkey_obj = cJSON_CreateObject();
            // The target is always the new counter from the current template
            // put name of counter in target_goal
            cJSON_AddStringToObject(new_hotkey_obj, "target_goal", item->root_name);

            const char *inc_key = "None";
            const char *dec_key = "None";

            // Try to get the hotkey from the OLD settings at the SAME index
            if (cJSON_IsArray(old_hotkeys_array)) {
                cJSON *old_hotkey_item = cJSON_GetArrayItem(old_hotkeys_array, counter_index);
                if (old_hotkey_item) {
                    // If a binding exists at this index, preserve its keys.
                    cJSON *old_inc_key = cJSON_GetObjectItem(old_hotkey_item, "increment_key");
                    cJSON *old_dec_key = cJSON_GetObjectItem(old_hotkey_item, "decrement_key");
                    if (old_inc_key) inc_key = old_inc_key->valuestring;
                    if (old_dec_key) dec_key = old_dec_key->valuestring;
                }
            }

            cJSON_AddStringToObject(new_hotkey_obj, "increment_key", inc_key);
            cJSON_AddStringToObject(new_hotkey_obj, "decrement_key", dec_key);
            cJSON_AddItemToArray(new_hotkeys_array, new_hotkey_obj);

            counter_index++; // Move to the next counter index
        }
    }
    cJSON_ReplaceItemInObject(settings_root, "hotkeys", new_hotkeys_array);

    // Write the synchronized settings back to the file
    FILE *file = fopen(get_settings_file_path(), "w");
    if (file) {
        char *json_str = cJSON_Print(settings_root);
        fputs(json_str, file);
        fclose(file);
        free(json_str);
        json_str = nullptr;
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


void tracker_free(Tracker **tracker, const AppSettings *settings) {
    (void) settings;

    if (tracker && *tracker) {
        Tracker *t = *tracker;

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

    format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));

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

    snprintf(title_buffer, sizeof(title_buffer),
             "  Advancely  %s    |    %s    -    %s%s%s    |    %s IGT",
             ADVANCELY_VERSION, t->world_name,
             settings->display_version_str,
             category_chunk, // This contains "    -    Category" or ""
             progress_chunk, formatted_time);

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

    // Format the time to DD:HH:MM:SS.MS
    char formatted_time[128];
    format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time));


    log_message(LOG_INFO, "============================================================\n");
    log_message(LOG_INFO, " World:      %s\n", t->world_name);
    log_message(LOG_INFO, " Version:    %s\n", settings->display_version_str);

    // When category display name isn't empty
    if (settings->category_display_name[0] != '\0') {
        log_message(LOG_INFO, " Category:   %s\n", settings->category_display_name);
    }

    log_message(LOG_INFO, " Play Time:  %s\n", formatted_time);
    log_message(LOG_INFO, "============================================================\n");


    // Check if the run is completed, check both advancement and overall progress
    if (t->template_data->advancements_completed_count >= t->template_data->advancement_count && t->template_data->
        overall_progress_percentage >= 100.0f) {
        log_message(LOG_INFO, "\n                  *** RUN COMPLETE! ***\n\n");
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
