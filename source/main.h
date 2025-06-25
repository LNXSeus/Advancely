//
// Created by Linus on 24.06.2025.
//

#ifndef MAIN_H
#define MAIN_H

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h> // Load with IMG_Load and get *SDL_Surface
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>
#include <string.h> // For strncpy, strtok, strcspn, strstr, strlen, strcat, snprintf
#include <stdio.h> // For exit, EXIT_FAILURE, EXIT_SUCCESS, Memory Management
#include <stdlib.h>

#include <cJSON.h>

#include "init_sdl.h" // load h file after all includes


// Set my own SDL_FLAGS
#define SDL_FLAGS SDL_INIT_VIDEO // TODO: OR'd together with SDL_INIT_AUDIO
#define SDL_TRACKER_WINDOW_FLAGS 0 // SDL_WINDOW_FULLSCREEN, SDL_WINDOW_RESIZABLE

#define TRACKER_TITLE "Advancely" // TODO: Dynamically update title based on progress
#define TRACKER_WIDTH 1440 // 16:9
#define TRACKER_HEIGHT 960

#define OVERLAY_TITLE "Stream Overlay" // Also needs the icon
#define OVERLAY_WIDTH 1440
#define OVERLAY_HEIGHT 420

#define CONFIG_PATH "resources/config/settings.json" // ENGLISH ONLY

#define FPS 60.0f
#define FRAME_TARGET_TIME (1000.0f / FPS)




#endif //MAIN_H
