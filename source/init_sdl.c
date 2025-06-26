//
// Created by Linus on 24.06.2025.
//

#include "init_sdl.h" //  includes tracker.h and then main.h through that


// Creates a hitbox for the overlay to be draggable
SDL_HitTestResult HitTestCallback(SDL_Window *win, const SDL_Point *area, void *data) {
    (void) win;
    (void) data;

    if (area->y < OVERLAY_TITLE_BAR_HEIGHT) {
        return SDL_HITTEST_DRAGGABLE; // This area is draggable
    }
    return SDL_HITTEST_NORMAL;
}

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


    printf("Tracker initialized!\n"); // Shows through MINGW64, not terminal ./Advancely to run
    return true;
}


bool overlay_init_sdl(struct Overlay *o) {
    o->window = SDL_CreateWindow(OVERLAY_TITLE, OVERLAY_WIDTH, OVERLAY_HEIGHT, SDL_OVERLAY_WINDOW_FLAGS);
    if (!o->window) {
        fprintf(stderr, "Failed to create overlay window: %s\n", SDL_GetError());
        return false;
    }

    // For the draggable hitbox
    if (!SDL_SetWindowHitTest(o->window, HitTestCallback, NULL)) {
        fprintf(stderr, "Failed to set hit test: %s\n", SDL_GetError());
        return false;
    }

    o->renderer = SDL_CreateRenderer(o->window, NULL);

    if (!o->renderer) {
        fprintf(stderr, "Failed to create overlay renderer: %s\n", SDL_GetError());
        return false;
    } // Then destroy the renderer in overlay_free


    printf("Overlay initialized!\n"); // Shows through MINGW64, not terminal ./Advancely to run
    return true;
}

bool settings_init_sdl(struct Settings *s) {
    s->window = SDL_CreateWindow(SETTINGS_TITLE, SETTINGS_WIDTH, SETTINGS_HEIGHT, SDL_SETTINGS_WINDOW_FLAGS);
    if (!s->window) {
        fprintf(stderr, "Failed to create settings window: %s\n", SDL_GetError());
        return false;
    }

    s->renderer = SDL_CreateRenderer(s->window, NULL);

    if (!s->renderer) {
        fprintf(stderr, "Failed to create settings renderer: %s\n", SDL_GetError());
        return false;
    } // Then destroy the renderer in settings_free


    printf("Settings initialized!\n"); // Shows through MINGW64, not terminal ./Advancely to run
    return true;
}
