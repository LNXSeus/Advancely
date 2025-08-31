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

// This struct defines the exact data that will be sent from
// tracker process to the overlay process

typedef struct {
    TemplateData template_data;
    char world_name[MAX_PATH_LENGTH];
    float time_since_last_update;
} SharedData;

#endif //IPC_DATA_H
