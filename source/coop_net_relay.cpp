// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 25.04.2026.
//
// TLS client to the Advancely relay server with SHA-256 cert pinning.
// CA chain validation is intentionally skipped because the relay uses a
// self-signed cert; trust is established by comparing the peer cert's
// SHA-256 against the constant baked into relay_config.h at build time.

#include "coop_net_relay.h"
#include "relay_config.h"
#include "logger.h"

#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <cJSON.h>
#include <SDL3/SDL.h>
#include <miniz.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

struct RelayConn {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
};

static bool parse_pinned_fingerprint(unsigned char out[32]) {
    const char *s = RELAY_CERT_FINGERPRINT_SHA256;
    int byte_idx = 0;
    while (*s && byte_idx < 32) {
        if (*s == ':') { s++; continue; }
        if (!s[0] || !s[1]) return false;
        char hex[3] = { s[0], s[1], 0 };
        char *endp = nullptr;
        unsigned long v = strtoul(hex, &endp, 16);
        if (endp != hex + 2) return false;
        out[byte_idx++] = (unsigned char) v;
        s += 2;
    }
    while (*s) {
        if (*s != ':') return false;
        s++;
    }
    return byte_idx == 32;
}

static void format_fingerprint(const unsigned char *fp, char *out, size_t out_size) {
    if (out_size < 96) {
        if (out_size) out[0] = '\0';
        return;
    }
    char *p = out;
    for (int i = 0; i < 32; i++) {
        snprintf(p, 4, "%02X", fp[i]);
        p += 2;
        if (i < 31) *p++ = ':';
    }
    *p = '\0';
}

static void write_err(char *out_err, size_t err_size, const char *msg, int rc) {
    if (!out_err || !err_size) return;
    if (rc) {
        char rcbuf[128];
        mbedtls_strerror(rc, rcbuf, sizeof(rcbuf));
        snprintf(out_err, err_size, "%s: %s (-0x%04x)", msg, rcbuf, (unsigned) -rc);
    } else {
        snprintf(out_err, err_size, "%s", msg);
    }
}

RelayConn *relay_connect(char *out_err, size_t err_size) {
    unsigned char pinned_fp[32];
    if (!parse_pinned_fingerprint(pinned_fp)) {
        write_err(out_err, err_size, "invalid pinned fingerprint constant", 0);
        return nullptr;
    }

    psa_status_t pst = psa_crypto_init();
    if (pst != PSA_SUCCESS) {
        char msg[64];
        snprintf(msg, sizeof(msg), "psa_crypto_init failed: %d", (int) pst);
        write_err(out_err, err_size, msg, 0);
        return nullptr;
    }

    RelayConn *c = (RelayConn *) calloc(1, sizeof(RelayConn));
    if (!c) {
        write_err(out_err, err_size, "allocation failed", 0);
        return nullptr;
    }
    mbedtls_net_init(&c->net);
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);

    int rc;
    char port_str[12];
    snprintf(port_str, sizeof(port_str), "%d", RELAY_PORT);
    rc = mbedtls_net_connect(&c->net, RELAY_HOST, port_str, MBEDTLS_NET_PROTO_TCP);
    if (rc) { write_err(out_err, err_size, "tcp connect", rc); relay_close(c); return nullptr; }

    rc = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc) { write_err(out_err, err_size, "config_defaults", rc); relay_close(c); return nullptr; }

    // We pin the server cert ourselves below, so don't bother validating a CA
    // chain we never installed. Pinning is strictly stronger than CA-trust
    // here: even a real CA-issued cert for this IP would be rejected.
    // (RNG is wired internally via PSA — psa_crypto_init() above is enough,
    // mbedTLS 4.x removed the explicit conf_rng hook.)
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);

    rc = mbedtls_ssl_setup(&c->ssl, &c->conf);
    if (rc) { write_err(out_err, err_size, "ssl_setup", rc); relay_close(c); return nullptr; }

    rc = mbedtls_ssl_set_hostname(&c->ssl, RELAY_HOST);
    if (rc) { write_err(out_err, err_size, "set_hostname", rc); relay_close(c); return nullptr; }

    // Provide f_recv_timeout so relay_set_read_timeout can drive timed reads
    // post-handshake. mbedtls_net_recv_timeout uses select() under the hood.
    mbedtls_ssl_set_bio(&c->ssl, &c->net, mbedtls_net_send, mbedtls_net_recv,
                        mbedtls_net_recv_timeout);

    while ((rc = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            write_err(out_err, err_size, "tls handshake", rc);
            relay_close(c);
            return nullptr;
        }
    }

    const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&c->ssl);
    if (!peer || !peer->raw.p || peer->raw.len == 0) {
        write_err(out_err, err_size, "no peer cert", 0);
        relay_close(c);
        return nullptr;
    }

    unsigned char actual_fp[32];
    size_t hash_len = 0;
    pst = psa_hash_compute(PSA_ALG_SHA_256, peer->raw.p, peer->raw.len,
                           actual_fp, sizeof(actual_fp), &hash_len);
    if (pst != PSA_SUCCESS || hash_len != 32) {
        char msg[64];
        snprintf(msg, sizeof(msg), "psa_hash_compute failed: %d", (int) pst);
        write_err(out_err, err_size, msg, 0);
        relay_close(c);
        return nullptr;
    }

    if (memcmp(pinned_fp, actual_fp, 32) != 0) {
        char actual_hex[100];
        format_fingerprint(actual_fp, actual_hex, sizeof(actual_hex));
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "fingerprint mismatch: server presented %s (MITM or stale pin)",
                 actual_hex);
        write_err(out_err, err_size, msg, 0);
        relay_close(c);
        return nullptr;
    }

    return c;
}

