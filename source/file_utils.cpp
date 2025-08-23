//
// Created by Linus on 27.06.2025.
//

#include "file_utils.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// function to read a JSON file
// In file_utils.cpp

cJSON* cJSON_from_file(const char* filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (length <= 0) {
        fclose(f);
        return cJSON_CreateObject();
    }

    // Sanity check to prevent allocating massive buffers due to file errors
    if (length > 100000000) { // 100 MB limit, generous for a JSON file
        fprintf(stderr, "[FILE_UTILS] File size is abnormally large (%ld bytes). Aborting read: %s\n", length, filename);
        fclose(f);
        return nullptr;
    }

    char *buffer = (char*)malloc(length + 1);
    if (!buffer) {
        fprintf(stderr, "[FILE_UTILS] Failed to allocate buffer for file: %s\n", filename);
        fclose(f);
        return nullptr;
    }

    size_t bytes_read = fread(buffer, 1, length, f);
    fclose(f);

    if (bytes_read != (size_t)length) {
        fprintf(stderr, "[FILE_UTILS] Failed to read entire file (size changed during read): %s\n", filename);
        free(buffer);
        return nullptr;
    }

    buffer[length] = '\0';

    cJSON *json = cJSON_ParseWithLength(buffer, bytes_read);

    if (json == nullptr) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != nullptr) {
            fprintf(stderr, "[FILE UTILS] cJSON parse error near '%s' in file: %s\n", error_ptr, filename);
        }
    }

    free(buffer);
    return json;
}

// TODO: Remove
// // Safe version forr game files (no escaping)
// cJSON* cJSON_from_file(const char* filename) {
//     FILE *f = fopen(filename, "rb");
//     if (!f) {
//         return nullptr;
//     }
//
//     fseek(f, 0, SEEK_END);
//     long length = ftell(f);
//     fseek(f, 0, SEEK_SET);
//
//     if (length <= 0) {
//         fclose(f);
//         return cJSON_CreateObject();
//     }
//
//     char *buffer = (char*)malloc(length + 1);
//     if (!buffer) {
//         fprintf(stderr, "[FILE_UTILS] Failed to allocate buffer for file: %s\n", filename);
//         fclose(f);
//         return nullptr;
//     }
//
//     size_t bytes_read = fread(buffer, 1, length, f);
//     fclose(f);
//
//     if (bytes_read != (size_t)length) {
//         fprintf(stderr, "[FILE_UTILS] Failed to read entire file (size changed during read): %s\n", filename);
//         free(buffer);
//         buffer = nullptr;
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
//             fprintf(stderr, "[FILE UTILS] cJSON parse error near '%s' in file: %s\n", error_ptr, filename);
//         }
//     }
//
//     free(buffer);
//     buffer = nullptr;
//     return json;
// }
//
// // VERSION WITH BACKSLASH ESCAPING (for settings.json ONLY)
// cJSON* cJSON_from_file_with_escaping(const char* filename) {
//     FILE *f = nullptr;
//     char *buffer = nullptr;
//     char *escaped_buffer = nullptr;
//     cJSON *json = nullptr;
//     long length = 0;
//     size_t bytes_read = 0;
//     long original_len = 0;
//     long backslash_count = 0;
//     long escaped_len = 0;
//     long i = 0;
//     long j = 0;
//
//     f = fopen(filename, "rb");
//     if (!f) {
//         goto cleanup;
//     }
//
//     fseek(f, 0, SEEK_END);
//     length = ftell(f);
//     fseek(f, 0, SEEK_SET);
//
//     if (length <= 0) {
//         json = cJSON_CreateObject();
//         goto cleanup;
//     }
//
//     buffer = (char*)malloc(length + 1);
//     if (!buffer) {
//         fprintf(stderr, "[FILE_UTILS] Failed to allocate buffer for file: %s\n", filename);
//         goto cleanup;
//     }
//
//     bytes_read = fread(buffer, 1, length, f);
//     if (bytes_read != (size_t)length) {
//          fprintf(stderr, "[FILE_UTILS] Failed to read entire settings file (size changed during read): %s\n", filename);
//          goto cleanup;
//     }
//     buffer[length] = '\0';
//
//     // --- Start of escaping logic ---
//     original_len = length;
//     backslash_count = 0;
//     i = 0;
//     while (i < original_len) {
//         if (buffer[i] == '\\') {
//             if (i + 1 < original_len && buffer[i+1] == '\\') {
//                 i += 2;
//             } else {
//                 backslash_count++;
//                 i++;
//             }
//         } else {
//             i++;
//         }
//     }
//
//     escaped_len = original_len + backslash_count;
//     escaped_buffer = (char*)malloc(escaped_len + 1);
//     if (!escaped_buffer) {
//         fprintf(stderr, "[FILE_UTILS] Failed to allocate memory for escaped buffer.\n");
//         goto cleanup;
//     }
//
//     j = 0;
//     i = 0;
//     while (i < original_len) {
//         if (buffer[i] == '\\') {
//             if (i + 1 < original_len && buffer[i+1] == '\\') {
//                 escaped_buffer[j++] = buffer[i];
//                 escaped_buffer[j++] = buffer[i+1];
//                 i += 2;
//             } else {
//                 escaped_buffer[j++] = '\\';
//                 escaped_buffer[j++] = '\\';
//                 i++;
//             }
//         } else {
//             escaped_buffer[j++] = buffer[i];
//             i++;
//         }
//     }
//     escaped_buffer[j] = '\0';
//
//     json = cJSON_ParseWithLength(escaped_buffer, escaped_len);
//
//     if (json == nullptr) {
//         const char *error_ptr = cJSON_GetErrorPtr();
//         if (error_ptr != nullptr) {
//             fprintf(stderr, "[FILE UTILS] cJSON parse error near '%s' in file: %s\n", error_ptr, filename);
//         }
//     }
//
// cleanup:
//     if (f) {
//         fclose(f);
//     }
//     if (buffer) {
//         free(buffer);
//     }
//     if (escaped_buffer) {
//         free(escaped_buffer);
//     }
//
//     return json;
// }

