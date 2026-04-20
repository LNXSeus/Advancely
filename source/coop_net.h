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
    COOP_NET_IDLE, // Not started
    COOP_NET_LISTENING, // Host: server socket open, waiting for clients
    COOP_NET_CONNECTING, // Receiver: attempting connection to host
    COOP_NET_CONNECTED, // Receiver: connected to host
    COOP_NET_DISCONNECTED, // Clean disconnect (remote closed or stop called)
    COOP_NET_ERROR // Fatal error (bind failed, connect refused, etc.)
};

// ---- Message Protocol ----
// Wire format: [4 bytes type (network order)] [4 bytes length (network order)] [payload]

enum CoopMsgType {
    COOP_MSG_HEARTBEAT = 1, // Keepalive ping (empty payload)
    COOP_MSG_HEARTBEAT_ACK = 2, // Keepalive response (empty payload)
    COOP_MSG_DISCONNECT = 3, // Graceful disconnect notification (empty payload)
    COOP_MSG_STATE_UPDATE = 4, // Serialized tracker state
    COOP_MSG_TEMPLATE_SYNC = 5, // Template handshake data
    COOP_MSG_JOIN_REQUEST = 6, // Receiver -> Host: JSON {uuid, username, display_name}
    COOP_MSG_JOIN_ACCEPT = 7, // Host -> Receiver: JSON lobby player list
    COOP_MSG_JOIN_REJECT = 8, // Host -> Receiver: reason string, then close
    COOP_MSG_PLAYER_LIST = 9, // Host -> All receivers: JSON player list on any change
    COOP_MSG_KICK = 10, // Host -> Receiver: reason string, then close
    COOP_MSG_CUSTOM_GOAL_MOD = 11, // Receiver -> Host: custom goal/stat checkbox modification
    COOP_MSG_PLAYER_STATES = 12, // Host -> All receivers: per-player progress snapshots
    // Receiver -> Host: raw stats file upload for legacy (<=1.6.4) merging ONLY.
    // Payload: [2B uuid_len][uuid_utf8][2B world_len][world_utf8][4B file_len][file_bytes].
    // Mid-era and modern versions do not use this path (the host reads receiver files
    // directly via the world folder on a shared save). Legacy stores global .dat files
    // per-player under the launcher install dir, so the host must pull them over the wire.
    COOP_MSG_LEGACY_STATS_UPLOAD = 13
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

#define COOP_MAX_CLIENTS 31  // MAX_COOP_PLAYERS (32) minus the host
#define COOP_MAX_LOBBY   32  // Host + all clients

// A player entry in the lobby display (used by both host and receiver UI)
typedef struct {
    char username[64];
    char uuid[48];
    char display_name[64];
    bool is_host;
} CoopLobbyPlayer;

// Custom goal/stat checkbox modification actions (Receiver -> Host)
enum CoopGoalModAction {
    COOP_MOD_TOGGLE = 0, // Toggle a checkbox (custom goal or stat)
    COOP_MOD_INCREMENT = 1, // Increment a counter
    COOP_MOD_DECREMENT = 2, // Decrement a counter
    COOP_MOD_SET_VALUE = 3 // Set a specific value
};

typedef struct {
    char goal_root_name[192]; // root_name of the custom goal or stat
    char parent_root_name[192]; // parent root_name for sub-stats (empty = top-level)
    int action; // CoopGoalModAction
    int value; // For SET_VALUE action
    char source_uuid[48]; // UUID of the player whose per-player state this mod targets
} CoopCustomGoalModMsg;

#define COOP_MAX_CUSTOM_MODS 64

// Pending action the UI thread requests the host thread to carry out
enum CoopClientAction {
    COOP_ACTION_NONE = 0,
    COOP_ACTION_KICK,
    COOP_ACTION_REJECT,
};

typedef struct {
    coop_socket_t socket_fd; // Client socket descriptor
    char label[64]; // Display label e.g. "192.168.1.5:12345"
    bool active; // Slot is in use
    bool handshake_done; // JOIN_REQUEST validated and approved by host
    bool pending_approval; // Waiting for host to accept/reject
    char username[64]; // From handshake
    char uuid[48]; // From handshake
    char display_name[64]; // From handshake
    Uint32 connect_time; // When the socket was accepted (for handshake timeout)
    SDL_AtomicInt pending_action; // CoopClientAction: set by UI thread, processed by host thread
    char pending_action_reason[128]; // Reason string for kick/reject
} CoopClient;

// A pending join request shown in the host UI for approval
typedef struct {
    int client_slot; // Which client slot this request is from
    char username[64];
    char uuid[48];
    char display_name[64];
} CoopJoinRequest;

typedef struct {
    // -- State (atomically readable from any thread) --
    SDL_AtomicInt state; // CoopNetState
    SDL_AtomicInt should_stop; // Signal for network thread to shut down

    // -- Host fields --
    coop_socket_t server_fd; // Listening socket (COOP_INVALID_SOCKET when unused)
    CoopClient clients[COOP_MAX_CLIENTS];
    int client_count; // Handshaked (approved) clients only

    // -- Host identity (copied from settings at start) --
    char host_username[64];
    char host_uuid[48];
    char host_display_name[64];

    // -- Receiver fields --
    coop_socket_t client_fd; // Connection to host (COOP_INVALID_SOCKET when unused)
    char connect_ip[64]; // Target IP for receiver connect (set before thread spawn)
    int connect_port; // Target port for receiver connect

    // -- Receiver identity (set before starting receiver thread) --
    char connect_username[64];
    char connect_uuid[48];
    char connect_display_name[64];

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

    // -- Persistent merged snapshot (Receiver side: kept so dropdown switch can re-apply) --
    char *recv_merged_snapshot;
    size_t recv_merged_snapshot_size;

    // -- Per-player progress snapshots (Receiver side: one buffer per coop player) --
    char *recv_player_buffers[COOP_MAX_LOBBY];
    size_t recv_player_buffer_sizes[COOP_MAX_LOBBY];
    int recv_player_snapshot_count;
    bool recv_player_data_ready;

    // -- Lobby player list (mutex-protected, for UI display) --
    // Written by net thread, read by UI thread. Both host and receiver populate this.
    SDL_Mutex *lobby_mutex;
    CoopLobbyPlayer lobby_players[COOP_MAX_LOBBY];
    int lobby_player_count;
    bool lobby_changed;

    // -- Pending join requests (host only, mutex-protected via lobby_mutex) --
    CoopJoinRequest pending_requests[COOP_MAX_CLIENTS];
    int pending_request_count;
    bool pending_requests_changed;

    // -- Custom goal modification queue (host only, mutex-protected) --
    // Receivers send COOP_MSG_CUSTOM_GOAL_MOD messages; host thread queues them here.
    SDL_Mutex *custom_mod_mutex;
    CoopCustomGoalModMsg custom_mod_queue[COOP_MAX_CUSTOM_MODS];
    int custom_mod_count;

    // -- Legacy stats upload cache (host only, mutex-protected) --
    // LEGACY (<=1.6.4) ONLY. Each connected receiver pushes their raw stats .dat
    // to the host here so merge can read every player's global stats without
    // touching disk. Latest-wins per UUID.
    SDL_Mutex *legacy_upload_mutex;
    struct LegacyUploadEntry {
        char uuid[48];
        char world_name[256];
        void *bytes;          // malloc'd, freed on replace or context destroy
        uint32_t size;
        Uint64 last_update_ms;
    } legacy_uploads[COOP_MAX_CLIENTS];
    int legacy_upload_count;
    // Set by the net thread on a new legacy upload; main thread check-and-clears
    // it each frame to force a tracker re-merge (see coop_net_legacy_upload_consume).
    SDL_AtomicInt legacy_upload_pending;

    // -- Template sync (host sets, sent on approve; receiver receives and stores) --
    SDL_Mutex *template_sync_mutex;
    char template_sync_payload[1024]; // JSON string with version, category, optional_flag, merge settings
    bool template_sync_ready; // Receiver: true when new sync data available for main thread

    // -- Disconnect reason (receiver sets before stopping, sent in the thread's cleanup path) --
    char disconnect_reason[128]; // Non-empty = include as payload in DISCONNECT message
} CoopNetContext;

// ---- Public API ----

// Initialize the context (zero state, create mutexes). Call once at app startup.
bool coop_net_init(CoopNetContext *ctx);

// Tear down everything (stop thread, close sockets, destroy mutexes). Call at app shutdown.
void coop_net_shutdown(CoopNetContext *ctx);

// Start hosting with the host's identity. Spawns the host thread.
bool coop_net_start_host(CoopNetContext *ctx, const char *ip, int port,
                         const char *username, const char *uuid, const char *display_name);

// Connect to a host with the receiver's identity. Spawns the receiver thread.
bool coop_net_start_receiver(CoopNetContext *ctx, const char *ip, int port,
                             const char *username, const char *uuid, const char *display_name);

// Gracefully stop the current session (host or receiver). Blocks until the thread exits.
void coop_net_stop(CoopNetContext *ctx);

// Call once per frame from the main thread. Lightweight - checks state, processes received data.
void coop_net_tick(CoopNetContext *ctx);

// Get the current connection state (thread-safe atomic read).
CoopNetState coop_net_get_state(CoopNetContext *ctx);

// Copy the human-readable status message into out (mutex-protected).
void coop_net_get_status_msg(CoopNetContext *ctx, char *out, size_t out_size);

// Get the number of currently connected (approved) clients (Host only).
int coop_net_get_client_count(CoopNetContext *ctx);

// Broadcast data to all connected clients (Host only). Returns false if not hosting.
// Payload is sent with COOP_MSG_STATE_UPDATE type.
bool coop_net_broadcast(CoopNetContext *ctx, const void *data, size_t size);

// Broadcast per-player progress snapshots to all connected clients (Host only).
// payload_data is an array of {buffer, size} pairs, one per player.
// Format on wire: [4B player_count] then for each player [4B idx][4B size][data].
bool coop_net_broadcast_player_states(CoopNetContext *ctx,
                                      char **player_buffers, size_t *player_sizes,
                                      int player_count);

// ---- Lobby & Join Request API ----

// Get a thread-safe snapshot of the lobby player list. Returns player count.
int coop_net_get_lobby_players(CoopNetContext *ctx, CoopLobbyPlayer *out_players, int max_players);

// Check if the lobby list has changed since last call (check-and-clear).
bool coop_net_lobby_changed(CoopNetContext *ctx);

// Get a thread-safe snapshot of pending join requests (Host only). Returns count.
int coop_net_get_pending_requests(CoopNetContext *ctx, CoopJoinRequest *out_requests, int max_requests);

// Check if pending requests changed since last call (check-and-clear).
bool coop_net_pending_requests_changed(CoopNetContext *ctx);

// Host approves a pending join request by client slot index.
bool coop_net_approve_request(CoopNetContext *ctx, int client_slot);

// Host rejects a pending join request by client slot index.
bool coop_net_reject_request(CoopNetContext *ctx, int client_slot, const char *reason);

// Host kicks a connected (approved) client by slot index.
bool coop_net_kick_client(CoopNetContext *ctx, int client_slot, const char *reason);

// ---- Custom Goal Modification API ----

// Receiver sends a custom goal modification to the host. Returns false if not connected.
bool coop_net_send_custom_goal_mod(CoopNetContext *ctx, const CoopCustomGoalModMsg *msg);

// Host drains queued custom goal modifications (thread-safe). Returns count written to out_mods.
int coop_net_drain_custom_mods(CoopNetContext *ctx, CoopCustomGoalModMsg *out_mods, int max_mods);

// ---- Template Sync API ----

// Host: set the template sync payload (JSON). Called from main thread whenever settings change.
void coop_net_set_template_sync(CoopNetContext *ctx, const char *json_payload);

// Receiver: check-and-clear template sync availability. Returns true if new data is ready.
bool coop_net_template_sync_ready(CoopNetContext *ctx);

// Receiver: copy the template sync payload into out buffer. Returns true if data was available.
bool coop_net_get_template_sync(CoopNetContext *ctx, char *out, size_t out_size);

// Host: broadcast current template sync payload to all connected (approved) clients.
void coop_net_broadcast_template_sync(CoopNetContext *ctx);

// Receiver: send a DISCONNECT message with an optional reason string, then stop.
// The host will display the reason in its status area.
void coop_net_disconnect_with_reason(CoopNetContext *ctx, const char *reason);

// ---- Legacy Stats Upload API ----
// LEGACY (<=1.6.4) ONLY. Mid-era and modern versions read receiver data from a
// shared save folder; legacy stats live outside the save in the launcher install
// dir, so receivers must push their file bytes to the host for merging.

// Receiver: upload raw stats file bytes to the host. Safe to call when not connected
// or on non-legacy versions (returns false without sending).
bool coop_net_send_legacy_stats_upload(CoopNetContext *ctx,
                                       const char *uuid,
                                       const char *world_name,
                                       const void *file_bytes,
                                       uint32_t file_len);

// Host: copy the latest uploaded stats bytes for a given UUID into out_bytes (malloc'd,
// caller frees). Returns false if no upload exists for that UUID. Legacy-only.
bool coop_net_get_legacy_stats_upload(CoopNetContext *ctx,
                                      const char *uuid,
                                      void **out_bytes,
                                      uint32_t *out_size,
                                      char *out_world_name,
                                      size_t world_name_cap);

// Host: atomically check-and-clear the "new legacy upload arrived" flag. Main thread
// calls this every frame and triggers a re-merge when it returns true. Legacy-only.
bool coop_net_legacy_upload_consume(CoopNetContext *ctx);

// ---- Room Code (Base64-encoded IP:PORT) ----

// Encode IP:port into a Base64 room code. Returns true on success.
bool coop_encode_room_code(const char *ip, int port, char *out_code, size_t code_max_len);

// Decode a Base64 room code back into IP and port. Returns true on success.
bool coop_decode_room_code(const char *code, char *out_ip, size_t ip_max_len, int *out_port);

// ---- Global pointer (set in main.cpp so settings.cpp can read state) ----
extern CoopNetContext *g_coop_ctx;

#ifdef __cplusplus
}
#endif

#endif // COOP_NET_H
