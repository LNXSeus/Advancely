// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 06.04.2026.
//

#ifndef INVITE_CODE_H
#define INVITE_CODE_H

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generates a Base64 invite code from the host's IP, port, and display name.
 *
 * Combines the given IP, port, and host name as "IP\nPort\nHostName"
 * and Base64-encodes the result.
 *
 * @param ip The host's LAN/Hamachi IP address.
 * @param port The host port string (e.g., "25565").
 * @param host_name The host's display name (shown to receivers on decode).
 * @param out_code Buffer to write the Base64 invite code into.
 * @param out_code_len Size of the out_code buffer.
 * @return true on success, false if any input is empty or encoding fails.
 */
bool invite_code_generate(const char *ip, const char *port, const char *host_name,
                          char *out_code, size_t out_code_len);

/**
 * @brief Decodes a Base64 invite code back into IP, port, and host name.
 *
 * @param code The Base64-encoded invite code.
 * @param out_ip Buffer to store the decoded IP address.
 * @param ip_len Size of the out_ip buffer.
 * @param out_port Buffer to store the decoded port string.
 * @param port_len Size of the out_port buffer.
 * @param out_host_name Buffer to store the decoded host display name.
 * @param host_name_len Size of the out_host_name buffer.
 * @return true on success, false if decoding failed or format is invalid.
 */
bool invite_code_decode(const char *code, char *out_ip, size_t ip_len, char *out_port, size_t port_len,
                        char *out_host_name, size_t host_name_len);

#ifdef __cplusplus
}
#endif

#endif //INVITE_CODE_H