// // VERSION WITH BACKSLASH ESCAPING (for settings.json ONLY)
// cJSON* cJSON_from_file_with_escaping(const char* filename) {
//     FILE *f = fopen(filename, "rb");
//     if (!f) {
//         return nullptr;
//     }
//
//     fseek(f, 0, SEEK_END);
//     long length = ftell(f);
//     fseek(f, 0, SEEK_SET);
//
//     if (length <= 0) {
//         fclose(f);
//         return cJSON_CreateObject();
//     }
//
//     char *buffer = (char*)malloc(length + 1);
//     if (!buffer) {
//         fprintf(stderr, "[FILE_UTILS] Failed to allocate buffer for file: %s\n", filename);
//         fclose(f);
//         return nullptr;
//     }
//
//     size_t bytes_read = fread(buffer, 1, length, f);
//     fclose(f);
//
//     if (bytes_read != (size_t)length) {
//          fprintf(stderr, "[FILE_UTILS] Failed to read entire settings file (size changed during read): %s\n", filename);
//          free(buffer);
//          buffer = nullptr;
//          return nullptr;
//     }
//     buffer[length] = '\0';
//
//     cJSON *json = nullptr;
//     long original_len = length;
//     long backslash_count = 0;
//     long i = 0;
//     while (i < original_len) {
//         if (buffer[i] == '\\') {
//             if (i + 1 < original_len && buffer[i+1] == '\\') {
//                 i += 2;
//             } else {
//                 backslash_count++;
//                 i++;
//             }
//         } else {
//             i++;
//         }
//     }
//
//     long escaped_len = original_len + backslash_count;
//     char *escaped_buffer = (char*)malloc(escaped_len + 1);
//     if (!escaped_buffer) {
//         fprintf(stderr, "[FILE_UTILS] Failed to allocate memory for escaped buffer.\n");
//         free(buffer);
//         buffer = nullptr;
//         return nullptr;
//     }
//
//     long j = 0;
//     i = 0;
//     while (i < original_len) {
//         if (buffer[i] == '\\') {
//             if (i + 1 < original_len && buffer[i+1] == '\\') {
//                 escaped_buffer[j++] = buffer[i];
//                 escaped_buffer[j++] = buffer[i+1];
//                 i += 2;
//             } else {
//                 escaped_buffer[j++] = '\\';
//                 escaped_buffer[j++] = '\\';
//                 i++;
//             }
//         } else {
//             escaped_buffer[j++] = buffer[i];
//             i++;
//         }
//     }
//     escaped_buffer[j] = '\0';
//
//     json = cJSON_ParseWithLength(escaped_buffer, escaped_len);
//
//     if (json == nullptr) {
//         const char *error_ptr = cJSON_GetErrorPtr();
//         if (error_ptr != nullptr) {
//             fprintf(stderr, "[FILE UTILS] cJSON parse error near '%s' in file: %s\n", error_ptr, filename);
//         }
//     }
//
//     free(buffer);
//     buffer = nullptr;
//     free(escaped_buffer);
//     escaped_buffer = nullptr;
//
//     return json;
// }

