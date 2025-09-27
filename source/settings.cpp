//
// Created by Linus on 26.06.2025.
//


#include "settings.h"

#include <algorithm>
#include <string>
#include <cstring> // For memcmp on simple sub-structs

#include "logger.h"

#include <vector>

#include "dialog_utils.h"
#include "settings_utils.h" // ImGui imported through this
#include "global_event_handler.h" // For global variables
#include "path_utils.h" // For path_exists()
#include "template_scanner.h"
#include "temp_creator.h"
#include "tinyfiledialogs.h"
#include "update_checker.h"

// Helper function to robustly compare two AppSettings structs
// Changing window geometry of overlay and tracker window DO NOT cause the "Unsaved Changes" text to appear.
static bool are_settings_different(const AppSettings *a, const AppSettings *b) {
    if (a->path_mode != b->path_mode ||
        strcmp(a->manual_saves_path, b->manual_saves_path) != 0 ||
        strcmp(a->version_str, b->version_str) != 0 ||
        strcmp(a->category, b->category) != 0 ||
        strcmp(a->optional_flag, b->optional_flag) != 0 ||
        strcmp(a->lang_flag, b->lang_flag) != 0 ||
        a->enable_overlay != b->enable_overlay ||
        a->using_stats_per_world_legacy != b->using_stats_per_world_legacy ||
        a->fps != b->fps ||
        a->overlay_fps != b->overlay_fps ||
        a->tracker_always_on_top != b->tracker_always_on_top ||
        a->goal_hiding_mode != b->goal_hiding_mode ||
        a->print_debug_status != b->print_debug_status ||
        a->overlay_scroll_speed != b->overlay_scroll_speed ||
        a->overlay_progress_text_align != b->overlay_progress_text_align ||
        a->overlay_animation_speedup != b->overlay_animation_speedup ||
        a->overlay_row3_remove_completed != b->overlay_row3_remove_completed ||
        a->overlay_stat_cycle_speed != b->overlay_stat_cycle_speed ||
        a->notes_use_roboto_font != b->notes_use_roboto_font ||
        a->check_for_updates != b->check_for_updates ||
        a->show_welcome_on_startup != b->show_welcome_on_startup ||
        a->overlay_show_world != b->overlay_show_world ||
        a->overlay_show_run_details != b->overlay_show_run_details ||
        a->overlay_show_progress != b->overlay_show_progress ||
        a->overlay_show_igt != b->overlay_show_igt ||
        a->overlay_show_update_timer != b->overlay_show_update_timer ||
        // Changes to window geometry of overlay and tracker window DO NOT cause the "Unsaved Changes" text to appear.
        // memcmp(&a->tracker_window, &b->tracker_window, sizeof(WindowRect)) != 0 ||
        // memcmp(&a->overlay_window, &b->overlay_window, sizeof(WindowRect)) != 0 ||

        strcmp(a->tracker_font_name, b->tracker_font_name) != 0 ||
        a->tracker_font_size != b->tracker_font_size ||
        strcmp(a->ui_font_name, a->ui_font_name) != 0 ||
        a->ui_font_size != a->ui_font_size ||
        strcmp(a->overlay_font_name, b->overlay_font_name) != 0 ||

        memcmp(&a->tracker_bg_color, &b->tracker_bg_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->overlay_bg_color, &b->overlay_bg_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->text_color, &b->text_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->overlay_text_color, &b->overlay_text_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(a->section_order, b->section_order, sizeof(a->section_order)) != 0) {
        return true;
    }

    // Compare hotkeys separately
    if (a->hotkey_count != b->hotkey_count) return true;
    for (int i = 0; i < a->hotkey_count; ++i) {
        if (strcmp(a->hotkeys[i].target_goal, b->hotkeys[i].target_goal) != 0 ||
            strcmp(a->hotkeys[i].increment_key, b->hotkeys[i].increment_key) != 0 ||
            strcmp(a->hotkeys[i].decrement_key, b->hotkeys[i].decrement_key) != 0) {
            return true;
        }
    }

    return false;
}

void settings_render_gui(bool *p_open, AppSettings *app_settings, ImFont *roboto_font, Tracker *t,
                         bool *force_open_flag, bool *p_temp_creator_open) {
    // This static variable tracks the open state from the previous frame
    static bool was_open_last_frame = false;

    // Flag to track invalid manual path (especially important when auto path is invalid as well, to prevent dmon crashes)
    static bool show_invalid_manual_path_error = false;

    // Flag to show an error if the selected template doesn't exist
    static bool show_template_not_found_error = false;

    // Flag to show a confirmation message when settings are applied
    static bool show_applied_message = false;

    // Flag to show a confirmation message when settings are reset
    static bool show_defaults_applied_message = false;

    // Flag to show a warning about hotkeys needing a settings window restart
    static bool show_hotkey_warning_message = false;

    // Holds temporary copy of the settings for editing
    static AppSettings temp_settings;

    // Add a snapshot to compare agains
    static AppSettings saved_settings;

    static DiscoveredTemplate *discovered_templates = nullptr;
    static int discovered_template_count = 0;
    static char last_scanned_version[64] = "";

    static std::vector<std::string> unique_category_values;
    static std::vector<const char *> category_display_names;
    static std::vector<std::string> flag_values;
    static std::vector<const char *> flag_display_names;

    // State for version dropdown with counts
    static std::vector<std::string> version_display_names;
    static std::vector<const char *> version_display_c_strs;
    static bool version_counts_generated = false;

    if (!version_counts_generated) {
        version_display_names.reserve(VERSION_STRINGS_COUNT);
        for (int i = 0; i < VERSION_STRINGS_COUNT; ++i) {
            DiscoveredTemplate *templates = nullptr;
            int count = 0;
            scan_for_templates(VERSION_STRINGS[i], &templates, &count);

            char buffer[128];
            if (count > 0) {
                snprintf(buffer, sizeof(buffer), "%s (%d)", VERSION_STRINGS[i], count);
            } else {
                // strncpy is safer if VERSION_STRINGS[i] could be too long
                strncpy(buffer, VERSION_STRINGS[i], sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';
            }
            version_display_names.push_back(buffer);

            free_discovered_templates(&templates, &count);
        }

        version_display_c_strs.reserve(VERSION_STRINGS_COUNT);
        for (const auto &name: version_display_names) {
            version_display_c_strs.push_back(name.c_str());
        }

        version_counts_generated = true;
    }

    // Force a rescan if the template files have been changed by the creator
    if (SDL_SetAtomicInt(&g_templates_changed, 0) == 1) {
        last_scanned_version[0] = '\0';
    }

    // --- State management for window open/close ---
    // Detect the transition from closed to opened state.
    const bool just_opened = *p_open && !was_open_last_frame;

    was_open_last_frame = *p_open;

    if (!*p_open) return;

    // If the window was just opened (i.e., it was closed last frame but is open now),
    // we copy the current live settings into our temporary editing struct.
    if (just_opened) {
        memcpy(&temp_settings, app_settings, sizeof(AppSettings));
        memcpy(&saved_settings, app_settings, sizeof(AppSettings));
        show_applied_message = false; // Reset message visibility
        show_defaults_applied_message = false; // Reset "Defaults Applied" message visibility
        show_hotkey_warning_message = false;
        show_template_not_found_error = false;
    }

    // Window title
    ImGui::Begin("Advancely Settings", p_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);

    // Unsaved changes
    bool has_unsaved_changes = are_settings_different(&temp_settings, &saved_settings);

    // Revert Changes -> Ctrl+Z / Cmd+Z hotkey logic
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && has_unsaved_changes && !
        ImGui::IsAnyItemActive() &&
        (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_LeftSuper)) &&
        ImGui::IsKeyPressed(ImGuiKey_Z)) {
        memcpy(&temp_settings, &saved_settings, sizeof(AppSettings));
    }

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
        char default_saves_path_tooltip_buffer[1024];
        snprintf(default_saves_path_tooltip_buffer, sizeof(default_saves_path_tooltip_buffer),
                 "Automatically finds the default Minecraft saves path for your OS:\n"
                 "Windows: %%APPDATA%%\\.minecraft\\saves\n"
                 "Linux: ~/.minecraft/saves\n"
                 "macOS: ~/Library/Application Support/minecraft/saves\n\n"
                 "Uncheck this to manually set your saves path.");
        ImGui::SetTooltip("%s", default_saves_path_tooltip_buffer);
    }

    if (temp_settings.path_mode == PATH_MODE_MANUAL) {
        ImGui::Indent();
        ImGui::InputText("Manual Saves Path", temp_settings.manual_saves_path, MAX_PATH_LENGTH);

        if (ImGui::IsItemHovered()) {
            char manual_saves_path_tooltip_buffer[1024];
            snprintf(manual_saves_path_tooltip_buffer, sizeof(manual_saves_path_tooltip_buffer),
                     "Enter the path to your '.minecraft/saves' folder.\n"
                     "You can just paste it in.\n"
                     "Doesn't matter if the path uses forward- or backward slashes.");
            ImGui::SetTooltip("%s", manual_saves_path_tooltip_buffer);
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
            char open_instance_folder_tooltip_buffer[1024];
            snprintf(open_instance_folder_tooltip_buffer, sizeof(open_instance_folder_tooltip_buffer),
                     "IMPORTANT: If you just changed your saves path you'll need to hit 'Apply Settings' first.\n"
                     "Attempts to open the parent 'instances' folder (goes up 3 directories from your saves path).\n"
                     "Useful for quickly switching between instances in custom launchers.");
            ImGui::SetTooltip(
                "%s", open_instance_folder_tooltip_buffer);
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Template Settings
    ImGui::Text("Template Settings");
    if (ImGui::IsItemHovered()) {
        char template_settings_tooltip_buffer[1024];
        snprintf(template_settings_tooltip_buffer, sizeof(template_settings_tooltip_buffer),
                 "Select the Version, Category, Optional Flag, and Language to use for the tracker.\n\n"
                 "These settings construct the path to your template files, which looks like:\n"
                 "resources/templates/Version/Category/Version_CategoryOptionalFlag.json\n\n"
                 "Each template has one or more language files (e.g., ..._lang.json for default, ..._lang_eng.json for English)\n"
                 "that store all the display names shown in the UI.\n\n"
                 "Use the 'Edit Templates' button to build new templates, edit existing ones, and manage their language files.");
        ImGui::SetTooltip(
            "%s", template_settings_tooltip_buffer);
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f)); // Use a link-like color
    ImGui::Text("(Learn more)");
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered()) {
        char open_official_templates_tooltip_buffer[1024];
        snprintf(open_official_templates_tooltip_buffer, sizeof(open_official_templates_tooltip_buffer),
                 "Opens a table of officially added templates in your browser.");
        ImGui::SetTooltip("%s", open_official_templates_tooltip_buffer);
    }

    if (ImGui::IsItemClicked()) {
        const char *url = "https://github.com/LNXSeus/Advancely#Officially-Added-Templates";
        char command[1024];
#ifdef _WIN32
        snprintf(command, sizeof(command), "start %s", url);
#elif __APPLE__
        snprintf(command, sizeof(command), "open %s", url);
#else
        snprintf(command, sizeof(command), "xdg-open %s", url);
#endif
        system(command);
    }

    // Version dropdown
    int current_version_idx = -1;
    for (int i = 0; i < VERSION_STRINGS_COUNT; i++) {
        if (strcmp(VERSION_STRINGS[i], temp_settings.version_str) == 0) {
            current_version_idx = i;
            break;
        }
    }
    if (ImGui::Combo("Version", &current_version_idx, version_display_c_strs.data(), version_display_c_strs.size())) {
        if (current_version_idx >= 0) {
            strncpy(temp_settings.version_str, VERSION_STRINGS[current_version_idx],
                    sizeof(temp_settings.version_str) - 1);
            temp_settings.version_str[sizeof(temp_settings.version_str) - 1] = '\0';
        }
    }
    if (ImGui::IsItemHovered()) {
        char version_tooltip_buffer[1024];
        snprintf(version_tooltip_buffer, sizeof(version_tooltip_buffer), "Select the version of the template.\n"
                 "The number in brackets shows how many templates are available for that version.\n"
                 "This doesn't necessarily have to be the exact version of your minecraft instance.\n"
                 "Click on '(Learn more)' on the right to see the version ranges that functionally equal.");
        ImGui::SetTooltip("%s", version_tooltip_buffer);
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f)); // Use a link-like color
    ImGui::Text("(Learn more)");
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered()) {
        char open_official_templates_tooltip_buffer[1024];
        snprintf(open_official_templates_tooltip_buffer, sizeof(open_official_templates_tooltip_buffer),
                 "Opens the version support page in your browser.");
        ImGui::SetTooltip("%s", open_official_templates_tooltip_buffer);
    }

    if (ImGui::IsItemClicked()) {
        const char *url = "https://github.com/LNXSeus/Advancely#extensive-version-support";
        char command[1024];
#ifdef _WIN32
        snprintf(command, sizeof(command), "start %s", url);
#elif __APPLE__
        snprintf(command, sizeof(command), "open %s", url);
#else
        snprintf(command, sizeof(command), "xdg-open %s", url);
#endif
        system(command);
    }

    // Only show the StatsPerWorld checkbox for legacy versions
    MC_Version selected_version = settings_get_version_from_string(temp_settings.version_str);
    if (selected_version <= MC_VERSION_1_6_4) {
        ImGui::Checkbox("Using StatsPerWorld Mod", &temp_settings.using_stats_per_world_legacy);
        if (ImGui::IsItemHovered()) {
            char stats_per_world_tooltip_buffer[1024];
            snprintf(stats_per_world_tooltip_buffer, sizeof(stats_per_world_tooltip_buffer),
                     "The StatsPerWorld Mod (with Legacy Fabric) allows legacy Minecraft versions\n"
                     "to track stats locally per world. Check this if you're using this mod.\n\n"
                     "If unchecked, the tracker will use a snapshot system to simulate per-world\n"
                     "progress, and achievements will indicate if they were completed on a previous world.");
            ImGui::SetTooltip("%s", stats_per_world_tooltip_buffer);
        }
    }

    // --- Version-dependent labels ---
    // Achievement/Advancement
    const char *advancement_label_uppercase = (selected_version <= MC_VERSION_1_11_2) ? "Achievement" : "Advancement";

    // Advancements/Achievements
    const char *advancements_label_plural_uppercase = (selected_version <= MC_VERSION_1_11_2)
                                                          ? "Achievements"
                                                          : "Advancements";
    // advancements/achievements
    const char *advancements_label_plural_lowercase = (selected_version <= MC_VERSION_1_11_2)
                                                          ? "achievements"
                                                          : "advancements";
    // Adv/Ach
    const char *advancements_label_short_upper = (selected_version <= MC_VERSION_1_11_2) ? "Ach" : "Adv";

    // --- SCANNING & UI LOGIC ---
    if (strcmp(last_scanned_version, temp_settings.version_str) != 0) {
        free_discovered_templates(&discovered_templates, &discovered_template_count);
        scan_for_templates(temp_settings.version_str, &discovered_templates, &discovered_template_count);
        strncpy(last_scanned_version, temp_settings.version_str, sizeof(last_scanned_version) - 1);
        last_scanned_version[sizeof(last_scanned_version) - 1] = '\0';

        // Re-populate static category list
        unique_category_values.clear();
        if (discovered_template_count > 0) {
            for (int i = 0; i < discovered_template_count; ++i) {
                unique_category_values.push_back(discovered_templates[i].category);
            }
            std::sort(unique_category_values.begin(), unique_category_values.end());
            unique_category_values.erase(std::unique(unique_category_values.begin(), unique_category_values.end()),
                                         unique_category_values.end());
        }

        // After scan, validate and reset current selection if it's no longer valid for the new version
        bool category_is_valid = false;
        for (const auto &cat: unique_category_values) {
            if (cat == temp_settings.category) {
                category_is_valid = true;
                break;
            }
        }
        if (!category_is_valid) {
            if (!unique_category_values.empty()) {
                strncpy(temp_settings.category, unique_category_values[0].c_str(), sizeof(temp_settings.category) - 1);
                temp_settings.category[sizeof(temp_settings.category) - 1] = '\0';
            } else {
                temp_settings.category[0] = '\0';
            }
            // Since category is invalid, the flag must also be reset
            temp_settings.optional_flag[0] = '\0';
            temp_settings.lang_flag[0] = '\0';
        }

        // This change may have altered the hotkey structure. To prevent a false "Unsaved Changes"
        // flag, we re-sync the snapshot to this new, clean state.
        // memcpy(&saved_settings, &temp_settings, sizeof(AppSettings)); // TODO: Critical
    }

    // --- CATEGORY DROPDOWN ---
    category_display_names.clear();
    for (const auto &cat: unique_category_values) {
        category_display_names.push_back(cat.c_str());
    }

    int category_idx = -1;
    for (size_t i = 0; i < category_display_names.size(); ++i) {
        if (strcmp(category_display_names[i], temp_settings.category) == 0) {
            category_idx = i;
            break;
        }
    }

    if (ImGui::Combo("Category", &category_idx, category_display_names.data(), category_display_names.size())) {
        if (category_idx >= 0 && (size_t) category_idx < category_display_names.size()) {
            strncpy(temp_settings.category, category_display_names[category_idx], sizeof(temp_settings.category) - 1);
            temp_settings.category[sizeof(temp_settings.category) - 1] = '\0';

            // When category changes, immediately set the flag to the first available option
            bool flag_set = false;
            for (int i = 0; i < discovered_template_count; ++i) {
                if (strcmp(discovered_templates[i].category, temp_settings.category) == 0) {
                    strncpy(temp_settings.optional_flag, discovered_templates[i].optional_flag,
                            sizeof(temp_settings.optional_flag) - 1);
                    temp_settings.optional_flag[sizeof(temp_settings.optional_flag) - 1] = '\0';
                    flag_set = true;
                    break;
                }
            }
            if (!flag_set) temp_settings.optional_flag[0] = '\0';
        }
    }

    if (ImGui::IsItemHovered()) {
        char category_tooltip_buffer[1024];
        snprintf(category_tooltip_buffer, sizeof(category_tooltip_buffer),
                 "Choose between available categories for the selected version.\n"
                 "If the category you're looking for isn't available you can create it\n"
                 "by clicking the 'Edit Templates' button or view the list of officially added\n"
                 "templates by clicking the '(Learn more)' button next to the 'Template Settings'.");
        ImGui::SetTooltip("%s", category_tooltip_buffer);
    }


    // --- OPTIONAL FLAG DROPDOWN ---
    flag_values.clear();
    flag_display_names.clear();

    if (temp_settings.category[0] != '\0') {
        for (int i = 0; i < discovered_template_count; ++i) {
            if (strcmp(discovered_templates[i].category, temp_settings.category) == 0) {
                const char *flag = discovered_templates[i].optional_flag;
                flag_values.push_back(flag);
                if (flag[0] == '\0') {
                    flag_display_names.push_back("None");
                } else {
                    flag_display_names.push_back(flag_values.back().c_str());
                }
            }
        }
    }

    int flag_idx = -1;
    for (size_t i = 0; i < flag_values.size(); ++i) {
        if (strcmp(flag_values[i].c_str(), temp_settings.optional_flag) == 0) {
            flag_idx = i;
            break;
        }
    }

    if (ImGui::Combo("Optional Flag", &flag_idx, flag_display_names.data(), flag_display_names.size())) {
        if (flag_idx >= 0 && (size_t) flag_idx < flag_values.size()) {
            strncpy(temp_settings.optional_flag, flag_values[flag_idx].c_str(),
                    sizeof(temp_settings.optional_flag) - 1);
            temp_settings.optional_flag[sizeof(temp_settings.optional_flag) - 1] = '\0';
        }
    }

    if (ImGui::IsItemHovered()) {
        char flag_tooltip_buffer[1024];
        snprintf(flag_tooltip_buffer, sizeof(flag_tooltip_buffer),
                 "Choose between available optional flags for the selected version and category.\n"
                 "The optional flag is used to differentiate between different versions of the same template.\n");
        ImGui::SetTooltip("%s", flag_tooltip_buffer);
    }

    // --- LANGUAGE DROPDOWN ---
    if (category_idx != -1) {
        // Find the selected template to get its available languages
        DiscoveredTemplate *selected_template = nullptr;
        for (int i = 0; i < discovered_template_count; ++i) {
            if (strcmp(discovered_templates[i].category, temp_settings.category) == 0 &&
                strcmp(discovered_templates[i].optional_flag, temp_settings.optional_flag) == 0) {
                selected_template = &discovered_templates[i];
                break;
            }
        }

        if (selected_template) {
            std::vector<const char *> lang_display_names;
            for (const auto &flag: selected_template->available_lang_flags) {
                lang_display_names.push_back(flag.empty() ? "Default" : flag.c_str());
            }

            int lang_idx = -1;
            for (size_t i = 0; i < selected_template->available_lang_flags.size(); ++i) {
                if (selected_template->available_lang_flags[i] == temp_settings.lang_flag) {
                    lang_idx = (int) i;
                    break;
                }
            }

            if (ImGui::Combo("Language", &lang_idx, lang_display_names.data(), (int) lang_display_names.size())) {
                if (lang_idx >= 0 && (size_t) lang_idx < selected_template->available_lang_flags.size()) {
                    const std::string &selected_flag_str = selected_template->available_lang_flags[lang_idx];
                    strncpy(temp_settings.lang_flag, selected_flag_str.c_str(), sizeof(temp_settings.lang_flag) - 1);
                    temp_settings.lang_flag[sizeof(temp_settings.lang_flag) - 1] = '\0';
                }
            }
            if (ImGui::IsItemHovered()) {
                char lang_tooltip_buffer[1024];
                snprintf(lang_tooltip_buffer, sizeof(lang_tooltip_buffer),
                         "Choose between available language files for the selected template.\n"
                         "The Default `_lang.json` is usually english and comes with every template.");
                ImGui::SetTooltip("%s", lang_tooltip_buffer);
            }
        }
    }

    if (show_template_not_found_error) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f)); // Red text
        if (temp_settings.category[0] == '\0') {
            ImGui::TextWrapped(
                "This template does not exist. Choose different version/category/flag or create a template.");
        } else {
            ImGui::TextWrapped("Error: The selected template file does not exist. Settings were not applied.");
            // To help with debugging, we can show the path that was checked.
            // ImGui::Text("Path checked: %s", temp_settings.template_path);
        }
        ImGui::PopStyleColor();
    }


    if (ImGui::Button("Open Template Folder")) {
        char templates_path[MAX_PATH_LENGTH];
        snprintf(templates_path, sizeof(templates_path), "%s/templates", get_resources_path());
        char command[MAX_PATH_LENGTH + 32];
#ifdef _WIN32
        path_to_windows_native(templates_path); // Convert to backslashes for explorer
        snprintf(command, sizeof(command), "explorer \"%s\"", templates_path);
#elif __APPLE__
        snprintf(command, sizeof(command), "open \"%s\"", templates_path);
#else
        snprintf(command, sizeof(command), "xdg-open \"%s\"", templates_path);
#endif
        system(command);
    }
    if (ImGui::IsItemHovered()) {
        char open_templates_folder_tooltip_buffer[1024];
        snprintf(open_templates_folder_tooltip_buffer, sizeof(open_templates_folder_tooltip_buffer),
                 "Opens the 'resources/templates' folder in your file explorer.");
        ImGui::SetTooltip("%s", open_templates_folder_tooltip_buffer);
    }

    // Place Template Creator Button in same line
    ImGui::SameLine();

    if (ImGui::Button("Edit Templates")) {
        *p_temp_creator_open = true; // Open the template creator window
    }
    if (ImGui::IsItemHovered()) {
        char open_template_creator_tooltip_buffer[1024];
        snprintf(open_template_creator_tooltip_buffer, sizeof(open_template_creator_tooltip_buffer),
                 "Open the Template Editor to modify or build a new template or language.");
        ImGui::SetTooltip("%s", open_template_creator_tooltip_buffer);
    }

    ImGui::Separator();
    ImGui::Spacing();

    // General Settings
    ImGui::Text("General Settings");


    ImGui::Checkbox("Enable Overlay", &temp_settings.enable_overlay);
    if (ImGui::IsItemHovered()) {
        char enable_overlay_tooltip_buffer[1024];
        if (selected_version <= MC_VERSION_1_6_4) {
            // Legacy
            snprintf(enable_overlay_tooltip_buffer, sizeof(enable_overlay_tooltip_buffer),
                     "Enables a separate, customizable window to show your progress, perfect for streaming.\n\n"
                     "Overlay Layout:\n"
                     " • Row 1: Sub-stats of complex stats (if not template hidden).\n"
                     "   (If two visible items share an icon, the parent's icon is overlaid.)\n"
                     " • Row 2: Main %s.\n" // Main achievements
                     " • Row 3: Stats, custom goals, and multi-stage goals.\n\n"
                     "Tips:\n"
                     " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                     " • A negative scroll speed animates items from right to left.\n"
                     " • To adjust horizontal spacing, shorten display names in the template creator.\n\n"
                     "IMPORTANT FOR STREAMERS:\n"
                     "Applying settings will restart the overlay window.\n"
                     "You may need to reselect it in your streaming software (e.g., OBS).",
                     advancements_label_plural_lowercase
            );
        } else if (selected_version <= MC_VERSION_1_11_2) {
            // Mid-era
            snprintf(enable_overlay_tooltip_buffer, sizeof(enable_overlay_tooltip_buffer),
                     "Enables a separate, customizable window to show your progress, perfect for streaming.\n\n"
                     "Overlay Layout:\n"
                     " • Row 1: %s criteria and sub-stats of complex stats (if not template hidden).\n"
                     "   (If two visible items share an icon, the parent's icon is overlaid.)\n"
                     " • Row 2: Main %s.\n"
                     " • Row 3: Stats, custom goals, and multi-stage goals.\n\n"
                     "Tips:\n"
                     " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                     " • A negative scroll speed animates items from right to left.\n"
                     " • To adjust horizontal spacing, shorten display names in the template creator.\n\n"
                     "IMPORTANT FOR STREAMERS:\n"
                     "Applying settings will restart the overlay window.\n"
                     "You may need to reselect it in your streaming software (e.g., OBS).",
                     advancement_label_uppercase, advancements_label_plural_lowercase
            );
        } else if (selected_version == MC_VERSION_25W14CRAFTMINE) {
            // Craftmine
            snprintf(enable_overlay_tooltip_buffer, sizeof(enable_overlay_tooltip_buffer),
                     "Enables a separate, customizable window to show your progress, perfect for streaming.\n\n"
                     "Overlay Layout:\n"
                     " • Row 1: %s criteria and sub-stats of complex stats (if not template hidden).\n"
                     "   (If two visible items share an icon, the parent's icon is overlaid.)\n"
                     " • Row 2: Main %s, recipes and unlocks.\n"
                     " • Row 3: Stats, custom goals, and multi-stage goals.\n\n"
                     "Tips:\n"
                     " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                     " • A negative scroll speed animates items from right to left.\n"
                     " • To adjust horizontal spacing, shorten display names in the template creator.\n\n"
                     "IMPORTANT FOR STREAMERS:\n"
                     "Applying settings will restart the overlay window.\n"
                     "You may need to reselect it in your streaming software (e.g., OBS).",
                     advancement_label_uppercase, advancements_label_plural_lowercase
            );
        } else {
            // Modern
            snprintf(enable_overlay_tooltip_buffer, sizeof(enable_overlay_tooltip_buffer),
                     "Enables a separate, customizable window to show your progress, perfect for streaming.\n\n"
                     "Overlay Layout:\n"
                     " • Row 1: %s criteria and sub-stats of complex stats.\n"
                     "   (If two items share an icon, the parent's icon is overlaid.)\n"
                     " • Row 2: Main %s and recipes.\n"
                     " • Row 3: Stats, custom goals, and multi-stage goals.\n\n"
                     "Tips:\n"
                     " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                     " • A negative scroll speed animates items from right to left.\n"
                     " • To adjust horizontal spacing, shorten display names in the template creator.\n\n"
                     "IMPORTANT FOR STREAMERS:\n"
                     "Applying settings will restart the overlay window.\n"
                     "You may need to reselect it in your streaming software (e.g., OBS).",
                     advancement_label_uppercase, advancements_label_plural_lowercase
            );
        }
        ImGui::SetTooltip("%s", enable_overlay_tooltip_buffer);
    }

    // This toggles the framerate of everything
    ImGui::DragFloat("Tracker FPS Limit", &temp_settings.fps, 1.0f, 10.0f, 540.0f, "%.0f");
    if (ImGui::IsItemHovered()) {
        char tracker_fps_limit_tooltip_buffer[1024];
        snprintf(tracker_fps_limit_tooltip_buffer, sizeof(tracker_fps_limit_tooltip_buffer),
                 "Limits the frames per second of the tracker window. Default is 60 FPS.\n"
                 "Higher values may result in higher GPU usage.");
        ImGui::SetTooltip("%s", tracker_fps_limit_tooltip_buffer);
    }

    // Conditionally display overlay related settings
    if (temp_settings.enable_overlay) {
        ImGui::DragFloat("Overlay FPS Limit", &temp_settings.overlay_fps, 1.0f, 10.0f, 540.0f, "%.0f");
        if (ImGui::IsItemHovered()) {
            char overlay_fps_limit_tooltip_buffer[1024];
            snprintf(overlay_fps_limit_tooltip_buffer, sizeof(overlay_fps_limit_tooltip_buffer),
                     "Limits the frames per second of the overlay window. Default is 60 FPS.\n"
                     "Higher values may result in higher GPU usage.");
            ImGui::SetTooltip("%s", overlay_fps_limit_tooltip_buffer);
        }

        ImGui::DragFloat("Overlay Scroll Speed", &temp_settings.overlay_scroll_speed, 0.001f, -25.00f, 25.00f, "%.3f");
        if (ImGui::IsItemHovered()) {
            char overlay_scroll_speed_tooltip_buffer[1024];
            snprintf(overlay_scroll_speed_tooltip_buffer, sizeof(overlay_scroll_speed_tooltip_buffer),
                     "A negative scroll speed animates from right-to-left\n"
                     "(items always appear in the same order as they are on the tracker).\n"
                     "A scroll speed of 0.0 is static.\n"
                     "Default of 1.0 scrolls 1440 pixels (default width) in 24 seconds.");
            ImGui::SetTooltip("%s", overlay_scroll_speed_tooltip_buffer);
        }

        ImGui::DragFloat("Sub-Stat Cycle Interval (s)", &temp_settings.overlay_stat_cycle_speed, 0.1f, 0.1f, 60.0f,
                         "%.3f s");
        if (ImGui::IsItemHovered()) {
            char substat_cycling_interval_tooltip_buffer[1024];
            snprintf(substat_cycling_interval_tooltip_buffer, sizeof(substat_cycling_interval_tooltip_buffer),
                     "The time in seconds before cycling to the next sub-stat on a multi-stat goal on the overlay.\n");
            ImGui::SetTooltip(
                "%s", substat_cycling_interval_tooltip_buffer);
        }

        if (ImGui::Checkbox("Speed Up Animation", &temp_settings.overlay_animation_speedup)) {
        }
        if (ImGui::IsItemHovered()) {
            char speed_up_tooltip_buffer[1024];
            snprintf(speed_up_tooltip_buffer, sizeof(speed_up_tooltip_buffer),
                     "Toggles speeding up the overlay animation by a factor of %.1f. Don't forget to hit apply!\n"
                     "On top of that you can also hold SPACE (also %.1f) when tabbed into the overlay window.",
                     OVERLAY_SPEEDUP_FACTOR, OVERLAY_SPEEDUP_FACTOR);

            ImGui::SetTooltip("%s", speed_up_tooltip_buffer);
        }

        ImGui::SameLine();

        ImGui::Checkbox("Hide Completed Row 3 Goals", &temp_settings.overlay_row3_remove_completed);
        if (ImGui::IsItemHovered()) {
            char hide_completed_row_3_tooltip_buffer[1024];
            snprintf(hide_completed_row_3_tooltip_buffer, sizeof(hide_completed_row_3_tooltip_buffer),
                     "If checked, all Goals (Stats, Custom Goals and Multi-Stage Goals) will disappear from Row 3 of the overlay.\n"
                     "This is independent of the main 'Goal Visibility' setting.");


            ImGui::SetTooltip("%s", hide_completed_row_3_tooltip_buffer);
        }
    }

    ImGui::Checkbox("Always On Top", &temp_settings.tracker_always_on_top);
    if (ImGui::IsItemHovered()) {
        char always_on_top_tooltip_buffer[1024];
        snprintf(always_on_top_tooltip_buffer, sizeof(always_on_top_tooltip_buffer),
                 "Forces the tracker window to always display above any other window.");
        ImGui::SetTooltip("%s", always_on_top_tooltip_buffer);
    }
    ImGui::SeparatorText("Goal Visibility");
    ImGui::RadioButton("Hide All Completed", (int *) &temp_settings.goal_hiding_mode, HIDE_ALL_COMPLETED);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Strictest hiding. Hides goals when they are completed AND hides goals marked as \"hidden\" in the template file.");
    }

    ImGui::SameLine();
    ImGui::RadioButton("Hide Template-Hidden Only", (int *) &temp_settings.goal_hiding_mode, HIDE_ONLY_TEMPLATE_HIDDEN);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Hides goals marked as \"hidden\" in the template file, but keeps all other completed goals visible.");
    }

    ImGui::SameLine();
    ImGui::RadioButton("Show All", (int *) &temp_settings.goal_hiding_mode, SHOW_ALL);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Shows everything. No goals will be hidden, regardless of their completion or template status.");
    }

    ImGui::Separator();
    ImGui::Spacing();

    if (temp_settings.enable_overlay) {
        ImGui::Text("Overlay Title Alignment:");
        if (ImGui::IsItemHovered()) {
            char overlay_title_alignment_tooltip_buffer[1024];
            snprintf(overlay_title_alignment_tooltip_buffer, sizeof(overlay_title_alignment_tooltip_buffer),
                     "Adjusts the horizontal positioning of the progress text on the overlay.");

            ImGui::SetTooltip("%s", overlay_title_alignment_tooltip_buffer);
        }
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
        if (ImGui::IsItemHovered()) {
            char overlay_text_sections_tooltip_buffer[1024];
            snprintf(overlay_text_sections_tooltip_buffer, sizeof(overlay_text_sections_tooltip_buffer),
                     "Configure which sections of the overlay progress text to display.\n"
                     "Hover over each checkbox for more info.\n"
                     "The socials can't be removed.");
            ImGui::SetTooltip("%s", overlay_text_sections_tooltip_buffer);
        }
        ImGui::SameLine();
        ImGui::Checkbox("World", &temp_settings.overlay_show_world);
        if (ImGui::IsItemHovered()) {
            char overlay_text_world_tooltip_buffer[1024];
            snprintf(overlay_text_world_tooltip_buffer, sizeof(overlay_text_world_tooltip_buffer),
                     "Shows the current world name.");
            ImGui::SetTooltip("%s", overlay_text_world_tooltip_buffer);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Run Details", &temp_settings.overlay_show_run_details);
        if (ImGui::IsItemHovered()) {
            char overlay_text_run_tooltip_buffer[1024];
            snprintf(overlay_text_run_tooltip_buffer, sizeof(overlay_text_run_tooltip_buffer),
                     "Shows the selected version, template category and optional flag.");
            ImGui::SetTooltip("%s", overlay_text_run_tooltip_buffer);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Progress", &temp_settings.overlay_show_progress);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);

            ImGui::TextUnformatted("Progress Breakdown");
            ImGui::Separator();

            ImGui::BulletText(
                "The %s counter tracks only the main goals defined in the \"%s\" section of your template file.",
                advancement_label_uppercase, advancements_label_plural_lowercase);

            ImGui::BulletText(
                "The Progress %% shows your total completion across all individual sub-tasks from all categories.\n"
                "Each of the following tasks has an equal weight in the calculation:");
            ImGui::Indent();
            ImGui::BulletText("Recipes");
            ImGui::BulletText("%s Criteria", advancements_label_short_upper);
            ImGui::BulletText("Unlocks (exclusive to 25w14craftmine)");
            ImGui::BulletText("Individual Sub-Stats");
            ImGui::BulletText("Custom Goals");
            ImGui::BulletText("Multi-Stage Goal Stages");
            ImGui::Unindent();

            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
        ImGui::Checkbox("IGT", &temp_settings.overlay_show_igt);
        if (ImGui::IsItemHovered()) {
            char overlay_text_igt_tooltip_buffer[1024];
            snprintf(overlay_text_igt_tooltip_buffer, sizeof(overlay_text_igt_tooltip_buffer),
                     "Shows the in-game time since the start of the run.\n"
                     "It's read from the statistics file so it's in ticks and only updated when the game saves.");
            ImGui::SetTooltip("%s", overlay_text_igt_tooltip_buffer);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Update Timer", &temp_settings.overlay_show_update_timer);
        if (ImGui::IsItemHovered()) {
            char overlay_text_timer_tooltip_buffer[1024];
            snprintf(overlay_text_timer_tooltip_buffer, sizeof(overlay_text_timer_tooltip_buffer),
                     "Shows the time since the last game file update.");
            ImGui::SetTooltip("%s", overlay_text_timer_tooltip_buffer);
        }

        ImGui::Separator();
        ImGui::Spacing();
    }

    ImGui::Text("Visual Settings");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();

        ImGui::BulletText(
            "For sliders (like R, G, B), you can drag the label\nleft or right to quickly adjust the value.");
        ImGui::BulletText("You can also click directly on any number to type in a precise value.");

        ImGui::BulletText("Click any color swatch to open a detailed color picker.");
        ImGui::BulletText(
            "Inside the picker, you can right-click the large color preview\nto copy its value as a HEX code (e.g., #0D1117).");

        ImGui::EndTooltip();
    }


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

    if (ImGui::ColorEdit3("Tracker Background Color", tracker_bg)) {
        temp_settings.tracker_bg_color = {
            (Uint8) (tracker_bg[0] * 255), (Uint8) (tracker_bg[1] * 255), (Uint8) (tracker_bg[2] * 255),
            (Uint8) (tracker_bg[3] * 255)
        };
    }
    if (ImGui::IsItemHovered()) {
        char tracker_bg_tooltip_buffer[1024];
        snprintf(tracker_bg_tooltip_buffer, sizeof(tracker_bg_tooltip_buffer),
                 "Configure the color of the tracker background.");
        ImGui::SetTooltip("%s", tracker_bg_tooltip_buffer);
    }

    // Conditionally display overlay background color picker
    if (temp_settings.enable_overlay) {
        if (ImGui::ColorEdit3("Overlay Background Color", overlay_bg)) {
            temp_settings.overlay_bg_color = {
                (Uint8) (overlay_bg[0] * 255), (Uint8) (overlay_bg[1] * 255), (Uint8) (overlay_bg[2] * 255),
                (Uint8) (overlay_bg[3] * 255)
            };
        }
        if (ImGui::IsItemHovered()) {
            char overlay_bg_tooltip_buffer[1024];
            snprintf(overlay_bg_tooltip_buffer, sizeof(overlay_bg_tooltip_buffer),
                     "Configure the color of the overlay background.\n"
                     "This is the color you'll need to color key in your streaming software (e.g., OBS).\n"
                     "Good settings to start within the color key filter: Similarity: 1, Smoothness: 210.");
            ImGui::SetTooltip("%s", overlay_bg_tooltip_buffer);
        }
    }

    if (ImGui::ColorEdit3("Tracker Text Color", text_col)) {
        temp_settings.text_color = {
            (Uint8) (text_col[0] * 255), (Uint8) (text_col[1] * 255), (Uint8) (text_col[2] * 255),
            (Uint8) (text_col[3] * 255)
        };
    }
    if (ImGui::IsItemHovered()) {
        char tracker_bg_tooltip_buffer[1024];
        snprintf(tracker_bg_tooltip_buffer, sizeof(tracker_bg_tooltip_buffer),
                 "Configure the text color of the tracker window.\n"
                 "This also affects the info window, the checkboxes and\n"
                 "the controls in the bottom right.");
        ImGui::SetTooltip("%s", tracker_bg_tooltip_buffer);
    }

    if (temp_settings.enable_overlay) {
        if (ImGui::ColorEdit3("Overlay Text Color", overlay_text_col)) {
            temp_settings.overlay_text_color = {
                (Uint8) (overlay_text_col[0] * 255), (Uint8) (overlay_text_col[1] * 255),
                (Uint8) (overlay_text_col[2] * 255), (Uint8) (overlay_text_col[3] * 255)
            };
        }
        if (ImGui::IsItemHovered()) {
            char tracker_bg_tooltip_buffer[1024];
            snprintf(tracker_bg_tooltip_buffer, sizeof(tracker_bg_tooltip_buffer),
                     "Configure the text color of the overlay window.");
            ImGui::SetTooltip("%s", tracker_bg_tooltip_buffer);
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
            char overlay_width_tooltip_buffer[1024];
            snprintf(overlay_width_tooltip_buffer, sizeof(overlay_width_tooltip_buffer),
                     "Adjusts the width of the overlay window.\nDefault: %dpx", OVERLAY_DEFAULT_WIDTH);
            ImGui::SetTooltip("%s", overlay_width_tooltip_buffer);
        }
    }

    ImGui::SeparatorText("Fonts");

    // --- Tracker/Overlay Font ---
    ImGui::Text("Tracker Font: %s", temp_settings.tracker_font_name);
    ImGui::SameLine();
    if (ImGui::Button("Browse##TrackerFont")) {
        char selected_font[256];
        if (open_font_file_dialog(selected_font, sizeof(selected_font))) {
            strncpy(temp_settings.tracker_font_name, selected_font, sizeof(temp_settings.tracker_font_name) - 1);
            temp_settings.tracker_font_name[sizeof(temp_settings.tracker_font_name) - 1] = '\0';
        }
    }
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[1024];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Select the font for the main tracker view.\n"
                 "This affects the goal display text, the top info bar, and the bottom control buttons.\n"
                 "Only choose fonts within the resources/fonts directory.\n\n"
                 "A restart is required to properly apply changes.");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    // TODO: Implement later with proper relative spacing, just need to enable setting again
    // ImGui::DragFloat("Tracker Font Size", &temp_settings.tracker_font_size, 0.5f, 8.0f, 72.0f, "%.1f");

    // --- Overlay Font ---
    if (temp_settings.enable_overlay) {
        ImGui::Text("Overlay Font: %s", temp_settings.overlay_font_name);
        ImGui::SameLine();
        if (ImGui::Button("Browse##OverlayFont")) {
            char selected_font[256];
            if (open_font_file_dialog(selected_font, sizeof(selected_font))) {
                strncpy(temp_settings.overlay_font_name, selected_font, sizeof(temp_settings.overlay_font_name) - 1);
                temp_settings.overlay_font_name[sizeof(temp_settings.overlay_font_name) - 1] = '\0';
            }
        }
        if (ImGui::IsItemHovered()) {
            char tooltip_buffer[1024];
            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                     "Select the font for the text in the separate stream overlay window.\n"
                     "Only choose fonts within the resources/fonts directory.");
            ImGui::SetTooltip("%s", tooltip_buffer);
        }
    }

    // --- Settings/UI Font ---
    ImGui::Text("Settings/UI Font: %s", temp_settings.ui_font_name);
    ImGui::SameLine();
    if (ImGui::Button("Browse##UIFont")) {
        char selected_font[256];
        if (open_font_file_dialog(selected_font, sizeof(selected_font))) {
            strncpy(temp_settings.ui_font_name, selected_font, sizeof(temp_settings.ui_font_name) - 1);
            temp_settings.ui_font_name[sizeof(temp_settings.ui_font_name) - 1] = '\0';
        }
    }
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[1024];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Select the font for UI windows.\n"
                 "This affects the Settings, Template Creator, and Notes windows.\n"
                 "Only choose fonts within the resources/fonts directory.\n\n"
                 "A restart is required to properly apply changes.");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    // TODO: Implement later with proper relative spacing, just need to enable setting again
    // ImGui::DragFloat("Settings Font Size", &temp_settings.ui_font_size, 0.5f, 8.0f, 72.0f, "%.1f");

    // --- Restart Warning ---
    bool font_settings_changed =
            strcmp(temp_settings.tracker_font_name, saved_settings.tracker_font_name) != 0 ||
            temp_settings.tracker_font_size != saved_settings.tracker_font_size ||
            // Overlay restarts automatically when changes are applied
            // strcmp(temp_settings.overlay_font_name, saved_settings.overlay_font_name) != 0 ||
            strcmp(temp_settings.ui_font_name, saved_settings.ui_font_name) != 0 ||
            temp_settings.ui_font_size != saved_settings.ui_font_size;

    if (font_settings_changed) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                           "First apply the settings then restart the app to apply font changes.");
    }

    ImGui::Separator();
    ImGui::Spacing();

    // --- Section Order ---
    ImGui::Text("Section Order");
    if (ImGui::IsItemHovered()) {
        char section_order_tooltip_buffer[1024];
        snprintf(section_order_tooltip_buffer, sizeof(section_order_tooltip_buffer),
                 "Drag and drop to reorder the sections in the main tracker window.");
        ImGui::SetTooltip("%s", section_order_tooltip_buffer);
    }

    // Create a temporary list of the indices of sections that should be visible for this version.
    std::vector<int> visible_section_indices;
    for (int i = 0; i < SECTION_COUNT; ++i) {
        int section_id = temp_settings.section_order[i];
        bool is_visible = true;

        if (section_id == SECTION_UNLOCKS && selected_version != MC_VERSION_25W14CRAFTMINE) {
            is_visible = false; // Hide "Unlocks" if the version is not 25w14craftmine
        }

        if (is_visible) {
            visible_section_indices.push_back(i);
        }
    }

    // Now, loop through only the visible sections to render them.
    for (size_t n = 0; n < visible_section_indices.size(); ++n) {
        int original_array_index = visible_section_indices[n];
        int item_type_id = temp_settings.section_order[original_array_index];

        // Determine the correct display name (Advancements vs. Achievements)
        const char *item_name;
        if (item_type_id == SECTION_ADVANCEMENTS) {
            item_name = (selected_version <= MC_VERSION_1_11_2) ? "Achievements" : "Advancements";
        } else {
            item_name = TRACKER_SECTION_NAMES[item_type_id];
        }

        ImGui::Selectable(item_name);

        // Drag Source: The payload is the index 'n' from our visible list.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("DND_SECTION_ORDER", &n, sizeof(int));
            ImGui::Text("Reorder %s", item_name);
            ImGui::EndDragDropSource();
        }

        // Drop Target
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_SECTION_ORDER")) {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int source_visible_index = *(const int *) payload->Data;
                int target_visible_index = n;

                // Map the visible list indices back to their original positions in the full section_order array.
                int source_original_index = visible_section_indices[source_visible_index];
                int target_original_index = visible_section_indices[target_visible_index];

                // Swap the items in the actual temp_settings.section_order array.
                int temp = temp_settings.section_order[target_original_index];
                temp_settings.section_order[target_original_index] = temp_settings.section_order[source_original_index];
                temp_settings.section_order[source_original_index] = temp;

                // The list will correctly re-render on the next frame.
            }
            ImGui::EndDragDropTarget();
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Debug Settings");

    ImGui::Checkbox("Print Debug To Console", &temp_settings.print_debug_status);
    if (ImGui::IsItemHovered()) {
        char debug_print_tooltip_buffer[1024];
        snprintf(debug_print_tooltip_buffer, sizeof(debug_print_tooltip_buffer),
                 "This toggles printing a detailed progress report to the console after every file update.\n"
                 "Currently it also toggles an FPS counter for the overlay window.\n\n"
                 "IMPORTANT: This can spam the console with a large amount of text if your template files contain many entries.\n\n"
                 "This setting only affects the detailed report.\n"
                 "Progress on %s is only printed if the game sends an update.\n"
                 "General status messages and errors are always printed to the console and saved to advancely_log.txt.\n"
                 "The log is flushed after every message and reset on startup, making it ideal for diagnosing crashes.\n"
                 "Everything the application prints to a console (like MSYS2 MINGW64) can also be found in advancely_log.txt.",
                 advancements_label_plural_lowercase);
        ImGui::SetTooltip("%s", debug_print_tooltip_buffer);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto-Check for Updates", &temp_settings.check_for_updates);
    if (ImGui::IsItemHovered()) {
        char auto_update_tooltip_buffer[1024];
        snprintf(auto_update_tooltip_buffer, sizeof(auto_update_tooltip_buffer),
                 "If enabled, Advancely will check for a new version on startup and notify you if one is available.\n"
                 "Through that notification you'll then be able to automatically install the update\n"
                 "for your operating system. You can find more instructions on that popup.");
        ImGui::SetTooltip("%s", auto_update_tooltip_buffer);
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
        ImGui::Text("Hotkey Settings for Custom Counters");
        if (ImGui::IsItemHovered()) {
            char hotkey_settings_tooltip_buffer[1024];
            snprintf(hotkey_settings_tooltip_buffer, sizeof(hotkey_settings_tooltip_buffer),
                     "IMPORTANT: Hotkeys are remembered between templates.\n"
                     "You might have to restart the settings window for the hotkeys to appear.\n\n"
                     "Assign keys to increment/decrement custom counters\n"
                     "(only work when tabbed into the tracker). Maximum of %d hotkeys are supported.",
                     MAX_HOTKEYS);
            ImGui::SetTooltip("%s", hotkey_settings_tooltip_buffer);
        }

        // Loop through the counters provided by the LIVE TEMPLATE to build the UI rows
        for (const auto &counter: custom_counters) {
            // For each counter, find its corresponding binding in our editable temp_settings
            HotkeyBinding *binding = nullptr;
            for (int i = 0; i < temp_settings.hotkey_count; ++i) {
                if (strcmp(temp_settings.hotkeys[i].target_goal, counter->root_name) == 0) {
                    binding = &temp_settings.hotkeys[i];
                    break;
                }
            }

            ImGui::Text("%s", counter->display_name);
            ImGui::SameLine();

            // --- Increment Key Combo ---
            char *inc_key_val = binding ? binding->increment_key : (char *) "None";
            int current_inc_key_idx = 0;
            for (int k = 0; k < key_names_count; ++k) {
                if (strcmp(inc_key_val, key_names[k]) == 0) {
                    current_inc_key_idx = k;
                    break;
                }
            }

            char inc_label[64];
            snprintf(inc_label, sizeof(inc_label), "##inc_%s", counter->root_name);
            if (ImGui::Combo(inc_label, &current_inc_key_idx, key_names, key_names_count)) {
                // User made a change. We now modify temp_settings.
                if (!binding) {
                    // If binding didn't exist, add a new one.
                    if (temp_settings.hotkey_count < MAX_HOTKEYS) {
                        binding = &temp_settings.hotkeys[temp_settings.hotkey_count++];
                        strncpy(binding->target_goal, counter->root_name, sizeof(binding->target_goal) - 1);
                        binding->target_goal[sizeof(binding->target_goal) - 1] = '\0';
                        strcpy(binding->decrement_key, "None"); // Set default for the other key
                        binding->decrement_key[sizeof(binding->decrement_key) - 1] = '\0';
                    }
                }
                if (binding) {
                    strncpy(binding->increment_key, key_names[current_inc_key_idx], sizeof(binding->increment_key) - 1);
                    binding->increment_key[sizeof(binding->increment_key) - 1] = '\0';
                }
            }

            ImGui::SameLine();

            // --- Decrement Key Combo ---
            char *dec_key_val = binding ? binding->decrement_key : (char *) "None";
            int current_dec_key_idx = 0;
            for (int k = 0; k < key_names_count; ++k) {
                if (strcmp(dec_key_val, key_names[k]) == 0) {
                    current_dec_key_idx = k;
                    break;
                }
            }

            char dec_label[64];
            snprintf(dec_label, sizeof(dec_label), "##dec_%s", counter->root_name);
            if (ImGui::Combo(dec_label, &current_dec_key_idx, key_names, key_names_count)) {
                // User made a change. We now modify temp_settings.
                if (!binding) {
                    // If binding didn't exist, add a new one.
                    if (temp_settings.hotkey_count < MAX_HOTKEYS) {
                        binding = &temp_settings.hotkeys[temp_settings.hotkey_count++];
                        strncpy(binding->target_goal, counter->root_name, sizeof(binding->target_goal) - 1);
                        binding->target_goal[sizeof(binding->target_goal) - 1] = '\0';
                        strcpy(binding->increment_key, "None"); // Set default for the other key
                        binding->increment_key[sizeof(binding->increment_key) - 1] = '\0';
                    }
                } else {
                    // if binding exists, update it
                    strncpy(binding->decrement_key, key_names[current_dec_key_idx], sizeof(binding->decrement_key) - 1);
                    binding->decrement_key[sizeof(binding->decrement_key) - 1] = '\0';
                }
            }
        }

        ImGui::Separator();
        ImGui::Spacing();
    }

    // Apply the changes or pressing Enter key in the settings window when NO popup is shown
    if (ImGui::Button("Apply Settings") || (ImGui::IsKeyPressed(ImGuiKey_Enter) &&
                                            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !
                                            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))) {
        // Reset message visibility on each new attempt
        show_applied_message = false;
        show_defaults_applied_message = false; // Reset the other message
        show_hotkey_warning_message = false;

        // Assume the error is cleared unless we find one
        show_invalid_manual_path_error = false;

        show_template_not_found_error = false;

        // 1. Construct the potential template path from the temporary settings.
        construct_template_paths(&temp_settings);

        // 2. Check if a category is selected and if the corresponding template file exists.
        if (temp_settings.category[0] == '\0') {
            // It's an error to apply with "None" selected for category.
            show_template_not_found_error = true;
        } else if (!path_exists(temp_settings.template_path)) {
            // The constructed path does not point to a real file.
            show_template_not_found_error = true;
        } else {
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

                    // Hotkey warning logic
                    // Check if this is a template change AND if there were any active hotkeys
                    bool is_template_change = (strcmp(temp_settings.version_str, saved_settings.version_str) != 0 ||
                                               strcmp(temp_settings.category, saved_settings.category) != 0 ||
                                               strcmp(temp_settings.optional_flag, saved_settings.optional_flag) != 0);
                    bool had_active_hotkeys = false;
                    for (int i = 0; i < saved_settings.hotkey_count; ++i) {
                        if (strcmp(saved_settings.hotkeys[i].increment_key, "None") != 0 ||
                            strcmp(saved_settings.hotkeys[i].decrement_key, "None") != 0) {
                            had_active_hotkeys = true;
                            break;
                        }
                    }

                    // Copy temp settings to the real settings, save, and trigger a reload
                    memcpy(app_settings, &temp_settings, sizeof(AppSettings));
                    // Also update our clean snapshot
                    memcpy(&saved_settings, &temp_settings, sizeof(AppSettings));
                    SDL_SetWindowAlwaysOnTop(t->window, app_settings->tracker_always_on_top);
                    settings_save(app_settings, nullptr, SAVE_CONTEXT_ALL);
                    SDL_SetAtomicInt(&g_settings_changed, 1); // Trigger a reload
                    SDL_SetAtomicInt(&g_apply_button_clicked, 1);
                    show_applied_message = true;

                    // Display hotkey warning
                    if (is_template_change && had_active_hotkeys) {
                        show_hotkey_warning_message = true;
                    } else {
                        show_applied_message = true;
                    }
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

                    // Hotkey Warning Logic
                    bool is_template_change = (strcmp(temp_settings.version_str, saved_settings.version_str) != 0 ||
                                               strcmp(temp_settings.category, saved_settings.category) != 0 ||
                                               strcmp(temp_settings.optional_flag, saved_settings.optional_flag) != 0);
                    bool had_active_hotkeys = false;
                    for (int i = 0; i < saved_settings.hotkey_count; ++i) {
                        if (strcmp(saved_settings.hotkeys[i].increment_key, "None") != 0 ||
                            strcmp(saved_settings.hotkeys[i].decrement_key, "None") != 0) {
                            had_active_hotkeys = true;
                            break;
                        }
                    }

                    memcpy(app_settings, &temp_settings, sizeof(AppSettings));
                    // Also update our clean snapshot
                    memcpy(&saved_settings, &temp_settings, sizeof(AppSettings));
                    SDL_SetWindowAlwaysOnTop(t->window, app_settings->tracker_always_on_top);
                    settings_save(app_settings, nullptr, SAVE_CONTEXT_ALL);
                    SDL_SetAtomicInt(&g_settings_changed, 1);
                    SDL_SetAtomicInt(&g_apply_button_clicked, 1);
                    show_applied_message = true;

                    // Display hotkey warning message
                    if (is_template_change && had_active_hotkeys) {
                        show_hotkey_warning_message = true;
                    } else {
                        show_applied_message = true;
                    }
                }
            }
        }
    }

    // Hover text for the apply button
    if (ImGui::IsItemHovered()) {
        char apply_button_tooltip_buffer[1024];
        snprintf(apply_button_tooltip_buffer, sizeof(apply_button_tooltip_buffer),
                 "Apply any changes made in this window. You can also press 'Enter' to apply.\n"
                 "Changes made to the overlay window will cause the overlay to restart,\n"
                 "which might lead to OBS not capturing the overlay anymore.\n"
                 "It will fail to apply if any warnings are shown.");
        ImGui::SetTooltip("%s", apply_button_tooltip_buffer);
    }

    // If there are unsaved changes, display the indicator
    if (has_unsaved_changes) {
        ImGui::SameLine();
        // Replace the TextColored indicator with a Revert button
        if (ImGui::Button("Revert Changes")) {
            memcpy(&temp_settings, &saved_settings, sizeof(AppSettings));
        }
        if (ImGui::IsItemHovered()) {
            char revert_button_tooltip_buffer[1024];
            snprintf(revert_button_tooltip_buffer, sizeof(revert_button_tooltip_buffer),
                     "Revert any changes made within the settings window since the last save.\n"
                     "(Ctrl+Z / Cmd+Z)");
            ImGui::SetTooltip("%s", revert_button_tooltip_buffer);
        }
    }


    // Show the confirmation message if settings were applied
    if (show_applied_message) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Settings Applied!");
    }

    // Show the hotkey warning message if needed
    if (show_hotkey_warning_message) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Reopen settings to see updated hotkeys.");
    }

    // Show the confirmation message if settings were reset
    if (show_defaults_applied_message) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Defaults Applied!");
    }

    // Place the next button on the same line
    ImGui::SameLine();

    if (ImGui::Button("Reset To Defaults")) {
        // Clear any previous "Applied!" message and show the "Defaults!" message
        show_applied_message = false;
        show_defaults_applied_message = true;

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
                 "  - Category: %s\n"
                 "  - Optional Flag: %s\n"
                 "  - Language: Default\n"
                 "  - StatsPerWorld Mod (Legacy): %s\n"
                 "  - Section Order: %s -> Recipes -> Unlocks -> Stats -> Custom -> Multi-Stage\n"
                 "  - Enable Overlay: %s\n"
                 "  - Tracker FPS Limit: %d\n"
                 "  - Overlay FPS Limit: %d\n"
                 "  - Overlay Scroll Speed: %.2f\n"
                 "  - Sub-Stat Cycle Speed: %.1f s\n"
                 "  - Speed Up Animation: %s\n"
                 "  - Hide Completed Row 3 Goals: %s\n"
                 "  - Always On Top: %s\n"
                 "  - Tracker Font: %s\n"
                 "  - Overlay Font: %s\n"
                 "  - UI Font: %s\n"
                 "  - Goal Visibility: Hide All Completed\n"
                 "  - Overlay Width: %dpx\n"
                 "  - Notes Use Settings Font: %s\n"
                 "  - Print Debug To Console: %s\n"
                 "  - Check For Updates: %s\n"
                 "  - Show Welcome on Startup: %s",

                 DEFAULT_VERSION,
                 DEFAULT_CATEGORY,
                 DEFAULT_OPTIONAL_FLAG,
                 DEFAULT_USING_STATS_PER_WORLD_LEGACY ? "Enabled" : "Disabled",
                 advancements_label_plural_uppercase,
                 DEFAULT_ENABLE_OVERLAY ? "Enabled" : "Disabled",
                 DEFAULT_FPS,
                 DEFAULT_OVERLAY_FPS,
                 DEFAULT_OVERLAY_SCROLL_SPEED,
                 DEFAULT_OVERLAY_STAT_CYCLE_SPEED,
                 DEFAULT_OVERLAY_SPEED_UP ? "Enabled" : "Disabled",
                 DEFAULT_OVERLAY_ROW3_REMOVE_COMPLETED ? "Enabled" : "Disabled",
                 DEFAULT_TRACKER_ALWAYS_ON_TOP ? "Enabled" : "Disabled",
                 DEFAULT_TRACKER_FONT,
                 DEFAULT_OVERLAY_FONT,
                 DEFAULT_UI_FONT,
                 OVERLAY_DEFAULT_WIDTH,
                 DEFAULT_NOTES_USE_ROBOTO ? "Enabled" : "Disabled",
                 DEFAULT_PRINT_DEBUG_STATUS ? "Enabled" : "Disabled",
                 DEFAULT_CHECK_FOR_UPDATES ? "Enabled" : "Disabled",
                 DEFAULT_SHOW_WELCOME_ON_STARTUP ? "Enabled" : "Disabled"
        );
        ImGui::SetTooltip("%s", tooltip_buffer);
    }

    ImGui::SameLine();

    if (ImGui::Button("Restart Advancely")) {
        // 1. Save any pending changes from the settings window first.
        memcpy(app_settings, &temp_settings, sizeof(AppSettings));
        settings_save(app_settings, nullptr, SAVE_CONTEXT_ALL);

        // 2. Initiate the restart process.
        if (application_restart()) {
            // 3. If the script was launched successfully, post a quit event to close the app.
            SDL_Event quit_event;
            quit_event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit_event);
        } else {
            // If creating the script failed, notify the user.
            show_error_message("Restart Failed",
                               "Could not create the restart script. Please restart the application manually.");
        }
    }
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[1024];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Saves all current settings and restarts the application.\n"
                 "This is required to apply changes to fonts within the tracker window.");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }

    if (roboto_font) {
        ImGui::PopFont();
    }

    ImGui::End();
}
