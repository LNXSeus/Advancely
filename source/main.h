//
// Created by Linus on 24.06.2025.
//

#ifndef MAIN_H
#define MAIN_H

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h> // Load with IMG_Load and get *SDL_Surface, SDL_SetWindowIcon (maybe for overlay idk) -> https://youtu.be/EP6EwVwfCiU?t=1200
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>
#include <string.h> // For strncpy, strtok, strcspn, strstr, strlen, strcat, snprintf
#include <stdio.h> // For exit, EXIT_FAILURE, EXIT_SUCCESS, Memory Management
#include <stdlib.h>

#include <cJSON.h>

// Not including any utility headers here



// Set my own SDL_FLAGS
#define SDL_FLAGS SDL_INIT_VIDEO // TODO: OR'd together with SDL_INIT_AUDIO

#define SDL_TRACKER_WINDOW_FLAGS SDL_WINDOW_HIGH_PIXEL_DENSITY // SDL_WINDOW_ALWAYS_ON_TOP // SDL_WINDOW_FULLSCREEN, SDL_WINDOW_RESIZABLE, TODO: Make this a setting
#define TRACKER_TITLE "Advancely" // TODO: Dynamically update title based on progress
#define TRACKER_WIDTH 1440 // 16:9
#define TRACKER_HEIGHT 900
#define TRACKER_BACKGROUND_COLOR (SDL_Color){13, 17, 23, 255} // not a pointer so accessing with .r, .g, .b, .a

#define SDL_SETTINGS_WINDOW_FLAGS SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_HIGH_PIXEL_DENSITY
#define SETTINGS_TITLE "Advancely Settings"
#define SETTINGS_WIDTH 1280
#define SETTINGS_HEIGHT 720
#define SETTINGS_BACKGROUND_COLOR (SDL_Color){13, 17, 23, 255}

#define SDL_OVERLAY_WINDOW_FLAGS SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_BORDERLESS
#define OVERLAY_TITLE "Advancely Overlay" // Also needs the icon
#define OVERLAY_TITLE_BAR_HEIGHT 26
#define OVERLAY_WIDTH 1440
#define OVERLAY_HEIGHT (420 + OVERLAY_TITLE_BAR_HEIGHT)
#define OVERLAY_BACKGROUND_COLOR (SDL_Color){0, 80, 255, 255}

#define OVERLAY_SPEEDUP_FACTOR 3


// Used in path_utils.h, settings_utils.h
#define MAX_PATH_LENGTH 1024 // Also defined in path_utils.h

// PATH DETECTION MODE IN SETTINGS -> AUTO OR MANUAL -> path_utils.h

// #define CONFIG_PATH "resources/config/settings.json" // ENGLISH ONLY

#define FPS 60.0f
#define FRAME_TARGET_TIME (1000.0f / FPS)


#endif //MAIN_H
