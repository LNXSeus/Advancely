// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 24.06.2025.
//

#include "overlay.h" // Has tracker.h
#include "init_sdl.h"
#include "settings_utils.h"
#include "format_utils.h"
#include "logger.h"
#include "supporters.h"
#include "skin_cache.h" // Co-op contributor faces in Compact mode

#include <cstdio>
#include <cstdlib>
#include <cmath> // Required for roundf()
#include <string>
#include <vector> // Required for collecting items to render
#include <algorithm> // Required for std::reverse
#include <unordered_map> // Compact pop-out stack: previous-state snapshot for the diff engine

#define SOCIAL_CYCLE_SECONDS 15.0f

// Minimum width of the auto-fitted Compact-mode window. A small counter panel would otherwise
// make the window narrower than its "Advancely Overlay" title bar; this lower bound keeps the
// title fully visible. Tune by trial and error - the panel stays centered within this width.
#define COMPACT_MIN_WINDOW_WIDTH 440

// A goal marked hidden in the template is normally excluded from every overlay mode; the "Show Hidden
// Goals" setting overrides that. Returns true only when a goal should still be treated as hidden
// (hidden AND the override is off), so exclusion checks read `... || goal_is_hidden(x->is_hidden, s)`.
static inline bool goal_is_hidden(bool is_hidden, const AppSettings *settings) {
    return is_hidden && !settings->overlay_show_hidden_goals;
}

// TODO: Add more socials here
const char *SOCIALS[] = {
    "Advancely " ADVANCELY_VERSION "!",
    "Advance to the",
    "next level with",
    "Advancely!",
    "Download Advancely at",
    "github.com/LNXSeus/Advancely",
    "Support LNXS on",
    "youtube.com/@lnxs",
    "Support LNXS on",
    "twitch.tv/lnxseus",
    "Support LNXS on",
    "youtube.com/@lnxsarchive",
    "Support LNXS on",
    "discord.gg/TyNgXDz",
    "Support Advancely on",
    "streamlabs.com/lnxseus/tip",
};
const int NUM_SOCIALS = sizeof(SOCIALS) / sizeof(char *);

// --- Supporter Showcase for Completed Runs ---
// Icons used in the supporter showcase. The supporter list itself is in supporters.h.
const char *SUPPORTER_ICONS[] = {
    "emotes/glorpLove-4x.png",
    "emotes/Lnxseuheart.png",
    "emotes/Love_emote.png",
    "emotes/luvv-4x.png",
    "emotes/poggSpin-4x_unoptimized.gif",
    "emotes/peepoLove-4x.png",
    "emotes/catHeart-4x_unoptimized.gif",
    "emotes/doorLove-4x_unoptimized.gif",
    "emotes/CatBop-4x_unoptimized.gif",
    "emotes/Clap-4x_unoptimized.gif",
    "emotes/Clap2-4x_unoptimized.gif",
    "emotes/Dance-4x_unoptimized.gif",
    "emotes/FeelsStrongJAM-4x_unoptimized.gif",
    "emotes/HappiJamW-4x_unoptimized.gif",
    "emotes/Jammies-4x_unoptimized.gif",
    "emotes/LETSGO-4x_unoptimized.gif",
    "emotes/OtterSpin-4x_unoptimized.gif",
    "emotes/POGGIES-4x_unoptimized.gif",
    "emotes/PagBounce-4x_unoptimized.gif",
    "emotes/PepePls-4x_unoptimized.gif",
    "emotes/Pog-4x_unoptimized.gif",
    "emotes/RainbowPls-4x_unoptimized.gif",
    "emotes/WW-4x_unoptimized.gif",
    "emotes/YIPPEE-4x_unoptimized.gif",
    "emotes/agahappi-4x_unoptimized.gif",
    "emotes/beeBobble-4x_unoptimized.gif",
    "emotes/catClap-4x_unoptimized.gif",
    "emotes/catJam-4x_unoptimized.gif",
    "emotes/catKISS-4x_unoptimized.gif",
    "emotes/catRave-4x_unoptimized.gif",
    "emotes/catRock-4x_unoptimized.gif",
    "emotes/clapp-4x_unoptimized.gif",
    "emotes/clappyclap-4x_unoptimized.gif",
    "emotes/cool-4x_unoptimized.gif",
    "emotes/dogDance-4x_unoptimized.gif",
    "emotes/duckDance-4x_unoptimized.gif",
    "emotes/gettingjiggywithit-4x_unoptimized.gif",
    "emotes/gg-4x_unoptimized.gif",
    "emotes/gooseJAM-4x_unoptimized.gif",
    "emotes/happi_unoptimized.gif",
    "emotes/happicat-4x_unoptimized.gif",
    "emotes/happie-4x_unoptimized.gif",
    "emotes/jamgie-4x_unoptimized.gif",
    "emotes/jamm-4x_unoptimized.gif",
    "emotes/kittyJam-4x_unoptimized.gif",
    "emotes/peepoCheer-4x_unoptimized.gif",
    "emotes/peepoCheer2-4x_unoptimized.gif",
    "emotes/peepoHappier-4x_unoptimized.gif",
    "emotes/pepeDS-4x_unoptimized.gif",
    "emotes/piglinJAM-4x_unoptimized.gif",
    "emotes/popCat-4x_unoptimized.gif",
    "emotes/ratJAM-4x_unoptimized.gif",
    "emotes/ratJAMJAM-4x_unoptimized.gif",
    "emotes/walterJAM-4x_unoptimized.gif",
    "emotes/wowie-4x_unoptimized.gif"
};
const int NUM_SUPPORTER_ICONS = sizeof(SUPPORTER_ICONS) / sizeof(char *);

// A structure to hold the pre-calculated rendering info for each supporter.
typedef struct {
    const Supporter *supporter;
    const char *icon_path;
    SDL_Texture *background_static;
    AnimatedTexture *background_anim;
} SupporterRenderInfo;

/**
 * @brief Helper function for text caching to improve performance.
 * @param o The Overlay instance
 * @param font The font to render with (top bar and rows may use different sizes)
 * @param text The text to cache
 * @param color The color of the text
 * @return The SDL_Texture of the cached text
 */
// Upper bound on cached text textures. Volatile strings (the IGT/timer in the top
// info bar change every frame) would otherwise grow this without limit; once the cap
// is hit the least-recently-used entry is evicted so memory stays bounded.
#define MAX_TEXT_CACHE_ENTRIES 512

static SDL_Texture *get_text_texture_from_cache(Overlay *o, TTF_Font *font, const char *text, SDL_Color color) {
    if (!text || text[0] == '\0') {
        return nullptr;
    }

    Uint32 now = SDL_GetTicks();

    // 1. Check if the texture is already in the cache
    for (int i = 0; i < o->text_cache_count; i++) {
        TextCacheEntry *entry = &o->text_cache[i];
        if (entry->font == font && strcmp(entry->text, text) == 0 &&
            entry->color.r == color.r && entry->color.g == color.g &&
            entry->color.b == color.b && entry->color.a == color.a) {
            entry->last_used = now;
            return entry->texture;
        }
    }

    // 2. If not in cache, create it and add it
    SDL_Surface *text_surface = TTF_RenderText_Blended(font, text, 0, color);
    if (!text_surface) {
        return nullptr;
    }
    SDL_Texture *text_texture = SDL_CreateTextureFromSurface(o->renderer, text_surface);
    SDL_DestroySurface(text_surface);
    if (!text_texture) {
        return nullptr;
    }
    SDL_SetTextureScaleMode(text_texture, SDL_SCALEMODE_NEAREST);

    // 3. Pick a slot: reuse the least-recently-used one at the cap, else append
    // (growing the array, but never past the cap).
    TextCacheEntry *slot;
    if (o->text_cache_count >= MAX_TEXT_CACHE_ENTRIES) {
        int lru = 0;
        for (int i = 1; i < o->text_cache_count; i++) {
            if (o->text_cache[i].last_used < o->text_cache[lru].last_used) lru = i;
        }
        if (o->text_cache[lru].texture) SDL_DestroyTexture(o->text_cache[lru].texture);
        slot = &o->text_cache[lru];
    } else {
        if (o->text_cache_count >= o->text_cache_capacity) {
            int new_capacity = o->text_cache_capacity == 0 ? 32 : o->text_cache_capacity * 2;
            if (new_capacity > MAX_TEXT_CACHE_ENTRIES) new_capacity = MAX_TEXT_CACHE_ENTRIES;
            auto *new_cache = (TextCacheEntry *) realloc(o->text_cache, new_capacity * sizeof(TextCacheEntry));
            if (!new_cache) {
                SDL_DestroyTexture(text_texture);
                return nullptr;
            }
            o->text_cache = new_cache;
            o->text_cache_capacity = new_capacity;
        }
        slot = &o->text_cache[o->text_cache_count++];
    }

    strncpy(slot->text, text, sizeof(slot->text) - 1);
    slot->text[sizeof(slot->text) - 1] = '\0';
    slot->color = color;
    slot->font = font; // Was previously never stored, so lookups always missed and leaked a texture per frame.
    slot->texture = text_texture;
    slot->last_used = now;

    return slot->texture;
}

static inline float snap_px(float v) {
    return roundf(v);
}

// A row scrolls at its own speed when the per-row custom speed is enabled,
// otherwise it falls back to the global overlay scroll speed.
static inline float effective_scroll_speed(bool custom_enabled, float custom_speed, float global_speed) {
    return custom_enabled ? custom_speed : global_speed;
}

// --- Scrolling conveyor belt ---------------------------------------------
// A row of fixed-width tiles that scrolls horizontally. Each tile holds a
// stable item index (into the row's ordered item list) or -1 for a gap. When
// an item is cleared its on-screen tiles turn into gaps that quietly ride off
// the exit edge while fresh items keep entering the spawn edge, so nothing ever
// jumps. If only a few items remain the belt repeats, so one cleared item can
// leave several gaps at once. Identity is by index (pointers are not stable
// across IPC rebuilds); a signature of the item ids resets the belt when the
// template itself changes.
struct ScrollBelt {
    std::vector<int> tiles; // item indices left->right; -1 = gap
    float head_x = 0.0f;    // left edge x of tiles.front()
    float prev_offset = 0.0f;
    int head_idx = -1;      // real item index seeding left (prepend) spawns
    int tail_idx = -1;      // real item index seeding right (append) spawns
    unsigned long long signature = 0;
    int flow = 0;           // +1 = scrolls right, -1 = scrolls left, 0 = uninit
    bool init = false;

    // Clear (crop) animation: seconds elapsed since each item was cleared, so a
    // tile can shrink away in place before it becomes a gap.
    std::vector<float> clear_elapsed;
    std::vector<char> was_removed;
    Uint32 anim_prev = 0;
};

// One tile to draw this frame. clear is 0 for a normal item, or (0,1] while the
// item is animating out (the fraction already cropped away).
struct BeltTile {
    int idx;     // item index, or -1 for a gap
    float x;     // left edge
    float clear; // 0 = full, 1 = fully cropped
};

// Next/previous not-removed index, searched cyclically from `from` (exclusive).
static int belt_step_active(int from, int dir, int F, const std::vector<char> &removed) {
    for (int s = 1; s <= F; ++s) {
        int i = ((from + dir * s) % F + F) % F;
        if (!removed[i]) return i;
    }
    return -1;
}

// Advances the belt one frame and fills `out` covering [left_bound, right_bound].
// scroll_offset drives motion (its delta is the per-frame pixel movement); its
// fractional part is preserved so every row snaps to whole pixels in sync.
// A cleared item keeps its tiles (cropping over `duration` seconds) before they
// turn into gaps; duration <= 0 clears instantly.
static void belt_update(ScrollBelt &b, float scroll_offset, float iw,
                        float left_bound, float right_bound,
                        int F, const std::vector<char> &removed, float duration,
                        bool flow_right, unsigned long long signature,
                        std::vector<BeltTile> &out) {
    out.clear();
    if (F <= 0 || iw <= 0.0f) {
        b.tiles.clear();
        b.init = false;
        return;
    }

    int flow = flow_right ? 1 : -1;
    // Template-index step that corresponds to moving one tile to the right along
    // the belt. When scrolling right the spawn edge is the left, so the belt is
    // laid out in reverse order; that keeps items appearing in template order for
    // both scroll directions.
    int order = flow_right ? -1 : 1;
    float frac = scroll_offset - floorf(scroll_offset);

    // Reset on first use, template change, direction change or item-count change.
    if (!b.init || b.signature != signature || b.flow != flow || (int) b.clear_elapsed.size() != F) {
        b.tiles.clear();
        b.head_x = left_bound + frac;
        b.prev_offset = scroll_offset;
        b.head_idx = b.tail_idx = -1;
        b.signature = signature;
        b.flow = flow;
        b.clear_elapsed.assign((size_t) F, 0.0f);
        b.was_removed.assign((size_t) F, 0);
        // Treat items that are already cleared as fully gone so they don't play
        // the crop animation on startup or after a template change.
        for (int i = 0; i < F; ++i) {
            if (removed[i]) { b.was_removed[i] = 1; b.clear_elapsed[i] = duration; }
        }
        b.anim_prev = SDL_GetTicks();
        b.init = true;
    }

    // Advance the per-item clear timers and decide which items are fully gone.
    Uint32 now = SDL_GetTicks();
    float adt = (float) (now - b.anim_prev) / 1000.0f;
    b.anim_prev = now;
    if (adt < 0.0f) adt = 0.0f;
    if (adt > 0.25f) adt = 0.25f; // ignore long stalls (window minimized, etc.)

    std::vector<char> gone((size_t) F);
    for (int i = 0; i < F; ++i) {
        if (removed[i]) {
            if (!b.was_removed[i]) b.clear_elapsed[i] = 0.0f; // just cleared
            else b.clear_elapsed[i] += adt;
            gone[i] = (duration <= 0.0f || b.clear_elapsed[i] >= duration) ? 1 : 0;
        } else {
            b.clear_elapsed[i] = 0.0f;
            gone[i] = 0;
        }
        b.was_removed[i] = removed[i];
    }

    // First spawnable (never-cleared) item.
    int seed = -1;
    for (int i = 0; i < F; ++i) {
        if (!removed[i]) { seed = i; break; }
    }

    // Move with the scroll, then turn fully-cleared tiles into gaps.
    b.head_x += scroll_offset - b.prev_offset;
    b.prev_offset = scroll_offset;
    for (int &t: b.tiles) {
        if (t >= 0 && gone[t]) t = -1;
    }

    // Drop tiles that have fully left either edge.
    while (!b.tiles.empty() && b.head_x + iw <= left_bound) {
        b.tiles.erase(b.tiles.begin());
        b.head_x += iw;
    }
    while (!b.tiles.empty() && b.head_x + (float) (b.tiles.size() - 1) * iw >= right_bound) {
        b.tiles.pop_back();
    }

    // Only spawn / extend when there is a not-cleared item to pull from. Tiles
    // still animating out are kept even when nothing spawnable remains.
    if (seed >= 0) {
        if (b.tiles.empty()) {
            b.head_x = left_bound + frac;
            b.tiles.push_back(seed);
            b.head_idx = b.tail_idx = seed;
        }

        // Keep the spawn seeds pointing at not-cleared items.
        if (b.head_idx < 0 || removed[b.head_idx]) {
            int real = seed;
            for (int t: b.tiles) { if (t >= 0 && !removed[t]) { real = t; break; } }
            b.head_idx = real;
        }
        if (b.tail_idx < 0 || removed[b.tail_idx]) {
            int real = seed;
            for (auto it = b.tiles.rbegin(); it != b.tiles.rend(); ++it) {
                if (*it >= 0 && !removed[*it]) { real = *it; break; }
            }
            b.tail_idx = real;
        }

        // Extend on both edges to cover the screen (only the spawn edge grows
        // during steady scrolling; both fill on startup).
        while (b.head_x + (float) b.tiles.size() * iw < right_bound) {
            int ni = belt_step_active(b.tail_idx, order, F, removed);
            if (ni < 0) break;
            b.tiles.push_back(ni);
            b.tail_idx = ni;
        }
        while (b.head_x > left_bound) {
            int pi = belt_step_active(b.head_idx, -order, F, removed);
            if (pi < 0) break;
            b.tiles.insert(b.tiles.begin(), pi);
            b.head_idx = pi;
            b.head_x -= iw;
        }
    }

    out.reserve(b.tiles.size());
    for (size_t j = 0; j < b.tiles.size(); ++j) {
        int idx = b.tiles[j];
        float clear = 0.0f;
        if (idx >= 0 && removed[idx] && duration > 0.0f) {
            clear = b.clear_elapsed[idx] / duration;
            if (clear < 0.0f) clear = 0.0f;
            if (clear > 1.0f) clear = 1.0f;
        }
        out.push_back({idx, b.head_x + (float) j * iw, clear});
    }
}

// Sets a renderer clip so an item being cleared is cropped vertically over a
// [band_top, band_bottom] strip. clear in (0,1] is the fraction removed; a
// positive animation setting clears upwards (keeps the top), negative clears
// downwards (keeps the bottom). Returns true if a clip was set (reset it after).
static bool belt_set_clear_clip(SDL_Renderer *r, int window_w, float clear,
                                float band_top, float band_bottom, float anim_sign) {
    if (clear <= 0.0f) return false;
    float H = band_bottom - band_top;
    float vis_h = (1.0f - clear) * H;
    SDL_Rect clip;
    clip.x = 0;
    clip.w = window_w;
    if (anim_sign >= 0.0f) {
        clip.y = (int) floorf(band_top);
        clip.h = (int) ceilf(vis_h);
    } else {
        clip.y = (int) floorf(band_top + clear * H);
        clip.h = (int) ceilf(vis_h);
    }
    SDL_SetRenderClipRect(r, &clip);
    return true;
}

// Stable unique id for a row 2/3 display item, used to build the belt signature.
static const char *overlay_item_root(const OverlayDisplayItem &di) {
    switch (di.type) {
        case OverlayDisplayItem::ADVANCEMENT:
        case OverlayDisplayItem::STAT:
            return static_cast<TrackableCategory *>(di.item_ptr)->root_name;
        case OverlayDisplayItem::UNLOCK:
        case OverlayDisplayItem::CUSTOM:
            return static_cast<TrackableItem *>(di.item_ptr)->root_name;
        case OverlayDisplayItem::MULTISTAGE:
            return static_cast<MultiStageGoal *>(di.item_ptr)->root_name;
        case OverlayDisplayItem::COUNTER:
            return static_cast<CounterGoal *>(di.item_ptr)->root_name;
    }
    return "";
}

// When a row is set to freeze once its items fit, lay them out statically instead
// of scrolling: each still-visible item is drawn once, in template order, aligned
// within the window. `iw` is the per-item cell + spacing (matching the belt), and
// `cell` is the item cell width (icon or text cell) used to size the content block.
// Returns true and fills `out` when the row should be frozen (feature enabled and
// all visible items fit); otherwise returns false and leaves `out` untouched so the
// caller falls back to the scrolling belt.
static bool freeze_layout(bool freeze_enabled, OverlayProgressTextAlignment align,
                          int window_w, float iw, float cell,
                          int F, const std::vector<char> &removed,
                          std::vector<BeltTile> &out) {
    if (!freeze_enabled) return false;

    int visible = 0;
    for (int i = 0; i < F; i++) if (!removed[i]) visible++;
    if (visible <= 0) return false;

    // Total width of the visible items laid out edge to edge (no trailing gap).
    float content_width = (float) (visible - 1) * iw + cell;
    // Reserve the same small edge padding as the top info bar on both sides so a
    // left- or right-aligned item never sits flush with (or gets clipped by) the
    // window border. The fit test accounts for it too, so freezing only kicks in
    // once the items fit *with* the padding.
    const float edge_padding = 10.0f;
    if (content_width > (float) window_w - 2.0f * edge_padding) return false; // too wide: keep scrolling

    float start_x;
    if (align == OVERLAY_PROGRESS_TEXT_ALIGN_CENTER)
        start_x = ((float) window_w - content_width) / 2.0f;
    else if (align == OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT)
        start_x = (float) window_w - content_width - edge_padding;
    else
        start_x = edge_padding;

    out.clear();
    int slot = 0;
    for (int i = 0; i < F; i++) {
        if (removed[i]) continue;
        out.push_back({i, snap_px(start_x + (float) slot * iw), 0.0f});
        slot++;
    }
    return true;
}

// --- Page mode -----------------------------------------------------------
// A static, centered slice of items that flips to the next slice like the pages
// of a book (a sharp cut, no scrolling). A page holds as many items as fit the
// window width; the still-visible items are laid out once, centered. Items that
// clear while a page is showing crop away in place and leave a gap for the rest
// of that page; the next page is recomputed fresh so the gap disappears. Shares
// BeltTile with the belt so the same draw loop renders both modes. Kept separate
// from ScrollBelt so more layout modes can be added the same way later.
struct PageView {
    unsigned long long signature = 0;
    int page_offset = 0;   // start index into the not-removed item list for the current page
    int last_page = -1;    // shared page index (Overlay::page_index) last snapped at
    bool init = false;

    std::vector<int> tiles; // item indices for the current page in template order (fixed slots; -1 handled at draw)

    // Per-item clear (crop) timers, mirroring ScrollBelt so a completed item can
    // shrink away in place before its slot becomes a gap.
    std::vector<float> clear_elapsed;
    std::vector<char> was_removed;
    Uint32 anim_prev = 0;
};

// How many items fit on one page given the per-item stride `iw` and the widest
// item cell `cell`. A page of n items spans (n-1)*iw + cell (no trailing spacing);
// page_update then centers that, splitting whatever is left over into equal side
// margins. So we fit against nearly the full window width and keep only a tiny edge
// guard - reserving more here would drop a goal that actually fits and inflate the
// side margins by up to a whole item's stride. Always >= 1.
static int page_capacity(int window_w, float iw, float cell) {
    if (iw <= 0.0f) return 1;
    const float edge_guard = 2.0f; // keeps a full page a hair off the window edges
    float avail = (float) window_w - 2.0f * edge_guard;
    int n = 1 + (int) floorf((avail - cell) / iw);
    if (n < 1) n = 1;
    return n;
}

// Rebuild the current page's tile snapshot from the not-removed items in template
// order. Without repeat the page is the slice [page_offset, page_offset + per_page)
// and page_offset wraps back to the start once it runs past the end, so paging
// cycles like a book (the last page may be partial). With repeat the page is always
// exactly per_page tiles, cycling through the items so it is never partial.
static void page_snapshot(PageView &p, int per_page, bool repeat, int F, const std::vector<char> &removed) {
    std::vector<int> active;
    active.reserve((size_t) F);
    for (int i = 0; i < F; i++) if (!removed[i]) active.push_back(i);

    p.tiles.clear();
    if (active.empty()) { p.page_offset = 0; return; }
    int n = (int) active.size();

    if (repeat) {
        p.page_offset %= n;
        if (p.page_offset < 0) p.page_offset = 0;
        for (int k = 0; k < per_page; k++) {
            p.tiles.push_back(active[(p.page_offset + k) % n]);
        }
    } else {
        if (p.page_offset >= n) p.page_offset = 0;
        for (int k = 0; k < per_page && p.page_offset + k < n; k++) {
            p.tiles.push_back(active[p.page_offset + k]);
        }
    }
}

// Advances the page view one frame and fills `out` with the current page's tiles,
// centered within the window. `page_index` is the shared page counter (advanced by
// the interval timer or by SPACE); the page flips whenever it changes. A cleared
// item keeps its slot (cropping over `duration` seconds) before turning into a gap;
// duration <= 0 clears instantly.
static void page_update(PageView &p, int page_index, OverlayProgressTextAlignment align, bool repeat,
                        int window_w, float iw, float cell,
                        int F, const std::vector<char> &removed, float duration,
                        unsigned long long signature, std::vector<BeltTile> &out) {
    out.clear();
    if (F <= 0 || iw <= 0.0f) {
        p.tiles.clear();
        p.init = false;
        return;
    }

    int per_page = page_capacity(window_w, iw, cell);
    Uint32 now = SDL_GetTicks();

    // Reset on first use, template change or item-count change.
    if (!p.init || p.signature != signature || (int) p.clear_elapsed.size() != F) {
        p.signature = signature;
        p.page_offset = 0;
        p.clear_elapsed.assign((size_t) F, 0.0f);
        p.was_removed.assign((size_t) F, 0);
        // Items already cleared start fully gone so they never animate on startup.
        for (int i = 0; i < F; i++) {
            if (removed[i]) { p.was_removed[i] = 1; p.clear_elapsed[i] = duration; }
        }
        p.anim_prev = now;
        p.last_page = page_index;
        page_snapshot(p, per_page, repeat, F, removed);
        p.init = true;
    }

    // Advance the per-item clear timers (crop animation for items completed while shown).
    float adt = (float) (now - p.anim_prev) / 1000.0f;
    p.anim_prev = now;
    if (adt < 0.0f) adt = 0.0f;
    if (adt > 0.25f) adt = 0.25f; // ignore long stalls (window minimized, etc.)
    for (int i = 0; i < F; i++) {
        if (removed[i]) {
            if (!p.was_removed[i]) p.clear_elapsed[i] = 0.0f; // just cleared
            else p.clear_elapsed[i] += adt;
        } else {
            p.clear_elapsed[i] = 0.0f;
        }
        p.was_removed[i] = removed[i];
    }

    // Flip to the next page whenever the shared index changes (sharp cut), advanced
    // by the interval timer or by SPACE. Every row reads the same index, so they all
    // switch on the same frame.
    if (page_index != p.last_page) {
        p.page_offset += per_page; // wraps inside page_snapshot
        page_snapshot(p, per_page, repeat, F, removed);
        p.last_page = page_index;
    }

    int slots = (int) p.tiles.size();
    if (slots <= 0) return;

    // The slot count is fixed for the page's lifetime, so a cleared item leaving a
    // gap does not shift the remaining items. A not-full page is aligned relative to
    // where a *full* page would sit, so the left padding stays consistent as the page
    // empties (Left), the items stay centered (Center), or they push to a full page's
    // right edge (Right). A full page makes all three coincide.
    float content_width = (float) (slots - 1) * iw + cell;
    float full_width = (float) (per_page - 1) * iw + cell;
    float left_margin = ((float) window_w - full_width) / 2.0f;
    float start_x;
    if (align == OVERLAY_PROGRESS_TEXT_ALIGN_CENTER)
        start_x = ((float) window_w - content_width) / 2.0f;
    else if (align == OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT)
        start_x = left_margin + (full_width - content_width);
    else
        start_x = left_margin;

    out.reserve((size_t) slots);
    for (int k = 0; k < slots; k++) {
        int idx = p.tiles[k];
        float x = snap_px(start_x + (float) k * iw);
        float clear = 0.0f;
        if (idx >= 0 && removed[idx]) {
            if (duration > 0.0f) {
                clear = p.clear_elapsed[idx] / duration;
                if (clear < 0.0f) clear = 0.0f;
                if (clear > 1.0f) clear = 1.0f;
            }
            // Once fully cropped (or instant clear) the slot becomes a gap.
            if (duration <= 0.0f || p.clear_elapsed[idx] >= duration) idx = -1;
        }
        out.push_back({idx, x, clear});
    }
}

