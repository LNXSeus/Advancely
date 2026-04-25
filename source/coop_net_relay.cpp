// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// TLS client to the Advancely relay server with SHA-256 cert pinning.
// CA chain validation is intentionally skipped because the relay uses a
// self-signed cert; trust is established by comparing the peer cert's
// SHA-256 against the constant baked into relay_config.h at build time.

#include "coop_net_relay.h"
#include "relay_config.h"

#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>

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

    mbedtls_ssl_set_bio(&c->ssl, &c->net, mbedtls_net_send, mbedtls_net_recv, nullptr);

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

bool relay_smoke_test(void) {
    printf("[Relay] smoke-test: connecting to <relay>:%d ...\n", RELAY_PORT);
    fflush(stdout);
    char err[256] = {0};
    RelayConn *c = relay_connect(err, sizeof(err));
    if (!c) {
        fprintf(stderr, "[Relay] smoke-test FAILED: %s\n", err);
        fflush(stderr);
        return false;
    }
    printf("[Relay] smoke-test OK: TLS handshake complete, pinned fingerprint verified.\n");
    fflush(stdout);
    relay_close(c);
    return true;
}