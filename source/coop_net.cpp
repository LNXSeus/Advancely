// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 07.04.2026.
//

// ---- Cross-platform socket includes (must come before anything that pulls in windows.h) ----
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define COOP_CLOSE_SOCKET closesocket
static bool wsa_initialized = false;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#define COOP_CLOSE_SOCKET close
#define SOCKET_ERROR (-1)
#endif

#include "coop_net.h"
#include "main.h"
#include "logger.h"

#include <cJSON.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>

// ---- Global pointer ----
CoopNetContext *g_coop_ctx = nullptr;

// ---- Internal helpers ----

static void set_status(CoopNetContext *ctx, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    SDL_LockMutex(ctx->status_mutex);
    vsnprintf(ctx->status_msg, sizeof(ctx->status_msg), fmt, args);
    SDL_UnlockMutex(ctx->status_mutex);
    va_end(args);
}

static void set_state(CoopNetContext *ctx, CoopNetState s) {
    SDL_SetAtomicInt(&ctx->state, s);
}

static bool should_stop(CoopNetContext *ctx) {
    return SDL_GetAtomicInt(&ctx->should_stop) != 0;
}

// Set a socket to non-blocking mode
static bool set_nonblocking(coop_socket_t fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

// Enable TCP_NODELAY (disable Nagle's algorithm) for lower latency
static void set_tcp_nodelay(coop_socket_t fd) {
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *) &flag, sizeof(flag));
}

// Send exactly `len` bytes. Returns true on success, false on error/disconnect.
// Handles EWOULDBLOCK on non-blocking sockets by waiting up to 5 seconds for the socket
// to become writable. This prevents disconnects when sending large payloads that fill
// the TCP send buffer (e.g., broadcasting full template state after granting all advancements).
static bool send_all(coop_socket_t fd, const void *data, size_t len) {
    const char *ptr = (const char *) data;
    size_t remaining = len;
    while (remaining > 0) {
        int sent = send(fd, ptr, (int) remaining, 0);
        if (sent > 0) {
            ptr += sent;
            remaining -= (size_t) sent;
            continue;
        }
        if (sent == 0) return false; // Connection closed

        // sent < 0: check if it's a recoverable WOULDBLOCK
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) return false;
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK) return false;
#endif
        // Wait for the socket to become writable (up to 5 seconds)
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(fd, &write_fds);
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        int ready = select((int) fd + 1, nullptr, &write_fds, nullptr, &tv);
        if (ready <= 0) return false; // Timeout or error
    }
    return true;
}

// Send a framed message: [type(4)] [length(4)] [payload(N)]
static bool send_message(coop_socket_t fd, uint32_t type, const void *payload, uint32_t payload_len) {
    uint32_t header[2];
    header[0] = htonl(type);
    header[1] = htonl(payload_len);
    if (!send_all(fd, header, sizeof(header))) return false;
    if (payload_len > 0 && payload) {
        if (!send_all(fd, payload, payload_len)) return false;
    }
    return true;
}

// Try to receive exactly `len` bytes (non-blocking friendly).
// Returns: >0 = bytes read (may be < len), 0 = connection closed, -1 = would block, -2 = error
static int recv_exact(coop_socket_t fd, void *buf, size_t len) {
    int result = recv(fd, (char *) buf, (int) len, 0);
    if (result > 0) return result;
    if (result == 0) return 0; // Connection closed
#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) return -1;
    log_message(LOG_ERROR, "[COOP NET] recv() error: WSA %d\n", err);
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    log_message(LOG_ERROR, "[COOP NET] recv() error: errno %d (%s)\n", errno, strerror(errno));
#endif
    return -2; // Real error
}

// Read a complete framed message from a socket.
// Returns true if a message was read. Sets out_type, out_payload (caller must free), out_payload_len.
// Returns false if no complete message available yet or error.
// Sets *disconnected = true if the remote side closed the connection.
static bool read_message(coop_socket_t fd, uint32_t *out_type, char **out_payload, uint32_t *out_payload_len,
                         bool *disconnected) {
    *disconnected = false;
    *out_payload = nullptr;
    *out_payload_len = 0;

    // Read header
    uint32_t header[2];
    size_t header_read = 0;
    while (header_read < sizeof(header)) {
        int r = recv_exact(fd, (char *) header + header_read, sizeof(header) - header_read);
        if (r == 0) {
            *disconnected = true;
            return false;
        }
        if (r == -1) return false; // Would block, no data yet
        if (r == -2) {
            *disconnected = true;
            return false;
        }
        header_read += (size_t) r;
    }

    *out_type = ntohl(header[0]);
    *out_payload_len = ntohl(header[1]);

    // Sanity check payload size (16 MB max)
    if (*out_payload_len > 16 * 1024 * 1024) {
        *disconnected = true;
        return false;
    }

    if (*out_payload_len > 0) {
        *out_payload = (char *) malloc(*out_payload_len);
        if (!*out_payload) {
            *disconnected = true;
            return false;
        }

        size_t payload_read = 0;
        while (payload_read < *out_payload_len) {
            int r = recv_exact(fd, *out_payload + payload_read, *out_payload_len - payload_read);
            if (r == 0) {
                // Connection closed
                free(*out_payload);
                *out_payload = nullptr;
                *disconnected = true;
                return false;
            }
            if (r == -1) {
                // Would block - header consumed but payload incomplete
                // Spin-wait briefly for remaining payload (it must arrive since header was valid)
                continue;
            }
            if (r == -2) {
                // Real error
                free(*out_payload);
                *out_payload = nullptr;
                *disconnected = true;
                return false;
            }
            payload_read += (size_t) r;
        }
    }

    return true;
}

// Translate a socket error code into a user-facing explanation
static const char *socket_error_hint(int err) {
#ifdef _WIN32
    switch (err) {
        case 10060: /* WSAETIMEDOUT */ return "Connection timed out. The host's firewall may be blocking the port.";
        case 10061: /* WSAECONNREFUSED */ return
                    "Connection refused. The host may not be listening, or the IP/port is wrong.";
        case 10065: /* WSAEHOSTUNREACH */ return "Host unreachable. Check your network/VPN connection.";
        case 10051: /* WSAENETUNREACH */ return "Network unreachable. Check your network/VPN connection.";
        case 10064: /* WSAEHOSTDOWN */ return "Host is down. Check that the host device is online.";
        case 10048: /* WSAEADDRINUSE */ return "Port already in use. Another application may be using it.";
        case 10049: /* WSAEADDRNOTAVAIL */return "IP address not available on this machine. Check the IP.";
        default: return nullptr;
    }
#else
    switch (err) {
        case ETIMEDOUT: return "Connection timed out. The host's firewall may be blocking the port.";
        case ECONNREFUSED: return "Connection refused. The host may not be listening, or the IP/port is wrong.";
        case EHOSTUNREACH: return "Host unreachable. Check your network/VPN connection.";
        case ENETUNREACH: return "Network unreachable. Check your network/VPN connection.";
        case EADDRINUSE: return "Port already in use. Another application may be using it.";
        case EADDRNOTAVAIL: return "IP address not available on this machine. Check the IP.";
        default: return nullptr;
    }
#endif
}

// Set the status to a user-friendly connection error message
static void set_connect_error_status(CoopNetContext *ctx, int err) {
    const char *hint = socket_error_hint(err);
    if (hint) {
        set_status(ctx, "%s", hint);
    } else {
        set_status(ctx, "Connection failed (error %d).", err);
    }
}

// ---- Base64 Encode/Decode ----

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const uint8_t *data, size_t len, char *out, size_t out_max) {
    size_t out_len = 4 * ((len + 2) / 3);
    if (out_len + 1 > out_max) return 0;

    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t a = data[i];
        uint32_t b = (i + 1 < len) ? data[i + 1] : 0;
        uint32_t c = (i + 2 < len) ? data[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? b64_table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? b64_table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    return j;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t base64_decode(const char *encoded, uint8_t *out, size_t out_max) {
    size_t in_len = strlen(encoded);
    if (in_len % 4 != 0) return 0;

    size_t out_len = (in_len / 4) * 3;
    if (encoded[in_len - 1] == '=') out_len--;
    if (in_len >= 2 && encoded[in_len - 2] == '=') out_len--;
    if (out_len > out_max) return 0;

    size_t j = 0;
    for (size_t i = 0; i < in_len; i += 4) {
        int a = b64_decode_char(encoded[i]);
        int b = b64_decode_char(encoded[i + 1]);
        int c = (encoded[i + 2] == '=') ? 0 : b64_decode_char(encoded[i + 2]);
        int d = (encoded[i + 3] == '=') ? 0 : b64_decode_char(encoded[i + 3]);

        if (a < 0 || b < 0 || c < 0 || d < 0) return 0; // Invalid character

        uint32_t triple = ((uint32_t) a << 18) | ((uint32_t) b << 12) | ((uint32_t) c << 6) | (uint32_t) d;
        if (j < out_len) out[j++] = (triple >> 16) & 0xFF;
        if (j < out_len) out[j++] = (triple >> 8) & 0xFF;
        if (j < out_len) out[j++] = triple & 0xFF;
    }
    return j;
}

// ---- Room Code: Base64("IP:PORT") ----

bool coop_encode_room_code(const char *ip, int port, char *out_code, size_t code_max_len) {
    char plain[128];
    int n = snprintf(plain, sizeof(plain), "%s:%d", ip, port);
    if (n <= 0 || (size_t) n >= sizeof(plain)) return false;
    return base64_encode((const uint8_t *) plain, (size_t) n, out_code, code_max_len) > 0;
}

bool coop_decode_room_code(const char *code, char *out_ip, size_t ip_max_len, int *out_port) {
    if (!code || code[0] == '\0') return false;

    uint8_t decoded[128];
    size_t decoded_len = base64_decode(code, decoded, sizeof(decoded) - 1);
    if (decoded_len == 0) return false;
    decoded[decoded_len] = '\0';

    // Find the colon separator
    const char *colon = strchr((const char *) decoded, ':');
    if (!colon) return false;

    size_t ip_len = (size_t) (colon - (const char *) decoded);
    if (ip_len == 0 || ip_len >= ip_max_len) return false;

    memcpy(out_ip, decoded, ip_len);
    out_ip[ip_len] = '\0';

    int port_val = atoi(colon + 1);
    if (port_val < 1 || port_val > 65535) return false;
    *out_port = port_val;

    return true;
}

// Close a socket if it's valid, then set to INVALID_SOCKET
static void close_socket(coop_socket_t *fd) {
    if (*fd != COOP_INVALID_SOCKET) {
        COOP_CLOSE_SOCKET(*fd);
        *fd = COOP_INVALID_SOCKET;
    }
}

// Graceful close: shutdown send side first so any pending data is flushed before close.
// On Windows, closesocket() after send() without shutdown() can send RST, losing the data.
static void graceful_close_socket(coop_socket_t *fd) {
    if (*fd != COOP_INVALID_SOCKET) {
#ifdef _WIN32
        shutdown(*fd, SD_SEND);
#else
        shutdown(*fd, SHUT_WR);
#endif
        // Brief drain: read any remaining data until close or timeout
        char drain[64];
        for (int attempt = 0; attempt < 10; attempt++) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(*fd, &rfds);
            struct timeval tv = {0, 10000}; // 10ms
            if (select((int) (*fd + 1), &rfds, nullptr, nullptr, &tv) <= 0) break;
            int r = recv(*fd, drain, sizeof(drain), 0);
            if (r <= 0) break;
        }
        COOP_CLOSE_SOCKET(*fd);
        *fd = COOP_INVALID_SOCKET;
    }
}

// ---- Lobby Helpers ----

// Rebuild the lobby player list from the host's perspective. Must NOT hold lobby_mutex when calling.
static void rebuild_lobby_list(CoopNetContext *ctx) {
    SDL_LockMutex(ctx->lobby_mutex);
    ctx->lobby_player_count = 0;

    // Host is always first
    CoopLobbyPlayer *host_entry = &ctx->lobby_players[ctx->lobby_player_count++];
    memset(host_entry, 0, sizeof(CoopLobbyPlayer));
    strncpy(host_entry->username, ctx->host_username, sizeof(host_entry->username) - 1);
    strncpy(host_entry->uuid, ctx->host_uuid, sizeof(host_entry->uuid) - 1);
    strncpy(host_entry->display_name, ctx->host_display_name, sizeof(host_entry->display_name) - 1);
    host_entry->is_host = true;

    // Add each approved client
    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        if (!ctx->clients[i].active || !ctx->clients[i].handshake_done) continue;
        if (ctx->lobby_player_count >= COOP_MAX_LOBBY) break;
        CoopLobbyPlayer *entry = &ctx->lobby_players[ctx->lobby_player_count++];
        memset(entry, 0, sizeof(CoopLobbyPlayer));
        strncpy(entry->username, ctx->clients[i].username, sizeof(entry->username) - 1);
        strncpy(entry->uuid, ctx->clients[i].uuid, sizeof(entry->uuid) - 1);
        strncpy(entry->display_name, ctx->clients[i].display_name, sizeof(entry->display_name) - 1);
        entry->is_host = false;
    }

    ctx->lobby_changed = true;
    SDL_UnlockMutex(ctx->lobby_mutex);
}

