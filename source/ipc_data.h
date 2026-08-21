// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 31.08.2025.
//


// Inter-Process Communication -> To communicate between tracker and overlay window, overlay is always reading


#ifndef IPC_DATA_H
#define IPC_DATA_H

#include "data_structures.h" // For TemplateData
#include "main.h" // For MAX_PATH_LENGTH

// These names are the "keys" that allow the two separate processes
// to find the same shared memory block and mutex
#define SHARED_MEM_NAME "AdvancelySharedMemory"
#define MUTEX_NAME "AdvancelyMutex"

// Guard held by whichever process is currently running the overlay window, no matter who started
// it. The tracker checks it before spawning its own overlay child so a manually launched
// "Advancely --overlay" (needed for Waywall, where the overlay has to be its own process to be
// captured into the game) never gets a second overlay stacked on top of it. Windows uses a named
// mutex, Linux/macOS an flock'd file next to settings.json; both are released by the OS when the
// overlay process dies, so a crashed overlay never leaves a stale guard behind.
#define OVERLAY_INSTANCE_MUTEX_NAME "AdvancelyOverlayInstanceMutex"

// Define a large, fixed-size buffer for our serialized data.
// 64MB should be more than enough for any template.
#define SHARED_BUFFER_SIZE (64 * 1024 * 1024)

// The new struct holds the size of the data and the data itself as a raw byte buffer.
typedef struct {
    size_t data_size;
    bool shutdown_requested; // To gracefully close the overlay process and finish its log file
    char buffer[SHARED_BUFFER_SIZE];
} SharedData;


#endif //IPC_DATA_H
