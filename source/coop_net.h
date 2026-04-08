// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 07.04.2026.
//

#ifndef COOP_NET_H
#define COOP_NET_H

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Connection States ----

enum CoopNetState {
    COOP_NET_IDLE,          // Not started
    COOP_NET_LISTENING,     // Host: server socket open, waiting for clients
    COOP_NET_CONNECTING,    // Receiver: attempting connection to host
    COOP_NET_CONNECTED,     // Receiver: connected to host
    COOP_NET_DISCONNECTED,  // Clean disconnect (remote closed or stop called)
    COOP_NET_ERROR          // Fatal error (bind failed, connect refused, etc.)
};

// ---- Message Protocol ----
// Wire format: [4 bytes type (network order)] [4 bytes length (network order)] [payload]

enum CoopMsgType {
    COOP_MSG_HEARTBEAT     = 1, // Keepalive ping (empty payload)
    COOP_MSG_HEARTBEAT_ACK = 2, // Keepalive response (empty payload)
    COOP_MSG_DISCONNECT    = 3, // Graceful disconnect notification (empty payload)
    COOP_MSG_STATE_UPDATE  = 4, // Serialized tracker state (Step 5)
    COOP_MSG_TEMPLATE_SYNC = 5  // Template handshake data (Step 6)
};

#define COOP_MSG_HEADER_SIZE 8 // 4 bytes type + 4 bytes length

// ---- Cross-platform socket type ----
// On Windows, SOCKET is UINT_PTR (8 bytes on x64). We mirror that size without
// pulling in winsock2.h (which must precede windows.h and can conflict).
#ifdef _WIN32
    #include <stddef.h>
    typedef size_t coop_socket_t; // Same width as SOCKET (UINT_PTR)
    #define COOP_INVALID_SOCKET ((coop_socket_t)(~0))
#else
    typedef int coop_socket_t;
    #define COOP_INVALID_SOCKET (-1)
#endif

// ---- Data Structures ----

#define COOP_MAX_CLIENTS 31 // MAX_COOP_PLAYERS (32) minus the host

typedef struct {
    coop_socket_t socket_fd; // Client socket descriptor
    char label[64];          // Display label e.g. "192.168.1.5:12345"
    bool active;             // Slot is in use
} CoopClient;

typedef struct {
    // -- State (atomically readable from any thread) --
    SDL_AtomicInt state;        // CoopNetState
    SDL_AtomicInt should_stop;  // Signal for network thread to shut down

    // -- Host fields --
    coop_socket_t server_fd;                // Listening socket (COOP_INVALID_SOCKET when unused)
    CoopClient clients[COOP_MAX_CLIENTS];
    int client_count;

    // -- Receiver fields --
    coop_socket_t client_fd; // Connection to host (COOP_INVALID_SOCKET when unused)
    char connect_ip[64];   // Target IP for receiver connect (set before thread spawn)
    int connect_port;      // Target port for receiver connect

    // -- Threading --
    SDL_Thread *thread; // The network thread (host or receiver)

    // -- Status message (mutex-protected, for UI display) --
    SDL_Mutex *status_mutex;
    char status_msg[256];

    // -- Receive buffer (Receiver side: filled by net thread, consumed by main thread) --
    SDL_Mutex *recv_mutex;
    char *recv_buffer;
    size_t recv_buffer_size;
    bool recv_data_ready;
} CoopNetContext;

// ---- Public API ----

// Initialize the context (zero state, create mutexes). Call once at app startup.
bool coop_net_init(CoopNetContext *ctx);

// Tear down everything (stop thread, close sockets, destroy mutexes). Call at app shutdown.
void coop_net_shutdown(CoopNetContext *ctx);

// Start hosting on the given IP and port. Spawns the host thread.
bool coop_net_start_host(CoopNetContext *ctx, const char *ip, int port);

// Connect to a host at the given IP and port. Spawns the receiver thread.
bool coop_net_start_receiver(CoopNetContext *ctx, const char *ip, int port);

// Gracefully stop the current session (host or receiver). Blocks until the thread exits.
void coop_net_stop(CoopNetContext *ctx);

// Call once per frame from the main thread. Lightweight — checks state, processes received data.
void coop_net_tick(CoopNetContext *ctx);

// Get the current connection state (thread-safe atomic read).
CoopNetState coop_net_get_state(CoopNetContext *ctx);

// Copy the human-readable status message into out (mutex-protected).
void coop_net_get_status_msg(CoopNetContext *ctx, char *out, size_t out_size);

// Get the number of currently connected clients (Host only).
int coop_net_get_client_count(CoopNetContext *ctx);

// Broadcast data to all connected clients (Host only). Returns false if not hosting.
// Payload is sent with COOP_MSG_STATE_UPDATE type.
bool coop_net_broadcast(CoopNetContext *ctx, const void *data, size_t size);

// ---- Global pointer (set in main.cpp so settings.cpp can read state) ----
extern CoopNetContext *g_coop_ctx;

#ifdef __cplusplus
}
#endif

#endif // COOP_NET_H