// Build a JSON string of the lobby player list for sending over the network.
// Caller must free the returned string.
static char *build_lobby_json(CoopNetContext *ctx) {
    cJSON *arr = cJSON_CreateArray();

    SDL_LockMutex(ctx->lobby_mutex);
    for (int i = 0; i < ctx->lobby_player_count; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "username", ctx->lobby_players[i].username);
        cJSON_AddStringToObject(obj, "uuid", ctx->lobby_players[i].uuid);
        cJSON_AddStringToObject(obj, "display_name", ctx->lobby_players[i].display_name);
        cJSON_AddBoolToObject(obj, "is_host", ctx->lobby_players[i].is_host);
        cJSON_AddItemToArray(arr, obj);
    }
    SDL_UnlockMutex(ctx->lobby_mutex);

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return json_str;
}

// Parse a JSON player list string into the lobby_players array (receiver side).
static void parse_lobby_json(CoopNetContext *ctx, const char *json_str) {
    cJSON *arr = cJSON_Parse(json_str);
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return;
    }

    SDL_LockMutex(ctx->lobby_mutex);
    ctx->lobby_player_count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (ctx->lobby_player_count >= COOP_MAX_LOBBY) break;
        CoopLobbyPlayer *entry = &ctx->lobby_players[ctx->lobby_player_count++];
        memset(entry, 0, sizeof(CoopLobbyPlayer));

        cJSON *u = cJSON_GetObjectItem(item, "username");
        if (u && cJSON_IsString(u)) strncpy(entry->username, u->valuestring, sizeof(entry->username) - 1);
        cJSON *id = cJSON_GetObjectItem(item, "uuid");
        if (id && cJSON_IsString(id)) strncpy(entry->uuid, id->valuestring, sizeof(entry->uuid) - 1);
        cJSON *d = cJSON_GetObjectItem(item, "display_name");
        if (d && cJSON_IsString(d)) strncpy(entry->display_name, d->valuestring, sizeof(entry->display_name) - 1);
        cJSON *h = cJSON_GetObjectItem(item, "is_host");
        entry->is_host = (h && cJSON_IsTrue(h));
    }
    ctx->lobby_changed = true;
    SDL_UnlockMutex(ctx->lobby_mutex);

    cJSON_Delete(arr);
}

// Broadcast the lobby player list to all approved clients.
static void broadcast_player_list(CoopNetContext *ctx) {
    char *json = build_lobby_json(ctx);
    if (!json) return;

    uint32_t len = (uint32_t) strlen(json);
    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        if (!ctx->clients[i].active || !ctx->clients[i].handshake_done) continue;
        send_message(ctx->clients[i].socket_fd, COOP_MSG_PLAYER_LIST, json, len);
    }
    free(json);
}

// Remove a pending request by client slot.
static void remove_pending_request(CoopNetContext *ctx, int client_slot) {
    SDL_LockMutex(ctx->lobby_mutex);
    for (int i = 0; i < ctx->pending_request_count; i++) {
        if (ctx->pending_requests[i].client_slot == client_slot) {
            // Shift remaining requests
            for (int j = i; j < ctx->pending_request_count - 1; j++) {
                ctx->pending_requests[j] = ctx->pending_requests[j + 1];
            }
            ctx->pending_request_count--;
            ctx->pending_requests_changed = true;
            break;
        }
    }
    SDL_UnlockMutex(ctx->lobby_mutex);
}

// ---- Host Thread ----

