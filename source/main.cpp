//
// Created by Linus on 24.06.2025.
//

extern "C" {
#define DMON_IMPL // Required for dmon
#include "dmon.h"
#include <cJSON.h>
}

// SDL imports
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3/SDL_messagebox.h> // For SDL_ShowMessageBox
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_mutex.h>

// Local includes
#include "tracker.h" // includes main.h
#include "overlay.h"
#include "settings.h"
#include "global_event_handler.h"
#include "path_utils.h" // Include for find_player_data_files
#include "settings_utils.h" // Include for AppSettings and version checking
#include "logger.h"

// ImGUI imports
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/imgui_impl_sdlrenderer3.h"

// global flag TODO: Should be set to true when custom goal is checked off (manual update) -> SDL_SetAtomicInt(&g_needs_update, 1);
// We make g_needs_update available to global_event_handler.h with external linkage
SDL_AtomicInt g_needs_update;
SDL_AtomicInt g_settings_changed; // Watching when settings.json is modified to re-init paths
SDL_AtomicInt g_game_data_changed; // When game data is modified, custom counter is changed or manually override changed
bool g_force_open_settings = false;
static Uint64 g_last_file_mod_time = 0; // Standard 64-bit integer for the timestamp


// A global helper to show a user-facing error pop-up
void show_error_message(const char* title, const char* message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, nullptr);
}

// Global mutex to protect the watcher and paths (see they don't break when called in close succession)
static SDL_Mutex *g_watcher_mutex = nullptr;


/**
 * @brief Callback function for dmon file watcher.
 * This function is called by dmon in a separate thread whenever a file event occurs.
 * It sets a global flag (g_needs_update) to true if a file is modified.
 */
