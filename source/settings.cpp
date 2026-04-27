// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 26.06.2025.
//


#include "settings.h"

#include "format_utils.h"
#include "supporters.h"

// Includes for fork() and execvp() on Linux/macOS
#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif

#include <algorithm>
#include <string>
#include <cstring> // For memcmp on simple sub-structs

#include "logger.h"

#include <vector>

#include "dialog_utils.h"
#include "mojang_api.h"
#include "settings_utils.h" // ImGui imported through this
#include "global_event_handler.h" // For global variables
#include "path_utils.h" // For path_exists()
#include "template_scanner.h"
#include "temp_creator.h"
#include "update_checker.h"
#include "coop_net.h" // For co-op networking status display
#include <SDL3/SDL_clipboard.h> // For room code copy/paste

// Build and set the template sync JSON payload on the co-op context.
// Called when host starts and when settings are applied while hosting.
static void update_coop_template_sync(const AppSettings *s) {
    if (!g_coop_ctx) return;
    const char *stat_merge = (s->coop_stat_merge == COOP_STAT_CUMULATIVE) ? "cumulative" : "highest";
    const char *stat_cb = (s->coop_stat_checkbox == COOP_STAT_CHECKBOX_HOST_ONLY) ? "host_only" : "any_player";
    const char *custom = (s->coop_custom_goal_mode == COOP_CUSTOM_HOST_ONLY) ? "host_only" : "any_player";
    // Compute a hash of the template's goal structure for receiver validation
    uint64_t goal_hash = compute_template_goal_hash(s->template_path);
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "{\"version\":\"%s\",\"category\":\"%s\",\"optional_flag\":\"%s\","
             "\"stat_merge\":\"%s\",\"stat_checkbox\":\"%s\",\"custom_goal_mode\":\"%s\","
             "\"template_hash\":\"%016llx\",\"using_hermes\":%s}",
             s->version_str, s->category, s->optional_flag,
             stat_merge, stat_cb, custom,
             (unsigned long long) goal_hash,
             s->using_hermes ? "true" : "false");
    coop_net_set_template_sync(g_coop_ctx, buf);
}

