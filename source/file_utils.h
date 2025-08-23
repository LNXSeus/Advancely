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
 * This function is designed to be safe against race conditions where the
 * file might be modified during the read operation.
 *
 * @param filename The path to the JSON file.
 * @return A pointer to a cJSON object on success, or NULL on failure.
 * The caller is responsible for freeing the returned object with cJSON_Delete().
 */
    cJSON* cJSON_from_file(const char* filename);

// TODO: Remove
// /**
//  * @brief Reads a file and parses it into cJSON, WITH backslash escaping.
//  * This function should ONLY be used for reading settings.json to allow users
//  * to paste Windows paths without manually escaping them. This logic is more
//  * fragile and should not be used for potentially corrupt game files.
//  *
//  * @param filename The path to the JSON file (e.g., settings.json).
//  * @return A pointer to a cJSON object on success, or NULL on failure.
//  */
// cJSON* cJSON_from_file_with_escaping(const char* filename);

#ifdef __cplusplus
}
#endif


#endif //FILE_UTILS_H_H
