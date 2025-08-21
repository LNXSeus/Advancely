//
// Created by Linus on 27.06.2025.
//

#include "file_utils.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// function to read a JSON file
cJSON* cJSON_from_file(const char* filename) {
    // Open the file in binary mode
    FILE *f = fopen(filename, "rb");

    if (!f) {
        fprintf(stderr, "[FILE_UTILS] Could not open file: %s\n", filename);
        return nullptr;
    }

    // Safer chunk-based file reading to prevent race conditions
    char *buffer = nullptr;
    char *tmp = nullptr;
    size_t total_read = 0;
    size_t chunk_size = 4096; // Read in 4KB chunks
    size_t bytes_read;

    while (true) {
        // Grow the buffer
        tmp = (char*)realloc(buffer, total_read + chunk_size + 1);
        if (!tmp) {
            fprintf(stderr, "[FILE_UTILS] Failed to reallocate buffer for file: %s\n", filename);
            free(buffer);
            fclose(f);
            return nullptr;
        }
        buffer = tmp;

        // Read the next chunk
        bytes_read = fread(buffer + total_read, 1, chunk_size, f);
        total_read += bytes_read;

        // If we read less than a full chunk, we've either hit the end of the file or an error
        if (bytes_read < chunk_size) {
            if (feof(f)) {
                // End of file, break loop
                break;
            } else if (ferror(f)) {
                // A read error occurred
                fprintf(stderr, "[FILE_UTILS] Error reading file: %s\n", filename);
                free(buffer);
                fclose(f);
                return nullptr;
            }
        }
    }
    fclose(f);
    buffer[total_read] = '\0'; // Null-terminate the final buffer

    cJSON *json = nullptr;
    if (buffer) {
        // Pre-process buffer to escape single backslashes to double backslashes
        // This allows users to paste Windows paths directly into settings.json
        long original_len = total_read;
        long backslash_count = 0;
        long i = 0;
        while (i < original_len) {
            if (buffer[i] == '\\') {
                if (i + 1 < original_len && buffer[i+1] == '\\') {
                    // This is an already-escaped backslash, skip it.
                    i += 2;
                } else {
                    // This is a single backslash that needs to be escaped.
                    backslash_count++;
                    i++;
                }
            } else {
                i++;
            }
        }

        // Allocate memory for the escaped buffer
        long escaped_len = original_len + backslash_count;
        char *escaped_buffer = (char*)malloc(escaped_len + 1);
        if (!escaped_buffer) {
            fprintf(stderr, "[FILE_UTILS] Failed to allocate memory for escaped buffer.\n");
            free(buffer);
            return nullptr;
        }

        // Process the buffer
        long j = 0;
        i = 0;
        while (i < original_len) {
            if (buffer[i] == '\\') {
                if (i + 1 < original_len && buffer[i+1] == '\\') {
                    // It's a \\, copy it as-is and skip both characters.
                    escaped_buffer[j++] = buffer[i];
                    escaped_buffer[j++] = buffer[i+1];
                    i += 2;
                } else {
                    // It's a single \, so escape it.
                    escaped_buffer[j++] = '\\';
                    escaped_buffer[j++] = '\\';
                    i++;
                }
            } else {
                // Not a backslash, just copy it.
                escaped_buffer[j++] = buffer[i];
                i++;
            }
        }
        escaped_buffer[j] = '\0';

        // Parse the processed buffer
        json = cJSON_Parse(escaped_buffer);

        if (json == nullptr) {
            const char *error_ptr = cJSON_GetErrorPtr();
            if (error_ptr != nullptr) {
                fprintf(stderr, "[FILE UTILS] cJSON parse error: [%s] in file: %s\n", error_ptr, filename);
            }
        }

        // Free both buffers
        free(buffer);
        free(escaped_buffer);
    }

    return json;
}