// Helper function to robustly compare two AppSettings structs
// Changing window geometry of overlay and tracker window DO NOT cause the "Unsaved Changes" text to appear.
static bool are_settings_different(const AppSettings *a, const AppSettings *b) {
    if (a->path_mode != b->path_mode ||
        strcmp(a->manual_saves_path, b->manual_saves_path) != 0 ||
        strcmp(a->fixed_world_path, b->fixed_world_path) != 0 ||
        strcmp(a->version_str, b->version_str) != 0 ||
        strcmp(a->display_version_str, b->display_version_str) != 0 ||
        strcmp(a->category, b->category) != 0 ||
        strcmp(a->optional_flag, b->optional_flag) != 0 ||
        strcmp(a->category_display_name, b->category_display_name) != 0 ||
        a->lock_category_display_name != b->lock_category_display_name ||
        strcmp(a->lang_flag, b->lang_flag) != 0 ||
        a->enable_overlay != b->enable_overlay ||
        a->using_stats_per_world_legacy != b->using_stats_per_world_legacy ||
        a->using_hermes != b->using_hermes ||
        a->fps != b->fps ||
        a->overlay_fps != b->overlay_fps ||
        a->tracker_always_on_top != b->tracker_always_on_top ||
        a->goal_hiding_mode != b->goal_hiding_mode ||
        a->print_debug_status != b->print_debug_status ||

        // Overlay settings
        a->overlay_scroll_speed != b->overlay_scroll_speed ||
        a->overlay_progress_text_align != b->overlay_progress_text_align ||
        a->overlay_row1_spacing != b->overlay_row1_spacing ||
        a->overlay_row1_shared_icon_size != b->overlay_row1_shared_icon_size ||
        a->overlay_row2_custom_spacing_enabled != b->overlay_row2_custom_spacing_enabled ||
        a->overlay_row2_custom_spacing != b->overlay_row2_custom_spacing ||
        a->overlay_row3_custom_spacing_enabled != b->overlay_row3_custom_spacing_enabled ||
        a->overlay_row3_custom_spacing != b->overlay_row3_custom_spacing ||
        a->overlay_row3_remove_completed != b->overlay_row3_remove_completed ||
        a->overlay_stat_cycle_speed != b->overlay_stat_cycle_speed ||
        a->tracker_vertical_spacing != b->tracker_vertical_spacing ||

        // LOD Settings
        a->lod_text_sub_threshold != b->lod_text_sub_threshold ||
        a->lod_text_main_threshold != b->lod_text_main_threshold ||
        a->lod_icon_detail_threshold != b->lod_icon_detail_threshold ||

        a->scrollable_list_threshold != b->scrollable_list_threshold ||
        a->tracker_list_scroll_speed != b->tracker_list_scroll_speed ||

        a->notes_use_roboto_font != b->notes_use_roboto_font ||
        a->check_for_updates != b->check_for_updates ||
        a->show_welcome_on_startup != b->show_welcome_on_startup ||
        a->overlay_show_world != b->overlay_show_world ||
        a->overlay_show_run_details != b->overlay_show_run_details ||
        a->overlay_show_progress != b->overlay_show_progress ||
        a->overlay_show_igt != b->overlay_show_igt ||
        a->igt_unit_spacing != b->igt_unit_spacing ||
        a->igt_always_show_ms != b->igt_always_show_ms ||
        a->overlay_show_update_timer != b->overlay_show_update_timer ||

        strcmp(a->tracker_font_name, b->tracker_font_name) != 0 ||
        a->tracker_font_size != b->tracker_font_size ||
        a->tracker_sub_font_size != b->tracker_sub_font_size ||
        a->tracker_ui_font_size != b->tracker_ui_font_size ||
        strcmp(a->ui_font_name, b->ui_font_name) != 0 ||
        a->ui_font_size != a->ui_font_size ||
        strcmp(a->overlay_font_name, b->overlay_font_name) != 0 ||

        memcmp(&a->tracker_bg_color, &b->tracker_bg_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->overlay_bg_color, &b->overlay_bg_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->text_color, &b->text_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->overlay_text_color, &b->overlay_text_color, sizeof(ColorRGBA)) != 0 ||

        // UI Colors
        memcmp(&a->ui_text_color, &b->ui_text_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_window_bg_color, &b->ui_window_bg_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_frame_bg_color, &b->ui_frame_bg_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_frame_bg_hovered_color, &b->ui_frame_bg_hovered_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_frame_bg_active_color, &b->ui_frame_bg_active_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_title_bg_active_color, &b->ui_title_bg_active_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_button_color, &b->ui_button_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_button_hovered_color, &b->ui_button_hovered_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_button_active_color, &b->ui_button_active_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_header_color, &b->ui_header_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_header_hovered_color, &b->ui_header_hovered_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_header_active_color, &b->ui_header_active_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->ui_check_mark_color, &b->ui_check_mark_color, sizeof(ColorRGBA)) != 0 ||

        memcmp(a->tracker_section_custom_width_enabled, b->tracker_section_custom_width_enabled,
               sizeof(a->tracker_section_custom_width_enabled)) != 0 ||
        memcmp(a->tracker_section_custom_item_width, b->tracker_section_custom_item_width,
               sizeof(a->tracker_section_custom_item_width)) != 0 ||
        memcmp(a->section_order, b->section_order, sizeof(a->section_order)) != 0 ||

        // Account settings
        a->account_type != b->account_type ||
        strcmp(a->local_player.username, b->local_player.username) != 0 ||
        strcmp(a->local_player.uuid, b->local_player.uuid) != 0 ||
        strcmp(a->local_player.display_name, b->local_player.display_name) != 0 ||

        // Co-op settings
        a->coop_enabled != b->coop_enabled ||
        a->coop_auto_accept != b->coop_auto_accept ||
        a->network_mode != b->network_mode ||
        a->coop_transport != b->coop_transport ||
        a->coop_stat_merge != b->coop_stat_merge ||
        a->coop_stat_checkbox != b->coop_stat_checkbox ||
        a->coop_custom_goal_mode != b->coop_custom_goal_mode ||
        strcmp(a->host_ip, b->host_ip) != 0 ||
        strcmp(a->host_port, b->host_port) != 0 ||
        a->coop_player_count != b->coop_player_count) {
        return true;
    }

    // Compare player roster
    for (int i = 0; i < a->coop_player_count; ++i) {
        if (strcmp(a->coop_players[i].username, b->coop_players[i].username) != 0 ||
            strcmp(a->coop_players[i].uuid, b->coop_players[i].uuid) != 0 ||
            strcmp(a->coop_players[i].display_name, b->coop_players[i].display_name) != 0) {
            return true;
        }
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

// Robustly opens a URL or local folder using SDL, falling back to system commands if needed.
/**
 * @brief Robustly opens a URL or local folder using SDL, falling back to system commands if needed.
 *
 * @param target The URL or local folder path to open.
 */
static void open_content(const char *target) {
    if (!target || target[0] == '\0') return;

    // SDL_OpenURL covers all URLs and most local paths cross-platform.
    // On Windows it maps to ShellExecuteW internally, so for URLs we never
    // fall through to the ShellExecute fallback (which would open a second tab).
    bool is_url = (strncmp(target, "http://", 7) == 0 || strncmp(target, "https://", 8) == 0);
    if (SDL_OpenURL(target) == 0 || is_url) {
        return;
    }

    // Fallback for local directories if SDL_OpenURL fails (common on macOS/Linux for file paths)
#ifdef _WIN32
    // Windows: ShellExecute for local paths only (URLs already handled above)
    ShellExecuteA(nullptr, "open", target, nullptr, nullptr, SW_SHOW);
#elif defined(__APPLE__)
    // macOS: The 'open' command is non-blocking and handles both URLs and Paths
    char command[MAX_PATH_LENGTH + 16];
    snprintf(command, sizeof(command), "open \"%s\"", target);
    system(command);
#else
    // Linux: 'xdg-open' is the standard
    char command[MAX_PATH_LENGTH + 16];
    snprintf(command, sizeof(command), "xdg-open \"%s\"", target);
    system(command);
#endif
}

void settings_render_gui(bool *p_open, AppSettings *app_settings, ImFont *roboto_font, Tracker *t,
                         ForceOpenReason *force_open_reason, bool *p_temp_creator_open) {
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

    // Co-op error flag (block Apply when host IP/port is invalid)
    static bool coop_host_input_error = false;

    // Hotkey duplicate error flag (block Apply when two goals share the same key)
    static bool hotkey_duplicate_error = false;

    // Account validation error flag (block Apply when UUID is empty or has bad format)
    static bool account_validation_error = false;

    // Co-op tab state (at function scope so revert/open can reset them)
    static char coop_identity_status_msg[256] = "";
    static bool coop_identity_status_is_error = false;
    static char coop_room_code_buf[128] = "";
    static char coop_room_code_error[256] = "";
    static bool coop_ip_revealed = false;
    static bool coop_public_ip_revealed = false;
    static bool coop_relay_password_host_revealed = false;
    static bool coop_relay_password_recv_revealed = false;
    // Relay session inputs (intentionally NOT persisted to settings.json).
    static char coop_relay_password_host[128] = "";
    static char coop_relay_room_code_recv[16] = "";
    static char coop_relay_password_recv[128] = "";

    // Holds temporary copy of the settings for editing
    static AppSettings temp_settings;

    // Add a snapshot to compare agains
    static AppSettings saved_settings;

    static DiscoveredTemplate *discovered_templates = nullptr;
    static int discovered_template_count = 0;
    static char last_scanned_version[64] = "";

    static std::vector<std::string> unique_category_values;
    static std::vector<const char *> category_display_names;
    static std::vector<std::string> category_display_strings; // Owned strings for category display
    static std::vector<std::string> flag_values;
    static std::vector<const char *> flag_display_names;
    static std::vector<std::string> flag_display_strings; // Owned strings for flag display

    // State for version dropdown with counts
    static std::vector<std::string> version_display_names;
    static std::vector<const char *> version_display_c_strs;
    static bool version_counts_generated = false;

    // Helper lambda to auto-select a language for the currently selected template
    auto auto_select_language = [&]() {
        DiscoveredTemplate *selected_template = nullptr;
        for (int i = 0; i < discovered_template_count; ++i) {
            if (strcmp(discovered_templates[i].category, temp_settings.category) == 0 &&
                strcmp(discovered_templates[i].optional_flag, temp_settings.optional_flag) == 0) {
                selected_template = &discovered_templates[i];
                break;
            }
        }

        if (selected_template) {
            // Check if the currently set language is valid for this template
            bool current_is_valid = false;
            for (const auto &flag: selected_template->available_lang_flags) {
                if (flag == temp_settings.lang_flag) {
                    current_is_valid = true;
                    break;
                }
            }

            // If the current language is valid, keep it (fixes reset on startup)
            if (current_is_valid) return;

            bool default_lang_found = false;
            for (const auto &flag: selected_template->available_lang_flags) {
                if (flag.empty()) {
                    // ".empty()" is true for the default "" flag
                    default_lang_found = true;
                    break;
                }
            }

            if (default_lang_found) {
                // Prioritize default language
                temp_settings.lang_flag[0] = '\0';
            } else if (!selected_template->available_lang_flags.empty()) {
                // If no default, pick the first available one
                strncpy(temp_settings.lang_flag, selected_template->available_lang_flags[0].c_str(),
                        sizeof(temp_settings.lang_flag) - 1);
                temp_settings.lang_flag[sizeof(temp_settings.lang_flag) - 1] = '\0';
            } else {
                // No languages found (unlikely), set to default
                temp_settings.lang_flag[0] = '\0';
            }
        } else {
            // No matching template found, reset to default
            temp_settings.lang_flag[0] = '\0';
        }
    };

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

    // Helper Lmbda to auto-fill display category
    auto update_temp_display_category = [&]() {
        // If Display name is locked
        if (temp_settings.lock_category_display_name) return;

        char formatted_category[MAX_PATH_LENGTH];
        format_category_string(temp_settings.category, formatted_category, sizeof(formatted_category));
        // Use provided function

        if (temp_settings.optional_flag[0] != '\0') {
            char formatted_flag[MAX_PATH_LENGTH];
            format_category_string(temp_settings.optional_flag, formatted_flag, sizeof(formatted_flag));
            // Use provided function
            snprintf(temp_settings.category_display_name, sizeof(temp_settings.category_display_name), "%s - %s",
                     formatted_category, formatted_flag);
        } else {
            strncpy(temp_settings.category_display_name, formatted_category,
                    sizeof(temp_settings.category_display_name) - 1);
            temp_settings.category_display_name[sizeof(temp_settings.category_display_name) - 1] = '\0';
        }
    };

    // If the window was just opened (i.e., it was closed last frame but is open now),
    // we copy the current live settings into our temporary editing struct.
    if (just_opened) {
        memcpy(&temp_settings, app_settings, sizeof(AppSettings));
        memcpy(&saved_settings, app_settings, sizeof(AppSettings));
        show_applied_message = false; // Reset message visibility
        show_defaults_applied_message = false; // Reset "Defaults Applied" message visibility
        show_hotkey_warning_message = false;
        show_template_not_found_error = false;
        // Reset co-op tab transient state
        coop_ip_revealed = false;
        coop_public_ip_revealed = false;
        coop_relay_password_host_revealed = false;
        coop_relay_password_recv_revealed = false;
        coop_identity_status_msg[0] = '\0';
        coop_identity_status_is_error = false;
        coop_room_code_error[0] = '\0';
        // Don't clear coop_room_code_buf - it's only valid while hosting
    }

    // Position the settings window to the right half of the viewport when force-opened,
    // so it doesn't overlap the welcome window which ImGui places near the top-left.
    if (just_opened && force_open_reason && *force_open_reason != FORCE_OPEN_NONE) {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.1f), ImGuiCond_Always);
    }

    // Window title
    ImGui::Begin("Advancely Settings", p_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);

    if (roboto_font) {
        ImGui::PushFont(roboto_font);
    }

    // Unsaved changes
    bool has_unsaved_changes = are_settings_different(&temp_settings, &saved_settings);

    // Communicate settings unsaved state to tracker for quit confirmation popup
    if (t) t->settings_has_unsaved_changes = has_unsaved_changes;

    // Revert Changes -> Ctrl+Z / Cmd+Z hotkey logic
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && has_unsaved_changes && !
        ImGui::IsAnyItemActive() &&
        (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_LeftSuper)) &&
        ImGui::IsKeyPressed(ImGuiKey_Z)) {
        memcpy(&temp_settings, &saved_settings, sizeof(AppSettings));
        coop_identity_status_msg[0] = '\0';
        coop_identity_status_is_error = false;
        coop_ip_revealed = false;
        coop_public_ip_revealed = false;
        coop_relay_password_host_revealed = false;
        coop_relay_password_recv_revealed = false;
        coop_room_code_error[0] = '\0';
    }

    // If settings were forced open, display a prominent and context-aware warning message
    if (force_open_reason && *force_open_reason != FORCE_OPEN_NONE) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f)); // Yellow text
        if (*force_open_reason == FORCE_OPEN_AUTO_FAIL) {
            ImGui::TextWrapped("IMPORTANT: Could not find Minecraft saves folder automatically.");
            ImGui::TextWrapped(
                "Please select a different mode or ensure the default Minecraft path exists, then click 'Apply Settings'.");
        } else if (*force_open_reason == FORCE_OPEN_MANUAL_FAIL) {
            ImGui::TextWrapped("IMPORTANT: The manually configured saves path is invalid or does not exist.");
            ImGui::TextWrapped("Please check the path you have entered and click 'Apply Settings'.");
        } else if (*force_open_reason == FORCE_OPEN_ACCOUNT_SETUP) {
            ImGui::TextWrapped("Welcome! Configure your Minecraft account in the Account tab.");
            ImGui::TextWrapped(
                "This lets Advancely read the correct player files by UUID. "
                "Click 'Apply Settings' when done.");
        }
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // --- No saves path warning (suppressed for connected receivers - they get data from host) ---
    {
        bool is_receiver_connected = (temp_settings.network_mode == NETWORK_RECEIVER &&
                                      g_coop_ctx && coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED);
        if (!is_receiver_connected) {
            bool has_valid_saves = t && t->saves_path[0] != '\0' && path_exists(t->saves_path);
            if (has_valid_saves) {
                size_t sp_len = strlen(t->saves_path);
                if (sp_len > 0 && t->saves_path[sp_len - 1] == '/') sp_len--;
                has_valid_saves = (sp_len >= 6 && strncmp(t->saves_path + sp_len - 6, "/saves", 6) == 0);
            }
            if (!has_valid_saves) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
                ImGui::TextWrapped("Warning: No valid Minecraft saves folder is currently being tracked.");
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::Spacing();
            }
        }
    }

    // --- Restart Warning (only applies to UI font size and fonts) ---
    bool font_settings_changed =
            strcmp(temp_settings.tracker_font_name, saved_settings.tracker_font_name) != 0 ||
            // Restart needed for tracker font size because of notes window
            temp_settings.tracker_font_size != saved_settings.tracker_font_size ||
            // temp_settings.tracker_sub_font_size != saved_settings.tracker_sub_font_size ||
            // temp_settings.tracker_ui_font_size != saved_settings.tracker_ui_font_size ||
            strcmp(temp_settings.ui_font_name, saved_settings.ui_font_name) != 0 ||
            temp_settings.ui_font_size != saved_settings.ui_font_size;

    // --- Version-dependent labels ---
    MC_Version selected_version = settings_get_version_from_string(temp_settings.version_str);

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

    // SETTINGS TABS START
    if (ImGui::BeginTabBar("SettingsTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Paths & Templates")) {
            // Path Settings
            ImGui::Text("Path Settings");

            // The (int*) cast is necessary because ImGui::RadioButton works with integers.
            if (ImGui::RadioButton("Auto-Detect Default Saves Path", (int *) &temp_settings.path_mode,
                                   PATH_MODE_AUTO)) {
                // Action to take when this specific button is clicked (optional)
            }
            if (ImGui::IsItemHovered()) {
                char default_saves_path_tooltip_buffer[1024];
                snprintf(default_saves_path_tooltip_buffer, sizeof(default_saves_path_tooltip_buffer),
                         "Automatically finds the default Minecraft (-Launcher) saves path for your OS.\n"
                         "Windows: %%APPDATA%%\\.minecraft\\saves\n"
                         "Linux: ~/.minecraft/saves\n"
                         "macOS: ~/Library/Application Support/minecraft/saves\n"
                         "This is Path Mode: %d", PATH_MODE_AUTO);
                ImGui::SetTooltip("%s", default_saves_path_tooltip_buffer);
            }


            if (ImGui::RadioButton("Auto-Track Active Instance", (int *) &temp_settings.path_mode,
                                   PATH_MODE_INSTANCE)) {
            }
            if (ImGui::IsItemHovered()) {
                char tooltip[512];
                snprintf(tooltip, sizeof(tooltip),
                         "DEFAULT: Automatically detect and track the active Minecraft instance\n"
                         "launched from MultiMC or Prism Launcher, even when switching between\n"
                         "multiple running instances. The tracker always follows the most recently\n"
                         "active world.\n\n"
                         "WARNING:\n"
                         "Without Minecraft running the tracker may not work correctly.\n"
                         "This is Path Mode: %d", PATH_MODE_INSTANCE);
                ImGui::SetTooltip("%s", tooltip);
            }

            if (ImGui::RadioButton("Track Fixed World", (int *) &temp_settings.path_mode, PATH_MODE_FIXED_WORLD)) {
            }
            if (ImGui::IsItemHovered()) {
                char tooltip[512];
                snprintf(tooltip, sizeof(tooltip),
                         "Lock the tracker to one specific world folder.\n"
                         "Unlike other modes, the tracker stays on the chosen world\n"
                         "regardless of which world you open next in Minecraft.\n"
                         "This is Path Mode: %d", PATH_MODE_FIXED_WORLD);
                ImGui::SetTooltip("%s", tooltip);
            }

            if (temp_settings.path_mode == PATH_MODE_FIXED_WORLD) {
                ImGui::Indent();
                ImGui::InputText("##fixed_world_path", temp_settings.fixed_world_path, MAX_PATH_LENGTH);
                ImGui::SameLine();
                if (ImGui::Button("Browse##fixed_world")) {
                    // Use the manual_saves_path as a starting hint if set, otherwise nullptr
                    const char *saves_hint = temp_settings.manual_saves_path[0] != '\0'
                                                 ? temp_settings.manual_saves_path
                                                 : nullptr;
                    char picked[MAX_PATH_LENGTH];
                    if (open_world_folder_dialog(picked, sizeof(picked), saves_hint)) {
                        strncpy(temp_settings.fixed_world_path, picked, MAX_PATH_LENGTH - 1);
                        temp_settings.fixed_world_path[MAX_PATH_LENGTH - 1] = '\0';
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Select the world folder inside your saves directory.\n"
                        "e.g. /home/user/.minecraft/saves/MyWorld");
                }
                if (show_invalid_manual_path_error && temp_settings.path_mode == PATH_MODE_FIXED_WORLD) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    ImGui::TextWrapped("The specified world folder is invalid or does not exist.");
                    ImGui::PopStyleColor();
                }
                ImGui::Unindent();
            }

            if (ImGui::RadioButton("Track Custom Saves Folder", (int *) &temp_settings.path_mode, PATH_MODE_MANUAL)) {
                // Action to take when this specific button is clicked (optional)
            }
            if (ImGui::IsItemHovered()) {
                char tooltip[512];
                snprintf(tooltip, sizeof(tooltip), "Manually specify the path to your '.minecraft/saves' folder.\n"
                         "Useful for custom launchers or non-standard installations.\n"
                         "This is Path Mode: %d", PATH_MODE_MANUAL);
                ImGui::SetTooltip("%s", tooltip);
            }

            // Conditionally show the manual path input only when its radio button is selected
            if (temp_settings.path_mode == PATH_MODE_MANUAL) {
                ImGui::Indent();
                ImGui::InputText("##manual_saves_path", temp_settings.manual_saves_path, MAX_PATH_LENGTH);
                ImGui::SameLine();
                if (ImGui::Button("Browse##saves")) {
                    char picked[MAX_PATH_LENGTH];
                    if (open_saves_folder_dialog(picked, sizeof(picked))) {
                        strncpy(temp_settings.manual_saves_path, picked, MAX_PATH_LENGTH - 1);
                        temp_settings.manual_saves_path[MAX_PATH_LENGTH - 1] = '\0';
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Select the path to your '.minecraft/saves' folder.\n"
                        "You can also paste the path directly into the text field.");
                }
                if (show_invalid_manual_path_error) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    ImGui::TextWrapped(
                        "The specified path is invalid or does not exist.\n"
                        "Please provide a valid path to your '.minecraft/saves' folder.\n");
                    ImGui::PopStyleColor();
                }
                ImGui::Unindent();
            }

            // Open Instances Folder Button
            bool is_saves_path_valid = t->saves_path[0] != '\0' && path_exists(t->saves_path);

            // If the path is not valid, begin a disabled state for the button.
            if (!is_saves_path_valid) {
                ImGui::BeginDisabled();
            }

            if (ImGui::Button("Open Instances Folder")) {
                char instances_path[MAX_PATH_LENGTH];

                if (get_parent_directory(t->saves_path, instances_path, sizeof(instances_path), 3)) {
#ifdef _WIN32
                    path_to_windows_native(instances_path);
#endif
                    open_content(instances_path); // Clean replacement
                }
            }

            // If the button was disabled, end the disabled state.
            if (!is_saves_path_valid) {
                ImGui::EndDisabled();
                // Add a tooltip that only appears when hovering over the disabled button.
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    char open_instance_folder_tooltip_buffer[1024];
                    snprintf(open_instance_folder_tooltip_buffer, sizeof(open_instance_folder_tooltip_buffer),
                             "A valid saves path must be active to use this feature.\nPlease apply a correct path first.");
                    ImGui::SetTooltip(
                        "%s", open_instance_folder_tooltip_buffer);
                }
            } else {
                // This is the original tooltip for when the button is enabled.
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
            // Template selection is locked for both host and receivers during an active lobby
            CoopNetState tpl_net_state = g_coop_ctx ? coop_net_get_state(g_coop_ctx) : COOP_NET_IDLE;
            bool coop_template_locked = g_coop_ctx &&
                                        ((temp_settings.network_mode == NETWORK_RECEIVER && tpl_net_state ==
                                          COOP_NET_CONNECTED) ||
                                         (temp_settings.network_mode == NETWORK_HOST && tpl_net_state ==
                                          COOP_NET_LISTENING));
            const char *coop_template_locked_tooltip = (temp_settings.network_mode == NETWORK_HOST)
                                                           ? "Template settings are locked while a lobby is active"
                                                           : "Controlled by Host";
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
            ImGui::Text("(Official Templates)");
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered()) {
                char open_official_templates_tooltip_buffer[1024];
                snprintf(open_official_templates_tooltip_buffer, sizeof(open_official_templates_tooltip_buffer),
                         "Opens a table of officially added templates in your browser.\n"
                         "These templates/languages get replaced through auto-updates.");
                ImGui::SetTooltip("%s", open_official_templates_tooltip_buffer);
            }

            if (ImGui::IsItemClicked()) {
                open_content("https://github.com/LNXSeus/Advancely#Officially-Added-Templates");
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f)); // Use a link-like color
            ImGui::Text("(Version Support)");
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered()) {
                char open_official_templates_tooltip_buffer[1024];
                snprintf(open_official_templates_tooltip_buffer, sizeof(open_official_templates_tooltip_buffer),
                         "Opens the version support page in your browser.\n"
                         "This page shows which versions are functionally equal.\n"
                         "for Advancely.");
                ImGui::SetTooltip("%s", open_official_templates_tooltip_buffer);
            }

            if (ImGui::IsItemClicked()) {
                open_content("https://github.com/LNXSeus/Advancely#extensive-version-support");
            }

            int current_template_version_idx = -1;
            for (int i = 0; i < VERSION_STRINGS_COUNT; i++) {
                if (strcmp(VERSION_STRINGS[i], temp_settings.version_str) == 0) {
                    current_template_version_idx = i;
                    break;
                }
            }
            if (coop_template_locked) ImGui::BeginDisabled();
            if (ImGui::Combo("Template Version", &current_template_version_idx, version_display_c_strs.data(),
                             version_display_c_strs.size())) {
                if (current_template_version_idx >= 0) {
                    strncpy(temp_settings.version_str, VERSION_STRINGS[current_template_version_idx],
                            sizeof(temp_settings.version_str) - 1);
                    temp_settings.version_str[sizeof(temp_settings.version_str) - 1] = '\0';

                    // Always update the display version to match the template version for convenience
                    strncpy(temp_settings.display_version_str, temp_settings.version_str,
                            sizeof(temp_settings.display_version_str) - 1);
                    temp_settings.display_version_str[sizeof(temp_settings.display_version_str) - 1] = '\0';

                    // This logic will be handled by the rescan block below, but we call it
                    // here to make the UI feel responsive *before* the rescan happens.
                    update_temp_display_category();
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                char version_tooltip_buffer[1024];
                if (coop_template_locked) {
                    snprintf(version_tooltip_buffer, sizeof(version_tooltip_buffer),
                             "%s", coop_template_locked_tooltip);
                } else {
                    snprintf(version_tooltip_buffer, sizeof(version_tooltip_buffer),
                             "Select the functional version of the template.\n"
                             "This determines which template file to load and how to parse game data.\n"
                             "The number in brackets shows how many templates are available for that version.\n"
                             "This doesn't necessarily have to be the exact version of your minecraft instance.\n"
                             "(E.g., Playing 1.21.6 (Template Version) all_advancements in 1.21.10 (Display Version).)\n"
                             "This way templates don't need to be copied for each subversion.\n"
                             "Click on '(Version Support)' to see the version ranges that functionally equal.");
                }
                ImGui::SetTooltip("%s", version_tooltip_buffer);
            }
            if (coop_template_locked) ImGui::EndDisabled();

            // "Display Version" dropdown
            int current_display_version_idx = -1;
            for (int i = 0; i < VERSION_STRINGS_COUNT; i++) {
                if (strcmp(VERSION_STRINGS[i], temp_settings.display_version_str) == 0) {
                    current_display_version_idx = i;
                    break;
                }
            }
            // Use the *non-count* version strings for the display version dropdown
            if (ImGui::Combo("Display Version", &current_display_version_idx, VERSION_STRINGS, VERSION_STRINGS_COUNT)) {
                if (current_display_version_idx >= 0) {
                    strncpy(temp_settings.display_version_str, VERSION_STRINGS[current_display_version_idx],
                            sizeof(temp_settings.display_version_str) - 1);
                    temp_settings.display_version_str[sizeof(temp_settings.display_version_str) - 1] = '\0';
                }
            }
            if (ImGui::IsItemHovered()) {
                char display_version_tooltip_buffer[1024];
                snprintf(display_version_tooltip_buffer, sizeof(display_version_tooltip_buffer),
                         "Select the version to show on the tracker info bar and overlay.\n"
                         "This is purely for display and does not affect which template is loaded.\n"
                         "(E.g., You select the 1.21.6 (Template Version) all_advancements template,\n"
                         "but play on 1.21.10 (Display Version) that has the same advancements.)\n"
                         "So no need to copy the same template for each subversion.\n"
                         "By default, this matches the 'Template Version'.");
                ImGui::SetTooltip("%s", display_version_tooltip_buffer);
            }

            // Coop state check - shared by StatsPerWorld and Hermes checkboxes
            CoopNetState hermes_net_state = g_coop_ctx ? coop_net_get_state(g_coop_ctx) : COOP_NET_IDLE;
            bool hermes_net_active = (hermes_net_state == COOP_NET_LISTENING ||
                                      hermes_net_state == COOP_NET_CONNECTED ||
                                      hermes_net_state == COOP_NET_CONNECTING);

            // Only show the StatsPerWorld checkbox for legacy versions.
            // Disable for receivers (they always fall back to global stats and sync
            // to the host regardless), and also for hosts once a lobby is active
            // so the setting can't change mid-session.
            if (selected_version <= MC_VERSION_1_6_4) {
                const bool spw_is_receiver = (hermes_net_state == COOP_NET_CONNECTED);
                const bool spw_host_locked = !spw_is_receiver && hermes_net_active;
                const bool spw_disabled = spw_is_receiver || spw_host_locked;
                if (spw_disabled) ImGui::BeginDisabled();
                ImGui::Checkbox("Using StatsPerWorld Mod", &temp_settings.using_stats_per_world_legacy);
                if (spw_disabled) ImGui::EndDisabled();
                if (ImGui::IsItemHovered(spw_disabled ? ImGuiHoveredFlags_AllowWhenDisabled : 0)) {
                    char stats_per_world_tooltip_buffer[1024];
                    if (spw_is_receiver) {
                        snprintf(stats_per_world_tooltip_buffer, sizeof(stats_per_world_tooltip_buffer),
                                 "Receivers always fall back to global stats even with the mod active\n"
                                 "(no world is created on the receiver side), and they sync their stats\n"
                                 "up to the host rather than syncing from the host's stats.\n"
                                 "This setting only applies when running as host or in singleplayer.");
                    } else if (spw_host_locked) {
                        snprintf(stats_per_world_tooltip_buffer, sizeof(stats_per_world_tooltip_buffer),
                                 "Cannot change while a lobby is active.\n"
                                 "Stop the lobby first if you need to toggle this.");
                    } else {
                        snprintf(stats_per_world_tooltip_buffer, sizeof(stats_per_world_tooltip_buffer),
                                 "The StatsPerWorld Mod (with Legacy Fabric) allows legacy Minecraft versions\n"
                                 "to track stats locally per world. Check this if you're using this mod.\n\n"
                                 "If unchecked, the tracker will use a snapshot system to simulate per-world\n"
                                 "progress, and achievements will indicate if they were completed on a previous world.");
                    }
                    ImGui::SetTooltip("%s", stats_per_world_tooltip_buffer);
                }
            }

            // Hermes Mod checkbox - available for all versions that support Fabric
            // Disable when lobby is active (host or receiver)
            if (hermes_net_active) ImGui::BeginDisabled();
            ImGui::Checkbox("Using Hermes Mod (Live Tracking)", &temp_settings.using_hermes);
            if (hermes_net_active) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(hermes_net_active ? ImGuiHoveredFlags_AllowWhenDisabled : 0)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 38.0f);
                ImGui::TextUnformatted("Hermes Mod (by DuncanRuns, for Fabric)");
                ImGui::Separator();
                if (selected_version <= MC_VERSION_1_6_4) {
                    // Whenever the mod gets released for 1.6.4 or lower
                    ImGui::TextWrapped(
                        "Hermes is a speedrun-legal Legacy Fabric mod that writes real-time game events to a "
                        "ciphered log file inside each world's folder. When enabled, Advancely reads this "
                        "log in addition to the normal game files, giving you near-instant updates "
                        "instead of waiting for the game to save.\n\n"
                        "How the two sources are combined:\n");
                } else {
                    // Whenever the mod gets released for mid-era and other versions after 1.6.4
                    ImGui::TextWrapped(
                        "Hermes is a speedrun-legal Fabric mod that writes real-time game events to a "
                        "ciphered log file inside each world's folder. When enabled, Advancely reads this "
                        "log in addition to the normal game files, giving you near-instant updates "
                        "instead of waiting for the game to save.\n\n"
                        "How the two sources are combined:\n");
                }
                // Achievements/Advancements
                if (selected_version <= MC_VERSION_1_11_2) {
                    ImGui::BulletText(
                        "Achievements: Hermes only provides gained achievements, so to ensure\n"
                        "  accuracy, Advancely will read the actual stats file and synchronize\n"
                        "  when the game actually saves.");
                } else {
                    // modern versions
                    ImGui::BulletText(
                        "Advancements: Hermes only provides gained advancements/criteria, so to ensure\n"
                        "  accuracy, Advancely will read the actual advancements file and synchronize\n"
                        "  when the game actually saves.");
                }
                // Stats, version neutral
                ImGui::BulletText(
                    "Stats: Hermes provides real-time values for the stats it tracks. Stats that\n"
                    "  Hermes intentionally omits (high-frequency ones like distance walked) are\n"
                    "  still read from the regular game files as usual. Stats are also synchronized\n"
                    "  when the game actually saves.");
                ImGui::Spacing();
                ImGui::TextDisabled("Requires Hermes to be installed and a world to be loaded.");
                if (hermes_net_active) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
                                       "Cannot change while a lobby is active.");
                }
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

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
                    unique_category_values.erase(
                        std::unique(unique_category_values.begin(), unique_category_values.end()),
                        unique_category_values.end());
                }

                // --- After scan, validate and reset current selection if it's no longer valid for the new version ---

                // Step 1: Validate the category.
                bool category_is_valid = false;
                for (const auto &cat: unique_category_values) {
                    if (cat == temp_settings.category) {
                        category_is_valid = true;
                        break;
                    }
                }
                if (!category_is_valid) {
                    // If the old category doesn't exist for this version, pick the first one.
                    if (!unique_category_values.empty()) {
                        strncpy(temp_settings.category, unique_category_values[0].c_str(),
                                sizeof(temp_settings.category) - 1);
                        temp_settings.category[sizeof(temp_settings.category) - 1] = '\0';
                    } else {
                        temp_settings.category[0] = '\0';
                    }
                    // Since the category is being reset, the flags must also be reset.
                    temp_settings.optional_flag[0] = '\0';
                    temp_settings.lang_flag[0] = '\0';
                }

                // Step 2: Validate the optional flag for the (now guaranteed to be valid) category.
                // This runs whether the category was reset or was already valid.
                bool flag_is_valid = false;
                if (temp_settings.category[0] != '\0') {
                    for (int i = 0; i < discovered_template_count; ++i) {
                        if (strcmp(discovered_templates[i].category, temp_settings.category) == 0 &&
                            strcmp(discovered_templates[i].optional_flag, temp_settings.optional_flag) == 0) {
                            flag_is_valid = true;
                            break;
                        }
                    }
                }

                if (!flag_is_valid) {
                    // The current flag is invalid for this version/category pair.
                    // Find and set the first available flag for the current category.
                    bool flag_set = false;
                    for (int i = 0; i < discovered_template_count; ++i) {
                        if (strcmp(discovered_templates[i].category, temp_settings.category) == 0) {
                            strncpy(temp_settings.optional_flag, discovered_templates[i].optional_flag,
                                    sizeof(temp_settings.optional_flag) - 1);
                            temp_settings.optional_flag[sizeof(temp_settings.optional_flag) - 1] = '\0';
                            flag_set = true;
                            break; // Found the first one, we're done.
                        }
                    }
                    if (!flag_set) {
                        // This case should not happen if the category is valid, but as a fallback:
                        temp_settings.optional_flag[0] = '\0';
                    }
                    // Since the flag was reset, the language must also be reset.
                    temp_settings.lang_flag[0] = '\0';
                }

                // Reformat display name AFTER validation/resets
                update_temp_display_category();

                // Auto-select language based on new defaults
                auto_select_language();
            }


            // --- CATEGORY DROPDOWN ---
            category_display_names.clear();
            category_display_strings.clear();
            for (const auto &cat: unique_category_values) {
                // Show "(has layout)" on the category when the no-flag template has layout data
                bool show_layout_on_category = false;
                for (int i = 0; i < discovered_template_count; ++i) {
                    if (strcmp(discovered_templates[i].category, cat.c_str()) == 0 &&
                        discovered_templates[i].optional_flag[0] == '\0' &&
                        discovered_templates[i].has_layout) {
                        show_layout_on_category = true;
                        break;
                    }
                }
                if (show_layout_on_category) {
                    category_display_strings.push_back(cat + " (has layout)");
                } else {
                    category_display_strings.push_back(cat);
                }
            }
            for (const auto &s: category_display_strings) {
                category_display_names.push_back(s.c_str());
            }

            int category_idx = -1;
            for (size_t i = 0; i < unique_category_values.size(); ++i) {
                if (strcmp(unique_category_values[i].c_str(), temp_settings.category) == 0) {
                    category_idx = (int) i;
                    break;
                }
            }

            if (coop_template_locked) ImGui::BeginDisabled();
            if (ImGui::Combo("Category", &category_idx, category_display_names.data(), category_display_names.size())) {
                if (category_idx >= 0 && (size_t) category_idx < unique_category_values.size()) {
                    // Use the raw category value, not the display name with "(has layout)"
                    strncpy(temp_settings.category, unique_category_values[category_idx].c_str(),
                            sizeof(temp_settings.category) - 1);
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

                    update_temp_display_category(); // Update Display Category Name (in settings)

                    // Auto-select language based on new category/flag
                    auto_select_language();
                }
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                char category_tooltip_buffer[1024];
                if (coop_template_locked) {
                    snprintf(category_tooltip_buffer, sizeof(category_tooltip_buffer),
                             "%s", coop_template_locked_tooltip);
                } else {
                    snprintf(category_tooltip_buffer, sizeof(category_tooltip_buffer),
                             "Choose between available categories for the selected version.\n"
                             "If the category you're looking for isn't available you can create it\n"
                             "by clicking the 'Edit Templates' button or view the list of officially added\n"
                             "templates by clicking the '(Learn more)' button next to the 'Template Settings'.\n\n"
                             "Templates marked with '(has layout)' include pre-defined positions for goals.\n"
                             "Enable the 'Manual Layout' checkbox to use them.");
                }
                ImGui::SetTooltip("%s", category_tooltip_buffer);
            }
            if (coop_template_locked) ImGui::EndDisabled();


            // --- OPTIONAL FLAG DROPDOWN ---
            flag_values.clear();
            flag_display_names.clear();
            flag_display_strings.clear();

            if (temp_settings.category[0] != '\0') {
                for (int i = 0; i < discovered_template_count; ++i) {
                    if (strcmp(discovered_templates[i].category, temp_settings.category) == 0) {
                        const char *flag = discovered_templates[i].optional_flag;
                        flag_values.push_back(flag);
                        // Show "(has layout)" on flag entries that have layout data
                        std::string display;
                        if (flag[0] == '\0') {
                            display = "None";
                        } else {
                            display = flag;
                        }
                        if (flag[0] != '\0' && discovered_templates[i].has_layout) {
                            display += " (has layout)";
                        }
                        flag_display_strings.push_back(display);
                    }
                }
            }
            for (const auto &s: flag_display_strings) {
                flag_display_names.push_back(s.c_str());
            }

            int flag_idx = -1;
            for (size_t i = 0; i < flag_values.size(); ++i) {
                if (strcmp(flag_values[i].c_str(), temp_settings.optional_flag) == 0) {
                    flag_idx = (int) i;
                    break;
                }
            }

            if (coop_template_locked) ImGui::BeginDisabled();
            if (ImGui::Combo("Optional Flag", &flag_idx, flag_display_names.data(), flag_display_names.size())) {
                if (flag_idx >= 0 && (size_t) flag_idx < flag_values.size()) {
                    // Use the raw flag value, not the display name with "(has layout)"
                    strncpy(temp_settings.optional_flag, flag_values[flag_idx].c_str(),
                            sizeof(temp_settings.optional_flag) - 1);
                    temp_settings.optional_flag[sizeof(temp_settings.optional_flag) - 1] = '\0';

                    update_temp_display_category(); // Update Display Category Name (in settings)

                    // Auto-select language based on new flag
                    auto_select_language();
                }
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                char flag_tooltip_buffer[1024];
                if (coop_template_locked) {
                    snprintf(flag_tooltip_buffer, sizeof(flag_tooltip_buffer),
                             "%s", coop_template_locked_tooltip);
                } else {
                    snprintf(flag_tooltip_buffer, sizeof(flag_tooltip_buffer),
                             "Choose between available optional flags for the selected version and category.\n"
                             "The optional flag is used to differentiate between different alterations of the same template.\n\n"
                             "Templates marked with '(has layout)' include pre-defined positions for goals.\n"
                             "Enable the 'Manual Layout' checkbox to use them.");
                }
                ImGui::SetTooltip("%s", flag_tooltip_buffer);
            }
            if (coop_template_locked) ImGui::EndDisabled();

            // --- Category Display Name Text Input ---

            // Calculate the standard width used by other items (like the Combos above)
            float standard_width = ImGui::CalcItemWidth();
            // float spacing = ImGui::GetStyle().ItemSpacing.x;

            // Calculate the width of the "Lock" checkbox so we can subtract it
            // Checkbox Width = Box Square (FrameHeight) + Text Gap (ItemInnerSpacing) + Text Width ("Lock")
            // float lock_checkbox_width = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Lock").x;

            // Set the input field width to fill the remaining space
            ImGui::SetNextItemWidth(standard_width);

            // Disable input if locked
            if (temp_settings.lock_category_display_name) ImGui::BeginDisabled();


            ImGui::InputText("Display Category", temp_settings.category_display_name,
                             sizeof(temp_settings.category_display_name));

            if (temp_settings.lock_category_display_name) ImGui::EndDisabled(); // End disabled if locked


            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                char tooltip_buffer[512];
                if (temp_settings.lock_category_display_name) {
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Display Name is currently locked.\n"
                             "Uncheck the box to edit or auto-update.");
                } else {
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "This is the name used for display on the tracker, overlay, and in debug logs.\n"
                             "It is automatically formatted from the Category and Optional Flag,\n"
                             "but you can override it with any custom text here.");
                }
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            // The Lock Checkbox
            ImGui::SameLine();
            // The Lock Checkbox
            ImGui::Checkbox("Lock", &temp_settings.lock_category_display_name);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Prevent the Display Name from changing automatically when switching templates.");
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

                    if (ImGui::Combo("Language", &lang_idx, lang_display_names.data(),
                                     (int) lang_display_names.size())) {
                        if (lang_idx >= 0 && (size_t) lang_idx < selected_template->available_lang_flags.size()) {
                            const std::string &selected_flag_str = selected_template->available_lang_flags[lang_idx];
                            strncpy(temp_settings.lang_flag, selected_flag_str.c_str(),
                                    sizeof(temp_settings.lang_flag) - 1);
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
#ifdef _WIN32
                path_to_windows_native(templates_path);
#endif
                open_content(templates_path); // Clean replacement
            }
            if (ImGui::IsItemHovered()) {
                char open_templates_folder_tooltip_buffer[1024];
                snprintf(open_templates_folder_tooltip_buffer, sizeof(open_templates_folder_tooltip_buffer),
                         "Opens the 'resources/templates' folder in your file explorer.");
                ImGui::SetTooltip("%s", open_templates_folder_tooltip_buffer);
            }

            // Place Template Creator Button in same line
            ImGui::SameLine();

            bool coop_session_active = g_coop_ctx &&
                                       (coop_net_get_state(g_coop_ctx) == COOP_NET_LISTENING ||
                                        coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED ||
                                        coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTING);
            if (coop_session_active) ImGui::BeginDisabled();
            if (ImGui::Button("Edit Templates")) {
                *p_temp_creator_open = true; // Open the template creator window
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                char open_template_creator_tooltip_buffer[1024];
                if (coop_session_active) {
                    snprintf(open_template_creator_tooltip_buffer, sizeof(open_template_creator_tooltip_buffer),
                             "Template editing is disabled during a Co-op session");
                } else {
                    snprintf(open_template_creator_tooltip_buffer, sizeof(open_template_creator_tooltip_buffer),
                             "Open the Template Editor to modify or build a new template or language.");
                }
                ImGui::SetTooltip("%s", open_template_creator_tooltip_buffer);
            }
            if (coop_session_active) ImGui::EndDisabled();


            ImGui::EndTabItem();
        } // End of Paths and Templates Tab

        if (ImGui::BeginTabItem("Tracker Visuals")) {
            ImGui::Text("Window & Behavior");

            ImGui::Checkbox("Always On Top", &temp_settings.tracker_always_on_top);
            if (ImGui::IsItemHovered()) {
                char always_on_top_tooltip_buffer[1024];
                snprintf(always_on_top_tooltip_buffer, sizeof(always_on_top_tooltip_buffer),
                         "Forces the tracker window to always display above any other window.");
                ImGui::SetTooltip("%s", always_on_top_tooltip_buffer);
            }

            // This toggles the framerate of everything
            if (ImGui::DragFloat("Tracker FPS Limit", &temp_settings.fps, 1.0f, 10.0f, 540.0f, "%.0f")) {
                if (temp_settings.fps < 10.0f) temp_settings.fps = 10.0f;
                if (temp_settings.fps > 540.0f) temp_settings.fps = 540.0f;
            }
            if (ImGui::IsItemHovered()) {
                char tracker_fps_limit_tooltip_buffer[1024];
                snprintf(tracker_fps_limit_tooltip_buffer, sizeof(tracker_fps_limit_tooltip_buffer),
                         "Limits the frames per second of the tracker window. Default is 60 FPS.\n"
                         "Higher values may result in higher CPU usage.");
                ImGui::SetTooltip("%s", tracker_fps_limit_tooltip_buffer);
            }

            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Goal Visibility");

            ImGui::RadioButton("Hide All Completed", (int *) &temp_settings.goal_hiding_mode, HIDE_ALL_COMPLETED);
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[1024];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Strictest hiding. Hides goals when they are completed AND hides goals marked as \"Hidden\" in the template.\n"
                         "Section counters will only display the total number of remaining (visible) items, e.g., (5 - 12) or (5).\n\n"
                         "Automatic layout & overlay: The \"Hidden\" checkbox in the template editor controls visibility.\n"
                         "Manual layout: The per-position \"Hide\" checkboxes control visibility instead.\n"
                         "Completed goals fully disappear from the manual layout in this mode.");
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            ImGui::SameLine();
            ImGui::RadioButton("Hide Template-Hidden Only", (int *) &temp_settings.goal_hiding_mode,
                               HIDE_ONLY_TEMPLATE_HIDDEN);
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[1024];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Hides goals marked as \"Hidden\" in the template, but keeps all other completed goals visible.\n"
                         "Section counters will count all items NOT marked as hidden in the template,\n"
                         "regardless of completion e.g., (5/10 - 12/20) or (5/10).\n\n"
                         "Automatic layout & overlay: The \"Hidden\" checkbox in the template editor controls visibility.\n"
                         "Manual layout: The per-position \"Hide\" checkboxes control visibility instead.\n"
                         "Completed goals are greyed out but remain visible in the manual layout in this mode.");
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            ImGui::SameLine();
            ImGui::RadioButton("Show All", (int *) &temp_settings.goal_hiding_mode, SHOW_ALL);
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[1024];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Shows everything. No goals will be hidden, regardless of their completion or template status.\n"
                         "Section counters will count every single item defined in the template\n"
                         "for that section e.g., (5/10 - 12/20) or (5/10).\n\n"
                         "Both \"Hidden\" checkboxes and per-position \"Hide\" checkboxes are ignored in this mode.");
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Layout & Spacing");

            // --- Section Order ---
            ImGui::SeparatorText("Section Order");
            if (ImGui::IsItemHovered()) {
                char section_order_tooltip_buffer[256];
                snprintf(section_order_tooltip_buffer, sizeof(section_order_tooltip_buffer),
                         "Drag and drop to reorder the sections in the main tracker window.\n"
                         "This doesn't affect the 'Manual Layout'.\n"
                         "Drop items between others to insert them at that position.");
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

                if (section_id == SECTION_RECIPES && selected_version < MC_VERSION_1_12) {
                    is_visible = false; // Hide "Recipes" if the version is pre-1.12
                }

                if (is_visible) {
                    visible_section_indices.push_back(i);
                }
            }

            // State variables for the reorder operation
            int section_dnd_source_vis_index = -1;
            int section_dnd_target_vis_index = -1;

            for (size_t n = 0; n < visible_section_indices.size(); n++) {
                int original_array_index = visible_section_indices[n];
                int item_type_id = temp_settings.section_order[original_array_index];

                // Determine display name (same as before)
                const char *item_name;
                if (item_type_id == SECTION_ADVANCEMENTS) {
                    item_name = (selected_version <= MC_VERSION_1_11_2) ? "Achievements" : "Advancements";
                } else {
                    item_name = TRACKER_SECTION_NAMES[item_type_id];
                }

                ImGui::PushID(n);

                // Top Drop Zone, using small invis button as a gap
                ImGui::InvisibleButton("drop_target_top", ImVec2(-1, 4.0f));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_SECTION_ORDER")) {
                        section_dnd_source_vis_index = *(const int *) payload->Data;
                        section_dnd_target_vis_index = (int) n;
                    }
                    ImGui::EndDragDropTarget();
                }

                // Selectable Item (Drag Source)
                ImGui::Selectable(item_name);

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("DND_SECTION_ORDER", &n, sizeof(int));
                    ImGui::Text("Reorder %s", item_name);
                    ImGui::EndDragDropSource();
                }

                ImGui::PopID();
            }

            // Final Drop Zone (Bottom of the list)
            ImGui::InvisibleButton("drop_target_bottom", ImVec2(-1, 4.0f)); // Same-sized target at the end
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_SECTION_ORDER")) {
                    section_dnd_source_vis_index = *(const int *) payload->Data;
                    section_dnd_target_vis_index = (int) visible_section_indices.size();
                }
                ImGui::EndDragDropTarget();
            }

            // --- Perform Reorder Logic ---
            if (section_dnd_source_vis_index != -1 && section_dnd_target_vis_index != -1 && section_dnd_source_vis_index
                !=
                section_dnd_target_vis_index) {
                // Convert visible indices back to the full 'setion_order' array manipulation
                // We need to perform the move on the underlying array, but respecting the order of visible items

                // Extract the full list of actual Section IDs in their current order
                std::vector<int> current_order_list;
                for (int i = 0; i < SECTION_COUNT; ++i) current_order_list.push_back(temp_settings.section_order[i]);

                // Identify the item ID moving and the item ID it is moving before
                // The visible list tells us the current sequence.
                int moving_item_id = temp_settings.section_order[visible_section_indices[section_dnd_source_vis_index]];

                // Find where this item currently is in the full list
                auto source_it = std::find(current_order_list.begin(), current_order_list.end(), moving_item_id);

                // Remove it from the full list temporarily
                if (source_it != current_order_list.end()) {
                    current_order_list.erase(source_it);
                }

                // If target is end of visible list, append after the last visible item
                // Otherwise, insert before the target visible item
                std::vector<int>::iterator insert_pos;

                if (section_dnd_target_vis_index >= (int) visible_section_indices.size()) {
                    // Insert after the last visible item found in the full list
                    int last_visible_id = temp_settings.section_order[visible_section_indices.back()];
                    auto last_it = std::find(current_order_list.begin(), current_order_list.end(), last_visible_id);
                    insert_pos = (last_it == current_order_list.end()) ? current_order_list.end() : last_it + 1;
                } else {
                    // Insert before the specific target item
                    int target_item_id = temp_settings.section_order[visible_section_indices[
                        section_dnd_target_vis_index]];
                    insert_pos = std::find(current_order_list.begin(), current_order_list.end(), target_item_id);
                }

                // Re-insert and update settings
                current_order_list.insert(insert_pos, moving_item_id);

                for (int i = 0; i < SECTION_COUNT; ++i) {
                    temp_settings.section_order[i] = current_order_list[i];
                }
            }

            ImGui::SeparatorText("Vertical Spacing");

            // --- Tracker Vertical Spacing ---
            if (ImGui::DragFloat("Tracker Vertical Spacing", &temp_settings.tracker_vertical_spacing, 1.0f, 0.0f,
                                 1024.0f,
                                 "%.0f px")) {
                if (temp_settings.tracker_vertical_spacing < 0.0f) temp_settings.tracker_vertical_spacing = 0.0f;
                if (temp_settings.tracker_vertical_spacing > 1024.0f) temp_settings.tracker_vertical_spacing = 1024.0f;
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[256];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Adjusts the vertical gap (in pixels) between rows of items in the tracker window\n"
                         "for all sections. Default: %.1f px",
                         DEFAULT_TRACKER_VERTICAL_SPACING);
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            // --- Custom Tracker Section Width ---
            ImGui::SeparatorText("Custom Section Item Width");
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[512];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Enable and adjust the horizontal width (in pixels) for *each item* within a section.\n"
                         "This overrides the dynamic width calculation. WARNING: Small values will cause text to overlap.\n"
                         "Sections not available in the selected template version will be hidden.");
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            ImGui::Indent();

            // We use the already-calculated selected_version and its labels
            for (int i = 0; i < SECTION_COUNT; ++i) {
                TrackerSection section_id = (TrackerSection) i;
                bool is_visible = true;
                const char *label = TRACKER_SECTION_NAMES[i];
                char checkbox_label[128];

                if (section_id == SECTION_ADVANCEMENTS) {
                    label = advancements_label_plural_uppercase;
                } else if (section_id == SECTION_RECIPES && selected_version < MC_VERSION_1_12) {
                    is_visible = false; // Hide Recipes for legacy/mid
                } else if (section_id == SECTION_UNLOCKS && selected_version != MC_VERSION_25W14CRAFTMINE) {
                    is_visible = false; // Hide Unlocks for non-Craftmine
                }

                if (is_visible) {
                    snprintf(checkbox_label, sizeof(checkbox_label), "%s Width", label);
                    ImGui::Checkbox(checkbox_label, &temp_settings.tracker_section_custom_width_enabled[i]);
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[512];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Check this to override the dynamic width calculation for items in the '%s' section.\n"
                                 "This allows you to set a fixed, uniform total width for all items in this row.",
                                 label);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }

                    if (temp_settings.tracker_section_custom_width_enabled[i]) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(150.0f); // Give the slider a fixed width
                        char slider_label[128];
                        snprintf(slider_label, sizeof(slider_label), "##%sWidthSlider", label);
                        if (ImGui::DragFloat(slider_label, &temp_settings.tracker_section_custom_item_width[i], 1.0f,
                                             96.0f,
                                             2048.0f, "%.0f px")) {
                            if (temp_settings.tracker_section_custom_item_width[i] < 96.0f)
                                temp_settings.tracker_section_custom_item_width[i] = 96.0f;
                            if (temp_settings.tracker_section_custom_item_width[i] > 2048.0f)
                                temp_settings.tracker_section_custom_item_width[i] = 2048.0f;
                        }
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[512];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "Item width for %s. WARNING: Text may overlap if too small.\n"
                                     "The item icon is %dpx wide. Default: %.0fpx",
                                     label, 96, DEFAULT_TRACKER_SECTION_ITEM_WIDTH);
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }
                    }
                }
            }
            ImGui::Unindent();


            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Level of Detail & Lists");

            // --- Level of Detail Settings ---

            if (ImGui::DragFloat("Hide Sub-Item Text At", &temp_settings.lod_text_sub_threshold, 0.001f, 0.05f, 10.0f,
                                 "%.3f")) {
                if (temp_settings.lod_text_sub_threshold < 0.05f) temp_settings.lod_text_sub_threshold = 0.05f;
                if (temp_settings.lod_text_sub_threshold > 10.0f) temp_settings.lod_text_sub_threshold = 10.0f;
            }
            if (ImGui::IsItemHovered()) {
                char lod_sub_tooltip[1024];
                snprintf(lod_sub_tooltip, sizeof(lod_sub_tooltip),
                         "The zoom threshold below which sub-item text is hidden.\n"
                         "Higher values are more zoomed in.\n"
                         "Affects:\n"
                         " - Names of Criteria, Sub-Stats, and Stages.\n"
                         " - Progress Text like '(5/10)'.\n"
                         "Default: %.3f", DEFAULT_LOD_TEXT_SUB_THRESHOLD);
                ImGui::SetTooltip("%s", lod_sub_tooltip);
            }

            if (ImGui::DragFloat("Hide Main Text/Checkbox At", &temp_settings.lod_text_main_threshold, 0.001f, 0.05f,
                                 10.0f,
                                 "%.3f")) {
                if (temp_settings.lod_text_main_threshold < 0.05f) temp_settings.lod_text_main_threshold = 0.05f;
                if (temp_settings.lod_text_main_threshold > 10.0f) temp_settings.lod_text_main_threshold = 10.0f;
            }
            if (ImGui::IsItemHovered()) {
                char lod_main_tooltip[1024];
                snprintf(lod_main_tooltip, sizeof(lod_main_tooltip),
                         "The zoom threshold below which main item text and interactive elements are hidden.\n"
                         "Higher values are more zoomed in.\n"
                         "Affects:\n"
                         " - Main Category Names (e.g., 'Monster Hunter').\n"
                         " - Checkboxes for manual completion (Parent and Sub-Stat checkboxes).\n"
                         "Default: %.3f", DEFAULT_LOD_TEXT_MAIN_THRESHOLD);
                ImGui::SetTooltip("%s", lod_main_tooltip);
            }

            if (ImGui::DragFloat("Simplify Icons At", &temp_settings.lod_icon_detail_threshold, 0.001f, 0.05f, 10.0f,
                                 "%.3f")) {
                if (temp_settings.lod_icon_detail_threshold < 0.05f) temp_settings.lod_icon_detail_threshold = 0.05f;
                if (temp_settings.lod_icon_detail_threshold > 10.0f) temp_settings.lod_icon_detail_threshold = 10.0f;
            }
            if (ImGui::IsItemHovered()) {
                char lod_icon_tooltip[1024];
                snprintf(lod_icon_tooltip, sizeof(lod_icon_tooltip),
                         "The zoom threshold below which sub-item icons are simplified.\n"
                         "Higher values are more zoomed in.\n"
                         "Affects:\n"
                         " - Criteria and Sub-Stat icons turn into simple colored squares.\n"
                         " - The squares use your chosen Text Color with low opacity to indicate presence.\n"
                         " - The scroll bar on the side of scrolling lists.\n"
                         "Default: %.3f", DEFAULT_LOD_ICON_DETAIL_THRESHOLD);
                ImGui::SetTooltip("%s", lod_icon_tooltip);
            }

            // Slider for Scroll Threshold
            if (ImGui::DragInt("Scrollable List Threshold", &temp_settings.scrollable_list_threshold, 1.0f, 1, 2048)) {
                if (temp_settings.scrollable_list_threshold < 1) temp_settings.scrollable_list_threshold = 1;
                if (temp_settings.scrollable_list_threshold > 2048) temp_settings.scrollable_list_threshold = 2048;
            }
            if (ImGui::IsItemHovered()) {
                char scroll_tooltip[512];
                snprintf(scroll_tooltip, sizeof(scroll_tooltip),
                         "The maximum number of criteria/sub-stats to show before turning the list into a scrollable box.\n"
                         "Use the Scroll Wheel or left-click dragging the bar to scroll.\n"
                         "\n\nNote: Scrollable lists are automatically disabled for a specific goal\n"
                         "if 'Manual Layout' is active and any of its criteria/sub-stats use manual coordinates."
                         "Default: %d", DEFAULT_SCROLLABLE_LIST_THRESHOLD);
                ImGui::SetTooltip("%s", scroll_tooltip);
            }

            // List Scroll Speed Slider
            if (ImGui::DragFloat("List Scroll Speed", &temp_settings.tracker_list_scroll_speed, 1.0f, 1.0f, 1024.0f,
                                 "%.0f px")) {
                if (temp_settings.tracker_list_scroll_speed < 1.0f) temp_settings.tracker_list_scroll_speed = 1.0f;
                if (temp_settings.tracker_list_scroll_speed > 1024.0f)
                    temp_settings.tracker_list_scroll_speed = 1024.0f;
            }
            if (ImGui::IsItemHovered()) {
                char speed_tooltip[256];
                snprintf(speed_tooltip, sizeof(speed_tooltip),
                         "How many pixels the list scrolls per mouse wheel notch.\n"
                         "Use the Scroll Wheel or left-click dragging the bar to scroll.\n"
                         "Default: %.0f px", DEFAULT_TRACKER_LIST_SCROLL_SPEED);
                ImGui::SetTooltip("%s", speed_tooltip);
            }

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Fonts & Aesthetics");

            // --- Tracker Font ---
            ImGui::Text("Tracker Font: %s", temp_settings.tracker_font_name);
            ImGui::SameLine();
            if (ImGui::Button("Browse##TrackerFont")) {
                char selected_font[256];
                if (open_font_file_dialog(selected_font, sizeof(selected_font))) {
                    strncpy(temp_settings.tracker_font_name, selected_font,
                            sizeof(temp_settings.tracker_font_name) - 1);
                    temp_settings.tracker_font_name[sizeof(temp_settings.tracker_font_name) - 1] = '\0';
                }
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[1024];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Select the font for the main tracker view.\n"
                         "This affects the goal display text, the top info bar,\n"
                         "the bottom control buttons, the notes window,\n"
                         "and manual layout text header decorations.\n"
                         "Only choose fonts within the resources/fonts directory.\n\n"
                         "A restart is required to properly apply changes.");
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            // Tracker Font Size
            if (ImGui::DragFloat("Tracker Font Size", &temp_settings.tracker_font_size, 0.5f, 8.0f, 64.0f, "%.1f pt")) {
                if (temp_settings.tracker_font_size < 8.0f) temp_settings.tracker_font_size = 8.0f;
                if (temp_settings.tracker_font_size > 64.0f) temp_settings.tracker_font_size = 64.0f;
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[1024];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Adjust the font size for main goal names, the notes window,\n"
                         "and manual layout text header decorations.\n"
                         "Default: %.1f pt.",
                         DEFAULT_TRACKER_FONT_SIZE);
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            // Tracker Sub-Font Size
            if (ImGui::DragFloat("Sub-Item Font Size", &temp_settings.tracker_sub_font_size, 0.5f, 8.0f, 32.0f,
                                 "%.1f pt")) {
                if (temp_settings.tracker_sub_font_size < 8.0f) temp_settings.tracker_sub_font_size = 8.0f;
                if (temp_settings.tracker_sub_font_size > 32.0f) temp_settings.tracker_sub_font_size = 32.0f;
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[1024];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Adjust the font size for sub-items like criteria,\n"
                         "sub-stats, and multi-stage goal stages.\n"
                         "Default: %.1f pt.",
                         DEFAULT_TRACKER_SUB_FONT_SIZE);
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            // Tracker UI-Font Size
            if (ImGui::DragFloat("Tracker UI Font Size", &temp_settings.tracker_ui_font_size, 0.5f, 8.0f, 64.0f,
                                 "%.1f pt")) {
                if (temp_settings.tracker_ui_font_size < 8.0f) temp_settings.tracker_ui_font_size = 8.0f;
                if (temp_settings.tracker_ui_font_size > 64.0f) temp_settings.tracker_ui_font_size = 64.0f;
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[1024];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Adjust the font size for the top info bar and bottom control bar.\n"
                         "Default: %.1f pt.",
                         DEFAULT_TRACKER_UI_FONT_SIZE);
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            static float tracker_bg[4];
            tracker_bg[0] = (float) temp_settings.tracker_bg_color.r / 255.0f;
            tracker_bg[1] = (float) temp_settings.tracker_bg_color.g / 255.0f;
            tracker_bg[2] = (float) temp_settings.tracker_bg_color.b / 255.0f;
            tracker_bg[3] = (float) temp_settings.tracker_bg_color.a / 255.0f;

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

            static float text_col[4];
            text_col[0] = (float) temp_settings.text_color.r / 255.0f;
            text_col[1] = (float) temp_settings.text_color.g / 255.0f;
            text_col[2] = (float) temp_settings.text_color.b / 255.0f;
            text_col[3] = (float) temp_settings.text_color.a / 255.0f;

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

            if (font_settings_changed) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "Click 'Restart Advancely' to properly apply these font/size changes.");
            }

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Background Textures");

            // --- Background Texture Settings ---
            // Helper lambda for Browse button and text display
            auto RenderBackgroundSetting = [&
                    ](const char *label, char *path_buffer, size_t buffer_size, const char *setting_id) {
                ImGui::Text("%s:", label);
                ImGui::SameLine();
                ImGui::TextWrapped("%s", path_buffer); // Display current path, wrapped

                ImGui::SameLine(); // Align button to the right
                char button_label[64];
                snprintf(button_label, sizeof(button_label), "Browse##%s", setting_id);
                if (ImGui::Button(button_label)) {
                    char selected_file[MAX_PATH_LENGTH];
                    if (open_gui_texture_dialog(selected_file, sizeof(selected_file))) {
                        strncpy(path_buffer, selected_file, buffer_size - 1);
                        path_buffer[buffer_size - 1] = '\0';
                    }
                }
                if (ImGui::IsItemHovered()) {
                    char tooltip_buffer[512];
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Select the background texture for %s items.\n"
                             "Textures should ideally be square (e.g., 24x24 pixels - scaled to 96x96 pixels).\n"
                             "Must be a .png or .gif file located inside the resources/gui folder.", label);
                    ImGui::SetTooltip("%s", tooltip_buffer);
                }
            };

            RenderBackgroundSetting("Default", temp_settings.adv_bg_path, sizeof(temp_settings.adv_bg_path),
                                    "DefaultBg");
            RenderBackgroundSetting("Half-Done", temp_settings.adv_bg_half_done_path,
                                    sizeof(temp_settings.adv_bg_half_done_path), "HalfDoneBg");
            RenderBackgroundSetting("Done", temp_settings.adv_bg_done_path, sizeof(temp_settings.adv_bg_done_path),
                                    "DoneBg");

            // Duplicate Texture Warning
            bool duplicate_warning = false;
            if (strcmp(temp_settings.adv_bg_path, temp_settings.adv_bg_half_done_path) == 0 && temp_settings.adv_bg_path
                [0] != '\0')
                duplicate_warning = true;
            if (strcmp(temp_settings.adv_bg_path, temp_settings.adv_bg_done_path) == 0 && temp_settings.adv_bg_path[0]
                != '\0')
                duplicate_warning = true;
            if (strcmp(temp_settings.adv_bg_half_done_path, temp_settings.adv_bg_done_path) == 0 && temp_settings.
                adv_bg_half_done_path[0] != '\0')
                duplicate_warning = true;

            if (duplicate_warning) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f)); // Yellow text
                ImGui::TextWrapped(
                    "Warning: Using the same texture for multiple states makes it harder to distinguish completion status.");
                ImGui::PopStyleColor();
            }


            ImGui::EndTabItem();
        } // End of Tracker Visuals Tab

        if (ImGui::BeginTabItem("UI Visuals")) {
            ImGui::Text("UI Fonts");

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
                         "IMPORTANT: Requires restarting Advancely to apply.");
                ImGui::SetTooltip("%s", tooltip_buffer);
            }
            // UI Font Size
            if (ImGui::DragFloat("Settings/UI Font Size", &temp_settings.ui_font_size, 0.5f, 8.0f, 64.0f, "%.1f pt")) {
                if (temp_settings.ui_font_size < 8.0f) temp_settings.ui_font_size = 8.0f;
                if (temp_settings.ui_font_size > 64.0f) temp_settings.ui_font_size = 64.0f;
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[1024];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Adjust the font size for UI windows.\n"
                         "Affects Settings, Template Editor, and Notes windows.\n"
                         "Default: %.1f pt. Max: 64.0 pt.\n\n"
                         "IMPORTANT: Requires restarting Advancely to apply.",
                         DEFAULT_UI_FONT_SIZE);
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            if (font_settings_changed) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "Click 'Restart Advancely' to properly apply these font/size changes.");
            }

            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("UI Colors");

            // Helper macro to reduce boilerplate for color pickers
