// Copyright (c) 2025 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 02.09.2025.
//

#ifndef TEMPLATE_SCANNER_H
#define TEMPLATE_SCANNER_H

#include "main.h" // For MAX_PATH_LENGTH
#include <vector>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct {
    char category[MAX_PATH_LENGTH];
    char optional_flag[MAX_PATH_LENGTH];
    std::vector<std::string> available_lang_flags;
} DiscoveredTemplate;

/**
* @brief Scans the template directory for a given version to find all valid templates and all of their associated language files.
* It allocates memory for the results, which must be freed by the caller using free_discovered_templates().
* @param version_str The Minecraft version string (e.g., "1.16.1").
* @param out_templates A pointer to an array of DiscoveredTemplate structs that will be allocated.
* @param out_count A pointer to an integer that will store the number of templates found.
*/
void scan_for_templates(const char* version_str, DiscoveredTemplate** out_templates, int* out_count);

/**
* @brief Frees the memory allocated by scan_for_templates().
* @param templates The array of templates to free.
* @param count The number of templates in the array.
*/
void free_discovered_templates(DiscoveredTemplate** templates, int* count);

#ifdef __cplusplus
}
#endif

#endif //TEMPLATE_SCANNER_H
