// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 26.06.2025.
//


#include "global_event_handler.h"
#include "global_hotkeys.h"
#include "tracker.h"
#include "overlay.h"
#include "settings.h"
#include "settings_utils.h" // For AppSettings
#include "coop_net.h"

#include "imgui_impl_sdl3.h"
#include "logger.h"

bool hotkey_apply_counter_action(Tracker *t, AppSettings *app_settings,
                                 const char *target_goal_root, int mod_action) {
    if (!t || !app_settings || !target_goal_root || target_goal_root[0] == '\0') return false;
    if (!t->template_data || !t->template_data->custom_goals) return false;

    // Hotkeys don't work when in visual layout editing mode
    if (t->is_visual_layout_editing) return false;

    // Co-op: Receivers with host-only custom goals cannot use counter hotkeys
    bool rcv_in_lobby = (app_settings->network_mode == NETWORK_RECEIVER &&
                         g_coop_ctx && coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED);

    // Viewer-is-self gate: hotkeys only mutate state when the dropdown shows
    // your own view or "All Players". Viewing another specific player is read-only.
    // Track own-UUID specifically (excludes All-Players) so HOST_ONLY can yield to it.
    bool view_is_self_or_all = true;
    bool viewing_own_uuid = false;
    if (app_settings->network_mode != NETWORK_SINGLEPLAYER) {
        int sel = t->selected_coop_player_idx;
        if (sel >= 0 && sel < app_settings->coop_player_count) {
            const char *view_uuid = app_settings->coop_players[sel].uuid;
            viewing_own_uuid = (app_settings->local_player.uuid[0] != '\0' &&
                                strcmp(view_uuid, app_settings->local_player.uuid) == 0);
            view_is_self_or_all = viewing_own_uuid;
        }
    }
    if (!view_is_self_or_all) return false;

    bool coop_hotkeys_blocked = (rcv_in_lobby &&
                                 app_settings->coop_custom_goal_mode == COOP_CUSTOM_HOST_ONLY &&
                                 !viewing_own_uuid);
    if (coop_hotkeys_blocked) return false;

    // Find the goal this hotkey is bound to
    TrackableItem *target_goal = nullptr;
    for (int j = 0; j < t->template_data->custom_goal_count; j++) {
        if (strcmp(t->template_data->custom_goals[j]->root_name, target_goal_root) == 0) {
            target_goal = t->template_data->custom_goals[j];
            break;
        }
    }
    if (!target_goal) return false;

    // Block increment/decrement on infinite counters (goal == -1) once
    // the user has manually marked the goal complete. The toggle stays
    // independent of progress, but accidentally bumping a "completed"
    // counter is unwanted noise
    if (target_goal->goal == -1 && target_goal->is_manually_completed &&
        mod_action != COOP_MOD_TOGGLE) {
        return false;
    }

    // Co-op Receiver: send modification to host (any-player mode, or self-view under host-only).
    if (rcv_in_lobby &&
        (app_settings->coop_custom_goal_mode == COOP_CUSTOM_ANY_PLAYER || viewing_own_uuid)) {
        CoopCustomGoalModMsg mod = {};
        snprintf(mod.goal_root_name, sizeof(mod.goal_root_name), "%s", target_goal->root_name);
        mod.parent_root_name[0] = '\0';
        mod.action = mod_action;
        snprintf(mod.source_uuid, sizeof(mod.source_uuid), "%s", app_settings->local_player.uuid);
        coop_net_send_custom_goal_mod(g_coop_ctx, &mod);
        // Optimistic in-memory mutation for instant feedback,
        // plus pending-mod registration so the next host
        // STATE_UPDATE doesn't visually revert the increment
        // before the host's echo arrives.
        tracker_apply_mod_to_view(t, &mod);
        tracker_pending_mod_register(mod.parent_root_name, mod.goal_root_name, 2000);
        return true;
    }

    bool host_in_lobby = (app_settings->network_mode == NETWORK_HOST && g_coop_ctx &&
                          coop_net_get_state(g_coop_ctx) == COOP_NET_LISTENING);
    if (host_in_lobby) {
        // Host's own hotkey: optimistic view mutation +
        // queue for batched persistence. Direct settings.json
        // writes per-keypress stalled the UI while panning.
        CoopCustomGoalModMsg mod = {};
        snprintf(mod.goal_root_name, sizeof(mod.goal_root_name), "%s", target_goal->root_name);
        mod.parent_root_name[0] = '\0';
        mod.action = mod_action;
        snprintf(mod.source_uuid, sizeof(mod.source_uuid), "%s", app_settings->local_player.uuid);
        tracker_apply_mod_to_view(t, &mod);
        tracker_queue_host_mod(&mod);
        return true;
    }

    // Singleplayer: direct in-memory mutation + save.
    if (mod_action == COOP_MOD_TOGGLE) {
        // Mirrors clicking the goal on the map. Clearing the flag drops "done" as well; the re-read
        // triggered below puts it back when linked goals still satisfy the goal on their own.
        target_goal->is_manually_completed = !target_goal->is_manually_completed;
        target_goal->done = target_goal->is_manually_completed;
        // For goal == -1 (infinite counter) the running count stays independent of the checkbox.
        if (target_goal->goal != -1) {
            target_goal->progress = target_goal->done ? 1 : 0;
        }
    } else {
        if (mod_action == COOP_MOD_INCREMENT) {
            target_goal->progress++;
        } else {
            target_goal->progress--;
        }
        if (target_goal->goal > 0) {
            target_goal->done = (target_goal->progress >= target_goal->goal);
        }
    }
    SDL_SetAtomicInt(&g_suppress_settings_watch, 1);
    settings_save(app_settings, t->template_data, SAVE_CONTEXT_ALL);
    SDL_SetAtomicInt(&g_coop_broadcast_needed, 1);
    SDL_SetAtomicInt(&g_game_data_changed, 1);
    return true;
}

