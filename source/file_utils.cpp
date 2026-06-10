// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 27.06.2025.
//

#include "file_utils.h"
#include "logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <io.h> // _commit, _fileno
#else
#include <unistd.h> // fsync, getpid
#endif

// function to read a JSON file
// In file_utils.cpp

cJSON *cJSON_from_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        // This is a common case when the game is saving, so we don't print an error.
        return nullptr;
    }

    // File size calculation
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (length <= 0) {
        fclose(f);
        return cJSON_Parse(""); // Return null for empty files
    }

    // Sanity check to prevent allocating massive buffers due to file errors
    if (length > 100000000) {
        // 100 MB limit, generous for a JSON file
        log_message(LOG_ERROR, "[FILE_UTILS] File size is abnormally large (%ld bytes). Aborting read: %s\n", length,
                    filename);
        fclose(f);
        return nullptr;
    }

    char *buffer = (char *) malloc(length + 1);
    if (!buffer) {
        log_message(LOG_ERROR, "[FILE_UTILS] Failed to allocate buffer for file: %s\n", filename);
        fclose(f);
        return nullptr;
    }

    size_t bytes_read = fread(buffer, 1, length, f);
    fclose(f);

    if (bytes_read != (size_t) length) {
        log_message(LOG_ERROR, "[FILE_UTILS] Failed to read entire file (size changed during read): %s\n", filename);
        free(buffer);
        return nullptr;
    }

    buffer[length] = '\0';

    // ==================== DEBUG LOGGING BLOCK ====================
    // This will write the raw buffer content to a separate log file for inspection.
    // It always overwrites the file to keep it small and relevant to the last read operation.
    FILE *debug_log = fopen("_last_read_content.log", "w");
    if (debug_log) {
        // log_message(LOG_ERROR, "File size: %ld bytes\n", length);
        fwrite(buffer, 1, bytes_read, debug_log);
        fclose(debug_log);
    }
    // ===============================================================

    // Directly parse the buffer without manual pre-processing.
    // Use ParseWithLength for maximum safety against malformed strings.
    cJSON *json = cJSON_ParseWithLength(buffer, bytes_read);

    if (json == nullptr) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != nullptr) {
            log_message(LOG_ERROR, "[FILE UTILS] cJSON parse error near '%s' in file: %s\n", error_ptr, filename);
        }
    }

    // Free the single buffer we used
    free(buffer);

    return json;
}

bool cJSON_write_to_file_atomic(const char *filename, const cJSON *root) {
    if (!filename || !root) return false;

    char *json_str = cJSON_Print(root);
    if (!json_str) {
        log_message(LOG_ERROR, "[FILE_UTILS] cJSON_Print failed while saving: %s\n", filename);
        return false;
    }
    size_t json_len = strlen(json_str);

    // Temp file lives in the same directory as the target so the rename stays on
    // one volume (a cross-volume rename is a copy+delete and is not atomic). The
    // PID suffix keeps two processes from clobbering each other's temp file.
    char tmp_path[1088];
#ifdef _WIN32
    unsigned long pid = (unsigned long) GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long) getpid();
#endif
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%lu", filename, pid);
    if (n < 0 || (size_t) n >= sizeof(tmp_path)) {
        log_message(LOG_ERROR, "[FILE_UTILS] Temp path too long while saving: %s\n", filename);
        free(json_str);
        return false;
    }

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        log_message(LOG_ERROR, "[FILE_UTILS] Failed to open temp file for writing: %s\n", tmp_path);
        free(json_str);
        return false;
    }

    bool ok = (fwrite(json_str, 1, json_len, f) == json_len);
    free(json_str);
    json_str = nullptr;

    // Flush stdio buffers, then force the bytes to physical storage before the
    // rename so a crash or power loss can't leave a renamed-but-empty file.
    if (ok && fflush(f) != 0) ok = false;
    if (ok) {
#ifdef _WIN32
        if (_commit(_fileno(f)) != 0) ok = false;
#else
        if (fsync(fileno(f)) != 0) ok = false;
#endif
    }
    if (fclose(f) != 0) ok = false;

    if (!ok) {
        log_message(LOG_ERROR, "[FILE_UTILS] Failed to write temp file: %s\n", tmp_path);
        remove(tmp_path);
        return false;
    }

    // Atomically replace the target. On NTFS and POSIX same-volume renames this
    // is atomic, so a concurrent reader always sees either the old or new file whole.
#ifdef _WIN32
    if (!MoveFileExA(tmp_path, filename, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        log_message(LOG_ERROR, "[FILE_UTILS] Failed to replace '%s' (err %lu).\n", filename,
                    (unsigned long) GetLastError());
        remove(tmp_path);
        return false;
    }
#else
    if (rename(tmp_path, filename) != 0) {
        log_message(LOG_ERROR, "[FILE_UTILS] Failed to rename temp over '%s'.\n", filename);
        remove(tmp_path);
        return false;
    }
#endif
    return true;
}