static int SDLCALL host_thread_func(void *data) {
    CoopNetContext *ctx = (CoopNetContext *) data;

    // Heartbeat tracking
    Uint32 last_heartbeat = SDL_GetTicks();
    const Uint32 HEARTBEAT_INTERVAL_MS = 5000;
    const Uint32 HEARTBEAT_TIMEOUT_MS = 15000;
    const Uint32 HANDSHAKE_TIMEOUT_MS = 10000; // Kick if no JOIN_REQUEST in 10s

    Uint32 client_last_ack[COOP_MAX_CLIENTS];
    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        client_last_ack[i] = SDL_GetTicks();
    }

    // Build initial lobby list (host only)
    rebuild_lobby_list(ctx);

    log_message(LOG_INFO, "[COOP NET] Host thread started. Listening for connections.\n");

    while (!should_stop(ctx)) {
        // Process any pending kick/reject actions queued by the UI thread
        {
            bool lobby_dirty = false;
            for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
                if (!ctx->clients[i].active) continue;
                int action = SDL_GetAtomicInt(&ctx->clients[i].pending_action);
                if (action == COOP_ACTION_NONE) continue;
                SDL_SetAtomicInt(&ctx->clients[i].pending_action, COOP_ACTION_NONE);

                if (action == COOP_ACTION_KICK) {
                    send_message(ctx->clients[i].socket_fd, COOP_MSG_KICK,
                                 ctx->clients[i].pending_action_reason,
                                 (uint32_t) strlen(ctx->clients[i].pending_action_reason));
                    log_message(LOG_INFO, "[COOP NET] Kicked %s (%s): %s\n",
                                ctx->clients[i].username, ctx->clients[i].label,
                                ctx->clients[i].pending_action_reason);
                    set_status(ctx, "Kicked %s", ctx->clients[i].username);
                    graceful_close_socket(&ctx->clients[i].socket_fd);
                    ctx->clients[i].active = false;
                    ctx->client_count--;
                    lobby_dirty = true;
                } else if (action == COOP_ACTION_REJECT) {
                    send_message(ctx->clients[i].socket_fd, COOP_MSG_JOIN_REJECT,
                                 ctx->clients[i].pending_action_reason,
                                 (uint32_t) strlen(ctx->clients[i].pending_action_reason));
                    log_message(LOG_INFO, "[COOP NET] Rejected join request from %s (%s): %s\n",
                                ctx->clients[i].username, ctx->clients[i].label,
                                ctx->clients[i].pending_action_reason);
                    graceful_close_socket(&ctx->clients[i].socket_fd);
                    ctx->clients[i].active = false;
                    remove_pending_request(ctx, i);
                }
            }
            if (lobby_dirty) {
                rebuild_lobby_list(ctx);
                broadcast_player_list(ctx);
            }
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);

        // Build the fd_set, skipping invalid fds. On Linux FD_SET(-1, ...) is
        // undefined behavior (writes via negative index into fds_bits, smashing
        // the stack). The main thread sets server_fd to COOP_INVALID_SOCKET
        // during coop_net_stop, so we MUST snapshot once and validate before
        // inserting, then use the snapshot for FD_ISSET / accept below.
        coop_socket_t server_fd_snap = ctx->server_fd;
        coop_socket_t max_fd = 0;
        bool any_fd_valid = false;

        if (server_fd_snap != COOP_INVALID_SOCKET) {
            FD_SET(server_fd_snap, &read_fds);
            max_fd = server_fd_snap;
            any_fd_valid = true;
        }
        for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
            if (ctx->clients[i].active &&
                ctx->clients[i].socket_fd != COOP_INVALID_SOCKET) {
                FD_SET(ctx->clients[i].socket_fd, &read_fds);
                if (!any_fd_valid || ctx->clients[i].socket_fd > max_fd) {
                    max_fd = ctx->clients[i].socket_fd;
                }
                any_fd_valid = true;
            }
        }

        if (!any_fd_valid) {
            // All sockets have been closed by the main thread during shutdown.
            // Spin-sleep briefly so the while-loop condition re-checks should_stop.
            SDL_Delay(10);
            continue;
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms

        int ready = select((int) (max_fd + 1), &read_fds, nullptr, nullptr, &tv);
        if (ready < 0) {
#ifdef _WIN32
            int sel_err = WSAGetLastError();
            if (sel_err == WSAEINTR) continue;
            if (sel_err == WSAENOTSOCK) {
                log_message(LOG_ERROR, "[COOP NET] Host select() got WSAENOTSOCK, rebuilding fd_set.\n");
                continue;
            }
#else
            if (errno == EINTR) continue;
            if (errno == EBADF) {
                log_message(LOG_ERROR, "[COOP NET] Host select() got EBADF, rebuilding fd_set.\n");
                continue;
            }
#endif
            log_message(LOG_ERROR, "[COOP NET] Host select() error.\n");
            break;
        }

        // Accept new connections (not yet handshaked). Use the snapshot so
        // FD_ISSET never sees a just-closed -1 (UB on Linux).
        if (ready > 0 && server_fd_snap != COOP_INVALID_SOCKET &&
            FD_ISSET(server_fd_snap, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            coop_socket_t new_fd = accept(server_fd_snap, (struct sockaddr *) &client_addr, &addr_len);

            if (new_fd != COOP_INVALID_SOCKET) {
                set_nonblocking(new_fd);
                set_tcp_nodelay(new_fd);

                int slot = -1;
                for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
                    if (!ctx->clients[i].active) {
                        slot = i;
                        break;
                    }
                }

                if (slot >= 0) {
                    memset(&ctx->clients[slot], 0, sizeof(CoopClient));
                    ctx->clients[slot].socket_fd = new_fd;
                    ctx->clients[slot].active = true;
                    ctx->clients[slot].handshake_done = false;
                    ctx->clients[slot].pending_approval = false;
                    ctx->clients[slot].connect_time = SDL_GetTicks();
                    snprintf(ctx->clients[slot].label, sizeof(ctx->clients[slot].label),
                             "%s:%d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    client_last_ack[slot] = SDL_GetTicks();

                    log_message(LOG_INFO, "[COOP NET] TCP connection from %s (slot %d, awaiting handshake)\n",
                                ctx->clients[slot].label, slot);
                } else {
                    const char *reason = "Server full";
                    send_message(new_fd, COOP_MSG_JOIN_REJECT, reason, (uint32_t) strlen(reason));
#ifdef _WIN32
                    shutdown(new_fd, SD_SEND);
#else
                    shutdown(new_fd, SHUT_WR);
#endif
                    COOP_CLOSE_SOCKET(new_fd);
                    log_message(LOG_INFO, "[COOP NET] Rejected connection (server full).\n");
                }
            }
        }

        bool lobby_dirty = false;

        // Read from connected clients
        for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
            if (!ctx->clients[i].active) continue;
            if (ready > 0 && FD_ISSET(ctx->clients[i].socket_fd, &read_fds)) {
                uint32_t msg_type;
                char *payload;
                uint32_t payload_len;
                bool disconnected;

                if (read_message(ctx->clients[i].socket_fd, &msg_type, &payload, &payload_len, &disconnected)) {
                    if (msg_type == COOP_MSG_JOIN_REQUEST && !ctx->clients[i].handshake_done) {
                        // Parse identity from JSON payload
                        char *json_str = (char *) malloc(payload_len + 1);
                        if (json_str) {
                            memcpy(json_str, payload, payload_len);
                            json_str[payload_len] = '\0';

                            cJSON *json = cJSON_Parse(json_str);
                            free(json_str);

                            bool valid = false;
                            bool version_mismatch = false;
                            char req_uuid[48] = {0}, req_username[64] = {0}, req_display[64] = {0};

                            if (json) {
                                // Check version first
                                cJSON *v = cJSON_GetObjectItem(json, "version");
                                if (!v || !cJSON_IsString(v) || strcmp(v->valuestring, ADVANCELY_VERSION) != 0) {
                                    version_mismatch = true;
                                    char reason[256];
                                    snprintf(reason, sizeof(reason),
                                             "Version mismatch: host is %s, you are %s",
                                             ADVANCELY_VERSION,
                                             (v && cJSON_IsString(v)) ? v->valuestring : "unknown");
                                    send_message(ctx->clients[i].socket_fd, COOP_MSG_JOIN_REJECT,
                                                 reason, (uint32_t) strlen(reason));
                                    graceful_close_socket(&ctx->clients[i].socket_fd);
                                    ctx->clients[i].active = false;
                                    log_message(
                                        LOG_INFO, "[COOP NET] Rejected %s: version mismatch (theirs: %s, ours: %s).\n",
                                        ctx->clients[i].label,
                                        (v && cJSON_IsString(v)) ? v->valuestring : "unknown",
                                        ADVANCELY_VERSION);
                                }

                                if (!version_mismatch) {
                                    cJSON *u = cJSON_GetObjectItem(json, "uuid");
                                    cJSON *n = cJSON_GetObjectItem(json, "username");
                                    cJSON *d = cJSON_GetObjectItem(json, "display_name");
                                    if (u && cJSON_IsString(u) && n && cJSON_IsString(n)) {
                                        strncpy(req_uuid, u->valuestring, sizeof(req_uuid) - 1);
                                        strncpy(req_username, n->valuestring, sizeof(req_username) - 1);
                                        if (d && cJSON_IsString(d))
                                            strncpy(req_display, d->valuestring, sizeof(req_display) - 1);
                                        valid = true;
                                    }
                                }
                                cJSON_Delete(json);
                            }

                            if (version_mismatch) {
                                // Already handled above
                            } else if (!valid || req_uuid[0] == '\0') {
                                const char *reason = "Invalid handshake data";
                                send_message(ctx->clients[i].socket_fd, COOP_MSG_JOIN_REJECT,
                                             reason, (uint32_t) strlen(reason));
                                graceful_close_socket(&ctx->clients[i].socket_fd);
                                ctx->clients[i].active = false;
                                log_message(LOG_INFO, "[COOP NET] Rejected %s: invalid handshake.\n",
                                            ctx->clients[i].label);
                            } else {
                                // Check for duplicate UUID
                                bool duplicate_uuid = (strcmp(req_uuid, ctx->host_uuid) == 0);
                                if (!duplicate_uuid) {
                                    for (int j = 0; j < COOP_MAX_CLIENTS; j++) {
                                        if (j == i || !ctx->clients[j].active) continue;
                                        if (ctx->clients[j].uuid[0] != '\0' &&
                                            strcmp(ctx->clients[j].uuid, req_uuid) == 0) {
                                            duplicate_uuid = true;
                                            break;
                                        }
                                    }
                                }

                                // Check for duplicate username (case-insensitive). UUID remains the
                                // primary identity, but unique usernames are required so per-player
                                // snapshot files and UI labels don't collide.
                                bool duplicate_username = (strcasecmp(req_username, ctx->host_username) == 0);
                                if (!duplicate_uuid && !duplicate_username) {
                                    for (int j = 0; j < COOP_MAX_CLIENTS; j++) {
                                        if (j == i || !ctx->clients[j].active) continue;
                                        if (ctx->clients[j].username[0] != '\0' &&
                                            strcasecmp(ctx->clients[j].username, req_username) == 0) {
                                            duplicate_username = true;
                                            break;
                                        }
                                    }
                                }

                                if (duplicate_uuid) {
                                    const char *reason = "A player with this UUID is already in the lobby";
                                    send_message(ctx->clients[i].socket_fd, COOP_MSG_JOIN_REJECT,
                                                 reason, (uint32_t) strlen(reason));
                                    graceful_close_socket(&ctx->clients[i].socket_fd);
                                    ctx->clients[i].active = false;
                                    log_message(LOG_INFO, "[COOP NET] Rejected %s: duplicate UUID %s.\n",
                                                ctx->clients[i].label, req_uuid);
                                } else if (duplicate_username) {
                                    const char *reason = "Username already in lobby";
                                    send_message(ctx->clients[i].socket_fd, COOP_MSG_JOIN_REJECT,
                                                 reason, (uint32_t) strlen(reason));
                                    graceful_close_socket(&ctx->clients[i].socket_fd);
                                    ctx->clients[i].active = false;
                                    log_message(LOG_INFO, "[COOP NET] Rejected %s: duplicate username '%s'.\n",
                                                ctx->clients[i].label, req_username);
                                } else {
                                    // Store identity, add to pending approval queue
                                    strncpy(ctx->clients[i].username, req_username,
                                            sizeof(ctx->clients[i].username) - 1);
                                    strncpy(ctx->clients[i].uuid, req_uuid, sizeof(ctx->clients[i].uuid) - 1);
                                    strncpy(ctx->clients[i].display_name, req_display,
                                            sizeof(ctx->clients[i].display_name) - 1);
                                    ctx->clients[i].pending_approval = true;

                                    // Add to pending requests for UI
                                    SDL_LockMutex(ctx->lobby_mutex);
                                    if (ctx->pending_request_count < COOP_MAX_CLIENTS) {
                                        CoopJoinRequest *req = &ctx->pending_requests[ctx->pending_request_count++];
                                        req->client_slot = i;
                                        strncpy(req->username, req_username, sizeof(req->username) - 1);
                                        strncpy(req->uuid, req_uuid, sizeof(req->uuid) - 1);
                                        strncpy(req->display_name, req_display, sizeof(req->display_name) - 1);
                                        ctx->pending_requests_changed = true;
                                    }
                                    SDL_UnlockMutex(ctx->lobby_mutex);

                                    log_message(
                                        LOG_INFO,
                                        "[COOP NET] Join request from %s (%s, UUID: %s). Waiting for host approval.\n",
                                        ctx->clients[i].label, req_username, req_uuid);
                                    set_status(ctx, "Join request from %s", req_username);
                                }
                            }
                        }
                    } else if (msg_type == COOP_MSG_HEARTBEAT_ACK) {
                        client_last_ack[i] = SDL_GetTicks();
                    } else if (msg_type == COOP_MSG_CUSTOM_GOAL_MOD) {
                        // Queue the modification for the main thread to process
                        if (payload_len == sizeof(CoopCustomGoalModMsg) && ctx->clients[i].handshake_done) {
                            SDL_LockMutex(ctx->custom_mod_mutex);
                            if (ctx->custom_mod_count < COOP_MAX_CUSTOM_MODS) {
                                memcpy(&ctx->custom_mod_queue[ctx->custom_mod_count],
                                       payload, sizeof(CoopCustomGoalModMsg));
                                ctx->custom_mod_count++;
                            }
                            SDL_UnlockMutex(ctx->custom_mod_mutex);
                        }
                    } else if (msg_type == COOP_MSG_LEGACY_STATS_UPLOAD) {
                        // LEGACY (<=1.6.4) ONLY. Receiver shipped their raw global stats
                        // .dat so the host can merge everyone. Parse the framed payload
                        // and stash the latest per-UUID blob for the main thread.
                        if (ctx->clients[i].handshake_done && payload && payload_len >= 2 + 2 + 4) {
                            const uint8_t *p = (const uint8_t *) payload;
                            uint32_t remaining = payload_len;
                            uint16_t uuid_len = 0;
                            memcpy(&uuid_len, p, 2);
                            uuid_len = ntohs(uuid_len);
                            p += 2;
                            remaining -= 2;
                            if (uuid_len < 48 && remaining >= (uint32_t) uuid_len + 2 + 4) {
                                char uuid_buf[48] = {};
                                memcpy(uuid_buf, p, uuid_len);
                                uuid_buf[uuid_len] = '\0';
                                p += uuid_len;
                                remaining -= uuid_len;
                                uint16_t world_len = 0;
                                memcpy(&world_len, p, 2);
                                world_len = ntohs(world_len);
                                p += 2;
                                remaining -= 2;
                                if (world_len < 256 && remaining >= (uint32_t) world_len + 4) {
                                    char world_buf[256] = {};
                                    memcpy(world_buf, p, world_len);
                                    world_buf[world_len] = '\0';
                                    p += world_len;
                                    remaining -= world_len;
                                    uint32_t file_len = 0;
                                    memcpy(&file_len, p, 4);
                                    file_len = ntohl(file_len);
                                    p += 4;
                                    remaining -= 4;
                                    if (file_len > 0 && file_len <= remaining && file_len <= 4 * 1024 * 1024) {
                                        void *copy = malloc(file_len);
                                        if (copy) {
                                            memcpy(copy, p, file_len);
                                            bool content_changed = false;
                                            SDL_LockMutex(ctx->legacy_upload_mutex);
                                            int slot = -1;
                                            for (int k = 0; k < ctx->legacy_upload_count; k++) {
                                                if (strcmp(ctx->legacy_uploads[k].uuid, uuid_buf) == 0) {
                                                    slot = k;
                                                    break;
                                                }
                                            }
                                            if (slot < 0 && ctx->legacy_upload_count < COOP_MAX_CLIENTS) {
                                                slot = ctx->legacy_upload_count++;
                                                strncpy(ctx->legacy_uploads[slot].uuid, uuid_buf, 47);
                                                ctx->legacy_uploads[slot].uuid[47] = '\0';
                                                ctx->legacy_uploads[slot].bytes = nullptr;
                                                ctx->legacy_uploads[slot].size = 0;
                                            }
                                            if (slot >= 0) {
                                                // Only signal a re-merge when the content actually changed.
                                                // Receivers upload every 1s as a fallback; identical bytes
                                                // would otherwise thrash host -> receiver broadcasts.
                                                content_changed = (ctx->legacy_uploads[slot].size != file_len) ||
                                                                  (ctx->legacy_uploads[slot].bytes == nullptr) ||
                                                                  (memcmp(ctx->legacy_uploads[slot].bytes,
                                                                          copy, file_len) != 0);
                                                free(ctx->legacy_uploads[slot].bytes);
                                                ctx->legacy_uploads[slot].bytes = copy;
                                                ctx->legacy_uploads[slot].size = file_len;
                                                strncpy(ctx->legacy_uploads[slot].world_name, world_buf,
                                                        sizeof(ctx->legacy_uploads[slot].world_name) - 1);
                                                ctx->legacy_uploads[slot].world_name[
                                                    sizeof(ctx->legacy_uploads[slot].world_name) - 1] = '\0';
                                                ctx->legacy_uploads[slot].last_update_ms = SDL_GetTicks();
                                                copy = nullptr;
                                            }
                                            SDL_UnlockMutex(ctx->legacy_upload_mutex);
                                            free(copy); // non-null only if we failed to slot it
                                            // Only tell the main thread to re-merge on a real change.
                                            if (content_changed) {
                                                SDL_SetAtomicInt(&ctx->legacy_upload_pending, 1);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else if (msg_type == COOP_MSG_DISCONNECT) {
                        // Check if the disconnect includes a reason payload
                        if (payload && payload_len > 0) {
                            char reason[256];
                            size_t copy_len = payload_len < sizeof(reason) - 1
                                                  ? payload_len
                                                  : sizeof(reason) - 1;
                            memcpy(reason, payload, copy_len);
                            reason[copy_len] = '\0';
                            log_message(LOG_INFO, "[COOP NET] Client %s disconnected: %s\n",
                                        ctx->clients[i].label, reason);
                            set_status(ctx, "%s disconnected: %s",
                                       ctx->clients[i].display_name, reason);
                        } else {
                            log_message(LOG_INFO, "[COOP NET] Client %s disconnected gracefully.\n",
                                        ctx->clients[i].label);
                            const char *dn = ctx->clients[i].display_name[0] != '\0'
                                                 ? ctx->clients[i].display_name
                                                 : ctx->clients[i].username;
                            set_status(ctx, "%s disconnected", dn);
                        }
                        bool was_approved = ctx->clients[i].handshake_done;
                        bool was_pending = ctx->clients[i].pending_approval;
                        close_socket(&ctx->clients[i].socket_fd);
                        ctx->clients[i].active = false;
                        if (was_approved) {
                            ctx->client_count--;
                            lobby_dirty = true;
                        }
                        if (was_pending) remove_pending_request(ctx, i);
                    }
                    free(payload);
                } else if (disconnected) {
                    log_message(LOG_INFO, "[COOP NET] Client %s connection lost.\n", ctx->clients[i].label); {
                        const char *dn = ctx->clients[i].display_name[0] != '\0'
                                             ? ctx->clients[i].display_name
                                             : ctx->clients[i].username;
                        set_status(ctx, "%s: connection lost", dn);
                    }
                    bool was_approved = ctx->clients[i].handshake_done;
                    bool was_pending = ctx->clients[i].pending_approval;
                    close_socket(&ctx->clients[i].socket_fd);
                    ctx->clients[i].active = false;
                    if (was_approved) {
                        ctx->client_count--;
                        lobby_dirty = true;
                    }
                    if (was_pending) remove_pending_request(ctx, i);
                }
            }
        }

        // Heartbeats & timeouts
        Uint32 now = SDL_GetTicks();
        if (now - last_heartbeat >= HEARTBEAT_INTERVAL_MS) {
            last_heartbeat = now;
            for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
                if (!ctx->clients[i].active) continue;

                // Handshake timeout: no JOIN_REQUEST received in time
                if (!ctx->clients[i].handshake_done && !ctx->clients[i].pending_approval) {
                    if (now - ctx->clients[i].connect_time > HANDSHAKE_TIMEOUT_MS) {
                        log_message(LOG_INFO, "[COOP NET] Client %s handshake timeout.\n", ctx->clients[i].label);
                        close_socket(&ctx->clients[i].socket_fd);
                        ctx->clients[i].active = false;
                        continue;
                    }
                }

                // Only send heartbeats to approved clients
                if (!ctx->clients[i].handshake_done) continue;

                if (now - client_last_ack[i] > HEARTBEAT_TIMEOUT_MS) {
                    log_message(LOG_INFO, "[COOP NET] Client %s timed out (no heartbeat ACK).\n",
                                ctx->clients[i].label);
                    close_socket(&ctx->clients[i].socket_fd);
                    ctx->clients[i].active = false;
                    ctx->client_count--;
                    lobby_dirty = true;
                    continue;
                }

                if (!send_message(ctx->clients[i].socket_fd, COOP_MSG_HEARTBEAT, nullptr, 0)) {
                    log_message(LOG_INFO, "[COOP NET] Failed to send heartbeat to %s.\n",
                                ctx->clients[i].label);
                    close_socket(&ctx->clients[i].socket_fd);
                    ctx->clients[i].active = false;
                    ctx->client_count--;
                    lobby_dirty = true;
                }
            }
        }

        // If the lobby changed, rebuild and broadcast
        if (lobby_dirty) {
            rebuild_lobby_list(ctx);
            broadcast_player_list(ctx);
        }
    }

    // Graceful shutdown: notify all clients
    log_message(LOG_ERROR, "[CRASH-DEBUG H#1] Host thread: graceful shutdown loop start. client_count=%d\n", ctx->client_count);
    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        if (ctx->clients[i].active) {
            log_message(LOG_ERROR, "[CRASH-DEBUG H#2] Host thread: sending DISCONNECT to client[%d] fd=%d label='%s'\n",
                        i, (int) ctx->clients[i].socket_fd, ctx->clients[i].label);
            send_message(ctx->clients[i].socket_fd, COOP_MSG_DISCONNECT, nullptr, 0);
            close_socket(&ctx->clients[i].socket_fd);
            ctx->clients[i].active = false;
        }
    }
    ctx->client_count = 0;
    close_socket(&ctx->server_fd);
    log_message(LOG_ERROR, "[CRASH-DEBUG H#3] Host thread: all client sockets + server_fd closed.\n");

    log_message(LOG_INFO, "[COOP NET] Host thread exiting.\n");
    log_message(LOG_ERROR, "[CRASH-DEBUG H#4] Host thread: returning 0.\n");
    return 0;
}

// ---- Receiver Thread ----

static int SDLCALL receiver_thread_func(void *data) {
    CoopNetContext *ctx = (CoopNetContext *) data;

    // Create socket and store it in ctx immediately so coop_net_stop() can close it
    coop_socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == COOP_INVALID_SOCKET) {
        log_message(LOG_ERROR, "[COOP NET] Failed to create receiver socket.\n");
        set_state(ctx, COOP_NET_ERROR);
        set_status(ctx, "Failed to create socket");
        return 1;
    }
    ctx->client_fd = sock; // Store early so coop_net_stop() can close it to unblock connect

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t) ctx->connect_port);

    // Try inet_pton first (raw IP), then fall back to getaddrinfo (domain resolution)
    if (inet_pton(AF_INET, ctx->connect_ip, &addr.sin_addr) != 1) {
        // Not a valid IP - try resolving as a domain name
        struct addrinfo hints, *result = nullptr;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%d", ctx->connect_port);

        int gai_err = getaddrinfo(ctx->connect_ip, port_str, &hints, &result);
        if (gai_err != 0 || !result) {
            log_message(LOG_ERROR, "[COOP NET] Failed to resolve '%s' (error %d)\n",
                        ctx->connect_ip, gai_err);
            close_socket(&ctx->client_fd);
            set_state(ctx, COOP_NET_ERROR);
            set_status(ctx, "Could not resolve domain '%s'", ctx->connect_ip);
            if (result) freeaddrinfo(result);
            return 1;
        }

        // Use the first resolved address
        struct sockaddr_in *resolved = (struct sockaddr_in *) result->ai_addr;
        addr.sin_addr = resolved->sin_addr;

        char resolved_ip[64];
        inet_ntop(AF_INET, &addr.sin_addr, resolved_ip, sizeof(resolved_ip));
        log_message(LOG_INFO, "[COOP NET] Resolved '%s' to %s\n", ctx->connect_ip, resolved_ip);

        freeaddrinfo(result);
    }

    log_message(LOG_INFO, "[COOP NET] Receiver connecting to %s:%d...\n", ctx->connect_ip, ctx->connect_port);

    // Use non-blocking connect so we can check should_stop while waiting
    set_nonblocking(sock);

    int connect_result = connect(sock, (struct sockaddr *) &addr, sizeof(addr));
    bool connect_pending = false;
    if (connect_result == SOCKET_ERROR) {
#ifdef _WIN32
        int err = WSAGetLastError();
        log_message(LOG_INFO, "[COOP NET] connect() returned SOCKET_ERROR, WSA error=%d\n", err);
        if (err == WSAEWOULDBLOCK) {
            connect_pending = true;
        }
#else
        log_message(LOG_INFO, "[COOP NET] connect() returned SOCKET_ERROR, errno=%d (%s)\n", errno, strerror(errno));
        if (errno == EINPROGRESS) {
            connect_pending = true;
        }
#endif
        if (!connect_pending) {
#ifdef _WIN32
            int imm_err = err;
#else
            int imm_err = errno;
#endif
            log_message(LOG_ERROR, "[COOP NET] Immediate connect failure to %s:%d (error=%d)\n", ctx->connect_ip,
                        ctx->connect_port, imm_err);
            close_socket(&ctx->client_fd);
            set_state(ctx, COOP_NET_ERROR);
            set_connect_error_status(ctx, imm_err);
            return 1;
        }
    } else {
        log_message(LOG_INFO, "[COOP NET] connect() succeeded immediately to %s:%d\n", ctx->connect_ip,
                    ctx->connect_port);
    }

    // Wait for connection to complete (or should_stop / socket closed by stop)
    if (connect_pending) {
        bool connected = false;
        int connect_err = 0; // Stores the socket error if connection fails
        while (!should_stop(ctx)) {
            fd_set write_fds, err_fds;
            FD_ZERO(&write_fds);
            FD_ZERO(&err_fds);

            // Snapshot the fd once; the main thread may race close_socket() with us.
            // FD_SET(-1, ...) is UB on Linux (negative index into fds_bits smashes
            // the stack), so we must validate a local copy, not ctx->client_fd.
            coop_socket_t fd = ctx->client_fd;
            if (fd == COOP_INVALID_SOCKET) break;

            FD_SET(fd, &write_fds);
            FD_SET(fd, &err_fds);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200000; // 200ms poll interval

            int ready = select((int) (fd + 1), nullptr, &write_fds, &err_fds, &tv);
            if (ready < 0) break;
            if (ready == 0) continue; // Timeout, loop and check should_stop

            if (FD_ISSET(fd, &err_fds)) {
                socklen_t len = sizeof(connect_err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *) &connect_err, &len);
                log_message(LOG_ERROR, "[COOP NET] Connect error fd_set triggered (SO_ERROR=%d)\n", connect_err);
                break; // Connection failed
            }

            if (FD_ISSET(fd, &write_fds)) {
                // Check if connect actually succeeded via SO_ERROR
                socklen_t len = sizeof(connect_err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *) &connect_err, &len);
                if (connect_err == 0) {
                    connected = true;
                } else {
                    log_message(LOG_ERROR, "[COOP NET] Connect failed (SO_ERROR=%d)\n", connect_err);
                }
                break;
            }
        }

        if (!connected) {
            if (should_stop(ctx) || ctx->client_fd == COOP_INVALID_SOCKET) {
                log_message(LOG_INFO, "[COOP NET] Receiver connect cancelled.\n");
            } else {
                log_message(LOG_ERROR, "[COOP NET] Failed to connect to %s:%d (error=%d)\n", ctx->connect_ip,
                            ctx->connect_port, connect_err);
                set_state(ctx, COOP_NET_ERROR);
                set_connect_error_status(ctx, connect_err);
            }
            close_socket(&ctx->client_fd);
            return 1;
        }
    }

    set_tcp_nodelay(ctx->client_fd);

    log_message(LOG_INFO, "[COOP NET] TCP connected to %s:%d. Sending join request...\n",
                ctx->connect_ip, ctx->connect_port);

    // ---- Send JOIN_REQUEST with identity ----
    {
        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "version", ADVANCELY_VERSION);
        cJSON_AddStringToObject(req, "uuid", ctx->connect_uuid);
        cJSON_AddStringToObject(req, "username", ctx->connect_username);
        cJSON_AddStringToObject(req, "display_name", ctx->connect_display_name);
        char *json_str = cJSON_PrintUnformatted(req);
        cJSON_Delete(req);

        if (!json_str || !send_message(ctx->client_fd, COOP_MSG_JOIN_REQUEST, json_str, (uint32_t) strlen(json_str))) {
            free(json_str);
            log_message(LOG_ERROR, "[COOP NET] Failed to send join request.\n");
            set_state(ctx, COOP_NET_ERROR);
            set_status(ctx, "Failed to send join request");
            close_socket(&ctx->client_fd);
            return 1;
        }
        free(json_str);
    }

    set_status(ctx, "Waiting for host approval...");
    log_message(LOG_INFO, "[COOP NET] Join request sent. Waiting for host approval.\n");

    // ---- Wait for JOIN_ACCEPT / JOIN_REJECT ----
    bool accepted = false;
    while (!should_stop(ctx)) {
        // Snapshot fd locally to close the TOCTOU window with coop_net_stop.
        coop_socket_t fd = ctx->client_fd;
        if (fd == COOP_INVALID_SOCKET) break;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200ms

        int ready = select((int) (fd + 1), &read_fds, nullptr, nullptr, &tv);
        if (ready < 0) break;
        if (ready == 0) continue;

        uint32_t msg_type;
        char *payload;
        uint32_t payload_len;
        bool disconnected;

        if (read_message(ctx->client_fd, &msg_type, &payload, &payload_len, &disconnected)) {
            if (msg_type == COOP_MSG_JOIN_ACCEPT) {
                // Parse lobby player list
                char *json_str = (char *) malloc(payload_len + 1);
                if (json_str) {
                    memcpy(json_str, payload, payload_len);
                    json_str[payload_len] = '\0';
                    parse_lobby_json(ctx, json_str);
                    free(json_str);
                }
                accepted = true;
                free(payload);
                break;
            } else if (msg_type == COOP_MSG_JOIN_REJECT) {
                char reason[256] = "Rejected by host";
                if (payload_len > 0 && payload) {
                    size_t copy_len = (payload_len < sizeof(reason) - 1) ? payload_len : sizeof(reason) - 1;
                    memcpy(reason, payload, copy_len);
                    reason[copy_len] = '\0';
                }
                log_message(LOG_INFO, "[COOP NET] Join rejected: %s\n", reason);
                set_state(ctx, COOP_NET_ERROR);
                set_status(ctx, "%s", reason);
                free(payload);
                close_socket(&ctx->client_fd);
                return 1;
            } else if (msg_type == COOP_MSG_DISCONNECT) {
                log_message(LOG_INFO, "[COOP NET] Host disconnected while waiting for approval.\n");
                set_state(ctx, COOP_NET_DISCONNECTED);
                set_status(ctx, "Host shut down the lobby");
                free(payload);
                close_socket(&ctx->client_fd);
                return 1;
            }
            free(payload);
        } else if (disconnected) {
            log_message(LOG_INFO, "[COOP NET] Lost connection while waiting for approval. "
                        "The host may have rejected the request or closed the connection.\n");
            set_state(ctx, COOP_NET_DISCONNECTED);
            set_status(ctx, "Connection lost (host closed connection)");
            close_socket(&ctx->client_fd);
            return 1;
        }
    }

    if (!accepted) {
        // Cancelled via should_stop or socket closed
        close_socket(&ctx->client_fd);
        return 1;
    }

    // ---- Wait briefly for TEMPLATE_SYNC (sent right after JOIN_ACCEPT) ----
    {
        Uint32 sync_start = SDL_GetTicks();
        while (!should_stop(ctx) && (SDL_GetTicks() - sync_start) < 3000) {
            fd_set sync_fds;
            FD_ZERO(&sync_fds);
            coop_socket_t fd = ctx->client_fd;
            if (fd == COOP_INVALID_SOCKET) break;
            FD_SET(fd, &sync_fds);
            struct timeval stv;
            stv.tv_sec = 0;
            stv.tv_usec = 200000;
            int sr = select((int) (fd + 1), &sync_fds, nullptr, nullptr, &stv);
            if (sr <= 0) continue;
            uint32_t sync_type;
            char *sync_payload;
            uint32_t sync_len;
            bool sync_disc;
            if (read_message(ctx->client_fd, &sync_type, &sync_payload, &sync_len, &sync_disc)) {
                if (sync_type == COOP_MSG_TEMPLATE_SYNC && sync_payload) {
                    SDL_LockMutex(ctx->template_sync_mutex);
                    size_t copy_len = sync_len < sizeof(ctx->template_sync_payload) - 1
                                          ? sync_len
                                          : sizeof(ctx->template_sync_payload) - 1;
                    memcpy(ctx->template_sync_payload, sync_payload, copy_len);
                    ctx->template_sync_payload[copy_len] = '\0';
                    ctx->template_sync_ready = true;
                    SDL_UnlockMutex(ctx->template_sync_mutex);
                    log_message(LOG_INFO, "[COOP NET] Received template sync from host.\n");
                }
                free(sync_payload);
                break;
            } else if (sync_disc) {
                break;
            }
        }
    }

    set_state(ctx, COOP_NET_CONNECTED);
    set_status(ctx, "Connected to lobby");
    log_message(LOG_INFO, "[COOP NET] Joined lobby at %s:%d\n", ctx->connect_ip, ctx->connect_port);

    // ---- Main message loop ----
    Uint32 last_heartbeat_recv = SDL_GetTicks();
    const Uint32 HEARTBEAT_TIMEOUT_MS = 20000;

    while (!should_stop(ctx)) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        coop_socket_t fd = ctx->client_fd;
        if (fd == COOP_INVALID_SOCKET) break;
        FD_SET(fd, &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms

        int ready = select((int) (fd + 1), &read_fds, nullptr, nullptr, &tv);
        if (ready < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            log_message(LOG_ERROR, "[COOP NET] Receiver select() error.\n");
            set_state(ctx, COOP_NET_ERROR);
            set_status(ctx, "Connection error");
            break;
        }

        if (ready > 0 && FD_ISSET(fd, &read_fds)) {
            uint32_t msg_type;
            char *payload;
            uint32_t payload_len;
            bool disconnected;

            if (read_message(fd, &msg_type, &payload, &payload_len, &disconnected)) {
                if (msg_type == COOP_MSG_HEARTBEAT) {
                    send_message(ctx->client_fd, COOP_MSG_HEARTBEAT_ACK, nullptr, 0);
                    last_heartbeat_recv = SDL_GetTicks();
                } else if (msg_type == COOP_MSG_DISCONNECT) {
                    free(payload);
                    log_message(LOG_INFO, "[COOP NET] Host shut down the lobby.\n");
                    set_state(ctx, COOP_NET_DISCONNECTED);
                    set_status(ctx, "Host shut down the lobby");
                    break;
                } else if (msg_type == COOP_MSG_KICK) {
                    char reason[256] = "Kicked by host";
                    if (payload_len > 0 && payload) {
                        size_t copy_len = (payload_len < sizeof(reason) - 1) ? payload_len : sizeof(reason) - 1;
                        memcpy(reason, payload, copy_len);
                        reason[copy_len] = '\0';
                    }
                    free(payload);
                    log_message(LOG_INFO, "[COOP NET] Kicked: %s\n", reason);
                    set_state(ctx, COOP_NET_DISCONNECTED);
                    set_status(ctx, "Kicked: %s", reason);
                    break;
                } else if (msg_type == COOP_MSG_PLAYER_LIST) {
                    char *json_str = (char *) malloc(payload_len + 1);
                    if (json_str) {
                        memcpy(json_str, payload, payload_len);
                        json_str[payload_len] = '\0';
                        parse_lobby_json(ctx, json_str);
                        free(json_str);
                    }
                    last_heartbeat_recv = SDL_GetTicks();
                } else if (msg_type == COOP_MSG_STATE_UPDATE) {
                    SDL_LockMutex(ctx->recv_mutex);
                    free(ctx->recv_buffer);
                    ctx->recv_buffer = payload;
                    ctx->recv_buffer_size = payload_len;
                    ctx->recv_data_ready = true;
                    // Also keep a persistent copy of the merged snapshot
                    free(ctx->recv_merged_snapshot);
                    ctx->recv_merged_snapshot = (char *) malloc(payload_len);
                    if (ctx->recv_merged_snapshot) {
                        memcpy(ctx->recv_merged_snapshot, payload, payload_len);
                        ctx->recv_merged_snapshot_size = payload_len;
                    } else {
                        ctx->recv_merged_snapshot_size = 0;
                    }
                    SDL_UnlockMutex(ctx->recv_mutex);
                    payload = nullptr; // Ownership transferred
                    last_heartbeat_recv = SDL_GetTicks();
                } else if (msg_type == COOP_MSG_PLAYER_STATES) {
                    // Parse batched per-player snapshots:
                    // [4B player_count] then for each: [4B idx][4B size][data]
                    if (payload && payload_len >= 4) {
                        const char *p = payload;
                        uint32_t pc;
                        memcpy(&pc, p, 4);
                        pc = ntohl(pc);
                        p += 4;
                        SDL_LockMutex(ctx->recv_mutex);
                        // Free all old per-player buffers
                        for (int pi = 0; pi < COOP_MAX_LOBBY; pi++) {
                            free(ctx->recv_player_buffers[pi]);
                            ctx->recv_player_buffers[pi] = nullptr;
                            ctx->recv_player_buffer_sizes[pi] = 0;
                        }
                        ctx->recv_player_snapshot_count = 0;
                        for (uint32_t pi = 0; pi < pc && (size_t) (p - payload) + 8 <= payload_len; pi++) {
                            uint32_t idx, sz;
                            memcpy(&idx, p, 4);
                            idx = ntohl(idx);
                            p += 4;
                            memcpy(&sz, p, 4);
                            sz = ntohl(sz);
                            p += 4;
                            if ((size_t) (p - payload) + sz > payload_len) break;
                            if (idx < COOP_MAX_LOBBY) {
                                ctx->recv_player_buffers[idx] = (char *) malloc(sz);
                                if (ctx->recv_player_buffers[idx]) {
                                    memcpy(ctx->recv_player_buffers[idx], p, sz);
                                    ctx->recv_player_buffer_sizes[idx] = sz;
                                    if ((int) (idx + 1) > ctx->recv_player_snapshot_count)
                                        ctx->recv_player_snapshot_count = (int) (idx + 1);
                                }
                            }
                            p += sz;
                        }
                        ctx->recv_player_data_ready = true;
                        SDL_UnlockMutex(ctx->recv_mutex);
                        log_message(LOG_INFO, "[COOP NET] Received %u per-player snapshots from host.\n", pc);
                    }
                    last_heartbeat_recv = SDL_GetTicks();
                } else if (msg_type == COOP_MSG_TEMPLATE_SYNC) {
                    if (payload && payload_len > 0) {
                        SDL_LockMutex(ctx->template_sync_mutex);
                        size_t copy_len = payload_len < sizeof(ctx->template_sync_payload) - 1
                                              ? payload_len
                                              : sizeof(ctx->template_sync_payload) - 1;
                        memcpy(ctx->template_sync_payload, payload, copy_len);
                        ctx->template_sync_payload[copy_len] = '\0';
                        ctx->template_sync_ready = true;
                        SDL_UnlockMutex(ctx->template_sync_mutex);
                        log_message(LOG_INFO, "[COOP NET] Received template sync update from host.\n");
                    }
                    last_heartbeat_recv = SDL_GetTicks();
                }

                free(payload); // Free if not transferred
            } else if (disconnected) {
                log_message(LOG_INFO, "[COOP NET] Lost connection to host.\n");
                set_state(ctx, COOP_NET_DISCONNECTED);
                set_status(ctx, "Connection lost");
                break;
            }
        }

        Uint32 now = SDL_GetTicks();
        if (now - last_heartbeat_recv > HEARTBEAT_TIMEOUT_MS) {
            log_message(LOG_INFO, "[COOP NET] Host heartbeat timeout.\n");
            set_state(ctx, COOP_NET_DISCONNECTED);
            set_status(ctx, "Host timed out");
            break;
        }
    }

    // Graceful disconnect - include reason if one was set by coop_net_disconnect_with_reason
    log_message(LOG_ERROR, "[CRASH-DEBUG R#1] Receiver thread: entering graceful shutdown. client_fd=%d reason='%s'\n",
                (int) ctx->client_fd, ctx->disconnect_reason);
    if (ctx->client_fd != COOP_INVALID_SOCKET) {
        if (ctx->disconnect_reason[0] != '\0') {
            log_message(LOG_ERROR, "[CRASH-DEBUG R#2] Receiver thread: sending DISCONNECT with reason.\n");
            send_message(ctx->client_fd, COOP_MSG_DISCONNECT,
                         ctx->disconnect_reason, (uint32_t) strlen(ctx->disconnect_reason));
            ctx->disconnect_reason[0] = '\0';
        } else {
            log_message(LOG_ERROR, "[CRASH-DEBUG R#3] Receiver thread: sending empty DISCONNECT.\n");
            send_message(ctx->client_fd, COOP_MSG_DISCONNECT, nullptr, 0);
        }
        close_socket(&ctx->client_fd);
        log_message(LOG_ERROR, "[CRASH-DEBUG R#4] Receiver thread: client_fd closed.\n");
    } else {
        log_message(LOG_ERROR, "[CRASH-DEBUG R#4] Receiver thread: client_fd already closed; skipping graceful send.\n");
    }

    log_message(LOG_INFO, "[COOP NET] Receiver thread exiting.\n");
    log_message(LOG_ERROR, "[CRASH-DEBUG R#5] Receiver thread: returning 0.\n");
    return 0;
}

