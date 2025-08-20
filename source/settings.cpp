//
// Created by Linus on 26.06.2025.
//


#include "settings.h"

#include <vector>

#include "settings_utils.h" // ImGui imported through this
#include "global_event_handler.h" // For global variables
#include "path_utils.h" // For path_exists()

// #include "init_sdl.h" // TODO: Remove this when possible

void settings_render_gui(bool *p_open, AppSettings *app_settings, ImFont *roboto_font, Tracker *t) {
    if (!*p_open) {
        return;
    }

    // This static variable tracks the open state from the previous frame
    static bool was_open_last_frame = false;

    // HOlds temporary copy of the settings for editing
    static AppSettings temp_settings;

    // If the window was just opened (i.e., it was closed last frame but is open now),
    // we copy the current live settings into our temporary editing struct.
    if (*p_open && !was_open_last_frame) {
        memcpy(&temp_settings, app_settings, sizeof(AppSettings));
    }
    was_open_last_frame = *p_open;


    // Begin an ImGui window. The 'p_open' parameter provides the 'X' button to close it.
    ImGui::Begin("Advancely Settings", p_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);

    if (roboto_font) {
        ImGui::PushFont(roboto_font);
    }

    // Path Settings
    ImGui::Text("Path Settings");


    bool path_mode_is_auto = (temp_settings.path_mode == PATH_MODE_AUTO);
    if (ImGui::Checkbox("Auto-detect saves path", &path_mode_is_auto)) {
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
        ImGui::Unindent();
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Template Settings
    ImGui::Text("Template Settings");


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
            ImGui::SetTooltip("The StatsPerWorld Mod in combination with Legacy Fabric allows legacy versions of Minecraft\n"
                              "to track stats locally per world and not globally. Check this if you're using this mod.\n"
                              "If unchecked, the tracker will use a snapshotting system to simulate per-world achievements/stats.\n"
                              "Then, achievements will even indicate if they have been completed on a previous world or on your current one.");
        }
    }

    ImGui::InputText("Category", temp_settings.category, MAX_PATH_LENGTH);
    ImGui::InputText("Optional Flag", temp_settings.optional_flag, MAX_PATH_LENGTH);

    ImGui::Separator();
    ImGui::Spacing();

    // General Settings
    ImGui::Text("General Settings");
    ImGui::Checkbox("Enable Overlay", &temp_settings.enable_overlay);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enables a separate window for streaming.\n"
                          "Use a color key filter in your streaming software on the 'Overlay BG' hex color.\n"
                          "A negative scroll speed animates from right-to-left.");
    }

    // This toggles the framerate of everything
    ImGui::DragFloat("FPS Limit", &temp_settings.fps, 1.0f, 10.0f, 540.0f, "%.0f");

    // Conditionally display overlay related settings
    if (temp_settings.enable_overlay) {
        ImGui::DragFloat("Overlay Scroll Speed", &temp_settings.overlay_scroll_speed, 0.01f, -100.00f, 100.00f, "%.2f");
    }

    ImGui::Checkbox("Always on top", &temp_settings.tracker_always_on_top);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Forces the tracker window to always display above any other window.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Remove completed goals", &temp_settings.remove_completed_goals);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Hides fully completed goals and sub-goals to tidy up the view.");
    }

    if (temp_settings.enable_overlay) {
        ImGui::SameLine();
        ImGui::Checkbox("Goal align left", &temp_settings.goal_align_left);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When enabled, multi-stage goals and custom goals will fill in from the left, otherwise from the right.");
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Visual Settings");

    // Helper arrays to convert Uint8[0-255] to float[0-1] for ImGui color pickers
    static float tracker_bg[4], overlay_bg[4], text_col[4];
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

    if (ImGui::ColorEdit4("Tracker BG", tracker_bg)) {
        temp_settings.tracker_bg_color = {(Uint8)(tracker_bg[0]*255), (Uint8)(tracker_bg[1]*255), (Uint8)(tracker_bg[2]*255), (Uint8)(tracker_bg[3]*255)};
    }

    // Conditionally display overlay background color picker
    if (temp_settings.enable_overlay) {
        if (ImGui::ColorEdit4("Overlay BG", overlay_bg)) {
            temp_settings.overlay_bg_color = {(Uint8)(overlay_bg[0]*255), (Uint8)(overlay_bg[1]*255), (Uint8)(overlay_bg[2]*255), (Uint8)(overlay_bg[3]*255)};
        }
    }

    if (ImGui::ColorEdit4("Text Color", text_col)) {
        temp_settings.text_color = {(Uint8)(text_col[0]*255), (Uint8)(text_col[1]*255), (Uint8)(text_col[2]*255), (Uint8)(text_col[3]*255)};
    }

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Debug Settings");

    ImGui::Checkbox("Print Debug To Console", &temp_settings.print_debug_status);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("This toggles printing debug information to the console (only printf, fprintf are excluded). \nAny other prints including errors are unaffected by this setting.\nRequires running the application from a console (like MSYS2 MINGW64)\nto see the output. Just navigate to the path and execute with \"./Advancely\".");
    }

    ImGui::Separator();
    ImGui::Spacing();

    // --- Hotkey Settings ---

    // This section is only displayed if the current template has custom counters.
    static const char* key_names[] = {
        "None", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
        "PrintScreen", "ScrollLock", "Pause", "Insert", "Home", "PageUp", "Delete", "End", "PageDown",
        "Right", "Left", "Down", "Up", "Numlock", "Keypad /", "Keypad *", "Keypad -", "Keypad +", "Keypad Enter",
        "Keypad 1", "Keypad 2", "Keypad 3", "Keypad 4", "Keypad 5", "Keypad 6", "Keypad 7", "Keypad 8", "Keypad 9", "Keypad 0", "Keypad ."
    };

    const int key_names_count = sizeof(key_names) / sizeof(char*);

    // Create a temporary vector of counters to display
    std::vector<TrackableItem*> custom_counters;
    if (t && t->template_data) {
        for (int i = 0; i < t->template_data->custom_goal_count; ++i) {
            TrackableItem* item = t->template_data->custom_goals[i];
            if (item && (item->goal > 0 || item->goal == -1)) {
                custom_counters.push_back(item);
            }
        }
    }

        if (!custom_counters.empty()) {
        ImGui::Text("Hotkey Settings");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Assign keys to increment/decrement custom counters\n(only work when tabbed into the tracker). Maximum of %d hotkeys are supported.", MAX_HOTKEYS);
        }

        // Ensure the temp_settings has space for all potential hotkeys
        temp_settings.hotkey_count = custom_counters.size();

        for (int i = 0; i < (int)custom_counters.size(); ++i) {
            TrackableItem* counter = custom_counters[i];
            HotkeyBinding* binding = &temp_settings.hotkeys[i];

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
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Apply the changes
    if (ImGui::Button("Apply settings")) {

        // Validate the manual path before applying settings
        if (temp_settings.path_mode == PATH_MODE_MANUAL) {
            // Check if the path is empty or does not exist
            if (strlen(temp_settings.manual_saves_path) == 0 || !path_exists(temp_settings.manual_saves_path)) {
                // If invalid, revert to auto mode to prevent dmon errors
                temp_settings.path_mode = PATH_MODE_AUTO;
            }
        }

        // When the button is clicked, copy temp settings to the real settings, save, and trigger a reload
        memcpy(app_settings, &temp_settings, sizeof(AppSettings));
        SDL_SetWindowAlwaysOnTop(t->window, app_settings->tracker_always_on_top);
        settings_save(app_settings, nullptr);
        SDL_SetAtomicInt(&g_settings_changed, 1); // Trigger a reload
    }

    // Place the next button on the same line
    ImGui::SameLine();

    if (ImGui::Button("Reset to Defaults")) {
        // Reset the temporary settings struct to the default values
        settings_set_defaults(&temp_settings);
    }
    // TODO: Add default values always to this tooltip here
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[1024];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
            "Resets all settings in this window to their default values.\n"
            "This does not modify your template files.\n\n"
            "Defaults:\n"
            "  - Path Mode: Auto-detect\n"
            "  - Version: %s\n"
            "  - Category: %s\n"
            "  - Optional Flag: %s\n"
            "  - Overlay: %s\n"
            "  - StatsPerWorld Mod (1.0-1.6.4): %s\n"
            "  - Always on top: %s\n"
            "  - Remove completed: %s\n"
            "  - Goal align left: %s\n"
            "  - Print Debug To Console: %s\n"
            "  - FPS Limit: %d\n"
            "  - Overlay Scroll Speed: %.2f",
            DEFAULT_VERSION,
            DEFAULT_CATEGORY,
            DEFAULT_OPTIONAL_FLAG,
            DEFAULT_ENABLE_OVERLAY ? "Enabled" : "Disabled",
            DEFAULT_USING_STATS_PER_WORLD_LEGACY ? "Enabled" : "Disabled",
            DEFAULT_TRACKER_ALWAYS_ON_TOP ? "Enabled" : "Disabled",
            DEFAULT_REMOVE_COMPLETED_GOALS ? "Enabled" : "Disabled",
            DEFAULT_GOAL_ALIGN_LEFT ? "Enabled" : "Disabled",
            DEFAULT_PRINT_DEBUG_STATUS ? "Enabled" : "Disabled",
            DEFAULT_FPS,
            DEFAULT_OVERLAY_SCROLL_SPEED
        );
        ImGui::SetTooltip(tooltip_buffer);
    }

    if (roboto_font) {
        ImGui::PopFont();
    }

    ImGui::End();
}
