//
// Created by Linus on 04.07.2025.
//

#include "temp_create_utils.h"
#include <stdio.h>
#include <stdlib.h> // For free()
#include <string.h>
#include <sys/stat.h> // For stat and mkdir

#ifdef _WIN32
#include <direct.h> // For _mkdir
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0755) // 0755 provides read/write/execute for owner, read/execute for others
#endif

void fs_ensure_directory_exists(const char *path) {
    char *path_copy = strdup(path);
    if (!path_copy) return;

    // Iterate through the path and create each directory level
    for (char *p = path_copy + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char original_char = *p;
            *p = '\0'; // Temporarily terminate the string

            // Check if directory exists, if not, create it
            struct stat st = {0};
            if (stat(path_copy, &st) == -1) {
                MKDIR(path_copy);
            }

            *p = original_char; // Restore the slash
        }
    }
    free(path_copy);
}

void fs_create_empty_template_file(const char *path) {
    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "[TEMP CREATE UTILS] Failed to create template file: %s\n", path);
        return;
    }

    const char *skeleton = "{\n"
            "  \"advancements\": {},\n"
            "  \"stats\": [],\n"
            "  \"unlocks\": [],\n"
            "  \"custom\": [],\n"
            "  \"multi_stage_goals\": []\n"
            "}\n";
    fputs(skeleton, file);
    fclose(file);
    printf("[TEMP CREATE UTILS] Created template file: %s\n", path);
}

void fs_create_empty_lang_file(const char *path) {
    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "[TEMP CREATE UTILS] Failed to create language file: %s\n", path);
        return;
    }
    fputs("{\n}\n", file);
    fclose(file);
    printf("[TEMP CREATE UTILS] Created language file: %s\n", path);
}
