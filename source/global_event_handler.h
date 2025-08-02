//
// Created by Linus on 26.06.2025.
//

#ifndef GLOBAL_EVENT_HANDLER_H
#define GLOBAL_EVENT_HANDLER_H

#include <SDL3/SDL_atomic.h>


#ifdef __cplusplus
extern "C" {
#endif



// Forward declarations
struct Tracker;
struct Overlay;
struct Settings;
struct AppSettings;


// This global variable is defined in main.cpp and made accessible here
extern SDL_AtomicInt g_needs_update; // Global flag to signal that an update is needed initially in main.c

/**
 * @brief Processes the global SDL event queue.
 *
 * This function polls for all pending SDL events for the current frame and dispatches
 * them to the appropriate handlers (tracker, settings, overlay) based on the event's window ID.
 * It also handles global quit events.
 *
 * @param t A pointer to the main tracker struct.
 * @param o A pointer to the overlay struct.
 * @param s A pointer to the settings struct.
 * @param app_settings A pointer to the loaded application settings to be modified and saved.
 * @param is_running A pointer to the main application loop's running flag.
 * @param settings_opened A pointer to the settings window's opened flag.
 * @param deltaTime A pointer to the frame's delta time, passed to the overlay event handler.
 */

void handle_global_events(Tracker *t, Overlay *o, Settings *s, AppSettings *app_settings, bool *is_running,
                          bool *settings_opened, float *deltaTime);

#ifdef __cplusplus
}
#endif

#endif //GLOBAL_EVENT_HANDLER_H
