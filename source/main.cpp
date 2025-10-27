// Copyright (c) 2025 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 24.06.2025.
//

#include <ctime>
#include "path_utils.h"

// TODO: Add this line to enable the update test
// Make update_temp folder in the same directory as the executable and put .zip file extracted in there
// This flag needs to be enabled ONLY on the version before the update
// #define MANUAL_UPDATE_TEST

// Temporarily disable specific warnings for the dmon.h library inclusion on Clang (macOS)
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif

extern "C" {
#define DMON_IMPL // Required for dmon
#include "dmon.h"
#include <cJSON.h>

// #define MINIZ_IMPLEMENTATION // TODO: Remove
#include "external/miniz.h"
}

// Restore the warnings
#ifdef __clang__
#pragma clang diagnostic pop
#endif

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
#include "temp_creator.h"
#include "overlay.h"
#include "settings.h"
#include "global_event_handler.h"
#include "path_utils.h" // Include for find_player_data_files
#include "settings_utils.h" // Include for AppSettings and version checking
#include "logger.h"
#include "update_checker.h" // For update checker

// ImGUI imports
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/imgui_impl_sdlrenderer3.h"

// Platform specific includes
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h> // For fork, execv, kill
#include <sys/types.h> // For pid_t
#include <sys/wait.h> // For waitpid
#include <signal.h> // For SIGKILL
#include <fcntl.h>     // For O_* constants
#include <sys/mman.h>  // For shared memory
#include <semaphore.h> // For named semaphores (acting as mutexes)
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <libgen.h>
#endif

SDL_AtomicInt g_needs_update;
SDL_AtomicInt g_settings_changed; // Watching when settings.json is modified to re-init paths
SDL_AtomicInt g_game_data_changed; // When game data is modified, custom counter is changed or manually override changed
SDL_AtomicInt g_notes_changed;
SDL_AtomicInt g_apply_button_clicked;
SDL_AtomicInt g_templates_changed;

// Change the global flag from a bool to our new enum.
ForceOpenReason g_force_open_reason = FORCE_OPEN_NONE;


static bool g_show_release_notes_on_startup = false;
// After auto-installing update it shows link to github release notes
static bool show_release_notes_window = false; // For rendering the release notes window
static bool show_welcome_window = false; // For rendering the welcome window on startup
static char release_url_buffer[256] = {0};
static SDL_Texture *g_logo_texture = nullptr; // Loading the advancely logo

#ifdef _WIN32
// Warning the user with a popup window if file path contains non-ascii characters
// Especially a problem on windows
static bool path_contains_non_ascii(const char *path) {
    for (const char *p = path; *p; ++p) {
        // Check if the character has a value outside the standard ASCII range
        if ((unsigned char) *p > 127) {
            return true;
        }
    }
    return false;
}
#endif

// All builds now have the resources folder on the same level as the executable or .app bundle
static void find_and_set_resource_path(char *path_buffer, size_t buffer_size) {
    // For all builds (Windows, macOS, Linux and windows), find the executable's path.
    char exe_path[MAX_PATH_LENGTH];
    if (!get_executable_path(exe_path, sizeof(exe_path))) {
        strncpy(path_buffer, "resources", buffer_size - 1); // Fallback
        path_buffer[buffer_size - 1] = '\0';
        return;
    }

#if defined(__APPLE__)
    // On macOS, if running from a bundle, the resources folder is outside the .app directory.
    // The executable is at .../Advancely.app/Contents/MacOS/Advancely
    // The resources are at .../resources/
    // So we need to go up 3 levels from the executable.
    if (strstr(exe_path, ".app/") != NULL) {
        char bundle_root_path[MAX_PATH_LENGTH];
        if (get_parent_directory(exe_path, bundle_root_path, sizeof(bundle_root_path), 3)) {
            snprintf(path_buffer, buffer_size, "%s/resources", bundle_root_path);
            if (path_exists(path_buffer)) {
                return; // Found it.
            }
        }
    }
#endif

    char exe_dir[MAX_PATH_LENGTH];
    // For Windows, Linux, and non-bundle macOS builds (don't exist), 'resources' is next to the executable.
    if (!get_parent_directory(exe_path, exe_dir, sizeof(exe_dir), 1)) {
        strncpy(path_buffer, "resources", buffer_size - 1);
        path_buffer[buffer_size - 1] = '\0';
        return;
    }

    snprintf(path_buffer, buffer_size, "%s/resources", exe_dir);
    if (path_exists(path_buffer)) {
        return; // Found it, we're done
    }

    // Ultimate fallback to a relative path if all else fails.
    strncpy(path_buffer, "resources", buffer_size - 1);
    path_buffer[buffer_size - 1] = '\0';
}


/**
 * @brief Serializes the TemplateData into a flat byte buffer.
 * This "flattens" the complex data structure so it can be sent to another process.
 */
static size_t serialize_template_data(TemplateData *td, char *buffer) {
    if (!td || !buffer) return 0;

    char *head = buffer;

    // 1. Copy the main TemplateData struct.
    memcpy(head, td, sizeof(TemplateData));
    head += sizeof(TemplateData);

    // 2. Copy advancements and their criteria.
    for (int i = 0; i < td->advancement_count; i++) {
        memcpy(head, td->advancements[i], sizeof(TrackableCategory));
        head += sizeof(TrackableCategory);
        for (int j = 0; j < td->advancements[i]->criteria_count; j++) {
            memcpy(head, td->advancements[i]->criteria[j], sizeof(TrackableItem));
            head += sizeof(TrackableItem);
        }
    }

    // 3. Copy stats and their criteria.
    for (int i = 0; i < td->stat_count; i++) {
        memcpy(head, td->stats[i], sizeof(TrackableCategory));
        head += sizeof(TrackableCategory);
        for (int j = 0; j < td->stats[i]->criteria_count; j++) {
            memcpy(head, td->stats[i]->criteria[j], sizeof(TrackableItem));
            head += sizeof(TrackableItem);
        }
    }

    // 4. Copy multi-stage goals and their stages.
    for (int i = 0; i < td->multi_stage_goal_count; i++) {
        memcpy(head, td->multi_stage_goals[i], sizeof(MultiStageGoal));
        head += sizeof(MultiStageGoal);
        for (int j = 0; j < td->multi_stage_goals[i]->stage_count; j++) {
            memcpy(head, td->multi_stage_goals[i]->stages[j], sizeof(SubGoal));
            head += sizeof(SubGoal);
        }
    }

    // 5. Copy unlocks.
    for (int i = 0; i < td->unlock_count; i++) {
        memcpy(head, td->unlocks[i], sizeof(TrackableItem));
        head += sizeof(TrackableItem);
    }

    // 6. Copy custom goals.
    for (int i = 0; i < td->custom_goal_count; i++) {
        memcpy(head, td->custom_goals[i], sizeof(TrackableItem));
        head += sizeof(TrackableItem);
    }

    return head - buffer; // Return the total size of the serialized data.
}


/**
 * @brief Deserializes a flat byte buffer back into a valid TemplateData structure.
 * This "rebuilds" the data in the overlay's memory space with new, valid pointers.
 */
