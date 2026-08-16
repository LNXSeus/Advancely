// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 10.08.2026.
//
// Carbon's RegisterEventHotKey is the one macOS hotkey API that needs no Accessibility permission
// prompt, and Cocoa still routes key events through the Carbon event dispatcher, so it works from
// an SDL app. Nothing from the rest of Advancely may be included here - see global_hotkeys_mac.h.

#include "global_hotkeys_mac.h"

#include <stdio.h>

#include <Carbon/Carbon.h>

#define GH_MAC_SIGNATURE 0x41445643u // 'ADVC', spelled out so -Wmultichar stays quiet.

// GH_MAC_MOD_CTRL stands for Command here, which is what the Hotkeys tab names it and what
// hotkey_mods_from_sdl() records, but that function also accepts the physical Control key for the
// same bit. A second registration per slot keeps both keys working system-wide, so the key that
// created the binding is always the key that triggers it.
static EventHotKeyRef g_gh_mac_refs[GH_MAC_MAX_SLOTS];
static EventHotKeyRef g_gh_mac_refs_control[GH_MAC_MAX_SLOTS];
static EventHandlerRef g_gh_mac_handler = nullptr;
static GhMacHotkeyCallback g_gh_mac_callback = nullptr;

static UInt32 gh_scancode_to_mac_vk(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_A: return kVK_ANSI_A;
        case SDL_SCANCODE_B: return kVK_ANSI_B;
        case SDL_SCANCODE_C: return kVK_ANSI_C;
        case SDL_SCANCODE_D: return kVK_ANSI_D;
        case SDL_SCANCODE_E: return kVK_ANSI_E;
        case SDL_SCANCODE_F: return kVK_ANSI_F;
        case SDL_SCANCODE_G: return kVK_ANSI_G;
        case SDL_SCANCODE_H: return kVK_ANSI_H;
        case SDL_SCANCODE_I: return kVK_ANSI_I;
        case SDL_SCANCODE_J: return kVK_ANSI_J;
        case SDL_SCANCODE_K: return kVK_ANSI_K;
        case SDL_SCANCODE_L: return kVK_ANSI_L;
        case SDL_SCANCODE_M: return kVK_ANSI_M;
        case SDL_SCANCODE_N: return kVK_ANSI_N;
        case SDL_SCANCODE_O: return kVK_ANSI_O;
        case SDL_SCANCODE_P: return kVK_ANSI_P;
        case SDL_SCANCODE_Q: return kVK_ANSI_Q;
        case SDL_SCANCODE_R: return kVK_ANSI_R;
        case SDL_SCANCODE_S: return kVK_ANSI_S;
        case SDL_SCANCODE_T: return kVK_ANSI_T;
        case SDL_SCANCODE_U: return kVK_ANSI_U;
        case SDL_SCANCODE_V: return kVK_ANSI_V;
        case SDL_SCANCODE_W: return kVK_ANSI_W;
        case SDL_SCANCODE_X: return kVK_ANSI_X;
        case SDL_SCANCODE_Y: return kVK_ANSI_Y;
        case SDL_SCANCODE_Z: return kVK_ANSI_Z;
        case SDL_SCANCODE_0: return kVK_ANSI_0;
        case SDL_SCANCODE_1: return kVK_ANSI_1;
        case SDL_SCANCODE_2: return kVK_ANSI_2;
        case SDL_SCANCODE_3: return kVK_ANSI_3;
        case SDL_SCANCODE_4: return kVK_ANSI_4;
        case SDL_SCANCODE_5: return kVK_ANSI_5;
        case SDL_SCANCODE_6: return kVK_ANSI_6;
        case SDL_SCANCODE_7: return kVK_ANSI_7;
        case SDL_SCANCODE_8: return kVK_ANSI_8;
        case SDL_SCANCODE_9: return kVK_ANSI_9;
        case SDL_SCANCODE_RETURN: return kVK_Return;
        case SDL_SCANCODE_TAB: return kVK_Tab;
        case SDL_SCANCODE_SPACE: return kVK_Space;
        case SDL_SCANCODE_BACKSPACE: return kVK_Delete;
        case SDL_SCANCODE_ESCAPE: return kVK_Escape;
        case SDL_SCANCODE_MINUS: return kVK_ANSI_Minus;
        case SDL_SCANCODE_EQUALS: return kVK_ANSI_Equal;
        case SDL_SCANCODE_LEFTBRACKET: return kVK_ANSI_LeftBracket;
        case SDL_SCANCODE_RIGHTBRACKET: return kVK_ANSI_RightBracket;
        case SDL_SCANCODE_BACKSLASH: return kVK_ANSI_Backslash;
        case SDL_SCANCODE_SEMICOLON: return kVK_ANSI_Semicolon;
        case SDL_SCANCODE_APOSTROPHE: return kVK_ANSI_Quote;
        case SDL_SCANCODE_GRAVE: return kVK_ANSI_Grave;
        case SDL_SCANCODE_COMMA: return kVK_ANSI_Comma;
        case SDL_SCANCODE_PERIOD: return kVK_ANSI_Period;
        case SDL_SCANCODE_SLASH: return kVK_ANSI_Slash;
        case SDL_SCANCODE_F1: return kVK_F1;
        case SDL_SCANCODE_F2: return kVK_F2;
        case SDL_SCANCODE_F3: return kVK_F3;
        case SDL_SCANCODE_F4: return kVK_F4;
        case SDL_SCANCODE_F5: return kVK_F5;
        case SDL_SCANCODE_F6: return kVK_F6;
        case SDL_SCANCODE_F7: return kVK_F7;
        case SDL_SCANCODE_F8: return kVK_F8;
        case SDL_SCANCODE_F9: return kVK_F9;
        case SDL_SCANCODE_F10: return kVK_F10;
        case SDL_SCANCODE_F11: return kVK_F11;
        case SDL_SCANCODE_F12: return kVK_F12;
        case SDL_SCANCODE_F13: return kVK_F13;
        case SDL_SCANCODE_F14: return kVK_F14;
        case SDL_SCANCODE_F15: return kVK_F15;
        case SDL_SCANCODE_F16: return kVK_F16;
        case SDL_SCANCODE_F17: return kVK_F17;
        case SDL_SCANCODE_F18: return kVK_F18;
        case SDL_SCANCODE_F19: return kVK_F19;
        case SDL_SCANCODE_F20: return kVK_F20;
        case SDL_SCANCODE_HOME: return kVK_Home;
        case SDL_SCANCODE_END: return kVK_End;
        case SDL_SCANCODE_PAGEUP: return kVK_PageUp;
        case SDL_SCANCODE_PAGEDOWN: return kVK_PageDown;
        case SDL_SCANCODE_DELETE: return kVK_ForwardDelete;
        case SDL_SCANCODE_LEFT: return kVK_LeftArrow;
        case SDL_SCANCODE_RIGHT: return kVK_RightArrow;
        case SDL_SCANCODE_UP: return kVK_UpArrow;
        case SDL_SCANCODE_DOWN: return kVK_DownArrow;
        case SDL_SCANCODE_KP_0: return kVK_ANSI_Keypad0;
        case SDL_SCANCODE_KP_1: return kVK_ANSI_Keypad1;
        case SDL_SCANCODE_KP_2: return kVK_ANSI_Keypad2;
        case SDL_SCANCODE_KP_3: return kVK_ANSI_Keypad3;
        case SDL_SCANCODE_KP_4: return kVK_ANSI_Keypad4;
        case SDL_SCANCODE_KP_5: return kVK_ANSI_Keypad5;
        case SDL_SCANCODE_KP_6: return kVK_ANSI_Keypad6;
        case SDL_SCANCODE_KP_7: return kVK_ANSI_Keypad7;
        case SDL_SCANCODE_KP_8: return kVK_ANSI_Keypad8;
        case SDL_SCANCODE_KP_9: return kVK_ANSI_Keypad9;
        case SDL_SCANCODE_KP_PERIOD: return kVK_ANSI_KeypadDecimal;
        case SDL_SCANCODE_KP_PLUS: return kVK_ANSI_KeypadPlus;
        case SDL_SCANCODE_KP_MINUS: return kVK_ANSI_KeypadMinus;
        case SDL_SCANCODE_KP_MULTIPLY: return kVK_ANSI_KeypadMultiply;
        case SDL_SCANCODE_KP_DIVIDE: return kVK_ANSI_KeypadDivide;
        case SDL_SCANCODE_KP_ENTER: return kVK_ANSI_KeypadEnter;
        case SDL_SCANCODE_KP_EQUALS: return kVK_ANSI_KeypadEquals;
        default: return 0xFFFF; // No Carbon virtual key exists (F21-F24 and friends).
    }
}

