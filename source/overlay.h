//
// Created by Linus on 24.06.2025.
//

#ifndef OVERLAY_H
#define OVERLAY_H

#include "main.h"


struct Overlay { // TODO: Also needs to be defineed in init_sdl.h
    SDL_Window *window;
    SDL_Renderer *renderer;
    // SDL_Texture *texture;

    int overlay_width;
    int overlay_height;

    cJSON *settings;
    cJSON *translation; // ENGLISH ONLY

    // More stuff to be added like TTF_Font *font and SDL_Texture *sprite whatever
};

bool overlay_new(struct Overlay **overlay);
void overlay_events(struct Overlay *o, SDL_Event *event, bool *is_running, float *deltaTime); // pass pointer so we can modify deltaTime
void overlay_update(struct Overlay *o, float *deltaTime);
void overlay_render(struct Overlay *o);
void overlay_free(struct Overlay **overlay);

#endif //OVERLAY_H