/** @brief Helper function to render a texture (static or animated) with alpha modulation
 * It also corrects the aspect ratio of the .png textures.
 *
 * @param renderer The SDL_Renderer to render the texture on.
 * @param texture The SDL_Texture to render.
 * @param anim_texture The AnimatedTexture to render.
 * @param dest The destination rectangle for the texture.
 * @param alpha The alpha value to apply to the texture.
 *
 */
static void render_texture_with_alpha(SDL_Renderer *renderer, SDL_Texture *texture, AnimatedTexture *anim_texture,
                                      const SDL_FRect *dest, Uint8 alpha) {
    SDL_Texture *texture_to_render = nullptr;
    bool is_animated = false;

    if (anim_texture && anim_texture->frame_count > 0) {
        is_animated = true;
        if (anim_texture->delays && anim_texture->total_duration > 0) {
            Uint32 elapsed_time = SDL_GetTicks() % anim_texture->total_duration;
            int current_frame = 0;
            Uint32 time_sum = 0;
            for (int frame_idx = 0; frame_idx < anim_texture->frame_count; ++frame_idx) {
                time_sum += anim_texture->delays[frame_idx];
                if (elapsed_time < time_sum) {
                    current_frame = frame_idx;
                    break;
                }
            }
            texture_to_render = anim_texture->frames[current_frame];
        } else {
            texture_to_render = anim_texture->frames[0];
        }
    } else if (texture) {
        texture_to_render = texture;
    }

    if (texture_to_render) {
        // Reset texture color modulation to white (no tint)
        SDL_SetTextureColorMod(texture_to_render, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture_to_render, alpha);

        // Aspect ratio correction for .png files
        if (!is_animated) {
            // This is a static texture (.png), so we correct its aspect ratio.
            // Animated textures (.gif) are already padded to be square at load time.
            float tex_w, tex_h;
            SDL_GetTextureSize(texture_to_render, &tex_w, &tex_h);

            // Calculate the best scale to fit the texture within the destination box
            float scale_factor = fminf(dest->w / tex_w, dest->h / tex_h);

            // Calculate the new dimensions of the texture
            float scaled_w = tex_w * scale_factor;
            float scaled_h = tex_h * scale_factor;

            // Calculate padding to center the texture inside the destination box
            float pad_x = snap_px((dest->w - scaled_w) / 2.0f);
            float pad_y = snap_px((dest->h - scaled_h) / 2.0f);

            // Create the final, centered, and correctly scaled destination rectangle
            SDL_FRect final_dest = {dest->x + pad_x, dest->y + pad_y, scaled_w, scaled_h};
            SDL_RenderTexture(renderer, texture_to_render, nullptr, &final_dest);
        } else {
            // For animated textures, render stretched as before, since they are pre-padded.
            SDL_RenderTexture(renderer, texture_to_render, nullptr, dest);
        }

        SDL_SetTextureAlphaMod(texture_to_render, 255); // Reset for other render calls
    }
}


// Returns the animated texture's current frame based on the elapsed time, mirroring the frame
// selection in render_texture_with_alpha. Used so a .gif panel can be 9-sliced frame by frame.
static SDL_Texture *anim_current_frame(AnimatedTexture *anim) {
    if (!anim || anim->frame_count <= 0) return nullptr;
    if (anim->delays && anim->total_duration > 0) {
        Uint32 elapsed = SDL_GetTicks() % anim->total_duration;
        Uint32 sum = 0;
        for (int i = 0; i < anim->frame_count; ++i) {
            sum += anim->delays[i];
            if (elapsed < sum) return anim->frames[i];
        }
    }
    return anim->frames[0];
}

// Draws a 9-slice (9-patch) texture stretched to `dest`. The four inset x inset source
// corners are drawn at `scale` (constant pixel size), the edges stretch along one axis
// and the center stretches both, so a small square panel can grow to any size while the
// bevelled border keeps a constant pixel thickness. Used by the Compact overlay mode;
// plain stretch suits the default 1px-center panel (integer tiling for multi-px custom
// centers can be added later).
static void draw_nine_slice(SDL_Renderer *r, SDL_Texture *tex, const SDL_FRect *dest,
                            int il, int ir, int it, int ib, int scale) {
    if (!tex || scale < 1 || !dest) return;

    float tw = 0.0f, th = 0.0f;
    SDL_GetTextureSize(tex, &tw, &th);
    int tex_w = (int) tw, tex_h = (int) th;
    if (tex_w <= 0 || tex_h <= 0) return;

    if (il < 0) il = 0;
    if (ir < 0) ir = 0;
    if (it < 0) it = 0;
    if (ib < 0) ib = 0;
    if (il + ir >= tex_w) { il = 0; ir = 0; } // insets must leave a center strip
    if (it + ib >= tex_h) { it = 0; ib = 0; }

    float src_cw = (float) (tex_w - il - ir); // center source width / height
    float src_ch = (float) (tex_h - it - ib);
    float dl = (float) (il * scale); // destination corner sizes
    float dr = (float) (ir * scale);
    float dt = (float) (it * scale);
    float db = (float) (ib * scale);
    float dcw = dest->w - dl - dr; // stretched center width / height
    float dch = dest->h - dt - db;
    if (dcw < 0.0f) dcw = 0.0f;
    if (dch < 0.0f) dch = 0.0f;

    float rx = (float) (tex_w - ir); // right column source x
    float by = (float) (tex_h - ib); // bottom row source y
    float dx = dest->x, dy = dest->y;

    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);

    struct { SDL_FRect s, d; } quads[9] = {
        {{0.0f, 0.0f, (float) il, (float) it},     {dx, dy, dl, dt}}, // top-left
        {{rx, 0.0f, (float) ir, (float) it},       {dx + dl + dcw, dy, dr, dt}}, // top-right
        {{0.0f, by, (float) il, (float) ib},       {dx, dy + dt + dch, dl, db}}, // bottom-left
        {{rx, by, (float) ir, (float) ib},         {dx + dl + dcw, dy + dt + dch, dr, db}}, // bottom-right
        {{(float) il, 0.0f, src_cw, (float) it},   {dx + dl, dy, dcw, dt}}, // top edge
        {{(float) il, by, src_cw, (float) ib},     {dx + dl, dy + dt + dch, dcw, db}}, // bottom edge
        {{0.0f, (float) it, (float) il, src_ch},   {dx, dy + dt, dl, dch}}, // left edge
        {{rx, (float) it, (float) ir, src_ch},     {dx + dl + dcw, dy + dt, dr, dch}}, // right edge
        {{(float) il, (float) it, src_cw, src_ch}, {dx + dl, dy + dt, dcw, dch}}, // center
    };
    for (int i = 0; i < 9; i++) {
        if (quads[i].s.w <= 0.0f || quads[i].s.h <= 0.0f || quads[i].d.w <= 0.0f || quads[i].d.h <= 0.0f) continue;
        SDL_RenderTexture(r, tex, &quads[i].s, &quads[i].d);
    }
}

// One entry in the Compact panel's cycle: a display label (a section name like "Advancements" or an
// individual goal's own name) over its count. The label is wider than CompactCounter's so it can hold
// an individual goal's full display name. Targeted entries render "completed/total"; open-ended ones
// (a stat with target -1, or a custom goal with target -1/0) drop the denominator and show just the
// count. Goals with a completion state get a plain-ASCII marker (same as the pop-out stack): "[x] "
// manually checked off, "[a] " auto-completed (a linked goal or the target/game), "[o] " not done.
// Manually-checkable goals (simple stats, multi-stats, open-ended custom) show all three; auto-only
// goals (targeted custom goals, counters) show "[a] " when done and nothing otherwise.
struct CompactEntry {
    char label[200];
    int completed;
    int total;       // valid only when !no_target
    bool no_target;  // open-ended goal: show the count with no denominator
    bool checkbox;   // manually-checkable goal: shows [x] manual / [a] auto-done / [o] not done
    bool auto_mark;  // auto-only goal (targeted custom, counter): shows [a] when done, nothing otherwise
    bool manual;     // is_manually_completed (drives [x])
    bool done;       // completed by any means (drives [a])
};

// True if the user selected the individual goal (kind + root_name) into the Compact cycle.
static bool compact_item_selected(const AppSettings *settings, OverlayCompactCounterType kind, const char *root) {
    for (int i = 0; i < settings->compact_cycle_item_count; i++)
        if (settings->compact_cycle_items[i].kind == kind &&
            strcmp(settings->compact_cycle_items[i].root_name, root) == 0)
            return true;
    return false;
}

// Display label for a goal, falling back to its root_name/ID when the display name is empty
// (matching how the rest of the app renders unnamed goals).
static const char *compact_display_name(const char *display, const char *root) {
    return (display && display[0] != '\0') ? display : root;
}

// Build the ordered list of Compact cycle entries from the user's selection: first each selected
// whole-section type count that is present in the template (fixed enum order), then each selected
// individual goal, walked in TEMPLATE order per category and in the same category order the settings
// dropdowns present them (complex advancements, complex recipes, simple stats, multi-stats, custom
// goals, counters) so the cycle order stays consistent with the dropdowns regardless of the order
// they were checked. Version rules
// for the type counts come from the shared compact_compute_type_counters. Falls back to a single
// entry (first present type, else an empty Advancements/Achievements 0/0). Returns >= 1.
static int compact_build_cycle(const Tracker *t, const AppSettings *settings, CompactEntry *out, int max_entries) {
    MC_Version version = settings_get_version_from_string(settings->version_str);
    const TemplateData *td = (t && t->template_data) ? t->template_data : nullptr;

    CompactCounter cc[COMPACT_COUNTER_TYPE_COUNT];
    compact_compute_type_counters(td, version, cc, true); // panel counts show the real totals

    int n = 0;
    for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT && n < max_entries; i++) {
        if (!settings->compact_cycle_type[i] || cc[i].total <= 0) continue;
        snprintf(out[n].label, sizeof(out[n].label), "%s", cc[i].label);
        out[n].completed = cc[i].completed;
        out[n].total = cc[i].total;
        out[n].no_target = false;
        out[n].checkbox = false;
        out[n].auto_mark = false;
        out[n].manual = false;
        out[n].done = false;
        n++;
    }
    if (td) {
        // Complex advancements, then complex recipes -> their criteria progress. Two passes so the
        // cycle order matches the two separate settings dropdowns.
        for (int pass = 0; pass < 2; pass++) {
            bool want_recipe = (pass == 1);
            OverlayCompactCounterType kind = want_recipe
                                                 ? COMPACT_COUNTER_RECIPE_CRITERIA
                                                 : COMPACT_COUNTER_CRITERIA;
            for (int i = 0; i < td->advancement_count && n < max_entries; i++) {
                TrackableCategory *a = td->advancements[i];
                if (!a || a->is_recipe != want_recipe || a->criteria_count <= 0 ||
                    goal_is_hidden(a->is_hidden, settings))
                    continue;
                if (!compact_item_selected(settings, kind, a->root_name)) continue;
                snprintf(out[n].label, sizeof(out[n].label), "%s",
                         compact_display_name(a->display_name, a->root_name));
                out[n].completed = a->completed_criteria_count;
                out[n].total = a->criteria_progress_total;
                out[n].no_target = false;
                out[n].checkbox = false;
                out[n].auto_mark = false;
                out[n].manual = false;
                out[n].done = false;
                n++;
            }
        }
        // Simple stats (single-value stat categories). A real target (goal > 0) shows value / target;
        // an open-ended stat (goal -1) shows the checkbox + running value. goal 0 = legacy helper, skip.
        for (int i = 0; i < td->stat_count && n < max_entries; i++) {
            TrackableCategory *s = td->stats[i];
            if (!s || !s->is_single_stat_category || goal_is_hidden(s->is_hidden, settings)) continue;
            if (s->criteria_count < 1 || !s->criteria[0]) continue;
            int goal = s->criteria[0]->goal;
            if (goal <= 0 && goal != -1) continue;
            if (!compact_item_selected(settings, COMPACT_COUNTER_STATS, s->root_name)) continue;
            snprintf(out[n].label, sizeof(out[n].label), "%s", compact_display_name(s->display_name, s->root_name));
            out[n].completed = s->criteria[0]->progress;
            out[n].checkbox = true; // simple stats can be completed manually
            out[n].auto_mark = false;
            out[n].manual = s->is_manually_completed;
            out[n].done = s->done;
            if (goal > 0) {
                out[n].total = goal;
                out[n].no_target = false;
            } else {
                out[n].total = 0;
                out[n].no_target = true;
            }
            n++;
        }
        // Multi-stats (complex stat categories) -> their sub-stat progress.
        for (int i = 0; i < td->stat_count && n < max_entries; i++) {
            TrackableCategory *s = td->stats[i];
            if (!s || s->is_single_stat_category || goal_is_hidden(s->is_hidden, settings)) continue;
            if (!compact_item_selected(settings, COMPACT_COUNTER_SUB_STATS, s->root_name)) continue;
            snprintf(out[n].label, sizeof(out[n].label), "%s", compact_display_name(s->display_name, s->root_name));
            out[n].completed = s->completed_criteria_count;
            out[n].total = s->criteria_count;
            out[n].no_target = false;
            out[n].checkbox = true; // multi-stats can be completed manually
            out[n].auto_mark = false;
            out[n].manual = s->is_manually_completed;
            out[n].done = s->done;
            n++;
        }
        // Custom goals -> a real target (goal > 0) shows progress / goal; an open-ended custom goal
        // (goal -1 or 0) shows the checkbox + running count and its manual done state.
        for (int i = 0; i < td->custom_goal_count && n < max_entries; i++) {
            TrackableItem *c = td->custom_goals[i];
            if (!c || goal_is_hidden(c->is_hidden, settings)) continue;
            if (!compact_item_selected(settings, COMPACT_COUNTER_CUSTOM, c->root_name)) continue;
            snprintf(out[n].label, sizeof(out[n].label), "%s", compact_display_name(c->display_name, c->root_name));
            out[n].completed = c->progress;
            if (c->goal > 0) {
                // Targeted custom goals are counter-driven, not manually checkable: [a] only when done.
                out[n].total = c->goal;
                out[n].no_target = false;
                out[n].checkbox = false;
                out[n].auto_mark = true;
                out[n].manual = false;
                out[n].done = c->done;
            } else {
                out[n].total = 0;
                out[n].no_target = true;
                out[n].checkbox = true;
                out[n].auto_mark = false;
                out[n].manual = c->is_manually_completed;
                out[n].done = c->done;
            }
            n++;
        }
        // Completion counters -> completed/linked goals.
        for (int i = 0; i < td->counter_goal_count && n < max_entries; i++) {
            CounterGoal *c = td->counter_goals[i];
            if (!c || goal_is_hidden(c->is_hidden, settings)) continue;
            if (!compact_item_selected(settings, COMPACT_COUNTER_COUNTERS, c->root_name)) continue;
            snprintf(out[n].label, sizeof(out[n].label), "%s", compact_display_name(c->display_name, c->root_name));
            out[n].completed = c->completed_count;
            out[n].total = c->linked_goal_count;
            out[n].no_target = false;
            out[n].checkbox = false;
            out[n].auto_mark = true; // counters aren't manually checkable: [a] only when done
            out[n].manual = false;
            out[n].done = c->done;
            n++;
        }
    }
    if (n == 0) {
        for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++) {
            if (cc[i].total > 0) {
                snprintf(out[0].label, sizeof(out[0].label), "%s", cc[i].label);
                out[0].completed = cc[i].completed;
                out[0].total = cc[i].total;
                out[0].no_target = false;
                out[0].checkbox = false;
                out[0].auto_mark = false;
                out[0].manual = false;
                out[0].done = false;
                n = 1;
                break;
            }
        }
        if (n == 0) {
            snprintf(out[0].label, sizeof(out[0].label), "%s",
                     (version >= MC_VERSION_1_12) ? "Advancements" : "Achievements");
            out[0].completed = 0;
            out[0].total = 0;
            out[0].no_target = false;
            out[0].checkbox = false;
            out[0].auto_mark = false;
            out[0].manual = false;
            out[0].done = false;
            n = 1;
        }
    }
    return n;
}

// The font's widest decimal digit, so a worst-case count string reserves the maximum width the
// live numerator can ever reach (keeps the panel a fixed size for the whole run).
static char compact_widest_digit(TTF_Font *font) {
    char widest = '0';
    int widest_w = 0;
    for (char c = '0'; c <= '9'; ++c) {
        char one[2] = {c, '\0'};
        int dw = 0;
        TTF_MeasureString(font, one, 0, 0, &dw, nullptr);
        if (dw > widest_w) {
            widest_w = dw;
            widest = c;
        }
    }
    return widest;
}

// Build the widest count string a "completed/total" counter can display: the numerator is the
// widest digit repeated for as many digits as `total` has, over the actual `total`.
static void compact_worst_count(char *buf, size_t buf_sz, int total, char wdigit) {
    int digits = 1;
    for (int g = total; g >= 10; g /= 10) digits++;
    int p = 0;
    for (int i = 0; i < digits && p < (int) buf_sz - 12; i++) buf[p++] = wdigit;
    buf[p++] = '/';
    snprintf(buf + p, buf_sz - (size_t) p, "%d", total);
}

// Format the count line an entry shows. The body is "completed/total" for targeted goals, or just
// the running count for open-ended ones. Completion markers (ASCII only, so they render in any font):
// manually-checkable goals show "[x] " manual / "[a] " auto-done / "[o] " not done; auto-only goals
// show "[a] " when done and nothing otherwise. The marker is part of the line, which is centered as a
// whole: centering the count alone and hanging the marker off its left reads far worse.
static void compact_format_count(char *buf, size_t buf_sz, const AppSettings *s, const CompactEntry *e) {
    const char *box = "";
    if (s->compact_show_completion_markers) {
        if (e->checkbox)
            box = e->manual ? "[x] " : (e->done ? "[a] " : "[o] ");
        else if (e->auto_mark && e->done)
            box = "[a] ";
    }
    if (e->no_target)
        snprintf(buf, buf_sz, "%s%d", box, e->completed);
    else
        snprintf(buf, buf_sz, "%s%d/%d", box, e->completed, e->total);
}

// Worst-case count string for panel sizing. Targeted goals: widest digit repeated over the total.
// Open-ended goals: the widest digit repeated for the running count's current digit length (so the
// panel only grows if the value gains a digit). A checkbox prefix is reserved for checkbox entries;
// "[x]"/"[o]" are the same length so a manual-toggle never resizes the panel.
static void compact_worst_count_entry(char *buf, size_t buf_sz, const AppSettings *s, const CompactEntry *e,
                                      char wdigit) {
    char body[48];
    if (e->no_target) {
        int v = e->completed < 0 ? -e->completed : e->completed;
        int digits = 1;
        for (int d = v; d >= 10; d /= 10) digits++;
        int p = 0;
        for (int i = 0; i < digits && p < (int) sizeof(body) - 1; i++) body[p++] = wdigit;
        body[p] = '\0';
    } else {
        compact_worst_count(body, sizeof(body), e->total, wdigit);
    }
    if (s->compact_show_completion_markers && (e->checkbox || e->auto_mark))
        snprintf(buf, buf_sz, "[x] %s", body); // reserve the marker width ([x]/[a]/[o] are all the same length)
    else
        snprintf(buf, buf_sz, "%s", body);
}

// ---------------------------------------------------------------------------
// Compact pop-out stack (Stage 4)
//
// As goals progress or complete in-game a horizontal "[icon] text" line (or a 2-line
// parent + criterion group) slides out from under the counter panel and stacks below
// it, holds, then leaves. Motion is purely positional + clip (never alpha) so OBS
// chroma keying stays clean. Which goals may pop is configured independently of the
// panel cycle via compact_stack_type / compact_stack_items.
// ---------------------------------------------------------------------------

#define COMPACT_POP_LINE_GAP 8.0f   // Vertical gap below each pop-out line (folded into the line height)
#define COMPACT_POP_TEXT_GAP 10.0f  // Horizontal gap between a pop-out icon and its text
#define COMPACT_POP_FACE_GAP 6.0f   // Horizontal gap flanking the co-op contributor face slot

// Co-op stack contributor faces are active only in an active lobby's merged All-Players view with the
// contributor-faces setting on - exactly when the main tracker draws them. In a specific-player/ghost
// view the pinned panel face is used instead (drawn in overlay_render_compact), so no per-line faces.
static bool overlay_coop_stack_faces_on(const Overlay *o, const AppSettings *s) {
    return o && s && s->coop_show_contributor_faces && o->coop_lobby_active && o->coop_all_players_view;
}

// Resolve a contributor UUID to its account type from the lobby roster synced in the IPC header, so
// offline accounts skip the Mojang fetch and fall straight to Notch (matches the tracker). Unknown
// UUIDs (e.g. a spectated ghost not in the lobby) default to online, as the tracker's lookup does.
static AccountType overlay_coop_account_type(const Overlay *o, const char *uuid) {
    if (!o || !uuid || !uuid[0]) return ACCOUNT_ONLINE;
    for (int i = 0; i < o->coop_lobby_count; i++)
        if (strcmp(o->coop_lobby[i].uuid, uuid) == 0)
            return o->coop_lobby[i].is_offline ? ACCOUNT_OFFLINE : ACCOUNT_ONLINE;
    return ACCOUNT_ONLINE;
}

// Draw an 8x8 player face texture (nearest-neighbour, full opacity) into a square slot.
static void compact_draw_face(Overlay *o, const char *uuid, AccountType acc, float fx, float fy, float sz) {
    if (!uuid || !uuid[0]) return;
    SDL_Texture *tex = skin_cache_get_face(uuid, acc);
    if (!tex) return;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_FRect d = {snap_px(fx), snap_px(fy), sz, sz};
    SDL_RenderTexture(o->renderer, tex, nullptr, &d);
}

// One active on-screen pop-out group. Criterion / sub-stat completions are 2-line groups (the parent
// advancement/category on top, the criterion below); everything else is a single line.
struct CompactPopGroup {
    std::string key;          // stable identity, used to coalesce repeat increments in place
    bool two_line;
    std::string parent_icon;  // resolved icon path (used directly, like the item rows)
    std::string parent_text;
    std::string item_icon;
    std::string item_text;
    bool item_shared;         // overlay the parent icon on the item icon (shared criterion)
    std::string face_uuid;    // co-op contributor whose face rides this line (empty = none)
    float hold_left;          // seconds until it disappears
    float anim_y;             // current top Y (lerps toward the settled slot)
    bool placed;              // anim_y initialised
};

struct CompactStackEngine {
    unsigned long long signature = 0;
    bool seeded = false;
    Uint64 last_tick = 0;
    std::unordered_map<std::string, unsigned long long> prev; // last-seen encoded value per key
    std::vector<CompactPopGroup> groups;                      // newest first (index 0 = top)
};

static CompactStackEngine s_compact_stack;

// Encodes a goal's (progress, done) into one monotonically-rising value: the done bit sits above the
// progress so completing a goal always reads as an increase, and progress increments do too. Lets the
// diff pop on "value went up" without tracking done separately. Regressions (undo) decrease it, so
// they never pop.
static unsigned long long compact_enc(long long progress, bool done) {
    unsigned long long p = progress > 0 ? (unsigned long long) progress : 0ULL;
    return (done ? (1ULL << 40) : 0ULL) + p;
}

// The progress a goal contributes to its pop test (compact_enc), which is all the encoded value is
// used for - the line's text is built separately, so it always shows the real numbers.
//
// A counting type whose "Pop On Progress" is off contributes nothing here, leaving the done bit as the
// only thing that can raise its encoded value: it pops once, on completion, instead of on every
// increment. Types that are only ever done-or-not have no progress to drop and are unaffected.
// Multi-stage goals pass their in-stage stat progress through this and keep the stage index outside
// it, so switching them to completion-only stops the per-tick pops but still pops each stage.
static long long compact_pop_progress(const AppSettings *s, OverlayCompactCounterType kind, long long progress) {
    if (compact_type_has_progress(kind) && !s->compact_stack_pop_on_progress[kind]) return 0;
    return progress;
}

// Multi-stage goals: fold the stage index and the stat progress inside the active stage into one
// rising value for compact_enc. A stat stage counts up on its own and restarts at every stage, so the
// stage index has to outweigh it: the progress gets the low 31 bits (game stats are 32-bit ints, so it
// never has to be clamped) and the stage sits above it, capped at what stays under compact_enc's done
// bit. Non-stat stages just pass 0 progress and move only when the stage does.
#define COMPACT_MS_STAGE_SHIFT 31
#define COMPACT_MS_MAX_STAGE 511
static long long compact_ms_pack(int stage, int stat_progress) {
    if (stage < 0) stage = 0;
    if (stage > COMPACT_MS_MAX_STAGE) stage = COMPACT_MS_MAX_STAGE;
    long long p = stat_progress > 0 ? (long long) stat_progress : 0LL;
    return ((long long) stage << COMPACT_MS_STAGE_SHIFT) + p;
}

// A multi-stage goal pops as one line ("name: stage"), so its name needs the separator the Belt and
// Page rows get for free by putting the stage on its own line. A template can already end the name
// with a colon ("Need:"), which would read as "Need:: stage", so the colon is only added when there
// is not one there: trailing blanks are dropped first, then one colon is appended if needed.
static void compact_ms_label(char *buf, size_t buf_sz, const char *name) {
    size_t n = name ? strlen(name) : 0;
    while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '\t')) n--;
    if (n > buf_sz - 2) n = buf_sz - 2;
    if (n > 0) memcpy(buf, name, n);
    if (n > 0 && buf[n - 1] != ':') buf[n++] = ':';
    buf[n] = '\0';
}