static void deserialize_template_data(char *buffer, TemplateData *target_td) {
    if (!buffer || !target_td) return;

    char *head = buffer;

    // 1. Read the main TemplateData struct to get the counts.
    memcpy(target_td, head, sizeof(TemplateData));
    head += sizeof(TemplateData);

    // 2. Allocate memory for all the pointer arrays in the overlay's address space.
    target_td->advancements = (TrackableCategory **) calloc(target_td->advancement_count, sizeof(TrackableCategory *));
    target_td->stats = (TrackableCategory **) calloc(target_td->stat_count, sizeof(TrackableCategory *));
    target_td->multi_stage_goals = (MultiStageGoal **) calloc(target_td->multi_stage_goal_count,
                                                              sizeof(MultiStageGoal *));
    target_td->unlocks = (TrackableItem **) calloc(target_td->unlock_count, sizeof(TrackableItem *));
    target_td->custom_goals = (TrackableItem **) calloc(target_td->custom_goal_count, sizeof(TrackableItem *));

    // 3. Read advancements and their criteria.
    for (int i = 0; i < target_td->advancement_count; i++) {
        target_td->advancements[i] = (TrackableCategory *) calloc(1, sizeof(TrackableCategory));
        memcpy(target_td->advancements[i], head, sizeof(TrackableCategory));
        head += sizeof(TrackableCategory);
        target_td->advancements[i]->criteria = (TrackableItem **) calloc(
            target_td->advancements[i]->criteria_count, sizeof(TrackableItem *));
        for (int j = 0; j < target_td->advancements[i]->criteria_count; j++) {
            target_td->advancements[i]->criteria[j] = (TrackableItem *) calloc(1, sizeof(TrackableItem));
            memcpy(target_td->advancements[i]->criteria[j], head, sizeof(TrackableItem));
            head += sizeof(TrackableItem);
        }
    }

    // 4. Read stats and their criteria.
    for (int i = 0; i < target_td->stat_count; i++) {
        target_td->stats[i] = (TrackableCategory *) calloc(1, sizeof(TrackableCategory));
        memcpy(target_td->stats[i], head, sizeof(TrackableCategory));
        head += sizeof(TrackableCategory);
        target_td->stats[i]->criteria = (TrackableItem **) calloc(target_td->stats[i]->criteria_count,
                                                                  sizeof(TrackableItem *));
        for (int j = 0; j < target_td->stats[i]->criteria_count; j++) {
            target_td->stats[i]->criteria[j] = (TrackableItem *) calloc(1, sizeof(TrackableItem));
            memcpy(target_td->stats[i]->criteria[j], head, sizeof(TrackableItem));
            head += sizeof(TrackableItem);
        }
    }

    // 5. Read multi-stage goals and their stages.
    for (int i = 0; i < target_td->multi_stage_goal_count; i++) {
        target_td->multi_stage_goals[i] = (MultiStageGoal *) calloc(1, sizeof(MultiStageGoal));
        memcpy(target_td->multi_stage_goals[i], head, sizeof(MultiStageGoal));
        head += sizeof(MultiStageGoal);
        target_td->multi_stage_goals[i]->stages = (SubGoal **) calloc(target_td->multi_stage_goals[i]->stage_count,
                                                                      sizeof(SubGoal *));
        for (int j = 0; j < target_td->multi_stage_goals[i]->stage_count; j++) {
            target_td->multi_stage_goals[i]->stages[j] = (SubGoal *) calloc(1, sizeof(SubGoal));
            memcpy(target_td->multi_stage_goals[i]->stages[j], head, sizeof(SubGoal));
            head += sizeof(SubGoal);
        }
    }

    // 6. Read unlocks.
    for (int i = 0; i < target_td->unlock_count; i++) {
        target_td->unlocks[i] = (TrackableItem *) calloc(1, sizeof(TrackableItem));
        memcpy(target_td->unlocks[i], head, sizeof(TrackableItem));
        head += sizeof(TrackableItem);
    }

    // 7. Read custom goals.
    for (int i = 0; i < target_td->custom_goal_count; i++) {
        target_td->custom_goals[i] = (TrackableItem *) calloc(1, sizeof(TrackableItem));
        memcpy(target_td->custom_goals[i], head, sizeof(TrackableItem));
        head += sizeof(TrackableItem);
    }
}


static void free_deserialized_data(TemplateData *td) {
    if (!td) return;

    if (td->advancements) {
        for (int i = 0; i < td->advancement_count; i++) {
            if (td->advancements[i]) {
                if (td->advancements[i]->criteria) {
                    for (int j = 0; j < td->advancements[i]->criteria_count; j++) {
                        free(td->advancements[i]->criteria[j]);
                    }
                    free(td->advancements[i]->criteria);
                }
                free(td->advancements[i]);
            }
        }
        free(td->advancements);
    }

    if (td->stats) {
        for (int i = 0; i < td->stat_count; i++) {
            if (td->stats[i]) {
                if (td->stats[i]->criteria) {
                    for (int j = 0; j < td->stats[i]->criteria_count; j++) {
                        free(td->stats[i]->criteria[j]);
                    }
                    free(td->stats[i]->criteria);
                }
                free(td->stats[i]);
            }
        }
        free(td->stats);
    }

    if (td->multi_stage_goals) {
        for (int i = 0; i < td->multi_stage_goal_count; i++) {
            if (td->multi_stage_goals[i]) {
                if (td->multi_stage_goals[i]->stages) {
                    for (int j = 0; j < td->multi_stage_goals[i]->stage_count; j++) {
                        free(td->multi_stage_goals[i]->stages[j]);
                    }
                    free(td->multi_stage_goals[i]->stages);
                }
                free(td->multi_stage_goals[i]);
            }
        }
        free(td->multi_stage_goals);
    }

    if (td->unlocks) {
        for (int i = 0; i < td->unlock_count; i++) {
            free(td->unlocks[i]);
        }
        free(td->unlocks);
    }

    if (td->custom_goals) {
        for (int i = 0; i < td->custom_goal_count; i++) {
            free(td->custom_goals[i]);
        }
        free(td->custom_goals);
    }

    // Set pointers to nullptr after freeing to prevent double-freeing
    memset(td, 0, sizeof(TemplateData));
}


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
    (void) watch_id;
    (void) rootdir;
    (void) oldfilepath;
    (void) user;

    // We only care about file modifications
    if (action == DMON_ACTION_MODIFY) {
        // The filepath from dmon is just the filename. We need to check if it's our notes file.
        // We can't access AppSettings here, so we just signal a generic notes change.
        const char *ext = strrchr(filepath, '.');
        if (ext && strcmp(ext, ".txt") == 0 && strstr(filepath, "_notes")) {
            SDL_SetAtomicInt(&g_notes_changed, 1);
        }
    }
}


