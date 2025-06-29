//
// Created by Linus on 24.06.2025.
//

#include "init_sdl.h" //  includes tracker.h and then main.h through that

bool tracker_init_sdl(struct Tracker *t) {
    if (!SDL_Init(SDL_FLAGS)) {
        fprintf(stderr, "[INIT SDL] Failed to initialize SDL3: %s\n", SDL_GetError());
        return false;
    }

    t->window = SDL_CreateWindow(TRACKER_TITLE, TRACKER_WIDTH, TRACKER_HEIGHT, SDL_TRACKER_WINDOW_FLAGS);
    if (!t->window) {
        fprintf(stderr, "[INIT SDL] Failed to create tracker window: %s\n", SDL_GetError());
        return false;
    }

    t->renderer = SDL_CreateRenderer(t->window, NULL);

    if (!t->renderer) {
        fprintf(stderr, "[INIT SDL] Failed to create tracker renderer: %s\n", SDL_GetError());
        return false;
    } // Then destroy the renderer in tracker_free


    printf("[INIT SDL] Tracker initialized!\n"); // Shows through MINGW64, not terminal ./Advancely to run
    return true;
}


bool overlay_init_sdl(struct Overlay *o) {
    o->window = SDL_CreateWindow(OVERLAY_TITLE, OVERLAY_WIDTH, OVERLAY_HEIGHT, SDL_OVERLAY_WINDOW_FLAGS);
    if (!o->window) {
        fprintf(stderr, "[INIT SDL] Failed to create overlay window: %s\n", SDL_GetError());
        return false;
    }

    o->renderer = SDL_CreateRenderer(o->window, NULL);

    if (!o->renderer) {
        fprintf(stderr, "[INIT SDL] Failed to create overlay renderer: %s\n", SDL_GetError());
        return false;
    } // Then destroy the renderer in overlay_free


    printf("[INIT SDL] Overlay initialized!\n"); // Shows through MINGW64, not terminal ./Advancely to run
    return true;
}

bool settings_init_sdl(struct Settings *s) {
    s->window = SDL_CreateWindow(SETTINGS_TITLE, SETTINGS_WIDTH, SETTINGS_HEIGHT, SDL_SETTINGS_WINDOW_FLAGS);
    if (!s->window) {
        fprintf(stderr, "[INIT SDL] Failed to create settings window: %s\n", SDL_GetError());
        return false;
    }

    s->renderer = SDL_CreateRenderer(s->window, NULL);

    if (!s->renderer) {
        fprintf(stderr, "[INIT SDL] Failed to create settings renderer: %s\n", SDL_GetError());
        return false;
    } // Then destroy the renderer in settings_free


    printf("[INIT SDL] Settings initialized!\n"); // Shows through MINGW64, not terminal ./Advancely to run
    return true;
}
