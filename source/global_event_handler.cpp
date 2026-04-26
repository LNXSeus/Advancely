// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 26.06.2025.
//


#include "global_event_handler.h"
#include "tracker.h"
#include "overlay.h"
#include "settings.h"
#include "settings_utils.h" // For AppSettings
#include "coop_net.h"

#include "imgui_impl_sdl3.h"
#include "logger.h"

void handle_global_events(Tracker *t, Overlay *o, AppSettings *app_settings,
                          bool *is_running, bool *settings_opened, float *deltaTime) {
    // create one event out of tracker->event and overlay->event
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        // TOP LEVEL QUIT when it's not the X on the settings window
        if (event.type == SDL_EVENT_QUIT) {
            // Check for unsaved changes or active lobby before quitting
            CoopNetState quit_net_state = g_coop_ctx ? coop_net_get_state(g_coop_ctx) : COOP_NET_IDLE;
            bool quit_lobby_active = (quit_net_state == COOP_NET_LISTENING || quit_net_state == COOP_NET_CONNECTED
                                      || quit_net_state == COOP_NET_CONNECTING);
            if (t && (t->settings_has_unsaved_changes || t->template_editor_has_unsaved_changes || quit_lobby_active)) {
                t->quit_requested = true;
            } else {
                *is_running = false;
            }
            break;
        }

        // Event-based HOTKEY HANDLING
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat == 0) {
            // Hotkey for UI control (e.g., focusing the search box)
            SDL_Keymod mod_state = SDL_GetModState();

            // Ctrl + F or Cmd + F for search box focus
            // Global hotkey for focusing the search box (Ctrl+F or Cmd+F)
            bool is_ctrl_or_cmd = (mod_state & SDL_KMOD_CTRL) || (mod_state & SDL_KMOD_GUI);

            if (is_ctrl_or_cmd && event.key.scancode == SDL_SCANCODE_F && !ImGui::IsPopupOpen(
                    nullptr, ImGuiPopupFlags_AnyPopup) && t) {
                // TRACKER SEARCH BOX -> only if the template creator is not focused
                if (!t->is_temp_creator_focused) {
                    // if the user is currently typing in another text box (like the settings or notes).
                    t->focus_search_box_requested = true;
                }
            }

            // TEMPLATE CREATOR SEARCH BOX IMPLEMENTED IN TEMP_CREATOR.CPP -> TOP main gui function
            // If any ImGui widget is inactive active (e.g., not typing in a text box), then process hotkeys.
            if (!ImGui::IsAnyItemActive()) {
                // We don't break here; we want other event processing to continue,
                // but we skip the hotkey logic for this specific event.
                // Only trigger on initial key press
                // Defensive check to prevent crash if data is not ready
                // CUSTOM GOAL HOTKEYS
                // Hotkeys don't work when in visual layout editing mode
                // Co-op: Receivers with host-only custom goals cannot use counter hotkeys
                bool rcv_in_lobby = (app_settings->network_mode == NETWORK_RECEIVER &&
                                     g_coop_ctx && coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED);

                // Viewer-is-self gate: hotkeys only mutate state when the dropdown shows
                // your own view or "All Players". Viewing another specific player is read-only.
                // Track own-UUID specifically (excludes All-Players) so HOST_ONLY can yield to it.
                bool view_is_self_or_all = true;
                bool viewing_own_uuid = false;
                if (t && app_settings->network_mode != NETWORK_SINGLEPLAYER) {
                    int sel = t->selected_coop_player_idx;
                    if (sel >= 0 && sel < app_settings->coop_player_count) {
                        const char *view_uuid = app_settings->coop_players[sel].uuid;
                        viewing_own_uuid = (app_settings->local_player.uuid[0] != '\0' &&
                                            strcmp(view_uuid, app_settings->local_player.uuid) == 0);
                        view_is_self_or_all = viewing_own_uuid;
                    }
                }

                bool coop_hotkeys_blocked = (rcv_in_lobby &&
                                             app_settings->coop_custom_goal_mode == COOP_CUSTOM_HOST_ONLY &&
                                             !viewing_own_uuid);

                if (t && t->template_data && t->template_data->custom_goals &&
                    !t->is_visual_layout_editing && !coop_hotkeys_blocked && view_is_self_or_all) {
                    for (int i = 0; i < app_settings->hotkey_count; i++) {
                        HotkeyBinding *hb = &app_settings->hotkeys[i];
                        TrackableItem *target_goal = nullptr;

                        // Find the goal this hotkey is bound to
                        for (int j = 0; j < t->template_data->custom_goal_count; j++) {
                            if (strcmp(t->template_data->custom_goals[j]->root_name, hb->target_goal) == 0) {
                                target_goal = t->template_data->custom_goals[j];
                                break;
                            }
                        }
                        if (!target_goal) continue;

                        // Convert key names from settings to scancodes for comparison
                        SDL_Scancode inc_scancode = SDL_GetScancodeFromName(hb->increment_key);
                        SDL_Scancode dec_scancode = SDL_GetScancodeFromName(hb->decrement_key);

                        // Check if the pressed key matches a hotkey
                        int mod_action = -1;
                        if (event.key.scancode == inc_scancode) {
                            mod_action = COOP_MOD_INCREMENT;
                        } else if (event.key.scancode == dec_scancode) {
                            mod_action = COOP_MOD_DECREMENT;
                        }

                        if (mod_action >= 0) {
                            // Co-op Receiver: send modification to host (any-player mode, or self-view under host-only).
                            if (rcv_in_lobby &&
                                (app_settings->coop_custom_goal_mode == COOP_CUSTOM_ANY_PLAYER ||
                                 viewing_own_uuid)) {
                                CoopCustomGoalModMsg mod = {};
                                snprintf(mod.goal_root_name, sizeof(mod.goal_root_name),
                                         "%s", target_goal->root_name);
                                mod.parent_root_name[0] = '\0';
                                mod.action = mod_action;
                                snprintf(mod.source_uuid, sizeof(mod.source_uuid),
                                         "%s", app_settings->local_player.uuid);
                                coop_net_send_custom_goal_mod(g_coop_ctx, &mod);
                            } else {
                                bool host_in_lobby = (app_settings->network_mode == NETWORK_HOST &&
                                                      g_coop_ctx &&
                                                      coop_net_get_state(g_coop_ctx) == COOP_NET_LISTENING);
                                if (host_in_lobby) {
                                    // Route host's own hotkey through the same per-UUID persistence
                                    // path used for receiver mods, so host's increments only affect
                                    // host's subtree (not the merged view stored in template_data).
                                    CoopCustomGoalModMsg mod = {};
                                    snprintf(mod.goal_root_name, sizeof(mod.goal_root_name),
                                             "%s", target_goal->root_name);
                                    mod.parent_root_name[0] = '\0';
                                    mod.action = mod_action;
                                    snprintf(mod.source_uuid, sizeof(mod.source_uuid),
                                             "%s", app_settings->local_player.uuid);
                                    tracker_apply_coop_mods(t, app_settings, &mod, 1);
                                    SDL_SetAtomicInt(&g_needs_update, 1);
                                } else {
                                    // Singleplayer: direct in-memory mutation + save.
                                    if (mod_action == COOP_MOD_INCREMENT) {
                                        target_goal->progress++;
                                    } else {
                                        target_goal->progress--;
                                    }
                                    if (target_goal->goal > 0) {
                                        target_goal->done = (target_goal->progress >= target_goal->goal);
                                    }
                                    SDL_SetAtomicInt(&g_suppress_settings_watch, 1);
                                    settings_save(app_settings, t->template_data, SAVE_CONTEXT_ALL);
                                    SDL_SetAtomicInt(&g_coop_broadcast_needed, 1);
                                    SDL_SetAtomicInt(&g_game_data_changed, 1);
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }

        // --- Dispatch keyboard/mouse events ---
        if (event.type >= SDL_EVENT_KEY_DOWN && event.type <= SDL_EVENT_MOUSE_WHEEL) {
            if (t && event.key.windowID == SDL_GetWindowID(t->window)) {
                tracker_events(t, &event, is_running, settings_opened);
            } else if (o && event.key.windowID == SDL_GetWindowID(o->window)) {
                overlay_events(o, &event, is_running, deltaTime, app_settings);
            }
        }
        // --- Dispatch window events (move, resize, etc.) ---
        // In-memory rect is kept current so Apply/shutdown can persist it;
        // we intentionally do NOT write to settings.json here - per-move saves
        // would trigger dmon and force a full template re-init + game-file reload,
        // wiping Hermes in-memory state.
        else if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
            if (t && event.window.windowID == SDL_GetWindowID(t->window)) {
                if (event.type == SDL_EVENT_WINDOW_MOVED || event.type == SDL_EVENT_WINDOW_RESIZED) {
                    SDL_GetWindowPosition(t->window, &app_settings->tracker_window.x, &app_settings->tracker_window.y);
                    SDL_GetWindowSize(t->window, &app_settings->tracker_window.w, &app_settings->tracker_window.h);
                }
                tracker_events(t, &event, is_running, settings_opened); // still pass other window events
            } else if (o && event.window.windowID == SDL_GetWindowID(o->window)) {
                if (event.type == SDL_EVENT_WINDOW_MOVED || event.type == SDL_EVENT_WINDOW_RESIZED) {
                    SDL_GetWindowPosition(o->window, &app_settings->overlay_window.x, &app_settings->overlay_window.y);
                    int w, h;
                    SDL_GetWindowSize(o->window, &w, &h);
                    app_settings->overlay_window.w = w;
                    app_settings->overlay_window.h = OVERLAY_FIXED_HEIGHT;

                    if (event.type == SDL_EVENT_WINDOW_RESIZED && h != OVERLAY_FIXED_HEIGHT) {
                        SDL_SetWindowSize(o->window, w, OVERLAY_FIXED_HEIGHT);
                    }
                }
                overlay_events(o, &event, is_running, deltaTime, app_settings);
            }
        }
    }
}