void relay_close(RelayConn *conn) {
    if (!conn) return;
    // close_notify is best-effort; ignore errors.
    mbedtls_ssl_close_notify(&conn->ssl);
    mbedtls_ssl_free(&conn->ssl);
    mbedtls_ssl_config_free(&conn->conf);
    mbedtls_net_free(&conn->net);
    free(conn);
}

// ---- Framed send/recv over TLS ----
// Mirrors coop_net.cpp's send_message / recv path but speaks mbedtls_ssl_*.
// Wire format is identical: [4B type BE][4B length BE][payload].

static void put_be32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char) (v >> 24);
    p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);
    p[3] = (unsigned char) v;
}

static uint32_t get_be32(const unsigned char *p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static thread_local char tls_last_error[192] = {0};

const char *relay_last_error(void) { return tls_last_error; }

void relay_clear_last_error(void) { tls_last_error[0] = '\0'; }

static void set_last_error(const char *where, int n) {
    char strbuf[128] = "?";
    // Distinguish the common peer-side close modes so the log tells us
    // *who* tore the stream down. n == 0 from mbedtls_ssl_read means the
    // peer's TLS layer signalled close (close_notify or socket EOF before
    // any alert). The two named mbedTLS codes below are the explicit
    // variants we'd see if mbedTLS surfaces them instead of collapsing to 0.
    if (n == 0) {
        snprintf(strbuf, sizeof(strbuf), "peer closed stream (close_notify or socket EOF)");
    } else if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        snprintf(strbuf, sizeof(strbuf), "peer sent TLS close_notify (graceful shutdown)");
    } else if (n == MBEDTLS_ERR_NET_CONN_RESET) {
        snprintf(strbuf, sizeof(strbuf), "TCP connection reset by peer (RST)");
    } else {
        mbedtls_strerror(n, strbuf, sizeof(strbuf));
    }
    snprintf(tls_last_error, sizeof(tls_last_error),
             "%s: %s (n=%d, -0x%04x)", where, strbuf, n, (unsigned) -n);
    log_message(LOG_ERROR, "[Relay] %s\n", tls_last_error);
}