// Goal-type kinds that have NO individual-goal dropdown (only a whole-type toggle): regular
// advancements, recipes and unlocks pop as whole completions. Every other kind is chosen per-goal in
// its own dropdown, so its type toggle is ignored (avoids the confusing overlap).
static bool compact_type_is_pickerless(OverlayCompactCounterType k) {
    return k == COMPACT_COUNTER_ADVANCEMENTS || k == COMPACT_COUNTER_RECIPES ||
           k == COMPACT_COUNTER_UNLOCKS;
}

// True if the stack may show a goal: for a pickerless kind, its whole-type toggle is on; for any other
// kind, the specific goal (`indiv_kind` + `indiv_root`) is individually whitelisted. indiv_root may be
// null for pickerless kinds.
static bool compact_stack_allows(const AppSettings *s, OverlayCompactCounterType type_kind,
                                 OverlayCompactCounterType indiv_kind, const char *indiv_root) {
    if (compact_type_is_pickerless(type_kind) && s->compact_stack_type[type_kind]) return true;
    if (indiv_root) {
        for (int i = 0; i < s->compact_stack_item_count; i++)
            if (s->compact_stack_items[i].kind == indiv_kind &&
                strcmp(s->compact_stack_items[i].root_name, indiv_root) == 0)
                return true;
    }
    return false;
}

// Completion marker for a manually-checkable stack line: "[x] " checked off manually, "[a] " auto-
// completed (a linked goal, or the target/game), "[o] " not done. All markers are 4 chars wide so the
// box never resizes the line. Goals that can't be checked off manually don't use this (they show
// "[a] " when done and nothing otherwise, inline at the call site). Returns "" when the user has hidden
// the completion markers.
static const char *compact_done_box(const AppSettings *s, bool manual, bool done) {
    if (!s->compact_show_completion_markers) return "";
    if (manual) return "[x] ";
    if (done) return "[a] ";
    return "[o] ";
}

// The "[a] " auto-only marker for goals that can't be checked off manually (targeted custom goals,
// counters): shown when done, hidden otherwise, and gone entirely when markers are turned off.
static const char *compact_auto_box(const AppSettings *s, bool done) {
    return (s->compact_show_completion_markers && done) ? "[a] " : "";
}

// Goal types whose Compact line carries an [o]/[a]/[x] completion marker (manually checkable or auto-
// completable). Advancements, recipes, unlocks, criteria and multi-stage stage changes have no marker.
static bool compact_type_has_marker(OverlayCompactCounterType k) {
    return k == COMPACT_COUNTER_STATS || k == COMPACT_COUNTER_SUB_STATS ||
           k == COMPACT_COUNTER_CUSTOM || k == COMPACT_COUNTER_COUNTERS;
}

// The done flag a marker-bearing goal contributes to its pop test. With the markers hidden a bare
// completion no longer changes the line's look, so it must not pop on its own: the done bit is dropped
// and only a real value change (via compact_pop_progress) can still pop the line. Types without a
// marker keep their done flag and always pop on completion.
static bool compact_pop_done(const AppSettings *s, OverlayCompactCounterType kind, bool done) {
    if (!s->compact_show_completion_markers && compact_type_has_marker(kind)) return false;
    return done;
}

// Assembles a stack line's text with its completion marker on the side that keeps it on the text's
// outer edge once the whole line is mirrored for a right-aligned panel: prefixed for a left-aligned
// stack ("[x] Name (1/2)"), suffixed for a right-aligned one ("Name (1/2) [x]"). `box` is the "[x] "
// form (trailing space) or "" when there is no marker.
static void compact_marked_line(char *buf, size_t buf_sz, const char *box, bool right_align,
                                const char *body) {
    if (!box || !box[0]) {
        snprintf(buf, buf_sz, "%s", body);
        return;
    }
    if (right_align) {
        char b[8];
        snprintf(b, sizeof(b), "%s", box);
        size_t n = strlen(b);
        while (n > 0 && b[n - 1] == ' ') b[--n] = '\0'; // drop the prefix's trailing space
        snprintf(buf, buf_sz, "%s %s", body, b);
    } else {
        snprintf(buf, buf_sz, "%s%s", box, body);
    }
}

// Draws one square icon of a stack line at full opacity (never alpha-blended - OBS chroma keying can't
// respect partial alpha). Handles both a static .png and an animated .gif, from the overlay's caches.
static void compact_draw_icon(Overlay *o, const char *path, float ix, float iy, float sz) {
    if (!path || !path[0]) return;
    SDL_Texture *tex = nullptr;
    AnimatedTexture *anim = nullptr;
    if (strstr(path, ".gif"))
        anim = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
                                               &o->anim_cache_capacity, path, SDL_SCALEMODE_NEAREST);
    else
        tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                     &o->texture_cache_capacity, path, SDL_SCALEMODE_NEAREST);
    SDL_FRect d = {snap_px(ix), snap_px(iy), sz, sz};
    if (tex || anim) render_texture_with_alpha(o->renderer, tex, anim, &d, 255);
}

// FNV-1a over every poppable goal's root_name(s), so the stack reseeds (and clears) only when the
// template itself changes - mirrors the belt/page signature approach.
static unsigned long long compact_stack_signature(const TemplateData *td) {
    unsigned long long sig = 1469598103934665603ULL;
    auto mix = [&](const char *s) {
        for (; s && *s; s++) sig = (sig ^ (unsigned char) *s) * 1099511628211ULL;
        sig = (sig ^ 0xFFu) * 1099511628211ULL; // separator so a|b differs from ab
    };
    if (!td) return sig;
    for (int i = 0; i < td->advancement_count; i++) {
        const TrackableCategory *a = td->advancements[i];
        if (!a) continue;
        mix(a->root_name);
        for (int j = 0; j < a->criteria_count; j++)
            if (a->criteria[j]) mix(a->criteria[j]->root_name);
    }
    for (int i = 0; i < td->stat_count; i++) {
        const TrackableCategory *s = td->stats[i];
        if (!s) continue;
        mix(s->root_name);
        for (int j = 0; j < s->criteria_count; j++)
            if (s->criteria[j]) mix(s->criteria[j]->root_name);
    }
    for (int i = 0; i < td->unlock_count; i++)
        if (td->unlocks[i]) mix(td->unlocks[i]->root_name);
    for (int i = 0; i < td->custom_goal_count; i++)
        if (td->custom_goals[i]) mix(td->custom_goals[i]->root_name);
    for (int i = 0; i < td->multi_stage_goal_count; i++)
        if (td->multi_stage_goals[i]) mix(td->multi_stage_goals[i]->root_name);
    for (int i = 0; i < td->counter_goal_count; i++)
        if (td->counter_goals[i]) mix(td->counter_goals[i]->root_name);
    return sig;
}

// Diffs the template against the previous snapshot and updates the pop-out stack (seed on load /
// template change, enqueue or coalesce on any increase, expire on hold, cut on overflow), then lays
// out and draws the stack below the panel. Called every frame from overlay_render_compact once the
// panel geometry is known. `stack_x` is the panel's left edge and `panel_w` its width; the stack
// left-aligns at stack_x, or (for a right-aligned panel) mirrors to right-align at stack_x + panel_w.
static void compact_render_stack(Overlay *o, const Tracker *t, const AppSettings *settings,
                                 float stack_x, float panel_w, float panel_bottom, float stack_top,
                                 int window_bottom, SDL_Color text_color) {
    CompactStackEngine &eng = s_compact_stack;
    const TemplateData *td = (t && t->template_data) ? t->template_data : nullptr;
    TTF_Font *stack_font = o->compact_stack_font ? o->compact_stack_font : o->font;
    // Co-op All-Players view: each pop-out line carries the face of whoever the main tracker credits.
    // When on, a face-sized slot is reserved right of every line's icon so text stays left-aligned.
    bool faces_on = overlay_coop_stack_faces_on(o, settings);
    // A right-aligned panel mirrors the whole stack: text, then face, then icon (icon flush to the
    // panel's right edge), with each line's completion marker moved to the text's right side.
    bool right_align = settings->compact_panel_align == OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT;

    // Reseed (and clear) when the template changes or on the first frame, so nothing pops on load.
    unsigned long long sig = compact_stack_signature(td);
    if (!eng.seeded || eng.signature != sig) {
        eng.signature = sig;
        eng.prev.clear();
        eng.groups.clear();
        eng.seeded = false; // this frame's walk only records the baseline; no pops
    }

    Uint64 now = SDL_GetTicks();
    float dt = (eng.last_tick == 0) ? 0.0f : (float) (now - eng.last_tick) / 1000.0f;
    eng.last_tick = now;
    if (dt > 0.25f) dt = 0.25f; // clamp after a stall / first frame

    // --- Diff walk: record every poppable goal. A NEW increase enqueues a group; a goal already
    // showing is refreshed in place on any change (progress up or reverted) and removed if it has
    // reverted all the way back to nothing. ---
    auto consider = [&](OverlayCompactCounterType type_kind, OverlayCompactCounterType indiv_kind,
                        const char *indiv_root, const char *key, long long progress, bool done, bool two_line,
                        const char *parent_icon, const char *parent_text, const char *item_icon,
                        const char *item_text, bool item_shared, const char *face_uuid) {
        unsigned long long enc = compact_enc(progress, done);
        std::string k(key);
        auto it = eng.prev.find(k);
        unsigned long long old = (it == eng.prev.end()) ? 0ULL : it->second;
        eng.prev[k] = enc;
        if (!eng.seeded) return; // seeding pass: record only
        // Already on-screen? Refresh in place regardless of direction (revert or progress).
        for (size_t gi = 0; gi < eng.groups.size(); gi++) {
            if (eng.groups[gi].key != k) continue;
            if (enc == 0ULL) { // reverted to no progress and not done -> just disappear
                eng.groups.erase(eng.groups.begin() + (long) gi);
                return;
            }
            CompactPopGroup &g = eng.groups[gi];
            g.two_line = two_line;
            g.parent_icon = parent_icon ? parent_icon : "";
            // parent_text is the parent's aggregate count ("(2/39)") on a two-line group, which counts
            // OTHER goals (sibling criteria / sub-stats), not this line. It is frozen at pop time so an
            // already-shown group keeps the count it popped with: two criteria of the same advancement
            // pop as separate groups reading (1/39) then (2/39), and the earlier one is not bumped to
            // the newer total when a sibling completes. The item line below still refreshes, so a goal
            // that updates in place (a stat counting up without a fresh pop) keeps rising live.
            g.item_icon = item_icon ? item_icon : "";
            g.item_text = item_text ? item_text : "";
            g.item_shared = item_shared;
            g.face_uuid = face_uuid ? face_uuid : "";
            if (enc > old) g.hold_left = settings->compact_stack_hold_time; // only a real increment refreshes the hold
            return;
        }
        if (enc <= old) return; // not showing: only appear on a real increase
        if (!compact_stack_allows(settings, type_kind, indiv_kind, indiv_root)) return;
        CompactPopGroup g;
        g.key = k;
        g.two_line = two_line;
        g.parent_icon = parent_icon ? parent_icon : "";
        g.parent_text = parent_text ? parent_text : "";
        g.item_icon = item_icon ? item_icon : "";
        g.item_text = item_text ? item_text : "";
        g.item_shared = item_shared;
        g.face_uuid = face_uuid ? face_uuid : "";
        g.hold_left = settings->compact_stack_hold_time;
        g.anim_y = 0.0f;
        g.placed = false;
        eng.groups.insert(eng.groups.begin(), g); // newest on top
    };

    if (td) {
        char key[512], ptext[256], itext[256];
        // faces_on (hoisted above) gates the per-type contributor UUID passed to consider() below.
        // Advancements / recipes: the whole goal (1 line) + each criterion (2-line group).
        for (int i = 0; i < td->advancement_count; i++) {
            TrackableCategory *a = td->advancements[i];
            if (!a || goal_is_hidden(a->is_hidden, settings)) continue;
            bool recipe = a->is_recipe;
            OverlayCompactCounterType whole_kind = recipe ? COMPACT_COUNTER_RECIPES : COMPACT_COUNTER_ADVANCEMENTS;
            OverlayCompactCounterType crit_kind = recipe ? COMPACT_COUNTER_RECIPE_CRITERIA : COMPACT_COUNTER_CRITERIA;
            const char *aname = compact_display_name(a->display_name, a->root_name);
            // First completer (simple = who got it; complex-assigned = the current leader), like the
            // tracker's advancement face and its right-of-icon criterion face.
            const char *adv_face = (faces_on && a->first_contributor_uuid[0]) ? a->first_contributor_uuid : nullptr;
            snprintf(key, sizeof(key), "advw|%s", a->root_name);
            consider(whole_kind, whole_kind, nullptr, key, 0, a->done, false,
                     nullptr, nullptr, a->icon_path, aname, false, adv_face);
            snprintf(ptext, sizeof(ptext), "%s (%d/%d)", aname, a->completed_criteria_count,
                     a->criteria_progress_total);
            for (int j = 0; j < a->criteria_count; j++) {
                TrackableItem *c = a->criteria[j];
                if (!c || goal_is_hidden(c->is_hidden, settings)) continue;
                snprintf(key, sizeof(key), "crit|%s|%s", a->root_name, c->root_name);
                consider(crit_kind, crit_kind, a->root_name, key, 0, c->done, true,
                         a->icon_path, ptext, c->icon_path,
                         compact_display_name(c->display_name, c->root_name), c->is_shared, adv_face);
            }
        }
        // Stats: simple stats pop as a graded single line; multi-stat sub-stats pop as 2-line groups.
        for (int i = 0; i < td->stat_count; i++) {
            TrackableCategory *s = td->stats[i];
            if (!s || goal_is_hidden(s->is_hidden, settings)) continue;
            const char *sname = compact_display_name(s->display_name, s->root_name);
            if (s->is_single_stat_category) {
                if (s->criteria_count < 1 || !s->criteria[0]) continue;
                int goal = s->criteria[0]->goal;
                if (goal <= 0 && goal != -1) continue; // goal 0 = legacy helper
                int prog = s->criteria[0]->progress;
                // Completion box (the stack has no done-background like the item rows): [x] manually
                // checked off, [a] auto-completed (a linked goal or the target reached), [o] not done.
                const char *box = compact_done_box(settings, s->is_manually_completed, s->done);
                char sbody[240];
                if (goal > 0) snprintf(sbody, sizeof(sbody), "%s (%d/%d)", sname, prog, goal);
                else snprintf(sbody, sizeof(sbody), "%s (%d)", sname, prog);
                compact_marked_line(itext, sizeof(itext), box, right_align, sbody);
                snprintf(key, sizeof(key), "stat|%s", s->root_name);
                // A lone manual completer wins (tracker draws it under the checkbox); otherwise the
                // highest contributor, only in HIGHEST merge mode, like the tracker's simple-stat face.
                const char *stat_face = nullptr;
                if (faces_on) {
                    if (s->is_manually_completed && s->manual_completer_uuid[0]) stat_face = s->manual_completer_uuid;
                    else if (settings->coop_stat_merge == COOP_STAT_HIGHEST &&
                             s->criteria[0]->highest_contributor_uuid[0])
                        stat_face = s->criteria[0]->highest_contributor_uuid;
                }
                consider(COMPACT_COUNTER_STATS, COMPACT_COUNTER_STATS, s->root_name, key,
                         compact_pop_progress(settings, COMPACT_COUNTER_STATS, prog),
                         compact_pop_done(settings, COMPACT_COUNTER_STATS, s->done), false,
                         nullptr, nullptr, s->icon_path, itext, false, stat_face);
            } else {
                // The category itself is manually checkable, so its parent line carries a box too.
                const char *catbox = compact_done_box(settings, s->is_manually_completed, s->done);
                char pbody[240];
                snprintf(pbody, sizeof(pbody), "%s (%d/%d)", sname, s->completed_criteria_count,
                         s->criteria_count);
                compact_marked_line(ptext, sizeof(ptext), catbox, right_align, pbody);
                for (int j = 0; j < s->criteria_count; j++) {
                    TrackableItem *sub = s->criteria[j];
                    if (!sub || goal_is_hidden(sub->is_hidden, settings)) continue;
                    // [x] manually checked off, [a] auto-completed (linked goal), [o] not done. A
                    // sub-stat counts up, so it shows its value and pops on every increment (goal > 0
                    // shows value / target, an open-ended one just the value).
                    const char *subbox = compact_done_box(settings, sub->is_manually_completed, sub->done);
                    const char *subname = compact_display_name(sub->display_name, sub->root_name);
                    char subbody[240];
                    if (sub->goal > 0)
                        snprintf(subbody, sizeof(subbody), "%s (%d/%d)", subname, sub->progress, sub->goal);
                    else
                        snprintf(subbody, sizeof(subbody), "%s (%d)", subname, sub->progress);
                    compact_marked_line(itext, sizeof(itext), subbox, right_align, subbody);
                    snprintf(key, sizeof(key), "sub|%s|%s", s->root_name, sub->root_name);
                    // Lone manual completer, else the highest contributor (HIGHEST merge only), matching
                    // the tracker's sub-stat manual-checkbox face and right-of-icon contributor face.
                    const char *sub_face = nullptr;
                    if (faces_on) {
                        if (sub->is_manually_completed && sub->manual_completer_uuid[0])
                            sub_face = sub->manual_completer_uuid;
                        else if (settings->coop_stat_merge == COOP_STAT_HIGHEST && sub->highest_contributor_uuid[0])
                            sub_face = sub->highest_contributor_uuid;
                    }
                    consider(COMPACT_COUNTER_SUB_STATS, COMPACT_COUNTER_SUB_STATS, s->root_name, key,
                             compact_pop_progress(settings, COMPACT_COUNTER_SUB_STATS, sub->progress),
                             compact_pop_done(settings, COMPACT_COUNTER_SUB_STATS, sub->done), true,
                             s->icon_path, ptext, sub->icon_path, itext, sub->is_shared, sub_face);
                }
            }
        }
        for (int i = 0; i < td->unlock_count; i++) {
            TrackableItem *u = td->unlocks[i];
            if (!u || goal_is_hidden(u->is_hidden, settings)) continue;
            snprintf(key, sizeof(key), "unl|%s", u->root_name);
            // Unlocks are AND-merged across players (no single contributor), so no face, like the tracker.
            consider(COMPACT_COUNTER_UNLOCKS, COMPACT_COUNTER_UNLOCKS, nullptr, key, 0, u->done, false,
                     nullptr, nullptr, u->icon_path, compact_display_name(u->display_name, u->root_name), false,
                     nullptr);
        }
        for (int i = 0; i < td->custom_goal_count; i++) {
            TrackableItem *c = td->custom_goals[i];
            if (!c || goal_is_hidden(c->is_hidden, settings)) continue;
            const char *cname = compact_display_name(c->display_name, c->root_name);
            if (c->goal > 0) {
                // Targeted custom goals are counter-driven, not manually checkable; the count usually
                // shows completion, but a linked goal can complete one early (count < goal), so mark
                // [a] (auto) when done, nothing otherwise.
                const char *box = compact_auto_box(settings, c->done);
                char cbody[240];
                snprintf(cbody, sizeof(cbody), "%s (%d/%d)", cname, c->progress, c->goal);
                compact_marked_line(itext, sizeof(itext), box, right_align, cbody);
            } else {
                // Open-ended custom goals show the box: [x] manual, [a] auto (linked goal), [o] not done.
                const char *box = compact_done_box(settings, c->is_manually_completed, c->done);
                char cbody[240];
                snprintf(cbody, sizeof(cbody), "%s (%d)", cname, c->progress);
                compact_marked_line(itext, sizeof(itext), box, right_align, cbody);
            }
            snprintf(key, sizeof(key), "cus|%s", c->root_name);
            // Lone manual completer, else the lone counter-value contributor, matching the tracker.
            const char *cus_face = nullptr;
            if (faces_on) {
                if (c->is_manually_completed && c->manual_completer_uuid[0]) cus_face = c->manual_completer_uuid;
                else if (c->custom_contributor_uuid[0]) cus_face = c->custom_contributor_uuid;
            }
            consider(COMPACT_COUNTER_CUSTOM, COMPACT_COUNTER_CUSTOM, c->root_name, key,
                     compact_pop_progress(settings, COMPACT_COUNTER_CUSTOM, c->progress),
                     compact_pop_done(settings, COMPACT_COUNTER_CUSTOM, c->done), false,
                     nullptr, nullptr, c->icon_path, itext, false, cus_face);
        }
        for (int i = 0; i < td->multi_stage_goal_count; i++) {
            MultiStageGoal *g = td->multi_stage_goals[i];
            if (!g || goal_is_hidden(g->is_hidden, settings)) continue;
            int stage = g->current_stage;
            bool done = (g->stage_count > 0 && stage >= g->stage_count - 1);
            char gname[224];
            compact_ms_label(gname, sizeof(gname), compact_display_name(g->display_name, g->root_name));
            const char *icon = g->icon_path;
            int stat_prog = 0;
            snprintf(itext, sizeof(itext), "%s", gname);
            if (stage >= 0 && stage < g->stage_count && g->stages && g->stages[stage]) {
                const SubGoal *st = g->stages[stage];
                if (g->use_stage_icons && st->icon_path[0]) icon = st->icon_path;
                // A stat stage counts up inside the stage, so it shows its value and pops on every
                // increment, like the Belt and Page rows do. Other stage types only move on a stage
                // change, so they stay text-only.
                if (st->type == SUBGOAL_STAT && st->required_progress > 0) {
                    stat_prog = st->current_stat_progress;
                    snprintf(itext, sizeof(itext), "%s %s (%d/%d)", gname, st->display_text, stat_prog,
                             st->required_progress);
                } else if (st->type == SUBGOAL_STAT && st->required_progress == -1) {
                    stat_prog = st->current_stat_progress;
                    snprintf(itext, sizeof(itext), "%s %s (%d)", gname, st->display_text, stat_prog);
                } else {
                    snprintf(itext, sizeof(itext), "%s %s", gname, st->display_text);
                }
            }
            snprintf(key, sizeof(key), "ms|%s", g->root_name);
            // Only the in-stage stat counts as this goal's progress; the stage index always moves the
            // encoded value, so a completion-only multi-stage goal still pops on every stage it clears.
            int enc_prog = (int) compact_pop_progress(settings, COMPACT_COUNTER_MULTISTAGE, stat_prog);
            // Multi-stage goals have no per-player contributor stamp, so no face (as on the tracker).
            consider(COMPACT_COUNTER_MULTISTAGE, COMPACT_COUNTER_MULTISTAGE, g->root_name, key,
                     compact_ms_pack(stage, enc_prog), done, false,
                     nullptr, nullptr, icon, itext, false, nullptr);
        }
        for (int i = 0; i < td->counter_goal_count; i++) {
            CounterGoal *c = td->counter_goals[i];
            if (!c || goal_is_hidden(c->is_hidden, settings)) continue;
            // Counters aren't manually checkable; mark [a] (auto) when done, nothing otherwise.
            const char *cbox = compact_auto_box(settings, c->done);
            char cntbody[240];
            snprintf(cntbody, sizeof(cntbody), "%s (%d/%d)",
                     compact_display_name(c->display_name, c->root_name), c->completed_count, c->linked_goal_count);
            compact_marked_line(itext, sizeof(itext), cbox, right_align, cntbody);
            snprintf(key, sizeof(key), "cnt|%s", c->root_name);
            // Counters aggregate linked goals with no single contributor, so no face (as on the tracker).
            consider(COMPACT_COUNTER_COUNTERS, COMPACT_COUNTER_COUNTERS, c->root_name, key,
                     compact_pop_progress(settings, COMPACT_COUNTER_COUNTERS, c->completed_count),
                     compact_pop_done(settings, COMPACT_COUNTER_COUNTERS, c->done), false,
                     nullptr, nullptr, c->icon_path, itext, false, nullptr);
        }
    }
    eng.seeded = true; // baseline recorded; subsequent frames pop on increases

    // A completed run hands the stack over to the completion showcase (compact_render_promo_line), so
    // no goal group from the final moments lingers under the panel. The diff above still ran, so the
    // baseline stays current and resetting the run doesn't dump the whole template into the stack.
    if (td && td->run_completed) {
        eng.groups.clear();
        return;
    }

    float icon_size = settings->compact_pop_icon_size;
    float line_h = icon_size + COMPACT_POP_LINE_GAP;
    int max_lines = settings->compact_stack_max_lines;

    // Expire by hold: a group whose hold has run out just disappears (no slide-off). Iterate back to
    // front so erasing doesn't skip entries. An infinite hold skips this entirely, leaving overflow
    // as the only way a group leaves the stack.
    if (settings->compact_stack_hold_time > COMPACT_STACK_HOLD_TIME_INFINITE) {
        for (int i = (int) eng.groups.size() - 1; i >= 0; i--) {
            eng.groups[i].hold_left -= dt;
            if (eng.groups[i].hold_left <= 0.0f) eng.groups.erase(eng.groups.begin() + i);
        }
    }

    // Overflow: while more than the line budget is on-screen, drop the oldest (bottom) group
    // immediately. A 2-line group counts as 2 lines.
    for (;;) {
        int lines = 0, oldest = -1;
        for (int i = 0; i < (int) eng.groups.size(); i++) {
            lines += eng.groups[i].two_line ? 2 : 1;
            oldest = i;
        }
        if (lines <= max_lines || oldest < 0) break;
        eng.groups.erase(eng.groups.begin() + oldest);
    }

    if (eng.groups.empty()) return;

    // Layout: settle groups top-to-bottom from stack_top; a fresh group starts hidden behind the
    // panel bottom and slides down into place (the only animation - removal is instant).
    float y = stack_top;
    float rise = settings->compact_stack_rise_time;
    for (auto &g : eng.groups) {
        float gh = (g.two_line ? 2.0f : 1.0f) * line_h;
        float target = y;
        y += gh;
        if (!g.placed) {
            g.anim_y = panel_bottom - gh; // emerge from under the panel
            g.placed = true;
        }
        float f = (rise <= 0.0f) ? 1.0f : fminf(1.0f, dt / rise);
        g.anim_y += (target - g.anim_y) * f;
    }

    // Clip the stack to the band below the panel so a group emerging from under the panel stays
    // hidden until it clears the panel's bottom edge (positional reveal, no alpha).
    int window_w = 0;
    SDL_GetWindowSizeInPixels(o->window, &window_w, nullptr);
    SDL_Rect clip = {0, (int) snap_px(panel_bottom), window_w, window_bottom - (int) snap_px(panel_bottom)};
    if (clip.h < 0) clip.h = 0;
    SDL_SetRenderClipRect(o->renderer, &clip);

    // Reserve a face-sized slot right of the icon on every line while co-op faces are active, so the
    // text left edge stays aligned whether or not a given line credits a single player.
    float face_slot = faces_on ? (icon_size + COMPACT_POP_FACE_GAP) : 0.0f;
    // Left-aligned: icon at stack_x, then the face slot, then the text. Right-aligned mirrors the row
    // about the panel's right edge (stack_x + panel_w): the icon sits flush to that edge, the face to
    // its left, and the text is right-aligned to the face slot's left.
    float stack_right = stack_x + panel_w;
    auto draw_line = [&](const std::string &icon, const std::string &text, float ly, bool shared,
                         const std::string &shared_icon, const std::string &face_uuid) {
        float icon_x = right_align ? (stack_right - icon_size) : stack_x;
        float face_x = right_align ? (stack_right - icon_size - face_slot)
                                   : (stack_x + icon_size + COMPACT_POP_FACE_GAP);
        compact_draw_icon(o, icon.c_str(), icon_x, ly, icon_size);
        if (shared && settings->compact_stack_shared_icon_size > 0.0f)
            compact_draw_icon(o, shared_icon.c_str(), icon_x, ly, settings->compact_stack_shared_icon_size);
        if (faces_on && !face_uuid.empty()) {
            AccountType acc = overlay_coop_account_type(o, face_uuid.c_str());
            compact_draw_face(o, face_uuid.c_str(), acc, face_x, ly, icon_size);
        }
        if (!text.empty()) {
            SDL_Texture *tt = get_text_texture_from_cache(o, stack_font, text.c_str(), text_color);
            if (tt) {
                float tw = 0.0f, th = 0.0f;
                SDL_GetTextureSize(tt, &tw, &th);
                float text_x = right_align
                                   ? (stack_right - icon_size - face_slot - COMPACT_POP_TEXT_GAP - tw)
                                   : (stack_x + icon_size + face_slot + COMPACT_POP_TEXT_GAP);
                SDL_FRect d = {snap_px(text_x), snap_px(ly + (icon_size - th) / 2.0f), tw, th};
                SDL_RenderTexture(o->renderer, tt, nullptr, &d);
            }
        }
    };
    for (auto &g : eng.groups) {
        if (g.two_line) {
            // The face rides the item (completed) line; the parent line reserves the slot but shows none.
            draw_line(g.parent_icon, g.parent_text, g.anim_y, false, g.parent_icon, std::string());
            draw_line(g.item_icon, g.item_text, g.anim_y + line_h, g.item_shared, g.parent_icon, g.face_uuid);
        } else {
            draw_line(g.item_icon, g.item_text, g.anim_y, g.item_shared, g.parent_icon, g.face_uuid);
        }
    }
    SDL_SetRenderClipRect(o->renderer, nullptr);
}