static void global_watch_callback(dmon_watch_id watch_id, dmon_action action, const char *rootdir, const char *filepath,
                                  const char *oldfilepath, void *user) {

    // The mutex is passed as the user data
    SDL_Mutex *watcher_mutex = (SDL_Mutex*)user;

    // Satisfying Werror - not used
    (void) watch_id;
    (void) rootdir;
    (void) oldfilepath;

    // We only care about file modifications to existing files
    if (action == DMON_ACTION_MODIFY) {
        const char *ext = strrchr(filepath, '.'); // Locate last '.' in string
        if (ext && (strcmp(ext, ".json") == 0) | (strcmp(ext, ".dat") == 0)) {
            // A game file was modified. Lock the mutex to safely update the shared timestamp
            SDL_LockMutex(watcher_mutex);
            g_last_file_mod_time = SDL_GetTicks();
            SDL_UnlockMutex(watcher_mutex);
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
    (void) user;

    // AppSettings *settings = (AppSettings *) user;

    if (action == DMON_ACTION_MODIFY) {
        if (strcmp(filepath, "settings.json") == 0) {
                log_message(LOG_INFO, "[DMON - MAIN] settings.json modified. Triggering update.\n");

            SDL_SetAtomicInt(&g_settings_changed, 1);
        }
    }
}


// ------------------------------------ END OF STATIC FUNCTIONS ------------------------------------


int main(int argc, char *argv[]) {
    // Satisfying Werror
    (void) argc;
    (void) argv;

    // Initialize the logger at the very beginning
    log_init();

    // Check for write permissions in the current directory before doing anything else
    FILE *write_test = fopen(".advancely-write-test", "w");
    if (write_test == nullptr) {
        show_error_message("Permission Error", "Advancely does not have permission to write files in this folder.\nPlease move it to another location (e.g., your Desktop or Documents folder) and run it again.");
        log_message(LOG_ERROR,"CRITICAL: Failed to get write permissions in the application directory. Exiting.\n");
        log_close();
        return EXIT_FAILURE;
    }
    fclose(write_test);
    remove(".advancely-write-test");

    // Expect the worst
    bool exit_status = EXIT_FAILURE;
    bool dmon_initialized = false; // Make sure dmon is initialized

    // Load settings ONCE at the start and check if file was incomplete to use default values
    // settings_load() returns true if the file was incomplete and used default values
    AppSettings app_settings;

    if (settings_load(&app_settings)) {
        // ----- This is the only if statement with print_debug_status outside of logger -----------
            log_message(LOG_INFO, "[MAIN] Settings file was incomplete or missing, saving with default values.\n");

        settings_save(&app_settings, nullptr); // Save complete settings back to the file
    }

    log_set_settings(&app_settings); // Give the logger access to the settings

    Tracker *tracker = nullptr; // pass address to function
    Overlay *overlay = nullptr;

    // Variable to hold the ID of our saves watcher
    dmon_watch_id saves_watcher_id = {0}; // Initialize to a known invalid state (id=0)

    g_watcher_mutex = SDL_CreateMutex();
    if (!g_watcher_mutex) {
        log_message(LOG_ERROR,"[MAIN] Failed to create mutex: %s\n", SDL_GetError());
        log_close();
        return EXIT_FAILURE;
    }

    // Initialize SDL ttf
    if (!TTF_Init()) {
        log_message(LOG_ERROR,"[MAIN] Failed to initialize SDL_ttf: %s\n", SDL_GetError());
        log_close();
        return EXIT_FAILURE;
    }

    if (tracker_new(&tracker, &app_settings)) {
        // Conditionally create the overlay at startup, based on settings
        if (app_settings.enable_overlay) {
            if (!overlay_new(&overlay, &app_settings)) {
                log_message(LOG_ERROR,"[MAIN] Failed to create overlay, continuing without it.\n");
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
            log_message(LOG_ERROR,
                "[MAIN - IMGUI] Failed to load font: resources/fonts/Roboto-Regular.ttf. Settings window will use default font.\n");
        }

        // Check if the currently configured saves path is valid, regardless of mode.
        // This ensures that on every launch, if the path is bad, we force the user to fix it.
        if (!path_exists(tracker->saves_path)) {
            log_message(LOG_ERROR,"[MAIN] Current saves path is invalid. Forcing manual configuration.\n");

            // If the current path is invalid, we must be in manual mode for the user to fix it.
            app_settings.path_mode = PATH_MODE_MANUAL;
            g_force_open_settings = true;

            // Save this state so if the app is closed and reopened, it remembers to force the settings open again.
            settings_save(&app_settings, nullptr);
        }

        dmon_init();
        dmon_initialized = true;
        SDL_SetAtomicInt(&g_needs_update, 1);
        SDL_SetAtomicInt(&g_settings_changed, 0);
        SDL_SetAtomicInt(&g_game_data_changed, 1);

        // HARDCODED SETTINGS DIRECTORY
            log_message(LOG_INFO,"[DMON - MAIN] Watching config directory: resources/config/\n");

        dmon_watch("resources/config/", settings_watch_callback, 0, nullptr);


        // Watch saves directory and store the watcher ID, ONLY if the path is valid
        if (path_exists(tracker->saves_path)) {
            if (strlen(tracker->saves_path) > 0) {
                    log_message(LOG_INFO,"[DMON - MAIN] Watching saves directory: %s\n", tracker->saves_path);

                // Watch saves directory and monitor child directories, passing the mutex as user data
                saves_watcher_id = dmon_watch(tracker->saves_path, global_watch_callback, DMON_WATCHFLAGS_RECURSIVE,
                                              g_watcher_mutex);
            } else {
                log_message(LOG_ERROR,"[DMON - MAIN] Failed to watch saves directory as it's empty: %s\n", tracker->saves_path);
            }
        } else {
            log_message(LOG_INFO,"[DMON - MAIN] Saves path is invalid. File watcher not started.\n");
        }

        bool is_running = true;
        bool settings_opened = false;
        Uint32 last_frame_time = SDL_GetTicks();
        float frame_target_time = 1000.0f / app_settings.fps;

        // Unified Main Loop at 60 FPS
        while (is_running) {

            // Force settings window open if the path was invalid on startup
            if (g_force_open_settings) {
                settings_opened = true;
            }

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
                    log_message(LOG_INFO,"[MAIN] Settings changed. Re-initializing template and file watcher.\n");


                // Stop watching the old directory, oNLY if it was being watched
                if (saves_watcher_id.id > 0) {
                    dmon_unwatch(saves_watcher_id);
                }

                // Reload settings from file to get the latest changes
                settings_load(&app_settings);
                log_set_settings(&app_settings); // Update the logger with the new settings

                // Logic to enable/disable overlay at runtime
                bool overlay_should_be_enabled = app_settings.enable_overlay;
                bool overlay_is_currently_enabled = (overlay != nullptr);

                if (overlay_should_be_enabled && !overlay_is_currently_enabled) {
                    // It was disabled, now enable it
                        log_message(LOG_INFO,"[MAIN] Enabling overlay.\n");

                    if (!overlay_new(&overlay, &app_settings)) {
                        log_message(LOG_ERROR,"[MAIN] Failed to create overlay at runtime.\n");
                    }
                } else if (!overlay_should_be_enabled && overlay_is_currently_enabled) {
                    // It was enabled, now disable it
                        log_message(LOG_INFO,"[MAIN] Disabling overlay.\n");

                    overlay_free(&overlay, &app_settings);
                }

                // If the overlay is active, apply any potential geometry changes from settings
                if (overlay) {
                    SDL_SetWindowSize(overlay->window, app_settings.overlay_window.w, OVERLAY_FIXED_HEIGHT);
                }

                // Update the tracker with the new paths and template data
                tracker_reinit_template(tracker, &app_settings);

                // Start watching the new directory
                if (strlen(tracker->saves_path) > 0) {
                        log_message(LOG_INFO,"[MAIN] Now watching new saves directory: %s\n", tracker->saves_path);

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
            if (SDL_SetAtomicInt(&g_needs_update, 0) == 1) { // If setting was successful it returns 1 (previous value)
                Uint64 current_ticks = SDL_GetTicks();

                // Check if the file has "settled" (no changes for a short period)
                // We can read g_last_file_mod_time safely because we hold the mutex
                if ((current_ticks - g_last_file_mod_time) >= FILE_SETTLE_DELAY_MS) {
                    // File has settled, so we can process the update.
                    // Atomically set the flag to 0 so we don't process this update again.
                    SDL_SetAtomicInt(&g_needs_update, 0);


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

                    // Print debug status (individual prints use log_message function)
                    // log_message function prints based on print_debug_status setting
                    tracker_print_debug_status(tracker, &app_settings);

                    // Update TITLE of the tracker window with some info, similar to the debug print
                    tracker_update_title(tracker, &app_settings);

                    // Check if the update was triggered by a game file change or hotkey press of counter
                    if (SDL_SetAtomicInt(&g_game_data_changed, 0) == 1) {
                        // Reset the timer as the update has happened
                        tracker->time_since_last_update = 0.0f;
                    }
                }
                // If the file has not settled, we do nothing this frame. g_needs_update remains at 1.
                // So it will check the next frame.
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
            settings_render_gui(&settings_opened, &app_settings, tracker->roboto_font, tracker, &g_force_open_settings);


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
            const float frame_time = (float) SDL_GetTicks() - (float) current_time;
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

    // Close logger at the end
    log_close();

    // One happy path
    return exit_status;
}