// NO CHUNK BASED READING
// cJSON* cJSON_from_file(const char* filename) {
//     FILE *f = fopen(filename, "rb");
//     if (!f) {
//         // This is a common case when the game is saving, so we don't print an error.
//         return nullptr;
//     }
//
//     fseek(f, 0, SEEK_END);
//     long length = ftell(f);
//     fseek(f, 0, SEEK_SET);
//
//     if (length <= 0) {
//         fclose(f);
//         return cJSON_Parse(""); // Return null for empty files
//     }
//
//     char *buffer = (char*)malloc(length + 1);
//     if (!buffer) {
//         fprintf(stderr, "[FILE_UTILS] Failed to allocate buffer for file: %s\n", filename);
//         fclose(f);
//         return nullptr;
//     }
//
//     size_t bytes_read = fread(buffer, 1, length, f);
//     fclose(f);
//
//     if (bytes_read != (size_t)length) {
//          fprintf(stderr, "[FILE_UTILS] Failed to read entire file: %s\n", filename);
//          free(buffer);
//          return nullptr;
//     }
//
//     buffer[length] = '\0';
//
//     cJSON *json = nullptr;
//     // The rest of your backslash-escaping logic remains the same.
//     // Pre-process buffer to escape single backslashes to double backslashes
//     // This allows users to paste Windows paths directly into settings.json
//     long original_len = length;
//     long backslash_count = 0;
//     long i = 0;
//     while (i < original_len) {
//         if (buffer[i] == '\\') {
//             if (i + 1 < original_len && buffer[i+1] == '\\') {
//                 // This is an already-escaped backslash, skip it.
//                 i += 2;
//             } else {
//                 // This is a single backslash that needs to be escaped.
//                 backslash_count++;
//                 i++;
//             }
//         } else {
//             i++;
//         }
//     }
//
//     // Allocate memory for the escaped buffer
//     long escaped_len = original_len + backslash_count;
//     char *escaped_buffer = (char*)malloc(escaped_len + 1);
//     if (!escaped_buffer) {
//         fprintf(stderr, "[FILE_UTILS] Failed to allocate memory for escaped buffer.\n");
//         free(buffer);
//         return nullptr;
//     }
//
//     // Process the buffer
//     long j = 0;
//     i = 0;
//     while (i < original_len) {
//         if (buffer[i] == '\\') {
//             if (i + 1 < original_len && buffer[i+1] == '\\') {
//                 // It's a \\, copy it as-is and skip both characters.
//                 escaped_buffer[j++] = buffer[i];
//                 escaped_buffer[j++] = buffer[i+1];
//                 i += 2;
//             } else {
//                 // It's a single \, so escape it.
//                 escaped_buffer[j++] = '\\';
//                 escaped_buffer[j++] = '\\';
//                 i++;
//             }
//         } else {
//             // Not a backslash, just copy it.
//             escaped_buffer[j++] = buffer[i];
//             i++;
//         }
//     }
//     escaped_buffer[j] = '\0';
//
//     // Parse the processed buffer
//     json = cJSON_ParseWithLength(escaped_buffer, escaped_len);
//
//     if (json == nullptr) {
//         const char *error_ptr = cJSON_GetErrorPtr();
//         if (error_ptr != nullptr) {
//             fprintf(stderr, "[FILE UTILS] cJSON parse error: [%s] in file: %s\n", error_ptr, filename);
//         }
//     }
//
//     // Free both buffers
//     free(buffer);
//     buffer = nullptr;
//     free(escaped_buffer);
//     escaped_buffer = nullptr;
//
//     return json;
// }

