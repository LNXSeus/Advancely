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
#include <unordered_set> // For checking duplicates
#include <functional> // For std::function

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

// Helper to check for duplicate root_names in a vector of items
static bool has_duplicate_root_names(const std::vector<EditorTrackableItem> &items, char *error_message_buffer) {
    std::unordered_set<std::string> seen_names;
    for (const auto &item: items) {
        if (item.root_name[0] == '\0') {
            snprintf(error_message_buffer, 256, "Error: An item has an empty root name.");
            return true;
        }
        if (!seen_names.insert(item.root_name).second) {
            snprintf(error_message_buffer, 256, "Error: Duplicate root name found: '%s'", item.root_name);
            return true;
        }
    }
    return false;
}

// Helper to validate that all icon paths in a vector exist
static bool validate_icon_paths(const std::vector<EditorTrackableItem>& items, char* error_message_buffer) {
    for (const auto& item : items) {
        if (item.icon_path[0] == '\0') {
            continue; // Skip empty paths
        }
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, sizeof(full_path), "resources/icons/%s", item.icon_path);
        if (!path_exists(full_path)) {
            snprintf(error_message_buffer, 256, "Error: Icon file not found: '%s'", item.icon_path);
            return false;
        }
    }
    return true;
}

// Helper to parse a simple array like "unlocks" or "custom" from the template JSON
static void parse_editor_trackable_items(cJSON *json_array, std::vector<EditorTrackableItem> &item_vector) {
    item_vector.clear();
    if (!json_array) return;

    cJSON *item_json = nullptr;
    cJSON_ArrayForEach(item_json, json_array) {
        EditorTrackableItem new_item = {}; // Zero-initialize
        cJSON *root_name = cJSON_GetObjectItem(item_json, "root_name");
        cJSON *icon = cJSON_GetObjectItem(item_json, "icon");
        cJSON *target = cJSON_GetObjectItem(item_json, "target");
        cJSON *hidden = cJSON_GetObjectItem(item_json, "hidden");

        if (cJSON_IsString(root_name)) strncpy(new_item.root_name, root_name->valuestring,
                                               sizeof(new_item.root_name) - 1);
        if (cJSON_IsString(icon)) strncpy(new_item.icon_path, icon->valuestring, sizeof(new_item.icon_path) - 1);
        if (cJSON_IsNumber(target)) new_item.goal = target->valueint;
        if (cJSON_IsBool(hidden)) new_item.is_hidden = cJSON_IsTrue(hidden);

        // TODO: Load display_name from lang file

        item_vector.push_back(new_item);
    }
}

