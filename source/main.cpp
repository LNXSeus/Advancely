//
// Created by Linus on 24.06.2025.
//

extern "C" {
#define DMON_IMPL // Required for dmon
#include "dmon.h"
#include <cJSON.h>
}

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <SDL3/SDL_main.h>
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_mutex.h>
#include "tracker.h" // includes main.h
#include "overlay.h"
#include "settings.h"
#include "global_event_handler.h"
#include "path_utils.h" // Include for find_player_data_files
#include "settings_utils.h" // Include for AppSettings and version checking

// ImGUI imports
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/imgui_impl_sdlrenderer3.h"

// global flag TODO: Should be set to true when custom goal is checked off (manual update) -> SDL_SetAtomicInt(&g_needs_update, 1);
// We make g_needs_update available to global_event_handler.h with external linkage
SDL_AtomicInt g_needs_update;
SDL_AtomicInt g_settings_changed; // Watching when settings.json is modified to re-init paths
SDL_AtomicInt g_game_data_changed; // When game data is modified, custom counter is changed or manually override changed

// Global mutex to protect the watcher and paths (see they don't break when called in close succession)
static SDL_Mutex *g_watcher_mutex = nullptr;


/**
 * @brief Callback function for dmon file watcher.
 * This function is called by dmon in a separate thread whenever a file event occurs.
 * It sets a global flag (g_needs_update) to true if a file is modified.
 */
static void global_watch_callback(dmon_watch_id watch_id, dmon_action action, const char *rootdir, const char *filepath,
                                  const char *oldfilepath, void *user) {
    // Satisfying Werror - not used
    (void) watch_id;
    (void) rootdir;
    (void) oldfilepath;
    (void) user;

    // We only care about file modifications to existing files
    if (action == DMON_ACTION_MODIFY) {
        const char *ext = strrchr(filepath, '.'); // Locate last '.' in string
        if (ext && (strcmp(ext, ".json") == 0) | (strcmp(ext, ".dat") == 0)) {
            // Check for .json or .dat
            // Check if file extension is .json, IMPORTANT: This triggers for useless .json files as well
            // printf("[DMON - MAIN] File modified: %s. Triggering update.\n", filepath);

            // Automatically set the flag to 1. Safe to call from any thread.
            SDL_SetAtomicInt(&g_needs_update, 1);
            SDL_SetAtomicInt(&g_game_data_changed, 1);
        }
    }
}

/**
 * @brief Callback for dmon watching the CONFIG directory for settings.json.
 */
static void settings_watch_callback(dmon_watch_id watch_id, dmon_action action, const char *rootdir,
                                    const char *filepath, const char *oldfilepath, void *user) {
    (void) watch_id;
    (void) rootdir;
    (void) oldfilepath;

    AppSettings *settings = (AppSettings*)user;

    if (action == DMON_ACTION_MODIFY) {
        if (strcmp(filepath, "settings.json") == 0) {
            if (settings && settings->print_debug_status) {
                printf("[DMON - MAIN] settings.json modified. Triggering update.\n");
            }
            SDL_SetAtomicInt(&g_settings_changed, 1);
        }
    }
}


// ------------------------------------ END OF STATIC FUNCTIONS ------------------------------------