// ---- Public API Implementation ----

bool coop_net_init(CoopNetContext *ctx) {
    memset(ctx, 0, sizeof(CoopNetContext));
    ctx->server_fd = COOP_INVALID_SOCKET;
    ctx->client_fd = COOP_INVALID_SOCKET;
    SDL_SetAtomicInt(&ctx->state, COOP_NET_IDLE);
    SDL_SetAtomicInt(&ctx->should_stop, 0);

    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        ctx->clients[i].socket_fd = COOP_INVALID_SOCKET;
        ctx->clients[i].active = false;
    }

    ctx->status_mutex = SDL_CreateMutex();
    ctx->recv_mutex = SDL_CreateMutex();
    ctx->lobby_mutex = SDL_CreateMutex();
    ctx->custom_mod_mutex = SDL_CreateMutex();
    ctx->template_sync_mutex = SDL_CreateMutex();
    ctx->legacy_upload_mutex = SDL_CreateMutex();
    if (!ctx->status_mutex || !ctx->recv_mutex || !ctx->lobby_mutex || !ctx->custom_mod_mutex || !ctx->
        template_sync_mutex || !ctx->legacy_upload_mutex) {
        log_message(LOG_ERROR, "[COOP NET] Failed to create mutexes.\n");
        return false;
    }

    strncpy(ctx->status_msg, "Idle", sizeof(ctx->status_msg) - 1);