// Main function to load a whole template for editing
static bool load_template_for_editing(const char *version, const DiscoveredTemplate &template_info,
                                      EditorTemplate &editor_data, char *status_message_buffer) {
    editor_data.unlocks.clear();
    editor_data.custom_goals.clear();

    char version_filename[64];
    strncpy(version_filename, version, sizeof(version_filename) - 1);
    version_filename[sizeof(version_filename) - 1] = '\0';
    for (char *p = version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char template_path[MAX_PATH_LENGTH];
    snprintf(template_path, sizeof(template_path), "resources/templates/%s/%s/%s_%s%s.json",
             version, template_info.category, version_filename, template_info.category, template_info.optional_flag);

    cJSON *root = cJSON_from_file(template_path);
    if (!root) {
        snprintf(status_message_buffer, 256, "Error: Could not load template file for editing.");
        return false;
    }

    parse_editor_trackable_items(cJSON_GetObjectItem(root, "unlocks"), editor_data.unlocks);
    parse_editor_trackable_items(cJSON_GetObjectItem(root, "custom"), editor_data.custom_goals);
    // TODO: Load "advancements" and other arrays

    cJSON_Delete(root);
    return true;
}

// Helper to serialize a vector of items back into a cJSON array, for unlocks and custom goals
static void serialize_editor_trackable_items(cJSON *parent, const char *key,
                                             const std::vector<EditorTrackableItem> &item_vector) {
    cJSON *array = cJSON_CreateArray();
    for (const auto &item: item_vector) {
        cJSON *item_json = cJSON_CreateObject();
        cJSON_AddStringToObject(item_json, "root_name", item.root_name);
        cJSON_AddStringToObject(item_json, "icon", item.icon_path);
        if (item.goal != 0) {
            // Only add target if it's not 0 (default for unlocks)
            cJSON_AddNumberToObject(item_json, "target", item.goal);
        }
        if (item.is_hidden) {
            cJSON_AddBoolToObject(item_json, "hidden", item.is_hidden);
        }
        cJSON_AddItemToArray(array, item_json);
    }
    cJSON_AddItemToObject(parent, key, array);
}

// Main function to save the in-memory editor data back to a file
static bool save_template_from_editor(const char *version, const DiscoveredTemplate &template_info,
                                      EditorTemplate &editor_data, char *status_message_buffer) {
    char version_filename[64];
    strncpy(version_filename, version, sizeof(version_filename) - 1);
    version_filename[sizeof(version_filename) - 1] = '\0';
    for (char *p = version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char template_path[MAX_PATH_LENGTH];
    snprintf(template_path, sizeof(template_path), "resources/templates/%s/%s/%s_%s%s.json",
             version, template_info.category, version_filename, template_info.category, template_info.optional_flag);

    // Read the existing file to preserve sections we aren't editing yet
    cJSON *root = cJSON_from_file(template_path);
    if (!root) {
        root = cJSON_CreateObject(); // Create a new object if the file is empty or doesn't exist
    }

    // Replace the "unlocks" and "custom" sections with our edited data
    cJSON_DeleteItemFromObject(root, "unlocks");
    cJSON_DeleteItemFromObject(root, "custom");
    serialize_editor_trackable_items(root, "unlocks", editor_data.unlocks);
    serialize_editor_trackable_items(root, "custom", editor_data.custom_goals);

    // Write the modified JSON object back to the file
    FILE *file = fopen(template_path, "w");
    if (file) {
        char *json_str = cJSON_Print(root);
        if (json_str) {
            fputs(json_str, file);
            free(json_str);
        }
        fclose(file);
        // No message here, returns true on success
    } else {
        snprintf(status_message_buffer, 256, "Error: Failed to open template file for writing.");
        cJSON_Delete(root);
        return false;
    }

    cJSON_Delete(root);
    return true;
}


void temp_creator_render_gui(bool *p_open, AppSettings *app_settings, ImFont *roboto_font, Tracker *t) {
    (void) t;

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
    static bool editor_has_unsaved_changes = false;
    static bool show_unsaved_changes_popup = false;
    // static bool show_save_success_message = false; // TODO: Remove
    static std::function<void()> pending_action = nullptr;


    // State for user feedback next to save button in editor view
    enum SaveMessageType { MSG_NONE, MSG_SUCCESS, MSG_ERROR};
    static SaveMessageType save_message_type = MSG_NONE;
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

    // Handle user trying to close the window with unsaved changes
    if (was_open_last_frame && !(*p_open) && editor_has_unsaved_changes) {
        *p_open = true; // Prevent window from closing
        show_unsaved_changes_popup = true;
        pending_action = [&]() { *p_open = false; }; // The pending action is to close the window
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
            // If we are editing and have unsaved changes, trigger the popup.
            if (editing_template && editor_has_unsaved_changes) {
                show_unsaved_changes_popup = true;
                pending_action = [&, i]() {
                    // This action will run if the user saves or discards
                    selected_template_index = i;
                    selected_template_info = discovered_templates[i];
                    load_template_for_editing(creator_version_str, selected_template_info, current_template_data,
                                              status_message);
                    editor_has_unsaved_changes = false;
                };
            }
            // If we are already editing (but have no changes), just load the new template directly.
            else if (editing_template) {
                selected_template_index = i;
                selected_template_info = discovered_templates[i];
                load_template_for_editing(creator_version_str, selected_template_info, current_template_data,
                                          status_message);
            }
            // Otherwise, just select the template without entering the editor.
            else {
                selected_template_index = i;
            }

            // General state changes on any selection
            show_create_new_view = false;
            show_copy_view = false;
            status_message[0] = '\0';
        }

        // TODO: Remove
        // if (ImGui::Selectable(item_label, selected_template_index == i)) {
        //     selected_template_index = i;
        //     show_create_new_view = false; // Hide the "Create New" view when selecting existing template
        //     show_copy_view = false;
        //     status_message[0] = '\0';
        //
        //     // If already in editor mode, reload the data for the new selection
        //     if (editing_template) {
        //         selected_template_info = discovered_templates[i];
        //         load_template_for_editing(creator_version_str, selected_template_info, current_template_data, status_message);
        //     }
        // }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Pane: Actions & Editor View

    ImGui::BeginChild("ActionsView", ImVec2(0, 0));

    if (ImGui::Button("Create New Template")) {
        auto create_action = [&]() {
            show_create_new_view = true;
            show_copy_view = false;
            editing_template = false;
            selected_template_index = -1;
            status_message[0] = '\0';
            new_template_category[0] = '\0';
            new_template_flag[0] = '\0';
        };
        if (editing_template && editor_has_unsaved_changes) {
            show_unsaved_changes_popup = true;
            pending_action = create_action;
        } else {
            create_action();
        }
    }

    ImGui::SameLine();

    ImGui::BeginDisabled(selected_template_index == -1);
    if (ImGui::Button("Edit Template")) {
        if (selected_template_index != -1) {
            editing_template = true; // Only true here
            show_create_new_view = false;
            show_copy_view = false;
            editor_has_unsaved_changes = false; // Reset dirty flag on load


            // Store the info and load the data for the first time
            selected_template_info = discovered_templates[selected_template_index];
            load_template_for_editing(creator_version_str, selected_template_info, current_template_data,
                                      status_message);

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
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Copy the currently selected template. You can modify its version, category or optional flag.");

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
        ImGui::PushID(&selected_template_info); // Use address of info struct for a unique ID

        // Save when creator window is focused
        if (ImGui::Button("Save (Enter)") || (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))) {
            // Reset message state on new save attempt
            save_message_type = MSG_NONE;
            status_message[0] = '\0';
            // Perform all validation checks before saving
            if (has_duplicate_root_names(current_template_data.unlocks, status_message) ||
                has_duplicate_root_names(current_template_data.custom_goals, status_message) ||
                !validate_icon_paths(current_template_data.unlocks, status_message) ||
                !validate_icon_paths(current_template_data.custom_goals, status_message))
            {
                save_message_type = MSG_ERROR;
            } else {
                if (save_template_from_editor(creator_version_str, selected_template_info, current_template_data, status_message)) {
                    editor_has_unsaved_changes = false;
                    save_message_type = MSG_SUCCESS;
                    snprintf(status_message, sizeof(status_message), "Saved!");
                } else {
                    save_message_type = MSG_ERROR; // Save function failed and set error message
                }
            }
        }

        // Render the success or error message next to the button
        if (save_message_type != MSG_NONE) {
            ImGui::SameLine();
            // Green or red
            ImVec4 color = (save_message_type == MSG_SUCCESS) ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            ImGui::TextColored(color, "%s", status_message);
        }

        // Popup window for unsaved changes
        if (show_unsaved_changes_popup) {
            ImGui::OpenPopup("Unsaved Changes");
            show_unsaved_changes_popup = false;
        }

        if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("You have unsaved changes. Do you want to save them?\n\n");
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (save_template_from_editor(creator_version_str, selected_template_info, current_template_data,
                                              status_message)) {
                    editor_has_unsaved_changes = false;
                    if (pending_action) pending_action();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(120, 0))) {
                editor_has_unsaved_changes = false;
                if (pending_action) pending_action();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginTabBar("EditorTabs")) {
            if (ImGui::BeginTabItem("Advancements")) {
                ImGui::Text("Advancements editor coming soon.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Stats")) {
                ImGui::Text("Stats editor coming soon.");
                ImGui::EndTabItem();
            }
            // Only show the Unlocks tab for the specific version
            if (strcmp(creator_version_str, "25w14craftmine") == 0) {
                if (ImGui::BeginTabItem("Unlocks")) {
                    if (ImGui::Button("Add New Unlock")) {
                        current_template_data.unlocks.push_back({});
                        editor_has_unsaved_changes = true;
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    ImGui::Separator();
                    int item_to_remove = -1;
                    for (size_t i = 0; i < current_template_data.unlocks.size(); i++) {
                        ImGui::PushID(i);
                        auto &unlock = current_template_data.unlocks[i];
                        if (ImGui::InputText("Root Name", unlock.root_name, sizeof(unlock.root_name))) {
                            editor_has_unsaved_changes = true;
                            save_message_type = MSG_NONE; // Clear message on new edit
                        }
                        if (ImGui::InputText("Icon Path", unlock.icon_path, sizeof(unlock.icon_path))) {
                            editor_has_unsaved_changes = true;
                            save_message_type = MSG_NONE; // Clear message on new edit
                        }
                        if (ImGui::Checkbox("Hidden", &unlock.is_hidden)) {
                            editor_has_unsaved_changes = true;
                            save_message_type = MSG_NONE; // Clear message on new edit
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Remove")) {
                            item_to_remove = i;
                            editor_has_unsaved_changes = true;
                            save_message_type = MSG_NONE; // Clear message on new edit
                        }
                        ImGui::Separator();
                        ImGui::PopID();
                    }
                    if (item_to_remove != -1) {
                        current_template_data.unlocks.erase(current_template_data.unlocks.begin() + item_to_remove);
                    }
                    ImGui::EndTabItem();
                }
            }
            if (ImGui::BeginTabItem("Custom Goals")) {
                if (ImGui::Button("Add New Custom Goal")) {
                    current_template_data.custom_goals.push_back({});
                    editor_has_unsaved_changes = true;
                    save_message_type = MSG_NONE; // Clear message on new edit
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(Hotkeys are configured in the main Settings window)");
                ImGui::Separator();
                int item_to_remove = -1;
                for (size_t i = 0; i < current_template_data.custom_goals.size(); ++i) {
                    ImGui::PushID(i);
                    auto &goal = current_template_data.custom_goals[i];
                    if (ImGui::InputText("Root Name", goal.root_name, sizeof(goal.root_name))) {
                        editor_has_unsaved_changes = true;
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    if (ImGui::InputText("Icon Path", goal.icon_path, sizeof(goal.icon_path))) {
                        editor_has_unsaved_changes = true;
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    if (ImGui::InputInt("Target Goal", &goal.goal)) {
                        // No values below -1 allowed
                        if (goal.goal < -1) {
                            goal.goal = -1;
                        }
                        editor_has_unsaved_changes = true;
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "0 for a simple toggle, -1 for an infinite counter, >0 for a progress-based counter.");
                    if (ImGui::Checkbox("Hidden", &goal.is_hidden)) {
                        editor_has_unsaved_changes = true;
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove")) {
                        item_to_remove = i;
                        editor_has_unsaved_changes = true;
                        save_message_type = MSG_NONE; // Clear message on new edit
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
                if (item_to_remove != -1) {
                    current_template_data.custom_goals.erase(
                        current_template_data.custom_goals.begin() + item_to_remove);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Multi-Stage Goals")) {
                ImGui::Text("Multi-Stage Goals editor coming soon.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopID();
    } // End of editing_template

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

        ImGui::InputText("Optional Flag", new_template_flag, sizeof(new_template_flag));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "A variant for the category (e.g., '_optimized', '_modded').\nCannot contain spaces or special characters.");

        if (ImGui::Button("Create Files")) {
            if (new_template_version_idx >= 0) {
                char error_msg[256] = "";

                if (validate_and_create_template(creator_version_str, new_template_category, new_template_flag,
                                                 error_msg,
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

    ImGui::EndChild();

    if (roboto_font) {
        ImGui::PopFont();
    }
    ImGui::End();
}
