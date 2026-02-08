// Copyright (c) 2025 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, or use this software
// or its source code in any way without the express written permission of the copyright holder.
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
static void normalize_path(std::string &path) {
    for (char &c: path) {
        if (c == '\\') {
            c = '/';
        }
    }
}

// Gets the absolute path to the application's resources/icons directory
static bool get_icons_start_path(std::string &out_path) {
    char icons_path[MAX_PATH_LENGTH];
    snprintf(icons_path, sizeof(icons_path), "%s/icons/", get_resources_path());
    if (!path_exists(icons_path)) return false;
    out_path = icons_path;
    normalize_path(out_path); // normalize_path is still useful here
    return true;
}

// Gets the absolute path to the application's resources/gui directory
static bool get_gui_start_path(std::string &out_path) {
    char gui_path[MAX_PATH_LENGTH];
    snprintf(gui_path, sizeof(gui_path), "%s/gui/", get_resources_path());
    if (!path_exists(gui_path)) {
        log_message(LOG_ERROR, "[DIALOG UTILS] GUI texture directory not found at: %s\n", gui_path);
        return false;
    }
    out_path = gui_path;
    normalize_path(out_path);
    return true;
}


bool open_icon_file_dialog(char *out_relative_path, size_t max_len) {
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
    for (char &c: native_start_path) {
        if (c == '/') {
            c = '\\';
        }
    }
#endif

#ifdef __APPLE__
    const char *filter_patterns[2] = {"png", "gif"};
#else
    const char *filter_patterns[2] = {"*.png", "*.gif"};
#endif
    const char *selected_path = tinyfd_openFileDialog(
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

// Gets the absolute path to the application's resources/fonts directory
static bool get_fonts_start_path(std::string &out_path) {
    char fonts_path[MAX_PATH_LENGTH];
    snprintf(fonts_path, sizeof(fonts_path), "%s/fonts/", get_resources_path());
    if (!path_exists(fonts_path)) return false;
    out_path = fonts_path;
    normalize_path(out_path);
    return true;
}

bool open_font_file_dialog(char *out_filename, size_t max_len) {
    std::string start_path;
    if (!get_fonts_start_path(start_path)) {
        // Fallback if the robust method fails
        char cwd[MAX_PATH_LENGTH];
        if (!GETCWD(cwd, sizeof(cwd))) return false;
        start_path = std::string(cwd) + "/resources/fonts/";
        normalize_path(start_path);
    }
#ifdef __APPLE__
    const char *filter_patterns[2] = {"ttf", "otf"};
#else
    const char *filter_patterns[2] = {"*.ttf", "*.otf"};
#endif
    const char *selected_path = tinyfd_openFileDialog(
        "Select Font File",
        start_path.c_str(),
        2,
        filter_patterns,
        "Font Files (.ttf, .otf)",
        0
    );

    if (!selected_path) {
        return false; // User cancelled
    }

    // --- Validation Logic ---
    std::string full_path_str = selected_path;
    normalize_path(full_path_str);

    // Check if the selected path is inside the required fonts directory
    if (full_path_str.rfind(start_path, 0) == 0) {
        // rfind with pos=0 checks if string starts with
        // Path is valid, extract just the filename
        const char *filename = strrchr(selected_path, '/');
        if (!filename) filename = strrchr(selected_path, '\\'); // Fallback for Windows paths
        filename = filename ? filename + 1 : selected_path;

        strncpy(out_filename, filename, max_len - 1);
        out_filename[max_len - 1] = '\0';
        return true;
    }

    // If the path was invalid, show an error
    tinyfd_messageBox("Invalid Font Location",
                      "You must select a font from within the applications resources/fonts directory.", "ok", "error",
                      1);
    return false;
}

bool open_gui_texture_dialog(char *out_relative_path, size_t max_len) {
    std::string start_path;
    if (!get_gui_start_path(start_path)) {
        // Fallback if the robust method fails - unlikely for gui/
        char cwd[MAX_PATH_LENGTH];
        if (!GETCWD(cwd, sizeof(cwd))) return false;
        start_path = std::string(cwd) + "/resources/gui/";
        normalize_path(start_path);
        log_message(LOG_ERROR, "[DIALOG UTILS] Falling back to CWD for GUI path: %s\n", start_path.c_str());
    }

    // Use native separators for the dialog itself
    std::string native_start_path = start_path;
#ifdef _WIN32
    for (char &c: native_start_path) { if (c == '/') c = '\\'; }
#endif

#ifdef __APPLE__
    const char *filter_patterns[2] = {"png", "gif"}; // Only allow PNG AND .GIF
#else
    const char *filter_patterns[2] = {"*.png", "*.gif"}; // Only allow PNG AND .GIF
#endif
    const char *selected_path = tinyfd_openFileDialog(
        "Select Background Texture",
        native_start_path.c_str(), // Use native path for dialog
        2,
        filter_patterns,
        "Image Files (.png, .gif)",
        0
    );

    if (!selected_path) {
        return false; // User cancelled
    }

    // --- Validation Logic ---
    std::string full_path_str = selected_path;
    normalize_path(full_path_str); // Normalize back to forward slashes for comparison

    // Check if the selected path is inside the required gui directory
    if (full_path_str.rfind(start_path, 0) == 0) {
        // Check if string starts with start_path
        // Path is valid, extract just the filename (relative path within gui/)
        std::string relative_path = full_path_str.substr(start_path.length());

        strncpy(out_relative_path, relative_path.c_str(), max_len - 1);
        out_relative_path[max_len - 1] = '\0';
        return true;
    }

    // If the path was invalid, show an error
    tinyfd_messageBox("Invalid Texture Location",
                      "You must select a texture from within the applications resources/gui directory.", "ok", "error",
                      1);
    return false;
}