// Widest line (icon + gap + text) any pop-out could ever show for the current stack selection, so the
// overlay window can be widened to fit long lines instead of clipping them. Uses the widest digit
// repeated over each total (like the panel) so the width is stable for the whole run. Returns 0 if
// nothing can pop. Mirrors compact_render_stack's walk, filters and text (incl. the [x]/[o] box).
static float compact_stack_worst_width(Overlay *o, const Tracker *t, const AppSettings *settings) {
    const TemplateData *td = (t && t->template_data) ? t->template_data : nullptr;
    if (!td) return 0.0f;
    // Cache the result: it only depends on the template (totals + names) and the stack settings, and
    // any settings change restarts the overlay - so it is stable for a run and need only be measured
    // once per template (keyed by the same signature the stack engine uses).
    static unsigned long long cached_sig = ~0ULL;
    static float cached_w = 0.0f;
    unsigned long long sig = compact_stack_signature(td);
    if (sig == cached_sig) return cached_w;
    TTF_Font *font = o->compact_stack_font ? o->compact_stack_font : o->font;
    char wdig = compact_widest_digit(font);
    float max_text = 0.0f;
    auto measure = [&](const char *s) {
        int w = 0;
        TTF_MeasureString(font, s, 0, 0, &w, nullptr);
        if ((float) w > max_text) max_text = (float) w;
    };
    char buf[300], cnt[64];
    char open[16]; // worst-case open-ended count: widest digit x 6 (covers up to 999999)
    for (int i = 0; i < 6; i++) open[i] = wdig;
    open[6] = '\0';
    for (int i = 0; i < td->advancement_count; i++) {
        TrackableCategory *a = td->advancements[i];
        if (!a || goal_is_hidden(a->is_hidden, settings)) continue;
        bool recipe = a->is_recipe;
        OverlayCompactCounterType wk = recipe ? COMPACT_COUNTER_RECIPES : COMPACT_COUNTER_ADVANCEMENTS;
        OverlayCompactCounterType ck = recipe ? COMPACT_COUNTER_RECIPE_CRITERIA : COMPACT_COUNTER_CRITERIA;
        const char *an = compact_display_name(a->display_name, a->root_name);
        if (compact_stack_allows(settings, wk, wk, nullptr)) measure(an);
        if (compact_stack_allows(settings, ck, ck, a->root_name)) {
            compact_worst_count(cnt, sizeof(cnt), a->criteria_progress_total, wdig);
            snprintf(buf, sizeof(buf), "%s (%s)", an, cnt);
            measure(buf);
            for (int j = 0; j < a->criteria_count; j++) {
                TrackableItem *c = a->criteria[j];
                if (!c || goal_is_hidden(c->is_hidden, settings)) continue;
                measure(compact_display_name(c->display_name, c->root_name));
            }
        }
    }
    for (int i = 0; i < td->stat_count; i++) {
        TrackableCategory *s = td->stats[i];
        if (!s || goal_is_hidden(s->is_hidden, settings)) continue;
        const char *sn = compact_display_name(s->display_name, s->root_name);
        if (s->is_single_stat_category) {
            if (s->criteria_count < 1 || !s->criteria[0]) continue;
            int goal = s->criteria[0]->goal;
            if (goal <= 0 && goal != -1) continue;
            if (!compact_stack_allows(settings, COMPACT_COUNTER_STATS, COMPACT_COUNTER_STATS, s->root_name)) continue;
            if (goal > 0) {
                compact_worst_count(cnt, sizeof(cnt), goal, wdig);
                snprintf(buf, sizeof(buf), "[x] %s (%s)", sn, cnt);
            } else {
                snprintf(buf, sizeof(buf), "[x] %s (%s)", sn, open);
            }
            measure(buf);
        } else {
            if (!compact_stack_allows(settings, COMPACT_COUNTER_SUB_STATS, COMPACT_COUNTER_SUB_STATS, s->root_name)) continue;
            compact_worst_count(cnt, sizeof(cnt), s->criteria_count, wdig);
            snprintf(buf, sizeof(buf), "[x] %s (%s)", sn, cnt);
            measure(buf);
            for (int j = 0; j < s->criteria_count; j++) {
                TrackableItem *sub = s->criteria[j];
                if (!sub || goal_is_hidden(sub->is_hidden, settings)) continue;
                const char *subn = compact_display_name(sub->display_name, sub->root_name);
                if (sub->goal > 0) {
                    compact_worst_count(cnt, sizeof(cnt), sub->goal, wdig);
                    snprintf(buf, sizeof(buf), "[x] %s (%s)", subn, cnt);
                } else {
                    snprintf(buf, sizeof(buf), "[x] %s (%s)", subn, open);
                }
                measure(buf);
            }
        }
    }
    for (int i = 0; i < td->unlock_count; i++) {
        TrackableItem *u = td->unlocks[i];
        if (!u || goal_is_hidden(u->is_hidden, settings)) continue;
        if (!compact_stack_allows(settings, COMPACT_COUNTER_UNLOCKS, COMPACT_COUNTER_UNLOCKS, nullptr)) continue;
        measure(compact_display_name(u->display_name, u->root_name));
    }
    for (int i = 0; i < td->custom_goal_count; i++) {
        TrackableItem *c = td->custom_goals[i];
        if (!c || goal_is_hidden(c->is_hidden, settings)) continue;
        if (!compact_stack_allows(settings, COMPACT_COUNTER_CUSTOM, COMPACT_COUNTER_CUSTOM, c->root_name)) continue;
        const char *cn = compact_display_name(c->display_name, c->root_name);
        if (c->goal > 0) {
            compact_worst_count(cnt, sizeof(cnt), c->goal, wdig);
            snprintf(buf, sizeof(buf), "[x] %s (%s)", cn, cnt); // reserve the done-marker width
        } else {
            snprintf(buf, sizeof(buf), "[x] %s (%s)", cn, open);
        }
        measure(buf);
    }
    for (int i = 0; i < td->multi_stage_goal_count; i++) {
        MultiStageGoal *g = td->multi_stage_goals[i];
        if (!g || goal_is_hidden(g->is_hidden, settings)) continue;
        if (!compact_stack_allows(settings, COMPACT_COUNTER_MULTISTAGE, COMPACT_COUNTER_MULTISTAGE, g->root_name)) continue;
        char gn[224];
        compact_ms_label(gn, sizeof(gn), compact_display_name(g->display_name, g->root_name));
        for (int j = 0; j < g->stage_count; j++) {
            const SubGoal *st = (g->stages) ? g->stages[j] : nullptr;
            if (!st) continue;
            if (st->type == SUBGOAL_STAT && st->required_progress > 0) {
                compact_worst_count(cnt, sizeof(cnt), st->required_progress, wdig);
                snprintf(buf, sizeof(buf), "%s %s (%s)", gn, st->display_text, cnt);
            } else if (st->type == SUBGOAL_STAT && st->required_progress == -1) {
                snprintf(buf, sizeof(buf), "%s %s (%s)", gn, st->display_text, open);
            } else {
                snprintf(buf, sizeof(buf), "%s %s", gn, st->display_text);
            }
            measure(buf);
        }
    }
    for (int i = 0; i < td->counter_goal_count; i++) {
        CounterGoal *c = td->counter_goals[i];
        if (!c || goal_is_hidden(c->is_hidden, settings)) continue;
        if (!compact_stack_allows(settings, COMPACT_COUNTER_COUNTERS, COMPACT_COUNTER_COUNTERS, c->root_name)) continue;
        const char *cn = compact_display_name(c->display_name, c->root_name);
        compact_worst_count(cnt, sizeof(cnt), c->linked_goal_count, wdig);
        snprintf(buf, sizeof(buf), "[x] %s (%s)", cn, cnt); // reserve the done-marker width
        measure(buf);
    }
    cached_sig = sig;
    // Always reserve the co-op contributor face slot (icon-sized) in the worst-case width, even in
    // singleplayer, so the auto-fitted window never has to grow when a co-op face appears.
    float face_slot = settings->compact_pop_icon_size + COMPACT_POP_FACE_GAP;
    cached_w = (max_text <= 0.0f)
                   ? 0.0f
                   : (settings->compact_pop_icon_size + face_slot + COMPACT_POP_TEXT_GAP + max_text);
    return cached_w;
}

// ---------------------------------------------------------------------------
// Compact promo line + completed-run supporter showcase (Stage 5)
//
// The promo takes the stack's first slot (right under the panel) whenever no goal is popping: the
// Advancely logo, then the same rotating promo line the other modes show. It is how the free tracker
// gets promoted, so it is never toggleable, and it can't be cropped away in OBS without also cropping
// the slot every goal pops into. A line that fits the window just sits there; only one too long to fit
// moves, bouncing between its two ends so all of it can be read without ever widening the window (a
// long URL must never stretch the narrow panel).
//
// A completed run pins the promo - the goal stack is cleared, so the slot is always free - and stacks
// supporters below it: one pops in every COMPACT_SUPPORTER_GAP_MIN..MAX seconds at the second row and
// pushes the older ones down, each holding for the stack's hold time. At the default timings a
// supporter outlives the wait for the next one, so they overlap and at least one is always up; the
// last one standing is never expired, which keeps that true whatever the hold time is set to.
// ---------------------------------------------------------------------------

#define COMPACT_PROMO_ICON "Advancely_Logo_NoText.png" // Drawn in front of the promo text
#define COMPACT_PROMO_DONATE_TEXT "Donate (mentioning 'Advancely') to be featured!"
#define COMPACT_PROMO_SCROLL_SPEED 60.0f // On-screen px per second an overlong promo line scrolls
#define COMPACT_PROMO_BOUNCE_PAUSE 1.0f // Seconds an overlong line rests at each end before reversing
#define COMPACT_SUPPORTER_GAP_MIN 0.5f // Shortest wait before the next supporter pops in
#define COMPACT_SUPPORTER_GAP_MAX 8.0f // Longest wait before the next supporter pops in

// One supporter currently up in the showcase.
struct CompactSupporterPop {
    int index; // into SUPPORTERS (and the run's fixed emote list)
    float hold_left; // seconds until it leaves (the last one standing never does)
};

struct CompactPromoState {
    Uint64 last_tick = 0;
    // Bounce state for a promo line too long for the window: scroll_x runs from 0 (its left edge shown)
    // down to -overflow (its right edge shown), and dir flips at each end after a short rest.
    float scroll_x = 0.0f;
    float dir = -1.0f;
    float pause_left = COMPACT_PROMO_BOUNCE_PAUSE;
    char shown_text[256] = {0}; // the line the bounce state belongs to (a new line restarts it)
    bool run_was_complete = false;
    // Entrance slide: the promo (and the showcase under it) emerge from under the panel like a goal
    // pop-out every time the stack drains. appear_y is the animated top of the promo row; appear_placed
    // is cleared whenever a goal takes the slot back, so the slide replays on the next drain.
    float appear_y = 0.0f;
    bool appear_placed = false;
    // Completed-run showcase. Each supporter's emote is drawn once, when the run completes, so a
    // supporter always reappears with the same one - like the showcase in the other two modes.
    const char *emotes[NUM_SUPPORTERS > 0 ? NUM_SUPPORTERS : 1] = {nullptr};
    std::vector<int> order; // supporter indices, reshuffled once every one of them has popped in
    int pos = 0; // how far through `order` this pass has got
    float spawn_left = 0.0f; // seconds until the next supporter pops in
    std::vector<CompactSupporterPop> pops; // newest first (index 0 = the row right under the promo)
};

static CompactPromoState s_compact_promo;

// A random wait before the next supporter pops in, so the showcase never feels metronomic.
static float compact_supporter_gap() {
    float f = (float) rand() / (float) RAND_MAX;
    return COMPACT_SUPPORTER_GAP_MIN + f * (COMPACT_SUPPORTER_GAP_MAX - COMPACT_SUPPORTER_GAP_MIN);
}

// How long a supporter stays up: the stack's hold time, except on an infinite hold, which would pile
// every supporter up forever.
static float compact_supporter_hold(const AppSettings *settings) {
    return settings->compact_stack_hold_time > 0.0f
               ? settings->compact_stack_hold_time
               : DEFAULT_COMPACT_STACK_HOLD_TIME;
}

// Reshuffles the running order so every supporter pops in once per pass, in a fresh random order.
// Their emotes are NOT re-drawn here: those stay fixed for the whole completed run.
static void compact_promo_shuffle_order(CompactPromoState &p) {
    p.order.clear();
    for (int i = 0; i < NUM_SUPPORTERS; i++) p.order.push_back(i);
    for (int i = (int) p.order.size() - 1; i > 0; i--) { // Fisher-Yates
        int j = rand() % (i + 1);
        int tmp = p.order[i];
        p.order[i] = p.order[j];
        p.order[j] = tmp;
    }
    p.pos = 0;
}

// Draws one line of showcase text, clipped to the band right of the icons so a long line is cut at the
// window edge instead of widening it. `tx` may sit outside the band: an overlong promo line scrolls.
// `top_limit` is the highest y the text may show at (the panel's bottom), so a row still sliding in
// from under the panel stays hidden until it clears that edge - matching the goal stack's reveal.
static void compact_draw_band_text(Overlay *o, SDL_Texture *tex, float tx, float row_y, float band_x,
                                   float band_w, float row_h, float top_limit) {
    if (!tex) return;
    float tw = 0.0f, th = 0.0f;
    SDL_GetTextureSize(tex, &tw, &th);
    float ty = snap_px(row_y + (row_h - th) / 2.0f);
    // The band covers the icon's row and the text, whichever is taller, but never rises above the panel.
    float clip_top = fmaxf(fminf(ty, row_y), top_limit);
    float clip_bottom = fmaxf(row_y + row_h, ty + th);
    if (clip_bottom <= clip_top) return; // still entirely tucked under the panel: nothing shows yet
    SDL_Rect clip = {(int) band_x, (int) clip_top, (int) snap_px(band_w), (int) snap_px(clip_bottom - clip_top)};
    SDL_SetRenderClipRect(o->renderer, &clip);
    SDL_FRect d = {snap_px(tx), ty, tw, th};
    SDL_RenderTexture(o->renderer, tex, nullptr, &d);
    SDL_SetRenderClipRect(o->renderer, nullptr);
}

// Draws the promo line, plus the supporter showcase below it once the run is complete. Called every
// frame from overlay_render_compact after the goal stack; the promo only shows when the stack is quiet.
static void compact_render_promo_line(Overlay *o, const Tracker *t, const AppSettings *settings,
                                      float stack_x, float panel_w, float panel_bottom, float stack_top,
                                      int window_w, SDL_Color text_color) {
    CompactPromoState &p = s_compact_promo;
    TTF_Font *font = o->compact_stack_font ? o->compact_stack_font : o->font;
    const TemplateData *td = (t && t->template_data) ? t->template_data : nullptr;
    bool run_complete = td && td->run_completed;

    Uint64 now = SDL_GetTicks();
    float dt = (p.last_tick == 0) ? 0.0f : (float) (now - p.last_tick) / 1000.0f;
    p.last_tick = now;
    if (dt > 0.25f) dt = 0.25f; // clamp after a stall / first frame

    // Completing a run fixes every supporter's emote for the rest of it and starts the showcase, with
    // the first supporter popping in at once so the screen never sits there with none up. Resetting
    // the run clears them back out.
    if (run_complete != p.run_was_complete) {
        p.run_was_complete = run_complete;
        p.pops.clear();
        if (run_complete) {
            for (int i = 0; i < NUM_SUPPORTERS; i++)
                p.emotes[i] = NUM_SUPPORTER_ICONS > 0 ? SUPPORTER_ICONS[rand() % NUM_SUPPORTER_ICONS] : "";
            compact_promo_shuffle_order(p);
            p.spawn_left = 0.0f;
        }
    }

    float icon_size = settings->compact_pop_icon_size;
    float line_h = icon_size + COMPACT_POP_LINE_GAP;
    int supporter_rows = settings->compact_stack_max_lines - 1; // the promo itself owns the first row

    if (run_complete && NUM_SUPPORTERS > 0 && supporter_rows > 0) {
        // Pop the next supporter in above the others once its wait runs out, walking the shuffled
        // order and reshuffling for the next pass once every supporter has had a turn.
        p.spawn_left -= dt;
        if (p.spawn_left <= 0.0f) {
            if (p.pos >= (int) p.order.size()) compact_promo_shuffle_order(p);
            if (!p.order.empty()) {
                CompactSupporterPop pop;
                pop.index = p.order[p.pos++];
                pop.hold_left = compact_supporter_hold(settings);
                p.pops.insert(p.pops.begin(), pop);
            }
            p.spawn_left = compact_supporter_gap();
        }
        // Expire by hold, oldest first - but never the last one standing, so at least one supporter is
        // always up however the hold time and the random waits happen to line up.
        for (int i = (int) p.pops.size() - 1; i >= 0 && p.pops.size() > 1; i--) {
            p.pops[i].hold_left -= dt;
            if (p.pops[i].hold_left <= 0.0f) p.pops.erase(p.pops.begin() + i);
        }
        while ((int) p.pops.size() > supporter_rows) p.pops.pop_back(); // overflow: cut the oldest
    }

    // A popping goal owns the promo's slot; the promo waits for the stack to drain, then slides in.
    // While a goal is up the promo hides at once and its entrance re-arms, so it slides in fresh the
    // next time the stack drains. (A completed run has no goal groups - compact_render_stack clears
    // them - so the showcase always shows.)
    if (!s_compact_stack.groups.empty()) {
        p.appear_placed = false;
        return;
    }

    // A right-aligned panel mirrors the promo/showcase like the goal stack: the icon sits flush to the
    // panel's right edge and the text band runs to its left, out to the window's far padding.
    bool right_align = settings->compact_panel_align == OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT;
    float pad = settings->compact_panel_padding;
    float stack_right = stack_x + panel_w;
    float icon_x = right_align ? snap_px(stack_right - icon_size) : stack_x;
    float band_x, band_w;
    if (right_align) {
        band_x = snap_px(pad);
        band_w = snap_px(stack_right - icon_size - COMPACT_POP_TEXT_GAP) - band_x; // band left of the icons
    } else {
        band_x = snap_px(stack_x + icon_size + COMPACT_POP_TEXT_GAP); // text band, right of the icons
        band_w = (float) window_w - pad - band_x;
    }
    if (band_w <= 0.0f) return;

    // Entrance slide: emerge from under the panel bottom and settle into the first stack slot at
    // stack_top, exactly like a goal pop-out. `promo_y` drives the promo row; `slide` shifts the
    // supporters below it so the whole showcase moves as one. A clip below the panel hides whatever is
    // still tucked under it (positional reveal, no alpha, so OBS chroma keying stays clean).
    float rise = settings->compact_stack_rise_time;
    if (!p.appear_placed) {
        p.appear_y = panel_bottom - line_h; // start hidden under the panel
        p.appear_placed = true;
    }
    float rise_f = (rise <= 0.0f) ? 1.0f : fminf(1.0f, dt / rise);
    p.appear_y += (stack_top - p.appear_y) * rise_f;
    float promo_y = snap_px(p.appear_y);
    float slide = promo_y - stack_top; // <= 0 while sliding in, 0 once settled
    int reveal_w = 0, reveal_h = 0;
    SDL_GetWindowSizeInPixels(o->window, &reveal_w, &reveal_h);
    SDL_Rect reveal_clip = {0, (int) snap_px(panel_bottom), reveal_w, reveal_h - (int) snap_px(panel_bottom)};
    if (reveal_clip.h < 0) reveal_clip.h = 0;

    // --- The promo row (slides in with promo_y, clipped to below the panel) ---
    char logo_path[MAX_PATH_LENGTH];
    snprintf(logo_path, sizeof(logo_path), "%s/gui/%s", get_application_dir(), COMPACT_PROMO_ICON);
    SDL_SetRenderClipRect(o->renderer, &reveal_clip);
    compact_draw_icon(o, logo_path, icon_x, promo_y, icon_size);
    SDL_SetRenderClipRect(o->renderer, nullptr);

    // The rotating line is the shared one the other modes cycle through (advanced in overlay_update),
    // so every mode promotes the same thing at the same time. A completed run pins the donation line.
    const char *promo_text = run_complete ? COMPACT_PROMO_DONATE_TEXT : SOCIALS[o->current_social_index];
    SDL_Texture *promo_tex = get_text_texture_from_cache(o, font, promo_text, text_color);
    if (promo_tex) {
        float tw = 0.0f;
        SDL_GetTextureSize(promo_tex, &tw, nullptr); // only the width decides whether it has to move
        if (strncmp(p.shown_text, promo_text, sizeof(p.shown_text) - 1) != 0) { // a new line starts over
            strncpy(p.shown_text, promo_text, sizeof(p.shown_text) - 1);
            p.shown_text[sizeof(p.shown_text) - 1] = '\0';
            p.scroll_x = 0.0f;
            p.dir = -1.0f;
            p.pause_left = COMPACT_PROMO_BOUNCE_PAUSE;
        }
        // Only a line too long for the band moves: it scrolls until its right edge shows, rests, comes
        // back until its left edge shows, rests, and so on, so all of a long URL can be read.
        float overflow = tw - band_w;
        if (overflow <= 0.0f) {
            p.scroll_x = 0.0f;
            p.dir = -1.0f;
            p.pause_left = COMPACT_PROMO_BOUNCE_PAUSE;
        } else if (p.pause_left > 0.0f) {
            p.pause_left -= dt;
        } else {
            p.scroll_x += p.dir * COMPACT_PROMO_SCROLL_SPEED * dt;
            if (p.scroll_x <= -overflow) {
                p.scroll_x = -overflow;
                p.dir = 1.0f;
                p.pause_left = COMPACT_PROMO_BOUNCE_PAUSE;
            } else if (p.scroll_x >= 0.0f) {
                p.scroll_x = 0.0f;
                p.dir = -1.0f;
                p.pause_left = COMPACT_PROMO_BOUNCE_PAUSE;
            }
        }
        // Left-aligned rests at the band's left and scrolls left to reveal its end; right-aligned rests
        // flush to the band's right (against the icon) and mirrors the same bounce to reveal its start.
        float promo_tx = right_align ? (band_x + band_w - tw - p.scroll_x) : (band_x + p.scroll_x);
        compact_draw_band_text(o, promo_tex, promo_tx, promo_y, band_x, band_w, icon_size, panel_bottom);
    }

    // --- The supporters, stacked below the promo, newest first ---
    if (!run_complete) return;
    for (size_t i = 0; i < p.pops.size(); i++) {
        int si = p.pops[i].index;
        if (si < 0 || si >= NUM_SUPPORTERS) continue;
        // Ride the promo's entrance slide so the whole showcase moves in as one, and clip to below the
        // panel like the promo row.
        float row_y = snap_px(stack_top + (float) (i + 1) * line_h + slide);
        char emote_path[MAX_PATH_LENGTH];
        snprintf(emote_path, sizeof(emote_path), "%s/icons/%s", get_application_dir(),
                 p.emotes[si] ? p.emotes[si] : "");
        SDL_SetRenderClipRect(o->renderer, &reveal_clip);
        compact_draw_icon(o, emote_path, icon_x, row_y, icon_size);
        SDL_SetRenderClipRect(o->renderer, nullptr);
        char supporter_text[128];
        snprintf(supporter_text, sizeof(supporter_text), "%s ($%.2f)", SUPPORTERS[si].name,
                 SUPPORTERS[si].amount);
        SDL_Texture *st = get_text_texture_from_cache(o, font, supporter_text, text_color);
        // Right-aligned hugs the band's right edge (against the icon); left-aligned starts at its left.
        float stx = band_x;
        if (right_align && st) {
            float stw = 0.0f;
            SDL_GetTextureSize(st, &stw, nullptr);
            stx = band_x + band_w - stw;
        }
        compact_draw_band_text(o, st, stx, row_y, band_x, band_w, icon_size, panel_bottom);
    }
}

