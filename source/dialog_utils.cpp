// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
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
    snprintf(icons_path, sizeof(icons_path), "%s/icons/", get_application_dir());
    if (!path_exists(icons_path)) return false;
    out_path = icons_path;
    normalize_path(out_path); // normalize_path is still useful here
    return true;
}

// Gets the absolute path to the application's resources/gui directory
static bool get_gui_start_path(std::string &out_path) {
    char gui_path[MAX_PATH_LENGTH];
    snprintf(gui_path, sizeof(gui_path), "%s/gui/", get_application_dir());
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
    const char *filter_patterns[4] = {"*.png", "*.gif", "public.png", "com.compuserve.gif"};
    int filter_count = 4;
#else
    const char *filter_patterns[2] = {"*.png", "*.gif"};
    int filter_count = 2;
#endif
    const char *selected_path = tinyfd_openFileDialog(
        "Select an Icon - IMPORTANT: The icon must be inside the resources/icons folder!",
        start_path.c_str(),
        filter_count,
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
    snprintf(fonts_path, sizeof(fonts_path), "%s/fonts/", get_application_dir());
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
    const char *filter_patterns[4] = {"*.ttf", "*.otf", "public.truetype-ttf-font", "public.opentype-font"};
    int filter_count = 4;
#else
    const char *filter_patterns[2] = {"*.ttf", "*.otf"};
    int filter_count = 2;
#endif
    const char *selected_path = tinyfd_openFileDialog(
        "Select Font File",
        start_path.c_str(),
        filter_count,
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

    // Extract just the filename regardless of where it came from
    const char *filename = strrchr(selected_path, '/');
    if (!filename) filename = strrchr(selected_path, '\\');
    filename = filename ? filename + 1 : selected_path;

    // If the file is already inside the fonts directory, use it directly
    if (full_path_str.rfind(start_path, 0) == 0) {
        strncpy(out_filename, filename, max_len - 1);
        out_filename[max_len - 1] = '\0';
        return true;
    }

    // Warn the user the file will be copied before proceeding
    int confirmed = tinyfd_messageBox(
        "Copy Font?",
        "This font is outside the resources/fonts folder and will be copied into it.\n"
        "Note: frequently importing different fonts will accumulate files in that folder.",
        "yesno", "question", 1
    );
    if (confirmed != 1) return false;

    // Otherwise, copy it into the fonts directory so the app can find it
    std::string dest_path = start_path + filename;
    FILE *src = fopen(selected_path, "rb");
    if (!src) {
        tinyfd_messageBox("Error", "Could not open the selected font file.", "ok", "error", 1);
        return false;
    }
    FILE *dst = fopen(dest_path.c_str(), "wb");
    if (!dst) {
        fclose(src);
        tinyfd_messageBox("Error", "Could not copy font into the resources/fonts directory.", "ok", "error", 1);
        return false;
    }
    char copy_buf[4096];
    size_t bytes;
    while ((bytes = fread(copy_buf, 1, sizeof(copy_buf), src)) > 0)
        fwrite(copy_buf, 1, bytes, dst);
    fclose(src);
    fclose(dst);

    strncpy(out_filename, filename, max_len - 1);
    out_filename[max_len - 1] = '\0';
    return true;
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
    const char *filter_patterns[4] = {"*.png", "*.gif", "public.png", "com.compuserve.gif"};
    int filter_count = 4;
#else
    const char *filter_patterns[2] = {"*.png", "*.gif"}; // Only allow PNG AND .GIF
    int filter_count = 2;
#endif
    const char *selected_path = tinyfd_openFileDialog(
        "Select Background Texture",
        native_start_path.c_str(), // Use native path for dialog
        filter_count,
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

    // Extract just the filename regardless of where it came from
    const char *filename = strrchr(selected_path, '/');
    if (!filename) filename = strrchr(selected_path, '\\');
    filename = filename ? filename + 1 : selected_path;

    // If the file is already inside the gui directory, use it directly
    if (full_path_str.rfind(start_path, 0) == 0) {
        std::string relative_path = full_path_str.substr(start_path.length());
        strncpy(out_relative_path, relative_path.c_str(), max_len - 1);
        out_relative_path[max_len - 1] = '\0';
        return true;
    }

    // Warn the user the file will be copied before proceeding
    int confirmed = tinyfd_messageBox(
        "Copy Texture?",
        "This texture is outside the resources/gui folder and will be copied into it.\n"
        "Note: frequently importing different textures will accumulate files in that folder.",
        "yesno", "question", 1
    );
    if (confirmed != 1) return false;

    // Otherwise, copy it into the gui directory so the app can find it
    std::string dest_path = start_path + filename;
    FILE *src = fopen(selected_path, "rb");
    if (!src) {
        tinyfd_messageBox("Error", "Could not open the selected texture file.", "ok", "error", 1);
        return false;
    }
    FILE *dst = fopen(dest_path.c_str(), "wb");
    if (!dst) {
        fclose(src);
        tinyfd_messageBox("Error", "Could not copy texture into the resources/gui directory.", "ok", "error", 1);
        return false;
    }
    char copy_buf[4096];
    size_t bytes;
    while ((bytes = fread(copy_buf, 1, sizeof(copy_buf), src)) > 0)
        fwrite(copy_buf, 1, bytes, dst);
    fclose(src);
    fclose(dst);

    strncpy(out_relative_path, filename, max_len - 1);
    out_relative_path[max_len - 1] = '\0';
    return true;
}

bool open_saves_folder_dialog(char *out_path, size_t max_len) {
    // Start the dialog in the current saves path if it exists, otherwise home dir.
    const char *selected = tinyfd_selectFolderDialog(
        "Select Minecraft Saves Folder",
        nullptr // NULL lets tinyfd start at a sensible default (home dir)
    );

    if (!selected) return false; // User cancelled

    std::string result = selected;
    normalize_path(result);

    // Strip trailing slash for consistency
    if (!result.empty() && result.back() == '/') result.pop_back();

    strncpy(out_path, result.c_str(), max_len - 1);
    out_path[max_len - 1] = '\0';
    return true;
}

bool open_world_folder_dialog(char *out_path, size_t max_len, const char *saves_path) {
    // Start inside the saves folder if provided and valid, so the user is
    // already one click away from their world folders.
    const char *start_dir = nullptr;
    std::string start_dir_native;

    if (saves_path && saves_path[0] != '\0' && path_exists(saves_path)) {
        start_dir_native = saves_path;
#ifdef _WIN32
        for (char &c: start_dir_native) { if (c == '/') c = '\\'; }
#endif
        start_dir = start_dir_native.c_str();
    }

    const char *selected = tinyfd_selectFolderDialog(
        "Select World Folder (must be inside your saves directory)",
        start_dir
    );

    if (!selected) return false; // User cancelled

    std::string result = selected;
    normalize_path(result);

    // Strip trailing slash for consistency
    if (!result.empty() && result.back() == '/') result.pop_back();

    strncpy(out_path, result.c_str(), max_len - 1);
    out_path[max_len - 1] = '\0';
    return true;
}
