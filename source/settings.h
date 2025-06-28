//
// Created by Linus on 26.06.2025.
//

#ifndef SETTINGS_H
#define SETTINGS_H

#include "main.h"

struct Settings { // TODO: Also needs to be defined in init_sdl.h
    SDL_Window *window;
    SDL_Renderer *renderer;
    // SDL_Texture *texture;

    int settings_width;
    int settings_height;

    cJSON *settings; // Replaced by AppSettings in settings_utils
    cJSON *translation; // ENGLISH ONLY, probably unsused

    // More stuff to be added like TTF_Font *font and SDL_Texture *sprite whatever
};

bool settings_new(struct Settings **settings);
void settings_events(struct Settings *s, SDL_Event *event, bool *is_running, bool *settings_opened);
void settings_update(struct Settings *s, float *deltaTime);
void settings_render(struct Settings *s);
void settings_free(struct Settings **settings);

#endif //SETTINGS_H
