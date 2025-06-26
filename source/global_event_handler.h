//
// Created by Linus on 26.06.2025.
//

#ifndef GLOBAL_EVENT_HANDLER_H
#define GLOBAL_EVENT_HANDLER_H

#include "tracker.h"
#include "settings.h"
#include "overlay.h"

/**
 * @brief Processes the global SDL event queue.
 *
 * This function polls for all pending SDL events for the current frame and dispatches
 * them to the appropriate handlers (tracker or overlay) based on the event's window ID.
 * It also handles global quit events.
 *
 * @param t A pointer to the main tracker struct.
 * @param o A pointer to the overlay struct.
 * @param s A pointer to the settings struct.
 * @param is_running A pointer to the main application loop's running flag.
 * @param settings_opened A pointer to the settings window's opened flag.
 * @param deltaTime A pointer to the frame's delta time, passed to the overlay event handler.
 */

void handle_global_events(struct Tracker *t, struct Overlay *o, struct Settings *s, bool *is_running,
                          bool *settings_opened, float *deltaTime);

#endif //GLOBAL_EVENT_HANDLER_H