// Renders a welcome message window with the advancely logo and a small tutorial on startup depending on setting
static void welcome_render_gui(bool *p_open, AppSettings *app_settings, Tracker *tracker, SDL_Texture *logo_texture) {
    if (!*p_open) {
        return;
    }

    // This flag tracks the state of the checkbox
    static bool dont_show_again = !app_settings->show_welcome_on_startup;

    ImGui::Begin("Welcome to Advancely!", p_open, ImGuiWindowFlags_AlwaysAutoResize);

    if (logo_texture) {
        float w, h;
        SDL_GetTextureSize(logo_texture, &w, &h);
        const float target_width = ADVANCELY_LOGO_SIZE;
        float aspect_ratio = h / w;
        ImVec2 new_size = ImVec2(target_width, target_width * aspect_ratio);
        ImGui::Image((ImTextureID) logo_texture, new_size);
        ImGui::Spacing();
    }

    ImGui::TextWrapped("Thank you for using Advancely!");
    ImGui::TextWrapped("A highly customizable and interactive tool to track your Minecraft progress.");
    ImGui::TextWrapped("If you have any issues/suggestions you can find more info on the GitHub page.");
    ImGui::Separator();
    ImGui::Text("Getting Started:");
    ImGui::BulletText("Pan the View: Hold Right-Click or Middle-Click and drag.");
    ImGui::BulletText("Zoom: Use the Mouse Wheel.");
    ImGui::BulletText("Lock Layout: Press SPACE to prevent items from rearranging.");
    ImGui::BulletText("Open Settings: Press ESC to configure everything.");
    ImGui::BulletText("A lot more info can be found when hovering over certain elements.");
    ImGui::Separator();

    ImGui::Text("For more information, visit the main");
    ImGui::SameLine();
    // Style the text to look like a link
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
    ImGui::Text("GitHub Page");
    ImGui::PopStyleColor();
    // Make the text clickable
    if (ImGui::IsItemClicked()) {
        SDL_OpenURL("https://github.com/LNXSeus/Advancely");
    }
    if (ImGui::IsItemHovered()) {
        char project_main_page_tooltip_buffer[1024];
        snprintf(project_main_page_tooltip_buffer, sizeof(project_main_page_tooltip_buffer),
                 "Opens the project's main page in your browser.");
        ImGui::SetTooltip("%s", project_main_page_tooltip_buffer);
        // ALWAYS SET TOOLTIPS LIKE THIS WITH %s for macOS compiler
    }
    ImGui::SameLine();
    ImGui::Text(".");
    ImGui::Spacing();

    ImGui::Checkbox("Don't show this again", &dont_show_again);

    if (ImGui::Button("Close")) {
        *p_open = false;
    }

    // If the window was just closed (by button or 'X'), save the setting if it changed.
    if (!*p_open && (dont_show_again != !app_settings->show_welcome_on_startup)) {
        app_settings->show_welcome_on_startup = !dont_show_again;
        settings_save(app_settings, tracker->template_data, SAVE_CONTEXT_ALL);
    }

    ImGui::End();
}

// ------------------------------------ END OF STATIC FUNCTIONS ------------------------------------


// ACCESSOR FUNCTIONS

// This is the single source of truth for the resources path.
const char *get_resources_path() {
    static char path[MAX_PATH_LENGTH] = "";
    if (path[0] == '\0') {
        // This runs only ONCE, the very first time this function is called.
        find_and_set_resource_path(path, sizeof(path));
    }
    return path;
}

const char *get_settings_file_path() {
    static char path[MAX_PATH_LENGTH] = "";
    if (path[0] == '\0') {
        // It calls the function above to get the base path and builds its own path.
        snprintf(path, sizeof(path), "%s/config/settings.json", get_resources_path());
    }
    return path;
}

const char *get_notes_dir_path() {
    static char path[MAX_PATH_LENGTH] = "";
    if (path[0] == '\0') {
        snprintf(path, sizeof(path), "%s/notes", get_resources_path());
    }
    return path;
}

const char *get_notes_manifest_path() {
    static char path[MAX_PATH_LENGTH] = "";
    if (path[0] == '\0') {
        // This one cleverly builds on another accessor function.
        snprintf(path, sizeof(path), "%s/manifest.json", get_notes_dir_path());
    }
    return path;
}

int main(int argc, char *argv[]) {
    // Seed random number generator once at startup
    srand(time(nullptr));

    // TODO: Handle proper quitting for github action runners 5s test
    // Add a simple test/version flag that can run without a GUI
    // This communicates with the build.yml file, where the gtimeout or timeout are
    bool is_test_mode = false;
    if (argc > 1) {
        if (strcmp(argv[1], "--version") == 0) {
            printf("Advancely version: %s\n", ADVANCELY_VERSION);
            return 0; // Exit immediately for --version
        }
        if (strcmp(argv[1], "--test-mode") == 0) {
            is_test_mode = true;
            // We no longer exit here, allowing the app to launch
        }
    }


    // TODO: DEBUG FOR AUTO-UPDATE TESTING -> SET FLAG AT THE TOP OF THE FILE
#ifdef MANUAL_UPDATE_TEST
    log_init(); // Init logger to see update messages
    printf("[MANUAL TEST] Triggering apply_update()...\n");
    char exe_path[MAX_PATH_LENGTH];
    if (get_executable_path(exe_path, sizeof(exe_path))) {
        if (apply_update(exe_path)) {
            printf("[MANUAL TEST] apply_update() initiated. Exiting application to allow updater to run.\n");
            log_close();
            return EXIT_SUCCESS; // Exit cleanly to let the script take over
        } else {
            printf("[MANUAL TEST] apply_update() failed to start.\n");
        }
    }
    log_close();
    return EXIT_FAILURE; // Exit if it failed
#endif
    // --- END OF TEST BLOCK ---

    // As we're not only using SDL_main() as our entry point
    SDL_SetMainReady();

    // --- Non-ASCII Path Check (Windows-specific) ---
#ifdef _WIN32
    char exe_path_check[MAX_PATH_LENGTH];
    if (get_executable_path(exe_path_check, sizeof(exe_path_check))) {
        if (path_contains_non_ascii(exe_path_check)) {
            // Initialize SDL video subsystem just to be able to show a message box
            if (!SDL_Init(SDL_INIT_VIDEO)) {
                // Fallback if SDL can't even initialize
                log_message(
                    LOG_ERROR, "Critical Path Error: Advancely cannot run from a path with special characters.\n");
                return EXIT_FAILURE;
            }
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                     "Invalid Path Error",
                                     "Advancely cannot run from a folder path that contains special or non-English characters (e.g., ü, ö, ß).\n\n"
                                     "Please move the application to a simple path (e.g., C:\\Users\\YourName\\Desktop\\Advancely) and run it again.",
                                     nullptr);
            SDL_Quit();
            return EXIT_FAILURE;
        }
    }
