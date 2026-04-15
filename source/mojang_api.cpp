// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 06.04.2026.
//

#include "mojang_api.h"
#include "logger.h"
#include <cJSON.h>
#include <curl/curl.h>
#include <string>
#include <cstring>
#include <cstdio>

// Path to the CA certificate bundle (same as update_checker.cpp)
#define CERT_BUNDLE_PATH "resources/ca_certificates/cacert.pem"

// Callback function for libcurl to write received data into a std::string
static size_t mojang_write_callback(void *contents, size_t size, size_t nmemb, std::string *s) {
    size_t new_length = size * nmemb;
    try {
        s->append((char *) contents, new_length);
    } catch (std::bad_alloc &) {
        return 0;
    }
    return new_length;
}

// Formats a raw 32-char UUID into the hyphenated form: 8-4-4-4-12
static void format_uuid_with_hyphens(const char *raw, char *out, size_t out_len) {
    snprintf(out, out_len, "%.8s-%.4s-%.4s-%.4s-%.12s",
             raw, raw + 8, raw + 12, raw + 16, raw + 20);
}

bool mojang_fetch_uuid(const char *username, char *out_uuid, size_t uuid_max_len) {
    if (!username || username[0] == '\0' || !out_uuid || uuid_max_len < 37) {
        return false;
    }
    out_uuid[0] = '\0';

    // Build the API URL
    char url[256];
    snprintf(url, sizeof(url), "https://api.mojang.com/users/profiles/minecraft/%s", username);

    CURL *curl = curl_easy_init();
    if (!curl) {
        log_message(LOG_ERROR, "[MOJANG API] Failed to initialize curl.\n");
        return false;
    }

    std::string read_buffer;
    bool success = false;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Advancely/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mojang_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_CAINFO, CERT_BUNDLE_PATH);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10 second timeout

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code == 200) {
            cJSON *json = cJSON_Parse(read_buffer.c_str());
            if (json) {
                const cJSON *id_json = cJSON_GetObjectItem(json, "id");
                if (cJSON_IsString(id_json) && id_json->valuestring && strlen(id_json->valuestring) == 32) {
                    format_uuid_with_hyphens(id_json->valuestring, out_uuid, uuid_max_len);
                    log_message(LOG_INFO, "[MOJANG API] Fetched UUID for '%s': %s\n", username, out_uuid);
                    success = true;
                } else {
                    log_message(LOG_ERROR, "[MOJANG API] Unexpected 'id' field in response for '%s'.\n", username);
                }
                cJSON_Delete(json);
            } else {
                log_message(LOG_ERROR, "[MOJANG API] Failed to parse JSON response for '%s'.\n", username);
            }
        } else if (http_code == 404) {
            log_message(LOG_INFO, "[MOJANG API] Player '%s' not found (404).\n", username);
        } else {
            log_message(LOG_ERROR, "[MOJANG API] Unexpected HTTP status %ld for '%s'.\n", http_code, username);
        }
    } else {
        log_message(LOG_ERROR, "[MOJANG API] curl_easy_perform() failed for '%s': %s\n",
                    username, curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    return success;
}
