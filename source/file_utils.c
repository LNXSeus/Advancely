//
// Created by Linus on 27.06.2025.
//

#include "file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// function to read a JSON file
cJSON* cJSON_from_file(const char* filename) {
    char *buffer = NULL;
    long length;

    // Open the file in binary mode
    FILE *f = fopen(filename, "rb");
    cJSON *json = NULL;

    if (f) {
        // Get the file size
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        buffer = malloc(length + 1);
        if (buffer) {
            fread(buffer, 1, length, f);
            buffer[length] = '\0'; // Null-terminate the buffer
        }
        fclose(f);
    } else {
        fprintf(stderr, "[FILE_UTILS] Could not open file: %s\n", filename);
        return NULL;
    }

    if (buffer) {
        // Pre-process buffer to escape single backslashes to double backslashes
        // This allows users to paste Windows paths directly into settings.json
        long original_len = strlen(buffer);
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
        char *escaped_buffer = malloc(escaped_len + 1);
        if (!escaped_buffer) {
            fprintf(stderr, "[FILE_UTILS] Failed to allocate memory for escaped buffer.\n");
            free(buffer);
            return NULL;
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

        if (json == NULL) {
            const char *error_ptr = cJSON_GetErrorPtr();
            if (error_ptr != NULL) {
                fprintf(stderr, "[FILE UTILS] cJSON parse error: [%s] in file: %s\n", error_ptr, filename);
            }
        }

        // Free both buffers
        free(buffer);
        free(escaped_buffer);
    }

    return json;
}