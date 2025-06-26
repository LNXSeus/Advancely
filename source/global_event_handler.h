//
// Created by Linus on 26.06.2025.
//

#ifndef GLOBAL_EVENT_HANDLER_H
#define GLOBAL_EVENT_HANDLER_H

#include "tracker.h"
#include "overlay.h"

/**
 * @brief Processes the global SDL event queue.
 *
 * This function polls for all pending SDL events for the current frame and dispatches
 * them to the appropriate handlers (tracker or overlay) based on the event's window ID.
 * It also handles global quit events.
 *
 * @param tracker A pointer to the main tracker struct.
 * @param overlay A pointer to the overlay struct.
 * @param is_running A pointer to the main application loop's running flag.
 * @param deltaTime A pointer to the frame's delta time, passed to the overlay event handler.
 */

void handle_global_events(struct Tracker *tracker, struct Overlay *overlay, bool *is_running, float *deltaTime);

#endif //GLOBAL_EVENT_HANDLER_H
