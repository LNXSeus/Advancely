//
// Created by Linus on 27.06.2025.
//

#ifndef FILE_UTILS_H_H
#define FILE_UTILS_H_H

#ifdef __cplusplus
extern "C" {
#endif

#include <cJSON.h>


/**
 * @brief Reads a file from the given path and parses it into a cJSON object.
 *
 * This function handles opening, reading, and closing the file.
 * The function even has safer chunk-based file reading to prevent race conditions.
 * This function also allows the saves path to be a windows path as it immediately escapes the backslashes
 * to prevent .json related errors.
 *
 * @param filename The path to the JSON file.
 * @return A pointer to a cJSON object on success, or NULL on failure.
 * The caller is responsible for freeing the returned object with cJSON_Delete().
 */
cJSON* cJSON_from_file(const char* filename);

#ifdef __cplusplus
}
#endif


#endif //FILE_UTILS_H_H
