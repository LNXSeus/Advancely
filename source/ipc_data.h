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

// Define a large, fixed-size buffer for our serialized data.
// 64MB should be more than enough for any template.
#define SHARED_BUFFER_SIZE (64 * 1024 * 1024)

// The new struct holds the size of the data and the data itself as a raw byte buffer.
typedef struct {
    size_t data_size;
    char buffer[SHARED_BUFFER_SIZE];
} SharedData;


#endif //IPC_DATA_H