static OSStatus gh_mac_hotkey_handler(EventHandlerCallRef call_ref, EventRef event, void *userdata) {
    (void) call_ref;
    (void) userdata;
    EventHotKeyID hk_id;
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                          sizeof(hk_id), nullptr, &hk_id) == noErr &&
        hk_id.signature == GH_MAC_SIGNATURE &&
        hk_id.id < (UInt32) GH_MAC_MAX_SLOTS &&
        g_gh_mac_callback) {
        g_gh_mac_callback((int) hk_id.id);
    }
    return noErr;
}

bool gh_mac_backend_init(GhMacHotkeyCallback callback) {
    g_gh_mac_callback = callback;

    EventTypeSpec spec;
    spec.eventClass = kEventClassKeyboard;
    spec.eventKind = kEventHotKeyPressed;
    OSStatus status = InstallApplicationEventHandler(&gh_mac_hotkey_handler, 1, &spec,
                                                     nullptr, &g_gh_mac_handler);
    return status == noErr;
}

void gh_mac_backend_shutdown(void) {
    if (g_gh_mac_handler) {
        RemoveEventHandler(g_gh_mac_handler);
        g_gh_mac_handler = nullptr;
    }
    g_gh_mac_callback = nullptr;
}

bool gh_mac_backend_register(int slot_id, SDL_Scancode scancode, Uint16 mods,
                             char *out_reason, size_t reason_size) {
    if (slot_id < 0 || slot_id >= GH_MAC_MAX_SLOTS) return false;

    UInt32 vk = gh_scancode_to_mac_vk(scancode);
    if (vk == 0xFFFF) {
        snprintf(out_reason, reason_size, "This key cannot be registered with macOS.");
        return false;
    }

    UInt32 mac_mods = 0;
    if (mods & GH_MAC_MOD_CTRL) mac_mods |= cmdKey;
    if (mods & GH_MAC_MOD_SHIFT) mac_mods |= shiftKey;
    if (mods & GH_MAC_MOD_ALT) mac_mods |= optionKey;

    EventHotKeyID hk_id;
    hk_id.signature = GH_MAC_SIGNATURE;
    hk_id.id = (UInt32) slot_id;

    EventHotKeyRef ref = nullptr;
    OSStatus status = RegisterEventHotKey(vk, mac_mods, hk_id, GetApplicationEventTarget(), 0, &ref);
    if (status != noErr || !ref) {
        snprintf(out_reason, reason_size, "Another program already owns this shortcut.");
        return false;
    }
    g_gh_mac_refs[slot_id] = ref;

    // The Control variant is a bonus, not a requirement: macOS reserves several Control combos for
    // its own text editing, so a refusal here leaves the Command form working and stays silent.
    if (mods & GH_MAC_MOD_CTRL) {
        UInt32 control_mods = (mac_mods & ~(UInt32) cmdKey) | controlKey;
        EventHotKeyRef control_ref = nullptr;
        if (RegisterEventHotKey(vk, control_mods, hk_id, GetApplicationEventTarget(), 0,
                                &control_ref) == noErr && control_ref) {
            g_gh_mac_refs_control[slot_id] = control_ref;
        }
    }
    return true;
}

void gh_mac_backend_unregister(int slot_id) {
    if (slot_id < 0 || slot_id >= GH_MAC_MAX_SLOTS) return;
    if (g_gh_mac_refs[slot_id]) {
        UnregisterEventHotKey(g_gh_mac_refs[slot_id]);
        g_gh_mac_refs[slot_id] = nullptr;
    }
    if (g_gh_mac_refs_control[slot_id]) {
        UnregisterEventHotKey(g_gh_mac_refs_control[slot_id]);
        g_gh_mac_refs_control[slot_id] = nullptr;
    }
}
