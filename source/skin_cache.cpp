// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 12.05.2026.

#include "skin_cache.h"
#include "mojang_api.h"
#include "logger.h"
#include "main.h"      // get_resources_path
#include "path_utils.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cerrno>

#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_one(path) _mkdir(path)
#else
#define mkdir_one(path) mkdir((path), 0755)
#endif

#define SKIN_CACHE_MAX_ENTRIES 256
#define SKIN_CACHE_QUEUE_CAP   256
#define NOTCH_UUID             "069a79f4-44e9-4726-a5be-fca90e38aaf5"
#define CACHE_TTL_SECONDS      (24 * 60 * 60)

struct CacheEntry {
    char uuid[48];
    SDL_Texture *texture; // owned by cache; created on main thread
    SDL_Surface *pending_surface; // worker fills; main thread converts and frees
    bool fetch_in_flight;
    bool fetch_failed; // permanent fallback to Notch
    bool is_notch_alias; // entry has been remapped to use Notch's texture
};

struct FetchJob {
    char uuid[48];
    bool offline_known; // skip Mojang call, use Notch directly
};

static SDL_Renderer *g_renderer = nullptr;
static SDL_Mutex *g_cache_mutex = nullptr;
static CacheEntry g_entries[SKIN_CACHE_MAX_ENTRIES];
static int g_entry_count = 0;

static SDL_Mutex *g_queue_mutex = nullptr;
static SDL_Condition *g_queue_cond = nullptr;
static FetchJob g_queue[SKIN_CACHE_QUEUE_CAP];
static int g_queue_head = 0, g_queue_tail = 0;
static SDL_Thread *g_worker = nullptr;
static SDL_AtomicInt g_should_stop;

static char g_cache_dir[MAX_PATH_LENGTH] = {0};

static void build_cache_dir(void) {
    char parent[MAX_PATH_LENGTH];
    snprintf(parent, sizeof(parent), "%s/cache", get_resources_path());
    if (mkdir_one(parent) != 0 && errno != EEXIST) {
        log_message(LOG_ERROR, "[SKIN CACHE] mkdir '%s' failed: errno=%d\n", parent, errno);
    }
    snprintf(g_cache_dir, sizeof(g_cache_dir), "%s/cache/skins", get_resources_path());
    if (mkdir_one(g_cache_dir) != 0 && errno != EEXIST) {
        log_message(LOG_ERROR, "[SKIN CACHE] mkdir '%s' failed: errno=%d\n", g_cache_dir, errno);
    }
}

static void cache_path_for_uuid(const char *uuid, char *out, size_t out_size) {
    snprintf(out, out_size, "%s/%s.png", g_cache_dir, uuid);
}

// Returns -1 if not present.
static int find_entry_locked(const char *uuid) {
    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].uuid, uuid) == 0) return i;
    }
    return -1;
}

static int create_entry_locked(const char *uuid) {
    if (g_entry_count >= SKIN_CACHE_MAX_ENTRIES) return -1;
    CacheEntry *e = &g_entries[g_entry_count];
    memset(e, 0, sizeof(*e));
    strncpy(e->uuid, uuid, sizeof(e->uuid) - 1);
    return g_entry_count++;
}

// Returns true if file exists and its mtime is within TTL.
static bool disk_cache_fresh(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    time_t now = time(nullptr);
    return (now - st.st_mtime) < CACHE_TTL_SECONDS;
}