static bool ssl_write_all(mbedtls_ssl_context *ssl, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *) data;
    const size_t total = len;
    const Uint32 t_start = SDL_GetTicks();
    Uint32 t_last_progress = t_start;
    while (len > 0) {
        int n = mbedtls_ssl_write(ssl, p, len);
        if (n > 0) {
            p += n;
            len -= (size_t) n;
            t_last_progress = SDL_GetTicks();
            continue;
        }
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        set_last_error("ssl_write", n);
        char base[160];
        strncpy(base, tls_last_error, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        snprintf(tls_last_error, sizeof(tls_last_error),
                 "%s [wrote=%zu/%zu elapsed=%ums idle=%ums]",
                 base, total - len, total,
                 (unsigned)(SDL_GetTicks() - t_start),
                 (unsigned)(SDL_GetTicks() - t_last_progress));
        log_message(LOG_ERROR, "[Relay] %s\n", tls_last_error);
        return false;
    }
    return true;
}

// Returns 1 on full read, 0 on timeout (only meaningful if read_timeout is
// configured AND zero bytes had been read so far for the current call), -1
// on connection error. A timeout that occurs mid-buffer is treated as an
// error since we can't preserve partial frame state across calls.
static int ssl_read_exact(mbedtls_ssl_context *ssl, void *buf, size_t len,
                          size_t *out_consumed,
                          uint32_t *out_elapsed_ms,
                          uint32_t *out_idle_ms) {
    unsigned char *p = (unsigned char *) buf;
    size_t total = len;
    Uint32 t_start = SDL_GetTicks();
    Uint32 t_last_progress = t_start;
    while (len > 0) {
        int n = mbedtls_ssl_read(ssl, p, len);
        if (n > 0) {
            p += n;
            len -= (size_t) n;
            t_last_progress = SDL_GetTicks();
            continue;
        }
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        // TLS 1.3 servers may send a NewSessionTicket as a post-handshake
        // message before any application data. mbedTLS surfaces that to the
        // app; we just want to retry the read.
        if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
        if (n == MBEDTLS_ERR_SSL_TIMEOUT) {
            // Only surface as timeout when nothing was consumed yet; otherwise
            // we'd lose mid-frame bytes by retrying from scratch later.
            if (len == total) {
                if (out_elapsed_ms) *out_elapsed_ms = SDL_GetTicks() - t_start;
                if (out_idle_ms) *out_idle_ms = SDL_GetTicks() - t_last_progress;
                return 0;
            }
            continue;
        }
        if (out_consumed) *out_consumed = total - len;
        if (out_elapsed_ms) *out_elapsed_ms = SDL_GetTicks() - t_start;
        if (out_idle_ms) *out_idle_ms = SDL_GetTicks() - t_last_progress;
        set_last_error(len == total ? "ssl_read(header)" : "ssl_read(midframe)", n);
        return -1;
    }
    if (out_consumed) *out_consumed = total;
    if (out_elapsed_ms) *out_elapsed_ms = SDL_GetTicks() - t_start;
    if (out_idle_ms) *out_idle_ms = SDL_GetTicks() - t_last_progress;
    return 1;
}

// Wire-level compression flag: bit 31 of the type field. The relay forwards
// frames opaquely, so this is purely between host and receivers — both ends
// use the same helpers below and the relay never has to decode it.
//
// Compressed frame layout (set the flag, then payload is):
//   [4B uncompressed_len BE][zlib-deflated original_payload]
//
// Below the threshold the deflate overhead would dwarf any size win, so we
// skip compression entirely. We also fall back to uncompressed if the
// deflated result isn't actually smaller (already-compressed inputs, tiny
// inputs that round up after framing).
static const uint32_t RELAY_FRAME_FLAG_COMPRESSED = 0x80000000u;
static const uint32_t RELAY_FRAME_TYPE_MASK = 0x7FFFFFFFu;
static const uint32_t RELAY_COMPRESS_THRESHOLD = 8 * 1024;

