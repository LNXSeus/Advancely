// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// Relay transport for co-op networking. TLS connection to the Advancely relay
// server with client-side SHA-256 cert pinning. Endpoint + pin live in the
// gitignored relay_config.h.

#ifndef COOP_NET_RELAY_H
#define COOP_NET_RELAY_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RelayConn RelayConn;

// Connect to RELAY_HOST:RELAY_PORT, do a TLS handshake, and verify that the
// peer's certificate SHA-256 matches RELAY_CERT_FINGERPRINT_SHA256. On any
// failure returns NULL and writes a human-readable diagnostic into out_err.
RelayConn *relay_connect(char *out_err, size_t err_size);

void relay_close(RelayConn *conn);

// One-shot diagnostic: connect, verify, disconnect. Prints to stdout/stderr
// (intentionally not log_message so it works before log_init). Returns true
// on success.
bool relay_smoke_test(void);

#ifdef __cplusplus
}
#endif

#endif // COOP_NET_RELAY_H