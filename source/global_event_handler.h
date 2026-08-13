// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 26.06.2025.
//

#ifndef GLOBAL_EVENT_HANDLER_H
#define GLOBAL_EVENT_HANDLER_H

#include <SDL3/SDL_atomic.h>

#include "main.h" // For ForceOpenReason enum


#ifdef __cplusplus
extern "C" {
#endif


// Forward declarations
struct Tracker;
struct Overlay;
struct AppSettings;


// This global variable is defined in main.cpp and made accessible here
extern SDL_AtomicInt g_needs_update; // Global flag to signal that an update is needed initially in main.c
extern SDL_AtomicInt g_settings_changed;
extern SDL_AtomicInt g_game_data_changed; // To reset update timer on game data change or completion change (visually)
extern SDL_AtomicInt g_apply_button_clicked; // To signal when overlay window should restart (on apply button click)
extern SDL_AtomicInt g_templates_changed; // To signal that the template list needs to be rescanned.
extern SDL_AtomicInt g_template_preview_changed;
// The template editor handed the tracker an unsaved template (or dropped it again): rebuild the
// template data only, without the settings reload and watcher restart g_settings_changed does.
extern SDL_AtomicInt g_settings_resync_from_app;
// App-initiated app_settings change: Settings window re-seeds editing buffers without a spurious unsaved diff.
extern SDL_AtomicInt g_coop_broadcast_needed; // Custom goal change: broadcast + IPC without full file re-merge
extern SDL_AtomicInt g_suppress_settings_watch; // Suppress dmon settings watcher for app-initiated saves
extern SDL_AtomicInt g_hotkey_capture_armed; // Settings hotkey capture: 1 while waiting for a key press
extern SDL_AtomicInt g_hotkey_captured_scancode;
// Settings hotkey capture: SDL_Scancode written by event handler (0 = cleared/escape)
extern SDL_AtomicInt g_hotkey_captured_mods;
// Settings hotkey capture: HOTKEY_MOD_* bitmask of the modifiers held when the key was pressed
extern SDL_AtomicInt g_hotkey_captured_keycode;
// Settings hotkey capture: SDL_Keycode of the same key press (0 = cleared/escape). The Advancely
// shortcuts bind the keycap rather than the physical key, so they read this instead of the scancode.
extern ForceOpenReason g_force_open_reason; // Flag to force settings open on invalid path
extern char g_latest_known_version[64];
// Latest Advancely version observed via the startup update check ("" if unknown). Used to gate relay connections to "must be on latest".

/**
 * @brief Applies a counter hotkey action to a custom goal, whatever delivered the key press.
 *
 * Holds every gate and every mutation path in one place so the focus-only SDL key path and the
 * OS-level global hotkey path stay in lockstep: visual-layout-edit block, the co-op receiver /
 * host-only / viewer-is-self gates, the infinite-counter completion block, receiver-sends-to-host,
 * host optimistic mutation + batched persistence, and the singleplayer direct write.
 *
 * @param t A pointer to the main tracker struct.
 * @param app_settings A pointer to the loaded application settings.
 * @param target_goal_root The root name of the custom goal to modify.
 * @param mod_action COOP_MOD_INCREMENT or COOP_MOD_DECREMENT.
 * @return true if the action was applied, false if a gate rejected it or the goal was not found.
 */
bool hotkey_apply_counter_action(Tracker *t, AppSettings *app_settings,
                                 const char *target_goal_root, int mod_action);

/**
 * @brief Processes the global SDL event queue.
 *
 * This function polls for all pending SDL events for the current frame and dispatches
 * them to the appropriate handlers (tracker, settings, overlay) based on the event's window ID.
 * It also handles global quit events.
 *
 * @param t A pointer to the main tracker struct.
 * @param o A pointer to the overlay struct.
 * @param app_settings A pointer to the loaded application settings to be modified and saved.
 * @param is_running A pointer to the main application loop's running flag.
 * @param settings_opened A pointer to the settings window's opened flag.
 * @param deltaTime A pointer to the frame's delta time, passed to the overlay event handler.
 */

void handle_global_events(Tracker *t, Overlay *o, AppSettings *app_settings, bool *is_running,
                          bool *settings_opened, float *deltaTime);

#ifdef __cplusplus
}
#endif

#endif //GLOBAL_EVENT_HANDLER_H
