//
// Created by Linus on 24.06.2025.
//

#ifndef TRACKER_H
#define TRACKER_H

#include "main.h"
#include "settings_utils.h"

// TODO: Sync this with the advancement template
// Data structure for a single advancement criterion
typedef struct {
    char name[128];
    char root_name[128];
    bool done;
    SDL_Texture *texture;
} Criterion;

// TODO: Sync this with the advancement template
// Data structure for a single advancement

typedef struct {
    char name[128];
    char root_name[128];
    bool done;
    SDL_Texture *texture;
    int criteria_count;
    Criterion **criteria; // Array of pointers to criteria
} Advancement;


struct Tracker { // TODO: Also needs to be defined in init_sdl.h
    SDL_Window *window;
    SDL_Renderer *renderer;
    // SDL_Texture *texture;

    int tracker_width;
    int tracker_height;

    cJSON *translation_json; // ENGLISH ONLY

    // These paths can now be used in the tracker struct
    char saves_path[MAX_PATH_LENGTH];
    char advancements_path[MAX_PATH_LENGTH];
    char unlocks_path[MAX_PATH_LENGTH];
    char stats_path[MAX_PATH_LENGTH];
    char advancement_template_path[MAX_PATH_LENGTH]; // TODO: This will be the path to advancement templates

    int advancement_count;
    Advancement **advancements; // Array of pointers to all possible advancements

    // More stuff to be added like TTF_Font *font and SDL_Texture *sprite whatever
};

bool tracker_new(struct Tracker **tracker);
void tracker_events(struct Tracker *t, SDL_Event *event, bool *is_running, bool *settings_opened);
void tracker_update(struct Tracker *t, float *deltaTime);
void tracker_render(struct Tracker *t);
void tracker_free(struct Tracker **tracker);

void tracker_load_and_parse_advancements(struct Tracker *t);

#endif //TRACKER_H