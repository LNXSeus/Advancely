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



// Global error message function, accessible for other files
void show_error_message(const char *title, const char *message);

// Not including any utility headers here

const char* get_resources_path();
const char* get_settings_file_path();
const char* get_notes_dir_path();
const char* get_notes_manifest_path();

// Set my own SDL_FLAGS
#define SDL_FLAGS SDL_INIT_VIDEO // TODO: OR'd together with SDL_INIT_AUDIO

#define TRACKER_TITLE "Advancely"

// This is the version that gets compared with the latest release tag on GitHub
#define ADVANCELY_VERSION "v0.9.140" // vMAJOR.MINOR.PATCH // TODO: Update this always, SAME FORMAT ON RELEASE TAG!
// Starting from /gui folder
#define ADVANCELY_ICON_PATH "/gui/Advancely_Logo_NoText.png" // TODO: Use this in tracker_init_sdl()
// Starting from /gui folder
#define ADVANCELY_LOGO_PATH "/gui/Advancely_Logo.png"
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
