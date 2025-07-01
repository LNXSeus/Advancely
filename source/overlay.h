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

/**
 * @brief Initializes a new Overlay instance.
 *
 * Allocates memory for the Overlay struct and initializes its SDL components,
 * creating the window and renderer for the overlay.
 *
 * @param overlay A pointer to an Overlay struct pointer that will be allocated.
 * @return true if initialization was successful, false otherwise.
 */
bool overlay_new(struct Overlay **overlay);

/**
 * @brief Handles SDL events specifically for the overlay window.
 *
 * Processes events like closing the window or keyboard inputs. For example,
 * holding down the SPACE key increases the overlay animation speed.
 *
 * @param o A pointer to the Overlay struct.
 * @param event A pointer to the SDL_Event to process.
 * @param is_running A pointer to the main application loop's running flag.
 * @param deltaTime A pointer to the frame's delta time, which can be modified by events.
 */
void overlay_events(struct Overlay *o, SDL_Event *event, bool *is_running, float *deltaTime); // pass pointer so we can modify deltaTime

/**
 * @brief Updates the state of the overlay.
 *
 * This function is currently a placeholder for future logic, such as animations
 * or other dynamic updates within the overlay window. Pressing the SPACE key
 * in the overlay window will increase the animation speed.
 *
 * @param o A pointer to the Overlay struct.
 * @param deltaTime A pointer to the frame's delta time.
 */
void overlay_update(struct Overlay *o, float *deltaTime);

/**
 * @brief Renders the overlay window's contents.
 *
 * Clears the screen with the background color. This is where all visual elements
 * for the overlay will be drawn.
 *
 * @param o A pointer to the Overlay struct.
 */
void overlay_render(struct Overlay *o);

/**
 * @brief Frees all resources associated with the Overlay instance.
 *
 * This includes destroying the SDL renderer and window and deallocating
 * the memory for the Overlay struct itself.
 *
 * @param overlay A pointer to the Overlay struct pointer to be freed.
 */
void overlay_free(struct Overlay **overlay);

#endif //OVERLAY_H