#endif


    // Check for --updated flag on startup
    if (argc > 1 && strcmp(argv[1], "--updated") == 0) {
        // The --update flag gets passed from the apply_update() function
        // through the update.bat script on restart
        g_show_release_notes_on_startup = true;
        // DEBUG PRINT 1: Confirm the flag was detected.
        log_message(LOG_ERROR, "[DEBUG] --updated flag DETECTED. g_show_release_notes_on_startup is true.\n");
    }

    bool should_exit_after_update_check = false; // To signal exit after updating

    // Post-update logic
    if (g_show_release_notes_on_startup) {
        // DEBUG PRINT 2: Confirm we are entering the logic block to read the file.
        log_message(LOG_ERROR, "[DEBUG] Attempting to open 'update_url.txt'...\n");
        // Create file with something like: // Will have something like: https://github.com/LNXSeus/Advancely/releases/tag/v0.9.51
        FILE *f = fopen("update_url.txt", "r");
        if (f) {
            // DEBUG PRINT 3: Confirm the file was opened successfully.
            log_message(LOG_ERROR, "[DEBUG] 'update_url.txt' opened successfully.\n");
            // Read the URL from the file
            (void) fgets(release_url_buffer, sizeof(release_url_buffer), f);
            fclose(f);

            // Trim trailing newline characters
            release_url_buffer[strcspn(release_url_buffer, "\r\n")] = 0;

            // DEBUG PRINT 4: Show the content read from the file.
            log_message(LOG_ERROR, "[DEBUG] URL read from file: \"%s\"", release_url_buffer);

            if (release_url_buffer[0] != '\0') {
                show_release_notes_window = true; // Set the flag for the UI loop
                // DEBUG PRINT 5: Confirm the flag to show the window is being set.
                log_message(LOG_ERROR, "[DEBUG] URL is valid. show_release_notes_window set to true.\n");
            } else {
                log_message(LOG_ERROR, "[DEBUG] URL is empty. Window will not be shown.\n");
            }
        } else {
            // DEBUG PRINT 3 (Failure Case): This is the most likely error when testing manually.
            log_message(
                LOG_ERROR,
                "[DEBUG] FAILED to open 'update_url.txt'. The file does not exist in the current directory.\n");
        }
        // Whether it succeeded or not, delete the flag file and unset the global flag
        remove("update_url.txt");
        g_show_release_notes_on_startup = false;
    }

    bool is_overlay_mode = false;
    if (argc > 1 && strcmp(argv[1], "--overlay") == 0) {
        // Additional command line "--overlay"
        is_overlay_mode = true;
    }

    // If we are in overlay mode, run a separate, simplified main loop.
    if (is_overlay_mode) {
        log_init(); // Each process needs its own log
        log_message(LOG_INFO, "[OVERLAY PROCESS] Starting in overlay-only mode.\n");

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

        Overlay *overlay = nullptr;
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
            log_message(LOG_ERROR, "[OVERLAY IPC] Failed to open mutex. Is the tracker running? Error: %lu\n",
                        GetLastError());
            overlay_free(&overlay, &settings);
            return 1;
        }

        overlay->h_map_file = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);
        overlay->p_shared_data = (SharedData *) MapViewOfFile(overlay->h_map_file, FILE_MAP_ALL_ACCESS, 0, 0,
                                                              sizeof(SharedData));

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
        overlay->p_shared_data = (SharedData *) mmap(0, sizeof(SharedData), PROT_READ, MAP_SHARED, overlay->shm_fd, 0);

        if (overlay->p_shared_data == MAP_FAILED) {
            log_message(LOG_ERROR, "[OVERLAY IPC] Failed to map shared memory. Is the tracker running?\n");
            overlay_free(&overlay, &settings); // Cleans up SDL stuff as well
            return 1;
        }
