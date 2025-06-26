//
// Created by Linus on 24.06.2025.
//

#include "init_sdl.h" //  includes tracker.h and then main.h through that

bool tracker_init_sdl(struct Tracker *t) {
    if (!SDL_Init(SDL_FLAGS)) {
        fprintf(stderr, "Failed to initialize SDL3: %s\n", SDL_GetError());
        return false;
    }

    t->window = SDL_CreateWindow(TRACKER_TITLE, TRACKER_WIDTH, TRACKER_HEIGHT, SDL_TRACKER_WINDOW_FLAGS);
    if (!t->window) {
        fprintf(stderr, "Failed to create tracker window: %s\n", SDL_GetError());
        return false;
    }

    t->renderer = SDL_CreateRenderer(t->window, NULL);

    if (!t->renderer) {
        fprintf(stderr, "Failed to create tracker renderer: %s\n", SDL_GetError());
        return false;
    } // Then destroy the renderer in tracker_free


    printf("Tracker initialized!\n"); // Shows through MINGW64, not terminal ./SDL3_Tutorial to run
    return true;
}

bool overlay_init_sdl(struct Overlay *o) {
    // No need to check for SDL_Init here, it's already done in tracker_init_sdl
    o->window = SDL_CreateWindow(OVERLAY_TITLE, OVERLAY_WIDTH, OVERLAY_HEIGHT, SDL_OVERLAY_WINDOW_FLAGS);
    if (!o->window) {
        fprintf(stderr, "Failed to create overlay window: %s\n", SDL_GetError());
        return false;
    }

    o->renderer = SDL_CreateRenderer(o->window, NULL);

    if (!o->renderer) {
        fprintf(stderr, "Failed to create overlay renderer: %s\n", SDL_GetError());
        return false;
    } // Then destroy the renderer in tracker_free


    printf("Overlay initialized!\n"); // Shows through MINGW64, not terminal ./SDL3_Tutorial to run
    return true;
}