// Compresses src into a fresh malloc'd buffer with a 4-byte BE
// uncompressed-length prefix. On success returns the buffer in *out and the
// wire length (prefix + deflated bytes) in *out_len. Returns false if
// compression failed or the result wasn't smaller than the input; caller
// should fall back to an uncompressed frame in that case.
static bool relay_compress_payload(const void *src, uint32_t src_len,
                                   void **out, uint32_t *out_len) {
    if (!src || src_len == 0 || !out || !out_len) return false;
    mz_ulong bound = mz_compressBound((mz_ulong) src_len);
    if (bound > UINT32_MAX - 4) return false;
    unsigned char *buf = (unsigned char *) malloc(4 + bound);
    if (!buf) return false;
    mz_ulong dest_len = bound;
    int rc = mz_compress(buf + 4, &dest_len,
                         (const unsigned char *) src, (mz_ulong) src_len);
    if (rc != MZ_OK) {
        free(buf);
        return false;
    }
    if ((uint32_t)(4 + dest_len) >= src_len) {
        // No size win — caller will send uncompressed.
        free(buf);
        return false;
    }
    put_be32(buf, src_len);
    *out = buf;
    *out_len = (uint32_t)(4 + dest_len);
    return true;
}

// Inverse of relay_compress_payload. Wire bytes -> heap buffer of the
// original uncompressed size. Frees src is the caller's job.
static bool relay_decompress_payload(const void *src, uint32_t src_len,
                                     void **out, uint32_t *out_len) {
    if (!src || src_len < 4 || !out || !out_len) return false;
    const unsigned char *p = (const unsigned char *) src;
    uint32_t uncompressed_len = get_be32(p);
    if (uncompressed_len == 0) {
        *out = nullptr;
        *out_len = 0;
        return true;
    }
    unsigned char *buf = (unsigned char *) malloc(uncompressed_len);
    if (!buf) return false;
    mz_ulong dest_len = uncompressed_len;
    int rc = mz_uncompress(buf, &dest_len, p + 4, (mz_ulong)(src_len - 4));
    if (rc != MZ_OK || dest_len != uncompressed_len) {
        free(buf);
        return false;
    }
    *out = buf;
    *out_len = uncompressed_len;
    return true;
}

bool relay_send_frame(RelayConn *c, uint32_t type, const void *payload, uint32_t payload_len) {
    if (!c) return false;
    // The compression flag is reserved — callers must not set it themselves.
    type &= RELAY_FRAME_TYPE_MASK;

    const void *wire_payload = payload;
    uint32_t wire_len = payload_len;
    uint32_t wire_type = type;
    void *compressed_buf = nullptr;

    if (payload && payload_len >= RELAY_COMPRESS_THRESHOLD) {
        void *cbuf = nullptr;
        uint32_t clen = 0;
        if (relay_compress_payload(payload, payload_len, &cbuf, &clen)) {
            compressed_buf = cbuf;
            wire_payload = cbuf;
            wire_len = clen;
            wire_type = type | RELAY_FRAME_FLAG_COMPRESSED;
        }
    }

    unsigned char hdr[8];
    put_be32(hdr, wire_type);
    put_be32(hdr + 4, wire_len);
    bool ok = ssl_write_all(&c->ssl, hdr, 8);
    if (ok && wire_len > 0 && wire_payload) {
        ok = ssl_write_all(&c->ssl, wire_payload, wire_len);
    }
    if (compressed_buf) free(compressed_buf);
    return ok;
}

// Short tag for a COOP_MSG_* type so logs are readable without a header lookup.
// Keep in sync with coop_net.h's COOP_MSG_* enum. Unknown types fall back to
// "?" so the numeric type=N still tells you what it is.
static const char *coop_msg_type_name(uint32_t type) {
    switch (type) {
        case 1: return "HEARTBEAT";
        case 2: return "HEARTBEAT_ACK";
        case 3: return "DISCONNECT";
        case 4: return "STATE_UPDATE";
        case 5: return "TEMPLATE_SYNC";
        case 6: return "JOIN_REQUEST";
        case 7: return "JOIN_ACCEPT";
        case 8: return "JOIN_REJECT";
        case 9: return "PLAYER_LIST";
        case 10: return "KICK";
        case 11: return "CUSTOM_GOAL_MOD";
        case 12: return "PLAYER_STATES";
        case 13: return "LEGACY_STATS_UPLOAD";
        default: return "?";
    }
}

