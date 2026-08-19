// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 19.08.2026.
//

#ifndef PROFILER_H
#define PROFILER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Enabled with the --profiler command line flag. Every profiler call is a single
// bool check when this is false, so the instrumentation can stay in the hot loop.
extern bool g_profiler_enabled;

/**
 * @brief Turns profiling on and opens advancely_profile_log.txt.
 * @param enabled Whether frame profiling should run.
 * @param report_interval_seconds How often a summary is written (0 uses 5 seconds).
 */
void profiler_init(bool enabled, float report_interval_seconds);

/**
 * @brief Writes a final summary and closes the profile log.
 */
void profiler_shutdown(void);

/**
 * @brief Registers (or looks up) a named zone.
 * @param name Static string, used as the zone label.
 * @return A zone handle to pass to profiler_begin/profiler_end, or -1 when full.
 */
int profiler_zone(const char *name);

/**
 * @brief Starts timing a zone. Re-entrant: only the outermost pair is timed.
 */
void profiler_begin(int zone);

/**
 * @brief Stops timing a zone and folds the elapsed time into the current interval.
 */
void profiler_end(int zone);

/**
 * @brief Marks the start of a frame's measured work.
 */
void profiler_frame_begin(void);

/**
 * @brief Marks the end of a frame's measured work (call before any frame-limiter sleep)
 * and emits the interval summary once the report interval has elapsed.
 */
void profiler_frame_end(void);

/**
 * @brief Counts one log_message() call so the summary can expose logging spam.
 */
void profiler_note_log(void);

/**
 * @brief Counts one occurrence of a named event (no timing), for spotting work that
 * runs far more often than expected.
 */
void profiler_count(const char *name);

/**
 * @brief Writes a one-off line into the profile log, for inspecting values behind a
 * counter. Rate limited per report interval so a per-frame call cannot flood the file.
 */
void profiler_note(const char *format, ...);

#ifdef __cplusplus
}

// RAII helper for C++ call sites where a scope maps cleanly onto a zone.
struct ProfilerScope {
    int zone;

    explicit ProfilerScope(const char *name) : zone(g_profiler_enabled ? profiler_zone(name) : -1) {
        if (zone >= 0) profiler_begin(zone);
    }

    ~ProfilerScope() {
        if (zone >= 0) profiler_end(zone);
    }

    ProfilerScope(const ProfilerScope &) = delete;

    ProfilerScope &operator=(const ProfilerScope &) = delete;
};

#define PROFILE_SCOPE_CONCAT_INNER(a, b) a##b
#define PROFILE_SCOPE_CONCAT(a, b) PROFILE_SCOPE_CONCAT_INNER(a, b)
#define PROFILE_SCOPE(name) ProfilerScope PROFILE_SCOPE_CONCAT(prof_scope_, __LINE__)(name)

// For sections that are not a scope, such as consecutive blocks in the main loop.
// PROFILE_BEGIN and its matching PROFILE_END must sit in the same C++ scope.
#define PROFILE_BEGIN(id, name) \
    static const int prof_zone_##id = profiler_zone(name); \
    profiler_begin(prof_zone_##id)
#define PROFILE_END(id) profiler_end(prof_zone_##id)

#endif

#endif //PROFILER_H
