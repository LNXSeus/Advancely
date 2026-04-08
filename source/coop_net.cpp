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
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <sys/select.h>
    #define COOP_CLOSE_SOCKET close
    #define SOCKET_ERROR (-1)
#endif

#include "coop_net.h"
#include "logger.h"

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
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
}

// Send exactly `len` bytes. Returns true on success, false on error/disconnect.
static bool send_all(coop_socket_t fd, const void *data, size_t len) {
    const char *ptr = (const char *)data;
    size_t remaining = len;
    while (remaining > 0) {
        int sent = send(fd, ptr, (int)remaining, 0);
        if (sent <= 0) return false;
        ptr += sent;
        remaining -= (size_t)sent;
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
    int result = recv(fd, (char *)buf, (int)len, 0);
    if (result > 0) return result;
    if (result == 0) return 0; // Connection closed
#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) return -1;
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
#endif
    return -2; // Real error
}

// Read a complete framed message from a socket.
// Returns true if a message was read. Sets out_type, out_payload (caller must free), out_payload_len.
// Returns false if no complete message available yet or error.
// Sets *disconnected = true if the remote side closed the connection.
static bool read_message(coop_socket_t fd, uint32_t *out_type, char **out_payload, uint32_t *out_payload_len, bool *disconnected) {
    *disconnected = false;
    *out_payload = nullptr;
    *out_payload_len = 0;

    // Read header
    uint32_t header[2];
    size_t header_read = 0;
    while (header_read < sizeof(header)) {
        int r = recv_exact(fd, (char *)header + header_read, sizeof(header) - header_read);
        if (r == 0) { *disconnected = true; return false; }
        if (r == -1) return false; // Would block, no data yet
        if (r == -2) { *disconnected = true; return false; }
        header_read += (size_t)r;
    }

    *out_type = ntohl(header[0]);
    *out_payload_len = ntohl(header[1]);

    // Sanity check payload size (16 MB max)
    if (*out_payload_len > 16 * 1024 * 1024) {
        *disconnected = true;
        return false;
    }

    if (*out_payload_len > 0) {
        *out_payload = (char *)malloc(*out_payload_len);
        if (!*out_payload) { *disconnected = true; return false; }

        size_t payload_read = 0;
        while (payload_read < *out_payload_len) {
            int r = recv(fd, *out_payload + payload_read, (int)(*out_payload_len - payload_read), 0);
            if (r <= 0) {
                free(*out_payload);
                *out_payload = nullptr;
                *disconnected = true;
                return false;
            }
            payload_read += (size_t)r;
        }
    }

    return true;
}

// Translate a socket error code into a user-facing explanation
static const char *socket_error_hint(int err) {
#ifdef _WIN32
    switch (err) {
        case 10060: /* WSAETIMEDOUT */    return "Connection timed out. The host's firewall may be blocking the port.";
        case 10061: /* WSAECONNREFUSED */ return "Connection refused. The host may not be listening, or the IP/port is wrong.";
        case 10065: /* WSAEHOSTUNREACH */ return "Host unreachable. Check your network/VPN connection.";
        case 10051: /* WSAENETUNREACH */  return "Network unreachable. Check your network/VPN connection.";
        case 10064: /* WSAEHOSTDOWN */    return "Host is down. Check that the host device is online.";
        case 10048: /* WSAEADDRINUSE */   return "Port already in use. Another application may be using it.";
        case 10049: /* WSAEADDRNOTAVAIL */return "IP address not available on this machine. Check the IP.";
        default:                          return nullptr;
    }
#else
    switch (err) {
        case ETIMEDOUT:       return "Connection timed out. The host's firewall may be blocking the port.";
        case ECONNREFUSED:    return "Connection refused. The host may not be listening, or the IP/port is wrong.";
        case EHOSTUNREACH:    return "Host unreachable. Check your network/VPN connection.";
        case ENETUNREACH:     return "Network unreachable. Check your network/VPN connection.";
        case EADDRINUSE:      return "Port already in use. Another application may be using it.";
        case EADDRNOTAVAIL:   return "IP address not available on this machine. Check the IP.";
        default:              return nullptr;
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

// Close a socket if it's valid, then set to INVALID_SOCKET
static void close_socket(coop_socket_t *fd) {
    if (*fd != COOP_INVALID_SOCKET) {
        COOP_CLOSE_SOCKET(*fd);
        *fd = COOP_INVALID_SOCKET;
    }
}

// ---- Host Thread ----

static int SDLCALL host_thread_func(void *data) {
    CoopNetContext *ctx = (CoopNetContext *)data;

    // Heartbeat tracking
    Uint32 last_heartbeat = SDL_GetTicks();
    const Uint32 HEARTBEAT_INTERVAL_MS = 5000;
    const Uint32 HEARTBEAT_TIMEOUT_MS = 15000; // Disconnect if no response in 15s

    // Per-client last heartbeat ACK time
    Uint32 client_last_ack[COOP_MAX_CLIENTS];
    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        client_last_ack[i] = SDL_GetTicks();
    }

    log_message(LOG_INFO, "[COOP NET] Host thread started. Listening for connections.\n");

    while (!should_stop(ctx)) {
        // Build fd_set for select()
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(ctx->server_fd, &read_fds);

        coop_socket_t max_fd = ctx->server_fd;
        for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
            if (ctx->clients[i].active) {
                FD_SET(ctx->clients[i].socket_fd, &read_fds);
                if (ctx->clients[i].socket_fd > max_fd) {
                    max_fd = ctx->clients[i].socket_fd;
                }
            }
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms timeout

        int ready = select((int)(max_fd + 1), &read_fds, nullptr, nullptr, &tv);
        if (ready < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            log_message(LOG_ERROR, "[COOP NET] Host select() error.\n");
            break;
        }

        // Accept new connections
        if (ready > 0 && FD_ISSET(ctx->server_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            coop_socket_t new_fd = accept(ctx->server_fd, (struct sockaddr *)&client_addr, &addr_len);

            if (new_fd != COOP_INVALID_SOCKET) {
                set_nonblocking(new_fd);
                set_tcp_nodelay(new_fd);

                // Find a free slot
                int slot = -1;
                for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
                    if (!ctx->clients[i].active) {
                        slot = i;
                        break;
                    }
                }

                if (slot >= 0) {
                    ctx->clients[slot].socket_fd = new_fd;
                    ctx->clients[slot].active = true;
                    snprintf(ctx->clients[slot].label, sizeof(ctx->clients[slot].label),
                             "%s:%d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    client_last_ack[slot] = SDL_GetTicks();
                    ctx->client_count++;

                    log_message(LOG_INFO, "[COOP NET] Client connected: %s (slot %d, total: %d)\n",
                                ctx->clients[slot].label, slot, ctx->client_count);
                    set_status(ctx, "%d client(s) connected", ctx->client_count);
                } else {
                    // No room — reject
                    send_message(new_fd, COOP_MSG_DISCONNECT, nullptr, 0);
                    COOP_CLOSE_SOCKET(new_fd);
                    log_message(LOG_INFO, "[COOP NET] Rejected connection from %s:%d (server full).\n",
                                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                }
            }
        }

        // Read from connected clients
        for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
            if (!ctx->clients[i].active) continue;
            if (ready > 0 && FD_ISSET(ctx->clients[i].socket_fd, &read_fds)) {
                uint32_t msg_type;
                char *payload;
                uint32_t payload_len;
                bool disconnected;

                if (read_message(ctx->clients[i].socket_fd, &msg_type, &payload, &payload_len, &disconnected)) {
                    free(payload);

                    if (msg_type == COOP_MSG_HEARTBEAT_ACK) {
                        client_last_ack[i] = SDL_GetTicks();
                    } else if (msg_type == COOP_MSG_DISCONNECT) {
                        log_message(LOG_INFO, "[COOP NET] Client %s disconnected gracefully.\n",
                                    ctx->clients[i].label);
                        close_socket(&ctx->clients[i].socket_fd);
                        ctx->clients[i].active = false;
                        ctx->client_count--;
                        set_status(ctx, "%d client(s) connected", ctx->client_count);
                    }
                } else if (disconnected) {
                    log_message(LOG_INFO, "[COOP NET] Client %s connection lost.\n", ctx->clients[i].label);
                    close_socket(&ctx->clients[i].socket_fd);
                    ctx->clients[i].active = false;
                    ctx->client_count--;
                    set_status(ctx, "%d client(s) connected", ctx->client_count);
                }
            }
        }

        // Send heartbeats periodically
        Uint32 now = SDL_GetTicks();
        if (now - last_heartbeat >= HEARTBEAT_INTERVAL_MS) {
            last_heartbeat = now;
            for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
                if (!ctx->clients[i].active) continue;

                // Check for heartbeat timeout
                if (now - client_last_ack[i] > HEARTBEAT_TIMEOUT_MS) {
                    log_message(LOG_INFO, "[COOP NET] Client %s timed out (no heartbeat ACK).\n",
                                ctx->clients[i].label);
                    close_socket(&ctx->clients[i].socket_fd);
                    ctx->clients[i].active = false;
                    ctx->client_count--;
                    set_status(ctx, "%d client(s) connected", ctx->client_count);
                    continue;
                }

                if (!send_message(ctx->clients[i].socket_fd, COOP_MSG_HEARTBEAT, nullptr, 0)) {
                    log_message(LOG_INFO, "[COOP NET] Failed to send heartbeat to %s.\n",
                                ctx->clients[i].label);
                    close_socket(&ctx->clients[i].socket_fd);
                    ctx->clients[i].active = false;
                    ctx->client_count--;
                    set_status(ctx, "%d client(s) connected", ctx->client_count);
                }
            }
        }
    }

    // Graceful shutdown: notify all clients
    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        if (ctx->clients[i].active) {
            send_message(ctx->clients[i].socket_fd, COOP_MSG_DISCONNECT, nullptr, 0);
            close_socket(&ctx->clients[i].socket_fd);
            ctx->clients[i].active = false;
        }
    }
    ctx->client_count = 0;
    close_socket(&ctx->server_fd);

    log_message(LOG_INFO, "[COOP NET] Host thread exiting.\n");
    return 0;
}

// ---- Receiver Thread ----

static int SDLCALL receiver_thread_func(void *data) {
    CoopNetContext *ctx = (CoopNetContext *)data;

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
    addr.sin_port = htons((uint16_t)ctx->connect_port);

    if (inet_pton(AF_INET, ctx->connect_ip, &addr.sin_addr) != 1) {
        log_message(LOG_ERROR, "[COOP NET] Invalid receiver target IP: %s\n", ctx->connect_ip);
        close_socket(&ctx->client_fd);
        set_state(ctx, COOP_NET_ERROR);
        set_status(ctx, "Invalid IP address");
        return 1;
    }

    log_message(LOG_INFO, "[COOP NET] Receiver connecting to %s:%d...\n", ctx->connect_ip, ctx->connect_port);

    // Use non-blocking connect so we can check should_stop while waiting
    set_nonblocking(sock);

    int connect_result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
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
            log_message(LOG_ERROR, "[COOP NET] Immediate connect failure to %s:%d (error=%d)\n", ctx->connect_ip, ctx->connect_port, imm_err);
            close_socket(&ctx->client_fd);
            set_state(ctx, COOP_NET_ERROR);
            set_connect_error_status(ctx, imm_err);
            return 1;
        }
    } else {
        log_message(LOG_INFO, "[COOP NET] connect() succeeded immediately to %s:%d\n", ctx->connect_ip, ctx->connect_port);
    }

    // Wait for connection to complete (or should_stop / socket closed by stop)
    if (connect_pending) {
        bool connected = false;
        int connect_err = 0; // Stores the socket error if connection fails
        while (!should_stop(ctx)) {
            fd_set write_fds, err_fds;
            FD_ZERO(&write_fds);
            FD_ZERO(&err_fds);

            // Check if socket was closed by coop_net_stop
            if (ctx->client_fd == COOP_INVALID_SOCKET) break;

            FD_SET(ctx->client_fd, &write_fds);
            FD_SET(ctx->client_fd, &err_fds);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200000; // 200ms poll interval

            int ready = select((int)(ctx->client_fd + 1), nullptr, &write_fds, &err_fds, &tv);
            if (ready < 0) break;
            if (ready == 0) continue; // Timeout, loop and check should_stop

            if (FD_ISSET(ctx->client_fd, &err_fds)) {
                socklen_t len = sizeof(connect_err);
                getsockopt(ctx->client_fd, SOL_SOCKET, SO_ERROR, (char *)&connect_err, &len);
                log_message(LOG_ERROR, "[COOP NET] Connect error fd_set triggered (SO_ERROR=%d)\n", connect_err);
                break; // Connection failed
            }

            if (FD_ISSET(ctx->client_fd, &write_fds)) {
                // Check if connect actually succeeded via SO_ERROR
                socklen_t len = sizeof(connect_err);
                getsockopt(ctx->client_fd, SOL_SOCKET, SO_ERROR, (char *)&connect_err, &len);
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
                log_message(LOG_ERROR, "[COOP NET] Failed to connect to %s:%d (error=%d)\n", ctx->connect_ip, ctx->connect_port, connect_err);
                set_state(ctx, COOP_NET_ERROR);
                set_connect_error_status(ctx, connect_err);
            }
            close_socket(&ctx->client_fd);
            return 1;
        }
    }

    set_tcp_nodelay(ctx->client_fd);

    set_state(ctx, COOP_NET_CONNECTED);
    set_status(ctx, "Connected to %s:%d", ctx->connect_ip, ctx->connect_port);
    log_message(LOG_INFO, "[COOP NET] Receiver connected to %s:%d\n", ctx->connect_ip, ctx->connect_port);

    Uint32 last_heartbeat_recv = SDL_GetTicks();
    const Uint32 HEARTBEAT_TIMEOUT_MS = 20000; // Allow more slack on receiver side

    while (!should_stop(ctx)) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(ctx->client_fd, &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms

        int ready = select((int)(ctx->client_fd + 1), &read_fds, nullptr, nullptr, &tv);
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

        if (ready > 0 && FD_ISSET(ctx->client_fd, &read_fds)) {
            uint32_t msg_type;
            char *payload;
            uint32_t payload_len;
            bool disconnected;

            if (read_message(ctx->client_fd, &msg_type, &payload, &payload_len, &disconnected)) {
                if (msg_type == COOP_MSG_HEARTBEAT) {
                    // Respond with ACK
                    send_message(ctx->client_fd, COOP_MSG_HEARTBEAT_ACK, nullptr, 0);
                    last_heartbeat_recv = SDL_GetTicks();
                } else if (msg_type == COOP_MSG_DISCONNECT) {
                    free(payload);
                    log_message(LOG_INFO, "[COOP NET] Host sent disconnect.\n");
                    set_state(ctx, COOP_NET_DISCONNECTED);
                    set_status(ctx, "Host disconnected");
                    break;
                } else if (msg_type == COOP_MSG_STATE_UPDATE) {
                    // Copy payload into recv_buffer for main thread consumption
                    SDL_LockMutex(ctx->recv_mutex);
                    free(ctx->recv_buffer);
                    ctx->recv_buffer = payload;
                    ctx->recv_buffer_size = payload_len;
                    ctx->recv_data_ready = true;
                    SDL_UnlockMutex(ctx->recv_mutex);
                    payload = nullptr; // Ownership transferred
                    last_heartbeat_recv = SDL_GetTicks();
                } else if (msg_type == COOP_MSG_TEMPLATE_SYNC) {
                    // TODO: Step 6 — handle template sync handshake
                    free(payload);
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

        // Check heartbeat timeout
        Uint32 now = SDL_GetTicks();
        if (now - last_heartbeat_recv > HEARTBEAT_TIMEOUT_MS) {
            log_message(LOG_INFO, "[COOP NET] Host heartbeat timeout.\n");
            set_state(ctx, COOP_NET_DISCONNECTED);
            set_status(ctx, "Host timed out");
            break;
        }
    }

    // Graceful disconnect
    if (ctx->client_fd != COOP_INVALID_SOCKET) {
        send_message(ctx->client_fd, COOP_MSG_DISCONNECT, nullptr, 0);
        close_socket(&ctx->client_fd);
    }

    log_message(LOG_INFO, "[COOP NET] Receiver thread exiting.\n");
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
    if (!ctx->status_mutex || !ctx->recv_mutex) {
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
    free(ctx->recv_buffer);
    ctx->recv_buffer = nullptr;

#ifdef _WIN32
    if (wsa_initialized) {
        WSACleanup();
        wsa_initialized = false;
    }
#endif

    log_message(LOG_INFO, "[COOP NET] Networking context shut down.\n");
}

bool coop_net_start_host(CoopNetContext *ctx, const char *ip, int port) {
    // Stop any existing session
    coop_net_stop(ctx);

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
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

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

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
#ifdef _WIN32
        int bind_err = WSAGetLastError();
        log_message(LOG_ERROR, "[COOP NET] Failed to bind to %s:%d (WSA error=%d)\n", ip ? ip : "0.0.0.0", port, bind_err);
#else
        int bind_err = errno;
        log_message(LOG_ERROR, "[COOP NET] Failed to bind to %s:%d (errno=%d: %s)\n", ip ? ip : "0.0.0.0", port, bind_err, strerror(bind_err));
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
        if (getsockname(sock, (struct sockaddr *)&bound_addr, &bound_len) == 0) {
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
    set_status(ctx, "Listening on %s:%d", ip && ip[0] ? ip : "0.0.0.0", port);
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

bool coop_net_start_receiver(CoopNetContext *ctx, const char *ip, int port) {
    coop_net_stop(ctx);

    // Store target address for the thread to use (connect happens in thread to avoid UI freeze)
    strncpy(ctx->connect_ip, ip, sizeof(ctx->connect_ip) - 1);
    ctx->connect_ip[sizeof(ctx->connect_ip) - 1] = '\0';
    ctx->connect_port = port;

    set_state(ctx, COOP_NET_CONNECTING);
    set_status(ctx, "Connecting to %s:%d...", ip, port);
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
    if (!ctx->thread) return;

    SDL_SetAtomicInt(&ctx->should_stop, 1);

    // Close sockets from outside the thread to unblock select() / non-blocking connect()
    close_socket(&ctx->server_fd);
    close_socket(&ctx->client_fd);

    SDL_WaitThread(ctx->thread, nullptr);
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

    // Free receive buffer
    SDL_LockMutex(ctx->recv_mutex);
    free(ctx->recv_buffer);
    ctx->recv_buffer = nullptr;
    ctx->recv_buffer_size = 0;
    ctx->recv_data_ready = false;
    SDL_UnlockMutex(ctx->recv_mutex);

    set_state(ctx, COOP_NET_IDLE);
    set_status(ctx, "Idle");

    log_message(LOG_INFO, "[COOP NET] Networking stopped.\n");
}

void coop_net_tick(CoopNetContext *ctx) {
    // Currently a placeholder for main-thread processing.
    // In Step 5, this will check recv_data_ready and apply received state.
    (void)ctx;
}

CoopNetState coop_net_get_state(CoopNetContext *ctx) {
    return (CoopNetState)SDL_GetAtomicInt(&ctx->state);
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
    if (ctx->client_count == 0) return true; // Nothing to send, but not an error

    bool all_ok = true;
    for (int i = 0; i < COOP_MAX_CLIENTS; i++) {
        if (!ctx->clients[i].active) continue;
        if (!send_message(ctx->clients[i].socket_fd, COOP_MSG_STATE_UPDATE, data, (uint32_t)size)) {
            log_message(LOG_ERROR, "[COOP NET] Failed to broadcast to client %s.\n", ctx->clients[i].label);
            close_socket(&ctx->clients[i].socket_fd);
            ctx->clients[i].active = false;
            ctx->client_count--;
            set_status(ctx, "%d client(s) connected", ctx->client_count);
            all_ok = false;
        }
    }
    return all_ok;
}