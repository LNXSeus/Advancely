//
// Created by Linus on 24.06.2025.
//

#ifndef INIT_H
#define INIT_H

#include "tracker.h" // includes main.h
#include "settings.h"
#include "overlay.h"

struct Tracker;
struct Overlay;
struct Settings;


// Creates a hitbox for the overlay to be draggable
SDL_HitTestResult HitTestCallback(SDL_Window *win, const SDL_Point *area, void *data);

bool tracker_init_sdl(struct Tracker *t);
bool overlay_init_sdl(struct Overlay *o);
bool settings_init_sdl(struct Settings *s);


#endif //INIT_H