#endif

        // This local struct will hold the REBUILT data.
        TemplateData live_template_data{};

        // We create a "proxy" tracker struct to pass to the render functions.
        Tracker proxy_tracker{};
        proxy_tracker.template_data = &live_template_data; // Point it to our local rebuilt data


        bool is_running = true;
        Uint32 last_frame_time = SDL_GetTicks();

        // Unified OVERLAY LOOP -------------------------------------------------
        while (is_running) {
            // Separate loop for the overlay process -> with it's own framerate

            Uint32 current_time = SDL_GetTicks();
            float deltaTime = (float) (current_time - last_frame_time) / 1000.0f;
            last_frame_time = current_time;


            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    is_running = false;
                }
                // Minimal event handling for the overlay window
                overlay_events(overlay, &event, &is_running, &deltaTime, &settings);
            }

            if (overlay->p_shared_data) {
#ifdef _WIN32
                DWORD wait_result = WaitForSingleObject(overlay->h_mutex, 50);
                if (wait_result == WAIT_OBJECT_0) {
#else
                    if (sem_wait(overlay->mutex) == 0) {
#endif
                    // --- Critical Section: We have the lock ---
                    if (overlay->p_shared_data->data_size > 0) {
                        // Define the same header struct to read the data.
                        typedef struct {
                            char world_name[MAX_PATH_LENGTH];
                            float time_since_last_update;
                        } OverlayIPCHeader;

                        OverlayIPCHeader header;
                        char *buffer_head = overlay->p_shared_data->buffer;

                        // 1. Read the header from the start of the buffer.
                        memcpy(&header, buffer_head, sizeof(OverlayIPCHeader));
                        buffer_head += sizeof(OverlayIPCHeader); // Move pointer past the header.

                        // 2. Update the proxy tracker with the live data from the header.
                        strncpy(proxy_tracker.world_name, header.world_name, MAX_PATH_LENGTH - 1);
                        proxy_tracker.world_name[MAX_PATH_LENGTH - 1] = '\0';
                        proxy_tracker.time_since_last_update = header.time_since_last_update;

                        // 3. Free the template data from the PREVIOUS frame.
                        free_deserialized_data(&live_template_data);

                        // 4. Deserialize the main template data, which starts AFTER the header.
                        deserialize_template_data(buffer_head, &live_template_data);
                    }
                    // --- End of Critical Section ---
#ifdef _WIN32
                    ReleaseMutex(overlay->h_mutex);
#else
                    sem_post(overlay->mutex);
#endif
                }
            }


            // The update and render functions now receive live data!
            overlay_update(overlay, &deltaTime, &proxy_tracker, &settings);
            overlay_render(overlay, &proxy_tracker, &settings);

            float frame_target_time = 1000.0f / settings.overlay_fps; // Overlay has it's own FPS limit
            const float frame_time = (float) SDL_GetTicks() - (float) current_time;
            if (frame_time < frame_target_time) {
                SDL_Delay((Uint32) (frame_target_time - frame_time));
            }
        } // END OF OVERLAY LOOP

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

        // Final cleanup call before exiting
        free_deserialized_data(&live_template_data);

        overlay_free(&overlay, &settings);
        TTF_Quit();
        SDL_Quit();
        log_close();

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
        settings_save(&app_settings, nullptr, SAVE_CONTEXT_ALL); // Save complete settings back to the file
    }

    // Control welcome window visibility based on settings
    show_welcome_window = app_settings.show_welcome_on_startup;

    log_set_settings(&app_settings); // Give the logger access to the settings

    Tracker *tracker = nullptr; // pass address to function

    // Variable to hold the ID of our saves watcher
    dmon_watch_id saves_watcher_id = {0}; // Initialize to a known invalid state (id=0)

    // Initialize SDL ttf
    if (!TTF_Init()) {
        log_message(LOG_ERROR, "[MAIN] Failed to initialize SDL_ttf: %s\n", SDL_GetError());
        log_close();
        return EXIT_FAILURE;
    }

    if (tracker_new(&tracker, &app_settings)) {
        // Check for updates on startup
        if (app_settings.check_for_updates) {
            bool was_always_on_top = app_settings.tracker_always_on_top;
            if (was_always_on_top) {
                SDL_SetWindowAlwaysOnTop(tracker->window, false);
            }
            char latest_version_str[64];
            char download_url[256];
            char release_page_url[256];
            if (check_for_updates(ADVANCELY_VERSION, latest_version_str, sizeof(latest_version_str), download_url,
                                  sizeof(download_url), release_page_url, sizeof(release_page_url))) {
                char message_buffer[2048];
                snprintf(message_buffer, sizeof(message_buffer),
                         "A new version of Advancely is available!\n\n"
                         "  Your Version: %s\n"
                         "  Latest Version: %s\n\n"
                         "--- IMPORTANT: How to Protect Your Modified Templates ---\n"
                         "The updater replaces official template files. To protect your changes, please use the Template Editor BEFORE updating:\n\n"
                         " • If you only changed DISPLAY NAMES:\n"
                         "   Use the 'Copy Language' feature to create a new language file. This keeps your names safe while allowing the template's structure to receive updates.\n\n"
                         " • If you changed GOALS, ICONS, or functionality:\n"
                         "   Use the 'Copy Template' feature to create a separate version with a unique name or optional flag (e.g., '_custom'). Your copy will not be overwritten.\n\n"
                         "----------------------------------------------------------------\n"
                         "The update will KEEP your settings.json and _notes.txt files.\n"
                         "It will REPLACE the main executable, libraries (.dll, .dylib, .so), and official files in the 'resources' folder (fonts, icons, templates, etc.).\n\n"
                         "Options:\n"
                         " - Update: Install the new version now.\n"
                         " - Templates: Open your local templates folder to make backups.\n"
                         " - Official: View official templates online.\n"
                         " - Later: Skip updating until the next restart.\n\n"
                         "Would you like to install it now?\n"
                         "Expect 3 more windows after clicking \"Update\" that you need to confirm with \"OK\".\n\n"
                         "----------------------------------------------------------------\n"
                         "**DO NOT CLOSE ADVANCELY DURING THE UPDATE PROCESS!**",
                         ADVANCELY_VERSION, latest_version_str);


                const SDL_MessageBoxButtonData buttons[] = {
                    {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Update"},
                    {0, 2, "Templates"},
                    {0, 3, "Official"},
                    {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Later"},

                };
                const SDL_MessageBoxData messageboxdata = {
                    SDL_MESSAGEBOX_INFORMATION,
                    tracker->window,
                    "Update Available",
                    message_buffer,
                    SDL_arraysize(buttons),
                    buttons,
                    nullptr
                };

                int buttonid = -1;
                bool decision_made = false;

                while (!decision_made) {
                    SDL_ShowMessageBox(&messageboxdata, &buttonid);

                    switch (buttonid) {
                        case 1: // Install Update
                        case 0: // Later
                        case -1: // User closed with 'X'
                            decision_made = true;
                            break;

                        case 2: // Open Template Folder
                        {
                            char templates_path[MAX_PATH_LENGTH];
                            snprintf(templates_path, sizeof(templates_path), "%s/templates", get_resources_path());
#ifdef _WIN32
                            char command[MAX_PATH_LENGTH + 32];
                            path_to_windows_native(templates_path); // Convert to backslashes for explorer
                            snprintf(command, sizeof(command), "explorer \"%s\"", templates_path);
                            (void) system(command);
#else // macOS and Linux
                            pid_t pid = fork();
                            if (pid == 0) {  // Child process
#if __APPLE__
                            char *args[] = {(char *) "open", templates_path, nullptr};
#else
                            char *args[] = {(char *) "xdg-open", templates_path, nullptr};
#endif
                            execvp(args[0], args);
                            _exit(127); // Exit if exec fails
                            } else if (pid < 0) {
                                log_message(LOG_ERROR, "[MAIN] Failed to fork process to open folder.\n");
                            }
#endif
                            break; // Re-shows the message box
                        }

                        case 3: // View Templates Online
                        {
                            const char *url = "https://github.com/LNXSeus/Advancely#Officially-Added-Templates";
#ifdef _WIN32
                            char command[1024];
                            snprintf(command, sizeof(command), "start %s", url);
                            (void) system(command);
#else // macOS and Linux
                            pid_t pid = fork();
                            if (pid == 0) {  // Child process
#if __APPLE__
                            char *args[] = {(char *) "open", (char *) url, nullptr};
#else
                            char *args[] = {(char *) "xdg-open", (char *) url, nullptr};
#endif
                            execvp(args[0], args);
                            _exit(127); // Exit if exec fails
                            } else if (pid < 0) {
                                log_message(LOG_ERROR, "[MAIN] Failed to fork process to open URL.\n");
                            }
#endif
                            break; // Re-shows the message box
                        }
                    }
                }

                if (buttonid == 1) {
                    // -Save release page URL to a file before updating
                    FILE *url_file = fopen("update_url.txt", "w");
                    if (url_file) {
                        fputs(release_page_url, url_file);
                        fclose(url_file);
                        log_message(LOG_INFO, "[UPDATE] Release URL saved to update_url.txt.\n");
                    }

                    // User clicked "Install Update"
                    show_error_message("Downloading Update",
                                       "Downloading the latest version,\nplease wait after clicking \"OK\"...");

                    if (download_update_zip(download_url)) {
                        show_error_message("Download Complete",
                                           "Update downloaded.\nExtracting files after clicking \"OK\"...");

                        mz_zip_archive zip_archive;
                        memset(&zip_archive, 0, sizeof(zip_archive));

                        if (mz_zip_reader_init_file(&zip_archive, "update.zip", 0)) {
                            log_message(
                                LOG_INFO,
                                "[UPDATE] Successfully opened update.zip for extraction.\nClick \"OK\" to continue.\n");
                            const char *temp_dir = "update_temp";

                            // Clean up old temp dir if it exists
                            if (path_exists(temp_dir)) {
                                delete_directory_recursively(temp_dir);
                            }

#ifdef _WIN32
                            _mkdir(temp_dir);
#else
                            mkdir(temp_dir, 0755);
#endif

                            mz_uint i;
                            for (i = 0; i < mz_zip_reader_get_num_files(&zip_archive); i++) {
                                mz_zip_archive_file_stat file_stat;
                                if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) continue;

                                char out_path[MAX_PATH_LENGTH];
                                const char *filename_in_zip = file_stat.m_filename;

                                snprintf(out_path, sizeof(out_path), "%s/%s", temp_dir, filename_in_zip);

                                if (file_stat.m_is_directory) {
#ifdef _WIN32
                                    _mkdir(out_path);
#else
                                    mkdir(out_path, 0755);
#endif
                                } else {
                                    mz_zip_reader_extract_to_file(&zip_archive, i, out_path, 0);
                                }
                            }
                            mz_zip_reader_end(&zip_archive);

                            // Clean up the downloaded zip file
                            remove("update.zip");

                            // Now that files are extracted, apply the update
                            char exe_path[MAX_PATH_LENGTH];
                            if (get_executable_path(exe_path, sizeof(exe_path))) {
                                show_error_message("Update Ready",
                                                   "Advancely will now close to apply the update and then restart automatically.\nClick \"OK\" to continue.");
                                if (apply_update(exe_path)) {
                                    // The updater script has been launched.
                                    // Signal that the application should exit.
                                    should_exit_after_update_check = true;
                                }
                            } else {
                                show_error_message("Update Error", "Could not find application path to restart.");
                            }
                        } else {
                            log_message(LOG_ERROR, "[UPDATE] Failed to open update.zip for extraction.\n");
                            show_error_message("Update Error", "Failed to open the downloaded update file.");
                            remove("update.zip");
                        }
                    } else {
                        show_error_message("Update Error",
                                           "Failed to download the update.\nPlease check advancely_log.txt for details.");
                    }
                }
            }

            if (was_always_on_top) {
                SDL_SetWindowAlwaysOnTop(tracker->window, true);
            }
        }

        if (should_exit_after_update_check) {
            // Free resources and exit before the main loop starts
            tracker_free(&tracker, &app_settings);
            TTF_Quit();
            SDL_Quit();
            log_close();
            return EXIT_SUCCESS;
        }

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
        tracker->h_map_file = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedData),
                                                 SHARED_MEM_NAME);
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
        tracker->p_shared_data = (SharedData *) MapViewOfFile(tracker->h_map_file, FILE_MAP_ALL_ACCESS, 0, 0,
                                                              sizeof(SharedData));
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
        (void) ftruncate(tracker->shm_fd, sizeof(SharedData));

        // Map the shared memory object
        tracker->p_shared_data = (SharedData *) mmap(0, sizeof(SharedData), PROT_WRITE, MAP_SHARED, tracker->shm_fd, 0);
