//
// Created by Linus on 26.06.2025.
//


#include "settings.h"
#include "logger.h"

#include <vector>

#include "settings_utils.h" // ImGui imported through this
#include "global_event_handler.h" // For global variables
#include "path_utils.h" // For path_exists()

void settings_render_gui(bool *p_open, AppSettings *app_settings, ImFont *roboto_font, Tracker *t,
                         bool *force_open_flag) {
    if (!*p_open) {
        return;
    }

    // This static variable tracks the open state from the previous frame
    static bool was_open_last_frame = false;

    // Flag to track invalid manual path (especially important when auto path is invalid as well, to prevent dmon crashes)
    static bool show_invalid_manual_path_error = false;

    // Holds temporary copy of the settings for editing
    static AppSettings temp_settings;

    // If the window was just opened (i.e., it was closed last frame but is open now),
    // we copy the current live settings into our temporary editing struct.
    if (*p_open && !was_open_last_frame) {
        memcpy(&temp_settings, app_settings, sizeof(AppSettings));
    }
    was_open_last_frame = *p_open;


    // Begin an ImGui window. The 'p_open' parameter provides the 'X' button to close it.
    ImGui::Begin("Advancely Settings", p_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);

    // If settings were forced open, display a prominent warning message
    if (force_open_flag && *force_open_flag) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f)); // Yellow text
        ImGui::TextWrapped("IMPORTANT: Could not find Minecraft saves folder automatically.");
        ImGui::TextWrapped(
            "Please enter the correct path to your '.minecraft/saves' folder below and click 'Apply Settings'.");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }

    if (roboto_font) {
        ImGui::PushFont(roboto_font);
    }

    // Path Settings
    ImGui::Text("Path Settings");


    bool path_mode_is_auto = (temp_settings.path_mode == PATH_MODE_AUTO);
    if (ImGui::Checkbox("Auto-Detect Saves Path", &path_mode_is_auto)) {
        temp_settings.path_mode = path_mode_is_auto ? PATH_MODE_AUTO : PATH_MODE_MANUAL;
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically finds the default Minecraft saves path for your OS:\n"
            "Windows: %%APPDATA%%\\.minecraft\\saves\n"
            "Linux: ~/.minecraft/saves\n"
            "macOS: ~/Library/Application Support/minecraft/saves");
    }

    if (temp_settings.path_mode == PATH_MODE_MANUAL) {
        ImGui::Indent();
        ImGui::InputText("Manual Saves Path", temp_settings.manual_saves_path, MAX_PATH_LENGTH);

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enter the path to your '.minecraft/saves' folder.\n"
                              "You can just paste it in.\n"
                              "Doesn't matter if the path uses forward- or backward slashes.");
        }

        if (show_invalid_manual_path_error) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f)); // Red text
            ImGui::TextWrapped(
                "The specified path is invalid or does not exist. Please provide a valid path to your '.minecraft/saves' folder.");
            ImGui::PopStyleColor();
        }

        ImGui::Unindent();
    }

    // Open Instances Folder Button
    // Only show the button if the current saves path is valid and not empty.
    if (t->saves_path[0] != '\0' && path_exists(t->saves_path)) {
        if (ImGui::Button("Open Instances Folder")) {
            char instances_path[MAX_PATH_LENGTH];
            if (get_parent_directory(t->saves_path, instances_path, sizeof(instances_path), 3)) {
                char command[MAX_PATH_LENGTH + 32];
#ifdef _WIN32
                // Convert slashes to backslashes for the windows expolorer command
                path_to_windows_native(instances_path);
                snprintf(command, sizeof(command), "explorer \"%s\"", instances_path);
#elif __APPLE__
                snprintf(command, sizeof(command), "open \"%s\"", instances_path);
#else
                snprintf(command, sizeof(command), "xdg-open \"%s\"", instances_path);
#endif
                // DEBUG: Log the exact command being passed to the system.
                log_message(LOG_INFO, "[DEBUG settings] Executing system command: %s\n", command);
                system(command);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("IMPORTANT: If you just changed your saves path you'll need to hit 'Apply Settings' first.\n"
                "Attempts to open the parent 'instances' folder (goes up 3 directories from your saves path).\n"
                              "Useful for quickly switching between instances in custom launchers.");
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Template Settings
    ImGui::Text("Template Settings");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Select the Version, Category, and an Optional Flag to construct the path to your template files.\n"
            "The final path will look like: resources/templates/Version/Category/Version_CategoryOptionalFlag.json\n"
            "The corresponding language file must end in _lang.json and holds all the formatted text that displays in the UI.\n\n"
            "You can create new categories and templates by creating new folders and files that follow this structure.\n"
            "Feel free to modify the .json files to customize your goals.\n"
            "(Template creator built into this tracker coming soon!)"
        );
    }

    int current_version_idx = -1;
    for (int i = 0; i < VERSION_STRINGS_COUNT; i++) {
        if (strcmp(VERSION_STRINGS[i], temp_settings.version_str) == 0) {
            current_version_idx = i;
            break;
        }
    }

    if (ImGui::Combo("Version", &current_version_idx, VERSION_STRINGS, VERSION_STRINGS_COUNT)) {
        if (current_version_idx >= 0) {
            strncpy(temp_settings.version_str, VERSION_STRINGS[current_version_idx],
                    sizeof(temp_settings.version_str) - 1);
        }
    }

    // Only show the StatsPerWorld checkbox for legacy versions
    MC_Version selected_version = settings_get_version_from_string(temp_settings.version_str);
    if (selected_version <= MC_VERSION_1_6_4) {
        ImGui::Checkbox("Using StatsPerWorld Mod", &temp_settings.using_stats_per_world_legacy);

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "The StatsPerWorld Mod in combination with Legacy Fabric allows legacy versions of Minecraft\n"
                "to track stats locally per world and not globally. Check this if you're using this mod.\n"
                "If unchecked, the tracker will use a snapshotting system to simulate per-world achievements/stats.\n"
                "Then, achievements will even indicate if they have been completed on a previous world or on your current one.");
        }
    }

    ImGui::InputText("Category", temp_settings.category, MAX_PATH_LENGTH);
    ImGui::InputText("Optional Flag", temp_settings.optional_flag, MAX_PATH_LENGTH);

    if (ImGui::Button("Open Template Folder")) {
#ifdef _WIN32
        system("explorer resources\\templates");
#elif __APPLE__
        system("open resources/templates");
#else
        system("xdg-open resources/templates");
#endif
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Opens the 'resources/templates' folder in your file explorer.");
    }

    ImGui::Separator();
    ImGui::Spacing();

    // General Settings
    ImGui::Text("General Settings");
    ImGui::Checkbox("Enable Overlay", &temp_settings.enable_overlay);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enables a separate window to show your progress in your stream.\n"
            "More settings related to the overlay window become available once enabled.\n"
            "Use a color key filter in your streaming software on the 'Overlay BG' hex color.\n"
            "A negative scroll speed animates from right-to-left.\n"
            "To adjust the horizontal spacing between items per row,\nyou can shorten the display names in the language (*_lang.json) file.\n"
            "To turn off the overlay, disable this checkbox and hit 'Apply Settings'!");
    }

    // This toggles the framerate of everything
    ImGui::DragFloat("FPS Limit", &temp_settings.fps, 1.0f, 10.0f, 540.0f, "%.0f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Limits the frames per second of the tracker and overlay window. Default is 60 FPS.\n"
            "Higher values may result in higher GPU usage.");
    }

    // Conditionally display overlay related settings
    if (temp_settings.enable_overlay) {
        ImGui::DragFloat("Overlay Scroll Speed", &temp_settings.overlay_scroll_speed, 0.001f, -25.00f, 25.00f, "%.3f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A negative scroll speed animates from right-to-left.\n"
                "A scroll speed of 0.0 is static.\n"
                "Default of 1.0 scrolls 1440 pixels (default width) in 24 seconds.");
        }

        ImGui::DragFloat("Sub-Stat Cycle Interval (s)", &temp_settings.overlay_stat_cycle_speed, 0.1f, 0.1f, 60.0f, "%.3f s");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The time in seconds before cycling to the next sub-stat on a multi-stat goal on the overlay.\n");
        }

        if (ImGui::Checkbox("Speed Up Animation", &temp_settings.overlay_animation_speedup)) {
        }
        if (ImGui::IsItemHovered()) {
            char speed_up_tooltip_buffer[1024];
            snprintf(speed_up_tooltip_buffer, sizeof(speed_up_tooltip_buffer),
                     "Toggles speeding up the overlay animation by a factor of %.1f. Don't forget to hit apply!\nOn top of that you can also hold SPACE (also %.1f) when tabbed into the overlay window.",
                     OVERLAY_SPEEDUP_FACTOR, OVERLAY_SPEEDUP_FACTOR);

            ImGui::SetTooltip(speed_up_tooltip_buffer);
        }

        ImGui::SameLine();

        ImGui::Checkbox("Hide Completed Row 3 Goals", &temp_settings.overlay_row3_remove_completed);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "If checked, all Goals (Stats, Custom Goals and Multi-Stage Goals) will disappear from Row 3 of the overlay.\nThis is independent of the main 'Remove Completed Goals' setting.");
        }
    }

    ImGui::Checkbox("Always On Top", &temp_settings.tracker_always_on_top);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Forces the tracker window to always display above any other window.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Remove Completed Goals", &temp_settings.remove_completed_goals);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Hides fully completed goals and sub-goals from the tracker window to tidy up the view.");
    }

    ImGui::Separator();
    ImGui::Spacing();

    if (temp_settings.enable_overlay) {
        ImGui::Text("Overlay Title Alignment:");
        ImGui::SameLine();
        ImGui::RadioButton("Left", (int *) &temp_settings.overlay_progress_text_align,
                           OVERLAY_PROGRESS_TEXT_ALIGN_LEFT);
        ImGui::SameLine();
        ImGui::RadioButton("Center", (int *) &temp_settings.overlay_progress_text_align,
                           OVERLAY_PROGRESS_TEXT_ALIGN_CENTER);
        ImGui::SameLine();
        ImGui::RadioButton("Right", (int *) &temp_settings.overlay_progress_text_align,
                           OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT);

        ImGui::Text("Overlay Text Sections:");
        ImGui::SameLine();
        ImGui::Checkbox("World", &temp_settings.overlay_show_world);
        ImGui::SameLine();
        ImGui::Checkbox("Run Details", &temp_settings.overlay_show_run_details);
        ImGui::SameLine();
        ImGui::Checkbox("Progress", &temp_settings.overlay_show_progress);
        ImGui::SameLine();
        ImGui::Checkbox("IGT", &temp_settings.overlay_show_igt);
        ImGui::SameLine();
        ImGui::Checkbox("Update Timer", &temp_settings.overlay_show_update_timer);

        ImGui::Separator();
        ImGui::Spacing();
    }

    ImGui::Text("Visual Settings");

    // Helper arrays to convert Uint8[0-255] to float[0-1] for ImGui color pickers
    static float tracker_bg[4], overlay_bg[4], text_col[4], overlay_text_col[4];
    tracker_bg[0] = (float) temp_settings.tracker_bg_color.r / 255.0f;
    tracker_bg[1] = (float) temp_settings.tracker_bg_color.g / 255.0f;
    tracker_bg[2] = (float) temp_settings.tracker_bg_color.b / 255.0f;
    tracker_bg[3] = (float) temp_settings.tracker_bg_color.a / 255.0f;
    overlay_bg[0] = (float) temp_settings.overlay_bg_color.r / 255.0f;
    overlay_bg[1] = (float) temp_settings.overlay_bg_color.g / 255.0f;
    overlay_bg[2] = (float) temp_settings.overlay_bg_color.b / 255.0f;
    overlay_bg[3] = (float) temp_settings.overlay_bg_color.a / 255.0f;
    text_col[0] = (float) temp_settings.text_color.r / 255.0f;
    text_col[1] = (float) temp_settings.text_color.g / 255.0f;
    text_col[2] = (float) temp_settings.text_color.b / 255.0f;
    text_col[3] = (float) temp_settings.text_color.a / 255.0f;
    overlay_text_col[0] = (float) temp_settings.overlay_text_color.r / 255.0f;
    overlay_text_col[1] = (float) temp_settings.overlay_text_color.g / 255.0f;
    overlay_text_col[2] = (float) temp_settings.overlay_text_color.b / 255.0f;
    overlay_text_col[3] = (float) temp_settings.overlay_text_color.a / 255.0f;

    if (ImGui::ColorEdit4("Tracker BG", tracker_bg)) {
        temp_settings.tracker_bg_color = {
            (Uint8) (tracker_bg[0] * 255), (Uint8) (tracker_bg[1] * 255), (Uint8) (tracker_bg[2] * 255),
            (Uint8) (tracker_bg[3] * 255)
        };
    }

    // Conditionally display overlay background color picker
    if (temp_settings.enable_overlay) {
        if (ImGui::ColorEdit4("Overlay BG", overlay_bg)) {
            temp_settings.overlay_bg_color = {
                (Uint8) (overlay_bg[0] * 255), (Uint8) (overlay_bg[1] * 255), (Uint8) (overlay_bg[2] * 255),
                (Uint8) (overlay_bg[3] * 255)
            };
        }
    }

    if (ImGui::ColorEdit4("Text Color", text_col)) {
        temp_settings.text_color = {
            (Uint8) (text_col[0] * 255), (Uint8) (text_col[1] * 255), (Uint8) (text_col[2] * 255),
            (Uint8) (text_col[3] * 255)
        };
    }

    if (temp_settings.enable_overlay) {
        if (ImGui::ColorEdit4("Overlay Text Color", overlay_text_col)) {
            temp_settings.overlay_text_color = {
                (Uint8) (overlay_text_col[0] * 255), (Uint8) (overlay_text_col[1] * 255),
                (Uint8) (overlay_text_col[2] * 255), (Uint8) (overlay_text_col[3] * 255)
            };
        }

        // Slider for overlay width
        static int overlay_width;
        overlay_width = temp_settings.overlay_window.w;
        if (ImGui::DragInt("Overlay Width", &overlay_width, 10.0f, 200, 7680)) {
            if (overlay_width > 0) {
                // Basic validation
                temp_settings.overlay_window.w = overlay_width;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Adjusts the width of the overlay window.\nDefault: %dpx", OVERLAY_DEFAULT_WIDTH);
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Debug Settings");

    ImGui::Checkbox("Print Debug To Console", &temp_settings.print_debug_status);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "This toggles printing a detailed progress report to the console after every file update.\n\n"
            "IMPORTANT: This can spam the console with a large amount of text if your template files contain many entries.\n\n"
            "This setting only affects the detailed report. General status messages and errors\n"
            "Progress on advancements is only printed if the game sends an update.\n"
            "are always printed to the console and saved to advancely_log.txt.\n"
            "The log is flushed after every message, making it ideal for diagnosing crashes.\n"
            "Everything the application prints to a console (like MSYS2 MINGW64) can also be found in advancely_log.txt.");
    }

    ImGui::Separator();
    ImGui::Spacing();

    // --- Hotkey Settings ---

    // This section is only displayed if the current template has custom counters.
    static const char *key_names[] = {
        "None", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U",
        "V", "W", "X", "Y", "Z",
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
        "F11", "F12",
        "PrintScreen", "ScrollLock", "Pause", "Insert", "Home", "PageUp", "Delete", "End", "PageDown",
        "Right", "Left", "Down", "Up", "Numlock", "Keypad /", "Keypad *", "Keypad -", "Keypad +", "Keypad Enter",
        "Keypad 1", "Keypad 2", "Keypad 3", "Keypad 4", "Keypad 5", "Keypad 6", "Keypad 7", "Keypad 8", "Keypad 9",
        "Keypad 0", "Keypad ."
    };

    const int key_names_count = sizeof(key_names) / sizeof(char *);

    // Create a temporary vector of counters to display
    std::vector<TrackableItem *> custom_counters;
    if (t && t->template_data) {
        for (int i = 0; i < t->template_data->custom_goal_count; ++i) {
            TrackableItem *item = t->template_data->custom_goals[i];
            if (item && (item->goal > 0 || item->goal == -1)) {
                custom_counters.push_back(item);
            }
        }
    }

    if (!custom_counters.empty()) {
        ImGui::Text("Hotkey Settings");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Assign keys to increment/decrement custom counters\n(only work when tabbed into the tracker). Maximum of %d hotkeys are supported.",
                MAX_HOTKEYS);
        }

        // Ensure the temp_settings has space for all potential hotkeys
        temp_settings.hotkey_count = custom_counters.size();

        for (int i = 0; i < (int) custom_counters.size(); ++i) {
            TrackableItem *counter = custom_counters[i];
            HotkeyBinding *binding = &temp_settings.hotkeys[i];

            // Set the target goal for this binding
            strncpy(binding->target_goal, counter->root_name, sizeof(binding->target_goal) - 1);

            ImGui::Text("%s", counter->display_name);
            ImGui::SameLine();

            // Find current index for the increment key
            int current_inc_key_idx = 0;
            for (int k = 0; k < key_names_count; ++k) {
                if (strcmp(binding->increment_key, key_names[k]) == 0) {
                    current_inc_key_idx = k;
                    break;
                }
            }

            // Find current index for the decrement key
            int current_dec_key_idx = 0;
            for (int k = 0; k < key_names_count; ++k) {
                if (strcmp(binding->decrement_key, key_names[k]) == 0) {
                    current_dec_key_idx = k;
                    break;
                }
            }

            // Render dropdowns
            char inc_label[64];
            snprintf(inc_label, sizeof(inc_label), "##inc_%s", counter->root_name);
            if (ImGui::Combo(inc_label, &current_inc_key_idx, key_names, key_names_count)) {
                strncpy(binding->increment_key, key_names[current_inc_key_idx], sizeof(binding->increment_key) - 1);
            }

            ImGui::SameLine();
            char dec_label[64];
            snprintf(dec_label, sizeof(dec_label), "##dec_%s", counter->root_name);
            if (ImGui::Combo(dec_label, &current_dec_key_idx, key_names, key_names_count)) {
                strncpy(binding->decrement_key, key_names[current_dec_key_idx], sizeof(binding->decrement_key) - 1);
            }
        }

        ImGui::Separator();
        ImGui::Spacing();
    }

    // Apply the changes
    if (ImGui::Button("Apply Settings")) {

        // Assume the error is cleared unless we find one
        show_invalid_manual_path_error = false;

        // Step 1: Validate the manual Path if it's being used
        if (temp_settings.path_mode == PATH_MODE_MANUAL) {
            // Check if the path is empty or does not exist
            if (strlen(temp_settings.manual_saves_path) == 0 || !path_exists(temp_settings.manual_saves_path)) {
                // CASE 1: Manual mode is selected, but the path is invalid.
                show_invalid_manual_path_error = true;
                // Do not apply settings; force user to correct the path
            } else {
                // CASE 2: Manual mode is selected, and the path is valid. Apply settings.
                show_invalid_manual_path_error = false; // Hide error message

                // If the path is now valid, we can clear the force_open_flag
                if (force_open_flag) {
                    *force_open_flag = false;
                }

                // Copy temp settings to the real settings, save, and trigger a reload
                memcpy(app_settings, &temp_settings, sizeof(AppSettings));
                SDL_SetWindowAlwaysOnTop(t->window, app_settings->tracker_always_on_top);
                settings_save(app_settings, nullptr);
                SDL_SetAtomicInt(&g_settings_changed, 1); // Trigger a reload
            }
        } else {
            // temp_settings.path_mode == PATH_MODE_AUTO
            char auto_path_buffer[MAX_PATH_LENGTH];
            get_saves_path(auto_path_buffer, MAX_PATH_LENGTH, PATH_MODE_AUTO, nullptr);

            if (!path_exists(auto_path_buffer)) {
                // CASE 3: Auto mode is selected, but the auto-detected path is invalid.
                temp_settings.path_mode = PATH_MODE_MANUAL; // Revert the choice in the UI
                if (force_open_flag) {
                    *force_open_flag = true; // Re-trigger the warning message
                }
            } else {
                // CASE 4: Auto mode is selected, and the path is valid. Apply settings.
                if (force_open_flag) {
                    *force_open_flag = false;
                }
                memcpy(app_settings, &temp_settings, sizeof(AppSettings));
                SDL_SetWindowAlwaysOnTop(t->window, app_settings->tracker_always_on_top);
                settings_save(app_settings, nullptr);
                SDL_SetAtomicInt(&g_settings_changed, 1);
            }
        }
    }

    // Hover text for the apply button
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Apply any changes made in this window. It will fail to apply if any warnings are shown.");
    }

    // Place the next button on the same line
    ImGui::SameLine();

    if (ImGui::Button("Reset To Defaults")) {
        // Preserve current window geometry before resetting other settings
        WindowRect current_tracker_window = temp_settings.tracker_window;
        WindowRect current_overlay_window = temp_settings.overlay_window;

        // Reset the temporary settings struct to the default values
        settings_set_defaults(&temp_settings);

        // Restore the preserved window geometry
        temp_settings.tracker_window = current_tracker_window;
        temp_settings.overlay_window = current_overlay_window;
    }
    // TODO: Add default values always to this tooltip here
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[1024];

        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Resets all settings (besides window size/position & hotkeys) in this window to their default values.\n"
                 "This does not modify your template files.\n\n"
                 "Defaults:\n"
                 "  - Path Mode: Auto-Detect\n"
                 "  - Version: %s\n"
                 "  - StatsPerWorld Mod (1.0-1.6.4): %s\n"
                 "  - Category: %s\n"
                 "  - Optional Flag: %s\n"
                 "  - Overlay: %s\n"
                 "  - FPS Limit: %d\n"
                 "  - Overlay Scroll Speed: %.2f\n"
                 "  - Sub-Stat Cycle Speed: %.1f s\n"
                 "  - Speed Up Animation: %s\n"
                 "  - Hide Completed Row 3 Goals: %s\n"
                 "  - Always On Top: %s\n"
                 "  - Remove Completed: %s\n"
                 "  - Overlay Width: %s\n"
                 "  - Use Settings Font: %s\n"
                 "  - Print Debug To Console: %s",

                 DEFAULT_VERSION,
                 DEFAULT_USING_STATS_PER_WORLD_LEGACY ? "Enabled" : "Disabled",
                 DEFAULT_CATEGORY,
                 DEFAULT_OPTIONAL_FLAG,
                 DEFAULT_ENABLE_OVERLAY ? "Enabled" : "Disabled",
                 DEFAULT_FPS,
                 DEFAULT_OVERLAY_SCROLL_SPEED,
                 DEFAULT_OVERLAY_STAT_CYCLE_SPEED,
                 DEFAULT_OVERLAY_SPEED_UP ? "Enabled" : "Disabled",
                 DEFAULT_OVERLAY_ROW3_REMOVE_COMPLETED ? "Enabled" : "Disabled",
                 DEFAULT_TRACKER_ALWAYS_ON_TOP ? "Enabled" : "Disabled",
                 DEFAULT_REMOVE_COMPLETED_GOALS ? "Enabled" : "Disabled",
                 "1440px",
                 DEFAULT_NOTES_USE_ROBOTO ? "Enabled" : "Disabled",
                 DEFAULT_PRINT_DEBUG_STATUS ? "Enabled" : "Disabled"
        );
        ImGui::SetTooltip(tooltip_buffer);
    }

    if (roboto_font) {
        ImGui::PopFont();
    }

    ImGui::End();
}
