// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 06.04.2026.
//

#include "invite_code.h"
#include "logger.h"
#include <curl/curl.h>
#include <string>
#include <cstring>
#include <cstdio>

#define CERT_BUNDLE_PATH "resources/ca_certificates/cacert.pem"

// --- Base64 encode/decode (RFC 4648) ---

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static bool base64_encode(const char *input, size_t input_len, char *output, size_t output_len) {
    size_t needed = 4 * ((input_len + 2) / 3) + 1;
    if (output_len < needed) return false;

    size_t j = 0;
    for (size_t i = 0; i < input_len;) {
        unsigned int a = (i < input_len) ? (unsigned char)input[i++] : 0;
        unsigned int b = (i < input_len) ? (unsigned char)input[i++] : 0;
        unsigned int c = (i < input_len) ? (unsigned char)input[i++] : 0;
        unsigned int triple = (a << 16) | (b << 8) | c;

        output[j++] = b64_table[(triple >> 18) & 0x3F];
        output[j++] = b64_table[(triple >> 12) & 0x3F];
        output[j++] = (i > input_len + 1) ? '=' : b64_table[(triple >> 6) & 0x3F];
        output[j++] = (i > input_len)     ? '=' : b64_table[triple & 0x3F];
    }
    output[j] = '\0';
    return true;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static bool base64_decode(const char *input, char *output, size_t output_len, size_t *decoded_len) {
    size_t input_len = strlen(input);
    // Strip trailing whitespace/newlines
    while (input_len > 0 && (input[input_len - 1] == '\n' || input[input_len - 1] == '\r' || input[input_len - 1] == ' ')) {
        input_len--;
    }
    if (input_len % 4 != 0) return false;

    size_t needed = (input_len / 4) * 3;
    if (output_len < needed + 1) return false;

    size_t j = 0;
    for (size_t i = 0; i < input_len; i += 4) {
        int a = b64_decode_char(input[i]);
        int b = b64_decode_char(input[i + 1]);
        int c = (input[i + 2] == '=') ? 0 : b64_decode_char(input[i + 2]);
        int d = (input[i + 3] == '=') ? 0 : b64_decode_char(input[i + 3]);

        if (a < 0 || b < 0 || c < 0 || d < 0) return false;

        unsigned int triple = ((unsigned int)a << 18) | ((unsigned int)b << 12) |
                              ((unsigned int)c << 6)  | (unsigned int)d;

        output[j++] = (char)((triple >> 16) & 0xFF);
        if (input[i + 2] != '=') output[j++] = (char)((triple >> 8) & 0xFF);
        if (input[i + 3] != '=') output[j++] = (char)(triple & 0xFF);
    }
    output[j] = '\0';
    if (decoded_len) *decoded_len = j;
    return true;
}

// --- Public IP fetch via api.ipify.org ---

static size_t ip_write_callback(void *contents, size_t size, size_t nmemb, std::string *s) {
    size_t new_length = size * nmemb;
    try {
        s->append((char *)contents, new_length);
    } catch (std::bad_alloc &) {
        return 0;
    }
    return new_length;
}

static bool fetch_public_ip(char *out_ip, size_t ip_len) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        log_message(LOG_ERROR, "[INVITE CODE] Failed to initialize curl.\n");
        return false;
    }

    std::string read_buffer;
    bool success = false;

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.ipify.org");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Advancely/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ip_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_CAINFO, CERT_BUNDLE_PATH);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code == 200 && !read_buffer.empty() && read_buffer.size() < ip_len) {
            strncpy(out_ip, read_buffer.c_str(), ip_len - 1);
            out_ip[ip_len - 1] = '\0';
            log_message(LOG_INFO, "[INVITE CODE] Fetched public IP: %s\n", out_ip);
            success = true;
        } else {
            log_message(LOG_ERROR, "[INVITE CODE] Unexpected response (HTTP %ld, len %zu).\n",
                        http_code, read_buffer.size());
        }
    } else {
        log_message(LOG_ERROR, "[INVITE CODE] curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    return success;
}

// --- Public API ---

bool invite_code_generate(const char *port, const char *host_name, char *out_code, size_t out_code_len) {
    if (!port || port[0] == '\0' || !host_name || host_name[0] == '\0' || !out_code || out_code_len < 32) {
        return false;
    }

    char ip[64];
    if (!fetch_public_ip(ip, sizeof(ip))) {
        return false;
    }

    // Build "IP\nPort\nHostName" string (newline delimiter avoids IPv6 colon ambiguity)
    char plain[256];
    snprintf(plain, sizeof(plain), "%s\n%s\n%s", ip, port, host_name);

    if (!base64_encode(plain, strlen(plain), out_code, out_code_len)) {
        log_message(LOG_ERROR, "[INVITE CODE] Base64 encode failed (buffer too small).\n");
        return false;
    }

    log_message(LOG_INFO, "[INVITE CODE] Generated invite code for %s:%s (host: %s)\n", ip, port, host_name);
    return true;
}

bool invite_code_decode(const char *code, char *out_ip, size_t ip_len, char *out_port, size_t port_len,
                        char *out_host_name, size_t host_name_len) {
    if (!code || code[0] == '\0' || !out_ip || !out_port || !out_host_name) {
        return false;
    }

    char decoded[512];
    size_t decoded_len = 0;
    if (!base64_decode(code, decoded, sizeof(decoded), &decoded_len)) {
        log_message(LOG_ERROR, "[INVITE CODE] Base64 decode failed for invite code.\n");
        return false;
    }

    // Format is "IP\nPort\nHostName"
    char *first_nl = strchr(decoded, '\n');
    if (!first_nl) {
        log_message(LOG_ERROR, "[INVITE CODE] Invalid invite code format (missing delimiters).\n");
        return false;
    }
    *first_nl = '\0';

    char *second_nl = strchr(first_nl + 1, '\n');
    if (!second_nl) {
        log_message(LOG_ERROR, "[INVITE CODE] Invalid invite code format (missing host name).\n");
        return false;
    }
    *second_nl = '\0';

    const char *ip_part = decoded;
    const char *port_part = first_nl + 1;
    const char *name_part = second_nl + 1;

    if (strlen(ip_part) >= ip_len || strlen(port_part) >= port_len || strlen(name_part) >= host_name_len) {
        log_message(LOG_ERROR, "[INVITE CODE] Decoded fields too long for buffers.\n");
        return false;
    }

    strncpy(out_ip, ip_part, ip_len - 1);
    out_ip[ip_len - 1] = '\0';
    strncpy(out_port, port_part, port_len - 1);
    out_port[port_len - 1] = '\0';
    strncpy(out_host_name, name_part, host_name_len - 1);
    out_host_name[host_name_len - 1] = '\0';

    log_message(LOG_INFO, "[INVITE CODE] Decoded invite code -> Host: %s, Port: %s\n", out_host_name, out_port);
    return true;
}