// Compact render mode: a tall/narrow counter panel (Zesskyo-style). One big "label over count"
// block that cycles through the user-selected entries (whole-section type counts and/or individual
// goals) on a wall-clock timer. The 9-slice panel is sized to the worst-case width across ALL
// selected entries so the background stays fixed for the whole run. Completed/progressing goals then
// slide out from under the panel and stack below it (compact_render_stack), and the promo line fills
// the stack's first slot while it is quiet (compact_render_promo_line). Once the run is complete the
// panel freezes on "RUN COMPLETED!" over the final in-game time.
static void overlay_render_compact(Overlay *o, const Tracker *t, const AppSettings *settings) {
    SDL_Color text_color = {
        settings->overlay_text_color.r, settings->overlay_text_color.g, settings->overlay_text_color.b, 255
    };

    // Fonts: dedicated Compact label/count faces, each falling back to an overlay font.
    TTF_Font *label_font = o->compact_label_font ? o->compact_label_font : o->font;
    TTF_Font *count_font = o->compact_count_font ? o->compact_count_font : o->font_top;

    const TemplateData *td = (t && t->template_data) ? t->template_data : nullptr;
    // The latched completion flag synced from the tracker (honors the per-template thresholds), like
    // the other modes use for their completion screen.
    bool run_complete = td && td->run_completed;

    char label_buf[224];
    char count_buf[64];
    float content_w = 0.0f; // widest line the panel must fit, sizes the panel

    if (run_complete) {
        // The run is over: the panel stops cycling and freezes on the completion screen. The final
        // time comes from the frozen tick count and honors the same IGT options as the other modes.
        snprintf(label_buf, sizeof(label_buf), "RUN COMPLETED!");
        format_time(td->frozen_play_time_ticks, count_buf, sizeof(count_buf), settings->igt_unit_spacing,
                    settings->igt_always_show_ms);
        int lwm = 0, cwm = 0;
        TTF_MeasureString(label_font, label_buf, 0, 0, &lwm, nullptr);
        TTF_MeasureString(count_font, count_buf, 0, 0, &cwm, nullptr);
        content_w = fmaxf((float) lwm, (float) cwm);
    } else {
        // Build the selected cycle, then pick the entry showing this frame. The shared page_index is
        // advanced by the cycle-interval timer (overlay_update) and by SPACE (overlay_events), exactly
        // like Page mode; static when there's only one entry.
        CompactEntry entries[COMPACT_COUNTER_TYPE_COUNT + MAX_COMPACT_CYCLE_ITEMS];
        int entry_count = compact_build_cycle(t, settings, entries, (int) (sizeof(entries) / sizeof(entries[0])));

        int cur_idx = (entry_count > 0) ? (((o->page_index % entry_count) + entry_count) % entry_count) : 0;
        CompactEntry *cur = &entries[cur_idx];

        snprintf(label_buf, sizeof(label_buf), "%s:", cur->label);
        compact_format_count(count_buf, sizeof(count_buf), settings, cur);

        // Worst-case content width across EVERY selected entry: the widest "Label:" plus the widest
        // possible count each can display (widest digit repeated over the total, never the live count).
        // Sizing the panel to this keeps the background a fixed size for the whole run, so it never
        // jumps as the cycle rotates or the numerator grows and stays alignable in OBS.
        char wdig = compact_widest_digit(count_font);
        for (int i = 0; i < entry_count; i++) {
            char lbl[224];
            snprintf(lbl, sizeof(lbl), "%s:", entries[i].label);
            int lwm = 0;
            TTF_MeasureString(label_font, lbl, 0, 0, &lwm, nullptr);
            char wc[64];
            compact_worst_count_entry(wc, sizeof(wc), settings, &entries[i], wdig);
            int cwm = 0;
            TTF_MeasureString(count_font, wc, 0, 0, &cwm, nullptr);
            float w = fmaxf((float) lwm, (float) cwm);
            if (w > content_w) content_w = w;
        }
    }

    SDL_Texture *label_tex = get_text_texture_from_cache(o, label_font, label_buf, text_color);
    SDL_Texture *count_tex = get_text_texture_from_cache(o, count_font, count_buf, text_color);

    float lw = 0.0f, lh = 0.0f, cw = 0.0f, ch = 0.0f;
    if (label_tex) SDL_GetTextureSize(label_tex, &lw, &lh);
    if (count_tex) SDL_GetTextureSize(count_tex, &cw, &ch);

    const float line_gap = 4.0f;
    float pad = settings->compact_panel_padding;
    float border_x = (float) ((settings->compact_panel_inset_left + settings->compact_panel_inset_right) *
                              settings->compact_panel_pixel_scale);
    float border_y = (float) ((settings->compact_panel_inset_top + settings->compact_panel_inset_bottom) *
                              settings->compact_panel_pixel_scale);

    // The count line's text texture includes the font's descent (empty space below the digits).
    // Trim it from the content height so that space folds into the bottom padding instead of
    // showing as extra room under the count, keeping the two lines visually centered.
    int count_descent = TTF_GetFontDescent(count_font);
    if (count_descent < 0) count_descent = -count_descent;

    float content_h = lh + line_gap + ch - (float) count_descent;
    float panel_w = snap_px(content_w + 2.0f * pad + border_x);
    float panel_h = snap_px(content_h + 2.0f * pad + border_y);

    // Reserve room below the panel for the pop-out stack: a pad-sized gap then the full line budget
    // (a 2-line group uses 2 lines). The window height is thus fixed for the whole run (inputs are
    // settings), so the stack always has space and the empty area below is transparent / keyed out.
    float stack_line_h = settings->compact_pop_icon_size + COMPACT_POP_LINE_GAP;
    float stack_reserve = pad + (float) settings->compact_stack_max_lines * stack_line_h;

    // Auto-fit the overlay window to the panel plus a pad-sized margin all around (and the reserved
    // stack area). Only resizes when the needed size actually changes (a new template with more
    // digits, or a settings tweak), so it stays put during a run. The panel is placed by alignment.
    int want_w = (int) snap_px(panel_w + 2.0f * pad);
    // Also widen to fit the worst-case pop-out line so long stack text isn't clipped. The stack
    // left-aligns at the panel's left edge (pad for Left alignment), so it needs stack_w + 2*pad.
    float stack_w = compact_stack_worst_width(o, t, settings);
    if (stack_w > 0.0f) {
        int stack_want_w = (int) snap_px(stack_w + 2.0f * pad);
        if (stack_want_w > want_w) want_w = stack_want_w;
    }
    if (want_w < COMPACT_MIN_WINDOW_WIDTH) want_w = COMPACT_MIN_WINDOW_WIDTH;
    int want_h = (int) snap_px(panel_h + 2.0f * pad + stack_reserve);
    int cur_w = 0, cur_h = 0;
    SDL_GetWindowSize(o->window, &cur_w, &cur_h);
    if (cur_w != want_w || cur_h != want_h) SDL_SetWindowSize(o->window, want_w, want_h);

    // Place the panel within the (possibly min-width-widened) window per the alignment setting.
    // Left keeps the left edge fixed as the panel grows (easy to left-align in OBS), Right the
    // right edge, Center keeps it centered.
    float panel_x;
    if (settings->compact_panel_align == OVERLAY_PROGRESS_TEXT_ALIGN_LEFT)
        panel_x = snap_px(pad);
    else if (settings->compact_panel_align == OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT)
        panel_x = snap_px((float) want_w - panel_w - pad);
    else
        panel_x = snap_px(((float) want_w - panel_w) / 2.0f);
    float panel_y = snap_px(pad);

    SDL_Texture *panel_tex = o->compact_panel ? o->compact_panel : anim_current_frame(o->compact_panel_anim);
    SDL_FRect panel_dest = {panel_x, panel_y, panel_w, panel_h};
    draw_nine_slice(o->renderer, panel_tex, &panel_dest,
                    settings->compact_panel_inset_left, settings->compact_panel_inset_right,
                    settings->compact_panel_inset_top, settings->compact_panel_inset_bottom,
                    settings->compact_panel_pixel_scale);

    float content_top = panel_y + (float) (settings->compact_panel_inset_top * settings->compact_panel_pixel_scale) +
                        pad;
    if (label_tex) {
        SDL_FRect d = {snap_px(panel_x + (panel_w - lw) / 2.0f), content_top, lw, lh};
        SDL_RenderTexture(o->renderer, label_tex, nullptr, &d);
    }
    // The whole count line (completion marker included) is centered as one block.
    if (count_tex) {
        SDL_FRect d = {snap_px(panel_x + (panel_w - cw) / 2.0f), snap_px(content_top + lh + line_gap), cw, ch};
        SDL_RenderTexture(o->renderer, count_tex, nullptr, &d);
    }

    // Co-op specific-player/ghost view: pin the selected player's face at the panel's bottom-right,
    // drawn over the panel (offsets inset its bottom-right corner from the panel's; size 0 hides it).
    // The merged All-Players view uses per-line stack faces instead, so nothing is pinned there.
    float pf_size = settings->compact_coop_panel_face_size;
    if (o->coop_lobby_active && o->coop_selected_uuid[0] != '\0' && pf_size > 0.0f) {
        AccountType pf_acc = o->coop_selected_offline ? ACCOUNT_OFFLINE : ACCOUNT_ONLINE;
        float pf_x = panel_x + panel_w - pf_size - settings->compact_coop_panel_face_offset_x;
        float pf_y = (panel_y + panel_h) - pf_size - settings->compact_coop_panel_face_offset_y;
        // Keep the face fully inside the overlay window regardless of the offsets: clamp its
        // top-left so the whole face stays within [0, want] on each axis (upper bound first so
        // the 0 lower bound wins if the face is somehow larger than the window).
        float pf_max_x = (float) want_w - pf_size;
        float pf_max_y = (float) want_h - pf_size;
        if (pf_x > pf_max_x) pf_x = pf_max_x;
        if (pf_x < 0.0f) pf_x = 0.0f;
        if (pf_y > pf_max_y) pf_y = pf_max_y;
        if (pf_y < 0.0f) pf_y = 0.0f;
        compact_draw_face(o, o->coop_selected_uuid, pf_acc, pf_x, pf_y, pf_size);
    }

    // Diff the template and draw the pop-out stack below the panel, then the promo line in the first
    // slot if no goal is using it. Both always left-align to the panel's left edge regardless of the
    // panel alignment setting.
    float panel_bottom = panel_y + panel_h;
    float stack_top = snap_px(panel_bottom + pad);
    compact_render_stack(o, t, settings, panel_x, panel_w, panel_bottom, stack_top, want_h, text_color);
    compact_render_promo_line(o, t, settings, panel_x, panel_w, panel_bottom, stack_top, want_w, text_color);
}

// Compute the vertical layout (row anchors + window height) from the loaded fonts.
//
// The base numbers (47 / 108 / 260 / 420) were tuned for the default Minecraft
// font at DEFAULT_OVERLAY_FONT_SIZE. Taller text needs more vertical room, so every
// anchor is offset by the extra line height it must accommodate. The top info bar
// and the row 2/3 text can use different sizes, so we track two deltas against the
// reference (default) line height:
//   dt = top bar line height  - reference   (the top bar sits above everything)
//   dr = row 2/3 line height  - reference   (rows 2/3 each stack 2 text lines)
// The coefficient on each delta is how many lines of that kind sit above the anchor:
//   row1_y : 1 top line
//   row2_y : 1 top line
//   row3_y : 1 top line + row 2's 2 lines
//   height : 1 top line + rows 2 and 3's 2 lines each
// At dt == dr == 0 the anchors equal the original tuned values, preserving the
// default spacing exactly.
//
// On top of the font deltas the user can add custom per-gap spacing (all default 0
// and never negative, so the stock layout is the minimum). Each gap cumulatively
// shifts every anchor below it and grows the window height, matching the stacking:
//   g1 = top bar -> row 1, g2 = row 1 -> row 2, g3 = row 2 -> row 3, g4 = row 3 -> bottom.
//
// This is called once, after the fonts are loaded in overlay_new. The overlay
// process is fully restarted whenever settings change, so the layout never needs
// to be recomputed at runtime and the window never resizes without a settings change.
static void overlay_compute_layout(Overlay *o, const AppSettings *settings) {
    // Compact mode uses a completely different, tall/narrow layout: a counter panel near the top plus
    // the reserved pop-out stack below it. Width stays user-controlled (overlay_window.w); overlay_render
    // auto-fits the exact panel size each frame. This initial height must match overlay_render_compact's
    // want_h so the window is created at the right size (no startup resize flash).
    if (settings->overlay_render_mode == OVERLAY_RENDER_MODE_COMPACT) {
        TTF_Font *label_font = o->compact_label_font ? o->compact_label_font : o->font;
        TTF_Font *count_font = o->compact_count_font ? o->compact_count_font : o->font_top;
        float label_lh = (float) TTF_GetFontHeight(label_font);
        float count_lh = (float) TTF_GetFontHeight(count_font);
        int count_descent = TTF_GetFontDescent(count_font);
        if (count_descent < 0) count_descent = -count_descent;
        const float line_gap = 4.0f;
        float pad = settings->compact_panel_padding;
        float border_y = (float) ((settings->compact_panel_inset_top + settings->compact_panel_inset_bottom) *
                                  settings->compact_panel_pixel_scale);
        float panel_h = label_lh + line_gap + count_lh - (float) count_descent + 2.0f * pad + border_y;
        float stack_line_h = settings->compact_pop_icon_size + COMPACT_POP_LINE_GAP;
        float stack_reserve = pad + (float) settings->compact_stack_max_lines * stack_line_h;
        o->layout_row1_y = 0.0f;
        o->layout_row2_y = 0.0f;
        o->layout_row3_y = 0.0f;
        o->layout_height = (int) snap_px(pad + panel_h + pad + stack_reserve);
        return;
    }

    const float BASE_ROW1_Y = 47.0f; // Centered between the top text and row 2 (13px above and below)
    const float BASE_ROW2_Y = 108.0f;
    const float BASE_ROW3_Y = 260.0f;
    const float BASE_HEIGHT = (float) OVERLAY_FIXED_HEIGHT; // 420

    float top_line_height = (float) TTF_GetFontHeight(o->font_top);
    float row_line_height = (float) TTF_GetFontHeight(o->font);

    // Self-calibrate the reference against the bundled default font at the same
    // base size, so the anchoring stays correct even if the default font changes.
    float ref_line_height = row_line_height;
    char ref_font_path[1024];
    snprintf(ref_font_path, sizeof(ref_font_path), "%s/fonts/%s", get_application_dir(), DEFAULT_OVERLAY_FONT);
    TTF_Font *ref_font = TTF_OpenFont(ref_font_path, DEFAULT_OVERLAY_FONT_SIZE);
    if (ref_font) {
        ref_line_height = (float) TTF_GetFontHeight(ref_font);
        TTF_CloseFont(ref_font);
    } else {
        log_message(LOG_ERROR, "[OVERLAY] Failed to open reference font '%s' for layout calibration: %s\n",
                    ref_font_path, SDL_GetError());
    }

    float dt = top_line_height - ref_line_height;
    float dr = row_line_height - ref_line_height;

    // Custom vertical spacing (0 unless the user enables it) is added cumulatively.
    float g1 = 0.0f, g2 = 0.0f, g3 = 0.0f, g4 = 0.0f;
    if (settings->overlay_custom_vertical_spacing_enabled) {
        g1 = settings->overlay_gap_top_to_row1;
        g2 = settings->overlay_gap_row1_to_row2;
        g3 = settings->overlay_gap_row2_to_row3;
        g4 = settings->overlay_gap_row3_to_bottom;
    }

    o->layout_row1_y = snap_px(BASE_ROW1_Y + dt + g1);
    o->layout_row2_y = snap_px(BASE_ROW2_Y + dt + g1 + g2);
    o->layout_row3_y = snap_px(BASE_ROW3_Y + dt + 2.0f * dr + g1 + g2 + g3);
    o->layout_height = (int) snap_px(BASE_HEIGHT + dt + 4.0f * dr + g1 + g2 + g3 + g4);
}


bool overlay_new(Overlay **overlay, const AppSettings *settings) {
    // dereference once and use calloc
    *overlay = (Overlay *) calloc(1, sizeof(Overlay));
    // Check here if calloc failed
    if (*overlay == nullptr) {
        log_message(LOG_ERROR, "[OVERLAY] Error allocating memory for overlay.\n");
        return false;
    }

    // temp variable to not dereference over and over again
    Overlay *o = *overlay;

    // Caches are zero initialized by calloc

    // Create the SDL window and renderer
    if (!overlay_init_sdl(o, settings)) {
        overlay_free(overlay, settings);
        return false;
    }

    o->text_engine = TTF_CreateRendererTextEngine(o->renderer);
    if (!o->text_engine) {
        log_message(LOG_ERROR, "[OVERLAY] Failed to create text engine: %s\n", SDL_GetError());
        overlay_free(overlay, settings);
        return false;
    }

    // Load global background textures using settings and cache
    char full_path[MAX_PATH_LENGTH]; // Buffer for constructing full path

    // Helper lambda to load one background
    auto load_bg = [&](const char *setting_path, const char *default_path,
                       SDL_Texture **tex_target, AnimatedTexture **anim_target) {
        *tex_target = nullptr;
        *anim_target = nullptr;
        snprintf(full_path, sizeof(full_path), "%s/gui/%s", get_application_dir(), setting_path);

        if (strstr(full_path, ".gif")) {
            *anim_target = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
                                                           &o->anim_cache_capacity, full_path, SDL_SCALEMODE_NEAREST);
        } else {
            *tex_target = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                 &o->texture_cache_capacity, full_path, SDL_SCALEMODE_NEAREST);
        }

        // Fallback if loading failed
        if (!*tex_target && !*anim_target) {
            log_message(LOG_ERROR, "[OVERLAY] Failed to load background: %s. Trying default...\n", setting_path);
            snprintf(full_path, sizeof(full_path), "%s/gui/%s", get_application_dir(), default_path);
            if (strstr(full_path, ".gif")) {
                *anim_target = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
                                                               &o->anim_cache_capacity, full_path,
                                                               SDL_SCALEMODE_NEAREST);
            } else {
                *tex_target = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                     &o->texture_cache_capacity, full_path, SDL_SCALEMODE_NEAREST);
            }
        }
    };

    load_bg(settings->adv_bg_path, DEFAULT_ADV_BG_PATH, &o->adv_bg, &o->adv_bg_anim);
    load_bg(settings->adv_bg_half_done_path, DEFAULT_ADV_BG_HALF_DONE_PATH, &o->adv_bg_half_done,
            &o->adv_bg_half_done_anim);
    load_bg(settings->adv_bg_done_path, DEFAULT_ADV_BG_DONE_PATH, &o->adv_bg_done, &o->adv_bg_done_anim);

    if ((!o->adv_bg && !o->adv_bg_anim) ||
        (!o->adv_bg_half_done && !o->adv_bg_half_done_anim) ||
        (!o->adv_bg_done && !o->adv_bg_done_anim)) {
        log_message(LOG_ERROR, "[OVERLAY] CRITICAL: Failed to load default background textures as fallback.\n");
        overlay_free(overlay, settings);
        return false; // Critical failure if defaults also fail
    }

    // Compact mode 9-slice panel texture. Like the item backgrounds it can be a static .png or an
    // animated .gif (each frame is 9-sliced). Not critical: the draw path guards a null.
    char compact_panel_full_path[MAX_PATH_LENGTH];
    snprintf(compact_panel_full_path, sizeof(compact_panel_full_path), "%s/gui/%s", get_application_dir(),
             settings->compact_panel_path);
    if (strstr(compact_panel_full_path, ".gif")) {
        o->compact_panel_anim = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
                                                                &o->anim_cache_capacity, compact_panel_full_path,
                                                                SDL_SCALEMODE_NEAREST);
    } else {
        o->compact_panel = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                  &o->texture_cache_capacity, compact_panel_full_path,
                                                  SDL_SCALEMODE_NEAREST);
    }
    if (!o->compact_panel && !o->compact_panel_anim) {
        log_message(LOG_ERROR, "[OVERLAY] Failed to load Compact panel '%s'. Trying default...\n",
                    settings->compact_panel_path);
        snprintf(compact_panel_full_path, sizeof(compact_panel_full_path), "%s/gui/%s", get_application_dir(),
                 DEFAULT_COMPACT_PANEL_PATH);
        o->compact_panel = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                  &o->texture_cache_capacity, compact_panel_full_path,
                                                  SDL_SCALEMODE_NEAREST);
    }

    // The overlay uses one font face at two point sizes: one for the top info bar
    // and one for the row 2 & 3 text. Fonts are HiDPI aware; SDL_ttf scales them
    // correctly on any monitor at render time.
    char overlay_font_path[1024];
    snprintf(overlay_font_path, sizeof(overlay_font_path), "%s/fonts/%s", get_application_dir(),
             settings->overlay_font_name);

    if (!path_exists(overlay_font_path)) {
        log_message(
            LOG_ERROR, "[OVERLAY] Tracker/Overlay Font '%s' not found. Falling back to default Minecraft font.\n",
            settings->overlay_font_name);
        snprintf(overlay_font_path, sizeof(overlay_font_path), "%s/fonts/Minecraft.ttf", get_application_dir());
    }

    o->font = TTF_OpenFont(overlay_font_path, settings->overlay_row_font_size);
    o->font_top = TTF_OpenFont(overlay_font_path, settings->overlay_progress_font_size);
    if (!o->font || !o->font_top) {
        log_message(LOG_ERROR, "[OVERLAY] Failed to load font: %s\n", SDL_GetError());
        overlay_free(overlay, settings);
        return false;
    }

    // Compact mode fonts: three configurable faces/sizes (goal-type label, big count, pop-out
    // stack). Each falls back to the bundled Minecraft font if its chosen face is missing. A
    // failure here is non-fatal: Compact rendering falls back to the main overlay fonts, so a bad
    // filename never breaks the overlay for belt/page users.
    auto load_compact_font = [&](const char *font_name, float size) -> TTF_Font * {
        char path[1024];
        snprintf(path, sizeof(path), "%s/fonts/%s", get_application_dir(), font_name);
        if (!path_exists(path)) {
            snprintf(path, sizeof(path), "%s/fonts/Minecraft.ttf", get_application_dir());
        }
        return TTF_OpenFont(path, size);
    };
    o->compact_label_font = load_compact_font(settings->compact_label_font_name, settings->compact_label_font_size);
    o->compact_count_font = load_compact_font(settings->compact_count_font_name, settings->compact_count_font_size);
    o->compact_stack_font = load_compact_font(settings->compact_stack_font_name, settings->compact_stack_font_size);
    if (!o->compact_label_font || !o->compact_count_font || !o->compact_stack_font) {
        log_message(LOG_ERROR, "[OVERLAY] A Compact mode font failed to load; using overlay font fallback. %s\n",
                    SDL_GetError());
    }

    // Size the window to the loaded font. The window was created with a placeholder
    // height in overlay_init_sdl; resize it now that we know the font's line height.
    overlay_compute_layout(o, settings);
    int current_w;
    SDL_GetWindowSize(o->window, &current_w, nullptr);
    int init_w = current_w;
    if (settings->overlay_render_mode == OVERLAY_RENDER_MODE_COMPACT) {
        // Open at the auto-fit minimum instead of the saved belt/page width, so the Compact window
        // appears small immediately. overlay_render widens it per-template afterward if needed.
        init_w = COMPACT_MIN_WINDOW_WIDTH;
    }
    SDL_SetWindowSize(o->window, init_w, o->layout_height);

    // The Compact window is created hidden (see overlay_init_sdl) to avoid a resize flash; reveal it
    // now that it is correctly sized. Harmless no-op for the already-visible belt/page window.
    SDL_ShowWindow(o->window);

    return true;
}


void overlay_events(Overlay *o, SDL_Event *event, bool *is_running, float *deltaTime, AppSettings *settings) {
    (void) o;
    (void) settings;
    (void) deltaTime;
    switch (event->type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: *is_running = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event->key.scancode == SDL_SCANCODE_SPACE) {
                if (settings->overlay_render_mode == OVERLAY_RENDER_MODE_PAGE ||
                    settings->overlay_render_mode == OVERLAY_RENDER_MODE_COMPACT) {
                    // Page/Compact mode: SPACE cuts to the next page / cycle entry. Ignore key-repeat
                    // so a single press advances exactly one step (holding does not spam).
                    if (!event->key.repeat) {
                        o->page_index++;
                        o->page_timer = 0.0f;
                    }
                } else {
                    // Belt mode: holding SPACE speeds up the scroll.
                    *deltaTime *= OVERLAY_SPEEDUP_FACTOR;
                }
            }
            break;

        // Keep the in-memory rect current so the overlay-shutdown save persists it.
        // We intentionally do NOT write settings.json on every move/resize - doing so
        // would fire the tracker's dmon watcher and force a full reload.
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_RESIZED: {
            SDL_GetWindowPosition(o->window, &settings->overlay_window.x, &settings->overlay_window.y);
            int w, h;
            SDL_GetWindowSize(o->window, &w, &h);

            // Compact mode auto-fits its own window in overlay_render (width and height both track
            // the panel), so don't persist the transient size or force it back here - that would
            // fight the auto-fit. Only the position is worth keeping.
            if (settings->overlay_render_mode == OVERLAY_RENDER_MODE_COMPACT) {
                break;
            }

            settings->overlay_window.w = w;
            settings->overlay_window.h = o->layout_height;

            if (event->type == SDL_EVENT_WINDOW_RESIZED && h != o->layout_height) {
                SDL_SetWindowSize(o->window, w, o->layout_height);
            }
            break;
        }
        default: break;
    }
}