// Modern skins are 64x64; legacy are 64x32. Face is at (8,8)-(15,15) in both.
// Hat overlay is at (40,8)-(47,15), only meaningful for 64x64. Returns a new
// 8x8 RGBA surface (caller frees).
static SDL_Surface *crop_face_with_hat(SDL_Surface *skin) {
    if (!skin) return nullptr;
    SDL_Surface *converted = SDL_ConvertSurface(skin, SDL_PIXELFORMAT_RGBA32);
    if (!converted) return nullptr;

    SDL_Surface *face = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
    if (!face) {
        SDL_DestroySurface(converted);
        return nullptr;
    }

    const uint32_t *src = (const uint32_t *) converted->pixels;
    uint32_t *dst = (uint32_t *) face->pixels;
    int src_pitch_px = converted->pitch / 4;
    int dst_pitch_px = face->pitch / 4;

    // Base face layer at (8,8)
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            dst[y * dst_pitch_px + x] = src[(8 + y) * src_pitch_px + (8 + x)];
        }
    }

    // Hat overlay at (40,8) is only defined in modern 64x64 skins. Legacy
    // 64x32 skins (Notch's classic, etc.) have no second layer; reading from
    // that region gives undefined bytes (often opaque black) which would
    // overwrite the face with garbage. Gate strictly on full 64x64 dimensions.
    if (converted->h >= 64 && converted->w >= 64) {
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                uint32_t hat_px = src[(8 + y) * src_pitch_px + (40 + x)];
                uint8_t a = (uint8_t) ((hat_px >> 24) & 0xFF);
                if (a == 0) continue;
                if (a == 255) {
                    dst[y * dst_pitch_px + x] = hat_px;
                    continue;
                }
                uint32_t base = dst[y * dst_pitch_px + x];
                uint8_t hr = (uint8_t) (hat_px & 0xFF);
                uint8_t hg = (uint8_t) ((hat_px >> 8) & 0xFF);
                uint8_t hb = (uint8_t) ((hat_px >> 16) & 0xFF);
                uint8_t br = (uint8_t) (base & 0xFF);
                uint8_t bg = (uint8_t) ((base >> 8) & 0xFF);
                uint8_t bb = (uint8_t) ((base >> 16) & 0xFF);
                uint8_t ba = (uint8_t) ((base >> 24) & 0xFF);
                uint8_t r = (uint8_t) ((hr * a + br * (255 - a)) / 255);
                uint8_t g = (uint8_t) ((hg * a + bg * (255 - a)) / 255);
                uint8_t b = (uint8_t) ((hb * a + bb * (255 - a)) / 255);
                uint8_t out_a = ba > a ? ba : a;
                dst[y * dst_pitch_px + x] = (uint32_t) r | ((uint32_t) g << 8) |
                                            ((uint32_t) b << 16) | ((uint32_t) out_a << 24);
            }
        }
    }

    SDL_DestroySurface(converted);
    return face;
}

static SDL_Surface *load_face_from_disk(const char *path) {
    SDL_Surface *s = IMG_Load(path);
    if (!s) return nullptr;
    if (s->w != 8 || s->h != 8) {
        // Stale/corrupt cache file. Caller will refetch.
        SDL_DestroySurface(s);
        return nullptr;
    }
    return s;
}

static bool save_face_to_disk(SDL_Surface *face, const char *path) {
    return IMG_SavePNG(face, path);
}

// Performs the network fetch + decode + crop for one UUID. On success returns
// a new 8x8 RGBA surface. On failure returns nullptr.
static SDL_Surface *fetch_face_from_mojang(const char *uuid) {
    char skin_url[512];
    if (!mojang_fetch_skin_url(uuid, skin_url, sizeof(skin_url))) {
        return nullptr;
    }

    unsigned char *png_bytes = nullptr;
    size_t png_size = 0;
    if (!mojang_download_url(skin_url, &png_bytes, &png_size)) {
        return nullptr;
    }

    SDL_IOStream *io = SDL_IOFromConstMem(png_bytes, png_size);
    SDL_Surface *skin = io ? IMG_Load_IO(io, true) : nullptr;
    free(png_bytes);
    if (!skin) {
        log_message(LOG_ERROR, "[SKIN CACHE] Failed to decode skin PNG for %s.\n", uuid);
        return nullptr;
    }

    SDL_Surface *face = crop_face_with_hat(skin);
    SDL_DestroySurface(skin);
    return face;
}

static void enqueue_locked(const char *uuid, bool offline_known) {
    int next = (g_queue_tail + 1) % SKIN_CACHE_QUEUE_CAP;
    if (next == g_queue_head) return; // queue full; silently drop
    FetchJob *job = &g_queue[g_queue_tail];
    memset(job, 0, sizeof(*job));
    strncpy(job->uuid, uuid, sizeof(job->uuid) - 1);
    job->offline_known = offline_known;
    g_queue_tail = next;
    SDL_SignalCondition(g_queue_cond);
}

