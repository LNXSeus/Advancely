// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 09.08.2026.
//

#ifndef GLOBAL_HOTKEYS_H
#define GLOBAL_HOTKEYS_H

#include <SDL3/SDL.h>

#include "settings_utils.h" // For AppSettings, MAX_HOTKEYS

// Every binding owns one OS-level slot per HotkeySlot: increment, decrement and toggle.
// A slot id is (hotkey_index * HOTKEY_SLOT_COUNT) + slot, which is what travels in the SDL event.
#define GLOBAL_HOTKEY_MAX_SLOTS (MAX_HOTKEYS * HOTKEY_SLOT_COUNT)

/**
 * @brief Prepares OS-level hotkey delivery. Call once, from the main thread, after SDL_Init.
 *
 * Registers the custom SDL event type used for delivery and performs whatever one-time platform
 * setup the backend needs (Windows message hook, X11 listener thread, Carbon event handler).
 * No hotkeys are registered yet - that is global_hotkeys_apply().
 *
 * @return true on success. On failure the module stays inert and every slot reports an error.
 */
bool global_hotkeys_init(void);

/**
 * @brief Unregisters every hotkey and tears down the platform backend. Safe to call uninitialized.
 */
void global_hotkeys_shutdown(void);

/**
 * @brief Makes the OS registrations match the bindings currently in app_settings.
 *
 * Only bindings with is_global set are registered, and only slots that pass
 * hotkey_global_slot_is_valid(). Cheap to call every frame: it diffs against the registrations
 * already in place and returns immediately when nothing changed.
 *
 * @param app_settings The settings whose hotkeys array should be mirrored to the OS.
 * @param template_data The template currently loaded. Bindings whose goal it does not contain are
 * left unregistered: settings.json keeps a binding per goal name across every template, and a key
 * the OS reserves is taken away from Minecraft, so a binding that cannot do anything must not hold
 * one. Passing nullptr registers nothing, which is what a session without a template can act on.
 */
void global_hotkeys_apply(const AppSettings *app_settings, const TemplateData *template_data);

/**
 * @brief Tests whether an SDL event is an OS-level hotkey press and reports which binding fired.
 *
 * @param event The polled SDL event.
 * @param out_hotkey_index Receives the index into AppSettings::hotkeys.
 * @param out_slot Receives which of the binding's slots fired.
 * @return true if the event was ours, false otherwise (outputs untouched).
 */
bool global_hotkeys_decode_event(const SDL_Event *event, int *out_hotkey_index, HotkeySlot *out_slot);

/**
 * @brief Whether the OS is currently delivering this slot.
 *
 * The SDL key path uses this to decide whether to skip a binding: a global binding the OS holds
 * must not also fire from the focused-window path, but one whose registration failed has to keep
 * working there, since that is the documented fallback.
 *
 * @param hotkey_index Index into AppSettings::hotkeys.
 * @param slot Which of the binding's slots to ask about.
 */
bool global_hotkeys_slot_is_registered(int hotkey_index, HotkeySlot slot);

/**
 * @brief The reason a slot is not currently registered, for the Hotkeys tab to show per row.
 *
 * @param hotkey_index Index into AppSettings::hotkeys.
 * @param slot Which of the binding's slots to ask about.
 * @return A human-readable reason, or nullptr when the slot is registered or intentionally unused.
 */
const char *global_hotkeys_slot_error(int hotkey_index, HotkeySlot slot);

/**
 * @brief False when this build/session cannot deliver global hotkeys at all (e.g. a Wayland
 * session with no X11 connection available), so the UI can say so once instead of per row.
 */
bool global_hotkeys_platform_supported(void);

#endif //GLOBAL_HOTKEYS_H
