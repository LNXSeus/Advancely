// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 19.08.2026.
//

// Background poller for PATH_MODE_INSTANCE. Detecting the active Minecraft instance means walking
// the whole process table and reading each Java process's command line out of its memory, which
// measured 12-22 ms per call. Run inline in the frame loop that dropped a frame every poll, so it
// lives on its own thread and the main loop only picks up the finished result.

#ifndef INSTANCE_POLLER_H
#define INSTANCE_POLLER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts the poller thread. Safe to call more than once; later calls do nothing.
 */
void instance_poller_start(void);

/**
 * @brief Signals the poller thread to exit and waits for it. Safe to call when not started.
 */
void instance_poller_stop(void);

/**
 * @brief Enables or disables polling, mirroring whether the active path mode is PATH_MODE_INSTANCE.
 * While disabled the thread idles instead of scanning.
 * @param enabled Whether the instance scan should run.
 */
void instance_poller_set_enabled(bool enabled);

/**
 * @brief Consumes the most recent scan result, if one arrived since the last call.
 * @param out_path Receives the detected saves path, or an empty string when no instance was found.
 * @param max_len Size of the out_path buffer.
 * @return true when a fresh result was consumed, false when nothing new is waiting.
 */
bool instance_poller_take_result(char *out_path, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif //INSTANCE_POLLER_H
