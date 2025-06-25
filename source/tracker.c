//
// Created by Linus on 24.06.2025.
//

#include "tracker.h"
#include "init_sdl.h"

bool tracker_new(struct Tracker **tracker) {
    // dereference once and use calloc
    *tracker = calloc(1, sizeof(struct Tracker));
    // Check here if calloc failed
    if (*tracker == NULL) {
        fprintf(stderr, "Error allocating memory for tracker.\n");
        return false;
    }

    // temp variable to not dereference over and over again
    struct Tracker *t = *tracker;

    if (!tracker_init_sdl(t)) {
        return false;
    }

    t->is_running = true;

    return true;
}


void tracker_free(struct Tracker **tracker) {
    if (*tracker) {
        struct Tracker *t = *tracker;

        SDL_Quit();

        // tracker is heap allocated so free it
        free(t);
        t = NULL;
        *tracker = NULL;

        printf("All freed!\n");

    }
}
