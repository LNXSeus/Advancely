//
// Created by Linus on 24.06.2025.
//

#ifndef MAIN_H
#define MAIN_H

#include <cstring> // For string manipulation functions like printf
#include <cstdio> // For standard input/output functions like printf
#include <cstdlib> // For general utilities like malloc, free, exit, ..

// These are C++ compatible as they have their own extern "C" guards
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h> // Load with IMG_Load and get *SDL_Surface, SDL_SetWindowIcon (maybe for overlay idk) -> https://youtu.be/EP6EwVwfCiU?t=1200
#include <SDL3_ttf/SDL_ttf.h> // TODO: Remove this later


#include "imstb_truetype.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/imgui_impl_sdlrenderer3.h"

#ifdef __cplusplus
extern "C" {
#endif

// Not including any utility headers here

// Set my own SDL_FLAGS
#define SDL_FLAGS SDL_INIT_VIDEO // TODO: OR'd together with SDL_INIT_AUDIO

#define TRACKER_TITLE "Advancely"
#define ADVANCELY_VERSION "v0.8.4" // vMAJOR.MINOR.PATCH // TODO: Update this always
#define ADVANCELY_ICON_PATH "resources/gui/Advancely_Logo_NoText.png" // TODO: Use this in tracker_init_sdl()
#define ADVANCELY_FADED_ALPHA 100
#define TRACKER_SEPARATOR_LINE_WIDTH 0.80f

// Settings window is NOT USER CONFIGURABLE SO HARDCODED HERE, ONLY COLOR IS
// Settings window will be part of tracker window
// Tracker window flags are in init_sdl.c in tracker_init_sdl()

// TODO: Settings window flags are in settings.cpp
// #define SETTINGS_TITLE "Advancely Settings"
// #define SETTINGS_WIDTH 1280
// #define SETTINGS_HEIGHT 720

#define OVERLAY_TITLE "Advancely Overlay"

// Not user-configurable, so it remains a define
#define OVERLAY_SPEEDUP_FACTOR 3.0f
#define MAX_PATH_LENGTH 1024

#ifdef __cplusplus
}
#endif

#endif //MAIN_H
