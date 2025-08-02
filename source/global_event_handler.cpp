//
// Created by Linus on 26.06.2025.
//


#include "global_event_handler.h"
#include "tracker.h"
#include "overlay.h"
#include "settings.h"
#include "settings_utils.h" // For AppSettings

#include "imgui_impl_sdl3.h"

void handle_global_events(Tracker *t, Overlay *o, Settings *s, AppSettings *app_settings,
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
        // Important saveguard to check for s to be not nullptr, then settings were opened and can be closed
        if (s != nullptr && event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.key.windowID ==
            SDL_GetWindowID(s->window)) {
            *settings_opened = false;
            continue; // Skip next event
        }

        // Event-based HOTKEY HANDLING
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat == 0) {
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

                    // Check if the pressed key matches a hotkey
                    if (event.key.scancode == hb->increment_scancode) {
                        target_goal->progress++;
                        hotkey_triggered = true;
                    } else if (event.key.scancode == hb->decrement_scancode) {
                        target_goal->progress--;
                        hotkey_triggered = true;
                    }

                    if (hotkey_triggered) {
                        settings_save(app_settings, t->template_data);
                        SDL_SetAtomicInt(&g_needs_update, 1); // Request a data refresh
                        break;
                    }
                }
            }
        }

        // --- Dispatch keyboard/mouse events ---
        if (event.type >= SDL_EVENT_KEY_DOWN && event.type <= SDL_EVENT_MOUSE_WHEEL) {
            if (event.key.windowID == SDL_GetWindowID(t->window)) {
                tracker_events(t, &event, is_running, settings_opened);
            } else if (event.key.windowID == SDL_GetWindowID(o->window)) {
                overlay_events(o, &event, is_running, deltaTime, app_settings);
            } else if (s != nullptr && event.key.windowID == SDL_GetWindowID(s->window)) {
                settings_events(s, &event, is_running, settings_opened);
            }
        }
        // --- Dispatch window events (move, resize, etc.) ---
        else if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
            bool settings_changed = false;
            if (event.window.windowID == SDL_GetWindowID(t->window)) {
                if (event.type == SDL_EVENT_WINDOW_MOVED || event.type == SDL_EVENT_WINDOW_RESIZED) {
                    SDL_GetWindowPosition(t->window, &app_settings->tracker_window.x, &app_settings->tracker_window.y);
                    SDL_GetWindowSize(t->window, &app_settings->tracker_window.w, &app_settings->tracker_window.h);
                    settings_changed = true;
                }
                tracker_events(t, &event, is_running, settings_opened); // still pass other window events
            } else if (event.window.windowID == SDL_GetWindowID(o->window)) {
                if (event.type == SDL_EVENT_WINDOW_MOVED || event.type == SDL_EVENT_WINDOW_RESIZED) {
                    SDL_GetWindowPosition(o->window, &app_settings->overlay_window.x, &app_settings->overlay_window.y);
                    SDL_GetWindowSize(o->window, &app_settings->overlay_window.w, &app_settings->overlay_window.h);
                    settings_changed = true;
                }
                overlay_events(o, &event, is_running, deltaTime, app_settings);
            } else if (s != nullptr && event.window.windowID == SDL_GetWindowID(s->window)) {
                settings_events(s, &event, is_running, settings_opened);
            }

            if (settings_changed) {
                // Save settings, passing nullptr for TemplateData as we only changed window geometry
                settings_save(app_settings, nullptr);
            }
        }
    }
}