// Helper to check the 'done' status of any displayable item type
static bool is_display_item_done(const OverlayDisplayItem &display_item, const AppSettings *settings) {
    // --- Step 1 & 2: Incorporate the "hidden" check from the new code ---
    bool is_hidden = false;
    bool in_2nd_row = false; // Track if this item is forced to row 2
    bool in_3rd_row = false; // Track if this item is forced to row 3

    switch (display_item.type) {
        case OverlayDisplayItem::ADVANCEMENT:
        case OverlayDisplayItem::STAT: {
            auto *cat = static_cast<TrackableCategory *>(display_item.item_ptr);
            is_hidden = cat->is_hidden;
            in_2nd_row = cat->in_2nd_row;
            in_3rd_row = cat->in_3rd_row;
            break;
        }
        case OverlayDisplayItem::UNLOCK:
        case OverlayDisplayItem::CUSTOM: {
            auto *item = static_cast<TrackableItem *>(display_item.item_ptr);
            is_hidden = item->is_hidden;
            in_2nd_row = item->in_2nd_row;
            in_3rd_row = item->in_3rd_row;
            break;
        }
        case OverlayDisplayItem::MULTISTAGE: {
            auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
            is_hidden = goal->is_hidden;
            in_2nd_row = goal->in_2nd_row;
            break;
        }
        case OverlayDisplayItem::COUNTER: {
            auto *counter = static_cast<CounterGoal *>(display_item.item_ptr);
            is_hidden = counter->is_hidden;
            in_2nd_row = counter->in_2nd_row;
            break;
        }
    }

    // If the item is marked as hidden in the template, hide it from the overlay - unless the user
    // has turned on "Show Hidden Goals".
    if (goal_is_hidden(is_hidden, settings)) {
        return true;
    }

    // --- Step 3: Use the exact logic from the old code ---
    bool should_hide_when_done;

    // Determine which setting controls the hiding behavior based on the item type
    switch (display_item.type) {
        case OverlayDisplayItem::STAT:
        case OverlayDisplayItem::CUSTOM:
        case OverlayDisplayItem::MULTISTAGE:
        case OverlayDisplayItem::COUNTER:
            // If forced to Row 2, treat as "always hide" (like Advancements).
            if (in_2nd_row) {
                should_hide_when_done = true;
            } else {
                // Otherwise, respect the Row 3 setting.
                should_hide_when_done = settings->overlay_row3_remove_completed;
            }
            break;

        case OverlayDisplayItem::ADVANCEMENT:
        case OverlayDisplayItem::UNLOCK:
        default:
            // If forced to Row 3, respect the Row 3 setting.
            if (in_3rd_row) {
                should_hide_when_done = settings->overlay_row3_remove_completed;
            } else {
                // These types belong to Row 2, ALWAYS HIDE THEM
                should_hide_when_done = true;
            }
            break;
    }

    // If hiding is disabled for this item's row, it's never considered "done" for removal purposes.
    if (!should_hide_when_done) return false;

    // If hiding is enabled, check the actual completion status of the item
    switch (display_item.type) {
        case OverlayDisplayItem::ADVANCEMENT: {
            auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
            return adv->done;
        }
        case OverlayDisplayItem::UNLOCK:
            return static_cast<TrackableItem *>(display_item.item_ptr)->done;
        case OverlayDisplayItem::STAT: {
            auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
            // Hide legacy helper stats
            MC_Version version = settings_get_version_from_string(settings->version_str);
            bool is_hidden_legacy = (version <= MC_VERSION_1_6_4 && stat->is_single_stat_category && stat->criteria[0]->
                                     goal <= 0);
            return stat->done || is_hidden_legacy;
        }
        case OverlayDisplayItem::CUSTOM:
            return static_cast<TrackableItem *>(display_item.item_ptr)->done;
        case OverlayDisplayItem::MULTISTAGE: {
            auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
            return goal->current_stage >= goal->stage_count - 1;
        }
        case OverlayDisplayItem::COUNTER:
            return static_cast<CounterGoal *>(display_item.item_ptr)->done;
    }
    return true;
}

void overlay_update(Overlay *o, float *deltaTime, const Tracker *t, const AppSettings *settings) {
    if (!t || !t->template_data) return;

    // Store the current delta time so we can display it in the render function.
    o->last_delta_time = *deltaTime;

    // --- Gather Items for Each Row ---
    std::vector<std::pair<TrackableItem *, TrackableCategory *> > row1_items;
    for (int i = 0; i < t->template_data->advancement_count; i++) {
        TrackableCategory *cat = t->template_data->advancements[i];
        for (int j = 0; j < cat->criteria_count; j++) row1_items.push_back({cat->criteria[j], cat});
    }

    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *cat = t->template_data->stats[i];

        // If the stat category is a simple stat (defined without a "criteria" block in the template),
        // do not add its auto-generated criterion to Row 1.
        if (cat->is_single_stat_category) {
            continue;
        }

        // For complex stats, add all of their defined criteria to Row 1.
        for (int j = 0; j < cat->criteria_count; j++) {
            row1_items.push_back({cat->criteria[j], cat});
        }
    }

    std::vector<OverlayDisplayItem> row2_items; // Advancements & Unlocks
    for (int i = 0; i < t->template_data->advancement_count; ++i) {
        if (t->template_data->advancements[i]->in_3rd_row) continue; // SKIP if forced to Row 3 ("in_3rd_row")
        row2_items.push_back({
            t->template_data->advancements[i], OverlayDisplayItem::ADVANCEMENT
        });
    }
    for (int i = 0; i < t->template_data->unlock_count; ++i) {
        if (t->template_data->unlocks[i]->in_3rd_row) continue; // SKIP if forced to Row 3 ("in_3rd_row")
        row2_items.push_back({
            t->template_data->unlocks[i], OverlayDisplayItem::UNLOCK
        });
    }

    // Add forced items to Row 2 (custom goals, ms goals, and stats can be forced using "in_2nd_row")
    for (int i = 0; i < t->template_data->stat_count; i++) {
        TrackableCategory *cat = t->template_data->stats[i];
        if (cat->in_2nd_row) {
            // Skip hidden helper stats even if forced (shouldn't happen but safe to check)
            if (cat->is_single_stat_category && cat->criteria_count > 0 && cat->criteria[0]->goal <= 0 &&
                cat->icon_path[0] == '\0') {
                continue;
            }
            row2_items.push_back({cat, OverlayDisplayItem::STAT});
        }
    }
    for (int i = 0; i < t->template_data->custom_goal_count; i++) {
        TrackableItem *item = t->template_data->custom_goals[i];
        if (item->in_2nd_row) {
            // Add to row 2
            row2_items.push_back({item, OverlayDisplayItem::CUSTOM});
        }
    }
    for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
        MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
        if (goal->in_2nd_row) {
            // Add to row 2
            row2_items.push_back({goal, OverlayDisplayItem::MULTISTAGE});
        }
    }
    for (int i = 0; i < t->template_data->counter_goal_count; i++) {
        CounterGoal *counter = t->template_data->counter_goals[i];
        if (counter->in_2nd_row) {
            row2_items.push_back({counter, OverlayDisplayItem::COUNTER});
        }
    }

    // --- Update Animation State ---
    // Each row uses the global scroll speed unless it has a custom speed enabled.
    const float base_scroll_speed = 60.0f;
    float row1_speed = settings->overlay_row1_custom_scroll_speed_enabled
                           ? settings->overlay_row1_scroll_speed
                           : settings->overlay_scroll_speed;
    float row2_speed = settings->overlay_row2_custom_scroll_speed_enabled
                           ? settings->overlay_row2_scroll_speed
                           : settings->overlay_scroll_speed;
    float row3_speed = settings->overlay_row3_custom_scroll_speed_enabled
                           ? settings->overlay_row3_scroll_speed
                           : settings->overlay_scroll_speed;
    // Speedup from holding SPACE is handled in overlay_events directly, mutliplying by deltatime
    float row1_scroll_delta = -(base_scroll_speed * row1_speed * (*deltaTime));
    float row2_scroll_delta = -(base_scroll_speed * row2_speed * (*deltaTime));
    float row3_scroll_delta = -(base_scroll_speed * row3_speed * (*deltaTime));


    // --- Row 1 Update Logic (Dynamic Width) ---
    if (!row1_items.empty()) {
        // scroll_delta is negative by default. Adding it makes the offset
        // increasingly negative, which moves items from Right to Left.
        // Do NOT reset the offset to 0. Let it accumulate.
        // The render function handles the wrapping via fmod().
        o->scroll_offset_row1 -= row1_scroll_delta;
    } else {
        o->scroll_offset_row1 = 0;
    }

    // --- Row 2 Update Logic (Dynamic Width) ---
    // Keep accumulating while the run is complete too: row 2 then hosts the
    // supporter showcase, which scrolls at row 2's speed even with no row 2 items.
    if (!row2_items.empty() || t->template_data->run_completed) {
        // Unified Logic: Just accumulate the scroll delta.
        // We no longer need to calculate widths or swap indices here.
        // The Block-Based renderer handles the wrapping automatically.
        o->scroll_offset_row2 -= row2_scroll_delta;
    } else {
        o->scroll_offset_row2 = 0;
    }

    // Row 3 doesn't disappear by default (only with setting)
    o->scroll_offset_row3 -= row3_scroll_delta;

    // The timing logic for complex stat categories lives in overlay_render().


    // --- Cycle through social media text ---
    o->social_media_timer += *deltaTime;
    if (o->social_media_timer >= SOCIAL_CYCLE_SECONDS) {
        o->social_media_timer -= SOCIAL_CYCLE_SECONDS;
        o->current_social_index = (o->current_social_index + 1) % NUM_SOCIALS;
    }

    // --- Page/Compact mode: advance the shared page index on its own interval ---
    // SPACE advances it directly (see overlay_events), so this only handles the
    // automatic flip. deltaTime here is the real frame time (SPACE only speeds up
    // scrolling in belt mode). Page mode uses its page interval; Compact uses its cycle interval.
    if (settings->overlay_render_mode == OVERLAY_RENDER_MODE_PAGE ||
        settings->overlay_render_mode == OVERLAY_RENDER_MODE_COMPACT) {
        float iv;
        if (settings->overlay_render_mode == OVERLAY_RENDER_MODE_COMPACT)
            iv = settings->compact_cycle_interval < COMPACT_CYCLE_INTERVAL_MIN
                     ? COMPACT_CYCLE_INTERVAL_MIN
                     : settings->compact_cycle_interval;
        else
            iv = settings->overlay_page_interval < 0.1f ? 0.1f : settings->overlay_page_interval;
        o->page_timer += *deltaTime;
        while (o->page_timer >= iv) {
            o->page_timer -= iv;
            o->page_index++;
        }
    } else {
        o->page_timer = 0.0f;
    }
}

