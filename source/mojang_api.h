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

/**
 * @brief Resolves a player UUID to their current skin texture URL.
 *
 * Calls https://sessionserver.mojang.com/session/minecraft/profile/<uuid>,
 * base64-decodes the textures property, and extracts the SKIN url. The returned
 * URL points to a 64x64 (or legacy 64x32) PNG on textures.minecraft.net.
 *
 * Synchronous; intended to be called from a background worker. Returns false
 * on 404 (UUID not registered with Mojang -> caller should fall back to Notch),
 * network failure, or malformed response.
 *
 * @param uuid Hyphenated or unhyphenated UUID string.
 * @param out_url Buffer for the resulting URL.
 * @param url_max_len Size of out_url (at least 256 recommended).
 * @return true on success, false on any failure.
 */
bool mojang_fetch_skin_url(const char *uuid, char *out_url, size_t url_max_len);

/**
 * @brief Downloads a binary blob over HTTPS into a malloc'd buffer.
 *
 * Generic GET helper used by the skin cache to fetch the texture PNG once we
 * know its URL. Caller frees *out_data via free(). Synchronous; intended to be
 * called from a background worker.
 *
 * @param url The HTTPS URL to fetch.
 * @param out_data Receives a heap-allocated buffer (NULL on failure).
 * @param out_size Receives the byte length of the buffer.
 * @return true on HTTP 200 with non-empty body, false otherwise.
 */
bool mojang_download_url(const char *url, unsigned char **out_data, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif //MOJANG_API_H
