//
// Created by Linus on 07.09.2025.
//

#include "temp_creator.h"
#include "settings_utils.h"
#include "logger.h"
#include "template_scanner.h"
#include "temp_creator_utils.h"
#include "path_utils.h"
#include "global_event_handler.h"
#include "file_utils.h" // For cJSON_from_file

#include <vector>
#include <string>

// In-memory representation of a template for editing
struct EditorTrackableItem {
    char root_name[192];
    char display_name[192];
    char icon_path[256];
    int goal;
    bool is_hidden;
};

struct EditorTemplate {
    std::vector<EditorTrackableItem> unlocks;
    std::vector<EditorTrackableItem> custom_goals;

    // TODO: Other sections will be added here later
};

void temp_creator_render_gui(bool *p_open, AppSettings *app_settings, ImFont *roboto_font, Tracker *t) {
    (void)t;

    if (!*p_open) {
        // TODO: Remove
        // // Reset the open tracker when the window is closed
        // static bool was_open_last_frame = false;
        // was_open_last_frame = false;
        return;
    }

    // STATE MANAGEMENT
    static DiscoveredTemplate *discovered_templates = nullptr;
    static int discovered_template_count = 0;
    static char last_scanned_version[64] = "";
    static int selected_template_index = -1;

    // State for the creator's independent version selection
    static bool was_open_last_frame = false;
    static int creator_version_idx = -1;
    static char creator_version_str[64] = "";

    // State for the "Create New" view
    static bool show_create_new_view = false;
    static int new_template_version_idx = -1;
    static char new_template_category[MAX_PATH_LENGTH] = "";
    static char new_template_flag[MAX_PATH_LENGTH] = "";

    // State for the "Copy" view
    static bool show_copy_view = false;
    static int copy_template_version_idx = -1;
    static char copy_template_category[MAX_PATH_LENGTH] = "";
    static char copy_template_flag[MAX_PATH_LENGTH] = "";

    // State for the editor view
    static bool editing_template = false;
    static EditorTemplate current_template_data;
    static DiscoveredTemplate selected_template_info;

    // State for user feedback
    static char status_message[256] = "";

    // LOGIC

    // Add state management for window open/close
    const bool just_opened = *p_open && !was_open_last_frame;
    was_open_last_frame = *p_open;

    if (just_opened) {
        // On first open, synchronize with the main app settings
        strncpy(creator_version_str, app_settings->version_str, sizeof(creator_version_str) - 1);
        creator_version_str[sizeof(creator_version_str) - 1] = '\0';
        for (int i = 0; i < VERSION_STRINGS_COUNT; i++) {
            if (strcmp(VERSION_STRINGS[i], creator_version_str) == 0) {
                creator_version_idx = i;
                break;
            }
        }
        last_scanned_version[0] = '\0'; // Force a scan on first open
    }

    // Check if the selected template is the one currently in use
    bool is_current_template = false;
    if (selected_template_index != -1) {
        const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
        // The version is implicitly the same because the scanner uses app_settings->version_str
        if (strcmp(selected.category, app_settings->category) == 0 &&
            strcmp(selected.optional_flag, app_settings->optional_flag) == 0) {
            is_current_template = true;
            }
    }



    // Rescan templates if the creator's version selection changes
    if (strcmp(last_scanned_version, creator_version_str) != 0) {
        free_discovered_templates(&discovered_templates, &discovered_template_count);
        scan_for_templates(creator_version_str, &discovered_templates, &discovered_template_count);
        strncpy(last_scanned_version, creator_version_str, sizeof(last_scanned_version) - 1);
        selected_template_index = -1; // Reset selection
        status_message[0] = '\0'; // Clear status message
    }

    // UI RENDERING

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Template Creator", p_open);

    if (roboto_font) {
        ImGui::PushFont(roboto_font);
    }

    // Version Selector
    ImGui::SetNextItemWidth(250); // Set a reasonable width for the combo box
    if (ImGui::Combo("Version", &creator_version_idx, VERSION_STRINGS, VERSION_STRINGS_COUNT)) {
        if (creator_version_idx >= 0) {
            strncpy(creator_version_str, VERSION_STRINGS[creator_version_idx], sizeof(creator_version_str) - 1);
            creator_version_str[sizeof(creator_version_str) - 1] = '\0';
        }
    }
    ImGui::Separator();

    // Left Pane: Template List
    ImGui::BeginChild("TemplateList", ImVec2(250, 0), true);
    ImGui::Text("Existing Templates");
    ImGui::Separator();

    for (int i = 0; i < discovered_template_count; i++) {
        char item_label[MAX_PATH_LENGTH * 2];
        if (discovered_templates[i].optional_flag[0] != '\0') {
            // With optional flag, display right after category
            snprintf(item_label, sizeof(item_label), "%s%s", discovered_templates[i].category,
                     discovered_templates[i].optional_flag);
        } else {
            // Without optional flag
            snprintf(item_label, sizeof(item_label), "%s", discovered_templates[i].category);
        }

        if (ImGui::Selectable(item_label, selected_template_index == i)) {
            selected_template_index = i;
            show_create_new_view = false; // Hide the "Create New" view when selecting existing template
            show_copy_view = false;
            editing_template = false; // Reset to not editing yet
            status_message[0] = '\0';
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Pane: Actions & Editor View

    ImGui::BeginChild("ActionsView", ImVec2(0, 0));

    if (ImGui::Button("Create New Template")) {
        show_create_new_view = true;
        show_copy_view = false; // Hide copy view
        editing_template = false;
        selected_template_index = -1; // Deselect any existing template
        status_message[0] = '\0';

        // Pre-fill version from main settings
        for (int i = 0; i < VERSION_STRINGS_COUNT; i++) {
            if (strcmp(VERSION_STRINGS[i], app_settings->version_str) == 0) {
                new_template_version_idx = i;
                break;
            }
        }

        // Clear old input
        new_template_category[0] = '\0';
        new_template_flag[0] = '\0';
    }

    ImGui::SameLine();

    ImGui::BeginDisabled(selected_template_index == -1);
    if (ImGui::Button("Edit Template")) {
        if (selected_template_index != -1) {
            editing_template = true; // Only true here
            show_create_new_view = false;
            show_copy_view = false;


            // Store the infor of the template being edited for later use
            selected_template_info = discovered_templates[selected_template_index];

            // TODO: Add logic to load the selected template file into current_template_data
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    // Allow copying of the currently used template
    ImGui::BeginDisabled(selected_template_index == -1);
    if (ImGui::Button("Copy Template")) {
        if (selected_template_index != -1) {
            show_copy_view = true;
            show_create_new_view = false;
            editing_template = false; // Still allow clicking other buttons (e.g., copy, delete, ...) when editing
            status_message[0] = '\0';

            // Pre-fill with selected template's info
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            strncpy(copy_template_category, selected.category, sizeof(copy_template_category) - 1);
            strncpy(copy_template_flag, selected.optional_flag, sizeof(copy_template_flag) - 1);
            // Default copy destination to the currently viewed version
            copy_template_version_idx = creator_version_idx;
        }
    }
    // Add hover text
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy the currently selected template. You can modify its version, category or optional flag.");

    ImGui::EndDisabled();
    ImGui::SameLine();

    // Disable if nothing is selected or the selected template is in use
    ImGui::BeginDisabled(selected_template_index == -1 || is_current_template);
    if (ImGui::Button("Delete Template")) {
        if (selected_template_index != -1) {
            ImGui::OpenPopup("Delete Template?");
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (selected_template_index != -1 && is_current_template) {
            ImGui::SetTooltip("Cannot delete the template currently in use.");
        } else if (selected_template_index != -1) {
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            char tooltip_text[512];
            if (selected.optional_flag[0] != '\0') {
                snprintf(tooltip_text, sizeof(tooltip_text), "Delete template:\nVersion: %s\nCategory: %s\nFlag: %s",
                         creator_version_str, selected.category, selected.optional_flag);
            } else {
                snprintf(tooltip_text, sizeof(tooltip_text), "Delete template:\nVersion: %s\nCategory: %s",
                         creator_version_str, selected.category);
            }
            ImGui::SetTooltip(tooltip_text);
        } else {
            ImGui::SetTooltip("Delete the currently selected template.");
        }
    }

    // Delete Confirmation Popup
    if (ImGui::BeginPopupModal("Delete Template?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (selected_template_index != -1) {
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            ImGui::Text("Are you sure you want to permanently delete this template?\nThis action cannot be undone.");

            // Construct detailed info string
            char template_info[512];
            if (selected.optional_flag[0] != '\0') {
                snprintf(template_info, sizeof(template_info), "Version: %s\nCategory: %s\nFlag: %s",
                         creator_version_str, selected.category, selected.optional_flag);
            } else {
                snprintf(template_info, sizeof(template_info), "Version: %s\nCategory: %s",
                         creator_version_str, selected.category);
            }
            ImGui::TextUnformatted(template_info);
            ImGui::Separator();

            if (ImGui::Button("OK", ImVec2(120, 0))) {
                if (delete_template_files(creator_version_str, selected.category, selected.optional_flag)) {
                    snprintf(status_message, sizeof(status_message), "Template '%s' deleted.", selected.category);
                    SDL_SetAtomicInt(&g_templates_changed, 1); // Signal change
                } else {
                    snprintf(status_message, sizeof(status_message), "Error: Failed to delete template '%s'.",
                             selected.category);
                }
                selected_template_index = -1;
                last_scanned_version[0] = '\0'; // Force rescan
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();


    // "Editing Template" Form

    if (editing_template) {
        // --- CORE EDITOR VIEW ---
        if (ImGui::BeginTabBar("EditorTabs")) {
            if (ImGui::BeginTabItem("Advancements")) {
                ImGui::Text("Advancements editor coming soon.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Stats")) {
                ImGui::Text("Stats editor coming soon.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Unlocks")) {
                ImGui::Text("Manage items in the 'unlocks' array.");
                // TODO: Implement the editor UI for unlocks
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Custom Goals")) {
                ImGui::Text("Manage items in the 'custom' array.");
                // TODO: Implement the editor UI for custom goals
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Multi-Stage Goals")) {
                ImGui::Text("Multi-Stage Goals editor coming soon.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

    }

    // "Create New" Form
    else if (show_create_new_view) {
        ImGui::Text("Create a New Template for %s", creator_version_str);
        ImGui::Spacing();

        // TODO: Remove
        // if (ImGui::Combo("Version", &new_template_version_idx, VERSION_STRINGS, VERSION_STRINGS_COUNT)) {
        //     // Version changed
        // }
        ImGui::InputText("Category Name", new_template_category, sizeof(new_template_category));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "The main classification for the template (e.g., 'all_advancements', 'all_trims').\nCannot contain spaces or special characters.");

        ImGui::InputText("Optional Flag (optional)", new_template_flag, sizeof(new_template_flag));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "A variant for the category (e.g., '_optimized', '_modded').\nCannot contain spaces or special characters.");

        if (ImGui::Button("Create Files")) {
            if (new_template_version_idx >= 0) {
                char error_msg[256] = "";

                if (validate_and_create_template(creator_version_str, new_template_category, new_template_flag, error_msg,
                                                 sizeof(error_msg))) {
                    snprintf(status_message, sizeof(status_message), "Success! Template '%s' created.",
                             new_template_category);
                    show_create_new_view = false;
                    // Force a rescan by clearing the last scanned version
                    SDL_SetAtomicInt(&g_templates_changed, 1); // Signal change
                    last_scanned_version[0] = '\0';
                } else {
                    strncpy(status_message, error_msg, sizeof(status_message) - 1);
                }
            } else {
                strncpy(status_message, "Error: A version must be selected.", sizeof(status_message) - 1);
            }
        }
    }

    // "Copy" Form
    else if (show_copy_view) {
        ImGui::Text("Copy Template");

        ImGui::Spacing();

        if (selected_template_index != -1) {
            const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
            ImGui::Text("Copying from: %s", selected.category);
        }

        if (ImGui::Combo("New Version", &copy_template_version_idx, VERSION_STRINGS, VERSION_STRINGS_COUNT)) {
        }
        ImGui::InputText("New Category Name", copy_template_category, sizeof(copy_template_category));
        ImGui::InputText("New Optional Flag", copy_template_flag, sizeof(copy_template_flag));

        if (ImGui::Button("Confirm Copy")) {
            if (selected_template_index != -1 && copy_template_version_idx >= 0) {
                const DiscoveredTemplate &selected = discovered_templates[selected_template_index];
                const char *dest_version = VERSION_STRINGS[copy_template_version_idx];
                char error_msg[256] = "";

                if (copy_template_files(app_settings->version_str, selected.category, selected.optional_flag,
                                        dest_version, copy_template_category, copy_template_flag,
                                        error_msg, sizeof(error_msg))) {
                    snprintf(status_message, sizeof(status_message), "Success! Template copied to '%s'.",
                             copy_template_category);
                    show_copy_view = false;
                    SDL_SetAtomicInt(&g_templates_changed, 1); // Signal change
                    last_scanned_version[0] = '\0'; // Force rescan
                } else {
                    strncpy(status_message, error_msg, sizeof(status_message) - 1);
                }
            }
        }
    }

    // Display status or error messages
    if (status_message[0] != '\0') {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextWrapped("%s", status_message);
    }

    ImGui::EndChild();

    if (roboto_font) {
        ImGui::PopFont();
    }
    ImGui::End();

}
