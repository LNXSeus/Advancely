// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// Relay transport for co-op networking. TLS connection to the Advancely relay
// server with client-side SHA-256 cert pinning. Endpoint + pin live in the
// gitignored relay_config.h.

#ifndef COOP_NET_RELAY_H
#define COOP_NET_RELAY_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Relay control protocol ----
// Wire format identical to COOP_MSG_* (see coop_net.h):
//   [4B type BE] [4B length BE] [payload]
// Types 100+ are control frames; types 1-13 (the existing COOP_MSG_*)
// are forwarded transparently by the relay between host and receivers.
enum {
    COOP_MSG_RELAY_LIST_ROOMS       = 100, // client -> relay; empty payload
    COOP_MSG_RELAY_ROOM_LIST_RESP   = 101, // relay -> client; JSON {rooms:[...]}
    COOP_MSG_RELAY_CREATE_ROOM      = 102, // client -> relay; JSON {password_hash, mc_version}
    COOP_MSG_RELAY_ROOM_CREATED     = 103, // relay -> client; JSON {code}
    COOP_MSG_RELAY_JOIN_ROOM        = 104, // client -> relay; JSON {code, password_hash}
    COOP_MSG_RELAY_JOIN_ROOM_OK     = 105, // relay -> client; empty
    COOP_MSG_RELAY_JOIN_ROOM_DENIED = 106, // relay -> client; UTF-8 reason string
    COOP_MSG_RELAY_ROOM_CLOSED      = 107  // relay -> client; empty
};

#define RELAY_ROOM_CODE_LEN     6
#define RELAY_PASSWORD_HASH_LEN 64  // SHA-256 hex
#define RELAY_MAX_ROOMS_LIST    256

typedef struct {
    char code[RELAY_ROOM_CODE_LEN + 1];
    int  player_count;
    char mc_version[32];
} RelayRoomEntry;

typedef struct {
    int count;
    RelayRoomEntry rooms[RELAY_MAX_ROOMS_LIST];
} RelayRoomList;

// Hash the given plaintext password into a 64-char SHA-256 hex string (lowercase),
// null-terminated. out must be at least RELAY_PASSWORD_HASH_LEN + 1 bytes.
// An empty / NULL password produces an empty string (allowed by the relay as
// "no password" — the relay just compares password_hash strings byte-for-byte).
bool relay_hash_password(const char *plaintext, char out[RELAY_PASSWORD_HASH_LEN + 1]);

// Encoders return a heap-allocated buffer (caller free()s) and write the byte
// length into *out_len. Return NULL on allocation failure.
char *relay_encode_create_room(const char *password_hash, const char *mc_version, size_t *out_len);
char *relay_encode_join_room  (const char *code,          const char *password_hash, size_t *out_len);

// Decoders parse a payload (NOT null-terminated; len bytes). Return false on
// malformed input. Output buffers/structs are written only on success.
bool relay_decode_room_created  (const char *payload, size_t len, char out_code[RELAY_ROOM_CODE_LEN + 1]);
bool relay_decode_room_list_resp(const char *payload, size_t len, RelayRoomList *out);

typedef struct RelayConn RelayConn;

// Connect to RELAY_HOST:RELAY_PORT, do a TLS handshake, and verify that the
// peer's certificate SHA-256 matches RELAY_CERT_FINGERPRINT_SHA256. On any
// failure returns NULL and writes a human-readable diagnostic into out_err.
RelayConn *relay_connect(char *out_err, size_t err_size);

void relay_close(RelayConn *conn);

// Send a framed message over the TLS connection. Same wire format as
// coop_net.cpp's send_message: [type BE 4B][length BE 4B][payload]. Retries
// MBEDTLS_ERR_SSL_WANT_READ/WANT_WRITE internally. Returns false on
// connection error.
bool relay_send_frame(RelayConn *c, uint32_t type, const void *payload, uint32_t payload_len);

// Receive one framed message. On success allocates *out_payload via malloc
// (caller free()s; *out_payload is NULL when *out_len == 0). Returns false on
// connection close or protocol error. Reject frames larger than max_payload
// to avoid memory blowups; pass 0 for the default cap.
bool relay_recv_frame(RelayConn *c, uint32_t *out_type, void **out_payload,
                      uint32_t *out_len, uint32_t max_payload);

// One-shot diagnostic: connect, verify, disconnect. Prints to stdout/stderr
// (intentionally not log_message so it works before log_init). Returns true
// on success.
bool relay_smoke_test(void);

#ifdef __cplusplus
}
#endif

#endif // COOP_NET_RELAY_H