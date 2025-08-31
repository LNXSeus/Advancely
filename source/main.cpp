//
// Created by Linus on 24.06.2025.
//

#include <ctime>

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

// Platform specific includes
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h> // For fork, execv, kill
#include <sys/types.h> // For pid_t
#include <signal.h> // For SIGKILL
#include <fcntl.h>     // For O_* constants
#include <sys/mman.h>  // For shared memory
#include <semaphore.h> // For named semaphores (acting as mutexes)
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// global flag TODO: Should be set to true when custom goal is checked off (manual update) -> SDL_SetAtomicInt(&g_needs_update, 1);
// We make g_needs_update available to global_event_handler.h with external linkage
SDL_AtomicInt g_needs_update;
SDL_AtomicInt g_settings_changed; // Watching when settings.json is modified to re-init paths
SDL_AtomicInt g_game_data_changed; // When game data is modified, custom counter is changed or manually override changed
SDL_AtomicInt g_notes_changed;
bool g_force_open_settings = false;

// TODO: Remove function below when fixed
// // This is our custom callback function that SDL will call with its internal log messages.
// static void SDLLogOutputFunction(void *userdata, int category, SDL_LogPriority priority, const char *message) {
//     // The 'userdata' parameter is the FILE* we will pass to SDL_SetLogOutputFunction.
//     FILE* log_file = (FILE*)userdata;
//     if (log_file) {
//         // We will add a timestamp to make the log clearer.
//         char time_buf[16];
//         time_t now = time(0);
//         strftime(time_buf, sizeof(time_buf), "%H:%M:%S", localtime(&now));
//
//         fprintf(log_file, "[%s] [SDL] Category: %d, Priority: %d, Message: %s\n", time_buf, category, priority, message);
//         fflush(log_file); // Ensure the message is written immediately in case of a crash.
//     }
// }


// A global helper to show a user-facing error pop-up
void show_error_message(const char *title, const char *message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, nullptr);
}


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
        if (ext && ((strcmp(ext, ".json") == 0) || (strcmp(ext, ".dat") == 0))) {
            // A game file was modified. Atomically update the timestamp and flags.
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

dmon_watch_id notes_watcher_id = {0};

static void notes_watch_callback(dmon_watch_id watch_id, dmon_action action, const char *rootdir,
                                 const char *filepath, const char *oldfilepath, void *user) {
    (void)watch_id; (void)rootdir; (void)oldfilepath; (void)user;

    // We only care about file modifications
    if (action == DMON_ACTION_MODIFY) {
        // The filepath from dmon is just the filename. We need to check if it's our notes file.
        // We can't access AppSettings here, so we just signal a generic notes change.
        const char* ext = strrchr(filepath, '.');
        if (ext && strcmp(ext, ".txt") == 0 && strstr(filepath, "_notes")) {
            SDL_SetAtomicInt(&g_notes_changed, 1);
        }
    }
}

// This cross-platform function gets the full path of the currently running executable.
static bool get_executable_path(char* out_path, size_t max_len) {
#ifdef _WIN32
    DWORD result = GetModuleFileNameA(nullptr, out_path, (DWORD)max_len);
    if (result == 0 || result >= max_len) {
        return false;
    }
    return true;
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)max_len;
    if (_NSGetExecutablePath(out_path, &size) != 0) {
        // Buffer was too small.
        return false;
    }
    return true;
#else // For Linux
    ssize_t len = readlink("/proc/self/exe", out_path, max_len - 1);
    if (len != -1) {
        out_path[len] = '\0';
        return true;
    }
    return false;
#endif
}

// ------------------------------------ END OF STATIC FUNCTIONS ------------------------------------