#ifdef _WIN32
    if (!wsa_initialized) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            log_message(LOG_ERROR, "[COOP NET] WSAStartup failed.\n");
            return false;
        }
        wsa_initialized = true;
        log_message(LOG_INFO, "[COOP NET] Winsock initialized.\n");
    }
#endif

    log_message(LOG_INFO, "[COOP NET] Networking context initialized.\n");
    return true;
}

void coop_net_shutdown(CoopNetContext *ctx) {
    coop_net_stop(ctx);

    if (ctx->status_mutex) {
        SDL_DestroyMutex(ctx->status_mutex);
        ctx->status_mutex = nullptr;
    }
    if (ctx->recv_mutex) {
        SDL_DestroyMutex(ctx->recv_mutex);
        ctx->recv_mutex = nullptr;
    }
    if (ctx->lobby_mutex) {
        SDL_DestroyMutex(ctx->lobby_mutex);
        ctx->lobby_mutex = nullptr;
    }
    if (ctx->custom_mod_mutex) {
        SDL_DestroyMutex(ctx->custom_mod_mutex);
        ctx->custom_mod_mutex = nullptr;
    }
    if (ctx->template_sync_mutex) {
        SDL_DestroyMutex(ctx->template_sync_mutex);
        ctx->template_sync_mutex = nullptr;
    }
    // Free any cached legacy stats uploads (LEGACY <=1.6.4 only).
    if (ctx->legacy_upload_mutex) {
        SDL_LockMutex(ctx->legacy_upload_mutex);
        for (int i = 0; i < ctx->legacy_upload_count; i++) {
            free(ctx->legacy_uploads[i].bytes);
            ctx->legacy_uploads[i].bytes = nullptr;
        }
        ctx->legacy_upload_count = 0;
        SDL_UnlockMutex(ctx->legacy_upload_mutex);
        SDL_DestroyMutex(ctx->legacy_upload_mutex);
        ctx->legacy_upload_mutex = nullptr;
    }
    free(ctx->recv_buffer);
    ctx->recv_buffer = nullptr;
    free(ctx->recv_merged_snapshot);
    ctx->recv_merged_snapshot = nullptr;
    for (int i = 0; i < COOP_MAX_LOBBY; i++) {
        free(ctx->recv_player_buffers[i]);
        ctx->recv_player_buffers[i] = nullptr;
    }
    ctx->recv_player_snapshot_count = 0;

#ifdef _WIN32
    if (wsa_initialized) {
        WSACleanup();
        wsa_initialized = false;
    }
#endif

    log_message(LOG_INFO, "[COOP NET] Networking context shut down.\n");
}

