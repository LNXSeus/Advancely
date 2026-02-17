// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 24.06.2025.
//

// -----------------------------------------------------------------------------------------------------------------
// MAIN.H IS CALLED FROM MAIN.CPP, SETTINGS_UTILS.CPP AND TRACKER.CPP because of show_error_message function
// -----------------------------------------------------------------------------------------------------------------

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

// Enum to track the specific reason for forcing the settings window open on startup.
typedef enum {
    FORCE_OPEN_NONE,
    FORCE_OPEN_AUTO_FAIL,
    FORCE_OPEN_MANUAL_FAIL
} ForceOpenReason;

// Global error message function, accessible for other files
void show_error_message(const char *title, const char *message);

const char* get_resources_path();
const char* get_application_dir();
const char* get_settings_file_path();
const char* get_notes_dir_path();
const char* get_notes_manifest_path();

// Set my own SDL_FLAGS
#define SDL_FLAGS SDL_INIT_VIDEO

#define TRACKER_TITLE "Advancely"

// This is the version that gets compared with the latest release tag on GitHub
// This automatically applies versioning to metainfo.xml through GitHub Actions.
// TODO: ONLY UPDATE PKGBUILD FOR RELEASES!!! FOR BIG RELEASES EDIT metainfo.xml MANUALLY
#define ADVANCELY_VERSION "v1.0.31" // vMAJOR.MINOR.PATCH // Update this always, SAME FORMAT ON RELEASE TAG!
#define ADVANCELY_ICON_PATH "/gui/Advancely_Logo_NoText.png" // Starting from /gui folder
#define ADVANCELY_LOGO_PATH "/gui/Advancely_Logo.png" // Starting from /gui folder
#define ADVANCELY_LOGO_SIZE 512.0f // Logo size on startup message window or update successful window
#define ADVANCELY_FADED_ALPHA 100
#define TRACKER_SEPARATOR_LINE_WIDTH 0.80f

// Framewise delay for GIFs if they have no timing information
// Used in load_animated_gif() function in tracker.cpp
#define DEFAULT_GIF_DELAY_MS 100 // (10 frames per second)

#define OVERLAY_TITLE "Advancely Overlay"
#define OVERLAY_FIXED_HEIGHT 420
#define OVERLAY_DEFAULT_WIDTH 1440
#define OVERLAY_SPEEDUP_FACTOR 5.0f // Both the setting and SPACEBAR use this value
#define OVERLAY_FADE_DURATION 0.5f // Duration of the fade-out animation in seconds

#define MAX_PATH_LENGTH 1024


#ifdef __cplusplus
}
#endif

#endif //MAIN_H
