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

#define TRACKER_TITLE "Advancely"
#define ADVANCELY_VERSION "v0.5.6" // vMAJOR.MINOR.PATCH // TODO: Update this always
#define ADVANCELY_ICON_PATH "resources/gui/Advancely_Logo_NoText.png" // TODO: Use this in tracker_init_sdl()

// Settings window is NOT USER CONFIGURABLE SO HARDCODED HERE, ONLY COLOR IS
// Tracker window flags are in init_sdl.c in tracker_init_sdl()
#define SDL_SETTINGS_WINDOW_FLAGS (SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_ALWAYS_ON_TOP)
#define SETTINGS_TITLE "Advancely Settings"
#define SETTINGS_WIDTH 1280
#define SETTINGS_HEIGHT 720


#define OVERLAY_TITLE "Advancely Overlay"

// Not user-configurable, so it remains a define
#define OVERLAY_SPEEDUP_FACTOR 3.0f
#define MAX_PATH_LENGTH 1024


#endif //MAIN_H