bool coop_net_start_host(CoopNetContext *ctx, const char *ip, int port,
                         const char *username, const char *uuid, const char *display_name) {
    // Stop any existing session
    coop_net_stop(ctx);

    // Store host identity
    strncpy(ctx->host_username, username ? username : "", sizeof(ctx->host_username) - 1);
    strncpy(ctx->host_uuid, uuid ? uuid : "", sizeof(ctx->host_uuid) - 1);
    strncpy(ctx->host_display_name, display_name ? display_name : "", sizeof(ctx->host_display_name) - 1);

    // Clear lobby and pending requests
    SDL_LockMutex(ctx->lobby_mutex);
    ctx->lobby_player_count = 0;
    ctx->lobby_changed = false;
    ctx->pending_request_count = 0;
    ctx->pending_requests_changed = false;
    SDL_UnlockMutex(ctx->lobby_mutex);

    // Create listening socket
    coop_socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == COOP_INVALID_SOCKET) {
        log_message(LOG_ERROR, "[COOP NET] Failed to create host socket.\n");
        set_state(ctx, COOP_NET_ERROR);
        set_status(ctx, "Failed to create socket");
        return false;
    }

    // Allow port reuse
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t) port);

    if (ip && ip[0] != '\0') {
        if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
            log_message(LOG_ERROR, "[COOP NET] Invalid host IP address: %s\n", ip);
            COOP_CLOSE_SOCKET(sock);
            set_state(ctx, COOP_NET_ERROR);
            set_status(ctx, "Invalid IP address");
            return false;
        }
    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) == SOCKET_ERROR) {
#ifdef _WIN32
        int bind_err = WSAGetLastError();
        log_message(LOG_ERROR, "[COOP NET] Failed to bind to %s:%d (WSA error=%d)\n", ip ? ip : "0.0.0.0", port,
                    bind_err);
#else
        int bind_err = errno;
        log_message(LOG_ERROR, "[COOP NET] Failed to bind to %s:%d (errno=%d: %s)\n", ip ? ip : "0.0.0.0", port,
                    bind_err, strerror(bind_err));
#endif
        COOP_CLOSE_SOCKET(sock);
        set_state(ctx, COOP_NET_ERROR);
        set_connect_error_status(ctx, bind_err);
        return false;
    }

    // Log the actual bound address for debugging
    {
        struct sockaddr_in bound_addr;
        socklen_t bound_len = sizeof(bound_addr);
        if (getsockname(sock, (struct sockaddr *) &bound_addr, &bound_len) == 0) {
            char bound_ip[64];
            inet_ntop(AF_INET, &bound_addr.sin_addr, bound_ip, sizeof(bound_ip));
            log_message(LOG_INFO, "[COOP NET] Socket actually bound to %s:%d\n", bound_ip, ntohs(bound_addr.sin_port));
        }
    }

    if (listen(sock, COOP_MAX_CLIENTS) == SOCKET_ERROR) {
        log_message(LOG_ERROR, "[COOP NET] Failed to listen on socket.\n");
        COOP_CLOSE_SOCKET(sock);
        set_state(ctx, COOP_NET_ERROR);
        set_status(ctx, "Failed to listen");
        return false;
    }

    set_nonblocking(sock);
    ctx->server_fd = sock;
    ctx->client_count = 0;

    set_state(ctx, COOP_NET_LISTENING);
    set_status(ctx, "Listening on port %d", port);
    SDL_SetAtomicInt(&ctx->should_stop, 0);

    ctx->thread = SDL_CreateThread(host_thread_func, "CoopHost", ctx);
    if (!ctx->thread) {
        log_message(LOG_ERROR, "[COOP NET] Failed to create host thread.\n");
        close_socket(&ctx->server_fd);
        set_state(ctx, COOP_NET_ERROR);
        set_status(ctx, "Failed to start host thread");
        return false;
    }

    log_message(LOG_INFO, "[COOP NET] Host started on %s:%d\n", ip && ip[0] ? ip : "0.0.0.0", port);
    return true;
}

