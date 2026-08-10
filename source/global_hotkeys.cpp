// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 09.08.2026.
//
// OS-level (global) hotkey backend. The tracker's normal hotkeys are plain SDL key events and
// only arrive while an Advancely window has focus. A binding marked "Global" is additionally
// registered with the operating system, which delivers it while Minecraft is focused. Every
// backend funnels presses into the same place: a custom SDL event carrying a slot id, pushed
// onto the SDL queue so handle_global_events consumes it exactly like a key press.

#include "global_hotkeys.h"

#include <stdio.h>
#include <string.h>

#include "logger.h"

#ifdef _WIN32
#include <windows.h>
#include <SDL3/SDL_system.h>
#elif defined(__APPLE__)
#include "global_hotkeys_mac.h"
#else
#include <dlfcn.h>
#include <sys/select.h>
#endif

// A slot is one registered key/mods pair. Kept separately from AppSettings so apply() can diff
// what the OS currently holds against what the settings now want.
typedef struct {
    bool active;
    SDL_Scancode scancode;
    Uint16 mods;
} GhSlot;

static bool g_gh_initialized = false;
static Uint32 g_gh_event_type = 0;
static GhSlot g_gh_slots[GLOBAL_HOTKEY_MAX_SLOTS]; // What the last apply() asked for.
static bool g_gh_registered[GLOBAL_HOTKEY_MAX_SLOTS]; // What the OS actually holds.
static char g_gh_slot_error[GLOBAL_HOTKEY_MAX_SLOTS][128];
static bool g_gh_platform_ok = false;

// Compared field by field rather than with memcmp: GhSlot has padding, and a struct assignment is
// not required to copy it, so a memcmp would report phantom differences and re-register forever.
static bool gh_slot_equal(const GhSlot *a, const GhSlot *b) {
    return a->active == b->active && a->scancode == b->scancode && a->mods == b->mods;
}

static void gh_push_slot_event(int slot_id) {
    if (g_gh_event_type == 0) return;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = g_gh_event_type;
    ev.user.type = g_gh_event_type;
    ev.user.code = slot_id;
    SDL_PushEvent(&ev); // Thread-safe, which is what lets the X11 listener thread use it.
}

// ---------------------------------------------------------------------------------------------
// Windows backend: RegisterHotKey on the main thread posts WM_HOTKEY to that thread's message
// queue. SDL's own pump retrieves thread messages, so the registered message hook is where the
// press surfaces.
// ---------------------------------------------------------------------------------------------
#ifdef _WIN32

static UINT gh_scancode_to_win_vk(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return (UINT) ('A' + (sc - SDL_SCANCODE_A));
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return (UINT) ('1' + (sc - SDL_SCANCODE_1));
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return (UINT) (VK_F1 + (sc - SDL_SCANCODE_F1));
    if (sc >= SDL_SCANCODE_F13 && sc <= SDL_SCANCODE_F24) return (UINT) (VK_F13 + (sc - SDL_SCANCODE_F13));
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return (UINT) (VK_NUMPAD1 + (sc - SDL_SCANCODE_KP_1));

    switch (sc) {
        case SDL_SCANCODE_0: return '0';
        case SDL_SCANCODE_RETURN: return VK_RETURN;
        case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
        case SDL_SCANCODE_BACKSPACE: return VK_BACK;
        case SDL_SCANCODE_TAB: return VK_TAB;
        case SDL_SCANCODE_SPACE: return VK_SPACE;
        case SDL_SCANCODE_MINUS: return VK_OEM_MINUS;
        case SDL_SCANCODE_EQUALS: return VK_OEM_PLUS;
        case SDL_SCANCODE_LEFTBRACKET: return VK_OEM_4;
        case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
        case SDL_SCANCODE_BACKSLASH: return VK_OEM_5;
        case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
        case SDL_SCANCODE_APOSTROPHE: return VK_OEM_7;
        case SDL_SCANCODE_GRAVE: return VK_OEM_3;
        case SDL_SCANCODE_COMMA: return VK_OEM_COMMA;
        case SDL_SCANCODE_PERIOD: return VK_OEM_PERIOD;
        case SDL_SCANCODE_SLASH: return VK_OEM_2;
        case SDL_SCANCODE_PRINTSCREEN: return VK_SNAPSHOT;
        case SDL_SCANCODE_SCROLLLOCK: return VK_SCROLL;
        case SDL_SCANCODE_PAUSE: return VK_PAUSE;
        case SDL_SCANCODE_INSERT: return VK_INSERT;
        case SDL_SCANCODE_HOME: return VK_HOME;
        case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
        case SDL_SCANCODE_DELETE: return VK_DELETE;
        case SDL_SCANCODE_END: return VK_END;
        case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
        case SDL_SCANCODE_RIGHT: return VK_RIGHT;
        case SDL_SCANCODE_LEFT: return VK_LEFT;
        case SDL_SCANCODE_DOWN: return VK_DOWN;
        case SDL_SCANCODE_UP: return VK_UP;
        case SDL_SCANCODE_NUMLOCKCLEAR: return VK_NUMLOCK;
        case SDL_SCANCODE_KP_DIVIDE: return VK_DIVIDE;
        case SDL_SCANCODE_KP_MULTIPLY: return VK_MULTIPLY;
        case SDL_SCANCODE_KP_MINUS: return VK_SUBTRACT;
        case SDL_SCANCODE_KP_PLUS: return VK_ADD;
        case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
        case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
        case SDL_SCANCODE_KP_PERIOD: return VK_DECIMAL;
        case SDL_SCANCODE_APPLICATION: return VK_APPS;
        default: return 0;
    }
}