int relay_recv_frame_timed(RelayConn *c, uint32_t *out_type, void **out_payload,
                           uint32_t *out_len, uint32_t max_payload) {
    if (!c || !out_type || !out_payload || !out_len) return -1;
    *out_payload = nullptr;
    *out_len = 0;
    // No hard cap by default: the relay forwards arbitrary-size frames and
    // we want to receive whatever it sends. Callers that need a sanity cap
    // (e.g. parsing untrusted control frames) can still pass max_payload
    // explicitly; 0 means "accept up to the wire's natural uint32 limit."
    if (max_payload == 0) max_payload = UINT32_MAX;

    unsigned char hdr[8];
    size_t hdr_got = 0;
    uint32_t hdr_elapsed = 0;
    uint32_t hdr_idle = 0;
    int rc = ssl_read_exact(&c->ssl, hdr, 8, &hdr_got, &hdr_elapsed, &hdr_idle);
    if (rc < 0) {
        // Append how many header bytes we got so callers can distinguish
        // "conn closed cleanly between frames" (0/8) from "torn-down
        // partway through a header" (1..7/8). Timing helps tell a
        // timeout-induced close (large elapsed) from an instant peer kick.
        char base[192];
        strncpy(base, tls_last_error, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        snprintf(tls_last_error, sizeof(tls_last_error),
                 "%s [hdr_got=%zu/8 elapsed=%ums idle=%ums]",
                 base, hdr_got, (unsigned) hdr_elapsed, (unsigned) hdr_idle);
        log_message(LOG_ERROR, "[Relay] %s\n", tls_last_error);
        return rc;
    }
    if (rc == 0) return rc;
    uint32_t wire_type = get_be32(hdr);
    uint32_t length = get_be32(hdr + 4);
    bool compressed = (wire_type & RELAY_FRAME_FLAG_COMPRESSED) != 0;
    uint32_t type = wire_type & RELAY_FRAME_TYPE_MASK;
    if (length > max_payload) {
        snprintf(tls_last_error, sizeof(tls_last_error),
                 "frame oversize: type=%u(%s)%s len=%u cap=%u",
                 type, coop_msg_type_name(type), compressed ? "+zlib" : "",
                 length, max_payload);
        log_message(LOG_ERROR, "[Relay] %s\n", tls_last_error);
        return -1;
    }

    if (length > 0) {
        void *buf = malloc(length);
        if (!buf) {
            snprintf(tls_last_error, sizeof(tls_last_error),
                     "malloc failed for payload: type=%u(%s) len=%u",
                     type, coop_msg_type_name(type), length);
            log_message(LOG_ERROR, "[Relay] %s\n", tls_last_error);
            return -1;
        }
        // Header is fully consumed; mid-frame timeouts are fatal here (treated
        // as -1 by ssl_read_exact since len != total on the second call).
        size_t payload_got = 0;
        uint32_t pl_elapsed = 0;
        uint32_t pl_idle = 0;
        rc = ssl_read_exact(&c->ssl, buf, length, &payload_got, &pl_elapsed, &pl_idle);
        if (rc <= 0) {
            // elapsed=total time inside this payload read;
            // idle=time since the *last* successful byte before the close,
            // which separates "peer stopped feeding bytes then closed after
            // T ms" (large idle) from "closed instantly after a chunk"
            // (idle ~0). pct shows how far we got through the frame.
            unsigned pct = length > 0 ? (unsigned)((payload_got * 100ULL) / length) : 0;
            char base[192];
            strncpy(base, tls_last_error, sizeof(base) - 1);
            base[sizeof(base) - 1] = '\0';
            snprintf(tls_last_error, sizeof(tls_last_error),
                     "%s [type=%u(%s) len=%u got=%zu(%u%%) elapsed=%ums idle=%ums]",
                     base, type, coop_msg_type_name(type), length, payload_got,
                     pct, (unsigned) pl_elapsed, (unsigned) pl_idle);
            log_message(LOG_ERROR, "[Relay] %s\n", tls_last_error);
            free(buf);
            return -1;
        }
        if (compressed) {
            void *decomp = nullptr;
            uint32_t decomp_len = 0;
            if (!relay_decompress_payload(buf, length, &decomp, &decomp_len)) {
                snprintf(tls_last_error, sizeof(tls_last_error),
                         "decompress failed: type=%u(%s) wire_len=%u",
                         type, coop_msg_type_name(type), length);
                log_message(LOG_ERROR, "[Relay] %s\n", tls_last_error);
                free(buf);
                return -1;
            }
            free(buf);
            *out_payload = decomp;
            *out_len = decomp_len;
        } else {
            *out_payload = buf;
            *out_len = length;
        }
    }
    *out_type = type;
    return 1;
}

bool relay_recv_frame(RelayConn *c, uint32_t *out_type, void **out_payload,
                      uint32_t *out_len, uint32_t max_payload) {
    int rc = relay_recv_frame_timed(c, out_type, out_payload, out_len, max_payload);
    return rc == 1;
}

void relay_set_read_timeout(RelayConn *c, uint32_t ms) {
    if (!c) return;
    mbedtls_ssl_conf_read_timeout(&c->conf, ms);
}

// ---- Password hashing ----

bool relay_hash_password(const char *plaintext, char out[RELAY_PASSWORD_HASH_LEN + 1]) {
    if (!out) return false;
    if (!plaintext || plaintext[0] == '\0') {
        out[0] = '\0';
        return true;
    }
    psa_status_t st = psa_crypto_init();
    if (st != PSA_SUCCESS) return false;

    unsigned char digest[32];
    size_t digest_len = 0;
    st = psa_hash_compute(PSA_ALG_SHA_256,
                          (const uint8_t *) plaintext, strlen(plaintext),
                          digest, sizeof(digest), &digest_len);
    if (st != PSA_SUCCESS || digest_len != 32) return false;

    for (int i = 0; i < 32; i++) {
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    }
    out[RELAY_PASSWORD_HASH_LEN] = '\0';
    return true;
}

// ---- Encoders ----

char *relay_encode_create_room(const char *password_hash, const char *mc_version, size_t *out_len) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return nullptr;
    cJSON_AddStringToObject(obj, "password_hash", password_hash ? password_hash : "");
    cJSON_AddStringToObject(obj, "mc_version", mc_version ? mc_version : "");
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return nullptr;
    if (out_len) *out_len = strlen(json);
    return json;
}