bool coop_net_start_receiver(CoopNetContext *ctx, const char *ip, int port,
                             const char *username, const char *uuid, const char *display_name) {
    coop_net_stop(ctx);

    // Store target address and identity for the thread
    strncpy(ctx->connect_ip, ip, sizeof(ctx->connect_ip) - 1);
    ctx->connect_ip[sizeof(ctx->connect_ip) - 1] = '\0';
    ctx->connect_port = port;
    strncpy(ctx->connect_username, username ? username : "", sizeof(ctx->connect_username) - 1);
    strncpy(ctx->connect_uuid, uuid ? uuid : "", sizeof(ctx->connect_uuid) - 1);
    strncpy(ctx->connect_display_name, display_name ? display_name : "", sizeof(ctx->connect_display_name) - 1);

    // Clear lobby
    SDL_LockMutex(ctx->lobby_mutex);
    ctx->lobby_player_count = 0;
    ctx->lobby_changed = false;
    SDL_UnlockMutex(ctx->lobby_mutex);

    set_state(ctx, COOP_NET_CONNECTING);
    set_status(ctx, "Connecting...");
    SDL_SetAtomicInt(&ctx->should_stop, 0);

    ctx->thread = SDL_CreateThread(receiver_thread_func, "CoopRecv", ctx);
    if (!ctx->thread) {
        log_message(LOG_ERROR, "[COOP NET] Failed to create receiver thread.\n");
        set_state(ctx, COOP_NET_ERROR);
        set_status(ctx, "Failed to start receiver thread");
        return false;
    }

    log_message(LOG_INFO, "[COOP NET] Receiver thread spawned, connecting to %s:%d\n", ip, port);
    return true;
}

void coop_net_stop(CoopNetContext *ctx) {
    log_message(LOG_ERROR, "[CRASH-DEBUG S#1] coop_net_stop entered. ctx=%p thread=%p server_fd=%d client_fd=%d disconnect_reason='%s'\n",
                (void *) ctx,
                ctx ? (void *) ctx->thread : nullptr,
                ctx ? (int) ctx->server_fd : -1,
                ctx ? (int) ctx->client_fd : -1,
                ctx ? ctx->disconnect_reason : "(null ctx)");
    if (!ctx->thread) {
        log_message(LOG_ERROR, "[CRASH-DEBUG S#2] coop_net_stop: no thread, early return.\n");
        return;
    }

    SDL_SetAtomicInt(&ctx->should_stop, 1);
    log_message(LOG_ERROR, "[CRASH-DEBUG S#3] coop_net_stop: should_stop set.\n");

    // Close sockets from outside the thread to unblock select() / non-blocking connect().
    // If a disconnect reason is pending, keep client_fd open so the receiver thread can
    // send the reason before closing it. The thread will exit via should_stop within ~200ms.
    close_socket(&ctx->server_fd);
    log_message(LOG_ERROR, "[CRASH-DEBUG S#4] coop_net_stop: server_fd closed.\n");
    if (ctx->disconnect_reason[0] == '\0') {
        close_socket(&ctx->client_fd);
        log_message(LOG_ERROR, "[CRASH-DEBUG S#5] coop_net_stop: client_fd closed (no reason pending).\n");
    } else {
        log_message(LOG_ERROR, "[CRASH-DEBUG S#5] coop_net_stop: client_fd left open for thread to flush reason.\n");
    }

    log_message(LOG_ERROR, "[CRASH-DEBUG S#6] coop_net_stop: waiting for thread...\n");
    SDL_WaitThread(ctx->thread, nullptr);
    log_message(LOG_ERROR, "[CRASH-DEBUG S#7] coop_net_stop: SDL_WaitThread returned.\n");
    ctx->thread = nullptr;

    // Clean up any remaining client sockets (should already be closed by thread)
    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        if (ctx->clients[i].active) {
            close_socket(&ctx->clients[i].socket_fd);
            ctx->clients[i].active = false;
        }
    }
    ctx->client_count = 0;
    close_socket(&ctx->client_fd);
    log_message(LOG_ERROR, "[CRASH-DEBUG S#8] coop_net_stop: remaining sockets closed.\n");

    // Free receive buffers
    SDL_LockMutex(ctx->recv_mutex);
    log_message(LOG_ERROR, "[CRASH-DEBUG S#9] coop_net_stop: recv_mutex locked; freeing buffers (recv_buffer=%p merged=%p).\n",
                (void *) ctx->recv_buffer, (void *) ctx->recv_merged_snapshot);
    free(ctx->recv_buffer);
    ctx->recv_buffer = nullptr;
    ctx->recv_buffer_size = 0;
    ctx->recv_data_ready = false;
    free(ctx->recv_merged_snapshot);
    ctx->recv_merged_snapshot = nullptr;
    ctx->recv_merged_snapshot_size = 0;
    for (int i = 0; i < COOP_MAX_LOBBY; i++) {
        free(ctx->recv_player_buffers[i]);
        ctx->recv_player_buffers[i] = nullptr;
        ctx->recv_player_buffer_sizes[i] = 0;
    }
    ctx->recv_player_snapshot_count = 0;
    ctx->recv_player_data_ready = false;
    SDL_UnlockMutex(ctx->recv_mutex);
    log_message(LOG_ERROR, "[CRASH-DEBUG S#10] coop_net_stop: recv_mutex unlocked.\n");

    // Clear lobby and pending requests
    SDL_LockMutex(ctx->lobby_mutex);
    ctx->lobby_player_count = 0;
    ctx->lobby_changed = true;
    ctx->pending_request_count = 0;
    ctx->pending_requests_changed = true;
    SDL_UnlockMutex(ctx->lobby_mutex);
    log_message(LOG_ERROR, "[CRASH-DEBUG S#11] coop_net_stop: lobby cleared.\n");

    set_state(ctx, COOP_NET_IDLE);
    set_status(ctx, "Idle");

    log_message(LOG_INFO, "[COOP NET] Networking stopped.\n");
    log_message(LOG_ERROR, "[CRASH-DEBUG S#12] coop_net_stop exiting cleanly.\n");
}

void coop_net_disconnect_with_reason(CoopNetContext *ctx, const char *reason) {
    if (!ctx) return;
    // Store the reason so the receiver thread's cleanup path can include it in the
    // DISCONNECT message. This avoids sending on the socket from the main thread
    // while the network thread is still reading from it.
    if (reason) {
        strncpy(ctx->disconnect_reason, reason, sizeof(ctx->disconnect_reason) - 1);
        ctx->disconnect_reason[sizeof(ctx->disconnect_reason) - 1] = '\0';
    }
    coop_net_stop(ctx);
}

void coop_net_tick(CoopNetContext *ctx) {
    // Lightweight per-frame check. The main loop in main.cpp reads recv_data_ready
    // directly after this call for state updates. This function handles any
    // housekeeping that needs to happen each frame.
    if (!ctx) return;
    // Currently no per-frame housekeeping needed beyond what main.cpp handles.
}

CoopNetState coop_net_get_state(CoopNetContext *ctx) {
    return (CoopNetState) SDL_GetAtomicInt(&ctx->state);
}

void coop_net_get_status_msg(CoopNetContext *ctx, char *out, size_t out_size) {
    SDL_LockMutex(ctx->status_mutex);
    strncpy(out, ctx->status_msg, out_size - 1);
    out[out_size - 1] = '\0';
    SDL_UnlockMutex(ctx->status_mutex);
}

int coop_net_get_client_count(CoopNetContext *ctx) {
    return ctx->client_count;
}

bool coop_net_broadcast(CoopNetContext *ctx, const void *data, size_t size) {
    if (coop_net_get_state(ctx) != COOP_NET_LISTENING) return false;
    if (ctx->client_count == 0) return true;

    bool all_ok = true;
    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        if (!ctx->clients[i].active || !ctx->clients[i].handshake_done) continue;
        if (!send_message(ctx->clients[i].socket_fd, COOP_MSG_STATE_UPDATE, data, (uint32_t) size)) {
            log_message(LOG_ERROR, "[COOP NET] Failed to broadcast to client %s.\n", ctx->clients[i].label);
            close_socket(&ctx->clients[i].socket_fd);
            ctx->clients[i].active = false;
            ctx->client_count--;
            all_ok = false;
        }
    }
    return all_ok;
}

bool coop_net_broadcast_player_states(CoopNetContext *ctx,
                                      char **player_buffers, size_t *player_sizes,
                                      int player_count) {
    if (coop_net_get_state(ctx) != COOP_NET_LISTENING) return false;
    if (ctx->client_count == 0 || player_count <= 0) return true;

    // Build batched payload: [4B player_count] then for each [4B idx][4B size][data]
    size_t total_size = 4;
    for (int i = 0; i < player_count; i++) {
        total_size += 4 + 4 + player_sizes[i]; // idx + size + data
    }

    char *payload = (char *) malloc(total_size);
    if (!payload) return false;

    char *p = payload;
    uint32_t net_pc = htonl((uint32_t) player_count);
    memcpy(p, &net_pc, 4);
    p += 4;

    for (int i = 0; i < player_count; i++) {
        uint32_t net_idx = htonl((uint32_t) i);
        uint32_t net_sz = htonl((uint32_t) player_sizes[i]);
        memcpy(p, &net_idx, 4);
        p += 4;
        memcpy(p, &net_sz, 4);
        p += 4;
        memcpy(p, player_buffers[i], player_sizes[i]);
        p += player_sizes[i];
    }

    bool all_ok = true;
    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        if (!ctx->clients[i].active || !ctx->clients[i].handshake_done) continue;
        if (!send_message(ctx->clients[i].socket_fd, COOP_MSG_PLAYER_STATES, payload, (uint32_t) total_size)) {
            log_message(LOG_ERROR, "[COOP NET] Failed to broadcast player states to client %s.\n",
                        ctx->clients[i].label);
            close_socket(&ctx->clients[i].socket_fd);
            ctx->clients[i].active = false;
            ctx->client_count--;
            all_ok = false;
        }
    }

    free(payload);
    return all_ok;
}

// ---- Lobby & Join Request API ----

int coop_net_get_lobby_players(CoopNetContext *ctx, CoopLobbyPlayer *out_players, int max_players) {
    SDL_LockMutex(ctx->lobby_mutex);
    int count = ctx->lobby_player_count;
    if (count > max_players) count = max_players;
    memcpy(out_players, ctx->lobby_players, (size_t) count * sizeof(CoopLobbyPlayer));
    SDL_UnlockMutex(ctx->lobby_mutex);
    return count;
}

bool coop_net_lobby_changed(CoopNetContext *ctx) {
    SDL_LockMutex(ctx->lobby_mutex);
    bool changed = ctx->lobby_changed;
    ctx->lobby_changed = false;
    SDL_UnlockMutex(ctx->lobby_mutex);
    return changed;
}