static int SDLCALL worker_thread(void *) {
    while (!SDL_GetAtomicInt(&g_should_stop)) {
        FetchJob job;
        SDL_LockMutex(g_queue_mutex);
        while (g_queue_head == g_queue_tail && !SDL_GetAtomicInt(&g_should_stop)) {
            SDL_WaitCondition(g_queue_cond, g_queue_mutex);
        }
        if (SDL_GetAtomicInt(&g_should_stop)) {
            SDL_UnlockMutex(g_queue_mutex);
            break;
        }
        job = g_queue[g_queue_head];
        g_queue_head = (g_queue_head + 1) % SKIN_CACHE_QUEUE_CAP;
        SDL_UnlockMutex(g_queue_mutex);

        char path[MAX_PATH_LENGTH];
        cache_path_for_uuid(job.uuid, path, sizeof(path));

        SDL_Surface *result = nullptr;

        // Disk cache hit (fresh) - just load and return.
        if (disk_cache_fresh(path)) {
            result = load_face_from_disk(path);
        }

        // Stale or missing - decide between Mojang fetch and Notch fallback.
        if (!result) {
            if (job.offline_known) {
                // Offline account: alias to Notch entry. The main thread will
                // notice fetch_failed + is_notch_alias and pick up Notch's
                // texture next pump.
                SDL_LockMutex(g_cache_mutex);
                int idx = find_entry_locked(job.uuid);
                if (idx >= 0) {
                    g_entries[idx].fetch_in_flight = false;
                    g_entries[idx].fetch_failed = true;
                    g_entries[idx].is_notch_alias = true;
                }
                SDL_UnlockMutex(g_cache_mutex);
                continue;
            }

            result = fetch_face_from_mojang(job.uuid);
            if (result) {
                if (!save_face_to_disk(result, path)) {
                    log_message(LOG_ERROR, "[SKIN CACHE] Failed to save %s: %s\n",
                                path, SDL_GetError());
                } else {
                    log_message(LOG_INFO, "[SKIN CACHE] Fetched + saved face for %s\n", job.uuid);
                }
            } else {
                // Mojang failed (probably offline UUID 404 or network down).
                // Mark as fallback to Notch.
                SDL_LockMutex(g_cache_mutex);
                int idx = find_entry_locked(job.uuid);
                if (idx >= 0) {
                    g_entries[idx].fetch_in_flight = false;
                    g_entries[idx].fetch_failed = true;
                    g_entries[idx].is_notch_alias = strcmp(job.uuid, NOTCH_UUID) != 0;
                }
                SDL_UnlockMutex(g_cache_mutex);
                continue;
            }
        }

        // Hand the surface to the main thread for SDL_Texture creation.
        SDL_LockMutex(g_cache_mutex);
        int idx = find_entry_locked(job.uuid);
        if (idx >= 0) {
            if (g_entries[idx].pending_surface) {
                SDL_DestroySurface(g_entries[idx].pending_surface);
            }
            g_entries[idx].pending_surface = result;
            g_entries[idx].fetch_in_flight = false;
        } else {
            SDL_DestroySurface(result);
        }
        SDL_UnlockMutex(g_cache_mutex);
    }
    return 0;
}

bool skin_cache_init(SDL_Renderer *renderer) {
    if (!renderer) return false;
    g_renderer = renderer;

    build_cache_dir();

    g_cache_mutex = SDL_CreateMutex();
    g_queue_mutex = SDL_CreateMutex();
    g_queue_cond = SDL_CreateCondition();
    if (!g_cache_mutex || !g_queue_mutex || !g_queue_cond) return false;

    SDL_SetAtomicInt(&g_should_stop, 0);

    g_worker = SDL_CreateThread(worker_thread, "skin_cache_worker", nullptr);
    if (!g_worker) {
        log_message(LOG_ERROR, "[SKIN CACHE] Failed to start worker thread.\n");
        return false;
    }

    // Pre-warm Notch so the fallback is ready before any lookup needs it.
    SDL_LockMutex(g_cache_mutex);
    int idx = create_entry_locked(NOTCH_UUID);
    if (idx >= 0) g_entries[idx].fetch_in_flight = true;
    SDL_UnlockMutex(g_cache_mutex);

    SDL_LockMutex(g_queue_mutex);
    enqueue_locked(NOTCH_UUID, false);
    SDL_UnlockMutex(g_queue_mutex);

    log_message(LOG_INFO, "[SKIN CACHE] Initialized at %s\n", g_cache_dir);
    return true;
}

