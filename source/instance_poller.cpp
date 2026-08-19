// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 19.08.2026.
//

#include "instance_poller.h"

#include <cstring>

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include "logger.h"
#include "path_utils.h" // For get_saves_path and MAX_PATH_LENGTH

// Same cadence the inline poll used: react quickly while an instance is being tracked, back off
// when Minecraft is closed so the process table isn't walked for nothing.
#define POLL_INTERVAL_FOUND_MS 2000
#define POLL_INTERVAL_IDLE_MS 10000

// The wait is split into slices so a quit request is picked up promptly instead of after a full
// interval, which would otherwise stall shutdown by up to ten seconds.
#define POLL_SLICE_MS 100

static SDL_Thread *g_thread = nullptr;
static SDL_Mutex *g_result_mutex = nullptr;

static SDL_AtomicInt g_quit;
static SDL_AtomicInt g_enabled;
static SDL_AtomicInt g_result_ready;

// Guarded by g_result_mutex.
static char g_result_path[MAX_PATH_LENGTH];
static bool g_result_found = false;

static int SDLCALL instance_poller_thread(void *data) {
    (void) data;

    // Only this thread touches it, so the backoff state needs no synchronisation.
    bool last_instance_found = false;

    while (!SDL_GetAtomicInt(&g_quit)) {
        const int interval_ms = last_instance_found ? POLL_INTERVAL_FOUND_MS : POLL_INTERVAL_IDLE_MS;

        for (int waited = 0; waited < interval_ms; waited += POLL_SLICE_MS) {
            if (SDL_GetAtomicInt(&g_quit)) return 0;
            SDL_Delay(POLL_SLICE_MS);
        }

        // Idle without scanning while another path mode is active.
        if (!SDL_GetAtomicInt(&g_enabled)) continue;
        if (SDL_GetAtomicInt(&g_quit)) return 0;

        char detected_path[MAX_PATH_LENGTH];
        const bool found = get_saves_path(detected_path, MAX_PATH_LENGTH, PATH_MODE_INSTANCE, nullptr);

        if (!found && last_instance_found) {
            // Log only on the transition, otherwise this repeats forever while Minecraft is closed.
            log_message(LOG_INFO, "[POLLER] No active Minecraft instance detected. Reducing poll rate.\n");
        }
        last_instance_found = found;

        SDL_LockMutex(g_result_mutex);
        g_result_found = found;
        if (found) {
            strncpy(g_result_path, detected_path, MAX_PATH_LENGTH - 1);
            g_result_path[MAX_PATH_LENGTH - 1] = '\0';
        } else {
            g_result_path[0] = '\0';
        }
        SDL_UnlockMutex(g_result_mutex);

        SDL_SetAtomicInt(&g_result_ready, 1);
    }

    return 0;
}

void instance_poller_start(void) {
    if (g_thread) return;

    SDL_SetAtomicInt(&g_quit, 0);
    SDL_SetAtomicInt(&g_result_ready, 0);
    g_result_path[0] = '\0';
    g_result_found = false;

    if (!g_result_mutex) {
        g_result_mutex = SDL_CreateMutex();
        if (!g_result_mutex) {
            log_message(LOG_ERROR, "[POLLER] Failed to create result mutex. Instance switching is disabled.\n");
            return;
        }
    }

    g_thread = SDL_CreateThread(instance_poller_thread, "AdvancelyInstancePoller", nullptr);
    if (!g_thread) {
        log_message(LOG_ERROR, "[POLLER] Failed to start the instance poller thread: %s\n", SDL_GetError());
        return;
    }

    log_message(LOG_INFO, "[POLLER] Instance poller thread started.\n");
}

void instance_poller_stop(void) {
    SDL_SetAtomicInt(&g_quit, 1);

    if (g_thread) {
        SDL_WaitThread(g_thread, nullptr);
        g_thread = nullptr;
        log_message(LOG_INFO, "[POLLER] Instance poller thread stopped.\n");
    }

    if (g_result_mutex) {
        SDL_DestroyMutex(g_result_mutex);
        g_result_mutex = nullptr;
    }
}

void instance_poller_set_enabled(bool enabled) {
    SDL_SetAtomicInt(&g_enabled, enabled ? 1 : 0);
    // Drop anything the thread produced before the mode changed, so switching back to instance
    // mode later can't act on a path that was detected under a different configuration.
    if (!enabled) SDL_SetAtomicInt(&g_result_ready, 0);
}

bool instance_poller_take_result(char *out_path, size_t max_len) {
    if (!out_path || max_len == 0) return false;
    if (!g_result_mutex) return false;

    // Claim the result so the same scan is only acted on once.
    if (SDL_SetAtomicInt(&g_result_ready, 0) != 1) return false;

    SDL_LockMutex(g_result_mutex);
    if (g_result_found) {
        strncpy(out_path, g_result_path, max_len - 1);
        out_path[max_len - 1] = '\0';
    } else {
        out_path[0] = '\0';
    }
    SDL_UnlockMutex(g_result_mutex);

    return true;
}