int main(int argc, char *argv[]) {
    // Satisfying Werror
    (void) argc;
    (void) argv;

    // Expect the worst
    bool exit_status = EXIT_FAILURE;
    bool dmon_initialized = false; // Make sure dmon is initialized

    // Load settings ONCE at the start and check if file was incomplete to use default values
    // settings_load() returns true if the file was incomplete and used default values
    AppSettings app_settings;
    if (settings_load(&app_settings)) {
        if (app_settings.print_debug_status) {
            printf("[MAIN] Settings file was incomplete or missing, saving with default values.\n");
        }
        settings_save(&app_settings, nullptr); // Save complete settings back to the file
    }


    Tracker *tracker = nullptr; // pass address to function
    Overlay *overlay = nullptr;

    // Variable to hold the ID of our saves watcher
    dmon_watch_id saves_watcher_id;

    g_watcher_mutex = SDL_CreateMutex();
    if (!g_watcher_mutex) {
        fprintf(stderr, "[MAIN] Failed to create mutex: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    // Initialize SDL ttf
    if (!TTF_Init()) {
        fprintf(stderr, "[MAIN] Failed to initialize SDL_ttf: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    if (tracker_new(&tracker, &app_settings)) {
        // Conditionally create the overlay at startup, based on settings
        if (app_settings.enable_overlay) {
            if (!overlay_new(&overlay, &app_settings)) {
                fprintf(stderr, "[MAIN] Failed to create overlay, continuing without it.\n");
            }
        }
        // Initialize ImGUI
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        (void) io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

        ImGui::StyleColorsDark(); // Or ImGui::StyleColorsClassic()

        // Setup Platform/Renderer backends
        // THERE SHOULD BE ANOTHER WAY TO ONLY USE IMGUI for tracker window and SDL3 for overlay and settings
        ImGui_ImplSDL3_InitForSDLRenderer(tracker->window, tracker->renderer);
        ImGui_ImplSDLRenderer3_Init(tracker->renderer);

        // Load Fonts
        io.Fonts->AddFontFromFileTTF("resources/fonts/Minecraft.ttf", 16.0f);

        // Roboto Font is for the settings inside the tracker
        tracker->roboto_font = io.Fonts->AddFontFromFileTTF("resources/fonts/Roboto-Regular.ttf", 16.0f);
        if (tracker->roboto_font == nullptr) {
            fprintf(
                stderr,
                "[MAIN - IMGUI] Failed to load font: resources/fonts/Roboto-Regular.ttf. Settings window will use default font.\n");
        }

        dmon_init();
        dmon_initialized = true;
        SDL_SetAtomicInt(&g_needs_update, 1);
        SDL_SetAtomicInt(&g_settings_changed, 0);
        SDL_SetAtomicInt(&g_game_data_changed, 1);

        // HARDCODED SETTINGS DIRECTORY
        if (app_settings.print_debug_status) {
            printf("[DMON - MAIN] Watching config directory: resources/config/\n");
        }
        dmon_watch("resources/config/", settings_watch_callback, 0, nullptr);


        // Watch saves directory and store the watcher ID
        if (strlen(tracker->saves_path) > 0) {
            if (app_settings.print_debug_status) {
                printf("[DMON - MAIN] Watching saves directory: %s\n", tracker->saves_path);
            }
            // Watch saves directory and monitor child diretories
            saves_watcher_id = dmon_watch(tracker->saves_path, global_watch_callback, DMON_WATCHFLAGS_RECURSIVE,
                                          &app_settings);
        } else {
            fprintf(stderr, "[DMON - MAIN] Failed to watch saves directory as it's empty: %s\n", tracker->saves_path);
        }

        bool is_running = true;
        bool settings_opened = false;
        Uint32 last_frame_time = SDL_GetTicks();
        float frame_target_time = 1000.0f / app_settings.fps;

        // Unified Main Loop at 60 FPS
        while (is_running) {
            Uint32 current_time = SDL_GetTicks();
            float deltaTime = (float) (current_time - last_frame_time) / 1000.0f;
            last_frame_time = current_time;

            // Increment the time since the last update every frame
            tracker->time_since_last_update += deltaTime;

            // --- Per-Frame Logic ---

            // Lock mutex before touching watchers or paths
            SDL_LockMutex(g_watcher_mutex);

            handle_global_events(tracker, overlay, &app_settings, &is_running, &settings_opened, &deltaTime);


            // Close immediately if app not running
            if (!is_running) break;


            // Check if settings.json has been modified (by UI or external editor)
            // Single point of truth for tracker data, triggered by "Apply" button
            if (SDL_SetAtomicInt(&g_settings_changed, 0) == 1) {
                if (app_settings.print_debug_status) {
                    printf("[MAIN] Settings changed. Re-initializing template and file watcher.\n");
                }

                // Stop watching the old directory
                dmon_unwatch(saves_watcher_id);

                // Reload settings from file to get the latest changes
                settings_load(&app_settings);

                // Logic to enable/disable overlay at runtime
                bool overlay_should_be_enabled = app_settings.enable_overlay;
                bool overlay_is_currently_enabled = (overlay != nullptr);

                if (overlay_should_be_enabled && !overlay_is_currently_enabled) {
                    // It was disabled, now enable it
                    if (app_settings.print_debug_status) {
                        printf("[MAIN] Enabling overlay.\n");
                    }
                    if (!overlay_new(&overlay, &app_settings)) {
                        fprintf(stderr, "[MAIN] Failed to create overlay at runtime.\n");
                    }
                } else if (!overlay_should_be_enabled && overlay_is_currently_enabled) {
                    // It was enabled, now disable it
                    if (app_settings.print_debug_status) {
                        printf("[MAIN] Disabling overlay.\n");
                    }
                    overlay_free(&overlay, &app_settings);
                }

                // If the overlay is active, apply any potential geometry changes from settings
                if (overlay) {
                    SDL_SetWindowSize(overlay->window, app_settings.overlay_window.w, OVERLAY_FIXED_HEIGHT);
                }

                // Update the tracker with the new paths and template data
                tracker_reinit_template(tracker, &app_settings);

                // // After re-init, reset overlay animation state to prevent crashes
                // if (overlay) {
                //     overlay->scroll_offset_row1 = 0.0f;
                //     overlay->scroll_offset_row2 = 0.0f;
                //     // overlay->scroll_offset_row3 = 0.0f; // TODO: Row 3 here as well??
                //     overlay->start_index_row1 = 0;
                //     overlay->start_index_row2 = 0;
                //     // overlay->start_index_row3 = 0; // TODO: Row 3 here as well??
                // }

                // Start watching the new directory
                if (strlen(tracker->saves_path) > 0) {
                    if (app_settings.print_debug_status) {
                        printf("[MAIN] Now watching new saves directory: %s\n", tracker->saves_path);
                    }
                    saves_watcher_id = dmon_watch(tracker->saves_path, global_watch_callback,
                                                  DMON_WATCHFLAGS_RECURSIVE,
                                                  nullptr);
                }

                // Force a data update and apply non-critical changes
                SDL_SetAtomicInt(&g_needs_update, 1);
                frame_target_time = 1000.0f / app_settings.fps;
                SDL_SetWindowAlwaysOnTop(tracker->window, app_settings.tracker_always_on_top);
            }


            // Check if dmon (or manual update through custom goal) has requested an update
            // Atomically check if the flag is 1, and if so, set it to 0.
            if (SDL_SetAtomicInt(&g_needs_update, 0) == 1) {
                MC_Version version = settings_get_version_from_string(app_settings.version_str);
                find_player_data_files(
                    tracker->saves_path,
                    version,
                    // This toggles if StatsPerWorld mod is enabled (local stats for legacy)
                    app_settings.using_stats_per_world_legacy,
                    &app_settings,
                    tracker->world_name,
                    tracker->advancements_path,
                    tracker->stats_path,
                    tracker->unlocks_path,
                    MAX_PATH_LENGTH
                );

                // Now update progress with the correct paths
                tracker_update(tracker, &deltaTime, &app_settings);

                // Print debug status based on settings
                if (app_settings.print_debug_status) tracker_print_debug_status(tracker, &app_settings);

                // Update TITLE of the tracker window with some info, similar to the debug print
                tracker_update_title(tracker, &app_settings);

                // Check if the update was triggered by a game file change or hotkey press of counter
                if (SDL_SetAtomicInt(&g_game_data_changed, 0) == 1) {
                    // Reset the timer as the update has happened
                    tracker->time_since_last_update = 0.0f;
                }
            }



            // Overlay animations should run every frame, if it exists
            if (overlay) {
                overlay_update(overlay, &deltaTime, tracker, &app_settings);
            }
            // IMGUI RENDERING
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            // Render the tracker GUI USING ImGui
            tracker_render_gui(tracker, &app_settings);

            // Render settings window in tracker window
            // settings_opened flag is triggered by Esc key -> tracker_events() and global event handler
            settings_render_gui(&settings_opened, &app_settings, tracker->roboto_font, tracker);


            ImGui::Render();

            SDL_SetRenderDrawColor(tracker->renderer, (Uint8) (app_settings.tracker_bg_color.r),
                                   (Uint8) (app_settings.tracker_bg_color.g), (Uint8) (app_settings.tracker_bg_color.b),
                                   (Uint8) (app_settings.tracker_bg_color.a));
            SDL_RenderClear(tracker->renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), tracker->renderer);
            SDL_RenderPresent(tracker->renderer);


            // Overlay window gets rendered using SDL, if it exists
            if (overlay) {
                overlay_render(overlay, tracker, &app_settings);
            }

            // Unlock mutex after all updates are done
            SDL_UnlockMutex(g_watcher_mutex);

            // --- Frame limiting ---
            const float frame_time = (float) SDL_GetTicks() - (float)current_time;
            if (frame_time < frame_target_time) {
                SDL_Delay((Uint32) (frame_target_time - frame_time));
            }
        }
        exit_status = EXIT_SUCCESS;
    }

    if (dmon_initialized) {
        dmon_deinit();
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    tracker_free(&tracker, &app_settings);
    overlay_free(&overlay, &app_settings);
    SDL_DestroyMutex(g_watcher_mutex); // Destroy the mutex
    SDL_Quit(); // This is ONCE for all windows

    // One happy path
    return exit_status;
}