int main(int argc, char *argv[]) {

    // As we're not only using SDL_main() as our entry point
    SDL_SetMainReady();

        bool is_overlay_mode = false;
    if (argc > 1 && strcmp(argv[1], "--overlay") == 0) { // Additional command line "--overlay"
        is_overlay_mode = true;
    }

    // If we are in overlay mode, run a separate, simplified main loop.
    if (is_overlay_mode) {
        log_init(); // Each process needs its own log
        log_message(LOG_INFO, "[OVERLAY PROCESS] Starting in overlay-only mode.\n");


        // TODO: DEBUGGING ------------------------------
        // This gives you 5 seconds to attach the debugger before the program continues.
        SDL_Delay(30000);
        // Open a dedicated file for SDL's internal logs.
        // FILE* sdl_log_file = fopen("sdl_overlay_log.txt", "w");
        // if (!sdl_log_file) {
        //     log_message(LOG_ERROR, "CRITICAL: Could not open sdl_overlay_log.txt for writing.\n");
        // }
        //
        // // Set our custom function as the log handler BEFORE any other SDL calls.
        // SDL_SetLogOutputFunction(SDLLogOutputFunction, sdl_log_file);
        //
        // // END OF DEBUGGING ------------------------------

        AppSettings settings;
        settings_load(&settings);
        log_set_settings(&settings);

        // Force SDL to use the reliable software renderer instead of hardware drivers.
        // SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");


        // Every process that creates a window must call SDL_Init first.
        if (!SDL_Init(SDL_FLAGS)) {
            log_message(LOG_ERROR, "[OVERLAY PROCESS] Failed to init SDL: %s\n", SDL_GetError());
            return 1;
        }

        if (!TTF_Init()) {
            log_message(LOG_ERROR, "[OVERLAY PROCESS] Failed to init TTF: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        Overlay* overlay = nullptr;
        if (!overlay_new(&overlay, &settings)) {
            log_message(LOG_ERROR, "[OVERLAY PROCESS] overlay_new() failed.\n");
            TTF_Quit();
            SDL_Quit();
            return 1;
        }

        // Open existing shared memory and mutex
        log_message(LOG_INFO, "[OVERLAY IPC] Opening shared memory and mutex...\n");
#ifdef _WIN32
        overlay->h_mutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, MUTEX_NAME);

        if (overlay->h_mutex == nullptr) {
            log_message(LOG_ERROR, "[OVERLAY IPC] Failed to open mutex. Is the tracker running? Error: %lu\n", GetLastError());
            overlay_free(&overlay, &settings);
            return 1;
        }

        // TODO: Could also do FILE_MAP_READ
        overlay->h_map_file = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);
        overlay->p_shared_data = (SharedData*)MapViewOfFile(overlay->h_map_file, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));

        if (overlay->p_shared_data == nullptr) {
            log_message(LOG_ERROR, "[OVERLAY IPC] Failed to map shared memory. Is the tracker running?\n");
            overlay_free(&overlay, &settings); // Cleans up SDL stuff as well
            return 1;
        }

#else
        overlay->mutex = sem_open(MUTEX_NAME, 0);

        if (overlay->mutex == SEM_FAILED) {
            log_message(LOG_ERROR, "[OVERLAY IPC] Failed to open semaphore/mutex. Is the tracker running?\n");
            overlay_free(&overlay, &settings);
            return 1;
        }

        overlay->shm_fd = shm_open(SHARED_MEM_NAME, O_RDONLY, 0666);
        overlay->p_shared_data = (SharedData*)mmap(0, sizeof(SharedData), PROT_READ, MAP_SHARED, overlay->shm_fd, 0);

        if (overlay->p_shared_data == MAP_FAILED) {
            log_message(LOG_ERROR, "[OVERLAY IPC] Failed to map shared memory. Is the tracker running?\n");
            overlay_free(&overlay, &settings); // Cleans up SDL stuff as well
            return 1;
        }
#endif

        // This local struct will hold the data copied from shared memory each frame.
        SharedData local_data_copy{};

        // We create a "proxy" tracker struct to pass to the render functions,
        // which avoids having to change their signatures.
        Tracker proxy_tracker{};
        proxy_tracker.template_data = &local_data_copy.template_data;

        // TODO: Remove
        // // Initialize all members, including the non-trivial ImVec2
        // Tracker placeholder_tracker{};
        // TemplateData placeholder_template_data{};
        //
        //
        // placeholder_tracker.template_data = &placeholder_template_data;
        // strncpy(placeholder_tracker.world_name, "Waiting for data...", MAX_PATH_LENGTH - 1);


        bool is_running = true;
        Uint32 last_frame_time = SDL_GetTicks();
        while(is_running) { // Separate loop for the overlay process -> with it's own framerate
             SDL_Event event;
             while (SDL_PollEvent(&event)) {
                 if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                     is_running = false;
                 }
                 // Minimal event handling for the overlay window
                 overlay_events(overlay, &event, &is_running, nullptr, &settings);
             }

            // Read the latest data from shared memory
            if (overlay->p_shared_data) {
#ifdef _WIN32
                DWORD wait_result = WaitForSingleObject(overlay->h_mutex, 50); // Wait up to 50ms
                if (wait_result == WAIT_OBJECT_0) {
#else
                if (sem_wait(overlay->mutex) == 0) {
#endif
                    // --- Critical Section: We have the lock ---
                    memcpy(&local_data_copy, overlay->p_shared_data, sizeof(SharedData));
                    // --- End of Critical Section ---
#ifdef _WIN32
                    ReleaseMutex(overlay->h_mutex);
#else
                    sem_post(overlay->mutex);
#endif
                }
            }

            // Update the proxy tracker with the latest data for the render functions.
            strncpy(proxy_tracker.world_name, local_data_copy.world_name, MAX_PATH_LENGTH - 1);
            proxy_tracker.time_since_last_update = local_data_copy.time_since_last_update;

            Uint32 current_time = SDL_GetTicks();
            float deltaTime = (float)(current_time - last_frame_time) / 1000.0f;
            last_frame_time = current_time;

            // The update and render functions now receive live data!
            overlay_update(overlay, &deltaTime, &proxy_tracker, &settings);
            overlay_render(overlay, &proxy_tracker, &settings);

             // TODO: Remove
             // Uint32 current_time = SDL_GetTicks();
             // float deltaTime = (float)(current_time - last_frame_time) / 1000.0f;
             // last_frame_time = current_time;
             //
             // // Update and render with placeholder data
             // overlay_update(overlay, &deltaTime, &placeholder_tracker, &settings);
             // overlay_render(overlay, &placeholder_tracker, &settings);

             float frame_target_time = 1000.0f / settings.fps;
             const float frame_time = (float)SDL_GetTicks() - (float)current_time;
             if (frame_time < frame_target_time) {
                 SDL_Delay((Uint32)(frame_target_time - frame_time));
             }
        }

        // Clean up IPC handles
#ifdef _WIN32
        UnmapViewOfFile(overlay->p_shared_data);
        CloseHandle(overlay->h_map_file);
        CloseHandle(overlay->h_mutex);
#else
        munmap(overlay->p_shared_data, sizeof(SharedData));
        close(overlay->shm_fd);
        sem_close(overlay->mutex);
#endif

        overlay_free(&overlay, &settings);
        TTF_Quit();
        SDL_Quit();
        log_close();

        // Close the dedicated SDL log file on successful exit.
        // TODO: Remove
        // if (sdl_log_file) {
        //     fclose(sdl_log_file);
        // }

        return 0; // End the overlay process here
    }

    // Original code ruNs if not in overlay mode
    // Satisfying Werror
    (void) argc;
    (void) argv;

    // Initialize the logger at the very beginning
    log_init();

    // Check for write permissions in the current directory before doing anything else
    FILE *write_test = fopen(".advancely-write-test", "w");
    if (write_test == nullptr) {
        show_error_message("Permission Error",
                           "Advancely does not have permission to write files in this folder.\nPlease move it to another location (e.g., your Desktop or Documents folder) and run it again.");
        log_message(LOG_ERROR, "CRITICAL: Failed to get write permissions in the application directory. Exiting.\n");
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
        if (app_settings.print_debug_status) {
            log_message(LOG_INFO, "[MAIN] Settings file was incomplete or missing, saving with default values.\n");
        }
        settings_save(&app_settings, nullptr); // Save complete settings back to the file
    }

    log_set_settings(&app_settings); // Give the logger access to the settings

    Tracker *tracker = nullptr; // pass address to function
    // Overlay *overlay = nullptr; // TODO: Remove

    // Variable to hold the ID of our saves watcher
    dmon_watch_id saves_watcher_id = {0}; // Initialize to a known invalid state (id=0)

    // Initialize SDL ttf
    if (!TTF_Init()) {
        log_message(LOG_ERROR, "[MAIN] Failed to initialize SDL_ttf: %s\n", SDL_GetError());
        log_close();
        return EXIT_FAILURE;
    }

    if (tracker_new(&tracker, &app_settings)) {

        // IPC CREATION

        log_message(LOG_INFO, "[IPC] Creating shared memory and mutex...\n");
#ifdef _WIN32
        // Create a named mutex for synchronization
        tracker->h_mutex = CreateMutexA(nullptr, FALSE, MUTEX_NAME);
        if (tracker->h_mutex == nullptr) {
            log_message(LOG_ERROR, "[IPC] Failed to create mutex: %s\n", SDL_GetError());

            // Free resources and exit
            tracker_free(&tracker, &app_settings);
            TTF_Quit();
            SDL_Quit();
            log_close();
            return EXIT_FAILURE;
        }

        // Create the shared memory file mapping
        tracker->h_map_file = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedData), SHARED_MEM_NAME);
        if (tracker->h_map_file == nullptr) {
            log_message(LOG_ERROR, "[IPC] Failed to create file mapping object.\n");

            // Free resources and exit
            tracker_free(&tracker, &app_settings);
            TTF_Quit();
            SDL_Quit();
            log_close();
            return EXIT_FAILURE;
        }

        // Map the shared memory into this process's address space
        tracker->p_shared_data = (SharedData*)MapViewOfFile(tracker->h_map_file, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
        if (tracker->p_shared_data == nullptr) {
            log_message(LOG_ERROR, "[IPC] Could not map view of file.\n");

            // Free resources and exit
            tracker_free(&tracker, &app_settings);
            TTF_Quit();
            SDL_Quit();
            log_close();
            return EXIT_FAILURE;
        }
#else
        // Create/open a named semaphore (acting as a mutex)
        tracker->mutex = sem_open(MUTEX_NAME, O_CREAT, 0644, 1);
        if (tracker->mutex == SEM_FAILED) {
            log_message(LOG_ERROR, "[IPC] Failed to create semaphore.\n");

            // Free resources and exit
            tracker_free(&tracker, &app_settings);
            TTF_Quit();
            SDL_Quit();
            log_close();
            return EXIT_FAILURE;
        }

        // Create the shared memory object
        tracker->shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
        ftruncate(tracker->shm_fd, sizeof(SharedData));

        // Map the shared memory object
        tracker->p_shared_data = (SharedData*)mmap(0, sizeof(SharedData), PROT_WRITE, MAP_SHARED, tracker->shm_fd, 0);
#endif


        // TODO: Remove
        // // Conditionally create the overlay at startup, based on settings
        // if (app_settings.enable_overlay) {
        //     if (!overlay_new(&overlay, &app_settings)) {
        //         log_message(LOG_ERROR, "[MAIN] Failed to create overlay, continuing without it.\n");
        //     }
        // }
        // Initialize ImGUI
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        (void) io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

        ImGui::StyleColorsDark(); // Or ImGui::StyleColorsClassic()

        // Setup Platform/Renderer backends
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
            log_message(LOG_ERROR, "[MAIN] Current saves path is invalid. Forcing manual configuration.\n");

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
        SDL_SetAtomicInt(&g_notes_changed, 0);

        // HARDCODED SETTINGS DIRECTORY
        log_message(LOG_INFO, "[DMON - MAIN] Watching config directory: resources/config/\n");

        dmon_watch("resources/config/", settings_watch_callback, 0, nullptr);


        // Watch saves directory and store the watcher ID, ONLY if the path is valid
        if (path_exists(tracker->saves_path)) {
            if (strlen(tracker->saves_path) > 0) {
                log_message(LOG_INFO, "[DMON - MAIN] Watching saves directory: %s\n", tracker->saves_path);

                // Watch saves directory and monitor child directories
                saves_watcher_id = dmon_watch(tracker->saves_path, global_watch_callback, DMON_WATCHFLAGS_RECURSIVE,
                                              &app_settings);
            } else {
                log_message(LOG_ERROR, "[DMON - MAIN] Failed to watch saves directory as it's empty: %s\n",
                            tracker->saves_path);
            }
        } else {
            log_message(LOG_ERROR, "[DMON - MAIN] Saves path is invalid. File watcher not started.\n");
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

            // Cap deltaTime to prevent massive jumps after a long frame (e.g., during file loading).
            // A cap of 0.1f means the game will never simulate more than 1/10th of a second,
            // regardless of how long the freeze was. This turns a stutter into a smooth slowdown.
            const float MAX_DELTATIME = 0.1f;  // frame_target_time * 4.0f; -> 15 fps on 60 fps
            if (deltaTime > MAX_DELTATIME) {
                deltaTime = MAX_DELTATIME;
            }

            // --- Per-Frame Logic ---

            // Increment the time since the last update every frame
            tracker->time_since_last_update += deltaTime;


            // TODO: Not passing an overlay right now
            handle_global_events(tracker, nullptr, &app_settings, &is_running, &settings_opened, &deltaTime);


            // Close immediately if app not running
            if (!is_running) break;


            // Check if settings.json has been modified (by UI or external editor)
            // Single point of truth for tracker data, triggered by "Apply" button
            if (SDL_SetAtomicInt(&g_settings_changed, 0) == 1) {
                log_message(LOG_INFO, "[MAIN] Settings changed. Re-initializing template and file watcher.\n");

                // To prevent deadlocks, we must fully de-initialize and re-initialize the dmon watcher
                dmon_deinit();
                // IMPORTANT: AFTER dmon_deinit ALL OLD WATCHER IDS ARE INVALID
                // Reset them to a known empty state
                saves_watcher_id = {0};
                notes_watcher_id = {0};
                dmon_init();

                // Re-watch the config directory first
                dmon_watch("resources/config/", settings_watch_callback, 0, nullptr);


                // Reload settings from file to get the latest changes
                settings_load(&app_settings);
                log_set_settings(&app_settings); // Update the logger with the new settings

                bool overlay_should_be_enabled = app_settings.enable_overlay;
                bool overlay_is_running = false;
#ifdef _WIN32
                overlay_is_running = tracker->overlay_process_info.hProcess != nullptr;
#else
                overlay_is_running = tracker->overlay_pid > 0;
#endif

                if (overlay_should_be_enabled && !overlay_is_running) {
                    log_message(LOG_INFO, "[MAIN] Enabling overlay process.\n");
                    char exe_path[MAX_PATH_LENGTH];
                    if (get_executable_path(exe_path, sizeof(exe_path))) {
#ifdef _WIN32
                        // Use memset for STARTUPINFOA initialization
                        STARTUPINFOA si;
                        memset(&si, 0, sizeof(si));

                        si.cb = sizeof(si);
                        char args[MAX_PATH_LENGTH + 16];
                        snprintf(args, sizeof(args), "\"%s\" --overlay", exe_path);

                        if (CreateProcessA(nullptr, args, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &tracker->overlay_process_info)) {
                            log_message(LOG_ERROR, "[MAIN] Overlay process started with PID: %lu\n", tracker->overlay_process_info.dwProcessId);
                        } else {
                            log_message(LOG_ERROR, "[MAIN] Failed to create overlay process. Error code: %lu\n", GetLastError());
                        }
#else
                        pid_t pid = fork();
                        if (pid == 0) { // Child process
                            char* args[] = {exe_path, (char*)"--overlay", nullptr};
                            execv(exe_path, args);
                            // If execv returns, it's an error
                            log_message(LOG_ERROR, "[MAIN] Child process execv failed.\n");
                            _exit(1);
                        } else if (pid > 0) { // Parent process
                            tracker->overlay_pid = pid;
                            log_message(LOG_INFO, "[MAIN] Overlay process started with PID: %d\n", pid);
                        } else {
                            log_message(LOG_ERROR, "[MAIN] Failed to fork overlay process.\n");
                        }
#endif
                    } else {
                        log_message(LOG_ERROR, "[MAIN] Could not get executable path to start overlay.\n");
                    }
                } else if (!overlay_should_be_enabled && overlay_is_running) {
                    log_message(LOG_INFO, "[MAIN] Disabling overlay process.\n");
#ifdef _WIN32
                    TerminateProcess(tracker->overlay_process_info.hProcess, 0);
                    CloseHandle(tracker->overlay_process_info.hProcess);
                    CloseHandle(tracker->overlay_process_info.hThread);
                    memset(&tracker->overlay_process_info, 0, sizeof(tracker->overlay_process_info));
#else
                    kill(tracker->overlay_pid, SIGKILL);
                    tracker->overlay_pid = 0;
#endif
                }

                // Update the tracker with the new paths and template data
                tracker_reinit_template(tracker, &app_settings);

                // Start watching the new directory
                if (path_exists(tracker->saves_path)) {
                    if (strlen(tracker->saves_path) > 0) {
                        log_message(LOG_INFO, "[MAIN] Now watching new saves directory: %s\n", tracker->saves_path);

                        saves_watcher_id = dmon_watch(tracker->saves_path, global_watch_callback,
                                                      DMON_WATCHFLAGS_RECURSIVE,
                                                      nullptr);
                    }
                }

                // Also re-watch the new notes file directory
                if (notes_watcher_id.id > 0) {
                    notes_watcher_id.id = 0;
                }

                if (app_settings.notes_path[0] != '\0') {
                    char notes_dir[MAX_PATH_LENGTH];
                    if (get_parent_directory(app_settings.notes_path, notes_dir, sizeof(notes_dir), 1)) {
                        notes_watcher_id = dmon_watch(notes_dir, notes_watch_callback, 0, nullptr);
                        log_message(LOG_INFO, "[MAIN] Now watching notes directory: %s\n", notes_dir);
                    }
                }

                // Force a data update and apply non-critical changes
                SDL_SetAtomicInt(&g_needs_update, 1);
                frame_target_time = 1000.0f / app_settings.fps;
                SDL_SetWindowAlwaysOnTop(tracker->window, app_settings.tracker_always_on_top);
            }

            // Check if the notes file has been changed externally
            if (SDL_SetAtomicInt(&g_notes_changed, 0) == 1) {
                log_message(LOG_INFO, "[MAIN] Notes file changed. Reloading.\n");
                tracker_load_notes(tracker, &app_settings);
            }


            // Check if dmon (or manual update through custom goal) has requested an update
            // Atomically check if the flag is 1
            if (SDL_GetAtomicInt(&g_needs_update) == 1) {

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

                // Update TITLE of the tracker window with some info, similar to the debug print
                tracker_update_title(tracker, &app_settings);

                // --- DATA WRITING TO COMMUNICATE WITH OVERLAY ---
                if (tracker->p_shared_data) {
#ifdef _WIN32
                    DWORD wait_result = WaitForSingleObject(tracker->h_mutex, 50); // Wait up to 50ms
                    if (wait_result == WAIT_OBJECT_0) {
#else
                    if (sem_wait(tracker->mutex) == 0) {
#endif
                        // --- Critical Section: We have the lock ---
                        memcpy(&tracker->p_shared_data->template_data, tracker->template_data, sizeof(TemplateData));
                        strncpy(tracker->p_shared_data->world_name, tracker->world_name, MAX_PATH_LENGTH - 1);
                        tracker->p_shared_data->time_since_last_update = tracker->time_since_last_update;
                        // --- End of Critical Section ---
#ifdef _WIN32
                        ReleaseMutex(tracker->h_mutex);
#else
                        sem_post(tracker->mutex);
#endif
                    }
                }

                // Check if the update was triggered by a game file change or hotkey press of counter
                if (SDL_SetAtomicInt(&g_game_data_changed, 0) == 1) {
                    // Reset the timer as the update has happened
                    tracker->time_since_last_update = 0.0f;

                    // REDUCE SPAM, only print when update was triggered by game file change
                    // Print debug status (individual prints use log_message function)
                    // log_message function prints based on print_debug_status setting
                    tracker_print_debug_status(tracker, &app_settings);
                }
            }

            // TODO: Remove
            // // Overlay animations should run every frame, if it exists
            // if (overlay) {
            //     overlay_update(overlay, &deltaTime, tracker, &app_settings);
            // }

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


            // TODO: Remove
            // // Overlay window gets rendered using SDL, if it exists
            // if (overlay) {
            //     overlay_render(overlay, tracker, &app_settings);
            // }


            // --- Frame limiting ---
            const float frame_time = (float) SDL_GetTicks() - (float) current_time;
            if (frame_time < frame_target_time) {
                SDL_Delay((Uint32) (frame_target_time - frame_time));
            }
        }
        exit_status = EXIT_SUCCESS;
    }

    // Ensure the overlay process is terminated when the main app exits
    bool overlay_is_running = false;
#ifdef _WIN32
    overlay_is_running = tracker && tracker->overlay_process_info.hProcess != nullptr;
#else
    overlay_is_running = tracker && tracker->overlay_pid > 0;
#endif
    if (overlay_is_running) {
        log_message(LOG_INFO, "[MAIN] Terminating overlay process on exit.\n");
#ifdef _WIN32
        TerminateProcess(tracker->overlay_process_info.hProcess, 0);
        CloseHandle(tracker->overlay_process_info.hProcess);
        CloseHandle(tracker->overlay_process_info.hThread);
#else
        kill(tracker->overlay_pid, SIGKILL);
#endif
    }

    // IPC CLEANUP

    log_message(LOG_INFO, "[IPC] Cleaning up shared memory and mutex.\n");
    if (tracker && tracker->p_shared_data) {
#ifdef _WIN32
        UnmapViewOfFile(tracker->p_shared_data);
        CloseHandle(tracker->h_map_file);
        CloseHandle(tracker->h_mutex);
#else
        munmap(tracker->p_shared_data, sizeof(SharedData));
        close(tracker->shm_fd);
        shm_unlink(SHARED_MEM_NAME);
        sem_close(tracker->mutex);
        sem_unlink(MUTEX_NAME);
#endif
    }

    if (dmon_initialized) {
        dmon_deinit();
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    tracker_free(&tracker, &app_settings);
    // overlay_free(&overlay, &app_settings); // TODO: Remove
    SDL_Quit(); // This is ONCE for all windows

    // Close logger at the end
    log_close();

    // One happy path
    return exit_status;
}
