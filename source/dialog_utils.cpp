//
// Created by Linus on 14.09.2025.
//

#include "dialog_utils.h"
#include "path_utils.h"
#include "main.h" // For MAX_PATH_LENGTH
#include "tinyfiledialogs.h"
#include <string>
#include <cstring>

#include "logger.h"

#ifdef _WIN32
#include <direct.h>
#define GETCWD _getcwd
#else
#include <unistd.h>
#define GETCWD getcwd
#endif

// Helper to normalize path separators for consistent string searching
static void normalize_path(std::string& path) {
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
}

// Gets the absolute path to the application's resources/icons directory
static bool get_icons_start_path(std::string& out_path) {
    char icons_path[MAX_PATH_LENGTH];
    snprintf(icons_path, sizeof(icons_path), "%s/icons/", get_resources_path());
    if (!path_exists(icons_path)) return false;
    out_path = icons_path;
    normalize_path(out_path); // normalize_path is still useful here
    return true;
}


bool open_icon_file_dialog(char* out_relative_path, size_t max_len) {
    std::string start_path;
    // Try the robust method based on the executable's location first.
    if (!get_icons_start_path(start_path)) {
        // Fallback to Current Working Directory if the robust method fails.
        char cwd[MAX_PATH_LENGTH];
        if (!GETCWD(cwd, sizeof(cwd))) {
            return false;
        }
        start_path = std::string(cwd) + "/resources/icons/";
        normalize_path(start_path);
    }

    // Convert path to native format for the dialog, which is more reliable on Windows.
    std::string native_start_path = start_path;
#ifdef _WIN32
    for (char& c : native_start_path) {
        if (c == '/') {
            c = '\\';
        }
    }
#endif

    const char* filter_patterns[2] = { "*.png", "*.gif" };
    const char* selected_path = tinyfd_openFileDialog(
        "Select an Icon - IMPORTANT: The icon must be inside the resources/icons folder!",
        start_path.c_str(),
        2,
        filter_patterns,
        "Image Files (.png, .gif)",
        0
    );

    if (!selected_path) {
        return false; // User cancelled
    }

    std::string full_path_str = selected_path;
    normalize_path(full_path_str);

    // THIS DOES NOT NEED TO BE CHANGED TO HAVE g_resources_path
    size_t found_pos = full_path_str.find("resources/icons/");
    if (found_pos != std::string::npos) {
        // Extract the path relative to the "icons" folder
        std::string relative_path = full_path_str.substr(found_pos + strlen("resources/icons/"));
        strncpy(out_relative_path, relative_path.c_str(), max_len - 1);
        out_relative_path[max_len - 1] = '\0';
        return true;
    }

    // If the path wasn't inside the project structure, show an error
    tinyfd_messageBox("Error", "Selected icon must be inside the resources/icons folder.", "ok", "error", 1);
    return false;
}
