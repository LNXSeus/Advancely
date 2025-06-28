//
// Created by Linus on 27.06.2025.
//

#ifndef FILE_UTILS_H_H
#define FILE_UTILS_H_H

#include <cJSON.h>

/**
 * @brief Reads a file from the given path and parses it into a cJSON object.
 *
 * This function handles opening, reading, and closing the file.
 *
 * @param filename The path to the JSON file.
 * @return A pointer to a cJSON object on success, or NULL on failure.
 * The caller is responsible for freeing the returned object with cJSON_Delete().
 */
cJSON* cJSON_from_file(const char* filename);

#endif //FILE_UTILS_H_H
