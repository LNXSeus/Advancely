//
// Created by Linus on 14.09.2025.
//

#ifndef DIALOG_UTILS_H
#define DIALOG_UTILS_H

#include <cstddef>

bool open_icon_file_dialog(char* out_relative_path, size_t max_len);

bool open_font_file_dialog(char* out_filename, size_t max_len);

#endif // DIALOG_UTILS_H