int coop_net_get_pending_requests(CoopNetContext *ctx, CoopJoinRequest *out_requests, int max_requests) {
    SDL_LockMutex(ctx->lobby_mutex);
    int count = ctx->pending_request_count;
    if (count > max_requests) count = max_requests;
    memcpy(out_requests, ctx->pending_requests, (size_t) count * sizeof(CoopJoinRequest));
    SDL_UnlockMutex(ctx->lobby_mutex);
    return count;
}

bool coop_net_pending_requests_changed(CoopNetContext *ctx) {
    SDL_LockMutex(ctx->lobby_mutex);
    bool changed = ctx->pending_requests_changed;
    ctx->pending_requests_changed = false;
    SDL_UnlockMutex(ctx->lobby_mutex);
    return changed;
}

bool coop_net_approve_request(CoopNetContext *ctx, int client_slot) {
    if (client_slot < 0 || client_slot >= COOP_MAX_CLIENTS) return false;
    if (!ctx->clients[client_slot].active || !ctx->clients[client_slot].pending_approval) return false;

    ctx->clients[client_slot].handshake_done = true;
    ctx->clients[client_slot].pending_approval = false;
    ctx->client_count++;

    log_message(LOG_INFO, "[COOP NET] Approved join request from %s (%s).\n",
                ctx->clients[client_slot].username, ctx->clients[client_slot].label);

    // Remove from pending queue
    remove_pending_request(ctx, client_slot);

    // Rebuild lobby and broadcast
    rebuild_lobby_list(ctx);

    // Send JOIN_ACCEPT to this client with current lobby state
    char *json = build_lobby_json(ctx);
    if (json) {
        send_message(ctx->clients[client_slot].socket_fd, COOP_MSG_JOIN_ACCEPT, json, (uint32_t) strlen(json));
        free(json);
    }

    // Send TEMPLATE_SYNC with host's version, category, optional flag, and merge settings
    SDL_LockMutex(ctx->template_sync_mutex);
    if (ctx->template_sync_payload[0] != '\0') {
        send_message(ctx->clients[client_slot].socket_fd, COOP_MSG_TEMPLATE_SYNC,
                     ctx->template_sync_payload, (uint32_t) strlen(ctx->template_sync_payload));
    }
    SDL_UnlockMutex(ctx->template_sync_mutex);

    // Broadcast updated player list to all other approved clients
    broadcast_player_list(ctx);
    return true;
}

bool coop_net_reject_request(CoopNetContext *ctx, int client_slot, const char *reason) {
    if (client_slot < 0 || client_slot >= COOP_MAX_CLIENTS) return false;
    if (!ctx->clients[client_slot].active || !ctx->clients[client_slot].pending_approval) return false;

    // Queue the reject for the host thread to process safely
    const char *msg = reason ? reason : "Rejected by host";
    strncpy(ctx->clients[client_slot].pending_action_reason, msg,
            sizeof(ctx->clients[client_slot].pending_action_reason) - 1);
    ctx->clients[client_slot].pending_action_reason[sizeof(ctx->clients[client_slot].pending_action_reason) - 1] = '\0';
    SDL_SetAtomicInt(&ctx->clients[client_slot].pending_action, COOP_ACTION_REJECT);
    return true;
}

bool coop_net_kick_client(CoopNetContext *ctx, int client_slot, const char *reason) {
    if (client_slot < 0 || client_slot >= COOP_MAX_CLIENTS) return false;
    if (!ctx->clients[client_slot].active || !ctx->clients[client_slot].handshake_done) return false;

    // Queue the kick for the host thread to process safely
    const char *msg = reason ? reason : "Kicked by host";
    strncpy(ctx->clients[client_slot].pending_action_reason, msg,
            sizeof(ctx->clients[client_slot].pending_action_reason) - 1);
    ctx->clients[client_slot].pending_action_reason[sizeof(ctx->clients[client_slot].pending_action_reason) - 1] = '\0';
    SDL_SetAtomicInt(&ctx->clients[client_slot].pending_action, COOP_ACTION_KICK);
    return true;
}

// LEGACY (<=1.6.4) ONLY. See header for rationale; mid-era and modern versions
// do not use this path.
bool coop_net_send_legacy_stats_upload(CoopNetContext *ctx,
                                       const char *uuid,
                                       const char *world_name,
                                       const void *file_bytes,
                                       uint32_t file_len) {
    if (!ctx || !uuid || !world_name) return false;
    if (!file_bytes || file_len == 0) return false;
    if (coop_net_get_state(ctx) != COOP_NET_CONNECTED) return false;
    if (ctx->client_fd == COOP_INVALID_SOCKET) return false;

    const size_t uuid_len = strlen(uuid);
    const size_t world_len = strlen(world_name);
    if (uuid_len > 0xFFFF || world_len > 0xFFFF) return false;

    // Payload: [2B uuid_len][uuid][2B world_len][world][4B file_len][file_bytes]
    const size_t total = 2 + uuid_len + 2 + world_len + 4 + file_len;
    uint8_t *buf = (uint8_t *) malloc(total);
    if (!buf) return false;

    size_t off = 0;
    uint16_t u16;
    uint32_t u32;

    u16 = htons((uint16_t) uuid_len);
    memcpy(buf + off, &u16, 2);
    off += 2;
    memcpy(buf + off, uuid, uuid_len);
    off += uuid_len;

    u16 = htons((uint16_t) world_len);
    memcpy(buf + off, &u16, 2);
    off += 2;
    memcpy(buf + off, world_name, world_len);
    off += world_len;

    u32 = htonl(file_len);
    memcpy(buf + off, &u32, 4);
    off += 4;
    memcpy(buf + off, file_bytes, file_len);
    off += file_len;

    bool ok = send_message(ctx->client_fd, COOP_MSG_LEGACY_STATS_UPLOAD,
                           buf, (uint32_t) total);
    free(buf);
    return ok;
}

// LEGACY (<=1.6.4) ONLY. Atomic check-and-clear for the "new upload arrived" flag.
bool coop_net_legacy_upload_consume(CoopNetContext *ctx) {
    if (!ctx) return false;
    return SDL_SetAtomicInt(&ctx->legacy_upload_pending, 0) == 1;
}

// LEGACY (<=1.6.4) ONLY. Host-side cache accessor.
bool coop_net_get_legacy_stats_upload(CoopNetContext *ctx,
                                      const char *uuid,
                                      void **out_bytes,
                                      uint32_t *out_size,
                                      char *out_world_name,
                                      size_t world_name_cap) {
    if (!ctx || !uuid || !out_bytes || !out_size) return false;
    if (!ctx->legacy_upload_mutex) return false;

    *out_bytes = nullptr;
    *out_size = 0;
    if (out_world_name && world_name_cap > 0) out_world_name[0] = '\0';

    bool found = false;
    SDL_LockMutex(ctx->legacy_upload_mutex);
    for (int i = 0; i < ctx->legacy_upload_count; i++) {
        if (strcmp(ctx->legacy_uploads[i].uuid, uuid) == 0 &&
            ctx->legacy_uploads[i].bytes && ctx->legacy_uploads[i].size > 0) {
            void *copy = malloc(ctx->legacy_uploads[i].size);
            if (copy) {
                memcpy(copy, ctx->legacy_uploads[i].bytes, ctx->legacy_uploads[i].size);
                *out_bytes = copy;
                *out_size = ctx->legacy_uploads[i].size;
                if (out_world_name && world_name_cap > 0) {
                    strncpy(out_world_name, ctx->legacy_uploads[i].world_name, world_name_cap - 1);
                    out_world_name[world_name_cap - 1] = '\0';
                }
                found = true;
            }
            break;
        }
    }
    SDL_UnlockMutex(ctx->legacy_upload_mutex);
    return found;
}

bool coop_net_send_custom_goal_mod(CoopNetContext *ctx, const CoopCustomGoalModMsg *msg) {
    if (!ctx || !msg) return false;
    if (coop_net_get_state(ctx) != COOP_NET_CONNECTED) return false;
    if (ctx->client_fd == COOP_INVALID_SOCKET) return false;

    return send_message(ctx->client_fd, COOP_MSG_CUSTOM_GOAL_MOD,
                        msg, (uint32_t) sizeof(CoopCustomGoalModMsg));
}

int coop_net_drain_custom_mods(CoopNetContext *ctx, CoopCustomGoalModMsg *out_mods, int max_mods) {
    if (!ctx || !out_mods || max_mods <= 0) return 0;

    SDL_LockMutex(ctx->custom_mod_mutex);
    int count = ctx->custom_mod_count;
    if (count > max_mods) count = max_mods;
    if (count > 0) {
        memcpy(out_mods, ctx->custom_mod_queue, count * sizeof(CoopCustomGoalModMsg));
        // Shift remaining items if we didn't drain all
        int remaining = ctx->custom_mod_count - count;
        if (remaining > 0) {
            memmove(ctx->custom_mod_queue, &ctx->custom_mod_queue[count],
                    remaining * sizeof(CoopCustomGoalModMsg));
        }
        ctx->custom_mod_count = remaining;
    }
    SDL_UnlockMutex(ctx->custom_mod_mutex);

    return count;
}

// ---- Template Sync API ----

void coop_net_set_template_sync(CoopNetContext *ctx, const char *json_payload) {
    if (!ctx || !json_payload) return;
    SDL_LockMutex(ctx->template_sync_mutex);
    strncpy(ctx->template_sync_payload, json_payload, sizeof(ctx->template_sync_payload) - 1);
    ctx->template_sync_payload[sizeof(ctx->template_sync_payload) - 1] = '\0';
    SDL_UnlockMutex(ctx->template_sync_mutex);
}

bool coop_net_template_sync_ready(CoopNetContext *ctx) {
    if (!ctx) return false;
    SDL_LockMutex(ctx->template_sync_mutex);
    bool ready = ctx->template_sync_ready;
    if (ready) ctx->template_sync_ready = false;
    SDL_UnlockMutex(ctx->template_sync_mutex);
    return ready;
}

bool coop_net_get_template_sync(CoopNetContext *ctx, char *out, size_t out_size) {
    if (!ctx || !out || out_size == 0) return false;
    SDL_LockMutex(ctx->template_sync_mutex);
    if (ctx->template_sync_payload[0] == '\0') {
        SDL_UnlockMutex(ctx->template_sync_mutex);
        return false;
    }
    strncpy(out, ctx->template_sync_payload, out_size - 1);
    out[out_size - 1] = '\0';
    SDL_UnlockMutex(ctx->template_sync_mutex);
    return true;
}

void coop_net_broadcast_template_sync(CoopNetContext *ctx) {
    if (!ctx) return;
    SDL_LockMutex(ctx->template_sync_mutex);
    if (ctx->template_sync_payload[0] == '\0') {
        SDL_UnlockMutex(ctx->template_sync_mutex);
        return;
    }
    char payload[1024];
    strncpy(payload, ctx->template_sync_payload, sizeof(payload) - 1);
    payload[sizeof(payload) - 1] = '\0';
    SDL_UnlockMutex(ctx->template_sync_mutex);

    uint32_t len = (uint32_t) strlen(payload);
    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        if (ctx->clients[i].active && ctx->clients[i].handshake_done) {
            send_message(ctx->clients[i].socket_fd, COOP_MSG_TEMPLATE_SYNC, payload, len);
        }
    }
}
