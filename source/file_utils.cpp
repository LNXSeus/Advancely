//
// Created by Linus on 27.06.2025.
//

#include "file_utils.h"
#include "logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// function to read a JSON file
// In file_utils.cpp

cJSON* cJSON_from_file(const char* filename) {
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
    if (length > 100000000) { // 100 MB limit, generous for a JSON file
        log_message(LOG_ERROR, "[FILE_UTILS] File size is abnormally large (%ld bytes). Aborting read: %s\n", length, filename);
        fclose(f);
        return nullptr;
    }

    char *buffer = (char*)malloc(length + 1);
    if (!buffer) {
        log_message(LOG_ERROR, "[FILE_UTILS] Failed to allocate buffer for file: %s\n", filename);
        fclose(f);
        return nullptr;
    }

    size_t bytes_read = fread(buffer, 1, length, f);
    fclose(f);

    if (bytes_read != (size_t)length) {
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


// cJSON *cJSON_from_file(const char *filename) {
//     FILE *f = fopen(filename, "rb");
//     if (!f) {
//         return nullptr;
//     }
//
//     fseek(f, 0, SEEK_END);
//     long length = ftell(f);
//     fseek(f, 0, SEEK_SET);
//
//
//     if (length <= 0) {
//         fclose(f);
//         return cJSON_CreateObject();
//     }
//
//     // TODO: Remove
//     // if (length <= 0) {
//     //     fclose(f);
//     //     return nullptr; // A zero-byte or invalid file contains no data.
//     // }
//
//     // Sanity check to prevent allocating massive buffers due to file errors
//     if (length > 100000000) {
//         // 100 MB limit, generous for a JSON file
//         log_message(LOG_ERROR, "[FILE_UTILS] File size is abnormally large (%ld bytes). Aborting read: %s\n", length,
//                     filename);
//         fclose(f);
//         return nullptr;
//     }
//
//     char *buffer = (char *) malloc(length + 1);
//     if (!buffer) {
//         log_message(LOG_ERROR, "[FILE_UTILS] Failed to allocate buffer for file: %s\n", filename);
//         fclose(f);
//         return nullptr;
//     }
//
//     size_t bytes_read = fread(buffer, 1, length, f);
//     fclose(f);
//
//     if (bytes_read != (size_t) length) {
//         log_message(LOG_ERROR, "[FILE_UTILS] Failed to read entire file (size changed during read): %s\n", filename);
//         free(buffer);
//         return nullptr;
//     }
//
//     buffer[length] = '\0';
//
//     cJSON *json = cJSON_ParseWithLength(buffer, bytes_read);
//
//     if (json == nullptr) {
//         const char *error_ptr = cJSON_GetErrorPtr();
//         if (error_ptr != nullptr) {
//             log_message(LOG_ERROR, "[FILE UTILS] cJSON parse error near '%s' in file: %s\n", error_ptr, filename);
//         }
//     }
//
//     free(buffer);
//     return json;
// }