void skin_cache_shutdown(void) {
    if (!g_worker) return;

    SDL_SetAtomicInt(&g_should_stop, 1);
    SDL_LockMutex(g_queue_mutex);
    SDL_BroadcastCondition(g_queue_cond);
    SDL_UnlockMutex(g_queue_mutex);

    SDL_WaitThread(g_worker, nullptr);
    g_worker = nullptr;

    for (int i = 0; i < g_entry_count; i++) {
        if (g_entries[i].pending_surface) SDL_DestroySurface(g_entries[i].pending_surface);
        // Texture-aliased entries share the Notch texture; only destroy once.
        if (g_entries[i].texture && !g_entries[i].is_notch_alias) {
            SDL_DestroyTexture(g_entries[i].texture);
        }
        memset(&g_entries[i], 0, sizeof(g_entries[i]));
    }
    g_entry_count = 0;

    if (g_cache_mutex) {
        SDL_DestroyMutex(g_cache_mutex);
        g_cache_mutex = nullptr;
    }
    if (g_queue_mutex) {
        SDL_DestroyMutex(g_queue_mutex);
        g_queue_mutex = nullptr;
    }
    if (g_queue_cond) {
        SDL_DestroyCondition(g_queue_cond);
        g_queue_cond = nullptr;
    }
    g_renderer = nullptr;
}

// Find Notch's texture (if loaded yet). Caller must hold g_cache_mutex.
static SDL_Texture *notch_texture_locked(void) {
    int idx = find_entry_locked(NOTCH_UUID);
    if (idx < 0) return nullptr;
    return g_entries[idx].texture;
}

void skin_cache_pump(void) {
    if (!g_renderer || !g_cache_mutex) return;
    SDL_LockMutex(g_cache_mutex);
    for (int i = 0; i < g_entry_count; i++) {
        CacheEntry *e = &g_entries[i];

        // Promote any pending surface into a texture.
        if (e->pending_surface) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, e->pending_surface);
            SDL_DestroySurface(e->pending_surface);
            e->pending_surface = nullptr;
            if (tex) {
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
                if (e->texture && !e->is_notch_alias) SDL_DestroyTexture(e->texture);
                e->texture = tex;
                e->is_notch_alias = false;
            }
        }

        // Fold in Notch alias once Notch itself has loaded.
        if (e->fetch_failed && e->is_notch_alias && !e->texture) {
            SDL_Texture *notch = notch_texture_locked();
            if (notch) e->texture = notch;
        }
    }
    SDL_UnlockMutex(g_cache_mutex);
}

SDL_Texture *skin_cache_get_face(const char *uuid, AccountType account_type) {
    if (!uuid || !uuid[0] || !g_cache_mutex) return nullptr;

    SDL_LockMutex(g_cache_mutex);
    int idx = find_entry_locked(uuid);
    if (idx < 0) {
        idx = create_entry_locked(uuid);
        if (idx < 0) {
            SDL_UnlockMutex(g_cache_mutex);
            return nullptr;
        }
        g_entries[idx].fetch_in_flight = true;
        SDL_UnlockMutex(g_cache_mutex);

        SDL_LockMutex(g_queue_mutex);
        enqueue_locked(uuid, account_type == ACCOUNT_OFFLINE);
        SDL_UnlockMutex(g_queue_mutex);
        return nullptr;
    }

    SDL_Texture *tex = g_entries[idx].texture;
    SDL_UnlockMutex(g_cache_mutex);
    return tex;
}