void overlay_render(Overlay *o, const Tracker *t, const AppSettings *settings) {
    SDL_SetRenderDrawColor(o->renderer, settings->overlay_bg_color.r, settings->overlay_bg_color.g,
                           settings->overlay_bg_color.b, settings->overlay_bg_color.a);
    SDL_RenderClear(o->renderer);

    // Compact render mode replaces the top info bar and the 3-row layout entirely.
    if (settings->overlay_render_mode == OVERLAY_RENDER_MODE_COMPACT) {
        overlay_render_compact(o, t, settings);
        SDL_RenderPresent(o->renderer);
        return;
    }

    // Get version
    MC_Version version = settings_get_version_from_string(settings->version_str);

    // Render Progress Text (Top Bar)
    if (t && t->template_data) {
        char info_buffer[512] = {0};
        char final_buffer[1024];
        char temp_chunk[256];
        bool first_item_added = false;

        // Build the separator string ("|" by default, configurable for fonts that
        // lack the pipe glyph) padded with surrounding spaces for readability.
        char segment_sep[16];
        const char *sep_char = (settings && settings->overlay_progress_separator[0] != '\0')
                                   ? settings->overlay_progress_separator
                                   : "|";
        snprintf(segment_sep, sizeof(segment_sep), " %s ", sep_char);

        auto add_component = [&](const char *component_str) {
            if (first_item_added) {
                strcat(info_buffer, segment_sep);
            }
            strcat(info_buffer, component_str);
            first_item_added = true;
        };

        // Use the latched completion flag synced from the tracker (honors the
        // optional per-template completion thresholds).
        bool is_run_complete = t->template_data->run_completed;

        if (is_run_complete) {
            char formatted_time[64];
            // Use frozen IGT so the final time doesn't keep ticking
            format_time(t->template_data->frozen_play_time_ticks, formatted_time, sizeof(formatted_time),
                        settings->igt_unit_spacing, settings->igt_always_show_ms);
            snprintf(info_buffer, sizeof(info_buffer),
                     "*** RUN COMPLETED! ***%sFinal Time: %s%sDonate (mentioning 'Advancely') to be featured!",
                     segment_sep, formatted_time, segment_sep);
        } else {
            // Conditionally build the progress string section by section
            if (settings->overlay_show_world && t->world_name[0] != '\0') {
                add_component(t->world_name);
            }

            if (settings->overlay_show_run_details) {
                if (settings->category_display_name[0] != '\0') {
                    snprintf(temp_chunk, sizeof(temp_chunk), "%s - %s", // Version - Category
                             settings->display_version_str,
                             settings->category_display_name);
                } else {
                    snprintf(temp_chunk, sizeof(temp_chunk), "%s", // Display Category Empty
                             settings->display_version_str);
                }
                add_component(temp_chunk);
            }

            // Show progress sections if they have something
            if (settings->overlay_show_progress) {
                bool show_adv_counter = (t->template_data->advancement_goal_count > 0);
                bool show_prog_percent = (t->template_data->total_progress_steps > 0);
                const char *adv_ach_label = (version >= MC_VERSION_1_12) ? "Adv" : "Ach";

                if (show_adv_counter && show_prog_percent) {
                    snprintf(temp_chunk, sizeof(temp_chunk), "%s: %d/%d - Prog: %.2f%%",
                             adv_ach_label, t->template_data->advancements_completed_count,
                             t->template_data->advancement_goal_count, t->template_data->overall_progress_percentage);
                    add_component(temp_chunk);
                } else if (show_adv_counter) {
                    snprintf(temp_chunk, sizeof(temp_chunk), "%s: %d/%d",
                             adv_ach_label, t->template_data->advancements_completed_count,
                             t->template_data->advancement_goal_count);
                    add_component(temp_chunk);
                } else if (show_prog_percent) {
                    snprintf(temp_chunk, sizeof(temp_chunk), "Prog: %.2f%%",
                             t->template_data->overall_progress_percentage);
                    add_component(temp_chunk);
                }
            }

            if (settings->overlay_show_igt) {
                char formatted_time[64];
                format_time(t->template_data->play_time_ticks, formatted_time, sizeof(formatted_time),
                            settings->igt_unit_spacing, settings->igt_always_show_ms);
                snprintf(temp_chunk, sizeof(temp_chunk), "%s IGT", formatted_time);
                add_component(temp_chunk);
            }

            if (settings->overlay_show_update_timer) {
                char formatted_update_time[64];
                float last_update_time_5_seconds = floorf(t->time_since_last_update / 5.0f) * 5.0f;
                format_time_since_update(last_update_time_5_seconds, formatted_update_time,
                                         sizeof(formatted_update_time), settings->igt_unit_spacing);
                // When Hermes is active the label changes to "Synced:" to reflect that
                // real-time updates are coming from Hermes and this timer only represents
                // the time since the last full game-save sync from disk.
                snprintf(temp_chunk, sizeof(temp_chunk), "%s %s",
                         settings->using_hermes ? "Synced:" : "Upd:", formatted_update_time);
                add_component(temp_chunk);
            }
        }

        // Always append the rotating social media text to the prepared message
        if (info_buffer[0] != '\0') {
            snprintf(final_buffer, sizeof(final_buffer), "%s%s%s", info_buffer, segment_sep,
                     SOCIALS[o->current_social_index]);
        } else {
            // If all sections are turned off, just show the socials
            strncpy(final_buffer, SOCIALS[o->current_social_index], sizeof(final_buffer) - 1);
            final_buffer[sizeof(final_buffer) - 1] = '\0';
        }


        SDL_Color text_color = {
            settings->overlay_text_color.r, settings->overlay_text_color.g, settings->overlay_text_color.b,
            settings->overlay_text_color.a
        };

        // Use text cache for top info bar
        SDL_Texture *text_texture = get_text_texture_from_cache(o, o->font_top, final_buffer, text_color);
        if (text_texture) {
            float w, h;
            SDL_GetTextureSize(text_texture, &w, &h);
            int overlay_w;
            SDL_GetWindowSize(o->window, &overlay_w, nullptr);
            const float padding = 10.0f;
            SDL_FRect dest_rect = {padding, padding, w, h};
            if (settings->overlay_progress_text_align == OVERLAY_PROGRESS_TEXT_ALIGN_CENTER)
                dest_rect.x = ((float) overlay_w - w) / 2.0f;
            else if (settings->overlay_progress_text_align == OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT)
                dest_rect.x = (float) overlay_w - w - padding;
            SDL_RenderTexture(o->renderer, text_texture, nullptr, &dest_rect);
        }
    }

    if (!t || !t->template_data) {
        SDL_RenderPresent(o->renderer);
        return;
    }

    int window_w;
    SDL_GetWindowSizeInPixels(o->window, &window_w, nullptr);
    SDL_Color text_color = {
        settings->overlay_text_color.r, settings->overlay_text_color.g, settings->overlay_text_color.b, 255
    };

    // --- ROW 1: Criteria & Sub-stats Icons ---
    {
        const float ROW1_Y_POS = o->layout_row1_y;
        const float ROW1_ICON_SIZE = 48.0f;
        const float ROW1_SHARED_ICON_SIZE = settings->overlay_row1_shared_icon_size; // Originally 30.0f
        const float item_full_width = snap_px(ROW1_ICON_SIZE + settings->overlay_row1_spacing);

        // Gather items
        std::vector<std::pair<TrackableItem *, TrackableCategory *> > row1_items;
        for (int i = 0; i < t->template_data->advancement_count; i++) {
            TrackableCategory *cat = t->template_data->advancements[i];
            for (int j = 0; j < cat->criteria_count; j++) {
                row1_items.push_back({cat->criteria[j], cat});
            }
        }

        for (int i = 0; i < t->template_data->stat_count; i++) {
            TrackableCategory *cat = t->template_data->stats[i];
            if (cat->is_single_stat_category) continue;
            for (int j = 0; j < cat->criteria_count; j++) {
                row1_items.push_back({cat->criteria[j], cat});
            }
        }

        // Build the removal mask (cleared items become gaps) and a signature so
        // the belt resets only when the template itself changes.
        int F = (int) row1_items.size();
        std::vector<char> removed((size_t) F);
        unsigned long long signature = 1469598103934665603ULL;
        for (int i = 0; i < F; i++) {
            TrackableItem *item = row1_items[i].first;
            TrackableCategory *parent = row1_items[i].second;
            removed[i] = (item->done || goal_is_hidden(parent->is_hidden, settings) ||
                          goal_is_hidden(item->is_hidden, settings))
                             ? 1
                             : 0;
            for (const char *s = item->root_name; *s; s++) signature = (signature ^ (unsigned char) *s) * 1099511628211ULL;
        }

        if (F > 0 && item_full_width > 0) {
            static ScrollBelt belt_row1;
            static PageView page_row1;
            std::vector<BeltTile> tiles;
            if (settings->overlay_render_mode == OVERLAY_RENDER_MODE_PAGE) {
                page_update(page_row1, o->page_index, settings->overlay_page_align,
                            settings->overlay_page_repeat, window_w, item_full_width, ROW1_ICON_SIZE,
                            F, removed, fabsf(settings->overlay_clear_animation), signature, tiles);
                belt_row1.init = false; // reset so the belt re-initialises cleanly if the mode switches back
            } else if (freeze_layout(settings->overlay_row1_freeze_enabled, settings->overlay_row1_freeze_align,
                              window_w, item_full_width, ROW1_ICON_SIZE, F, removed, tiles)) {
                belt_row1.init = false; // reset so scrolling re-initialises cleanly if it resumes
            } else {
                belt_update(belt_row1, o->scroll_offset_row1, item_full_width,
                            -item_full_width, (float) window_w + item_full_width,
                            F, removed, fabsf(settings->overlay_clear_animation),
                            effective_scroll_speed(settings->overlay_row1_custom_scroll_speed_enabled,
                                                   settings->overlay_row1_scroll_speed,
                                                   settings->overlay_scroll_speed) > 0, signature, tiles);
            }

            for (const auto &tile: tiles) {
                if (tile.idx < 0) continue; // gap
                TrackableItem *item_to_render = row1_items[tile.idx].first;
                TrackableCategory *parent = row1_items[tile.idx].second;
                float x_pos = snap_px(tile.x);

                bool clipped = belt_set_clear_clip(o->renderer, window_w, tile.clear,
                                                   ROW1_Y_POS, ROW1_Y_POS + ROW1_ICON_SIZE,
                                                   settings->overlay_clear_animation);

                // --- Render Icon ---
                SDL_FRect dest_rect = {x_pos, ROW1_Y_POS, ROW1_ICON_SIZE, ROW1_ICON_SIZE};

                SDL_Texture *tex = nullptr;
                AnimatedTexture *anim_tex = nullptr;
                if (strstr(item_to_render->icon_path, ".gif")) {
                    anim_tex = get_animated_texture_from_cache(
                        o->renderer, &o->anim_cache, &o->anim_cache_count, &o->anim_cache_capacity,
                        item_to_render->icon_path, SDL_SCALEMODE_NEAREST);
                } else {
                    tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                 &o->texture_cache_capacity, item_to_render->icon_path,
                                                 SDL_SCALEMODE_NEAREST);
                }

                if (!tex && !anim_tex) {
                    SDL_SetRenderDrawColor(o->renderer, 255, 0, 255, 100);
                    SDL_RenderFillRect(o->renderer, &dest_rect);
                } else {
                    render_texture_with_alpha(o->renderer, tex, anim_tex, &dest_rect, 255);
                }

                // --- Render Shared Parent Icon Overlay ---
                if (item_to_render->is_shared && parent) {
                    SDL_Texture *parent_tex = nullptr;
                    AnimatedTexture *parent_anim_tex = nullptr;
                    if (strstr(parent->icon_path, ".gif")) {
                        parent_anim_tex = get_animated_texture_from_cache(
                            o->renderer, &o->anim_cache, &o->anim_cache_count, &o->anim_cache_capacity,
                            parent->icon_path, SDL_SCALEMODE_NEAREST);
                    } else {
                        parent_tex = get_texture_from_cache(o->renderer, &o->texture_cache,
                                                            &o->texture_cache_count,
                                                            &o->texture_cache_capacity, parent->icon_path,
                                                            SDL_SCALEMODE_NEAREST);
                    }

                    SDL_FRect shared_dest_rect = {
                        x_pos, ROW1_Y_POS, ROW1_SHARED_ICON_SIZE, ROW1_SHARED_ICON_SIZE
                    };
                    render_texture_with_alpha(o->renderer, parent_tex, parent_anim_tex, &shared_dest_rect, 255);
                }

                if (clipped) SDL_SetRenderClipRect(o->renderer, nullptr);
            }
        }
    }

    // --- ROW 2: Advancements & Unlocks (AND forced items) ---
    // ROW 2 ALSO SHOWS SUPPORTERS WHEN RUN IS COMPLETED
    {
        const float ROW2_Y_POS = o->layout_row2_y;
        const float ITEM_WIDTH = 96.0f; // Minimum Width based on icon bg
        const float ITEM_SPACING = 16.0f;
        const float TEXT_Y_OFFSET = 4.0f;

        // Static variables to store the randomized supporter list and track completion state
        static std::vector<SupporterRenderInfo> static_supporter_render_list;
        static bool run_was_complete_last_frame = false;

        // Latched completion flag synced from the tracker (honors the optional
        // per-template completion thresholds).
        bool is_run_complete = t->template_data->run_completed;

        // If the run has just been completed, generate the randomized list ONCE.
        if (is_run_complete && !run_was_complete_last_frame) {
            static_supporter_render_list.clear(); // Clear any previous run's data

            // 1. Create a sortable list of pointers to the original supporters.
            std::vector<const Supporter *> sorted_supporters;
            for (int i = 0; i < NUM_SUPPORTERS; ++i) {
                sorted_supporters.push_back(&SUPPORTERS[i]);
            }

            // 2. Sort the pointers in descending order based on donation amount.
            std::sort(sorted_supporters.begin(), sorted_supporters.end(), [](const Supporter *a, const Supporter *b) {
                return a->amount > b->amount;
            });

            // 3. Calculate the index cutoffs for the top and middle thirds.
            const int top_third_count = (NUM_SUPPORTERS + 2) / 3; // Ensures a fair split, e.g., 5 supporters -> top 2
            const int middle_third_count = (NUM_SUPPORTERS * 2 + 2) / 3;

            // Prepare the persistent render info list
            for (int i = 0; i < NUM_SUPPORTERS; ++i) {
                SupporterRenderInfo info = {};
                info.supporter = sorted_supporters[i];
                // Assign a RANDOM icon and store it
                info.icon_path = SUPPORTER_ICONS[rand() % NUM_SUPPORTER_ICONS];

                // 4. Assign background texture based on the supporter's rank.
                if (i < top_third_count) {
                    info.background_static = o->adv_bg_done;
                    info.background_anim = o->adv_bg_done_anim;
                } else if (i < middle_third_count) {
                    info.background_static = o->adv_bg_half_done;
                    info.background_anim = o->adv_bg_half_done_anim;
                } else {
                    info.background_static = o->adv_bg;
                    info.background_anim = o->adv_bg_anim;
                }
                static_supporter_render_list.push_back(info);
            }
        }

        if (is_run_complete && NUM_SUPPORTERS > 0) {
            // Completed Run: Render Supporter Showcase
            float max_text_width = 0.0f;

            // First pass: Prepare render info and calculate max width from the static list
            for (const auto &render_info: static_supporter_render_list) {
                // Measure text widths for layout calculation
                char amount_buf[64];
                snprintf(amount_buf, sizeof(amount_buf), "$%.2f", render_info.supporter->amount);
                int name_w = 0, amount_w = 0;
                TTF_MeasureString(o->font, render_info.supporter->name, 0, 0, &name_w, nullptr);
                TTF_MeasureString(o->font, amount_buf, 0, 0, &amount_w, nullptr);
                max_text_width = fmaxf(max_text_width, (float) fmax(name_w, amount_w));
            }

            const float cell_width = snap_px(fmaxf(ITEM_WIDTH, max_text_width));
            const float item_full_width = cell_width + ITEM_SPACING;

            float total_row_width = NUM_SUPPORTERS * item_full_width;
            float start_pos = snap_px(fmod(o->scroll_offset_row2, total_row_width)); // Sync with row 2's speed
            int blocks_to_draw = (total_row_width > 0) ? (int) ceil((float) window_w / total_row_width) + 2 : 0;

            for (int block = -blocks_to_draw; block <= blocks_to_draw; ++block) {
                float block_offset = start_pos + (block * total_row_width);
                for (size_t i = 0; i < static_supporter_render_list.size(); ++i) {
                    float current_x = snap_px(block_offset + (i * item_full_width));
                    if (current_x + item_full_width < 0 || current_x > window_w) continue;

                    const auto &render_info = static_supporter_render_list[i];

                    // Render background
                    float bg_x_offset = snap_px((cell_width - ITEM_WIDTH) / 2.0f);
                    SDL_FRect bg_rect = {current_x + bg_x_offset, ROW2_Y_POS, ITEM_WIDTH, ITEM_WIDTH};
                    render_texture_with_alpha(o->renderer, render_info.background_static, render_info.background_anim,
                                              &bg_rect, 255);

                    // Render icon
                    SDL_FRect icon_rect = {bg_rect.x + 16.0f, bg_rect.y + 16.0f, 64.0f, 64.0f};

                    // Also support .gif icons
                    SDL_Texture *tex = nullptr;
                    AnimatedTexture *anim_tex = nullptr;
                    char full_icon_path[MAX_PATH_LENGTH];
                    snprintf(full_icon_path, sizeof(full_icon_path), "%s/icons/%s", get_application_dir(),
                             render_info.icon_path);

                    // Check the file extension to decide which cache function to use
                    if (strstr(full_icon_path, ".gif")) {
                        anim_tex = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
                                                                   &o->anim_cache_capacity, full_icon_path,
                                                                   SDL_SCALEMODE_NEAREST);
                    } else {
                        tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                     &o->texture_cache_capacity, full_icon_path, SDL_SCALEMODE_NEAREST);
                    }

                    if (tex || anim_tex) {
                        // Pass both pointers; the function will correctly choose which one to use
                        render_texture_with_alpha(o->renderer, tex, anim_tex, &icon_rect, 255);
                    } else {
                        // If texture loading fails for any reason, draw a placeholder
                        SDL_SetRenderDrawColor(o->renderer, 255, 0, 255, 255); // Bright Pink
                        SDL_RenderFillRect(o->renderer, &icon_rect);
                    }

                    // Render name
                    SDL_Texture *name_tex = get_text_texture_from_cache(o, o->font, render_info.supporter->name,
                                                                        text_color);
                    if (name_tex) {
                        float w, h;
                        SDL_GetTextureSize(name_tex, &w, &h);
                        float text_x = current_x + snap_px((cell_width - w) / 2.0f);
                        SDL_FRect dest_rect = {text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET, w, h};
                        SDL_RenderTexture(o->renderer, name_tex, nullptr, &dest_rect);

                        // Render amount
                        char amount_buf[64];
                        snprintf(amount_buf, sizeof(amount_buf), "$%.2f", render_info.supporter->amount);
                        SDL_Texture *amount_tex = get_text_texture_from_cache(o, o->font, amount_buf, text_color);
                        if (amount_tex) {
                            float pw, ph;
                            SDL_GetTextureSize(amount_tex, &pw, &ph);
                            float p_text_x = current_x + snap_px((cell_width - pw) / 2.0f);
                            SDL_FRect p_dest_rect = {p_text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + h, pw, ph};
                            SDL_RenderTexture(o->renderer, amount_tex, nullptr, &p_dest_rect);
                        }
                    }
                }
            }
        } else {
            // If run ISN'T complete
            // --- Default Behavior: Render Advancements & Unlocks ---
            SDL_Texture *static_bg = nullptr;
            AnimatedTexture *anim_bg = nullptr;

            std::vector<OverlayDisplayItem> row2_items;
            for (int i = 0; i < t->template_data->advancement_count; ++i) {
                if (t->template_data->advancements[i]->in_3rd_row) continue; // SKIP if forced to Row 3
                row2_items.push_back({
                    t->template_data->advancements[i], OverlayDisplayItem::ADVANCEMENT
                });
            }
            for (int i = 0; i < t->template_data->unlock_count; ++i) {
                if (t->template_data->unlocks[i]->in_3rd_row) continue; // SKIP if forced to Row 3
                row2_items.push_back({
                    t->template_data->unlocks[i], OverlayDisplayItem::UNLOCK
                });
            }

            // Add forced items to Row 2
            for (int i = 0; i < t->template_data->stat_count; i++) {
                TrackableCategory *cat = t->template_data->stats[i];
                if (cat->in_2nd_row) {
                    if (cat->is_single_stat_category && cat->criteria_count > 0 && cat->criteria[0]->goal <= 0 &&
                        cat->icon_path[0] == '\0') {
                        continue;
                    }
                    row2_items.push_back({cat, OverlayDisplayItem::STAT});
                }
            }
            for (int i = 0; i < t->template_data->custom_goal_count; i++) {
                TrackableItem *item = t->template_data->custom_goals[i];
                if (item->in_2nd_row) {
                    row2_items.push_back({item, OverlayDisplayItem::CUSTOM});
                }
            }
            for (int i = 0; i < t->template_data->multi_stage_goal_count; i++) {
                MultiStageGoal *goal = t->template_data->multi_stage_goals[i];
                if (goal->in_2nd_row) {
                    row2_items.push_back({goal, OverlayDisplayItem::MULTISTAGE});
                }
            }
            for (int i = 0; i < t->template_data->counter_goal_count; i++) {
                CounterGoal *counter = t->template_data->counter_goals[i];
                if (counter->in_2nd_row) {
                    row2_items.push_back({counter, OverlayDisplayItem::COUNTER});
                }
            }

            // --- 1. Calculate Max Text Width ---
            float max_text_width_row2 = 0.0f;
            for (const auto &display_item: row2_items) {
                // Skip template-hidden items
                bool is_template_hidden = false;
                if (display_item.type == OverlayDisplayItem::ADVANCEMENT)
                    is_template_hidden = static_cast<TrackableCategory *>(display_item.item_ptr)->is_hidden;
                else if (display_item.type == OverlayDisplayItem::UNLOCK)
                    is_template_hidden = static_cast<TrackableItem *>(display_item.item_ptr)->is_hidden;
                    // Check hidden status for forced types
                else if (display_item.type == OverlayDisplayItem::STAT)
                    is_template_hidden = static_cast<TrackableCategory *>(display_item.item_ptr)->is_hidden;
                else if (display_item.type == OverlayDisplayItem::CUSTOM)
                    is_template_hidden = static_cast<TrackableItem *>(display_item.item_ptr)->is_hidden;
                else if (display_item.type == OverlayDisplayItem::MULTISTAGE)
                    is_template_hidden = static_cast<MultiStageGoal *>(display_item.item_ptr)->is_hidden;
                else if (display_item.type == OverlayDisplayItem::COUNTER)
                    is_template_hidden = static_cast<CounterGoal *>(display_item.item_ptr)->is_hidden;

                if (goal_is_hidden(is_template_hidden, settings)) continue;

                char name_buf[256] = {0};
                char potential_progress_buf[64] = {0};
                char longest_criterion_buf[256] = {0};
                int w_name = 0, w_progress = 0, w_criterion = 0;

                if (display_item.type == OverlayDisplayItem::ADVANCEMENT) {
                    auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
                    strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);

                    if (adv->criteria_count > 0) {
                        snprintf(potential_progress_buf, sizeof(potential_progress_buf), "(%d / %d)",
                                 adv->criteria_count, adv->criteria_count);
                        TTF_MeasureString(o->font, potential_progress_buf, 0, 0, &w_progress, nullptr);

                        for (int j = 0; j < adv->criteria_count; ++j) {
                            if (adv->criteria[j] && strlen(adv->criteria[j]->display_name) > strlen(
                                    longest_criterion_buf)) {
                                strncpy(longest_criterion_buf, adv->criteria[j]->display_name,
                                        sizeof(longest_criterion_buf) - 1);
                                longest_criterion_buf[sizeof(longest_criterion_buf) - 1] = '\0';
                            }
                        }
                        if (longest_criterion_buf[0] != '\0') {
                            TTF_MeasureString(o->font, longest_criterion_buf, 0, 0, &w_criterion, nullptr);
                        }
                    }
                } else if (display_item.type == OverlayDisplayItem::UNLOCK) {
                    auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
                    strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                }

                // Handle width calculation for forced items (same logic as Row 3)
                else if (display_item.type == OverlayDisplayItem::STAT) {
                    auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
                    strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);

                    if (!stat->is_single_stat_category) {
                        // Complex stat (even if one sub-stat)
                        snprintf(name_buf, sizeof(name_buf), "%s (%d / %d)", stat->display_name,
                                 stat->completed_criteria_count, stat->criteria_count);
                        TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);

                        for (int j = 0; j < stat->criteria_count; ++j) {
                            TrackableItem *crit = stat->criteria[j];
                            char temp_sub_stat_buf[256] = {0};
                            if (crit->goal > 0) {
                                snprintf(temp_sub_stat_buf, sizeof(temp_sub_stat_buf), "%d. %s (%d / %d)", j + 1,
                                         crit->display_name, crit->goal, crit->goal);
                            } else if (crit->goal == -1) {
                                snprintf(temp_sub_stat_buf, sizeof(temp_sub_stat_buf), "%d. %s (999)", j + 1,
                                         crit->display_name);
                            }
                            if (strlen(temp_sub_stat_buf) > strlen(longest_criterion_buf)) {
                                // Reuse longest_criterion_buf
                                strcpy(longest_criterion_buf, temp_sub_stat_buf);
                            }
                        }
                    } else if (stat->criteria_count == 1) {
                        TrackableItem *crit = stat->criteria[0];
                        if (crit->goal > 0) {
                            snprintf(potential_progress_buf, sizeof(potential_progress_buf), "(%d / %d)", crit->goal,
                                     crit->goal);
                        } else if (crit->goal == -1) {
                            snprintf(potential_progress_buf, sizeof(potential_progress_buf), "(999)");
                        }
                    }
                } else if (display_item.type == OverlayDisplayItem::CUSTOM) {
                    auto *goal = static_cast<TrackableItem *>(display_item.item_ptr);
                    strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    if (goal->goal > 0) {
                        snprintf(potential_progress_buf, sizeof(potential_progress_buf), "(%d / %d)", goal->goal,
                                 goal->goal);
                    } else if (goal->goal == -1) {
                        snprintf(potential_progress_buf, sizeof(potential_progress_buf), "(999)");
                    }
                } else if (display_item.type == OverlayDisplayItem::MULTISTAGE) {
                    auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
                    strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    for (int j = 0; j < goal->stage_count; ++j) {
                        SubGoal *stage = goal->stages[j];
                        char temp_stage_buf[256];
                        if (stage->type == SUBGOAL_STAT && stage->required_progress > 0) {
                            snprintf(temp_stage_buf, sizeof(temp_stage_buf), "%s (%d/%d)", stage->display_text,
                                     stage->required_progress, stage->required_progress);
                        } else if (stage->type == SUBGOAL_STAT && stage->required_progress == -1) {
                            snprintf(temp_stage_buf, sizeof(temp_stage_buf), "%s (%d)", stage->display_text,
                                     stage->current_stat_progress);
                        } else {
                            strncpy(temp_stage_buf, stage->display_text, sizeof(temp_stage_buf) - 1);
                            temp_stage_buf[sizeof(temp_stage_buf) - 1] = '\0';
                        }
                        if (strlen(temp_stage_buf) > strlen(longest_criterion_buf)) {
                            strcpy(longest_criterion_buf, temp_stage_buf);
                        }
                    }
                } else if (display_item.type == OverlayDisplayItem::COUNTER) {
                    auto *counter = static_cast<CounterGoal *>(display_item.item_ptr);
                    strncpy(name_buf, counter->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    snprintf(potential_progress_buf, sizeof(potential_progress_buf), "(%d / %d)",
                             counter->linked_goal_count, counter->linked_goal_count);
                }

                if (potential_progress_buf[0] != '\0')
                    TTF_MeasureString(
                        o->font, potential_progress_buf, 0, 0, &w_progress, nullptr);
                if (longest_criterion_buf[0] != '\0')
                    TTF_MeasureString(o->font, longest_criterion_buf, 0, 0,
                                      &w_criterion, nullptr);

                float item_max_text_width = fmaxf((float) w_name, fmaxf((float) w_progress, (float) w_criterion));
                max_text_width_row2 = fmaxf(max_text_width_row2, item_max_text_width);
            }

            // --- 2. Apply Spacing Settings ---
            float cell_width_row2;
            float item_full_width_row2;

            if (settings->overlay_row2_custom_spacing_enabled) {
                item_full_width_row2 = snap_px(settings->overlay_row2_custom_spacing);
                cell_width_row2 = item_full_width_row2 - ITEM_SPACING;
            } else {
                cell_width_row2 = snap_px(fmaxf(ITEM_WIDTH, max_text_width_row2));
                item_full_width_row2 = cell_width_row2 + ITEM_SPACING;
            }
            o->calculated_row2_item_width = item_full_width_row2;

            int F = (int) row2_items.size();
            std::vector<char> removed((size_t) F);
            unsigned long long signature = 1469598103934665603ULL;
            for (int i = 0; i < F; i++) {
                removed[i] = is_display_item_done(row2_items[i], settings) ? 1 : 0;
                for (const char *s = overlay_item_root(row2_items[i]); *s; s++)
                    signature = (signature ^ (unsigned char) *s) * 1099511628211ULL;
            }

            if (F > 0 && item_full_width_row2 > 0) {
                // --- 3. Belt Render Loop (cleared items leave gaps that fill in) ---
                float coverage = max_text_width_row2 + 50.0f + item_full_width_row2;
                float clear_band_bottom = ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + 2.0f * (float) TTF_GetFontHeight(
                                              o->font);
                static ScrollBelt belt_row2;
                static PageView page_row2;
                std::vector<BeltTile> tiles;
                if (settings->overlay_render_mode == OVERLAY_RENDER_MODE_PAGE) {
                    page_update(page_row2, o->page_index, settings->overlay_page_align,
                                settings->overlay_page_repeat, window_w, item_full_width_row2, cell_width_row2,
                                F, removed, fabsf(settings->overlay_clear_animation), signature, tiles);
                    belt_row2.init = false; // reset so the belt re-initialises cleanly if the mode switches back
                } else if (freeze_layout(settings->overlay_row2_freeze_enabled, settings->overlay_row2_freeze_align,
                                  window_w, item_full_width_row2, cell_width_row2, F, removed, tiles)) {
                    belt_row2.init = false; // reset so scrolling re-initialises cleanly if it resumes
                } else {
                    belt_update(belt_row2, o->scroll_offset_row2, item_full_width_row2,
                                -coverage, (float) window_w + coverage,
                                F, removed, fabsf(settings->overlay_clear_animation),
                                effective_scroll_speed(settings->overlay_row2_custom_scroll_speed_enabled,
                                                       settings->overlay_row2_scroll_speed,
                                                       settings->overlay_scroll_speed) > 0, signature, tiles);
                }

                for (size_t ti = 0; ti < tiles.size(); ++ti) {
                    if (tiles[ti].idx < 0) continue; // gap left by a cleared item
                    const OverlayDisplayItem &display_item = row2_items[tiles[ti].idx];
                    {
                        float current_x = snap_px(tiles[ti].x);

                        float bg_x_offset = snap_px((cell_width_row2 - ITEM_WIDTH) / 2.0f);

                        bool clipped = belt_set_clear_clip(o->renderer, window_w, tiles[ti].clear,
                                                           ROW2_Y_POS, clear_band_bottom,
                                                           settings->overlay_clear_animation);

                        // --- Render Logic ---
                        std::string icon_path;
                        char name_buf[256] = {0};
                        char progress_buf[256] = {0};

                        switch (display_item.type) {
                            case OverlayDisplayItem::ADVANCEMENT: {
                                auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
                                static_bg = o->adv_bg;
                                anim_bg = o->adv_bg_anim;
                                if (adv->done) {
                                    static_bg = o->adv_bg_done;
                                    anim_bg = o->adv_bg_done_anim;
                                } else if (adv->completed_criteria_count > 0) {
                                    static_bg = o->adv_bg_half_done;
                                    anim_bg = o->adv_bg_half_done_anim;
                                }
                                icon_path = adv->icon_path;
                                strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                                name_buf[sizeof(name_buf) - 1] = '\0';

                                if (adv->criteria_progress_total > 0 &&
                                    adv->completed_criteria_count == adv->criteria_progress_total - 1) {
                                    for (int j = 0; j < adv->criteria_count; ++j) {
                                        if (!adv->criteria[j]->done) {
                                            strncpy(progress_buf, adv->criteria[j]->display_name,
                                                    sizeof(progress_buf) - 1);
                                            progress_buf[sizeof(progress_buf) - 1] = '\0';
                                            break;
                                        }
                                    }
                                } else if (adv->criteria_progress_total > 0) {
                                    snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                             adv->completed_criteria_count,
                                             adv->criteria_progress_total);
                                }
                                break;
                            }
                            case OverlayDisplayItem::UNLOCK: {
                                auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
                                static_bg = o->adv_bg;
                                anim_bg = o->adv_bg_anim;
                                if (unlock->done) {
                                    static_bg = o->adv_bg_done;
                                    anim_bg = o->adv_bg_done_anim;
                                }
                                icon_path = unlock->icon_path;
                                strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
                                name_buf[sizeof(name_buf) - 1] = '\0';
                                break;
                            }
                            // Render logic for forced items (same as Row 3)
                            case OverlayDisplayItem::STAT: {
                                auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
                                static_bg = o->adv_bg;
                                anim_bg = o->adv_bg_anim;
                                if (stat->done) {
                                    static_bg = o->adv_bg_done;
                                    anim_bg = o->adv_bg_done_anim;
                                } else if ((!stat->is_single_stat_category && stat->completed_criteria_count > 0) ||
                                           (stat->is_single_stat_category && stat->criteria_count > 0 && stat->criteria[
                                                0]->progress > 0)) {
                                    static_bg = o->adv_bg_half_done;
                                    anim_bg = o->adv_bg_half_done_anim;
                                }
                                icon_path = stat->icon_path;

                                if (!stat->is_single_stat_category) {
                                    // If complex stat it cycles (even if just one sub-stat)
                                    // Multi-stat / Complex Stat Logic
                                    snprintf(name_buf, sizeof(name_buf), "%s (%d / %d)", stat->display_name,
                                             stat->completed_criteria_count, stat->criteria_count);
                                    // Cycle logic for multi-stat
                                    std::vector<int> incomplete_indices;
                                    for (int j = 0; j < stat->criteria_count; ++j) {
                                        if (!stat->criteria[j]->done && !goal_is_hidden(stat->criteria[j]->is_hidden, settings)) {
                                            incomplete_indices.push_back(j);
                                        }
                                    }
                                    if (!incomplete_indices.empty()) {
                                        int cycle_duration_ms = (int) (settings->overlay_stat_cycle_speed * 1000.0f);
                                        if (cycle_duration_ms <= 0) cycle_duration_ms = 1000;
                                        Uint32 current_ticks = SDL_GetTicks();
                                        int num_incomplete = incomplete_indices.size();
                                        int list_index_to_show = (current_ticks / cycle_duration_ms) % num_incomplete;
                                        int original_crit_index = incomplete_indices[list_index_to_show];
                                        TrackableItem *crit = stat->criteria[original_crit_index];
                                        if (crit->goal > 0) {
                                            snprintf(progress_buf, sizeof(progress_buf), "%d. %s (%d / %d)",
                                                     original_crit_index + 1, crit->display_name, crit->progress,
                                                     crit->goal);
                                        } else if (crit->goal == -1) {
                                            snprintf(progress_buf, sizeof(progress_buf), "%d. %s (%d)",
                                                     original_crit_index + 1, crit->display_name, crit->progress);
                                        }
                                    }
                                } else {
                                    strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
                                    name_buf[sizeof(name_buf) - 1] = '\0';
                                    if (stat->criteria_count == 1) {
                                        TrackableItem *crit = stat->criteria[0];
                                        if (crit->goal > 0)
                                            snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                                     crit->progress, crit->goal);
                                        else if (crit->goal == -1)
                                            snprintf(
                                                progress_buf, sizeof(progress_buf), "(%d)", crit->progress);
                                    }
                                }
                                break;
                            }
                            case OverlayDisplayItem::CUSTOM: {
                                auto *goal = static_cast<TrackableItem *>(display_item.item_ptr);
                                static_bg = o->adv_bg;
                                anim_bg = o->adv_bg_anim;
                                if (goal->done) {
                                    static_bg = o->adv_bg_done;
                                    anim_bg = o->adv_bg_done_anim;
                                } else if (goal->progress > 0) {
                                    static_bg = o->adv_bg_half_done;
                                    anim_bg = o->adv_bg_half_done_anim;
                                }
                                icon_path = goal->icon_path;
                                strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                                name_buf[sizeof(name_buf) - 1] = '\0';
                                if (goal->goal > 0)
                                    snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                             goal->progress, goal->goal);
                                else if (goal->goal == -1)
                                    snprintf(
                                        progress_buf, sizeof(progress_buf), "(%d)", goal->progress);
                                break;
                            }
                            case OverlayDisplayItem::MULTISTAGE: {
                                auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
                                static_bg = o->adv_bg;
                                anim_bg = o->adv_bg_anim;
                                if (goal->current_stage >= goal->stage_count - 1) {
                                    static_bg = o->adv_bg_done;
                                    anim_bg = o->adv_bg_done_anim;
                                } else if (goal->current_stage > 0) {
                                    static_bg = o->adv_bg_half_done;
                                    anim_bg = o->adv_bg_half_done_anim;
                                }
                                icon_path = goal->icon_path;
                                strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                                name_buf[sizeof(name_buf) - 1] = '\0';

                                if (goal->current_stage < goal->stage_count) {
                                    SubGoal *active_stage = goal->stages[goal->current_stage];
                                    if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress > 0) {
                                        snprintf(progress_buf, sizeof(progress_buf), "%s (%d/%d)",
                                                 active_stage->display_text, active_stage->current_stat_progress,
                                                 active_stage->required_progress);
                                    } else if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress == -1) {
                                        snprintf(progress_buf, sizeof(progress_buf), "%s (%d)",
                                                 active_stage->display_text, active_stage->current_stat_progress);
                                    } else {
                                        snprintf(progress_buf, sizeof(progress_buf), "%s", active_stage->display_text);
                                    }
                                }
                                break;
                            }
                            case OverlayDisplayItem::COUNTER: {
                                auto *counter = static_cast<CounterGoal *>(display_item.item_ptr);
                                static_bg = o->adv_bg;
                                anim_bg = o->adv_bg_anim;
                                if (counter->done) {
                                    static_bg = o->adv_bg_done;
                                    anim_bg = o->adv_bg_done_anim;
                                } else if (counter->completed_count > 0) {
                                    static_bg = o->adv_bg_half_done;
                                    anim_bg = o->adv_bg_half_done_anim;
                                }
                                icon_path = counter->icon_path;
                                strncpy(name_buf, counter->display_name, sizeof(name_buf) - 1);
                                name_buf[sizeof(name_buf) - 1] = '\0';
                                snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                         counter->completed_count, counter->linked_goal_count);
                                break;
                            }

                            default: break;
                        }

                        SDL_FRect bg_rect = {current_x + bg_x_offset, ROW2_Y_POS, ITEM_WIDTH, ITEM_WIDTH};
                        render_texture_with_alpha(o->renderer, static_bg, anim_bg, &bg_rect, 255);

                        SDL_FRect icon_rect = {bg_rect.x + 16.0f, bg_rect.y + 16.0f, 64.0f, 64.0f};
                        SDL_Texture *tex = nullptr;
                        AnimatedTexture *anim_tex = nullptr;
                        if (!icon_path.empty() && strstr(icon_path.c_str(), ".gif")) {
                            anim_tex = get_animated_texture_from_cache(o->renderer, &o->anim_cache,
                                                                       &o->anim_cache_count,
                                                                       &o->anim_cache_capacity, icon_path.c_str(),
                                                                       SDL_SCALEMODE_NEAREST);
                        } else if (!icon_path.empty()) {
                            tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                         &o->texture_cache_capacity, icon_path.c_str(),
                                                         SDL_SCALEMODE_NEAREST);
                        }
                        render_texture_with_alpha(o->renderer, tex, anim_tex, &icon_rect, 255);

                        SDL_Texture *name_texture = get_text_texture_from_cache(o, o->font, name_buf, text_color);
                        if (name_texture) {
                            float w, h;
                            SDL_GetTextureSize(name_texture, &w, &h);
                            float text_x = current_x + snap_px((cell_width_row2 - w) / 2.0f);
                            SDL_FRect dest_rect = {text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET, w, h};
                            SDL_RenderTexture(o->renderer, name_texture, nullptr, &dest_rect);

                            if (progress_buf[0] != '\0') {
                                SDL_Texture *progress_texture =
                                        get_text_texture_from_cache(o, o->font, progress_buf, text_color);
                                if (progress_texture) {
                                    float pw, ph;
                                    SDL_GetTextureSize(progress_texture, &pw, &ph);
                                    float p_text_x = current_x + snap_px((cell_width_row2 - pw) / 2.0f);
                                    SDL_FRect p_dest_rect = {
                                        p_text_x, ROW2_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + h, pw, ph
                                    };
                                    SDL_RenderTexture(o->renderer, progress_texture, nullptr, &p_dest_rect);
                                }
                            }
                        }

                        if (clipped) SDL_SetRenderClipRect(o->renderer, nullptr);
                    }
                }
            }
        }

        // Update the state for the next frame
        run_was_complete_last_frame = is_run_complete;
    }

    // --- ROW 3: Stats & Goals ---
    // (excluding forced items with "in_2nd_row" flag)
    {
        const float ROW3_Y_POS = o->layout_row3_y; // Grows with font line height
        const float ITEM_WIDTH = 96.0f; // Minimum width based on icon bg
        const float ITEM_SPACING = 16.0f;
        const float TEXT_Y_OFFSET = 4.0f;

        // Gather items for this row
        std::vector<OverlayDisplayItem> row3_items;
        // Add advancements forced to Row 3 via "in_3rd_row"
        for (int i = 0; i < t->template_data->advancement_count; ++i) {
            if (!t->template_data->advancements[i]->in_3rd_row) continue;
            row3_items.push_back({t->template_data->advancements[i], OverlayDisplayItem::ADVANCEMENT});
        }
        // Add unlocks forced to Row 3 via "in_3rd_row"
        for (int i = 0; i < t->template_data->unlock_count; ++i) {
            if (!t->template_data->unlocks[i]->in_3rd_row) continue;
            row3_items.push_back({t->template_data->unlocks[i], OverlayDisplayItem::UNLOCK});
        }
        for (int i = 0; i < t->template_data->stat_count; ++i) {
            TrackableCategory *stat_cat = t->template_data->stats[i];
            if (stat_cat->in_2nd_row) continue; // SKIP if forced to Row 2 ("in_2nd_row")
            // Skip hidden helper stats that are not meant to be displayed
            if (stat_cat->is_single_stat_category && stat_cat->criteria_count > 0 && stat_cat->criteria[0]->goal <= 0 &&
                stat_cat->icon_path[0] == '\0') {
                continue;
            }
            row3_items.push_back({stat_cat, OverlayDisplayItem::STAT});
        }
        for (int i = 0; i < t->template_data->custom_goal_count; ++i) {
            if (t->template_data->custom_goals[i]->in_2nd_row) continue; // SKIP if forced to Row 2 ("in_2nd_row")
            row3_items.push_back({
                t->template_data->custom_goals[i], OverlayDisplayItem::CUSTOM
            });
        }
        for (int i = 0; i < t->template_data->multi_stage_goal_count; ++i) {
            if (t->template_data->multi_stage_goals[i]->in_2nd_row) continue; // SKIP if forced to Row 2 ("in_2nd_row")
            row3_items.push_back({
                t->template_data->multi_stage_goals[i], OverlayDisplayItem::MULTISTAGE
            });
        }
        for (int i = 0; i < t->template_data->counter_goal_count; ++i) {
            if (t->template_data->counter_goals[i]->in_2nd_row) continue;
            row3_items.push_back({
                t->template_data->counter_goals[i], OverlayDisplayItem::COUNTER
            });
        }

        float max_text_width_row3 = 0.0f;

        // Calculate max text width for row 3
        for (const auto &display_item: row3_items) {
            // Skip items hidden *in the template*
            bool is_template_hidden = false;
            if (display_item.type == OverlayDisplayItem::ADVANCEMENT) {
                is_template_hidden = static_cast<TrackableCategory *>(display_item.item_ptr)->is_hidden;
            } else if (display_item.type == OverlayDisplayItem::UNLOCK) {
                is_template_hidden = static_cast<TrackableItem *>(display_item.item_ptr)->is_hidden;
            } else if (display_item.type == OverlayDisplayItem::STAT) {
                TrackableCategory *stat_cat = static_cast<TrackableCategory *>(display_item.item_ptr);
                // Skip hidden legacy helper stats during width calculation
                if (version <= MC_VERSION_1_6_4 && stat_cat->is_single_stat_category && stat_cat->criteria_count > 0
                    &&
                    stat_cat->criteria[0]->goal <= 0 && stat_cat->icon_path[0] == '\0') {
                    continue;
                }
                is_template_hidden = stat_cat->is_hidden;
            } else if (display_item.type == OverlayDisplayItem::CUSTOM) {
                is_template_hidden = static_cast<TrackableItem *>(display_item.item_ptr)->is_hidden;
            } else if (display_item.type == OverlayDisplayItem::MULTISTAGE) {
                is_template_hidden = static_cast<MultiStageGoal *>(display_item.item_ptr)->is_hidden;
            } else if (display_item.type == OverlayDisplayItem::COUNTER) {
                is_template_hidden = static_cast<CounterGoal *>(display_item.item_ptr)->is_hidden;
            }
            if (goal_is_hidden(is_template_hidden, settings)) continue;

            char name_buf[256] = {0};
            char longest_progress_buf[256] = {0}; // To store the potentially longest progress/stage text
            int w_name = 0, w_progress = 0;

            switch (display_item.type) {
                case OverlayDisplayItem::ADVANCEMENT: {
                    auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
                    strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    if (adv->criteria_count > 0) {
                        snprintf(longest_progress_buf, sizeof(longest_progress_buf), "(%d / %d)",
                                 adv->criteria_count, adv->criteria_count);
                    }
                    break;
                }
                case OverlayDisplayItem::UNLOCK: {
                    auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
                    strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    break;
                }
                case OverlayDisplayItem::STAT: {
                    auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
                    strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);

                    if (!stat->is_single_stat_category) {
                        // Complex Stat
                        // Find longest sub-stat line (e.g., "1. Name (X / Y)")
                        snprintf(name_buf, sizeof(name_buf), "%s (%d / %d)", stat->display_name,
                                 stat->completed_criteria_count, stat->criteria_count);
                        TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);

                        for (int j = 0; j < stat->criteria_count; ++j) {
                            TrackableItem *crit = stat->criteria[j];
                            char temp_sub_stat_buf[256] = {0};
                            if (crit->goal > 0) {
                                snprintf(temp_sub_stat_buf, sizeof(temp_sub_stat_buf), "%d. %s (%d / %d)", j + 1,
                                         crit->display_name, crit->goal, crit->goal); // Use max progress for width
                            } else if (crit->goal == -1) {
                                snprintf(temp_sub_stat_buf, sizeof(temp_sub_stat_buf), "%d. %s (999)", j + 1,
                                         crit->display_name); // Assume 3 digits for width
                            }
                            if (strlen(temp_sub_stat_buf) > strlen(longest_progress_buf)) {
                                strcpy(longest_progress_buf, temp_sub_stat_buf);
                            }
                        }
                    } else if (stat->criteria_count == 1) {
                        // Simple stat
                        TrackableItem *crit = stat->criteria[0];
                        if (crit->goal > 0) {
                            snprintf(longest_progress_buf, sizeof(longest_progress_buf), "(%d / %d)", crit->goal,
                                     crit->goal); // Use max progress
                        } else if (crit->goal == -1) {
                            snprintf(longest_progress_buf, sizeof(longest_progress_buf), "(999)");
                            // Assume 3 digits
                        }
                    }
                    break;
                }
                case OverlayDisplayItem::CUSTOM: {
                    auto *goal = static_cast<TrackableItem *>(display_item.item_ptr);
                    strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    if (goal->goal > 0) {
                        snprintf(longest_progress_buf, sizeof(longest_progress_buf), "(%d / %d)", goal->goal,
                                 goal->goal);
                    } else if (goal->goal == -1) {
                        snprintf(longest_progress_buf, sizeof(longest_progress_buf), "(999)");
                    }
                    break;
                }
                case OverlayDisplayItem::MULTISTAGE: {
                    auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);

                    strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    // Find longest stage text
                    for (int j = 0; j < goal->stage_count; ++j) {
                        SubGoal *stage = goal->stages[j];
                        char temp_stage_buf[256];
                        if (stage->type == SUBGOAL_STAT && stage->required_progress > 0) {
                            snprintf(temp_stage_buf, sizeof(temp_stage_buf), "%s (%d/%d)", stage->display_text,
                                     stage->required_progress, stage->required_progress);
                        } else if (stage->type == SUBGOAL_STAT && stage->required_progress == -1) {
                            snprintf(temp_stage_buf, sizeof(temp_stage_buf), "%s (%d)", stage->display_text,
                                     stage->current_stat_progress);
                        } else {
                            strncpy(temp_stage_buf, stage->display_text, sizeof(temp_stage_buf) - 1);
                            temp_stage_buf[sizeof(temp_stage_buf) - 1] = '\0';
                        }
                        if (strlen(temp_stage_buf) > strlen(longest_progress_buf)) {
                            strcpy(longest_progress_buf, temp_stage_buf);
                        }
                    }
                    break;
                }
                case OverlayDisplayItem::COUNTER: {
                    auto *counter = static_cast<CounterGoal *>(display_item.item_ptr);
                    strncpy(name_buf, counter->display_name, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    TTF_MeasureString(o->font, name_buf, 0, 0, &w_name, nullptr);
                    snprintf(longest_progress_buf, sizeof(longest_progress_buf), "(%d / %d)",
                             counter->linked_goal_count, counter->linked_goal_count);
                    break;
                }
                default: break;
            }

            if (longest_progress_buf[0] != '\0') {
                TTF_MeasureString(o->font, longest_progress_buf, 0, 0, &w_progress, nullptr);
            }

            float item_max_text_width = fmaxf((float) w_name, (float) w_progress);
            max_text_width_row3 = fmaxf(max_text_width_row3, item_max_text_width);
        }

        // Apply spacing settings
        float cell_width_row3;
        float item_full_width_row3;

        if (settings->overlay_row3_custom_spacing_enabled) {
            // Use fixed width from setting
            item_full_width_row3 = snap_px(settings->overlay_row3_custom_spacing);
            cell_width_row3 = item_full_width_row3 - ITEM_SPACING;
        } else {
            cell_width_row3 = snap_px(fmaxf(ITEM_WIDTH, max_text_width_row3));
            item_full_width_row3 = cell_width_row3 + ITEM_SPACING;
        }

        o->calculated_row3_item_width = item_full_width_row3; // Store for next update cycle

        int F = (int) row3_items.size();
        std::vector<char> removed((size_t) F);
        unsigned long long signature = 1469598103934665603ULL;
        for (int i = 0; i < F; i++) {
            removed[i] = is_display_item_done(row3_items[i], settings) ? 1 : 0;
            for (const char *s = overlay_item_root(row3_items[i]); *s; s++)
                signature = (signature ^ (unsigned char) *s) * 1099511628211ULL;
        }

        if (F > 0 && item_full_width_row3 > 0) {
            // Belt render pass (cleared items leave gaps that quietly fill in)
            float coverage = max_text_width_row3 + 50.0f + item_full_width_row3;
            float clear_band_bottom = ROW3_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + 2.0f * (float) TTF_GetFontHeight(
                                          o->font);
            static ScrollBelt belt_row3;
            static PageView page_row3;
            std::vector<BeltTile> tiles;
            if (settings->overlay_render_mode == OVERLAY_RENDER_MODE_PAGE) {
                page_update(page_row3, o->page_index, settings->overlay_page_align,
                            settings->overlay_page_repeat, window_w, item_full_width_row3, cell_width_row3,
                            F, removed, fabsf(settings->overlay_clear_animation), signature, tiles);
                belt_row3.init = false; // reset so the belt re-initialises cleanly if the mode switches back
            } else if (freeze_layout(settings->overlay_row3_freeze_enabled, settings->overlay_row3_freeze_align,
                              window_w, item_full_width_row3, cell_width_row3, F, removed, tiles)) {
                belt_row3.init = false; // reset so scrolling re-initialises cleanly if it resumes
            } else {
                belt_update(belt_row3, o->scroll_offset_row3, item_full_width_row3,
                            -coverage, (float) window_w + coverage,
                            F, removed, fabsf(settings->overlay_clear_animation),
                            effective_scroll_speed(settings->overlay_row3_custom_scroll_speed_enabled,
                                                   settings->overlay_row3_scroll_speed,
                                                   settings->overlay_scroll_speed) > 0, signature, tiles);
            }

            for (size_t ti = 0; ti < tiles.size(); ++ti) {
                if (tiles[ti].idx < 0) continue; // gap left by a cleared item
                const OverlayDisplayItem &display_item = row3_items[tiles[ti].idx];
                {
                    float current_x = snap_px(tiles[ti].x);

                    float bg_x_offset = snap_px((cell_width_row3 - ITEM_WIDTH) / 2.0f);

                    bool clipped = belt_set_clear_clip(o->renderer, window_w, tiles[ti].clear,
                                                       ROW3_Y_POS, clear_band_bottom,
                                                       settings->overlay_clear_animation);

                    // --- Render the item ---
                    SDL_Texture *static_bg = nullptr;
                    AnimatedTexture *anim_bg = nullptr;

                    std::string icon_path;
                    char name_buf[256] = {0}; // Renamed
                    char progress_buf[256] = {0}; // Renamed and increased size

                    switch (display_item.type) {
                        case OverlayDisplayItem::ADVANCEMENT: {
                            auto *adv = static_cast<TrackableCategory *>(display_item.item_ptr);
                            static_bg = o->adv_bg;
                            anim_bg = o->adv_bg_anim;
                            if (adv->done) {
                                static_bg = o->adv_bg_done;
                                anim_bg = o->adv_bg_done_anim;
                            } else if (adv->completed_criteria_count > 0) {
                                static_bg = o->adv_bg_half_done;
                                anim_bg = o->adv_bg_half_done_anim;
                            }
                            icon_path = adv->icon_path;
                            strncpy(name_buf, adv->display_name, sizeof(name_buf) - 1);
                            name_buf[sizeof(name_buf) - 1] = '\0';

                            if (adv->criteria_progress_total > 0 &&
                                adv->completed_criteria_count == adv->criteria_progress_total - 1) {
                                for (int j = 0; j < adv->criteria_count; ++j) {
                                    if (!adv->criteria[j]->done) {
                                        strncpy(progress_buf, adv->criteria[j]->display_name,
                                                sizeof(progress_buf) - 1);
                                        progress_buf[sizeof(progress_buf) - 1] = '\0';
                                        break;
                                    }
                                }
                            } else if (adv->criteria_progress_total > 0) {
                                snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                         adv->completed_criteria_count, adv->criteria_progress_total);
                            }
                            break;
                        }
                        case OverlayDisplayItem::UNLOCK: {
                            auto *unlock = static_cast<TrackableItem *>(display_item.item_ptr);
                            static_bg = o->adv_bg;
                            anim_bg = o->adv_bg_anim;
                            if (unlock->done) {
                                static_bg = o->adv_bg_done;
                                anim_bg = o->adv_bg_done_anim;
                            }
                            icon_path = unlock->icon_path;
                            strncpy(name_buf, unlock->display_name, sizeof(name_buf) - 1);
                            name_buf[sizeof(name_buf) - 1] = '\0';
                            break;
                        }
                        case OverlayDisplayItem::STAT: {
                            auto *stat = static_cast<TrackableCategory *>(display_item.item_ptr);
                            static_bg = o->adv_bg;
                            anim_bg = o->adv_bg_anim;
                            if (stat->done) {
                                static_bg = o->adv_bg_done;
                                anim_bg = o->adv_bg_done_anim;
                            } else if ((!stat->is_single_stat_category && stat->completed_criteria_count > 0) ||
                                       (stat->is_single_stat_category && stat->criteria_count > 0 && stat->criteria[0]->
                                        progress > 0)) {
                                static_bg = o->adv_bg_half_done;
                                anim_bg = o->adv_bg_half_done_anim;
                            }
                            icon_path = stat->icon_path;

                            if (!stat->is_single_stat_category) {
                                // If it's a complex stat (even if just one sub-stat)
                                // Multi-stat / Complex stat logic
                                snprintf(name_buf, sizeof(name_buf), "%s (%d / %d)", stat->display_name,
                                         stat->completed_criteria_count, stat->criteria_count);

                                std::vector<int> incomplete_indices;
                                for (int j = 0; j < stat->criteria_count; ++j) {
                                    if (!stat->criteria[j]->done && !goal_is_hidden(stat->criteria[j]->is_hidden, settings)) {
                                        incomplete_indices.push_back(j);
                                    }
                                }

                                if (!incomplete_indices.empty()) {
                                    int cycle_duration_ms = (int) (settings->overlay_stat_cycle_speed * 1000.0f);
                                    if (cycle_duration_ms <= 0) cycle_duration_ms = 1000;

                                    Uint32 current_ticks = SDL_GetTicks();
                                    int num_incomplete = incomplete_indices.size();
                                    int list_index_to_show = (current_ticks / cycle_duration_ms) % num_incomplete;
                                    int original_crit_index = incomplete_indices[list_index_to_show];
                                    TrackableItem *crit = stat->criteria[original_crit_index];

                                    if (crit->goal > 0) {
                                        snprintf(progress_buf, sizeof(progress_buf), "%d. %s (%d / %d)",
                                                 original_crit_index + 1, crit->display_name, crit->progress,
                                                 crit->goal);
                                    } else if (crit->goal == -1) {
                                        snprintf(progress_buf, sizeof(progress_buf), "%d. %s (%d)",
                                                 original_crit_index + 1, crit->display_name, crit->progress);
                                    }
                                } else {
                                    progress_buf[0] = '\0';
                                }
                            } else {
                                // Simple stat
                                strncpy(name_buf, stat->display_name, sizeof(name_buf) - 1);
                                name_buf[sizeof(name_buf) - 1] = '\0';
                                if (stat->criteria_count == 1) {
                                    TrackableItem *crit = stat->criteria[0];
                                    if (crit->goal > 0) {
                                        snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                                 crit->progress, crit->goal);
                                    } else if (crit->goal == -1) {
                                        snprintf(progress_buf, sizeof(progress_buf), "(%d)", crit->progress);
                                    }
                                }
                            }
                            break;
                        }
                        case OverlayDisplayItem::CUSTOM: {
                            auto *goal = static_cast<TrackableItem *>(display_item.item_ptr);
                            static_bg = o->adv_bg;
                            anim_bg = o->adv_bg_anim;
                            if (goal->done) {
                                static_bg = o->adv_bg_done;
                                anim_bg = o->adv_bg_done_anim;
                            } else if (goal->progress > 0) {
                                static_bg = o->adv_bg_half_done;
                                anim_bg = o->adv_bg_half_done_anim;
                            }
                            icon_path = goal->icon_path;
                            strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                            name_buf[sizeof(name_buf) - 1] = '\0';
                            if (goal->goal > 0) {
                                snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)", goal->progress, goal->goal);
                            } else if (goal->goal == -1) {
                                snprintf(progress_buf, sizeof(progress_buf), "(%d)", goal->progress);
                            }
                            break;
                        }
                        case OverlayDisplayItem::MULTISTAGE: {
                            auto *goal = static_cast<MultiStageGoal *>(display_item.item_ptr);
                            static_bg = o->adv_bg;
                            anim_bg = o->adv_bg_anim;
                            if (goal->current_stage >= goal->stage_count - 1) {
                                static_bg = o->adv_bg_done;
                                anim_bg = o->adv_bg_done_anim;
                            } else if (goal->current_stage > 0) {
                                static_bg = o->adv_bg_half_done;
                                anim_bg = o->adv_bg_half_done_anim;
                            }

                            // Icons per stage logic
                            if (goal->use_stage_icons && goal->stage_count > 0) {
                                int idx = goal->current_stage;
                                if (idx >= goal->stage_count) idx = goal->stage_count - 1;
                                icon_path = goal->stages[idx]->icon_path;
                            } else {
                                icon_path = goal->icon_path;
                            }

                            strncpy(name_buf, goal->display_name, sizeof(name_buf) - 1);
                            name_buf[sizeof(name_buf) - 1] = '\0';
                            if (goal->current_stage < goal->stage_count) {
                                SubGoal *active_stage = goal->stages[goal->current_stage];
                                if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress > 0) {
                                    snprintf(progress_buf, sizeof(progress_buf), "%s (%d/%d)",
                                             active_stage->display_text, active_stage->current_stat_progress,
                                             active_stage->required_progress);
                                } else if (active_stage->type == SUBGOAL_STAT && active_stage->required_progress == -1) {
                                    snprintf(progress_buf, sizeof(progress_buf), "%s (%d)",
                                             active_stage->display_text, active_stage->current_stat_progress);
                                } else {
                                    snprintf(progress_buf, sizeof(progress_buf), "%s", active_stage->display_text);
                                }
                            }
                            break;
                        }
                        case OverlayDisplayItem::COUNTER: {
                            auto *counter = static_cast<CounterGoal *>(display_item.item_ptr);
                            static_bg = o->adv_bg;
                            anim_bg = o->adv_bg_anim;
                            if (counter->done) {
                                static_bg = o->adv_bg_done;
                                anim_bg = o->adv_bg_done_anim;
                            } else if (counter->completed_count > 0) {
                                static_bg = o->adv_bg_half_done;
                                anim_bg = o->adv_bg_half_done_anim;
                            }
                            icon_path = counter->icon_path;
                            strncpy(name_buf, counter->display_name, sizeof(name_buf) - 1);
                            name_buf[sizeof(name_buf) - 1] = '\0';
                            snprintf(progress_buf, sizeof(progress_buf), "(%d / %d)",
                                     counter->completed_count, counter->linked_goal_count);
                            break;
                        }
                        default: break;
                    }

                    // --- Make sure text positioning uses cell_width_row3 for centering ---
                    SDL_FRect bg_rect = {current_x + bg_x_offset, ROW3_Y_POS, ITEM_WIDTH, ITEM_WIDTH};
                    render_texture_with_alpha(o->renderer, static_bg, anim_bg, &bg_rect, 255);

                    SDL_FRect icon_rect = {bg_rect.x + 16.0f, bg_rect.y + 16.0f, 64.0f, 64.0f};
                    SDL_Texture *tex = nullptr;
                    AnimatedTexture *anim_tex = nullptr;
                    if (!icon_path.empty() && strstr(icon_path.c_str(), ".gif")) {
                        anim_tex = get_animated_texture_from_cache(o->renderer, &o->anim_cache, &o->anim_cache_count,
                                                                   &o->anim_cache_capacity, icon_path.c_str(),
                                                                   SDL_SCALEMODE_NEAREST);
                    } else if (!icon_path.empty()) {
                        tex = get_texture_from_cache(o->renderer, &o->texture_cache, &o->texture_cache_count,
                                                     &o->texture_cache_capacity, icon_path.c_str(),
                                                     SDL_SCALEMODE_NEAREST);
                    }
                    render_texture_with_alpha(o->renderer, tex, anim_tex, &icon_rect, 255);


                    // Text rendering uses cell_width_row3 for centering
                    SDL_Texture *name_texture = get_text_texture_from_cache(o, o->font, name_buf, text_color);
                    // Use name_buf calculated earlier
                    if (name_texture) {
                        float w, h;
                        SDL_GetTextureSize(name_texture, &w, &h);
                        float text_x = current_x + snap_px((cell_width_row3 - w) / 2.0f); // Center using cell_width_row3
                        SDL_FRect dest_rect = {text_x, ROW3_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET, w, h};
                        SDL_RenderTexture(o->renderer, name_texture, nullptr, &dest_rect);

                        if (progress_buf[0] != '\0') {
                            // Use progress_buf which holds current text
                            SDL_Texture *progress_texture = get_text_texture_from_cache(o, o->font, progress_buf, text_color);
                            if (progress_texture) {
                                float pw, ph;
                                SDL_GetTextureSize(progress_texture, &pw, &ph);
                                float p_text_x = current_x + snap_px((cell_width_row3 - pw) / 2.0f);
                                // Center using cell_width_row3
                                SDL_FRect p_dest_rect = {p_text_x, ROW3_Y_POS + ITEM_WIDTH + TEXT_Y_OFFSET + h, pw, ph};
                                SDL_RenderTexture(o->renderer, progress_texture, nullptr, &p_dest_rect);
                            }
                        }
                    }

                    if (clipped) SDL_SetRenderClipRect(o->renderer, nullptr);
                } // End tile scope
            } // End belt loop
        } // End if F > 0
    }

    // --- DEBUG: Performance Display ---
    if (settings->print_debug_status) {
        // Static variables to track frame rate
        static Uint32 frame_count = 0;
        static Uint32 last_fps_update_time = 0;
        static float current_fps = 0.0f;

        frame_count++;
        Uint32 current_ticks = SDL_GetTicks();

        // Calculate FPS once every second
        if ((current_ticks - last_fps_update_time) >= 1000) {
            current_fps = (float) frame_count;
            frame_count = 0;
            last_fps_update_time = current_ticks;
        }

        // Format the debug string
        char debug_buffer[128];
        snprintf(debug_buffer, sizeof(debug_buffer), "FPS: %.1f | dT: %.1f ms",
                 current_fps, o->last_delta_time * 1000.0f);

        // Render the text using the cache
        SDL_Color text_color = {255, 0, 255, 255}; // Purple for visibility
        SDL_Texture *text_texture = get_text_texture_from_cache(o, o->font_top, debug_buffer, text_color);
        if (text_texture) {
            float w, h;
            SDL_GetTextureSize(text_texture, &w, &h);
            SDL_FRect dest_rect = {5.0f, 5.0f, w, h};
            SDL_RenderTexture(o->renderer, text_texture, nullptr, &dest_rect);
        }
    }

    // END OF DEBUG -------------------------------------------------

    SDL_RenderPresent(o->renderer);
}


