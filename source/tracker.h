//
// Created by Linus on 24.06.2025.
//

#ifndef TRACKER_H
#define TRACKER_H

#include "main.h"


struct Tracker { // TODO: Also needs to be defineed in init_sdl.h
    SDL_Window *window;
    SDL_Renderer *renderer;
    // SDL_Texture *texture;
    SDL_Event event;
    bool is_running;

    int tracker_width;
    int tracker_height;

    int overlay_width;
    int overlay_height;

    cJSON *settings;
    cJSON *translation; // ENGLISH ONLY

    // More stuff to be added like TTF_Font *font and SDL_Texture *sprite whatever
};

bool tracker_new(struct Tracker **tracker);
void tracker_free(struct Tracker **tracker);
void tracker_events(struct Tracker *t);
void tracker_run(struct Tracker *t);

#endif //TRACKER_H