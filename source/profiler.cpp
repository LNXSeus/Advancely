// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 19.08.2026.
//

#include "profiler.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

bool g_profiler_enabled = false;

#define PROFILER_MAX_ZONES 96
#define PROFILER_MAX_COUNTERS 64

typedef struct {
    const char *name;
    Uint64 start;
    Uint64 accum; // Ticks spent in this zone during the current interval
    Uint64 top_level_accum; // Subset of accum that was not nested inside another zone
    Uint64 max_call; // Worst single call during the current interval
    int calls;
    int depth;
    bool started_at_top_level;
} ProfilerZone;

typedef struct {
    const char *name;
    int count;
} ProfilerCounter;

static ProfilerZone g_zones[PROFILER_MAX_ZONES];
static int g_zone_count = 0;

static ProfilerCounter g_counters[PROFILER_MAX_COUNTERS];
static int g_counter_count = 0;

static int g_active_depth = 0;

static Uint64 g_frame_start = 0;
static Uint64 g_interval_start = 0;
static Uint64 g_work_accum = 0; // Frame work this interval, excluding the frame limiter sleep
static Uint64 g_work_max = 0;
static int g_frame_count = 0;
static int g_slow_frames = 0; // Frames whose work exceeded 16.7 ms
static int g_log_calls = 0;

static double g_report_interval = 5.0;
static FILE *g_profile_file = nullptr;

// Counters are the only profiler state touched from more than one thread
// (the dmon watcher threads call profiler_count), so they get a lock.
static SDL_Mutex *g_counter_mutex = nullptr;

// Zones measure the frame loop, and their bookkeeping is deliberately lock-free. Some zoned
// functions also run on worker threads (get_saves_path from the instance poller), so recording is
// limited to the thread that owns the frame loop instead of paying for a lock on every begin/end.
static SDL_ThreadID g_main_thread_id = 0;

static double ticks_to_ms(Uint64 ticks) {
    const Uint64 freq = SDL_GetPerformanceFrequency();
    if (freq == 0) return 0.0;
    return (double) ticks * 1000.0 / (double) freq;
}

// Counter names built from __FILE__ carry the full build path; only the basename is useful.
static const char *counter_display_name(const char *name) {
    const char *best = name;
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\') best = p + 1;
    }
    return best;
}

static void profiler_write(const char *format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    printf("%s", message);
    if (g_profile_file) {
        fputs(message, g_profile_file);
        fflush(g_profile_file);
    }
}

void profiler_init(bool enabled, float report_interval_seconds) {
    g_profiler_enabled = enabled;
    g_report_interval = report_interval_seconds > 0.0f ? (double) report_interval_seconds : 5.0;
    g_main_thread_id = SDL_GetCurrentThreadID();
    if (!enabled) return;

    if (!g_counter_mutex) g_counter_mutex = SDL_CreateMutex();

    g_profile_file = fopen("advancely_profile_log.txt", "w");

    const time_t now = time(nullptr);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    profiler_write("Advancely Frame Profile - %s\n", time_buf);
    profiler_write("ms/s = milliseconds spent in that zone per second of runtime.\n");
    profiler_write("========================================\n\n");

    g_interval_start = SDL_GetPerformanceCounter();
}

void profiler_shutdown(void) {
    if (g_counter_mutex) {
        SDL_DestroyMutex(g_counter_mutex);
        g_counter_mutex = nullptr;
    }
    if (g_profile_file) {
        fputs("\n========================================\nProfile finished.\n", g_profile_file);
        fclose(g_profile_file);
        g_profile_file = nullptr;
    }
    g_profiler_enabled = false;
}

int profiler_zone(const char *name) {
    if (!name) return -1;
    // Registration mutates the zone table, so it stays on the frame-loop thread as well.
    if (SDL_GetCurrentThreadID() != g_main_thread_id) return -1;
    for (int i = 0; i < g_zone_count; i++) {
        if (strcmp(g_zones[i].name, name) == 0) return i;
    }
    if (g_zone_count >= PROFILER_MAX_ZONES) return -1;

    const int index = g_zone_count++;
    memset(&g_zones[index], 0, sizeof(ProfilerZone));
    g_zones[index].name = name;
    return index;
}

void profiler_begin(int zone) {
    if (!g_profiler_enabled || zone < 0 || zone >= g_zone_count) return;
    if (SDL_GetCurrentThreadID() != g_main_thread_id) return;

    ProfilerZone *z = &g_zones[zone];
    if (z->depth++ == 0) {
        z->start = SDL_GetPerformanceCounter();
        z->started_at_top_level = (g_active_depth == 0);
    }
    g_active_depth++;
}

void profiler_end(int zone) {
    if (!g_profiler_enabled || zone < 0 || zone >= g_zone_count) return;
    if (SDL_GetCurrentThreadID() != g_main_thread_id) return;

    ProfilerZone *z = &g_zones[zone];
    if (z->depth == 0) return;

    if (g_active_depth > 0) g_active_depth--;
    if (--z->depth != 0) return;

    const Uint64 elapsed = SDL_GetPerformanceCounter() - z->start;
    z->accum += elapsed;
    if (z->started_at_top_level) z->top_level_accum += elapsed;
    if (elapsed > z->max_call) z->max_call = elapsed;
    z->calls++;
}

