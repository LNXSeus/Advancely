// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 27.06.2025.
//

#ifndef FILE_UTILS_H_H
#define FILE_UTILS_H_H

#ifdef __cplusplus
extern "C" {
#endif

#include <cJSON.h>
#include <stdbool.h>


/**
 * @brief Reads a file from the given path and parses it into a cJSON object.
 * This function is designed to be safe against race conditions where the
 * file might be modified during the read operation.
 *
 * @param filename The path to the JSON file.
 * @return A pointer to a cJSON object on success, or NULL on failure.
 * The caller is responsible for freeing the returned object with cJSON_Delete().
 */
cJSON *cJSON_from_file(const char *filename);

/**
 * @brief Atomically writes a cJSON object to a file.
 * The data is first written to a unique temporary file in the same directory,
 * flushed to physical storage, and then atomically renamed over the target.
 * This guarantees a concurrent reader (another thread, the file watcher, or a
 * second process) never observes a truncated or partially-written file, which
 * is the root cause of settings.json corruption.
 *
 * @param filename The destination path.
 * @param root The cJSON object to serialize (formatted/pretty-printed).
 * @return true on success, false if any step failed (the destination is left untouched on failure).
 */
bool cJSON_write_to_file_atomic(const char *filename, const cJSON *root);

#ifdef __cplusplus
}
#endif


#endif //FILE_UTILS_H_H