void overlay_free(Overlay **overlay, const AppSettings *settings) {
    (void) settings;
    if (overlay && *overlay) {
        Overlay *o = *overlay;

        // Free the caches
        if (o->texture_cache) {
            for (int i = 0; i < o->texture_cache_count; i++) {
                if (o->texture_cache[i].texture) {
                    SDL_DestroyTexture(o->texture_cache[i].texture);
                }
            }
            free(o->texture_cache);
            o->texture_cache = nullptr;
        }
        if (o->anim_cache) {
            for (int i = 0; i < o->anim_cache_count; i++) {
                if (o->anim_cache[i].anim) {
                    free_animated_texture(o->anim_cache[i].anim);
                }
            }
            free(o->anim_cache);
            o->anim_cache = nullptr;
        }

        // Free the new text cache
        if (o->text_cache) {
            for (int i = 0; i < o->text_cache_count; i++) {
                if (o->text_cache[i].texture) {
                    SDL_DestroyTexture(o->text_cache[i].texture);
                }
            }
            free(o->text_cache);
            o->text_cache = nullptr;
        }

        if (o->text_engine) {
            TTF_DestroyRendererTextEngine(o->text_engine);
            o->text_engine = nullptr;
        }

        if (o->font) {
            TTF_CloseFont(o->font);
            o->font = nullptr;
        }

        if (o->font_top) {
            TTF_CloseFont(o->font_top);
            o->font_top = nullptr;
        }

        if (o->compact_label_font) {
            TTF_CloseFont(o->compact_label_font);
            o->compact_label_font = nullptr;
        }

        if (o->compact_count_font) {
            TTF_CloseFont(o->compact_count_font);
            o->compact_count_font = nullptr;
        }

        if (o->compact_stack_font) {
            TTF_CloseFont(o->compact_stack_font);
            o->compact_stack_font = nullptr;
        }

        if (o->renderer) {
            SDL_DestroyRenderer(o->renderer);

            // We still have an address
            o->renderer = nullptr;
        }

        if (o->window) {
            SDL_DestroyWindow(o->window);

            // We still have an address
            o->window = nullptr;
        }

        // SDL_Quit(); // This is ONCE for all windows in the main loop

        // tracker is heap allocated so free it
        free(o);
        o = nullptr;
        *overlay = nullptr;

        log_message(LOG_INFO, "[OVERLAY] Overlay freed!\n");
    }
}
