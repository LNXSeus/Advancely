// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 02.09.2025.
//

#ifndef TEMPLATE_SCANNER_H
#define TEMPLATE_SCANNER_H

#include "main.h" // For MAX_PATH_LENGTH
#include <vector>
#include <string>

#include "cJSON.h"
#include "data_structures.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct {
    char category[MAX_PATH_LENGTH];
    char optional_flag[MAX_PATH_LENGTH];
    std::vector<std::string> available_lang_flags;
    bool has_layout; // True if the template has any manual layout positions or decorations
} DiscoveredTemplate;

// Helper to read manual positions from JSON
void parse_manual_pos(cJSON *parent_json, const char *key, ManualPos *pos);

/**
* @brief Scans the template directory for a given version to find all valid templates and all of their associated language files.
* It allocates memory for the results, which must be freed by the caller using free_discovered_templates().
* @param version_str The Minecraft version string (e.g., "1.16.1").
* @param out_templates A pointer to an array of DiscoveredTemplate structs that will be allocated.
* @param out_count A pointer to an integer that will store the number of templates found.
*/
void scan_for_templates(const char *version_str, DiscoveredTemplate **out_templates, int *out_count);

/**
* @brief Frees the memory allocated by scan_for_templates().
* @param templates The array of templates to free.
* @param count The number of templates in the array.
*/
void free_discovered_templates(DiscoveredTemplate **templates, int *count);

/**
 * @brief Computes a 64-bit hash of the goal structure in a template JSON file.
 * Hashes only the tracking-relevant parts: root_names, criteria keys, goal/target values,
 * multi-stage goal stages, counter linked goals. Ignores display names, icons, positions, decorations.
 * @param template_file_path Absolute path to the main template JSON file.
 * @return A 64-bit FNV-1a hash of the goal structure, or 0 if the file could not be read.
 */
uint64_t compute_template_goal_hash(const char *template_file_path);

#ifdef __cplusplus
}
#endif

#endif //TEMPLATE_SCANNER_H
