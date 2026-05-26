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
#include "main.h"
#include <cJSON.h>
#include <curl/curl.h>
#include <mbedtls/base64.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>

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
    curl_easy_setopt(curl, CURLOPT_CAINFO, get_cert_bundle_path());
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

static void strip_uuid_hyphens(const char *in, char out[33]) {
    int oi = 0;
    for (int i = 0; in[i] && oi < 32; i++) {
        if (in[i] != '-') out[oi++] = in[i];
    }
    out[oi] = '\0';
}

bool mojang_fetch_skin_url(const char *uuid, char *out_url, size_t url_max_len) {
    if (!uuid || !out_url || url_max_len < 64) return false;
    out_url[0] = '\0';

    char raw_uuid[33];
    strip_uuid_hyphens(uuid, raw_uuid);
    if (strlen(raw_uuid) != 32) return false;

    char url[256];
    snprintf(url, sizeof(url),
             "https://sessionserver.mojang.com/session/minecraft/profile/%s",
             raw_uuid);

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    std::string body;
    bool success = false;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Advancely/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mojang_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_CAINFO, get_cert_bundle_path());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        log_message(LOG_ERROR, "[MOJANG API] profile fetch failed for %s: %s\n",
                    uuid, curl_easy_strerror(res));
        return false;
    }
    if (http_code == 404) {
        log_message(LOG_INFO, "[MOJANG API] UUID %s not found (404, likely offline account).\n", uuid);
        return false;
    }
    if (http_code != 200) {
        log_message(LOG_ERROR, "[MOJANG API] profile fetch HTTP %ld for %s.\n", http_code, uuid);
        return false;
    }

    cJSON *json = cJSON_Parse(body.c_str());
    if (!json) {
        log_message(LOG_ERROR, "[MOJANG API] profile JSON parse failed for %s.\n", uuid);
        return false;
    }

    const cJSON *props = cJSON_GetObjectItem(json, "properties");
    if (cJSON_IsArray(props)) {
        cJSON *prop;
        cJSON_ArrayForEach(prop, props) {
            const cJSON *pname = cJSON_GetObjectItem(prop, "name");
            const cJSON *pval = cJSON_GetObjectItem(prop, "value");
            if (!cJSON_IsString(pname) || !cJSON_IsString(pval)) continue;
            if (strcmp(pname->valuestring, "textures") != 0) continue;

            size_t b64_len = strlen(pval->valuestring);
            size_t decoded_max = (b64_len * 3) / 4 + 4;
            unsigned char *decoded = (unsigned char *) malloc(decoded_max + 1);
            if (!decoded) break;
            size_t decoded_len = 0;
            int rc = mbedtls_base64_decode(decoded, decoded_max, &decoded_len,
                                           (const unsigned char *) pval->valuestring, b64_len);
            if (rc != 0) {
                free(decoded);
                break;
            }
            decoded[decoded_len] = '\0';

            cJSON *tex_json = cJSON_Parse((const char *) decoded);
            free(decoded);
            if (!tex_json) break;

            const cJSON *tex = cJSON_GetObjectItem(tex_json, "textures");
            const cJSON *skin = tex ? cJSON_GetObjectItem(tex, "SKIN") : nullptr;
            const cJSON *url_json = skin ? cJSON_GetObjectItem(skin, "url") : nullptr;
            if (cJSON_IsString(url_json) && url_json->valuestring) {
                strncpy(out_url, url_json->valuestring, url_max_len - 1);
                out_url[url_max_len - 1] = '\0';
                success = true;
            }
            cJSON_Delete(tex_json);
            break;
        }
    }
    cJSON_Delete(json);

    if (!success) {
        log_message(LOG_ERROR, "[MOJANG API] No SKIN url in profile for %s.\n", uuid);
    }
    return success;
}

bool mojang_fetch_username_by_uuid(const char *uuid, char *out_name, size_t name_max_len) {
    if (!uuid || !out_name || name_max_len < 2) return false;
    out_name[0] = '\0';

    char raw_uuid[33];
    strip_uuid_hyphens(uuid, raw_uuid);
    if (strlen(raw_uuid) != 32) return false;

    char url[256];
    snprintf(url, sizeof(url),
             "https://sessionserver.mojang.com/session/minecraft/profile/%s",
             raw_uuid);

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    std::string body;
    bool success = false;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Advancely/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mojang_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_CAINFO, get_cert_bundle_path());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        log_message(LOG_ERROR, "[MOJANG API] username fetch failed for %s: %s\n",
                    uuid, curl_easy_strerror(res));
        return false;
    }
    if (http_code == 404) {
        log_message(LOG_INFO, "[MOJANG API] UUID %s not found (404, likely offline account).\n", uuid);
        return false;
    }
    if (http_code != 200) {
        log_message(LOG_ERROR, "[MOJANG API] username fetch HTTP %ld for %s.\n", http_code, uuid);
        return false;
    }

    cJSON *json = cJSON_Parse(body.c_str());
    if (!json) return false;
    const cJSON *name_json = cJSON_GetObjectItem(json, "name");
    if (cJSON_IsString(name_json) && name_json->valuestring && name_json->valuestring[0] != '\0') {
        strncpy(out_name, name_json->valuestring, name_max_len - 1);
        out_name[name_max_len - 1] = '\0';
        success = true;
        log_message(LOG_INFO, "[MOJANG API] Resolved UUID %s -> '%s'.\n", uuid, out_name);
    }
    cJSON_Delete(json);
    return success;
}

struct DownloadAccumulator {
    unsigned char *data;
    size_t size;
    size_t cap;
};

static size_t download_write_cb(void *contents, size_t size, size_t nmemb, DownloadAccumulator *acc) {
    size_t add = size * nmemb;
    if (acc->size + add > acc->cap) {
        size_t new_cap = acc->cap ? acc->cap * 2 : 4096;
        while (new_cap < acc->size + add) new_cap *= 2;
        unsigned char *grown = (unsigned char *) realloc(acc->data, new_cap);
        if (!grown) return 0;
        acc->data = grown;
        acc->cap = new_cap;
    }
    memcpy(acc->data + acc->size, contents, add);
    acc->size += add;
    return add;
}

bool mojang_download_url(const char *url, unsigned char **out_data, size_t *out_size) {
    if (!url || !out_data || !out_size) return false;
    *out_data = nullptr;
    *out_size = 0;

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    DownloadAccumulator acc = {nullptr, 0, 0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Advancely/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, download_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &acc);
    curl_easy_setopt(curl, CURLOPT_CAINFO, get_cert_bundle_path());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200 || acc.size == 0) {
        free(acc.data);
        log_message(LOG_ERROR, "[MOJANG API] download failed for %s: curl=%s http=%ld bytes=%zu\n",
                    url, curl_easy_strerror(res), http_code, acc.size);
        return false;
    }

    *out_data = acc.data;
    *out_size = acc.size;
    return true;
}