static bool SDLCALL gh_win_message_hook(void *userdata, MSG *msg) {
    (void) userdata;
    if (msg && msg->message == WM_HOTKEY) {
        int slot_id = (int) msg->wParam - 1; // Ids are stored +1 so a valid id is never 0.
        if (slot_id >= 0 && slot_id < GLOBAL_HOTKEY_MAX_SLOTS) {
            gh_push_slot_event(slot_id);
            return false; // Consumed; SDL has nothing to do with WM_HOTKEY.
        }
    }
    return true;
}

static bool gh_platform_init(void) {
    SDL_SetWindowsMessageHook(gh_win_message_hook, nullptr);
    return true;
}

static bool gh_platform_runtime_ok(void) {
    return true;
}

static void gh_platform_shutdown(void) {
    SDL_SetWindowsMessageHook(nullptr, nullptr);
}

static void gh_platform_unregister(int slot_id) {
    UnregisterHotKey(nullptr, slot_id + 1);
}

static bool gh_platform_register(int slot_id, SDL_Scancode scancode, Uint16 mods,
                                 char *out_reason, size_t reason_size) {
    UINT vk = gh_scancode_to_win_vk(scancode);
    if (vk == 0) {
        snprintf(out_reason, reason_size, "This key cannot be registered with Windows.");
        return false;
    }

    UINT win_mods = MOD_NOREPEAT; // Holding the key must not spam the counter.
    if (mods & HOTKEY_MOD_CTRL) win_mods |= MOD_CONTROL;
    if (mods & HOTKEY_MOD_SHIFT) win_mods |= MOD_SHIFT;
    if (mods & HOTKEY_MOD_ALT) win_mods |= MOD_ALT;

    if (!RegisterHotKey(nullptr, slot_id + 1, win_mods, vk)) {
        DWORD err = GetLastError();
        if (err == ERROR_HOTKEY_ALREADY_REGISTERED) {
            snprintf(out_reason, reason_size, "Another program already owns this shortcut.");
        } else {
            snprintf(out_reason, reason_size, "Windows refused this shortcut (error %lu).",
                     (unsigned long) err);
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// macOS backend: Carbon's RegisterEventHotKey. Cocoa still routes key events through the Carbon
// event dispatcher, so this works from an SDL app and, unlike an event tap, needs no
// Accessibility permission prompt.
//
// The Carbon calls themselves live in global_hotkeys_mac.cpp, which includes no Advancely headers:
// <Carbon/Carbon.h> reaches CoreText, whose struct AnchorPoint collides with the enum AnchorPoint
// in data_structures.h. Only these thin wrappers stay here.
// ---------------------------------------------------------------------------------------------
#elif defined(__APPLE__)

static_assert(GLOBAL_HOTKEY_MAX_SLOTS <= GH_MAC_MAX_SLOTS,
              "The macOS backend's slot array is smaller than the number of hotkey slots.");

// Translated rather than shared, since the macOS translation unit cannot see HOTKEY_MOD_*.
static Uint16 gh_mods_to_mac_bits(Uint16 mods) {
    Uint16 bits = 0;
    if (mods & HOTKEY_MOD_CTRL) bits |= GH_MAC_MOD_CTRL;
    if (mods & HOTKEY_MOD_SHIFT) bits |= GH_MAC_MOD_SHIFT;
    if (mods & HOTKEY_MOD_ALT) bits |= GH_MAC_MOD_ALT;
    return bits;
}

static bool gh_platform_init(void) {
    return gh_mac_backend_init(gh_push_slot_event);
}

static bool gh_platform_runtime_ok(void) {
    return true;
}

static void gh_platform_shutdown(void) {
    gh_mac_backend_shutdown();
}

static void gh_platform_unregister(int slot_id) {
    gh_mac_backend_unregister(slot_id);
}

static bool gh_platform_register(int slot_id, SDL_Scancode scancode, Uint16 mods,
                                 char *out_reason, size_t reason_size) {
    return gh_mac_backend_register(slot_id, scancode, gh_mods_to_mac_bits(mods),
                                   out_reason, reason_size);
}

// ---------------------------------------------------------------------------------------------
// Linux/X11 backend: XGrabKey on the root window of a second display connection, owned by a
// listener thread. libX11 is dlopen'd rather than linked so the .deb/.rpm/AUR/Nix packaging keeps
// its current dependency list and a machine without X11 simply loses the feature.
//
// Wayland has no equivalent protocol. Minecraft running on XWayland is still caught, because the
// grab happens on the X server both clients share; a Wayland-native game is not.
// ---------------------------------------------------------------------------------------------
#else

// Minimal Xlib surface, declared locally so no X11 headers are needed at build time.
typedef struct GhXDisplay GhXDisplay;
typedef unsigned long GhXWindow;
typedef unsigned long GhXKeySym;

#define GH_X_KEY_PRESS 2
#define GH_X_SHIFT_MASK (1 << 0)
#define GH_X_LOCK_MASK (1 << 1)
#define GH_X_CONTROL_MASK (1 << 2)
#define GH_X_MOD1_MASK (1 << 3)
#define GH_X_MOD2_MASK (1 << 4)
#define GH_X_GRAB_MODE_ASYNC 1

// Layout-compatible prefix of XKeyEvent. Only the fields up to keycode are read, and XEvent is a
// union, so a generously sized buffer is what actually gets passed to XNextEvent.
typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    GhXDisplay *display;
    GhXWindow window;
    GhXWindow root;
    GhXWindow subwindow;
    unsigned long time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned int keycode;
    int same_screen;
} GhXKeyEvent;

typedef union {
    int type;
    GhXKeyEvent key;
    long padding[32]; // XEvent is 24 longs; the extra room keeps this safe against variants.
} GhXEvent;

typedef GhXDisplay *(*gh_x_open_display_fn)(const char *);
typedef int (*gh_x_close_display_fn)(GhXDisplay *);
typedef GhXWindow (*gh_x_default_root_window_fn)(GhXDisplay *);
typedef unsigned char (*gh_x_keysym_to_keycode_fn)(GhXDisplay *, GhXKeySym);
typedef int (*gh_x_grab_key_fn)(GhXDisplay *, int, unsigned int, GhXWindow, int, int, int);
typedef int (*gh_x_ungrab_key_fn)(GhXDisplay *, int, unsigned int, GhXWindow);
typedef int (*gh_x_next_event_fn)(GhXDisplay *, GhXEvent *);
typedef int (*gh_x_pending_fn)(GhXDisplay *);
typedef int (*gh_x_sync_fn)(GhXDisplay *, int);
typedef int (*gh_x_connection_number_fn)(GhXDisplay *);
typedef int (*gh_x_flush_fn)(GhXDisplay *);
// Xlib's handler takes an XErrorEvent*, which stays opaque here: only the fact that an error
// arrived matters. Typed properly rather than through void* so no function-to-object cast is
// needed, which -Wpedantic rightly objects to.
typedef int (*gh_x_error_handler_fn)(GhXDisplay *, void *);
typedef gh_x_error_handler_fn (*gh_x_set_error_handler_fn)(gh_x_error_handler_fn);

static void *g_gh_x_lib = nullptr;
static gh_x_open_display_fn gh_XOpenDisplay = nullptr;
static gh_x_close_display_fn gh_XCloseDisplay = nullptr;
static gh_x_default_root_window_fn gh_XDefaultRootWindow = nullptr;
static gh_x_keysym_to_keycode_fn gh_XKeysymToKeycode = nullptr;
static gh_x_grab_key_fn gh_XGrabKey = nullptr;
static gh_x_ungrab_key_fn gh_XUngrabKey = nullptr;
static gh_x_next_event_fn gh_XNextEvent = nullptr;
static gh_x_pending_fn gh_XPending = nullptr;
static gh_x_sync_fn gh_XSync = nullptr;
static gh_x_connection_number_fn gh_XConnectionNumber = nullptr;
static gh_x_flush_fn gh_XFlush = nullptr;
static gh_x_set_error_handler_fn gh_XSetErrorHandler = nullptr;

// The listener thread owns the display connection outright: every Xlib call happens on it, so no
// Xlib locking is needed. The main thread only hands over a desired-slot snapshot behind a mutex.
static SDL_Thread *g_gh_x_thread = nullptr;
static SDL_Mutex *g_gh_x_mutex = nullptr;
static SDL_AtomicInt g_gh_x_quit;
static SDL_AtomicInt g_gh_x_dirty;
static SDL_AtomicInt g_gh_x_ready; // 1 = display open, -1 = failed, 0 = still starting.
static GhSlot g_gh_x_desired[GLOBAL_HOTKEY_MAX_SLOTS];
static bool g_gh_x_grabbed[GLOBAL_HOTKEY_MAX_SLOTS];
static unsigned int g_gh_x_grabbed_keycode[GLOBAL_HOTKEY_MAX_SLOTS];
static unsigned int g_gh_x_grabbed_mask[GLOBAL_HOTKEY_MAX_SLOTS];
static char g_gh_x_error[GLOBAL_HOTKEY_MAX_SLOTS][128];
static int g_gh_x_last_error = 0;

static int gh_x_error_handler(GhXDisplay *display, void *error_event) {
    (void) display;
    (void) error_event;
    g_gh_x_last_error = 1;
    return 0; // Swallow it: a failed grab is reported through the per-slot error text instead.
}

static GhXKeySym gh_scancode_to_keysym(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return 0xFFBE + (GhXKeySym) (sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_F13 && sc <= SDL_SCANCODE_F24) return 0xFFCA + (GhXKeySym) (sc - SDL_SCANCODE_F13);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return 0xFFB1 + (GhXKeySym) (sc - SDL_SCANCODE_KP_1);

    switch (sc) {
        case SDL_SCANCODE_RETURN: return 0xFF0D;
        case SDL_SCANCODE_ESCAPE: return 0xFF1B;
        case SDL_SCANCODE_BACKSPACE: return 0xFF08;
        case SDL_SCANCODE_TAB: return 0xFF09;
        case SDL_SCANCODE_PRINTSCREEN: return 0xFF61;
        case SDL_SCANCODE_SCROLLLOCK: return 0xFF14;
        case SDL_SCANCODE_PAUSE: return 0xFF13;
        case SDL_SCANCODE_INSERT: return 0xFF63;
        case SDL_SCANCODE_HOME: return 0xFF50;
        case SDL_SCANCODE_PAGEUP: return 0xFF55;
        case SDL_SCANCODE_DELETE: return 0xFFFF;
        case SDL_SCANCODE_END: return 0xFF57;
        case SDL_SCANCODE_PAGEDOWN: return 0xFF56;
        case SDL_SCANCODE_RIGHT: return 0xFF53;
        case SDL_SCANCODE_LEFT: return 0xFF51;
        case SDL_SCANCODE_DOWN: return 0xFF54;
        case SDL_SCANCODE_UP: return 0xFF52;
        case SDL_SCANCODE_NUMLOCKCLEAR: return 0xFF7F;
        case SDL_SCANCODE_KP_DIVIDE: return 0xFFAF;
        case SDL_SCANCODE_KP_MULTIPLY: return 0xFFAA;
        case SDL_SCANCODE_KP_MINUS: return 0xFFAD;
        case SDL_SCANCODE_KP_PLUS: return 0xFFAB;
        case SDL_SCANCODE_KP_ENTER: return 0xFF8D;
        case SDL_SCANCODE_KP_0: return 0xFFB0;
        case SDL_SCANCODE_KP_PERIOD: return 0xFFAE;
        case SDL_SCANCODE_APPLICATION: return 0xFF67;
        default: break;
    }

    // Everything else is a printable key, where the X keysym equals its Latin-1 code point and so
    // equals the unshifted SDL keycode.
    SDL_Keycode kc = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
    if (kc > 0 && kc < 0x100) return (GhXKeySym) kc;
    return 0;
}

static unsigned int gh_mods_to_x_mask(Uint16 mods) {
    unsigned int mask = 0;
    if (mods & HOTKEY_MOD_CTRL) mask |= GH_X_CONTROL_MASK;
    if (mods & HOTKEY_MOD_SHIFT) mask |= GH_X_SHIFT_MASK;
    if (mods & HOTKEY_MOD_ALT) mask |= GH_X_MOD1_MASK;
    return mask;
}

// Caps Lock and Num Lock show up in the event state and in what XGrabKey matches, so each binding
// is grabbed once per combination of those two.
static const unsigned int GH_X_IGNORED_MASKS[4] = {
    0,
    GH_X_LOCK_MASK,
    GH_X_MOD2_MASK,
    GH_X_LOCK_MASK | GH_X_MOD2_MASK
};

static void gh_x_sync_grabs(GhXDisplay *display, GhXWindow root) {
    GhSlot desired[GLOBAL_HOTKEY_MAX_SLOTS];
    SDL_LockMutex(g_gh_x_mutex);
    memcpy(desired, g_gh_x_desired, sizeof(desired));
    SDL_UnlockMutex(g_gh_x_mutex);

    char errors[GLOBAL_HOTKEY_MAX_SLOTS][128];
    memset(errors, 0, sizeof(errors));

    gh_x_error_handler_fn prev_handler = gh_XSetErrorHandler(gh_x_error_handler);

    for (int i = 0; i < GLOBAL_HOTKEY_MAX_SLOTS; i++) {
        if (g_gh_x_grabbed[i]) {
            for (int m = 0; m < 4; m++) {
                gh_XUngrabKey(display, (int) g_gh_x_grabbed_keycode[i],
                              g_gh_x_grabbed_mask[i] | GH_X_IGNORED_MASKS[m], root);
            }
            g_gh_x_grabbed[i] = false;
        }

        if (!desired[i].active) continue;

        GhXKeySym keysym = gh_scancode_to_keysym(desired[i].scancode);
        unsigned int keycode = keysym ? gh_XKeysymToKeycode(display, keysym) : 0;
        if (keycode == 0) {
            snprintf(errors[i], sizeof(errors[i]), "This key is not on the current X11 layout.");
            continue;
        }

        unsigned int mask = gh_mods_to_x_mask(desired[i].mods);
        g_gh_x_last_error = 0;
        for (int m = 0; m < 4; m++) {
            gh_XGrabKey(display, (int) keycode, mask | GH_X_IGNORED_MASKS[m], root, 1,
                        GH_X_GRAB_MODE_ASYNC, GH_X_GRAB_MODE_ASYNC);
        }
        gh_XSync(display, 0); // Grab errors arrive asynchronously; force them out here.

        if (g_gh_x_last_error) {
            for (int m = 0; m < 4; m++) {
                gh_XUngrabKey(display, (int) keycode, mask | GH_X_IGNORED_MASKS[m], root);
            }
            snprintf(errors[i], sizeof(errors[i]), "Another program already owns this shortcut.");
            continue;
        }

        g_gh_x_grabbed[i] = true;
        g_gh_x_grabbed_keycode[i] = keycode;
        g_gh_x_grabbed_mask[i] = mask;
    }

    gh_XSetErrorHandler(prev_handler);

    SDL_LockMutex(g_gh_x_mutex);
    memcpy(g_gh_x_error, errors, sizeof(errors));
    SDL_UnlockMutex(g_gh_x_mutex);
}

static int SDLCALL gh_x_thread_main(void *userdata) {
    (void) userdata;

    GhXDisplay *display = gh_XOpenDisplay(nullptr);
    if (!display) {
        log_message(LOG_ERROR,
                    "[HOTKEYS] No X11 display; global hotkeys stay window-focused (Wayland session?).\n");
        SDL_SetAtomicInt(&g_gh_x_ready, -1);
        return 0;
    }
    GhXWindow root = gh_XDefaultRootWindow(display);
    int fd = gh_XConnectionNumber(display);
    SDL_SetAtomicInt(&g_gh_x_ready, 1);

    while (SDL_GetAtomicInt(&g_gh_x_quit) == 0) {
        if (SDL_SetAtomicInt(&g_gh_x_dirty, 0) == 1) {
            gh_x_sync_grabs(display, root);
        }

        // Sleep on the connection rather than spinning; the timeout is what bounds how long a
        // settings change waits before its grabs are applied.
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;
        select(fd + 1, &read_set, nullptr, nullptr, &timeout);

        while (gh_XPending(display) > 0) {
            GhXEvent ev;
            memset(&ev, 0, sizeof(ev));
            gh_XNextEvent(display, &ev);
            if (ev.type != GH_X_KEY_PRESS) continue;

            unsigned int state = ev.key.state & ~(unsigned int) (GH_X_LOCK_MASK | GH_X_MOD2_MASK);
            for (int i = 0; i < GLOBAL_HOTKEY_MAX_SLOTS; i++) {
                if (!g_gh_x_grabbed[i]) continue;
                if (g_gh_x_grabbed_keycode[i] != ev.key.keycode) continue;
                if (g_gh_x_grabbed_mask[i] != state) continue;
                gh_push_slot_event(i);
                break;
            }
        }
    }

    for (int i = 0; i < GLOBAL_HOTKEY_MAX_SLOTS; i++) {
        if (!g_gh_x_grabbed[i]) continue;
        for (int m = 0; m < 4; m++) {
            gh_XUngrabKey(display, (int) g_gh_x_grabbed_keycode[i],
                          g_gh_x_grabbed_mask[i] | GH_X_IGNORED_MASKS[m], root);
        }
        g_gh_x_grabbed[i] = false;
    }
    gh_XFlush(display);
    gh_XCloseDisplay(display);
    return 0;
}

static bool gh_platform_init(void) {
    g_gh_x_lib = dlopen("libX11.so.6", RTLD_LAZY);
    if (!g_gh_x_lib) g_gh_x_lib = dlopen("libX11.so", RTLD_LAZY);
    if (!g_gh_x_lib) {
        log_message(LOG_INFO, "[HOTKEYS] libX11 not available; global hotkeys are disabled.\n");
        return false;
    }

    gh_XOpenDisplay = (gh_x_open_display_fn) dlsym(g_gh_x_lib, "XOpenDisplay");
    gh_XCloseDisplay = (gh_x_close_display_fn) dlsym(g_gh_x_lib, "XCloseDisplay");
    gh_XDefaultRootWindow = (gh_x_default_root_window_fn) dlsym(g_gh_x_lib, "XDefaultRootWindow");
    gh_XKeysymToKeycode = (gh_x_keysym_to_keycode_fn) dlsym(g_gh_x_lib, "XKeysymToKeycode");
    gh_XGrabKey = (gh_x_grab_key_fn) dlsym(g_gh_x_lib, "XGrabKey");
    gh_XUngrabKey = (gh_x_ungrab_key_fn) dlsym(g_gh_x_lib, "XUngrabKey");
    gh_XNextEvent = (gh_x_next_event_fn) dlsym(g_gh_x_lib, "XNextEvent");
    gh_XPending = (gh_x_pending_fn) dlsym(g_gh_x_lib, "XPending");
    gh_XSync = (gh_x_sync_fn) dlsym(g_gh_x_lib, "XSync");
    gh_XConnectionNumber = (gh_x_connection_number_fn) dlsym(g_gh_x_lib, "XConnectionNumber");
    gh_XFlush = (gh_x_flush_fn) dlsym(g_gh_x_lib, "XFlush");
    gh_XSetErrorHandler = (gh_x_set_error_handler_fn) dlsym(g_gh_x_lib, "XSetErrorHandler");

    if (!gh_XOpenDisplay || !gh_XCloseDisplay || !gh_XDefaultRootWindow || !gh_XKeysymToKeycode ||
        !gh_XGrabKey || !gh_XUngrabKey || !gh_XNextEvent || !gh_XPending || !gh_XSync ||
        !gh_XConnectionNumber || !gh_XFlush || !gh_XSetErrorHandler) {
        log_message(LOG_ERROR, "[HOTKEYS] libX11 is missing expected symbols; global hotkeys are disabled.\n");
        dlclose(g_gh_x_lib);
        g_gh_x_lib = nullptr;
        return false;
    }

    g_gh_x_mutex = SDL_CreateMutex();
    if (!g_gh_x_mutex) {
        dlclose(g_gh_x_lib);
        g_gh_x_lib = nullptr;
        return false;
    }

    SDL_SetAtomicInt(&g_gh_x_quit, 0);
    SDL_SetAtomicInt(&g_gh_x_dirty, 0);
    SDL_SetAtomicInt(&g_gh_x_ready, 0);
    g_gh_x_thread = SDL_CreateThread(gh_x_thread_main, "AdvancelyHotkeys", nullptr);
    if (!g_gh_x_thread) {
        SDL_DestroyMutex(g_gh_x_mutex);
        g_gh_x_mutex = nullptr;
        dlclose(g_gh_x_lib);
        g_gh_x_lib = nullptr;
        return false;
    }
    return true;
}

// The display connection is opened on the listener thread, so "did X11 work" is only knowable a
// moment after init. -1 means the connection failed, which is what a Wayland-only session gives.
static bool gh_platform_runtime_ok(void) {
    return g_gh_x_lib != nullptr && SDL_GetAtomicInt(&g_gh_x_ready) != -1;
}

static void gh_platform_shutdown(void) {
    if (g_gh_x_thread) {
        SDL_SetAtomicInt(&g_gh_x_quit, 1);
        SDL_WaitThread(g_gh_x_thread, nullptr);
        g_gh_x_thread = nullptr;
    }
    if (g_gh_x_mutex) {
        SDL_DestroyMutex(g_gh_x_mutex);
        g_gh_x_mutex = nullptr;
    }
    if (g_gh_x_lib) {
        dlclose(g_gh_x_lib);
        g_gh_x_lib = nullptr;
    }
}

// X11 grabs are applied as a whole set by the listener thread, so the per-slot entry points just
// stage the request; the thread picks it up on its next tick.
static void gh_platform_unregister(int slot_id) {
    if (!g_gh_x_mutex) return;
    SDL_LockMutex(g_gh_x_mutex);
    g_gh_x_desired[slot_id].active = false;
    SDL_UnlockMutex(g_gh_x_mutex);
    SDL_SetAtomicInt(&g_gh_x_dirty, 1);
}

static bool gh_platform_register(int slot_id, SDL_Scancode scancode, Uint16 mods,
                                 char *out_reason, size_t reason_size) {
    if (!g_gh_x_mutex || SDL_GetAtomicInt(&g_gh_x_ready) == -1) {
        snprintf(out_reason, reason_size, "No X11 connection (a Wayland session cannot do this).");
        return false;
    }
    if (gh_scancode_to_keysym(scancode) == 0) {
        snprintf(out_reason, reason_size, "This key cannot be registered with X11.");
        return false;
    }
    SDL_LockMutex(g_gh_x_mutex);
    g_gh_x_desired[slot_id].active = true;
    g_gh_x_desired[slot_id].scancode = scancode;
    g_gh_x_desired[slot_id].mods = mods;
    SDL_UnlockMutex(g_gh_x_mutex);
    SDL_SetAtomicInt(&g_gh_x_dirty, 1);
    return true; // A grab conflict is only knowable on the thread; it lands in the error text there.
}

#endif

// ---------------------------------------------------------------------------------------------
// Platform-neutral API
// ---------------------------------------------------------------------------------------------

bool global_hotkeys_init(void) {
    if (g_gh_initialized) return g_gh_platform_ok;

    memset(g_gh_slots, 0, sizeof(g_gh_slots));
    memset(g_gh_registered, 0, sizeof(g_gh_registered));
    memset(g_gh_slot_error, 0, sizeof(g_gh_slot_error));

    g_gh_event_type = SDL_RegisterEvents(1);
    if (g_gh_event_type == 0) {
        log_message(LOG_ERROR, "[HOTKEYS] Could not register an SDL event type for global hotkeys.\n");
        g_gh_initialized = true;
        g_gh_platform_ok = false;
        return false;
    }

    g_gh_platform_ok = gh_platform_init();
    g_gh_initialized = true;
    if (g_gh_platform_ok) {
        log_message(LOG_INFO, "[HOTKEYS] Global hotkey backend ready.\n");
    }
    return g_gh_platform_ok;
}

void global_hotkeys_shutdown(void) {
    if (!g_gh_initialized) return;

    if (g_gh_platform_ok) {
        for (int i = 0; i < GLOBAL_HOTKEY_MAX_SLOTS; i++) {
            if (g_gh_registered[i]) gh_platform_unregister(i);
        }
        gh_platform_shutdown();
    }

    memset(g_gh_slots, 0, sizeof(g_gh_slots));
    memset(g_gh_registered, 0, sizeof(g_gh_registered));
    memset(g_gh_slot_error, 0, sizeof(g_gh_slot_error));
    g_gh_platform_ok = false;
    g_gh_initialized = false;
}

void global_hotkeys_apply(const AppSettings *app_settings) {
    if (!g_gh_initialized || !app_settings) return;

    // Build what the settings ask for, then diff. A binding slot only becomes a registration when
    // it is marked global, actually bound, and passes the same validation the Settings UI enforces.
    GhSlot wanted[GLOBAL_HOTKEY_MAX_SLOTS];
    memset(wanted, 0, sizeof(wanted));

    int count = app_settings->hotkey_count;
    if (count > MAX_HOTKEYS) count = MAX_HOTKEYS;

    for (int i = 0; i < count; i++) {
        const HotkeyBinding *hb = &app_settings->hotkeys[i];
        if (!hb->is_global) continue;

        for (int half = 0; half < 2; half++) {
            const char *key_name = half == 0 ? hb->increment_key : hb->decrement_key;
            Uint16 mods = half == 0 ? hb->increment_mods : hb->decrement_mods;
            if (key_name[0] == '\0' || strcmp(key_name, "None") == 0) continue;

            char reason[128];
            if (!hotkey_global_slot_is_valid(key_name, mods, reason, sizeof(reason))) continue;

            SDL_Scancode sc = SDL_GetScancodeFromName(key_name);
            if (sc == SDL_SCANCODE_UNKNOWN) continue;

            int slot_id = i * 2 + half;
            wanted[slot_id].active = true;
            wanted[slot_id].scancode = sc;
            wanted[slot_id].mods = mods;
        }
    }

    bool changed = false;
    for (int i = 0; i < GLOBAL_HOTKEY_MAX_SLOTS; i++) {
        if (!gh_slot_equal(&g_gh_slots[i], &wanted[i])) {
            changed = true;
            break;
        }
    }
    if (!changed) return;

    if (!g_gh_platform_ok) {
        // Nothing can be registered, but the per-row reason still has to be truthful.
        for (int i = 0; i < GLOBAL_HOTKEY_MAX_SLOTS; i++) {
            g_gh_slots[i] = wanted[i];
            if (wanted[i].active) {
                snprintf(g_gh_slot_error[i], sizeof(g_gh_slot_error[i]),
                         "Global hotkeys are not available on this system.");
            } else {
                g_gh_slot_error[i][0] = '\0';
            }
        }
        return;
    }

    for (int i = 0; i < GLOBAL_HOTKEY_MAX_SLOTS; i++) {
        if (gh_slot_equal(&g_gh_slots[i], &wanted[i])) continue;

        if (g_gh_registered[i]) {
            gh_platform_unregister(i);
            g_gh_registered[i] = false;
        }
        // Store the request even when registration fails below, so a shortcut another program owns
        // is reported once instead of being retried on every frame.
        g_gh_slots[i] = wanted[i];
        g_gh_slot_error[i][0] = '\0';

        if (!wanted[i].active) continue;

        char reason[128];
        reason[0] = '\0';
        if (gh_platform_register(i, wanted[i].scancode, wanted[i].mods, reason, sizeof(reason))) {
            g_gh_registered[i] = true;
        } else {
            snprintf(g_gh_slot_error[i], sizeof(g_gh_slot_error[i]), "%s", reason);
            log_message(LOG_ERROR, "[HOTKEYS] Global binding %d (%s) failed: %s\n",
                        i / 2, (i % 2) == 0 ? "increment" : "decrement", reason);
        }
    }
}

bool global_hotkeys_decode_event(const SDL_Event *event, int *out_hotkey_index, bool *out_is_decrement) {
    if (!event || g_gh_event_type == 0 || event->type != g_gh_event_type) return false;

    int slot_id = (int) event->user.code;
    if (slot_id < 0 || slot_id >= GLOBAL_HOTKEY_MAX_SLOTS) return false;

    if (out_hotkey_index) *out_hotkey_index = slot_id / 2;
    if (out_is_decrement) *out_is_decrement = (slot_id % 2) == 1;
    return true;
}

bool global_hotkeys_slot_is_registered(int hotkey_index, bool decrement) {
    int slot_id = hotkey_index * 2 + (decrement ? 1 : 0);
    if (slot_id < 0 || slot_id >= GLOBAL_HOTKEY_MAX_SLOTS) return false;
    // The X11 backend accepts a registration up front and only discovers a grab conflict on its
    // listener thread, so a slot counts as registered only while it also has no error to report.
    return g_gh_registered[slot_id] && global_hotkeys_slot_error(hotkey_index, decrement) == nullptr;
}

const char *global_hotkeys_slot_error(int hotkey_index, bool decrement) {
    int slot_id = hotkey_index * 2 + (decrement ? 1 : 0);
    if (slot_id < 0 || slot_id >= GLOBAL_HOTKEY_MAX_SLOTS) return nullptr;

#if !defined(_WIN32) && !defined(__APPLE__)
    // A slot only reaches the X backend once apply() accepted it, so for those the listener thread
    // is the authority and its entry is copied verbatim, empty included. Copying only non-empty
    // text would leave a stale conflict on screen after a re-grab cleared it.
    if (g_gh_registered[slot_id]) {
        if (SDL_GetAtomicInt(&g_gh_x_ready) == -1) {
            // The display is opened on the thread, so a missing X server is only knowable after
            // apply() already accepted the slot. Without this the row would stay silent and, worse,
            // still count as registered, which makes the SDL key path skip it as well.
            snprintf(g_gh_slot_error[slot_id], sizeof(g_gh_slot_error[slot_id]),
                     "No X11 connection (a Wayland session cannot do this).");
        } else if (g_gh_x_mutex) {
            SDL_LockMutex(g_gh_x_mutex);
            snprintf(g_gh_slot_error[slot_id], sizeof(g_gh_slot_error[slot_id]), "%s",
                     g_gh_x_error[slot_id]);
            SDL_UnlockMutex(g_gh_x_mutex);
        }
    }
#endif

    return g_gh_slot_error[slot_id][0] != '\0' ? g_gh_slot_error[slot_id] : nullptr;
}

bool global_hotkeys_platform_supported(void) {
    return g_gh_platform_ok && gh_platform_runtime_ok();
}
