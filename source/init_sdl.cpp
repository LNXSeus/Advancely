// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 24.06.2025.
//

#include "init_sdl.h" //  C++ compatible header
#include "logger.h"

#include "tracker.h"
#include "overlay.h"
#include "settings.h"
#include "settings_utils.h"

#include <cstdio> // Standard I/O

bool tracker_init_sdl(Tracker *t, const AppSettings *settings) {
    if (!SDL_Init(SDL_FLAGS)) {
        log_message(LOG_ERROR, "[INIT SDL] Failed to initialize SDL3: %s\n", SDL_GetError());
        return false;
    }

    // First create without he ALWAYS_ON_TOP flag
    Uint32 window_flags = SDL_WINDOW_RESIZABLE;

    // Use loaded settings for window creation, with SDL_WINDOWPOS_CENTERED as a fallback
    int x = (settings->tracker_window.x == DEFAULT_WINDOW_POS)
                ? (int) SDL_WINDOWPOS_CENTERED
                : settings->tracker_window.x;
    int y = (settings->tracker_window.y == DEFAULT_WINDOW_POS)
                ? (int) SDL_WINDOWPOS_CENTERED
                : settings->tracker_window.y;
    int w = (settings->tracker_window.w == DEFAULT_WINDOW_SIZE) ? 1440 : settings->tracker_window.w;
    int h = (settings->tracker_window.h == DEFAULT_WINDOW_SIZE) ? 900 : settings->tracker_window.h;

    t->window = SDL_CreateWindow(TRACKER_TITLE, w, h, window_flags);
    if (!t->window) {
        log_message(LOG_ERROR, "[INIT SDL] Failed to create tracker window: %s\n", SDL_GetError());
        return false;
    }

    // Window Icon Cross-platform
    char icon_path[MAX_PATH_LENGTH];
    snprintf(icon_path, sizeof(icon_path), "%s%s", get_application_dir(), ADVANCELY_ICON_PATH);
    SDL_Surface *icon_surface = IMG_Load(icon_path);
    if (icon_surface) {
        SDL_SetWindowIcon(t->window, icon_surface);
        log_message(LOG_INFO, "[INIT SDL] Tracker window icon set to %s\n", icon_path);
        log_message(LOG_INFO, "[INIT SDL] Tracker window icon size: %dx%d\n", icon_surface->w, icon_surface->h);
        SDL_DestroySurface(icon_surface); // Destroy surface after logging
    } else {
        log_message(LOG_ERROR, "[INIT SDL] Failed to load tracker window icon (asure path contains only standard English (ASCII) characters): %s\n", SDL_GetError());
    }

    // Set position after creation to handle multi-monitor setups better
    SDL_SetWindowPosition(t->window, x, y);

    // Print the value being set at initialization
    log_message(LOG_INFO, "[INIT SDL] Settings initial AlwaysOnTop state to: %s\n",
                settings->tracker_always_on_top ? "true" : "false");


    // More reliable than SDL_WINDOW_ALWAYS_ON_TOP flag
    SDL_SetWindowAlwaysOnTop(t->window, settings->tracker_always_on_top);

    t->renderer = SDL_CreateRenderer(t->window, nullptr);

    if (!t->renderer) {
        log_message(LOG_ERROR, "[INIT SDL] Failed to create tracker renderer: %s\n", SDL_GetError());
        return false;
    } // Then destroy the renderer in tracker_free

    log_message(LOG_INFO, "[INIT SDL] Tracker initialized!\n");

    return true;
}


bool overlay_init_sdl(Overlay *o, const AppSettings *settings) {
    Uint32 window_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;

    int x = (settings->overlay_window.x == DEFAULT_WINDOW_POS)
                ? (int) SDL_WINDOWPOS_CENTERED
                : settings->overlay_window.x;
    int y = (settings->overlay_window.y == DEFAULT_WINDOW_POS)
                ? (int) SDL_WINDOWPOS_CENTERED
                : settings->overlay_window.y;
    int w = (settings->overlay_window.w == DEFAULT_WINDOW_SIZE) ? OVERLAY_DEFAULT_WIDTH : settings->overlay_window.w;
    int h = (settings->overlay_window.h == DEFAULT_WINDOW_SIZE) ? OVERLAY_FIXED_HEIGHT : settings->overlay_window.h;

    o->window = SDL_CreateWindow(OVERLAY_TITLE, w, h, window_flags);
    if (!o->window) {
        log_message(LOG_ERROR, "[INIT SDL] Failed to create overlay window: %s\n", SDL_GetError());
        return false;
    }

    // Window Icon Cross-platform
    char icon_path[MAX_PATH_LENGTH];
    snprintf(icon_path, sizeof(icon_path), "%s%s", get_resources_path(), ADVANCELY_ICON_PATH);
    SDL_Surface *icon_surface = IMG_Load(icon_path);
    if (icon_surface) {
        SDL_SetWindowIcon(o->window, icon_surface);
        SDL_DestroySurface(icon_surface);
        log_message(LOG_INFO, "[INIT SDL] Overlay window icon set to %s%s\n", get_application_dir(), ADVANCELY_ICON_PATH);
    } else {
        log_message(LOG_ERROR, "[INIT SDL] Failed to load overlay window icon: %s\n", SDL_GetError());
    }

    SDL_SetWindowPosition(o->window, x, y);

    o->renderer = SDL_CreateRenderer(o->window, nullptr);

    if (!o->renderer) {
        log_message(LOG_ERROR, "[INIT SDL] Failed to create overlay renderer: %s\n", SDL_GetError());
        return false;
    } // Then destroy the renderer in overlay_free

    log_message(LOG_INFO, "[INIT SDL] Overlay initialized!\n");

    return true;
}