char *relay_encode_join_room(const char *code, const char *password_hash, size_t *out_len) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return nullptr;
    cJSON_AddStringToObject(obj, "code", code ? code : "");
    cJSON_AddStringToObject(obj, "password_hash", password_hash ? password_hash : "");
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return nullptr;
    if (out_len) *out_len = strlen(json);
    return json;
}

// ---- Decoders ----

bool relay_decode_room_created(const char *payload, size_t len, char out_code[RELAY_ROOM_CODE_LEN + 1]) {
    if (!out_code) return false;
    out_code[0] = '\0';
    cJSON *obj = cJSON_ParseWithLength(payload, len);
    if (!obj) return false;
    cJSON *code = cJSON_GetObjectItem(obj, "code");
    bool ok = false;
    if (cJSON_IsString(code) && code->valuestring) {
        size_t clen = strlen(code->valuestring);
        if (clen > 0 && clen <= RELAY_ROOM_CODE_LEN) {
            memcpy(out_code, code->valuestring, clen);
            out_code[clen] = '\0';
            ok = true;
        }
    }
    cJSON_Delete(obj);
    return ok;
}

bool relay_decode_room_list_resp(const char *payload, size_t len, RelayRoomList *out) {
    if (!out) return false;
    out->count = 0;
    cJSON *obj = cJSON_ParseWithLength(payload, len);
    if (!obj) return false;
    cJSON *rooms = cJSON_GetObjectItem(obj, "rooms");
    // Empty / missing array is a valid (empty) response.
    if (cJSON_IsArray(rooms)) {
        cJSON *entry = nullptr;
        cJSON_ArrayForEach(entry, rooms) {
            if (out->count >= RELAY_MAX_ROOMS_LIST) break;
            if (!cJSON_IsObject(entry)) continue;
            cJSON *code = cJSON_GetObjectItem(entry, "code");
            cJSON *pc = cJSON_GetObjectItem(entry, "player_count");
            cJSON *mcv = cJSON_GetObjectItem(entry, "mc_version");
            if (!cJSON_IsString(code) || !code->valuestring) continue;
            size_t clen = strlen(code->valuestring);
            if (clen == 0 || clen > RELAY_ROOM_CODE_LEN) continue;

            RelayRoomEntry *e = &out->rooms[out->count];
            memset(e, 0, sizeof(*e));
            memcpy(e->code, code->valuestring, clen);
            e->code[clen] = '\0';
            e->player_count = cJSON_IsNumber(pc) ? pc->valueint : 0;
            if (cJSON_IsString(mcv) && mcv->valuestring) {
                strncpy(e->mc_version, mcv->valuestring, sizeof(e->mc_version) - 1);
                e->mc_version[sizeof(e->mc_version) - 1] = '\0';
            }
            out->count++;
        }
    }
    cJSON_Delete(obj);
    return true;
}