void profiler_note_log(void) {
    if (!g_profiler_enabled) return;
    g_log_calls++;
}

void profiler_count(const char *name) {
    if (!g_profiler_enabled || !name) return;

    if (g_counter_mutex) SDL_LockMutex(g_counter_mutex);
    bool found = false;
    for (int i = 0; i < g_counter_count; i++) {
        if (strcmp(g_counters[i].name, name) == 0) {
            g_counters[i].count++;
            found = true;
            break;
        }
    }
    if (!found && g_counter_count < PROFILER_MAX_COUNTERS) {
        g_counters[g_counter_count].name = name;
        g_counters[g_counter_count].count = 1;
        g_counter_count++;
    }
    if (g_counter_mutex) SDL_UnlockMutex(g_counter_mutex);
}

#define PROFILER_MAX_NOTES_PER_INTERVAL 4
static int g_notes_this_interval = 0;

void profiler_note(const char *format, ...) {
    if (!g_profiler_enabled) return;
    if (g_notes_this_interval >= PROFILER_MAX_NOTES_PER_INTERVAL) return;
    g_notes_this_interval++;

    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    profiler_write("[PROFILE]   note: %s\n", message);
}

void profiler_frame_begin(void) {
    if (!g_profiler_enabled) return;
    g_frame_start = SDL_GetPerformanceCounter();
    g_active_depth = 0;
    // A break or continue can skip a profiler_end(), so never carry depth across frames.
    for (int i = 0; i < g_zone_count; i++) g_zones[i].depth = 0;
}

static void profiler_report(double interval_seconds) {
    const double frames_per_second = interval_seconds > 0.0 ? (double) g_frame_count / interval_seconds : 0.0;
    const double avg_work_ms = g_frame_count > 0 ? ticks_to_ms(g_work_accum) / g_frame_count : 0.0;

    profiler_write("[PROFILE] %.1f fps | work avg %.2f ms | work max %.2f ms | slow frames %d/%d | log calls %d\n",
                   frames_per_second, avg_work_ms, ticks_to_ms(g_work_max), g_slow_frames, g_frame_count,
                   g_log_calls);

    int order[PROFILER_MAX_ZONES];
    int shown = 0;
    for (int i = 0; i < g_zone_count; i++) {
        if (g_zones[i].calls > 0) order[shown++] = i;
    }
    for (int i = 1; i < shown; i++) {
        const int key = order[i];
        int j = i - 1;
        while (j >= 0 && g_zones[order[j]].accum < g_zones[key].accum) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    Uint64 top_level_total = 0;
    for (int i = 0; i < g_zone_count; i++) top_level_total += g_zones[i].top_level_accum;

    for (int i = 0; i < shown; i++) {
        const ProfilerZone *z = &g_zones[order[i]];
        const double total_ms = ticks_to_ms(z->accum);
        profiler_write("[PROFILE]   %-28s %8.2f ms/s  %8.3f ms/call  max %7.2f ms  %7.1f calls/s\n",
                       z->name, total_ms / interval_seconds, z->calls > 0 ? total_ms / z->calls : 0.0,
                       ticks_to_ms(z->max_call), (double) z->calls / interval_seconds);
    }

    const double work_ms = ticks_to_ms(g_work_accum);
    const double measured_ms = ticks_to_ms(top_level_total);
    profiler_write("[PROFILE]   %-28s %8.2f ms/s (frame work not covered by any zone)\n",
                   "<unaccounted>", (work_ms - measured_ms) / interval_seconds);

    if (g_counter_mutex) SDL_LockMutex(g_counter_mutex);
    for (int i = 0; i < g_counter_count; i++) {
        profiler_write("[PROFILE]   count %-34s %8.1f /s\n",
                       counter_display_name(g_counters[i].name),
                       (double) g_counters[i].count / interval_seconds);
        g_counters[i].count = 0;
    }
    if (g_counter_mutex) SDL_UnlockMutex(g_counter_mutex);
    profiler_write("\n");

    for (int i = 0; i < g_zone_count; i++) {
        g_zones[i].accum = 0;
        g_zones[i].top_level_accum = 0;
        g_zones[i].max_call = 0;
        g_zones[i].calls = 0;
    }
    g_work_accum = 0;
    g_work_max = 0;
    g_frame_count = 0;
    g_slow_frames = 0;
    g_log_calls = 0;
    g_notes_this_interval = 0;
}

void profiler_frame_end(void) {
    if (!g_profiler_enabled) return;

    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 work = now - g_frame_start;
    g_work_accum += work;
    if (work > g_work_max) g_work_max = work;
    if (ticks_to_ms(work) > 16.7) g_slow_frames++;
    g_frame_count++;

    const double interval = ticks_to_ms(now - g_interval_start) / 1000.0;
    if (interval >= g_report_interval) {
        profiler_report(interval);
        g_interval_start = now;
    }
}
