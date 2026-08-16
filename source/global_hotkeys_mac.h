// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 10.08.2026.
//
// macOS half of the global hotkey backend, kept in its own translation unit on purpose:
// <Carbon/Carbon.h> pulls in CoreText, which declares a struct AnchorPoint, while Advancely's
// data_structures.h declares an enum AnchorPoint. The two cannot coexist in one file, so this
// header deliberately depends on nothing from the rest of the project.

#ifndef GLOBAL_HOTKEYS_MAC_H
#define GLOBAL_HOTKEYS_MAC_H

#include <SDL3/SDL.h>

// Slot capacity is fixed here rather than derived from MAX_HOTKEYS, since settings_utils.h cannot
// be included alongside Carbon. global_hotkeys.cpp static-asserts that the two still agree.
#define GH_MAC_MAX_SLOTS 96

// Modifier bits, mirroring HOTKEY_MOD_* without depending on that header. The caller translates.
// GH_MAC_MOD_CTRL means Command on macOS, which is how the Hotkeys tab labels it and how the key
// capture records it; the physical Control key is registered alongside it as a second form.
#define GH_MAC_MOD_CTRL 0x01u
#define GH_MAC_MOD_SHIFT 0x02u
#define GH_MAC_MOD_ALT 0x04u

typedef void (*GhMacHotkeyCallback)(int slot_id);

/**
 * @brief Installs the Carbon event handler that receives hotkey presses.
 * @param callback Invoked with the slot id each time a registered hotkey fires.
 * @return true if the handler was installed.
 */
bool gh_mac_backend_init(GhMacHotkeyCallback callback);

/**
 * @brief Removes the Carbon event handler. Registered hotkeys should be unregistered first.
 */
void gh_mac_backend_shutdown(void);

/**
 * @brief Registers one slot with the system hotkey API.
 * @param slot_id Slot index, below GH_MAC_MAX_SLOTS.
 * @param scancode The physical key.
 * @param mods GH_MAC_MOD_* bitmask.
 * @param out_reason Receives a human-readable failure reason.
 * @param reason_size Size of out_reason.
 * @return true when macOS accepted the registration.
 */
bool gh_mac_backend_register(int slot_id, SDL_Scancode scancode, Uint16 mods,
                             char *out_reason, size_t reason_size);

/**
 * @brief Releases a slot's registration. Safe to call on a slot that holds none.
 */
void gh_mac_backend_unregister(int slot_id);

#endif //GLOBAL_HOTKEYS_MAC_H
