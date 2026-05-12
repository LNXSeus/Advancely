// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 12.05.2026.
//
// Async skin face cache. Resolves Minecraft UUIDs to 8x8 face textures via the
// Mojang sessionserver API, with disk + memory caching. Offline accounts and
// fetch failures fall back to Notch's face. Thread-safe; one background worker
// handles network I/O and PNG decode, the main render thread promotes completed
// surfaces to SDL_Textures via skin_cache_pump().

#ifndef SKIN_CACHE_H
#define SKIN_CACHE_H

#include "settings_utils.h" // AccountType

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// One-time init. Spawns the worker thread, ensures the disk cache dir exists,
// and warms the Notch fallback so it's ready before any lookup. Call after the
// SDL_Renderer is created.
bool skin_cache_init(SDL_Renderer *renderer);

// Joins the worker, frees all cached surfaces/textures. Call before renderer
// destruction.
void skin_cache_shutdown(void);

// Main-thread frame hook. Promotes worker-decoded surfaces into SDL_Textures.
// Cheap when nothing is pending. Must be called from the renderer thread.
void skin_cache_pump(void);

// Get the 8x8 face texture for a player UUID. Returns NULL while the fetch is
// in flight (caller should render a placeholder or nothing). Once available,
// returns the same pointer until shutdown. Safe to call every frame; the cache
// dedupes lookups and only enqueues at most one fetch per UUID per session.
//
// account_type: pass the player's known account type when known (skips the
// network round-trip for offline accounts -> direct Notch fallback). Pass
// ACCOUNT_ONLINE if unknown; an offline UUID will then resolve to Notch via
// the Mojang 404 path (one wasted request the first time).
SDL_Texture *skin_cache_get_face(const char *uuid, AccountType account_type);

#ifdef __cplusplus
}
#endif

#endif // SKIN_CACHE_H