// How long a movement key has to be held before it starts repeating, and how often it repeats
// afterwards. The visual editor polls the keyboard itself instead of riding the OS key repeat,
// which only ever repeats the key pressed last and so can never express two directions at once.
#define VISUAL_MOVE_REPEAT_DELAY 0.4f
#define VISUAL_MOVE_REPEAT_INTERVAL 0.04f
// A frame long enough to owe more steps than this had the app stalled; the rest is dropped instead
// of teleporting the selection across the map.
#define VISUAL_MOVE_MAX_STEPS_PER_FRAME 16

// Whether the key bound to an action is currently held down, with the modifiers matching exactly,
// just like app_hotkey_matches does for a key press.
static bool app_hotkey_is_held(const AppSettings *settings, AppHotkeyAction action,
                               const bool *key_state, Uint16 held_mods) {
    if (!settings || !key_state || action < 0 || action >= APP_HOTKEY_COUNT) return false;
    const AppHotkey *hk = &settings->app_hotkeys[action];
    if (hk->key[0] == '\0' || strcmp(hk->key, "None") == 0) return false;
    if (held_mods != hk->mods) return false;

    SDL_Keycode key = SDL_GetKeyFromName(hk->key);
    if (key == SDLK_UNKNOWN) return false;
    SDL_Scancode scancode = SDL_GetScancodeFromKey(key, nullptr);
    if (scancode == SDL_SCANCODE_UNKNOWN) return false;
    return key_state[scancode];
}

