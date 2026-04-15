// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 06.04.2026.
//

#ifndef MOJANG_API_H
#define MOJANG_API_H

#include <cstddef> // For size_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fetches a Minecraft player's UUID from the Mojang API.
 *
 * Sends a GET request to https://api.mojang.com/users/profiles/minecraft/<username>
 * and parses the UUID from the JSON response. The returned UUID is formatted with
 * hyphens (e.g., "069a79f4-44e9-4726-a5be-fca90e38aaf5").
 *
 * @param username The Minecraft username to look up.
 * @param out_uuid Buffer to store the resulting UUID string (with hyphens).
 * @param uuid_max_len Size of the out_uuid buffer (should be at least 48).
 * @return true if the UUID was successfully fetched, false on error or if the player doesn't exist.
 */
bool mojang_fetch_uuid(const char *username, char *out_uuid, size_t uuid_max_len);

#ifdef __cplusplus
}
#endif

#endif //MOJANG_API_H
