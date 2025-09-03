//
// Created by Linus on 26.06.2025.
//


#include "global_event_handler.h"
#include "tracker.h"
#include "overlay.h"
#include "settings.h"
#include "settings_utils.h" // For AppSettings

#include "imgui_impl_sdl3.h"

void handle_global_events(Tracker *t, Overlay *o, AppSettings *app_settings,
                          bool *is_running, bool *settings_opened, float *deltaTime) {
    // create one event out of tracker->event and overlay->event
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        // TOP LEVEL QUIT when it's not the X on the settings window
        if (event.type == SDL_EVENT_QUIT) {
            *is_running = false;
            break;
        }

        // Event-based HOTKEY HANDLING
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat == 0) {
            // If any ImGui widget is active (e.g., typing in a text box), do not process hotkeys.
            if (ImGui::IsAnyItemActive()) {
                // We don't break here; we want other event processing to continue,
                // but we skip the hotkey logic for this specific event.
            } else {
                // Only trigger on initial key press
                bool hotkey_triggered = false;

                // Defensive check to prevent crash if data is not ready
                if (t && t->template_data && t->template_data->custom_goals) {
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
                        if (event.key.scancode == inc_scancode) {
                            target_goal->progress++;
                            hotkey_triggered = true;
                        } else if (event.key.scancode == dec_scancode) {
                            target_goal->progress--;
                            hotkey_triggered = true;
                        }

                        if (hotkey_triggered) {
                            settings_save(app_settings, t->template_data, SAVE_CONTEXT_ALL);
                            SDL_SetAtomicInt(&g_needs_update, 1); // Request a data refresh
                            SDL_SetAtomicInt(&g_game_data_changed, 1);
                            break;
                        }
                    }
                }
            }
            // TODO: Spacebar probably only used in overlay_events in overlay.cpp
            // Global hotkey for animation speedup
            if (event.key.scancode == SDL_SCANCODE_SPACE) {
                // Spacebar acts as a toggle
                // app_settings->overlay_animation_speedup = !app_settings->overlay_animation_speedup;
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
        else if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
            bool settings_changed = false;
            if (t && event.window.windowID == SDL_GetWindowID(t->window)) {
                if (event.type == SDL_EVENT_WINDOW_MOVED || event.type == SDL_EVENT_WINDOW_RESIZED) {
                    SDL_GetWindowPosition(t->window, &app_settings->tracker_window.x, &app_settings->tracker_window.y);
                    SDL_GetWindowSize(t->window, &app_settings->tracker_window.w, &app_settings->tracker_window.h);
                    settings_changed = true;
                }
                tracker_events(t, &event, is_running, settings_opened); // still pass other window events
            } else if (o && event.window.windowID == SDL_GetWindowID(o->window)) {
                // o might be nullptr, then skip
                if (event.type == SDL_EVENT_WINDOW_MOVED || event.type == SDL_EVENT_WINDOW_RESIZED) {
                    SDL_GetWindowPosition(o->window, &app_settings->overlay_window.x, &app_settings->overlay_window.y);
                    int w, h;
                    SDL_GetWindowSize(o->window, &w, &h);

                    // Always save the current width and the required fixed height
                    app_settings->overlay_window.w = w;
                    app_settings->overlay_window.h = OVERLAY_FIXED_HEIGHT;
                    settings_changed = true;

                    // If the resize event resulted in a different height, force it back.
                    // This creates a "sticky" height that can't be changed by the user dragging the window frame.
                    if (event.type == SDL_EVENT_WINDOW_RESIZED && h != OVERLAY_FIXED_HEIGHT) {
                        SDL_SetWindowSize(o->window, w, OVERLAY_FIXED_HEIGHT);
                    }
                }
                overlay_events(o, &event, is_running, deltaTime, app_settings);
            }

            if (settings_changed) {
                // Only save window geometry changes if the settings are not being forcibly configured
                if (!g_force_open_settings) {
                    // Save settings, passing nullptr for TemplateData as we only changed window geometry
                    settings_save(app_settings, nullptr, SAVE_CONTEXT_TRACKER_GEOM);
                }
            }
        }
    }
}
