//
// Created by Linus on 24.06.2025.
//

#include <SDL3/SDL_main.h>
#include "tracker.h" // includes main.h


int main(int argc, char *argv[]) {

    // Satisfying Werror
    (void)argc;
    (void)argv;

    bool exit_status = EXIT_FAILURE;

    struct Tracker *tracker = NULL; // pass address to function

    if (tracker_new(&tracker)) { // Address of pointer, that's why pointer to pointer
        tracker_run(tracker);
        exit_status = EXIT_SUCCESS;
    }

    tracker_free(&tracker);

    // One happy path
    return exit_status;
}