void handle_global_events(Tracker *t, Overlay *o, AppSettings *app_settings,
                          bool *is_running, bool *settings_opened, float *deltaTime) {
    // create one event out of tracker->event and overlay->event
    SDL_Event event;

    // One-shot hotkeys for the windows that render later this frame. Clearing them here, before any
    // event is read, means a press nobody picks up is gone by the next frame instead of firing into
    // a window that only opens later.
    if (t) {
        t->editor_save_pressed = false;
        t->editor_revert_pressed = false;
        t->settings_apply_pressed = false;
        t->settings_revert_pressed = false;
        t->editor_next_goal_pressed = false;
        t->editor_prev_goal_pressed = false;
    }

    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        // An OS-level hotkey arrives as a custom SDL event carrying the binding it belongs to.
        // The key never reached any window, so none of the focus-based gates below apply; the
        // action's own gates still do, inside hotkey_apply_counter_action.
        int global_hotkey_index = -1;
        HotkeySlot global_hotkey_slot = HOTKEY_SLOT_INCREMENT;
        if (global_hotkeys_decode_event(&event, &global_hotkey_index, &global_hotkey_slot)) {
            if (global_hotkey_index >= 0 && global_hotkey_index < app_settings->hotkey_count) {
                const HotkeyBinding *hb = &app_settings->hotkeys[global_hotkey_index];
                if (hb->is_global) {
                    int global_mod_action = COOP_MOD_INCREMENT;
                    if (global_hotkey_slot == HOTKEY_SLOT_DECREMENT) global_mod_action = COOP_MOD_DECREMENT;
                    else if (global_hotkey_slot == HOTKEY_SLOT_TOGGLE) global_mod_action = COOP_MOD_TOGGLE;
                    hotkey_apply_counter_action(t, app_settings, hb->target_goal, global_mod_action);
                }
            }
            continue;
        }

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

        // Hotkey capture: while the settings dialog is waiting for a binding,
        // consume the next non-modifier key press and report its scancode.
        int capture_armed = SDL_GetAtomicInt(&g_hotkey_capture_armed);
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat == 0 && capture_armed != 0) {
            SDL_Scancode sc = event.key.scancode;
            // Ignore pure modifiers so users can hold shift/ctrl without binding them.
            bool is_pure_modifier = (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT ||
                                     sc == SDL_SCANCODE_LCTRL || sc == SDL_SCANCODE_RCTRL ||
                                     sc == SDL_SCANCODE_LALT || sc == SDL_SCANCODE_RALT ||
                                     sc == SDL_SCANCODE_LGUI || sc == SDL_SCANCODE_RGUI);
            if (!is_pure_modifier) {
                // Mode 2 is the Advancely-shortcut capture, where Delete and Backspace have to stay
                // bindable (Delete is a default binding there), so only Escape clears a row.
                bool clears_binding = (sc == SDL_SCANCODE_ESCAPE) ||
                                      (capture_armed != 2 && (sc == SDL_SCANCODE_BACKSPACE ||
                                                              sc == SDL_SCANCODE_DELETE));
                if (clears_binding) {
                    SDL_SetAtomicInt(&g_hotkey_captured_scancode, 0); // Clear to "None"
                    SDL_SetAtomicInt(&g_hotkey_captured_keycode, 0);
                    SDL_SetAtomicInt(&g_hotkey_captured_mods, HOTKEY_MOD_NONE);
                } else {
                    SDL_SetAtomicInt(&g_hotkey_captured_scancode, (int) sc);
                    SDL_SetAtomicInt(&g_hotkey_captured_keycode, (int) event.key.key);
                    SDL_SetAtomicInt(&g_hotkey_captured_mods,
                                     (int) hotkey_mods_from_sdl(SDL_GetModState()));
                }
                SDL_SetAtomicInt(&g_hotkey_capture_armed, 0);
                continue; // do not let the captured key propagate as a hotkey
            }
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
                if (t && t->template_data && t->template_data->custom_goals) {
                    // Modifiers must match exactly, including the empty set. Anything looser
                    // lets a bare "E" binding swallow Alt+E and Ctrl+E as well, because the
                    // unmodified slot is tested first and would always win.
                    Uint16 held_mods = hotkey_mods_from_sdl(SDL_GetModState());

                    for (int i = 0; i < app_settings->hotkey_count; i++) {
                        HotkeyBinding *hb = &app_settings->hotkeys[i];

                        // A slot the OS is delivering must not fire twice while the tracker happens
                        // to be focused. One whose registration failed (already owned elsewhere, or
                        // no X11 at all) is deliberately still handled here, which is the fallback
                        // the Hotkeys tab promises on that row.
                        bool inc_handled_by_os = hb->is_global &&
                                                 global_hotkeys_slot_is_registered(i, HOTKEY_SLOT_INCREMENT);
                        bool dec_handled_by_os = hb->is_global &&
                                                 global_hotkeys_slot_is_registered(i, HOTKEY_SLOT_DECREMENT);
                        bool tog_handled_by_os = hb->is_global &&
                                                 global_hotkeys_slot_is_registered(i, HOTKEY_SLOT_TOGGLE);

                        // Convert key names from settings to scancodes for comparison
                        SDL_Scancode inc_scancode = SDL_GetScancodeFromName(hb->increment_key);
                        SDL_Scancode dec_scancode = SDL_GetScancodeFromName(hb->decrement_key);
                        SDL_Scancode tog_scancode = SDL_GetScancodeFromName(hb->toggle_key);

                        // Check if the pressed key matches a hotkey
                        int mod_action = -1;
                        if (!inc_handled_by_os && event.key.scancode == inc_scancode &&
                            held_mods == hb->increment_mods) {
                            mod_action = COOP_MOD_INCREMENT;
                        } else if (!dec_handled_by_os && event.key.scancode == dec_scancode &&
                                   held_mods == hb->decrement_mods) {
                            mod_action = COOP_MOD_DECREMENT;
                        } else if (!tog_handled_by_os && event.key.scancode == tog_scancode &&
                                   held_mods == hb->toggle_mods) {
                            mod_action = COOP_MOD_TOGGLE;
                        }

                        if (mod_action >= 0 &&
                            hotkey_apply_counter_action(t, app_settings, hb->target_goal, mod_action)) {
                            break;
                        }
                    }
                }
            }
        }

        // --- Advancely's own shortcuts ---
        // Key repeats are deliberately let through: holding a movement key keeps moving the visual
        // selection. Popups and active widgets swallow them, so typing a "w" into a text box never
        // moves anything.
        if (event.type == SDL_EVENT_KEY_DOWN && t && t->window &&
            event.key.windowID == SDL_GetWindowID(t->window) &&
            !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
            // The keycode, not the scancode: these bindings follow the keycap, so Ctrl+Z stays on
            // the key labeled Z no matter the keyboard layout.
            SDL_Keycode key = event.key.key;
            Uint16 app_mods = hotkey_mods_from_sdl(event.key.mod);

            // Save, apply and revert. Which window acts on them is decided where they are consumed,
            // by the same focus and enabled checks the buttons themselves use.
            if (event.key.repeat == 0) {
                if (app_hotkey_matches(app_settings, APP_HOTKEY_EDITOR_SAVE, key, app_mods)) {
                    t->editor_save_pressed = true;
                }
                if (app_hotkey_matches(app_settings, APP_HOTKEY_EDITOR_REVERT, key, app_mods)) {
                    t->editor_revert_pressed = true;
                }
                if (app_hotkey_matches(app_settings, APP_HOTKEY_SETTINGS_APPLY, key, app_mods)) {
                    t->settings_apply_pressed = true;
                }
                if (app_hotkey_matches(app_settings, APP_HOTKEY_SETTINGS_REVERT, key, app_mods)) {
                    t->settings_revert_pressed = true;
                }
            }

            // Editor list navigation repeats, so holding the key walks through the list. It belongs
            // to the template editor alone, so unlike the others it is gated on its focus here.
            if (t->is_temp_creator_focused) {
                if (app_hotkey_matches(app_settings, APP_HOTKEY_EDITOR_NEXT_GOAL, key, app_mods)) {
                    t->editor_next_goal_pressed = true;
                }
                if (app_hotkey_matches(app_settings, APP_HOTKEY_EDITOR_PREV_GOAL, key, app_mods)) {
                    t->editor_prev_goal_pressed = true;
                }
            }

            // Template editing is off limits during a co-op session, which is why the Settings button
            // that opens the editor is disabled then. These two hotkeys open it as well, so they obey
            // the same rule. Only opening is blocked: closing the editor or leaving the visual editor
            // stays possible whatever the lobby is doing.
            CoopNetState app_hotkey_net_state = g_coop_ctx ? coop_net_get_state(g_coop_ctx) : COOP_NET_IDLE;
            bool coop_session_active = (app_hotkey_net_state == COOP_NET_LISTENING ||
                                        app_hotkey_net_state == COOP_NET_CONNECTED ||
                                        app_hotkey_net_state == COOP_NET_CONNECTING);

            if (event.key.repeat == 0) {
                if (app_hotkey_matches(app_settings, APP_HOTKEY_TOGGLE_VISUAL_EDITING, key, app_mods)) {
                    if (!coop_session_active || t->is_visual_layout_editing) {
                        // A few frames of grace so the request survives the editor window opening.
                        t->toggle_visual_editing_request_ttl = 5;
                    }
                } else if (app_hotkey_matches(app_settings, APP_HOTKEY_TOGGLE_TEMPLATE_EDITOR, key, app_mods)) {
                    // Closing follows the same rule as the editor's close button, which is hidden
                    // while the visual editor runs or the template has unsaved changes. The hotkey
                    // must not be a way around a window that deliberately has no X.
                    bool editor_close_blocked = t->is_visual_layout_editing ||
                                                t->template_editor_has_unsaved_changes;
                    if (t->temp_creator_window_open) {
                        if (!editor_close_blocked) t->temp_creator_window_open = false;
                    } else if (!coop_session_active) {
                        t->temp_creator_window_open = true;
                    }
                } else if (app_hotkey_matches(app_settings, APP_HOTKEY_TOGGLE_NOTES, key, app_mods)) {
                    t->notes_window_open = !t->notes_window_open;
                }
            }

            // The movement keys themselves are not handled here: they are polled once per frame
            // below, so several directions can be held at the same time.
            if (t->is_visual_layout_editing) {
                if (event.key.repeat == 0) {
                    if (app_hotkey_matches(app_settings, APP_HOTKEY_TOGGLE_LAYOUT_HIDDEN, key, app_mods)) {
                        t->visual_toggle_layout_hidden_pressed = true;
                    }
                    if (app_hotkey_matches(app_settings, APP_HOTKEY_TOGGLE_GOAL_HIDDEN, key, app_mods)) {
                        t->visual_toggle_goal_hidden_pressed = true;
                    }
                    if (app_hotkey_matches(app_settings, APP_HOTKEY_DELETE_SELECTION, key, app_mods)) {
                        t->visual_delete_pressed = true;
                    }
                    if (app_hotkey_matches(app_settings, APP_HOTKEY_COPY_SELECTION, key, app_mods)) {
                        t->visual_copy_pressed = true;
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
                    app_settings->overlay_window.h = o->layout_height;

                    if (event.type == SDL_EVENT_WINDOW_RESIZED && h != o->layout_height) {
                        SDL_SetWindowSize(o->window, w, o->layout_height);
                    }
                }
                overlay_events(o, &event, is_running, deltaTime, app_settings);
            }
        }
    }

    // --- Visual layout editor movement, polled once per frame ---
    // Reading the keyboard state instead of the key events lets several directions be held at the
    // same time, so Up and Right together move diagonally, as do W and D or whatever else the
    // Hotkeys tab has them bound to. Each direction keeps its own repeat timer, which is why a
    // second key pressed later still starts moving immediately.
    {
        static float s_visual_move_timers[APP_HOTKEY_COUNT] = {};
        static bool s_visual_move_held[APP_HOTKEY_COUNT] = {};

        struct VisualMoveBinding {
            AppHotkeyAction action;
            float dx, dy;
        };
        static const VisualMoveBinding VISUAL_MOVE_BINDINGS[] = {
            {APP_HOTKEY_NUDGE_LEFT, -1.0f, 0.0f},
            {APP_HOTKEY_NUDGE_RIGHT, 1.0f, 0.0f},
            {APP_HOTKEY_NUDGE_UP, 0.0f, -1.0f},
            {APP_HOTKEY_NUDGE_DOWN, 0.0f, 1.0f},
            {APP_HOTKEY_MOVE_LEFT, -10.0f, 0.0f},
            {APP_HOTKEY_MOVE_RIGHT, 10.0f, 0.0f},
            {APP_HOTKEY_MOVE_UP, 0.0f, -10.0f},
            {APP_HOTKEY_MOVE_DOWN, 0.0f, 10.0f}
        };

        // The same gates the key events go through: the tracker window focused, an editing session
        // running, and nothing typing-related in the way. A settings row waiting for a binding also
        // stops the poll, so capturing a movement key does not move anything.
        bool movement_allowed = (t && t->window && t->is_visual_layout_editing &&
                                 (SDL_GetWindowFlags(t->window) & SDL_WINDOW_INPUT_FOCUS) &&
                                 SDL_GetAtomicInt(&g_hotkey_capture_armed) == 0 &&
                                 !ImGui::IsAnyItemActive() &&
                                 !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup));

        const bool *key_state = movement_allowed ? SDL_GetKeyboardState(nullptr) : nullptr;
        Uint16 poll_mods = movement_allowed ? hotkey_mods_from_sdl(SDL_GetModState()) : (Uint16) HOTKEY_MOD_NONE;
        float frame_time = deltaTime ? *deltaTime : 0.0f;

        for (const VisualMoveBinding &binding: VISUAL_MOVE_BINDINGS) {
            bool held = movement_allowed &&
                        app_hotkey_is_held(app_settings, binding.action, key_state, poll_mods);
            if (!held) {
                s_visual_move_held[binding.action] = false;
                s_visual_move_timers[binding.action] = 0.0f;
                continue;
            }

            int steps = 0;
            if (!s_visual_move_held[binding.action]) {
                // First frame of the press: one step right away, then wait out the repeat delay.
                s_visual_move_held[binding.action] = true;
                s_visual_move_timers[binding.action] = VISUAL_MOVE_REPEAT_DELAY;
                steps = 1;
            } else {
                s_visual_move_timers[binding.action] -= frame_time;
                while (s_visual_move_timers[binding.action] <= 0.0f && steps < VISUAL_MOVE_MAX_STEPS_PER_FRAME) {
                    s_visual_move_timers[binding.action] += VISUAL_MOVE_REPEAT_INTERVAL;
                    steps++;
                }
                if (s_visual_move_timers[binding.action] <= 0.0f) s_visual_move_timers[binding.action] = VISUAL_MOVE_REPEAT_INTERVAL;
            }

            // Movement accumulates instead of overwriting, so opposite directions cancel out and
            // perpendicular ones combine into a diagonal.
            t->pending_visual_move_x += binding.dx * (float) steps;
            t->pending_visual_move_y += binding.dy * (float) steps;
        }
    }
}
