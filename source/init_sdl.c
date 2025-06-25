//
// Created by Linus on 24.06.2025.
//

#include "init_sdl.h" //  includes tracker.h and then main.h through that

bool tracker_init_sdl(struct Tracker *t) {
    (void)t;
    if (!SDL_Init(SDL_FLAGS)) {
        fprintf(stderr, "Failed to initialize SDL3: %s\n", SDL_GetError());
        return false;
    }

    printf("Tracker initialized!\n"); // Shows through MINGW64, not terminal ./SDL3_Tutorial to run

    return true;
}