#define UI_COLOR_PICKER(label, field_name, tooltip_fmt, ...) \
static float field_name##_arr[4]; \
field_name##_arr[0] = (float)temp_settings.field_name.r / 255.0f; \
field_name##_arr[1] = (float)temp_settings.field_name.g / 255.0f; \
field_name##_arr[2] = (float)temp_settings.field_name.b / 255.0f; \
field_name##_arr[3] = (float)temp_settings.field_name.a / 255.0f; \
if (ImGui::ColorEdit4(label, field_name##_arr)) { \
temp_settings.field_name = { \
(Uint8)(field_name##_arr[0] * 255), (Uint8)(field_name##_arr[1] * 255), \
(Uint8)(field_name##_arr[2] * 255), (Uint8)(field_name##_arr[3] * 255) \
}; \
} \
if (ImGui::IsItemHovered()) { \
char tooltip_buffer[512]; \
snprintf(tooltip_buffer, sizeof(tooltip_buffer), tooltip_fmt, ##__VA_ARGS__); \
ImGui::SetTooltip("%s", tooltip_buffer); \
}

            UI_COLOR_PICKER("UI Text", ui_text_color,
                            "Color for most text within UI windows (Settings, Editor, Notes).");
            UI_COLOR_PICKER("Window Background", ui_window_bg_color, "Background color of UI windows.");
            UI_COLOR_PICKER("Frame Background", ui_frame_bg_color,
                            "Background color for input fields, checkboxes, sliders etc.");
            UI_COLOR_PICKER("Frame Bg Hovered", ui_frame_bg_hovered_color, "Background color for frames when hovered.");
            UI_COLOR_PICKER("Frame Bg Active", ui_frame_bg_active_color,
                            "Background color for frames when active (e.g., clicking a slider).");
            UI_COLOR_PICKER("Active Title Bar", ui_title_bg_active_color,
                            "Background color of the title bar for the currently active window.");
            UI_COLOR_PICKER("Button", ui_button_color, "Background color of buttons.");
            UI_COLOR_PICKER("Button Hovered", ui_button_hovered_color, "Background color of buttons when hovered.");
            UI_COLOR_PICKER("Button Active", ui_button_active_color, "Background color of buttons when clicked.");
            UI_COLOR_PICKER("Header", ui_header_color, "Background color of selected headers.");
            UI_COLOR_PICKER("Header Hovered", ui_header_hovered_color, "Background color of headers when hovered.");
            UI_COLOR_PICKER("Header Active", ui_header_active_color, "Background color of headers when active/open.");
            UI_COLOR_PICKER("Check Mark", ui_check_mark_color, "Color of the check mark inside checkboxes.");

#undef UI_COLOR_PICKER // Clean up the macro

            // Restart Warning
            // --- Check if any UI theme color settings have changed ---
            bool ui_theme_colors_changed =
                    memcmp(&temp_settings.ui_text_color, &saved_settings.ui_text_color, sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_window_bg_color, &saved_settings.ui_window_bg_color,
                           sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_frame_bg_color, &saved_settings.ui_frame_bg_color,
                           sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_frame_bg_hovered_color, &saved_settings.ui_frame_bg_hovered_color,
                           sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_frame_bg_active_color, &saved_settings.ui_frame_bg_active_color,
                           sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_title_bg_active_color, &saved_settings.ui_title_bg_active_color,
                           sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_button_color, &saved_settings.ui_button_color, sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_button_hovered_color, &saved_settings.ui_button_hovered_color,
                           sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_button_active_color, &saved_settings.ui_button_active_color,
                           sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_header_color, &saved_settings.ui_header_color, sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_header_hovered_color, &saved_settings.ui_header_hovered_color,
                           sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_header_active_color, &saved_settings.ui_header_active_color,
                           sizeof(ColorRGBA)) != 0 ||
                    memcmp(&temp_settings.ui_check_mark_color, &saved_settings.ui_check_mark_color,
                           sizeof(ColorRGBA)) != 0;

            // Conditionally show the warning
            if (ui_theme_colors_changed) {
                ImGui::Spacing(); // Add a little space before the warning
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "Click 'Restart Advancely' to properly apply these theme color changes.");
            }

            ImGui::EndTabItem();
        } // End of UI Visuals Tab

        if (ImGui::BeginTabItem("Overlay")) {
            // General Settings
            ImGui::Text("General");

            ImGui::Checkbox("Enable Overlay", &temp_settings.enable_overlay);
            if (ImGui::IsItemHovered()) {
                char enable_overlay_tooltip_buffer[2048];
                if (selected_version <= MC_VERSION_1_6_4) {
                    // Legacy
                    snprintf(enable_overlay_tooltip_buffer, sizeof(enable_overlay_tooltip_buffer),
                             "Enables a separate, customizable window to show your progress, perfect for streaming.\n"
                             "More overlay-related settings become visible.\n\n"
                             "Overlay Layout:\n"
                             " • Row 1: Sub-stats of complex stats (if not template hidden).\n"
                             "   (If two visible items share an icon, the parent's icon is overlaid.)\n"
                             " • Row 2: Main %s (Default).\n"
                             " • Row 3: Stats, custom goals, multi-stage goals, and counters (Default).\n"
                             "   (Goals can be forced between Row 2 and Row 3 in the Template Editor.)\n\n"
                             "Tips:\n"
                             " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                             " • A negative scroll speed animates items from right to left.\n"
                             " • Horizontal spacing depends on the length of the display text.\n\n"
                             "IMPORTANT FOR STREAMERS:\n"
                             "On Windows you MUST use GAME CAPTURE for the overlay (NOT window capture).\n"
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
                             " • Row 2: Main %s (Default).\n"
                             " • Row 3: Stats, custom goals, multi-stage goals, and counters (Default).\n"
                             "   (Goals can be forced between Row 2 and Row 3 in the Template Editor.)\n\n"
                             "Tips:\n"
                             " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                             " • A negative scroll speed animates items from right to left.\n"
                             " • Horizontal spacing depends on the length of the display text.\n\n"
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
                             " • Row 2: Main %s, recipes and unlocks (Default).\n"
                             " • Row 3: Stats, custom goals, multi-stage goals, and counters (Default).\n"
                             "   (Goals can be forced between Row 2 and Row 3 in the Template Editor.)\n\n"
                             "Tips:\n"
                             " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                             " • A negative scroll speed animates items from right to left.\n"
                             " • Horizontal spacing depends on the length of the display text.\n\n"
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
                             " • Row 2: Main %s and recipes (Default).\n"
                             " • Row 3: Stats, custom goals, multi-stage goals, and counters (Default).\n"
                             "   (Goals can be forced between Row 2 and Row 3 in the Template Editor.)\n\n"
                             "Tips:\n"
                             " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                             " • A negative scroll speed animates items from right to left.\n"
                             " • Horizontal spacing depends on the length of the display text.\n\n"
                             "IMPORTANT FOR STREAMERS:\n"
                             "Applying settings will restart the overlay window.\n"
                             "You may need to reselect it in your streaming software (e.g., OBS).",
                             advancement_label_uppercase, advancements_label_plural_lowercase
                    );
                }
                ImGui::SetTooltip("%s", enable_overlay_tooltip_buffer);
            }
            // Conditionally enable the remaining overlay settings
            if (temp_settings.enable_overlay) {
                if (ImGui::DragFloat("Overlay FPS Limit", &temp_settings.overlay_fps, 1.0f, 10.0f, 540.0f, "%.0f")) {
                    if (temp_settings.overlay_fps < 10.0f) temp_settings.overlay_fps = 10.0f;
                    if (temp_settings.overlay_fps > 540.0f) temp_settings.overlay_fps = 540.0f;
                }
                if (ImGui::IsItemHovered()) {
                    char overlay_fps_limit_tooltip_buffer[1024];
                    snprintf(overlay_fps_limit_tooltip_buffer, sizeof(overlay_fps_limit_tooltip_buffer),
                             "Limits the frames per second of the overlay window. Default is 60 FPS.\n"
                             "Higher values may result in higher GPU/CPU usage.");
                    ImGui::SetTooltip("%s", overlay_fps_limit_tooltip_buffer);
                }

                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Text("Content & Behavior");

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
                             "Shows the current world name.\n"
                             "For Co-op receivers, this shows 'Syncing with <Host>'\n"
                             "or 'Syncing for <Player>' depending on the player dropdown selection.");
                    ImGui::SetTooltip("%s", overlay_text_world_tooltip_buffer);
                }
                ImGui::SameLine();
                ImGui::Checkbox("Run Details", &temp_settings.overlay_show_run_details);
                if (ImGui::IsItemHovered()) {
                    char overlay_text_run_tooltip_buffer[1024];
                    snprintf(overlay_text_run_tooltip_buffer, sizeof(overlay_text_run_tooltip_buffer),
                             "Shows the selected Template Version & Template Category.");
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
                    ImGui::BulletText("Counter Goals");
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
                             "It's read from the statistics file so it's in ticks\n"
                             "and only updated when the game saves.");
                    ImGui::SetTooltip("%s", overlay_text_igt_tooltip_buffer);
                }
                ImGui::SameLine();
                ImGui::Checkbox("Update Timer", &temp_settings.overlay_show_update_timer);
                if (ImGui::IsItemHovered()) {
                    char overlay_text_timer_tooltip_buffer[1024];
                    snprintf(overlay_text_timer_tooltip_buffer, sizeof(overlay_text_timer_tooltip_buffer),
                             "Shows the time since the last game file update.\n"
                             "When Hermes is active this timer only represents the time\n"
                             "since the last full game-save sync from disk.");
                    ImGui::SetTooltip("%s", overlay_text_timer_tooltip_buffer);
                }

                ImGui::Checkbox("Timers Unit Spacing", &temp_settings.igt_unit_spacing);
                if (ImGui::IsItemHovered()) {
                    char igt_spacing_tooltip_buffer[256];
                    snprintf(igt_spacing_tooltip_buffer, sizeof(igt_spacing_tooltip_buffer),
                             "Adds a space between every number and its unit in the IGT\n"
                             "and Update Timer display.\n"
                             "Example: \"02m 04.500s\" becomes \"02 m 04 s 500 ms\".");
                    ImGui::SetTooltip("%s", igt_spacing_tooltip_buffer);
                }
                ImGui::SameLine();
                ImGui::Checkbox("IGT Always Show ms", &temp_settings.igt_always_show_ms);
                if (ImGui::IsItemHovered()) {
                    char igt_ms_tooltip_buffer[256];
                    snprintf(igt_ms_tooltip_buffer, sizeof(igt_ms_tooltip_buffer),
                             "Always shows milliseconds in the IGT display,\n"
                             "even when the time exceeds one minute.\n"
                             "Example: \"02m 04.500s\" instead of \"02m 04s\".");
                    ImGui::SetTooltip("%s", igt_ms_tooltip_buffer);
                }

                ImGui::Checkbox("Hide Completed Row 3 Goals", &temp_settings.overlay_row3_remove_completed);
                if (ImGui::IsItemHovered()) {
                    char hide_completed_row_3_tooltip_buffer[1024];
                    snprintf(hide_completed_row_3_tooltip_buffer, sizeof(hide_completed_row_3_tooltip_buffer),
                             "If checked, goals in Row 3 (Stats, Custom Goals,\n"
                             "Multi-Stage Goals, Counters, and any %s/Unlocks\n"
                             "forced to Row 3) will disappear when completed.\n"
                             "This is independent of the main 'Goal Visibility' setting.\n\n"
                             "NOTE: Goals forced to Row 2 via the Template Editor will ALWAYS hide when completed,\n"
                             "ignoring this setting.", advancements_label_plural_uppercase);

                    ImGui::SetTooltip("%s", hide_completed_row_3_tooltip_buffer);
                }

                if (ImGui::DragFloat("Sub-Stat Cycle Interval (s)", &temp_settings.overlay_stat_cycle_speed, 0.1f, 0.1f,
                                     60.0f,
                                     "%.3f s")) {
                    if (temp_settings.overlay_stat_cycle_speed < 0.1f) temp_settings.overlay_stat_cycle_speed = 0.1f;
                    if (temp_settings.overlay_stat_cycle_speed > 60.0f) temp_settings.overlay_stat_cycle_speed = 60.0f;
                }
                if (ImGui::IsItemHovered()) {
                    char substat_cycling_interval_tooltip_buffer[256];
                    snprintf(substat_cycling_interval_tooltip_buffer, sizeof(substat_cycling_interval_tooltip_buffer),
                             "The time in seconds before cycling to the next sub-stat on a multi-stat goal on the overlay.\n");
                    ImGui::SetTooltip("%s", substat_cycling_interval_tooltip_buffer);
                }

                if (ImGui::DragFloat("Overlay Scroll Speed", &temp_settings.overlay_scroll_speed, 0.001f, -25.00f,
                                     25.00f,
                                     "%.3f")) {
                    if (temp_settings.overlay_scroll_speed < -25.0f) temp_settings.overlay_scroll_speed = -25.0f;
                    if (temp_settings.overlay_scroll_speed > 25.0f) temp_settings.overlay_scroll_speed = 25.0f;
                }
                if (ImGui::IsItemHovered()) {
                    char overlay_scroll_speed_tooltip_buffer[1024];
                    snprintf(overlay_scroll_speed_tooltip_buffer, sizeof(overlay_scroll_speed_tooltip_buffer),
                             "A negative scroll speed animates from right-to-left\n"
                             "(items always appear in the same order as they are on the tracker).\n"
                             "A scroll speed of 0.0 is static.\n"
                             "Default of 1.0 scrolls 1440 pixels (default width) in 24 seconds.\n"
                             "Holding SPACE while the overlay window is focused speeds up the animation.");
                    ImGui::SetTooltip("%s", overlay_scroll_speed_tooltip_buffer);
                }

                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Text("Layout & Spacing");

                // Slider for overlay width
                static int overlay_width;
                overlay_width = temp_settings.overlay_window.w;
                if (ImGui::DragInt("Overlay Width", &overlay_width, 10.0f, 200, 7680)) {
                    // Strict clamping for width
                    if (overlay_width < 200) overlay_width = 200;
                    if (overlay_width > 7680) overlay_width = 7680;
                    temp_settings.overlay_window.w = overlay_width;
                }
                if (ImGui::IsItemHovered()) {
                    char overlay_width_tooltip_buffer[1024];
                    snprintf(overlay_width_tooltip_buffer, sizeof(overlay_width_tooltip_buffer),
                             "Adjusts the width of the overlay window.\nDefault: %dpx", OVERLAY_DEFAULT_WIDTH);
                    ImGui::SetTooltip("%s", overlay_width_tooltip_buffer);
                }

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

                if (ImGui::DragFloat("Row 1 Icon Spacing", &temp_settings.overlay_row1_spacing, 1.0f, 0.0f, 7680.0f,
                                     "%.0f px")) {
                    if (temp_settings.overlay_row1_spacing < 0.0f) temp_settings.overlay_row1_spacing = 0.0f;
                    if (temp_settings.overlay_row1_spacing > 7680.0f) temp_settings.overlay_row1_spacing = 7680.0f;
                }
                if (ImGui::IsItemHovered()) {
                    char tooltip_buffer[256];
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Adjusts the horizontal gap (in pixels) between icons\n"
                             "in the top row (Row 1) of the overlay.\n"
                             "The horizontal spacing of the 2nd and 3rd row\n"
                             "depends on the length of the display text.\n"
                             "Default: %.0f px",
                             DEFAULT_OVERLAY_ROW1_SPACING);
                    ImGui::SetTooltip("%s", tooltip_buffer);
                }

                if (ImGui::DragFloat("Row 1 Shared Icon Size", &temp_settings.overlay_row1_shared_icon_size, 1.0f, 0.0f,
                                     48.0f,
                                     "%.0f px")) {
                    if (temp_settings.overlay_row1_shared_icon_size < 0.0f)
                        temp_settings.overlay_row1_shared_icon_size = 0.0f;
                    if (temp_settings.overlay_row1_shared_icon_size > 48.0f)
                        temp_settings.overlay_row1_shared_icon_size = 48.0f;
                }
                if (ImGui::IsItemHovered()) {
                    char tooltip_buffer[256];
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Adjusts the size of the 'Parent Icon' overlay that appears when\n"
                             "multiple items share the same icon in Row 1.\n"
                             "Set to 0 to disable the shared icon overlay entirely.\n"
                             "Default: %.0f px",
                             DEFAULT_OVERLAY_ROW1_SHARED_ICON_SIZE);
                    ImGui::SetTooltip("%s", tooltip_buffer);
                }

                // --- Custom Row 2 Spacing ---
                ImGui::Checkbox("Custom Row 2 Spacing", &temp_settings.overlay_row2_custom_spacing_enabled);
                if (ImGui::IsItemHovered()) {
                    char tooltip_buffer[512];
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Check this to override the dynamic width calculation for Row 2 items.\n"
                             "This allows you to set a fixed, uniform width for all items in this row.\n"
                             "Applies to %s, Unlocks (unless forced to Row 3),\n"
                             "and any Stats/Goals forced to Row 2.",
                             advancements_label_plural_uppercase);
                    ImGui::SetTooltip("%s", tooltip_buffer);
                }

                if (temp_settings.overlay_row2_custom_spacing_enabled) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(150.0f); // Give the slider a fixed width
                    if (ImGui::DragFloat("Row 2 Item Width", &temp_settings.overlay_row2_custom_spacing, 1.0f, 96.0f,
                                         7680.0f,
                                         "%.0f px")) {
                        if (temp_settings.overlay_row2_custom_spacing < 96.0f)
                            temp_settings.overlay_row2_custom_spacing = 96.0f;
                        if (temp_settings.overlay_row2_custom_spacing > 7680.0f)
                            temp_settings.overlay_row2_custom_spacing = 7680.0f;
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[512];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Sets the total horizontal width (in pixels) for each item in Row 2.\n"
                                 "WARNING: If this value is too small, item text will overlap.\n"
                                 "The item icon is %dpx wide. Default: %.0fpx.",
                                 96, DEFAULT_OVERLAY_ROW2_CUSTOM_SPACING);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                }

                // --- Custom Row 3 Spacing ---
                ImGui::Checkbox("Custom Row 3 Spacing", &temp_settings.overlay_row3_custom_spacing_enabled);
                if (ImGui::IsItemHovered()) {
                    char tooltip_buffer[512];
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Check this to override the dynamic width calculation for Row 3 items.\n"
                             "This allows you to set a fixed, uniform width for all items in this row.\n"
                             "Applies to Stats, Custom Goals, Multi-Stage Goals, Counters,\n"
                             "and any %s/Unlocks forced to Row 3.",
                             advancements_label_plural_uppercase);
                    ImGui::SetTooltip("%s", tooltip_buffer);
                }

                if (temp_settings.overlay_row3_custom_spacing_enabled) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(150.0f); // Give the slider a fixed width
                    if (ImGui::DragFloat("Row 3 Item Width", &temp_settings.overlay_row3_custom_spacing, 1.0f, 96.0f,
                                         7680.0f,
                                         "%.0f px")) {
                        if (temp_settings.overlay_row3_custom_spacing < 96.0f)
                            temp_settings.overlay_row3_custom_spacing = 96.0f;
                        if (temp_settings.overlay_row3_custom_spacing > 7680.0f)
                            temp_settings.overlay_row3_custom_spacing = 7680.0f;
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[512];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Sets the total horizontal width (in pixels) for each item in Row 3.\n"
                                 "WARNING: If this value is too small, item text will overlap.\n"
                                 "The item icon is %dpx wide. Default: %.0fpx.",
                                 96, DEFAULT_OVERLAY_ROW3_CUSTOM_SPACING);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                }

                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Text("Aesthetics");

                ImGui::Text("Overlay Font: %s", temp_settings.overlay_font_name);
                ImGui::SameLine();
                if (ImGui::Button("Browse##OverlayFont")) {
                    char selected_font[256];
                    if (open_font_file_dialog(selected_font, sizeof(selected_font))) {
                        strncpy(temp_settings.overlay_font_name, selected_font,
                                sizeof(temp_settings.overlay_font_name) - 1);
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

                static float overlay_bg[4];
                overlay_bg[0] = (float) temp_settings.overlay_bg_color.r / 255.0f;
                overlay_bg[1] = (float) temp_settings.overlay_bg_color.g / 255.0f;
                overlay_bg[2] = (float) temp_settings.overlay_bg_color.b / 255.0f;
                overlay_bg[3] = (float) temp_settings.overlay_bg_color.a / 255.0f;

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

                static float overlay_text_col[4];
                overlay_text_col[0] = (float) temp_settings.overlay_text_color.r / 255.0f;
                overlay_text_col[1] = (float) temp_settings.overlay_text_color.g / 255.0f;
                overlay_text_col[2] = (float) temp_settings.overlay_text_color.b / 255.0f;
                overlay_text_col[3] = (float) temp_settings.overlay_text_color.a / 255.0f;

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
                }
            } // End of conditional overlay settings
            ImGui::EndTabItem();
        } // End of Overlay Tab

        // Account Tab — auto-select when forced open for first-time account setup
        ImGuiTabItemFlags account_tab_flags = ImGuiTabItemFlags_None;
        if (force_open_reason && *force_open_reason == FORCE_OPEN_ACCOUNT_SETUP && just_opened)
            account_tab_flags = ImGuiTabItemFlags_SetSelected;
        if (ImGui::BeginTabItem("Account", nullptr, account_tab_flags)) {
            CoopNetState acc_net_state = g_coop_ctx ? coop_net_get_state(g_coop_ctx) : COOP_NET_IDLE;
            bool acc_net_active = (acc_net_state == COOP_NET_LISTENING || acc_net_state == COOP_NET_CONNECTED
                                   || acc_net_state == COOP_NET_CONNECTING);

            ImGui::Text("Minecraft Account");
            ImGui::TextDisabled("Link your Minecraft account so Advancely reads the correct player files.\n"
                "This is required for Co-op and recommended for singleplayer when\n"
                "multiple players share the same world.");
            ImGui::Spacing();

            // Account type radio (Online / Offline)
            if (acc_net_active) ImGui::BeginDisabled();
            int acc_type = temp_settings.account_type;
            ImGui::RadioButton("Online##acc_type", &acc_type, ACCOUNT_ONLINE);
            if (ImGui::IsItemHovered(acc_net_active ? ImGuiHoveredFlags_AllowWhenDisabled : 0)) {
                char tooltip_buf[256];
                if (acc_net_active) {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Cannot change while a lobby is active.");
                } else {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Java Edition account. UUID is fetched automatically\n"
                             "from the Mojang API based on your username.");
                }
                ImGui::SetTooltip("%s", tooltip_buf);
            }
            ImGui::SameLine();
            ImGui::RadioButton("Offline##acc_type", &acc_type, ACCOUNT_OFFLINE);
            if (ImGui::IsItemHovered(acc_net_active ? ImGuiHoveredFlags_AllowWhenDisabled : 0)) {
                char tooltip_buf[256];
                if (acc_net_active) {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Cannot change while a lobby is active.");
                } else {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Offline/cracked account. You must enter your UUID manually.\n"
                             "Find it through your world's advancements or stats files.");
                }
                ImGui::SetTooltip("%s", tooltip_buf);
            }
            temp_settings.account_type = (AccountType) acc_type;
            if (acc_net_active) ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool account_linked = (temp_settings.account_type == ACCOUNT_ONLINE &&
                                   temp_settings.local_player.uuid[0] != '\0');
            bool username_disabled = acc_net_active || account_linked;

            if (username_disabled) ImGui::BeginDisabled();

            // Username input
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputText("Username##account", temp_settings.local_player.username,
                             sizeof(temp_settings.local_player.username));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                char tooltip_buf[256];
                if (acc_net_active) {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Cannot modify account while a lobby is active.");
                } else if (account_linked) {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Username is locked while the account is linked.\n"
                             "Unlink the account to change your username.");
                } else {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Your Minecraft username. Must exactly match your in-game name.\n"
                             "Capitalization does not matter. Hermes matches usernames case-insensitively.\n"
                             "Hermes checks BOTH username (lowercased) and UUID, so keep both accurate.");
                }
                ImGui::SetTooltip("%s", tooltip_buf);
            }
            if (username_disabled) ImGui::EndDisabled();

            if (temp_settings.account_type == ACCOUNT_ONLINE) {
                // Online mode: Link Account button fetches UUID from Mojang API
                ImGui::SameLine();
                bool can_link = !acc_net_active && !account_linked &&
                                temp_settings.local_player.username[0] != '\0';
                if (!can_link) ImGui::BeginDisabled();
                if (ImGui::Button("Link Account##account")) {
                    char fetched_uuid[48] = "";
                    bool fetched = mojang_fetch_uuid(temp_settings.local_player.username,
                                                     fetched_uuid, sizeof(fetched_uuid));
                    if (fetched) {
                        strncpy(temp_settings.local_player.uuid, fetched_uuid,
                                sizeof(temp_settings.local_player.uuid) - 1);
                        snprintf(coop_identity_status_msg, sizeof(coop_identity_status_msg),
                                 "Linked: %s", fetched_uuid);
                        coop_identity_status_is_error = false;
                    } else {
                        temp_settings.local_player.uuid[0] = '\0';
                        snprintf(coop_identity_status_msg, sizeof(coop_identity_status_msg),
                                 "Could not find player '%s'. Check the username.",
                                 temp_settings.local_player.username);
                        coop_identity_status_is_error = true;
                    }
                }
                if (!can_link) ImGui::EndDisabled();

                // Show link status
                if (temp_settings.local_player.uuid[0] != '\0') {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Linked: %s",
                                       temp_settings.local_player.uuid);
                    if (!acc_net_active) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Unlink##account")) {
                            temp_settings.local_player.uuid[0] = '\0';
                            coop_identity_status_msg[0] = '\0';
                            coop_identity_status_is_error = false;
                        }
                    }
                } else if (coop_identity_status_msg[0] != '\0') {
                    if (coop_identity_status_is_error)
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", coop_identity_status_msg);
                    else
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", coop_identity_status_msg);
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "Not linked yet");
                }
            } else {
                // Offline mode: manual UUID input
                if (acc_net_active) ImGui::BeginDisabled();
                ImGui::SetNextItemWidth(320.0f);
                ImGui::InputTextWithHint("UUID##account_offline", "e.g. 069a79f4-44e9-4726-a5be-fca90e38aaf5",
                                         temp_settings.local_player.uuid,
                                         sizeof(temp_settings.local_player.uuid));
                if (ImGui::IsItemHovered(acc_net_active ? ImGuiHoveredFlags_AllowWhenDisabled : 0)) {
                    char tooltip_buf[256];
                    if (acc_net_active) {
                        snprintf(tooltip_buf, sizeof(tooltip_buf),
                                 "Cannot modify account while a lobby is active.");
                    } else {
                        snprintf(tooltip_buf, sizeof(tooltip_buf),
                                 "Your offline UUID. Must be exact. This is the authoritative player\n"
                                 "identifier used by Hermes and legacy stats files.\n"
                                 "Look in your world's stats or playerdata folder for a JSON file named\n"
                                 "with your UUID (e.g. 069a79f4-...-fca90e38aaf5.json).");
                    }
                    ImGui::SetTooltip("%s", tooltip_buf);
                }
                if (temp_settings.local_player.uuid[0] != '\0') {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "UUID set");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "UUID not set");
                }
                if (acc_net_active) ImGui::EndDisabled();
            }

            // Display Name (optional, shared between online/offline)
            if (acc_net_active) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("Display Name##account", "Optional",
                                     temp_settings.local_player.display_name,
                                     sizeof(temp_settings.local_player.display_name));
            if (ImGui::IsItemHovered(acc_net_active ? ImGuiHoveredFlags_AllowWhenDisabled : 0)) {
                char tooltip_buf[256];
                if (acc_net_active) {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Cannot modify account while a lobby is active.");
                } else {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Optional display name shown in the Co-op lobby.\n"
                             "Leave empty to use your username.");
                }
                ImGui::SetTooltip("%s", tooltip_buf);
            }
            if (acc_net_active) ImGui::EndDisabled();

            // --- UUID validation (empty + format check) ---
            bool uuid_empty = (temp_settings.local_player.uuid[0] == '\0');
            bool uuid_bad_format = false;
            if (!uuid_empty) {
                const char *u = temp_settings.local_player.uuid;
                size_t len = strlen(u);
                if (len != 36) {
                    uuid_bad_format = true;
                } else {
                    for (size_t ci = 0; ci < 36 && !uuid_bad_format; ++ci) {
                        if (ci == 8 || ci == 13 || ci == 18 || ci == 23) {
                            if (u[ci] != '-') uuid_bad_format = true;
                        } else {
                            char ch = u[ci];
                            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')))
                                uuid_bad_format = true;
                        }
                    }
                }
            }
            account_validation_error = uuid_empty || uuid_bad_format;

            if (uuid_bad_format) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                   "Invalid UUID format. Expected: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            }

            // Status summary
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            bool identity_ready = !account_validation_error;
            if (identity_ready) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Account configured.");
                ImGui::TextDisabled("Advancely will use your UUID to read the correct player files.");
            } else if (uuid_bad_format) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "Invalid UUID format.");
                ImGui::TextDisabled("The UUID must be in the format 8-4-4-4-12 hex digits with dashes.");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "Account not configured.");
                ImGui::TextDisabled("Without an account, Advancely picks the first file it finds.\n"
                    "This can read the wrong player's data in shared worlds.");
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Co-op")) {
            // Co-op tab uses function-scoped statics: coop_identity_status_msg,
            // coop_identity_status_is_error, coop_room_code_buf, coop_room_code_error, coop_ip_revealed

            coop_host_input_error = false; // Reset each frame

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
            ImGui::Text("Co-op Documentation");
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                char coop_doc_tooltip_buf[128];
                snprintf(coop_doc_tooltip_buf, sizeof(coop_doc_tooltip_buf),
                         "Opens the full Co-op setup guide in your browser.");
                ImGui::SetTooltip("%s", coop_doc_tooltip_buf);
            }
            if (ImGui::IsItemClicked()) {
                open_content("https://github.com/LNXSeus/Advancely#co-op-multiplayer");
            }
            ImGui::Separator();
            ImGui::Spacing();

            // Get current networking state
            CoopNetState net_state = g_coop_ctx ? coop_net_get_state(g_coop_ctx) : COOP_NET_IDLE;
            bool net_is_active = (net_state == COOP_NET_LISTENING || net_state == COOP_NET_CONNECTED
                                  || net_state == COOP_NET_CONNECTING);

            // ============================================================
            // Step 1: Enable Co-op
            // ============================================================
            // Can't flip the master toggle while a lobby is live - stop it first.
            ImGui::BeginDisabled(net_is_active);
            ImGui::Checkbox("Enable Co-op", &temp_settings.coop_enabled);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                char tooltip_buf[320];
                if (net_is_active) {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Stop hosting or leave the lobby before changing this.");
                } else {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Enable cooperative multiplayer tracking.\n"
                             "Requires all players to be on the same local network.\n"
                             "Use ZeroTier (zerotier.com) to create a virtual LAN.");
                }
                ImGui::SetTooltip("%s", tooltip_buf);
            }
            if (temp_settings.coop_enabled) {
                ImGui::TextDisabled("Co-op requires all players to be on the same local network.\n"
                    "Use ZeroTier (zerotier.com) to create a virtual LAN.");
            }

            // If co-op was just unchecked while networking is active, stop it
            if (!temp_settings.coop_enabled && net_is_active && g_coop_ctx) {
                coop_net_stop(g_coop_ctx);
                net_state = COOP_NET_IDLE;
                net_is_active = false;
                coop_room_code_buf[0] = '\0';
                coop_ip_revealed = false;
                coop_public_ip_revealed = false;
            }

            if (temp_settings.coop_enabled) {
                bool identity_complete = temp_settings.local_player.uuid[0] != '\0';

                // Show identity status from Account tab
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (identity_complete) {
                    char id_label[128];
                    snprintf(id_label, sizeof(id_label), "Account: %s",
                             temp_settings.local_player.username[0] != '\0'
                                 ? temp_settings.local_player.username
                                 : temp_settings.local_player.uuid);
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", id_label);
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                       "Account not configured. Go to the Account tab first.");
                }

                if (identity_complete) {
                    ImGui::Spacing();

                    // ============================================================
                    // Step 3: Choose Role
                    // ============================================================
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::Text("Role");
                    ImGui::Spacing();

                    // Disable role switching while networking is active
                    if (net_is_active) ImGui::BeginDisabled();
                    int mode = temp_settings.network_mode;
                    if (mode == NETWORK_SINGLEPLAYER) mode = NETWORK_HOST; // Default to host when first choosing
                    ImGui::RadioButton("Host", &mode, NETWORK_HOST);
                    if (net_is_active) {
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            char tooltip_buf[256];
                            snprintf(tooltip_buf, sizeof(tooltip_buf),
                                     "Cannot change role while a lobby is active.");
                            ImGui::SetTooltip("%s", tooltip_buf);
                        }
                    } else {
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buf[256];
                            snprintf(tooltip_buf, sizeof(tooltip_buf),
                                     "Host a co-op lobby.\n"
                                     "You read game files for all players and share a room code.");
                            ImGui::SetTooltip("%s", tooltip_buf);
                        }
                    }
                    ImGui::SameLine();
                    ImGui::RadioButton("Receiver", &mode, NETWORK_RECEIVER);
                    if (net_is_active) {
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            char tooltip_buf[256];
                            snprintf(tooltip_buf, sizeof(tooltip_buf),
                                     "Cannot change role while a lobby is active.");
                            ImGui::SetTooltip("%s", tooltip_buf);
                        }
                    } else {
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buf[256];
                            snprintf(tooltip_buf, sizeof(tooltip_buf),
                                     "Join a co-op lobby.\n"
                                     "Paste a room code from the host to connect.");
                            ImGui::SetTooltip("%s", tooltip_buf);
                        }
                    }
                    temp_settings.network_mode = (NetworkMode) mode;
                    if (net_is_active) ImGui::EndDisabled();

                    ImGui::Spacing();

                    // ============================================================
                    // Transport selection (relay vs direct LAN/VPN)
                    // ============================================================
                    {
                        bool host_locally = (temp_settings.coop_transport == COOP_TRANSPORT_DIRECT);
                        ImGui::BeginDisabled(net_is_active);
                        if (ImGui::Checkbox("Host locally (LAN / VPN)", &host_locally)) {
                            temp_settings.coop_transport = host_locally
                                ? COOP_TRANSPORT_DIRECT
                                : COOP_TRANSPORT_RELAY;
                        }
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            char tt[384];
                            if (net_is_active) {
                                snprintf(tt, sizeof(tt),
                                         "Disconnect before changing the transport.");
                            } else {
                                snprintf(tt, sizeof(tt),
                                         "Off (default): connect through the Advancely relay server.\n"
                                         "On: classic LAN / VPN hosting with a direct IP + port.\n"
                                         "Use this if everyone is on the same network or VPN.");
                            }
                            ImGui::SetTooltip("%s", tt);
                        }
                    }
                    ImGui::Spacing();

                    bool transport_direct = (temp_settings.coop_transport == COOP_TRANSPORT_DIRECT);

                    // ============================================================
                    // Step 4a: Host a Lobby
                    // ============================================================
                    if (temp_settings.network_mode == NETWORK_HOST) {
                        ImGui::Separator();
                        ImGui::Spacing();
                        ImGui::Text("Host Settings");
                        ImGui::Spacing();

                        // Auto-accept toggle. Direct path only — on the relay path,
                        // the password is the gate and auto-accept is implicitly on.
                        // Hidden entirely on relay so users aren't confused by a
                        // setting that has no effect.
                        if (transport_direct) {
                            ImGui::BeginDisabled(net_is_active);
                            ImGui::Checkbox("Auto-accept join requests", &temp_settings.coop_auto_accept);
                            ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                char aa_tip[320];
                                if (net_is_active) {
                                    snprintf(aa_tip, sizeof(aa_tip),
                                             "Stop hosting before changing this.");
                                } else {
                                    snprintf(aa_tip, sizeof(aa_tip),
                                             "When enabled, any valid join request is\n"
                                             "instantly accepted without an approval prompt.\n"
                                             "Useful for trusted local groups.");
                                }
                                ImGui::SetTooltip("%s", aa_tip);
                            }
                        } else {
                            ImGui::TextDisabled(
                                "Auto-accept is always on for relay hosting. The password is the gate.");
                        }
                        ImGui::Spacing();

                        // IPv4 validation
                        auto is_valid_ipv4 = [](const char *ip) -> bool {
                            if (!ip || ip[0] == '\0') return false;
                            int octets = 0, value = -1;
                            for (const char *p = ip; ; p++) {
                                if (*p >= '0' && *p <= '9') {
                                    if (value < 0) value = 0;
                                    value = value * 10 + (*p - '0');
                                    if (value > 255) return false;
                                } else if (*p == '.' || *p == '\0') {
                                    if (value < 0) return false;
                                    octets++;
                                    value = -1;
                                    if (*p == '\0') break;
                                } else return false;
                            }
                            return octets == 4;
                        };
                        auto is_valid_port = [](const char *port) -> bool {
                            if (!port || port[0] == '\0') return false;
                            int value = 0;
                            for (const char *p = port; *p; p++) {
                                if (*p < '0' || *p > '9') return false;
                                value = value * 10 + (*p - '0');
                                if (value > 65535) return false;
                            }
                            return value >= 1;
                        };

                        auto is_valid_domain = [](const char *host) -> bool {
                            if (!host || host[0] == '\0') return false;
                            size_t len = strlen(host);
                            if (len > 253) return false;
                            bool has_dot = false;
                            for (size_t i = 0; i < len; i++) {
                                char c = host[i];
                                if (c == '.') {
                                    has_dot = true;
                                    continue;
                                }
                                if (c == '-') continue;
                                if (c >= '0' && c <= '9') continue;
                                if (c >= 'a' && c <= 'z') continue;
                                if (c >= 'A' && c <= 'Z') continue;
                                return false;
                            }
                            return has_dot && host[0] != '.' && host[len - 1] != '.';
                        };

                        bool ip_filled = temp_settings.host_ip[0] != '\0';
                        bool ip_valid = is_valid_ipv4(temp_settings.host_ip);
                        bool port_filled = temp_settings.host_port[0] != '\0';
                        bool port_valid = is_valid_port(temp_settings.host_port);
                        bool pub_ip_filled = temp_settings.host_public_ip[0] != '\0';
                        bool pub_ip_valid = !pub_ip_filled || is_valid_ipv4(temp_settings.host_public_ip)
                                            || is_valid_domain(temp_settings.host_public_ip);
                        bool pub_ip_duplicate = pub_ip_filled && ip_filled
                                                && strcmp(temp_settings.host_public_ip, temp_settings.host_ip) == 0;

                        if (transport_direct) {
                        if (net_is_active) ImGui::BeginDisabled();
                        ImGui::SetNextItemWidth(200.0f);
                        ImGuiInputTextFlags ip_flags = coop_ip_revealed ? 0 : ImGuiInputTextFlags_Password;
                        ImGui::InputText("IP Address", temp_settings.host_ip, sizeof(temp_settings.host_ip),
                                         ip_flags);
                        if (net_is_active) {
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                char tooltip_buf[256];
                                snprintf(tooltip_buf, sizeof(tooltip_buf),
                                         "Cannot change IP while a lobby is active.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                        } else {
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buf[512];
                                snprintf(tooltip_buf, sizeof(tooltip_buf),
                                         "The IP address Advancely binds to on this machine.\n"
                                         "Use your VPN/LAN IP (e.g. ZeroTier) or local network IP.\n"
                                         "This field is hidden to prevent accidental leaks on stream.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                        }
                        if (net_is_active) ImGui::EndDisabled();
                        // Reveal/Hide button stays enabled even while hosting
                        ImGui::SameLine();
                        if (coop_ip_revealed) {
                            if (ImGui::SmallButton("Hide IP")) {
                                coop_ip_revealed = false;
                            }
                        } else {
                            if (ImGui::SmallButton("Reveal IP")) {
                                ImGui::OpenPopup("Reveal IP?##coop");
                            }
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buf[256];
                                snprintf(tooltip_buf, sizeof(tooltip_buf),
                                         "Show the IP address in plain text.\n"
                                         "WARNING: Do not reveal this while streaming or screen sharing.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                        }
                        if (ip_filled && !ip_valid) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid IP (x.x.x.x)");
                        }

                        // Optional public IP or domain for the room code
                        if (net_is_active) ImGui::BeginDisabled();
                        ImGui::SetNextItemWidth(200.0f);
                        ImGuiInputTextFlags pub_ip_flags = coop_public_ip_revealed ? 0 : ImGuiInputTextFlags_Password;
                        ImGui::InputTextWithHint("Public IP##host", "Optional",
                                                 temp_settings.host_public_ip,
                                                 sizeof(temp_settings.host_public_ip),
                                                 pub_ip_flags);
                        if (net_is_active) {
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                char tooltip_buf[256];
                                snprintf(tooltip_buf, sizeof(tooltip_buf),
                                         "Cannot change Public IP while a lobby is active.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                        } else {
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buf[512];
                                snprintf(tooltip_buf, sizeof(tooltip_buf),
                                         "Optional. Your public IP or domain for players connecting over the internet.\n"
                                         "Requires port forwarding on your router.\n"
                                         "If set, the room code will use this instead of the bind IP.\n"
                                         "Leave empty to use the bind IP for the room code (VPN/LAN).\n"
                                         "Accepts IPv4 addresses (e.g. 203.0.113.5) or domains (e.g. play.example.com).\n"
                                         "This field is hidden to prevent accidental leaks on stream.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                        }
                        if (net_is_active) ImGui::EndDisabled();
                        // Reveal/Hide button for public IP
                        ImGui::SameLine();
                        if (coop_public_ip_revealed) {
                            if (ImGui::SmallButton("Hide Public IP")) {
                                coop_public_ip_revealed = false;
                            }
                        } else {
                            if (ImGui::SmallButton("Reveal Public IP")) {
                                ImGui::OpenPopup("Reveal Public IP?##coop");
                            }
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buf[256];
                                snprintf(tooltip_buf, sizeof(tooltip_buf),
                                         "Show the public IP address in plain text.\n"
                                         "WARNING: Do not reveal this while streaming or screen sharing.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                        }
                        if (pub_ip_filled && !pub_ip_valid) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid IP or domain");
                        }
                        if (pub_ip_duplicate) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                               "Public IP must be different from the bind IP.");
                        }

                        if (net_is_active) ImGui::BeginDisabled();
                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::InputText("Port", temp_settings.host_port, sizeof(temp_settings.host_port),
                                         ImGuiInputTextFlags_CharsDecimal);
                        if (net_is_active) {
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                char tooltip_buf[256];
                                snprintf(tooltip_buf, sizeof(tooltip_buf),
                                         "Cannot change port while a lobby is active.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                        } else {
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buf[256];
                                snprintf(tooltip_buf, sizeof(tooltip_buf), "Default is 12345.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                        }
                        if (port_filled && !port_valid) {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid port (1-65535)");
                        }
                        if (net_is_active) ImGui::EndDisabled();
                        } else {
                            // Relay host: just a password (optional). Room code is
                            // assigned by the relay on Start Lobby and displayed below.
                            if (net_is_active) ImGui::BeginDisabled();
                            ImGui::SetNextItemWidth(200.0f);
                            ImGuiInputTextFlags relay_host_pw_flags = coop_relay_password_host_revealed
                                                                          ? 0
                                                                          : ImGuiInputTextFlags_Password;
                            ImGui::InputTextWithHint("Room Password##relay_host", "(optional)",
                                                     coop_relay_password_host,
                                                     sizeof(coop_relay_password_host),
                                                     relay_host_pw_flags);
                            if (net_is_active) ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                char tt[320];
                                snprintf(tt, sizeof(tt),
                                         "Optional password for the room.\n"
                                         "Hashed locally before being sent to the relay.\n"
                                         "Leave empty to allow anyone with the room code in.");
                                ImGui::SetTooltip("%s", tt);
                            }
                            // Reveal/Hide button — same warning popup pattern as Reveal IP.
                            ImGui::SameLine();
                            if (coop_relay_password_host_revealed) {
                                if (ImGui::SmallButton("Hide##relay_host_pw")) {
                                    coop_relay_password_host_revealed = false;
                                }
                            } else {
                                if (ImGui::SmallButton("Reveal##relay_host_pw")) {
                                    ImGui::OpenPopup("Reveal Password?##coop_relay_host");
                                }
                                if (ImGui::IsItemHovered()) {
                                    char tt[256];
                                    snprintf(tt, sizeof(tt),
                                             "Show the password in plain text.\n"
                                             "WARNING: Do not reveal this while streaming or screen sharing.");
                                    ImGui::SetTooltip("%s", tt);
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Copy##relay_host_pw")) {
                                SDL_SetClipboardText(coop_relay_password_host);
                            }
                            if (ImGui::IsItemHovered()) {
                                char tt[256];
                                snprintf(tt, sizeof(tt),
                                         "Copy the password to your clipboard.\n"
                                         "Share it privately with players joining your room.");
                                ImGui::SetTooltip("%s", tt);
                            }
                        }

                        coop_host_input_error = transport_direct
                            && ((ip_filled && !ip_valid) || (port_filled && !port_valid)
                                || (pub_ip_filled && !pub_ip_valid) || pub_ip_duplicate);

                        // --- Goal Merging Rules (above Start Lobby so host configures before starting) ---
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                        ImGui::Text("Goal Merging Rules");
                        ImGui::TextDisabled("Controls how progress is combined across players.");
                        ImGui::Spacing();

                        // --- Automatic rules (read-only info) ---
                        {
                            char tooltip_buf[256];

                            ImGui::BulletText(
                                "%s: Completed if any player completes it, tracking the player with the most criteria.",
                                advancements_label_plural_uppercase);
                            ImGui::BulletText("Multi-Stage Goals: Any player progress counts globally.");
                            if (ImGui::IsItemHovered()) {
                                snprintf(tooltip_buf, sizeof(tooltip_buf),
                                         "Each stage can be advanced by any player. The furthest stage across all players is used.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                            ImGui::BulletText("Counters: Derived automatically from linked goals.");
                            if (selected_version == MC_VERSION_25W14CRAFTMINE) {
                                ImGui::BulletText("Unlocks (25w14craftmine): Every player must obtain it (AND).");
                                if (ImGui::IsItemHovered()) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Unlocks are per-player in craftmine.\n"
                                             "An unlock only counts as complete when all players have obtained it.\n"
                                             "Use the player dropdown to see each player's individual unlocks.");
                                    ImGui::SetTooltip("%s", tooltip_buf);
                                }
                            }
                        }

                        ImGui::Spacing();

                        // --- Configurable merge settings ---
                        {
                            char tooltip_buf[256];
                            bool merge_locked = net_is_active;
                            if (merge_locked) ImGui::BeginDisabled();

                            // Stats / Sub-Stats merge mode
                            ImGui::Text("Stats / Sub-Stats:");
                            ImGui::SameLine();
                            int stat_merge = temp_settings.coop_stat_merge;
                            ImGui::PushID("coop_stat_merge");
                            ImGui::RadioButton("Highest Value", &stat_merge, COOP_STAT_HIGHEST);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                if (merge_locked)
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Goal merging rules are locked while a lobby is active");
                                else
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Use whichever player has the highest value for each stat.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                            ImGui::SameLine();
                            ImGui::RadioButton("Cumulative (Sum)", &stat_merge, COOP_STAT_CUMULATIVE);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                if (merge_locked)
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Goal merging rules are locked while a lobby is active");
                                else
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Sum stat values across all players.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                            temp_settings.coop_stat_merge = (CoopStatMerge) stat_merge;
                            ImGui::PopID();

                            // Stat Checkboxes
                            ImGui::Text("Stat Checkboxes:");
                            ImGui::SameLine();
                            int stat_cb = temp_settings.coop_stat_checkbox;
                            ImGui::PushID("coop_stat_cb");
                            ImGui::RadioButton("Host Only", &stat_cb, COOP_STAT_CHECKBOX_HOST_ONLY);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                if (merge_locked)
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Goal merging rules are locked while a lobby is active");
                                else
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Only the host can manually check off stats.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                            ImGui::SameLine();
                            ImGui::RadioButton("Any Player", &stat_cb, COOP_STAT_CHECKBOX_ANY_PLAYER);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                if (merge_locked)
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Goal merging rules are locked while a lobby is active");
                                else
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Any player can manually check off stats.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                            temp_settings.coop_stat_checkbox = (CoopStatCheckbox) stat_cb;
                            ImGui::PopID();

                            // Custom Goals
                            ImGui::Text("Custom Goals:");
                            ImGui::SameLine();
                            int custom_mode = temp_settings.coop_custom_goal_mode;
                            ImGui::PushID("coop_custom");
                            ImGui::RadioButton("Host Only", &custom_mode, COOP_CUSTOM_HOST_ONLY);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                if (merge_locked)
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Goal merging rules are locked while a lobby is active");
                                else
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Only the host can modify custom goals and checkboxes.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                            ImGui::SameLine();
                            ImGui::RadioButton("Any Player", &custom_mode, COOP_CUSTOM_ANY_PLAYER);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                if (merge_locked)
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Goal merging rules are locked while a lobby is active");
                                else
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Any player can modify custom goals and checkboxes.");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                            temp_settings.coop_custom_goal_mode = (CoopCustomGoalMode) custom_mode;
                            ImGui::PopID();

                            if (merge_locked) ImGui::EndDisabled();
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        // Start Lobby button (disabled when already hosting, invalid input, unsaved changes, or template editor open)
                        {
                            bool editor_open = p_temp_creator_open && *p_temp_creator_open;
                            bool inputs_ok = transport_direct
                                             ? (ip_valid && port_valid && pub_ip_valid && !pub_ip_duplicate)
                                             : true; // Relay: no ip/port; password is optional.
                            bool can_start = inputs_ok
                                             && g_coop_ctx && !net_is_active && !has_unsaved_changes && !editor_open;
                            if (!can_start) ImGui::BeginDisabled();
                            if (ImGui::Button("Start Lobby")) {
                                if (transport_direct) {
                                    int port = atoi(temp_settings.host_port);
                                    if (coop_net_start_host(g_coop_ctx, temp_settings.host_ip, port,
                                                            temp_settings.local_player.username,
                                                            temp_settings.local_player.uuid,
                                                            temp_settings.local_player.display_name,
                                                            temp_settings.coop_auto_accept)) {
                                        update_coop_template_sync(&temp_settings);
                                        const char *room_code_ip = pub_ip_filled
                                                                       ? temp_settings.host_public_ip
                                                                       : temp_settings.host_ip;
                                        coop_encode_room_code(room_code_ip, port,
                                                              coop_room_code_buf, sizeof(coop_room_code_buf));
                                    }
                                } else {
                                    // Relay path: use selected MC version as the room's mc_version tag.
                                    if (coop_net_start_host_relay(g_coop_ctx, temp_settings.version_str,
                                                                  coop_relay_password_host,
                                                                  temp_settings.local_player.username,
                                                                  temp_settings.local_player.uuid,
                                                                  temp_settings.local_player.display_name)) {
                                        update_coop_template_sync(&temp_settings);
                                        // Surface the relay-assigned room code in the same UI slot the
                                        // direct path uses, so the existing Copy / display logic works.
                                        coop_net_get_room_code(g_coop_ctx, coop_room_code_buf,
                                                               sizeof(coop_room_code_buf));
                                    }
                                }
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !can_start) {
                                char tooltip_buf[256];
                                if (net_is_active) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf), "The lobby has already been started.");
                                } else if (has_unsaved_changes) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Apply settings before starting a lobby.");
                                } else if (!ip_valid && !port_valid) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "A valid IP address and port are required to start a lobby.");
                                } else if (!ip_valid) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "A valid IP address is required to start a lobby.");
                                } else if (!port_valid) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "A valid port is required to start a lobby.");
                                } else if (!pub_ip_valid) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "The public IP/domain is not valid.");
                                } else if (pub_ip_duplicate) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Public IP must be different from the bind IP.");
                                } else if (editor_open) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Close the Template Editor before starting a lobby.");
                                }
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                            if (!can_start) ImGui::EndDisabled();
                        }
                        // Copy Room Code button (shown next to Start Lobby when hosting).
                        // On relay we include the room password (if set) in the
                        // copied text so it's a single shareable line.
                        if (net_state == COOP_NET_LISTENING && coop_room_code_buf[0] != '\0') {
                            ImGui::SameLine();
                            if (ImGui::Button("Copy Room Code")) {
                                if (!transport_direct && coop_relay_password_host[0] != '\0') {
                                    char clipbuf[256];
                                    snprintf(clipbuf, sizeof(clipbuf),
                                             "Room Code: %s - Password: %s",
                                             coop_room_code_buf, coop_relay_password_host);
                                    SDL_SetClipboardText(clipbuf);
                                } else if (!transport_direct) {
                                    char clipbuf[128];
                                    snprintf(clipbuf, sizeof(clipbuf),
                                             "Room Code: %s - No Password",
                                             coop_room_code_buf);
                                    SDL_SetClipboardText(clipbuf);
                                } else {
                                    SDL_SetClipboardText(coop_room_code_buf);
                                }
                            }
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buf[512];
                                if (!transport_direct) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Copy the room code (and password if set) to your clipboard.\n"
                                             "Share the line privately with players joining your room.");
                                } else {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Copy the room code to your clipboard.\n"
                                             "Share this code privately with players on the same VPN/LAN.\n"
                                             "They can paste it in the Receiver tab to send a join request.");
                                }
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                        }

                        // Show error/disconnect status inline (lobby handles active states)
                        if (g_coop_ctx && (net_state == COOP_NET_ERROR || net_state == COOP_NET_DISCONNECTED)) {
                            ImGui::Spacing();
                            char status_buf[256];
                            coop_net_get_status_msg(g_coop_ctx, status_buf, sizeof(status_buf));
                            ImVec4 sc = (net_state == COOP_NET_ERROR)
                                            ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                            : ImVec4(1.0f, 0.6f, 0.4f, 1.0f);
                            ImGui::TextColored(sc, "%s", status_buf);

                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
                            ImGui::Text("(Co-op Chapter - Connection Error Codes at the end)");
                            ImGui::PopStyleColor();
                            if (ImGui::IsItemHovered()) {
                                char coop_err_tooltip_buf[512];
                                snprintf(coop_err_tooltip_buf, sizeof(coop_err_tooltip_buf),
                                         "Opens the Co-op Multiplayer chapter in your browser.\n"
                                         "Expand the chapter and scroll to the bottom for the\n"
                                         "'Connection Error Codes' table to look up the status above.");
                                ImGui::SetTooltip("%s", coop_err_tooltip_buf);
                            }
                            if (ImGui::IsItemClicked()) {
                                open_content("https://github.com/LNXSeus/Advancely#co-op-multiplayer");
                            }
                        }
                    }

                    // ============================================================
                    // Step 4b: Join a Lobby (Receiver)
                    // ============================================================
                    if (temp_settings.network_mode == NETWORK_RECEIVER) {
                        // Hide the entire "Join a Lobby" section once connected
                        if (net_state != COOP_NET_CONNECTED) {
                            ImGui::Separator();
                            ImGui::Spacing();
                            ImGui::Text("Join a Lobby");
                            ImGui::Spacing();

                            if (net_state == COOP_NET_CONNECTING) {
                                // Show disconnect + status while connecting
                                if (ImGui::Button("Disconnect")) {
                                    coop_net_stop(g_coop_ctx);
                                    SDL_SetAtomicInt(&g_settings_changed, 1);
                                    SDL_SetAtomicInt(&g_apply_button_clicked, 1);
                                }
                                ImGui::Spacing();
                                char status_buf[256];
                                coop_net_get_status_msg(g_coop_ctx, status_buf, sizeof(status_buf));
                                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", status_buf);
                            } else if (!transport_direct) {
                                bool join_editor_open = p_temp_creator_open && *p_temp_creator_open;
                                bool can_join_relay = !has_unsaved_changes && !join_editor_open
                                                       && coop_relay_room_code_recv[0] != '\0';

                                ImGui::SetNextItemWidth(120.0f);
                                ImGui::InputTextWithHint("Room Code##relay_recv", "ABC123",
                                                         coop_relay_room_code_recv,
                                                         sizeof(coop_relay_room_code_recv),
                                                         ImGuiInputTextFlags_CharsUppercase);
                                if (ImGui::IsItemHovered()) {
                                    char tt[256];
                                    snprintf(tt, sizeof(tt),
                                             "6-character code shared by the host.\n"
                                             "Get it from whoever is hosting the room.");
                                    ImGui::SetTooltip("%s", tt);
                                }

                                ImGui::SetNextItemWidth(200.0f);
                                ImGuiInputTextFlags relay_recv_pw_flags = coop_relay_password_recv_revealed
                                                                              ? 0
                                                                              : ImGuiInputTextFlags_Password;
                                ImGui::InputTextWithHint("Password##relay_recv", "(if required)",
                                                         coop_relay_password_recv,
                                                         sizeof(coop_relay_password_recv),
                                                         relay_recv_pw_flags);
                                if (ImGui::IsItemHovered()) {
                                    char tt[256];
                                    snprintf(tt, sizeof(tt),
                                             "Password set by the host (if any).\n"
                                             "Hashed locally before sending to the relay.");
                                    ImGui::SetTooltip("%s", tt);
                                }
                                ImGui::SameLine();
                                if (coop_relay_password_recv_revealed) {
                                    if (ImGui::SmallButton("Hide##relay_recv_pw")) {
                                        coop_relay_password_recv_revealed = false;
                                    }
                                } else {
                                    if (ImGui::SmallButton("Reveal##relay_recv_pw")) {
                                        ImGui::OpenPopup("Reveal Password?##coop_relay_recv");
                                    }
                                    if (ImGui::IsItemHovered()) {
                                        char tt[256];
                                        snprintf(tt, sizeof(tt),
                                                 "Show the password in plain text.\n"
                                                 "WARNING: Do not reveal this while streaming or screen sharing.");
                                        ImGui::SetTooltip("%s", tt);
                                    }
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Copy##relay_recv_pw")) {
                                    SDL_SetClipboardText(coop_relay_password_recv);
                                }
                                if (ImGui::IsItemHovered()) {
                                    char tt[256];
                                    snprintf(tt, sizeof(tt),
                                             "Copy the password to your clipboard.");
                                    ImGui::SetTooltip("%s", tt);
                                }

                                if (!can_join_relay) ImGui::BeginDisabled();
                                if (ImGui::Button("Join via Relay")) {
                                    if (g_coop_ctx) {
                                        coop_net_start_receiver_relay(g_coop_ctx,
                                                                      coop_relay_room_code_recv,
                                                                      coop_relay_password_recv,
                                                                      temp_settings.local_player.username,
                                                                      temp_settings.local_player.uuid,
                                                                      temp_settings.local_player.display_name);
                                    }
                                }
                                if (!can_join_relay) {
                                    ImGui::EndDisabled();
                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                        char tt[256];
                                        if (join_editor_open)
                                            snprintf(tt, sizeof(tt),
                                                     "Close the Template Editor before joining a lobby.");
                                        else if (has_unsaved_changes)
                                            snprintf(tt, sizeof(tt),
                                                     "Apply settings before joining a lobby.");
                                        else
                                            snprintf(tt, sizeof(tt),
                                                     "Enter a room code first.");
                                        ImGui::SetTooltip("%s", tt);
                                    }
                                }
                            } else {
                                // Idle / Error / Disconnected — show paste button
                                bool join_editor_open = p_temp_creator_open && *p_temp_creator_open;
                                bool can_join = !has_unsaved_changes && !join_editor_open;
                                if (!can_join) ImGui::BeginDisabled();
                                if (ImGui::Button("Paste Room Code")) {
                                    coop_room_code_error[0] = '\0';
                                    char *clipboard = SDL_GetClipboardText();
                                    if (!clipboard || clipboard[0] == '\0') {
                                        snprintf(coop_room_code_error, sizeof(coop_room_code_error),
                                                 "Clipboard is empty.");
                                    } else {
                                        char decoded_ip[64];
                                        int decoded_port;
                                        if (coop_decode_room_code(clipboard, decoded_ip, sizeof(decoded_ip),
                                                                  &decoded_port)) {
                                            if (g_coop_ctx) {
                                                coop_net_start_receiver(g_coop_ctx, decoded_ip, decoded_port,
                                                                        temp_settings.local_player.username,
                                                                        temp_settings.local_player.uuid,
                                                                        temp_settings.local_player.display_name);
                                            }
                                        } else {
                                            snprintf(coop_room_code_error, sizeof(coop_room_code_error),
                                                     "Invalid room code format.");
                                        }
                                    }
                                    SDL_free(clipboard);
                                }
                                if (!can_join) {
                                    ImGui::EndDisabled();
                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                        char tooltip_buf[256];
                                        if (join_editor_open) {
                                            snprintf(tooltip_buf, sizeof(tooltip_buf),
                                                     "Close the Template Editor before joining a lobby.");
                                        } else {
                                            snprintf(tooltip_buf, sizeof(tooltip_buf),
                                                     "Apply settings before joining a lobby.");
                                        }
                                        ImGui::SetTooltip("%s", tooltip_buf);
                                    }
                                } else {
                                    if (ImGui::IsItemHovered()) {
                                        char tooltip_buf[256];
                                        snprintf(tooltip_buf, sizeof(tooltip_buf),
                                                 "Paste the room code shared by the host.\n"
                                                 "This sends a join request that the host must accept.");
                                        ImGui::SetTooltip("%s", tooltip_buf);
                                    }
                                }
                            }

                            if (coop_room_code_error[0] != '\0') {
                                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", coop_room_code_error);
                            }

                            // Show error/disconnect status
                            if (g_coop_ctx && (net_state == COOP_NET_ERROR || net_state == COOP_NET_DISCONNECTED)) {
                                ImGui::Spacing();
                                char status_buf[256];
                                coop_net_get_status_msg(g_coop_ctx, status_buf, sizeof(status_buf));
                                ImVec4 sc = (net_state == COOP_NET_ERROR)
                                                ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                                : ImVec4(1.0f, 0.6f, 0.4f, 1.0f);
                                ImGui::TextColored(sc, "%s", status_buf);

                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
                                ImGui::Text("(Co-op Chapter - Connection Error Codes at the end)");
                                ImGui::PopStyleColor();
                                if (ImGui::IsItemHovered()) {
                                    char coop_err_tooltip_buf[512];
                                    snprintf(coop_err_tooltip_buf, sizeof(coop_err_tooltip_buf),
                                             "Opens the Co-op Multiplayer chapter in your browser.\n"
                                             "Expand the chapter and scroll to the bottom for the\n"
                                             "'Connection Error Codes' table to look up the status above.");
                                    ImGui::SetTooltip("%s", coop_err_tooltip_buf);
                                }
                                if (ImGui::IsItemClicked()) {
                                    open_content("https://github.com/LNXSeus/Advancely#co-op-multiplayer");
                                }
                            }
                        }
                    }

                    // --- Waiting Room (host only, shown right above lobby) ---
                    if (g_coop_ctx && net_state == COOP_NET_LISTENING) {
                        CoopJoinRequest pending[COOP_MAX_CLIENTS];
                        int pending_count = coop_net_get_pending_requests(g_coop_ctx, pending, COOP_MAX_CLIENTS);
                        if (pending_count > 0) {
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();
                            ImGui::Text("Waiting Room");
                            ImGui::Spacing();
                            ImGui::BeginChild("WaitingRoom", ImVec2(0, 150), true);
                            for (int i = 0; i < pending_count; i++) {
                                ImGui::PushID(2000 + i);
                                const char *name = pending[i].display_name[0]
                                                       ? pending[i].display_name
                                                       : pending[i].username;
                                ImGui::Text("%s", name);
                                ImGui::SameLine();
                                ImGui::TextDisabled("(%s)", pending[i].username);
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Accept")) {
                                    coop_net_approve_request(g_coop_ctx, pending[i].client_slot);
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Reject")) {
                                    coop_net_reject_request(g_coop_ctx, pending[i].client_slot, "Rejected by host");
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndChild();
                        }
                    }

                    // ============================================================
                    // Lobby Player List (shown for both host and receiver when active)
                    // ============================================================
                    if (g_coop_ctx && (net_state == COOP_NET_LISTENING || net_state == COOP_NET_CONNECTED)) {
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        // Status line with disconnect button
                        {
                            ImVec4 sc = (net_state == COOP_NET_LISTENING)
                                            ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f)
                                            : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
                            if (net_state == COOP_NET_CONNECTED) {
                                ImGui::TextColored(sc, "Connected");
                            } else {
                                ImGui::TextColored(sc, "Lobby Active");
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Disconnect##lobby")) {
                                coop_net_stop(g_coop_ctx);
                                coop_room_code_buf[0] = '\0';
                                // Apply settings to trigger a proper reload
                                SDL_SetAtomicInt(&g_settings_changed, 1);
                                SDL_SetAtomicInt(&g_apply_button_clicked, 1);
                            }
                        }

                        ImGui::Spacing();

                        // Player list. Header inlines the relay room code (when on
                        // relay) so it's visible at a glance: "ABC123 - Players (1/32)".
                        CoopLobbyPlayer lobby[COOP_MAX_LOBBY];
                        int lobby_count = coop_net_get_lobby_players(g_coop_ctx, lobby, COOP_MAX_LOBBY);

                        char active_room_code[16] = "";
                        if (g_coop_ctx && coop_net_is_relay(g_coop_ctx)) {
                            coop_net_get_room_code(g_coop_ctx, active_room_code, sizeof(active_room_code));
                        }

                        char player_header[96];
                        if (active_room_code[0] != '\0') {
                            snprintf(player_header, sizeof(player_header),
                                     "%s - Players (%d/%d)",
                                     active_room_code, lobby_count, COOP_MAX_LOBBY);
                        } else {
                            snprintf(player_header, sizeof(player_header),
                                     "Players (%d/%d)", lobby_count, COOP_MAX_LOBBY);
                        }
                        ImGui::Text("%s", player_header);
                        ImGui::Spacing();

                        ImGui::BeginChild("LobbyPlayerList", ImVec2(0, 200), true);
                        for (int i = 0; i < lobby_count; i++) {
                            ImGui::PushID(3000 + i);
                            const char *name = lobby[i].display_name[0] ? lobby[i].display_name : lobby[i].username;

                            if (lobby[i].is_host) {
                                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", name);
                                ImGui::SameLine();
                                ImGui::TextDisabled("(Host)");
                            } else {
                                ImGui::Text("%s", name);
                                ImGui::SameLine();
                                ImGui::TextDisabled("(Receiver)");

                                // Kick button (host only)
                                if (temp_settings.network_mode == NETWORK_HOST && net_state == COOP_NET_LISTENING) {
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Kick")) {
                                        // Find the client slot by UUID
                                        for (int j = 0; j < COOP_MAX_CLIENTS; j++) {
                                            if (g_coop_ctx->clients[j].active &&
                                                g_coop_ctx->clients[j].handshake_done &&
                                                strcmp(g_coop_ctx->clients[j].uuid, lobby[i].uuid) == 0) {
                                                coop_net_kick_client(g_coop_ctx, j, "Kicked by host");
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }
                } // end identity_complete
            } // end coop_enabled

            // --- Reveal IP Confirmation Popup ---
            {
                ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

                if (ImGui::BeginPopupModal("Reveal IP?##coop", nullptr,
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
                    ImGui::Text("Are you sure you want to reveal your IP address?");
                    ImGui::Spacing();
                    ImGui::TextDisabled("Make sure you are not streaming or screen sharing.");
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    bool enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(
                                             ImGuiKey_KeypadEnter);
                    if (ImGui::Button("Reveal") || enter_pressed) {
                        coop_ip_revealed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buf[128];
                        snprintf(tooltip_buf, sizeof(tooltip_buf),
                                 "Show the IP address in the text field.\n"
                                 "You can also press 'ENTER'.");
                        ImGui::SetTooltip("%s", tooltip_buf);
                    }

                    ImGui::SameLine();

                    bool esc_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
                    if (ImGui::Button("Cancel") || esc_pressed) {
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buf[128];
                        snprintf(tooltip_buf, sizeof(tooltip_buf),
                                 "Keep the IP address hidden.\n"
                                 "You can also press 'ESCAPE'.");
                        ImGui::SetTooltip("%s", tooltip_buf);
                    }

                    ImGui::EndPopup();
                }
            }

            // --- Reveal Relay Password Confirmation Popups ---
            for (int relay_pw_idx = 0; relay_pw_idx < 2; ++relay_pw_idx) {
                const char *popup_id   = relay_pw_idx == 0
                                             ? "Reveal Password?##coop_relay_host"
                                             : "Reveal Password?##coop_relay_recv";
                bool *revealed_ptr     = relay_pw_idx == 0
                                             ? &coop_relay_password_host_revealed
                                             : &coop_relay_password_recv_revealed;

                ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

                if (ImGui::BeginPopupModal(popup_id, nullptr,
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
                    ImGui::Text("Are you sure you want to reveal the room password?");
                    ImGui::Spacing();
                    ImGui::TextDisabled("Make sure you are not streaming or screen sharing.");
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    bool enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                                         ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
                    if (ImGui::Button("Reveal") || enter_pressed) {
                        *revealed_ptr = true;
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SameLine();
                    bool esc_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
                    if (ImGui::Button("Cancel") || esc_pressed) {
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
            }

            // --- Reveal Public IP Confirmation Popup ---
            {
                ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

                if (ImGui::BeginPopupModal("Reveal Public IP?##coop", nullptr,
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
                    ImGui::Text("Are you sure you want to reveal your public IP address?");
                    ImGui::Spacing();
                    ImGui::TextDisabled("Make sure you are not streaming or screen sharing.");
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    bool enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(
                                             ImGuiKey_KeypadEnter);
                    if (ImGui::Button("Reveal") || enter_pressed) {
                        coop_public_ip_revealed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buf[128];
                        snprintf(tooltip_buf, sizeof(tooltip_buf),
                                 "Show the public IP address in the text field.\n"
                                 "You can also press 'ENTER'.");
                        ImGui::SetTooltip("%s", tooltip_buf);
                    }

                    ImGui::SameLine();

                    bool esc_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
                    if (ImGui::Button("Cancel") || esc_pressed) {
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buf[128];
                        snprintf(tooltip_buf, sizeof(tooltip_buf),
                                 "Keep the public IP address hidden.\n"
                                 "You can also press 'ESCAPE'.");
                        ImGui::SetTooltip("%s", tooltip_buf);
                    }

                    ImGui::EndPopup();
                }
            }

            ImGui::EndTabItem();
        } // End of Co-op Tab

        if (ImGui::BeginTabItem("Hotkeys")) {
            hotkey_duplicate_error = false; // Reset each frame; re-evaluated below if counters exist
            ImGui::TextDisabled(
                "Select a template with custom goals using target values different from 0 to adjust their hotkeys here.");
            // --- Hotkey Settings ---

            // This section is only displayed if the current template has custom counters.
            static const char *key_names[] = {
                "None", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S",
                "T",
                "U",
                "V", "W", "X", "Y", "Z",
                "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9",
                "F10",
                "F11", "F12",
                "PrintScreen", "ScrollLock", "Pause", "Insert", "Home", "PageUp", "Delete", "End", "PageDown",
                "Right", "Left", "Down", "Up", "Numlock", "Keypad /", "Keypad *", "Keypad -", "Keypad +",
                "Keypad Enter",
                "Keypad 1", "Keypad 2", "Keypad 3", "Keypad 4", "Keypad 5", "Keypad 6", "Keypad 7", "Keypad 8",
                "Keypad 9",
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
                            strncpy(binding->increment_key, key_names[current_inc_key_idx],
                                    sizeof(binding->increment_key) - 1);
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
                        }
                        if (binding) {
                            strncpy(binding->decrement_key, key_names[current_dec_key_idx],
                                    sizeof(binding->decrement_key) - 1);
                            binding->decrement_key[sizeof(binding->decrement_key) - 1] = '\0';
                        }
                    }
                }

                // --- Duplicate Hotkey Validation ---
                // Collect all active (non-"None") keys and check for duplicates
                for (int i = 0; i < temp_settings.hotkey_count && !hotkey_duplicate_error; ++i) {
                    const char *inc_i = temp_settings.hotkeys[i].increment_key;
                    const char *dec_i = temp_settings.hotkeys[i].decrement_key;
                    bool inc_active = (strcmp(inc_i, "None") != 0);
                    bool dec_active = (strcmp(dec_i, "None") != 0);

                    // Check increment vs decrement within the same binding
                    if (inc_active && dec_active && strcmp(inc_i, dec_i) == 0) {
                        hotkey_duplicate_error = true;
                        break;
                    }

                    // Check against all other bindings
                    for (int j = i + 1; j < temp_settings.hotkey_count; ++j) {
                        const char *inc_j = temp_settings.hotkeys[j].increment_key;
                        const char *dec_j = temp_settings.hotkeys[j].decrement_key;
                        bool inc_j_active = (strcmp(inc_j, "None") != 0);
                        bool dec_j_active = (strcmp(dec_j, "None") != 0);

                        if (inc_active && inc_j_active && strcmp(inc_i, inc_j) == 0) {
                            hotkey_duplicate_error = true;
                            break;
                        }
                        if (inc_active && dec_j_active && strcmp(inc_i, dec_j) == 0) {
                            hotkey_duplicate_error = true;
                            break;
                        }
                        if (dec_active && inc_j_active && strcmp(dec_i, inc_j) == 0) {
                            hotkey_duplicate_error = true;
                            break;
                        }
                        if (dec_active && dec_j_active && strcmp(dec_i, dec_j) == 0) {
                            hotkey_duplicate_error = true;
                            break;
                        }
                    }
                }

                if (hotkey_duplicate_error) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       "Error: Two or more goals share the same hotkey. Each key can only be used once.");
                }
            }

            ImGui::EndTabItem();
        } // End of Hotkeys Tab

        if (ImGui::BeginTabItem("System & Debug")) {
            ImGui::Text("System");

            ImGui::Checkbox("Auto-Check for Updates", &temp_settings.check_for_updates);
            if (ImGui::IsItemHovered()) {
                char auto_update_tooltip_buffer[1024];
                snprintf(auto_update_tooltip_buffer, sizeof(auto_update_tooltip_buffer),
                         "If enabled, Advancely will check for a new version on startup and notify you if one is available.\n"
                         "You can see your current version (vX.X.X) in the title of the main Advancely window.\n"
                         "Through that notification you'll then be able to automatically install the update\n"
                         "for your operating system. You can find more instructions on that popup.");
                ImGui::SetTooltip("%s", auto_update_tooltip_buffer);
            }

            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Developer");

            ImGui::Checkbox("Print Debug To Console", &temp_settings.print_debug_status);
            if (ImGui::IsItemHovered()) {
                char debug_print_tooltip_buffer[1024];
                snprintf(debug_print_tooltip_buffer, sizeof(debug_print_tooltip_buffer),
                         "This toggles printing a detailed progress report to the console after every file update.\n"
                         "Currently it also toggles an FPS counter for the overlay window and debug window for the tracker.\n\n"
                         "IMPORTANT: This can spam the console with a large amount of text if your template files contain many entries.\n\n"
                         "This setting only affects the detailed report.\n"
                         "Progress on goals is only printed if the game sends an update.\n"
                         "General status messages and errors are always printed to the console and saved to advancely_log.txt\n"
                         "and advancely_overlay_log.txt for the overlay.\n"
                         "The log is flushed after every message and reset on startup, making it ideal for diagnosing crashes.\n"
                         "Everything the application prints to a console (like MSYS2 MINGW64) can also be found in advancely_log.txt.");
                ImGui::SetTooltip("%s", debug_print_tooltip_buffer);
            }

            ImGui::EndTabItem();
        } // End of System & Debug Tab

        ImGui::EndTabBar();
    } // Ending of Settings Tabs

    // --- Supporters box (always visible regardless of active tab) ---
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Supporters - Thank you! luvv <3");
    ImGui::BeginChild("SupportersBox", ImVec2(0, 72), true); {
        int sorted[NUM_SUPPORTERS];
        for (int i = 0; i < NUM_SUPPORTERS; ++i) sorted[i] = i;
        for (int i = 0; i < NUM_SUPPORTERS - 1; ++i)
            for (int j = i + 1; j < NUM_SUPPORTERS; ++j)
                if (SUPPORTERS[sorted[j]].amount > SUPPORTERS[sorted[i]].amount) {
                    int tmp = sorted[i];
                    sorted[i] = sorted[j];
                    sorted[j] = tmp;
                }
        for (int i = 0; i < NUM_SUPPORTERS; ++i) {
            char supporter_buf[128];
            snprintf(supporter_buf, sizeof(supporter_buf), "%s  $%.0f", SUPPORTERS[sorted[i]].name,
                     SUPPORTERS[sorted[i]].amount);
            ImGui::TextUnformatted(supporter_buf);
        }
    }
    ImGui::EndChild();
    if (ImGui::IsItemHovered()) {
        char supporter_tip_buf[256];
        snprintf(supporter_tip_buf, sizeof(supporter_tip_buf),
                 "Donate at streamlabs.com/lnxseus/tip and mention\n"
                 "\"Advancely\" to get your name listed here permanently!");
        ImGui::SetTooltip("%s", supporter_tip_buf);
    }
    ImGui::Spacing();

    // Start of Bottom Buttons

    const bool ctrl_s_pressed = !ImGui::IsAnyItemActive() &&
                                (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_LeftSuper)) &&
                                ImGui::IsKeyPressed(ImGuiKey_S);
    const bool enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsAnyItemActive();
    const bool window_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool no_popup_open = !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);

    // Disable "Apply Settings" button on visual editing mode or unsaved template editor changes
    bool visual_editing = t && t->is_visual_layout_editing;
    bool template_unsaved = t && t->template_editor_has_unsaved_changes;
    bool apply_disabled = visual_editing || template_unsaved || coop_host_input_error || hotkey_duplicate_error ||
                          account_validation_error;

    // Apply the changes or pressing Enter or Ctrl/Cmd + S keys in the settings window when NO popup is shown

    if (apply_disabled) ImGui::BeginDisabled();
    bool apply_clicked = ImGui::Button("Apply Settings");
    if (apply_disabled) ImGui::EndDisabled();

    // Apply the changes or pressing Enter or Ctrl/Cmd + S keys in the settings window when NO popup is shown
    if (!apply_disabled && (apply_clicked || ((enter_pressed || ctrl_s_pressed) && window_focused && no_popup_open))) {
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
            // This entire block is new/updated
            bool settings_applied = false;

            // Validate the selected path mode
            if (temp_settings.path_mode == PATH_MODE_MANUAL) {
                if (strlen(temp_settings.manual_saves_path) == 0 || !path_exists(temp_settings.manual_saves_path)) {
                    show_invalid_manual_path_error = true;
                    if (force_open_reason) *force_open_reason = FORCE_OPEN_MANUAL_FAIL;
                } else {
                    show_invalid_manual_path_error = false;
                    settings_applied = true;
                }
            } else if (temp_settings.path_mode == PATH_MODE_FIXED_WORLD) {
                if (strlen(temp_settings.fixed_world_path) == 0 || !path_exists(temp_settings.fixed_world_path)) {
                    show_invalid_manual_path_error = true;
                    if (force_open_reason) *force_open_reason = FORCE_OPEN_MANUAL_FAIL;
                } else {
                    show_invalid_manual_path_error = false;
                    settings_applied = true;
                }
            } else if (temp_settings.path_mode == PATH_MODE_AUTO) {
                char auto_path_buffer[MAX_PATH_LENGTH];
                if (!get_saves_path(auto_path_buffer, MAX_PATH_LENGTH, PATH_MODE_AUTO, nullptr)) {
                    temp_settings.path_mode = PATH_MODE_MANUAL;
                    if (force_open_reason) *force_open_reason = FORCE_OPEN_AUTO_FAIL;
                } else {
                    settings_applied = true;
                }
            } else if (temp_settings.path_mode == PATH_MODE_INSTANCE) {
                // This will always be valid -> No Worlds Found
                settings_applied = true;
            }

            // If any of the modes resulted in a valid configuration, apply the settings.
            if (settings_applied) {
                if (force_open_reason) {
                    *force_open_reason = FORCE_OPEN_NONE; // Clear any startup warnings
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

                // Preserve runtime state that is managed outside the settings UI
                temp_settings.use_manual_layout = app_settings->use_manual_layout;

                // Preserve the coop_players roster — it's managed by the lobby sync in main.cpp,
                // not the settings UI. Without this, Apply Settings wipes the roster to 0 players
                // and the host broadcasts empty (0%) progress.
                temp_settings.coop_player_count = app_settings->coop_player_count;
                memcpy(temp_settings.coop_players, app_settings->coop_players,
                       sizeof(app_settings->coop_players));

                // Copy temp settings to the real settings, save, and trigger a reload
                memcpy(app_settings, &temp_settings, sizeof(AppSettings));
                memcpy(&saved_settings, &temp_settings, sizeof(AppSettings)); // Update clean snapshot
                SDL_SetWindowAlwaysOnTop(t->window, app_settings->tracker_always_on_top);
                settings_save(app_settings, nullptr, SAVE_CONTEXT_ALL);
                SDL_SetAtomicInt(&g_settings_changed, 1); // Trigger a reload
                SDL_SetAtomicInt(&g_apply_button_clicked, 1);

                // Update template sync for connected receivers if hosting
                if (app_settings->network_mode == NETWORK_HOST && g_coop_ctx) {
                    update_coop_template_sync(app_settings);
                    coop_net_broadcast_template_sync(g_coop_ctx);
                }

                if (is_template_change && had_active_hotkeys) {
                    show_hotkey_warning_message = true;
                } else {
                    show_applied_message = true;
                }
            }
        }
    }

    // Hover text for the apply button
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        char apply_button_tooltip_buffer[1024];
        if (visual_editing) {
            snprintf(apply_button_tooltip_buffer, sizeof(apply_button_tooltip_buffer),
                     "Disabled while the Visual Layout Editor is active.\n"
                     "Applying settings reloads the template and breaks active editing.");
        } else if (template_unsaved) {
            snprintf(apply_button_tooltip_buffer, sizeof(apply_button_tooltip_buffer),
                     "Disabled while the Template Editor has unsaved changes.\n"
                     "Save or revert your template changes first, then apply settings.");
        } else if (coop_host_input_error) {
            snprintf(apply_button_tooltip_buffer, sizeof(apply_button_tooltip_buffer),
                     "Disabled due to an invalid IP address or port in the Co-op tab.\n"
                     "Fix the highlighted fields before applying.");
        } else if (hotkey_duplicate_error) {
            snprintf(apply_button_tooltip_buffer, sizeof(apply_button_tooltip_buffer),
                     "Disabled because two or more goals share the same hotkey.\n"
                     "Each key can only be assigned to one action across all goals.");
        } else if (account_validation_error) {
            snprintf(apply_button_tooltip_buffer, sizeof(apply_button_tooltip_buffer),
                     "Disabled because the Account tab has invalid settings.\n"
                     "A valid UUID is required (format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).");
        } else {
            snprintf(apply_button_tooltip_buffer, sizeof(apply_button_tooltip_buffer),
                     "Apply any changes made in this window. You can also press 'ENTER' or 'Ctrl/Cmd + S' to apply.\n"
                     "Changes made to the overlay window will cause the overlay to restart,\n"
                     "which might lead to OBS not capturing the overlay anymore.\n"
                     "It will fail to apply if any warnings are shown.");
        }
        ImGui::SetTooltip("%s", apply_button_tooltip_buffer);
    }

    // If there are unsaved changes, display the indicator
    if (has_unsaved_changes) {
        ImGui::SameLine();
        // Replace the TextColored indicator with a Revert button
        if (ImGui::Button("Revert Changes")) {
            memcpy(&temp_settings, &saved_settings, sizeof(AppSettings));
            coop_identity_status_msg[0] = '\0';
            coop_identity_status_is_error = false;
            coop_ip_revealed = false;
            coop_public_ip_revealed = false;
            coop_room_code_error[0] = '\0';
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
        char tooltip_buffer[4096];

        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Resets all settings (besides window size/position & hotkeys) in this window to their default values.\n"
                 "This does not modify your template files.\n\n"
                 "Defaults:\n"
                 "[Paths & Templates]\n"
                 "  - Path Mode: %d\n"
                 "  - Template/Display Version: %s\n"
                 "  - StatsPerWorld Mod: %s; Hermes Mod: %s\n"
                 "  - Category: %s, Optional Flag: %s, Display Category: %s (lock: %s), Language: Default\n"
                 "[Tracker Visuals]\n"
                 "  - Always On Top: %s; Tracker FPS Limit: %d\n"
                 "  - Goal Visibility: Hide All Completed\n"
                 "  - Section Order: Counters -> %s -> Recipes -> Unlocks -> Stats -> Custom -> Multi-Stage\n"
                 "  - Tracker Vertical Spacing: %.1f px; Custom Section Width: %s (%.0f px)\n"
                 "  - Level of Detail: Sub-Text: %.2f, Main-Text: %.2f, Icons: %.2f\n"
                 "  - Scrollable List Threshold: %d; List Scroll Speed: %.0f px\n"
                 "  - Tracker Font: %s (Main: %.1f pt, Sub: %.1f pt, UI: %.1f pt)\n"
                 "  - Colors: Default Dark Theme\n"
                 "  - Backgrounds: Default: %s, Half-Done: %s, Done: %s\n"
                 "[UI Visuals]\n"
                 "  - Settings/UI Font: %s (%.1f pt)\n"
                 "  - UI Colors: Default Dark Theme\n"
                 "[Overlay]\n"
                 "  - Enable Overlay: %s; Overlay FPS Limit: %d\n"
                 "  - Overlay Text Sections: All Enabled\n"
                 "  - Timer Formatting: Both Disabled\n"
                 "  - Hide Completed Row 3 Goals: %s\n"
                 "  - Sub-Stat Cycle Interval: %.1f s; Overlay Scroll Speed: %.2f\n"
                 "  - Overlay Width: %dpx; Overlay Title Alignment: Left\n"
                 "  - Spacing: Row 1: %.1f px, (%s) Row 2: %.0f px, (%s) Row 3: %.0f px\n"
                 "  - Overlay Font: %s\n"
                 "  - Colors: Default Dark Theme\n"
                 "[System & Debug]\n"
                 "  - Check For Updates: %s\n"
                 "  - Print Debug To Console: %s\n"
                 "  - Show Welcome on Startup: %s; Notes Use Settings Font: %s\n\n"
                 "More found in resources/reference_files/settings.json",

                 DEFAULT_PATH_MODE,
                 DEFAULT_VERSION,
                 DEFAULT_USING_STATS_PER_WORLD_LEGACY ? "Enabled" : "Disabled",
                 DEFAULT_USING_HERMES ? "Enabled" : "Disabled",
                 DEFAULT_CATEGORY, DEFAULT_OPTIONAL_FLAG, DEFAULT_DISPLAY_CATEGORY,
                 DEFAULT_LOCK_CATEGORY_DISPLAY_NAME ? "Enabled" : "Disabled",
                 DEFAULT_TRACKER_ALWAYS_ON_TOP ? "Enabled" : "Disabled",
                 DEFAULT_FPS,
                 advancements_label_plural_uppercase,
                 DEFAULT_TRACKER_VERTICAL_SPACING,
                 DEFAULT_TRACKER_SECTION_CUSTOM_WIDTH_ENABLED ? "Enabled" : "Disabled",
                 DEFAULT_TRACKER_SECTION_ITEM_WIDTH,
                 DEFAULT_LOD_TEXT_SUB_THRESHOLD, DEFAULT_LOD_TEXT_MAIN_THRESHOLD, DEFAULT_LOD_ICON_DETAIL_THRESHOLD,
                 DEFAULT_SCROLLABLE_LIST_THRESHOLD, DEFAULT_TRACKER_LIST_SCROLL_SPEED,
                 DEFAULT_TRACKER_FONT, DEFAULT_TRACKER_FONT_SIZE, DEFAULT_TRACKER_SUB_FONT_SIZE,
                 DEFAULT_TRACKER_UI_FONT_SIZE,
                 DEFAULT_ADV_BG_PATH, DEFAULT_ADV_BG_HALF_DONE_PATH, DEFAULT_ADV_BG_DONE_PATH,
                 DEFAULT_UI_FONT, DEFAULT_UI_FONT_SIZE,
                 DEFAULT_ENABLE_OVERLAY ? "Enabled" : "Disabled",
                 DEFAULT_OVERLAY_FPS,
                 DEFAULT_OVERLAY_ROW3_REMOVE_COMPLETED ? "Enabled" : "Disabled",
                 DEFAULT_OVERLAY_STAT_CYCLE_SPEED, DEFAULT_OVERLAY_SCROLL_SPEED,
                 OVERLAY_DEFAULT_WIDTH,
                 DEFAULT_OVERLAY_ROW1_SPACING,
                 DEFAULT_OVERLAY_ROW2_CUSTOM_SPACING_ENABLED ? "Enabled" : "Disabled",
                 DEFAULT_OVERLAY_ROW2_CUSTOM_SPACING,
                 DEFAULT_OVERLAY_ROW3_CUSTOM_SPACING_ENABLED ? "Enabled" : "Disabled",
                 DEFAULT_OVERLAY_ROW3_CUSTOM_SPACING,
                 DEFAULT_OVERLAY_FONT,
                 DEFAULT_CHECK_FOR_UPDATES ? "Enabled" : "Disabled",
                 DEFAULT_PRINT_DEBUG_STATUS ? "Enabled" : "Disabled",
                 DEFAULT_SHOW_WELCOME_ON_STARTUP ? "Enabled" : "Disabled",
                 DEFAULT_NOTES_USE_ROBOTO ? "Enabled" : "Disabled"
        );
        ImGui::SetTooltip("%s", tooltip_buffer);
    }

    ImGui::SameLine();

    if (apply_disabled) ImGui::BeginDisabled();
    if (ImGui::Button("Restart Advancely")) {
        // 1. Save any pending changes from the settings window first.
        temp_settings.use_manual_layout = app_settings->use_manual_layout;
        temp_settings.coop_player_count = app_settings->coop_player_count;
        memcpy(temp_settings.coop_players, app_settings->coop_players,
               sizeof(app_settings->coop_players));
        memcpy(app_settings, &temp_settings, sizeof(AppSettings));
        settings_save(app_settings, nullptr, SAVE_CONTEXT_ALL);
        saved_settings = temp_settings; // Sync so has_unsaved_changes stays false on future frames
        if (t) t->settings_has_unsaved_changes = false; // Clear immediately so the quit event skips the unsaved popup

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
    // Hover text for the restart button
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        char restart_button_tooltip_buffer[1024];
        if (visual_editing) {
            snprintf(restart_button_tooltip_buffer, sizeof(restart_button_tooltip_buffer),
                     "Disabled while the Visual Layout Editor is active.\n"
                     "Restarting reloads the template and breaks active editing.");
        } else if (template_unsaved) {
            snprintf(restart_button_tooltip_buffer, sizeof(restart_button_tooltip_buffer),
                     "Disabled while the Template Editor has unsaved changes.\n"
                     "Save or revert your template changes first, then restart.");
        } else if (coop_host_input_error) {
            snprintf(restart_button_tooltip_buffer, sizeof(restart_button_tooltip_buffer),
                     "Disabled due to an invalid IP address or port in the Co-op tab.\n"
                     "Fix the highlighted fields before restarting.");
        } else if (hotkey_duplicate_error) {
            snprintf(restart_button_tooltip_buffer, sizeof(restart_button_tooltip_buffer),
                     "Disabled because two or more goals share the same hotkey.\n"
                     "Each key can only be assigned to one action across all goals.");
        } else {
            snprintf(restart_button_tooltip_buffer, sizeof(restart_button_tooltip_buffer),
                     "Saves all current settings and restarts the application.\n"
                     "This is required to apply changes to fonts within the tracker window.");
        }
        ImGui::SetTooltip("%s", restart_button_tooltip_buffer);
    }
    if (apply_disabled) ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("Support Advancely!")) {
        open_content("https://streamlabs.com/lnxseus/tip");
    }

    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[512];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Support the development of Advancely! <3\n\n"
                 "IMPORTANT: Please include the word 'Advancely' in your\n"
                 "donation message to be immortalized on the overlay's\n"
                 "supporter showcase after a completed run and receive a\n"
                 "special role on discord!");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }

    if (roboto_font) {
        ImGui::PopFont();
    }

    ImGui::End();
}
