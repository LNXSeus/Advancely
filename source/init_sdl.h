//
// Created by Linus on 24.06.2025.
//

#ifndef INIT_H
#define INIT_H

#include "tracker.h" // includes main.h
#include "overlay.h"

struct Tracker;
struct Overlay;

bool tracker_init_sdl(struct Tracker *t);
bool overlay_init_sdl(struct Overlay *o);


#endif //INIT_H