#endif


        // Initialize ImGUI
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        (void) io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

        ImGui::StyleColorsDark(); // Or ImGui::StyleColorsClassic()

        // Apply Custom UI Theme Colors
        ImGuiStyle &style = ImGui::GetStyle();
        style.Colors[ImGuiCol_Text] = ImVec4((float) app_settings.ui_text_color.r / 255.f,
                                             (float) app_settings.ui_text_color.g / 255.f,
                                             (float) app_settings.ui_text_color.b / 255.f,
                                             (float) app_settings.ui_text_color.a / 255.f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4((float) app_settings.ui_window_bg_color.r / 255.f,
                                                 (float) app_settings.ui_window_bg_color.g / 255.f,
                                                 (float) app_settings.ui_window_bg_color.b / 255.f,
                                                 (float) app_settings.ui_window_bg_color.a / 255.f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4((float) app_settings.ui_frame_bg_color.r / 255.f,
                                                (float) app_settings.ui_frame_bg_color.g / 255.f,
                                                (float) app_settings.ui_frame_bg_color.b / 255.f,
                                                (float) app_settings.ui_frame_bg_color.a / 255.f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4((float) app_settings.ui_frame_bg_hovered_color.r / 255.f,
                                                       (float) app_settings.ui_frame_bg_hovered_color.g / 255.f,
                                                       (float) app_settings.ui_frame_bg_hovered_color.b / 255.f,
                                                       (float) app_settings.ui_frame_bg_hovered_color.a / 255.f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4((float) app_settings.ui_frame_bg_active_color.r / 255.f,
                                                      (float) app_settings.ui_frame_bg_active_color.g / 255.f,
                                                      (float) app_settings.ui_frame_bg_active_color.b / 255.f,
                                                      (float) app_settings.ui_frame_bg_active_color.a / 255.f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4((float) app_settings.ui_title_bg_active_color.r / 255.f,
                                                      (float) app_settings.ui_title_bg_active_color.g / 255.f,
                                                      (float) app_settings.ui_title_bg_active_color.b / 255.f,
                                                      (float) app_settings.ui_title_bg_active_color.a / 255.f);
        style.Colors[ImGuiCol_Button] = ImVec4((float) app_settings.ui_button_color.r / 255.f,
                                               (float) app_settings.ui_button_color.g / 255.f,
                                               (float) app_settings.ui_button_color.b / 255.f,
                                               (float) app_settings.ui_button_color.a / 255.f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4((float) app_settings.ui_button_hovered_color.r / 255.f,
                                                      (float) app_settings.ui_button_hovered_color.g / 255.f,
                                                      (float) app_settings.ui_button_hovered_color.b / 255.f,
                                                      (float) app_settings.ui_button_hovered_color.a / 255.f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4((float) app_settings.ui_button_active_color.r / 255.f,
                                                     (float) app_settings.ui_button_active_color.g / 255.f,
                                                     (float) app_settings.ui_button_active_color.b / 255.f,
                                                     (float) app_settings.ui_button_active_color.a / 255.f);
        style.Colors[ImGuiCol_Header] = ImVec4((float) app_settings.ui_header_color.r / 255.f,
                                               (float) app_settings.ui_header_color.g / 255.f,
                                               (float) app_settings.ui_header_color.b / 255.f,
                                               (float) app_settings.ui_header_color.a / 255.f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4((float) app_settings.ui_header_hovered_color.r / 255.f,
                                                      (float) app_settings.ui_header_hovered_color.g / 255.f,
                                                      (float) app_settings.ui_header_hovered_color.b / 255.f,
                                                      (float) app_settings.ui_header_hovered_color.a / 255.f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4((float) app_settings.ui_header_active_color.r / 255.f,
                                                     (float) app_settings.ui_header_active_color.g / 255.f,
                                                     (float) app_settings.ui_header_active_color.b / 255.f,
                                                     (float) app_settings.ui_header_active_color.a / 255.f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4((float) app_settings.ui_check_mark_color.r / 255.f,
                                                  (float) app_settings.ui_check_mark_color.g / 255.f,
                                                  (float) app_settings.ui_check_mark_color.b / 255.f,
                                                  (float) app_settings.ui_check_mark_color.a / 255.f);

        // Setup Platform/Renderer backends
        ImGui_ImplSDL3_InitForSDLRenderer(tracker->window, tracker->renderer);
        ImGui_ImplSDLRenderer3_Init(tracker->renderer);

        // Load the logo texture once at statup
        char logo_path[MAX_PATH_LENGTH];
        snprintf(logo_path, sizeof(logo_path), "%s%s", get_resources_path(), ADVANCELY_LOGO_PATH);
        if (path_exists(logo_path)) {
            g_logo_texture = IMG_LoadTexture(tracker->renderer, logo_path);
            if (g_logo_texture == nullptr) {
                log_message(LOG_ERROR, "[MAIN] Failed to load logo texture: %s\n", SDL_GetError());
            }
        } else {
            log_message(LOG_ERROR, "[MAIN] Could not find logo texture at %s\n", logo_path);
        }

        // 1. Load the UI Font (replaces Roboto).
        // The first font loaded becomes the default for ImGui. We also get a pointer to it.
        char ui_font_path[MAX_PATH_LENGTH];
        snprintf(ui_font_path, sizeof(ui_font_path), "%s/fonts/%s", get_resources_path(), app_settings.ui_font_name);
        if (path_exists(ui_font_path)) {
            tracker->roboto_font = io.Fonts->AddFontFromFileTTF(ui_font_path, app_settings.ui_font_size);
        } else {
            // Fallback to default if user-selected font is not found
            snprintf(ui_font_path, sizeof(ui_font_path), "%s/fonts/%s", get_resources_path(), DEFAULT_UI_FONT);
            tracker->roboto_font = io.Fonts->AddFontFromFileTTF(ui_font_path, DEFAULT_UI_FONT_SIZE);
            log_message(LOG_ERROR, "[MAIN] UI Font '%s' not found. Falling back to default.\n",
                        app_settings.ui_font_name);
        }

        // 2. Load the Tracker Font (replaces Minecraft) as a secondary font.
        char tracker_font_path[MAX_PATH_LENGTH];
        snprintf(tracker_font_path, sizeof(tracker_font_path), "%s/fonts/%s", get_resources_path(),
                 app_settings.tracker_font_name);
        if (path_exists(tracker_font_path)) {
            tracker->tracker_font = io.Fonts->AddFontFromFileTTF(tracker_font_path, app_settings.tracker_font_size);
        } else {
            // Fallback to default if user-selected font is not found
            snprintf(tracker_font_path, sizeof(tracker_font_path), "%s/fonts/%s", get_resources_path(),
                     DEFAULT_TRACKER_FONT);
            tracker->tracker_font = io.Fonts->AddFontFromFileTTF(tracker_font_path, DEFAULT_TRACKER_FONT_SIZE);
            log_message(LOG_ERROR, "[MAIN] Tracker Font '%s' not found. Falling back to default.\n",
                        app_settings.tracker_font_name);
        }

        // After the tracker is initialized, its saves_path is populated based on the loaded settings.
        // Check if this resolved path is actually valid. If not, set the reason for forcing
        // the settings window to open. We exclude instance tracking because it's expected
        // to fail if no game is running.
        if (!path_exists(tracker->saves_path)) {
            if (app_settings.path_mode == PATH_MODE_AUTO) {
                log_message(LOG_ERROR, "[MAIN] Auto-detected saves path is invalid. Forcing settings open.\n");
                g_force_open_reason = FORCE_OPEN_AUTO_FAIL;
            } else if (app_settings.path_mode == PATH_MODE_MANUAL) {
                log_message(LOG_ERROR, "[MAIN] Manual saves path is invalid. Forcing settings open.\n");
                g_force_open_reason = FORCE_OPEN_MANUAL_FAIL;
            }
        }

        dmon_init();
        dmon_initialized = true;
        SDL_SetAtomicInt(&g_needs_update, 1);
        SDL_SetAtomicInt(&g_settings_changed, 0);
        SDL_SetAtomicInt(&g_game_data_changed, 1);
        SDL_SetAtomicInt(&g_notes_changed, 0);
        SDL_SetAtomicInt(&g_apply_button_clicked, 0);
        SDL_SetAtomicInt(&g_templates_changed, 0);

        // HARDCODED SETTINGS DIRECTORY
        log_message(LOG_INFO, "[DMON - MAIN] Watching config directory: resources/config/\n");

        char dmon_config_path[MAX_PATH_LENGTH];
        snprintf(dmon_config_path, sizeof(dmon_config_path), "%s%s", get_resources_path(), "/config/");
        dmon_watch(dmon_config_path, settings_watch_callback, 0, nullptr);


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

        // Start a timer if test mode is enabled
        Uint32 test_mode_start_time = 0;
        if (is_test_mode) {
            log_message(LOG_INFO, "[MAIN] Test mode enabled. Application will shut down in 5 seconds.\n");
            test_mode_start_time = SDL_GetTicks();
        }

        // Launch overlay if enabled
        if (app_settings.enable_overlay) {
            log_message(LOG_INFO, "[MAIN] Overlay enabled in settings. Launching on startup.\n");
            char exe_path[MAX_PATH_LENGTH];
            if (get_executable_path(exe_path, sizeof(exe_path))) {
#ifdef _WIN32
                STARTUPINFOA si;
                memset(&si, 0, sizeof(si));
                si.cb = sizeof(si);
                char args[MAX_PATH_LENGTH + 16];
                snprintf(args, sizeof(args), "\"%s\" --overlay", exe_path);

                if (CreateProcessA(nullptr, args, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                                   &tracker->overlay_process_info)) {
                    log_message(LOG_INFO, "[MAIN] Overlay process started with PID: %lu\n",
                                tracker->overlay_process_info.dwProcessId);
                } else {
                    log_message(LOG_ERROR, "[MAIN] Failed to create overlay process on startup. Error code: %lu\n",
                                GetLastError());
                }
#else
                pid_t pid = fork();
                if (pid == 0) {
                    // Child process
                    char *args[] = {exe_path, (char *) "--overlay", nullptr};
                    execv(exe_path, args);
                    _exit(1); // Should not be reached
                } else if (pid > 0) {
                    // Parent process
                    tracker->overlay_pid = pid;
                    log_message(LOG_INFO, "[MAIN] Overlay process started with PID: %d\n", pid);
                } else {
                    log_message(LOG_ERROR, "[MAIN] Failed to fork overlay process on startup.\n");
                }
#endif
            }
        }

        // Unified MAIN TRACKER LOOP -------------------------------------------------
        while (is_running) {
            // Check for timed shutdown in test mode
            if (is_test_mode) {
                if (SDL_GetTicks() - test_mode_start_time >= 5000) {
                    log_message(LOG_INFO, "[MAIN] Test mode 5-second timer elapsed. Shutting down.\n");
                    is_running = false;
                    continue; // Exit the loop gracefully
                }
            }
            // Force settings window open if the path was invalid on startup
            if (g_force_open_reason != FORCE_OPEN_NONE) {
                settings_opened = true;
            }

            Uint32 current_time = SDL_GetTicks();
            float deltaTime = (float) (current_time - last_frame_time) / 1000.0f;
            last_frame_time = current_time;

            // Cap deltaTime to prevent massive jumps after a long frame (e.g., during file loading).
            // A cap of 0.1f means the game will never simulate more than 1/10th of a second,
            // regardless of how long the freeze was. This turns a stutter into a smooth slowdown.
            const float MAX_DELTATIME = 0.1f; // frame_target_time * 4.0f; -> 15 fps on 60 fps
            if (deltaTime > MAX_DELTATIME) {
                deltaTime = MAX_DELTATIME;
            }

            // --- Per-Frame Logic ---

            // Check if overlay process has terminated
            if (app_settings.enable_overlay) {
                // Only check if the overlay should be active
                bool overlay_has_terminated = false;
#ifdef _WIN32
                if (tracker->overlay_process_info.hProcess != nullptr) {
                    // GetExitCodeProcess returns STILL_ACTIVE if the process is running.
                    // If it returns anything else, the process has terminated.
                    DWORD exitCode;
                    if (GetExitCodeProcess(tracker->overlay_process_info.hProcess, &exitCode) && exitCode !=
                        STILL_ACTIVE) {
                        overlay_has_terminated = true;
                        CloseHandle(tracker->overlay_process_info.hProcess); // Clean up handles
                        CloseHandle(tracker->overlay_process_info.hThread);
                        memset(&tracker->overlay_process_info, 0, sizeof(tracker->overlay_process_info));
                    }
                }
#else // For Linux/macOS
                if (tracker->overlay_pid > 0) {
                    int status;
                    // waitpid with WNOHANG is a non-blocking check.
                    // It returns the PID of the exited child, 0 if it's still running, or -1 on error.
                    if (waitpid(tracker->overlay_pid, &status, WNOHANG) == tracker->overlay_pid) {
                        overlay_has_terminated = true;
                        tracker->overlay_pid = 0; // Clear the PID
                    }
                }
#endif

                if (overlay_has_terminated) {
                    log_message(LOG_INFO, "[MAIN] Overlay process terminated. Shutting down tracker.\n");
                    is_running = false; // Signal the tracker's main loop to shut down
                }
            }

            // Increment the time since the last update every frame
            tracker->time_since_last_update += deltaTime;


            // TODO: Not passing an overlay right now
            handle_global_events(tracker, nullptr, &app_settings, &is_running, &settings_opened, &deltaTime);


            // Close immediately if app not running
            if (!is_running) break;

            // Overlay Restart Logic (triggered ONLY by Apply button)
            if (SDL_SetAtomicInt(&g_apply_button_clicked, 0) == 1) {
                log_message(LOG_INFO, "[MAIN] 'Apply Settings' clicked. Re-initializing overlay process.\n");
                // First, check if an overlay process is currently running.
                bool overlay_is_running = false;
#ifdef _WIN32
                overlay_is_running = tracker->overlay_process_info.hProcess != nullptr;
#else
                overlay_is_running = tracker->overlay_pid > 0;
#endif

                // If an overlay is running, always terminate it to ensure settings are reapplied on restart.
                if (overlay_is_running) {
                    log_message(LOG_INFO, "[MAIN] Terminating existing overlay process to apply new settings.\n");
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


                // Now, check if the NEW settings require the overlay to be enabled.
                if (app_settings.enable_overlay) {
                    log_message(LOG_INFO, "[MAIN] Starting overlay process with new settings.\n");
                    char exe_path[MAX_PATH_LENGTH];
                    if (get_executable_path(exe_path, sizeof(exe_path))) {
#ifdef _WIN32
                        STARTUPINFOA si;
                        memset(&si, 0, sizeof(si));
                        si.cb = sizeof(si);
                        char args[MAX_PATH_LENGTH + 16];
                        snprintf(args, sizeof(args), "\"%s\" --overlay", exe_path);

                        if (CreateProcessA(nullptr, args, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                                           &tracker->overlay_process_info)) {
                            log_message(LOG_INFO, "[MAIN] Overlay process started with PID: %lu\n",
                                        tracker->overlay_process_info.dwProcessId);
                        } else {
                            log_message(LOG_ERROR, "[MAIN] Failed to create overlay process. Error code: %lu\n",
                                        GetLastError());
                        }
#else
                        pid_t pid = fork();
                        if (pid == 0) {
                            // Child process
                            char *args[] = {exe_path, (char *) "--overlay", nullptr};
                            execv(exe_path, args);
                            // If execv returns, it's an error
                            log_message(LOG_ERROR, "[MAIN] Child process execv failed.\n");
                            _exit(1);
                        } else if (pid > 0) {
                            // Parent process
                            tracker->overlay_pid = pid;
                            log_message(LOG_INFO, "[MAIN] Overlay process started with PID: %d\n", pid);
                        } else {
                            log_message(LOG_ERROR, "[MAIN] Failed to fork overlay process.\n");
                        }
#endif
                    } else {
                        log_message(LOG_INFO, "[MAIN] Overlay remains disabled as per new settings.\n");
                    }
                }
            }


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
                dmon_watch(dmon_config_path, settings_watch_callback, 0, nullptr);

                // Reload settings from file to get the latest changes.
                settings_load(&app_settings);
                log_set_settings(&app_settings); // Update the logger with the new settings.

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

                        // Define a header struct to hold extra IPC data.
                        typedef struct {
                            char world_name[MAX_PATH_LENGTH];
                            float time_since_last_update;
                        } OverlayIPCHeader;

                        OverlayIPCHeader header;
                        strncpy(header.world_name, tracker->world_name, MAX_PATH_LENGTH - 1);
                        header.world_name[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null-termination
                        header.time_since_last_update = tracker->time_since_last_update;

                        // Get a pointer to the beginning of the shared buffer.
                        char *buffer_head = tracker->p_shared_data->buffer;

                        // 1. Copy the header to the start of the buffer.
                        memcpy(buffer_head, &header, sizeof(OverlayIPCHeader));
                        buffer_head += sizeof(OverlayIPCHeader); // Move the pointer past the header.

                        // 2. Serialize the main template data immediately after the header.
                        size_t template_data_size = serialize_template_data(tracker->template_data, buffer_head);

                        // 3. The total data size is the header + the serialized template data.
                        tracker->p_shared_data->data_size = sizeof(OverlayIPCHeader) + template_data_size;

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

            // IMGUI RENDERING
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            // Load the welcome window
            welcome_render_gui(&show_welcome_window, &app_settings, tracker, g_logo_texture);

            // Release notes window
            if (show_release_notes_window) {
                ImGui::Begin("Update Successful!", &show_release_notes_window, ImGuiWindowFlags_AlwaysAutoResize);

                // Render the logo texture on "Updated Successful!" window if it exists
                if (g_logo_texture) {
                    float w, h;
                    SDL_GetTextureSize(g_logo_texture, &w, &h);

                    // Calculate a new size while maintaining the aspect ratio
                    const float target_width = ADVANCELY_LOGO_SIZE; // This adjusts the image size
                    float aspect_ratio = h / w;
                    ImVec2 new_size = ImVec2(target_width, target_width * aspect_ratio);

                    // Use the new size in the Image function
                    ImGui::Image((ImTextureID) g_logo_texture, new_size);
                    ImGui::Spacing();
                }

                ImGui::Text("Advancely has been updated to the latest version!");
                ImGui::Separator();
                ImGui::Text("Click the button below to see what's new on GitHub.");
                ImGui::Spacing();
                if (ImGui::Button("View Release Notes")) {
                    SDL_OpenURL(release_url_buffer);
                    show_release_notes_window = false; // Close window after clicking
                }
                ImGui::End();
            }

            // Render the tracker GUI USING ImGui
            tracker_render_gui(tracker, &app_settings);


            // Render settings window in tracker window
            // settings_opened flag is triggered by Esc key -> tracker_events() and global event handler
            settings_render_gui(&settings_opened, &app_settings, tracker->roboto_font, tracker, &g_force_open_reason,
                                &tracker->temp_creator_window_open);

            // Render the template creator window
            temp_creator_render_gui(&tracker->temp_creator_window_open, &app_settings, tracker->roboto_font, tracker);

            ImGui::Render();

            SDL_SetRenderDrawColor(tracker->renderer, (Uint8) (app_settings.tracker_bg_color.r),
                                   (Uint8) (app_settings.tracker_bg_color.g), (Uint8) (app_settings.tracker_bg_color.b),
                                   (Uint8) (app_settings.tracker_bg_color.a));
            SDL_RenderClear(tracker->renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), tracker->renderer);
            SDL_RenderPresent(tracker->renderer);


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

    // Cleanup global logo texture
    if (g_logo_texture) {
        SDL_DestroyTexture(g_logo_texture);
    }

    tracker_free(&tracker, &app_settings);
    SDL_Quit(); // This is ONCE for all windows

    // Close logger at the end
    log_close();

    // One happy path
    return exit_status;
}