bool relay_smoke_test(void) {
    printf("[Relay] smoke-test: connecting to <relay>:%d ...\n", RELAY_PORT);
    fflush(stdout);
    char err[256] = {0};
    RelayConn *c = relay_connect(err, sizeof(err));
    if (!c) {
        fprintf(stderr, "[Relay] smoke-test FAILED at connect: %s\n", err);
        fflush(stderr);
        return false;
    }
    printf("[Relay] handshake OK, pinned fingerprint verified.\n");
    fflush(stdout);

    // Round-trip a LIST_ROOMS to exercise the frame layer + protocol decoders.
    if (!relay_send_frame(c, COOP_MSG_RELAY_LIST_ROOMS, nullptr, 0)) {
        fprintf(stderr, "[Relay] smoke-test FAILED: could not send LIST_ROOMS\n");
        relay_close(c);
        return false;
    }

    uint32_t type = 0, len = 0;
    void *payload = nullptr;
    if (!relay_recv_frame(c, &type, &payload, &len, 0)) {
        fprintf(stderr, "[Relay] smoke-test FAILED: no response to LIST_ROOMS\n");
        relay_close(c);
        return false;
    }
    if (type != COOP_MSG_RELAY_ROOM_LIST_RESP) {
        fprintf(stderr, "[Relay] smoke-test FAILED: expected ROOM_LIST_RESP (101), got %u\n", type);
        free(payload);
        relay_close(c);
        return false;
    }

    RelayRoomList list;
    bool decoded = relay_decode_room_list_resp((const char *) payload, len, &list);
    free(payload);
    if (!decoded) {
        fprintf(stderr, "[Relay] smoke-test FAILED: malformed ROOM_LIST_RESP\n");
        relay_close(c);
        return false;
    }

    printf("[Relay] smoke-test OK: %d active room(s).\n", list.count);
    for (int i = 0; i < list.count; i++) {
        printf("[Relay]   %s  players=%d  mc=%s\n",
               list.rooms[i].code, list.rooms[i].player_count, list.rooms[i].mc_version);
    }
    fflush(stdout);
    relay_close(c);
    return true;
}