// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 28.09.2026.
//

#include "settings_preset_import.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "file_utils.h"
#include "logger.h"
#include "tracker.h" // For str_contains_insensitive
#include "imgui_internal.h" // For ImGuiItemFlags_MixedValue

const char *PRESET_PROGRESS_SECTIONS[PRESET_PROGRESS_SECTION_COUNT] = {
    "custom_progress",
    "stat_progress_override",
    "stat_stage_baselines",
};

static const char *PRESET_GROUP_NAMES[PRESET_GROUP_COUNT] = {
    "Paths & Templates",
    "Tracker Visuals",
    "UI Visuals",
    "Overlay",
    "Account",
    "Co-op",
    "Hotkeys",
    "System & Debug",
    "Tracker Window & View",
    "Notes Window",
    "Startup",
    "Progress",
    "Other",
};

struct PresetKeyDef {
    const char *paths;
    PresetGroup group;
    const char *label;
    unsigned flags;
    const char *value_names;
};

static const PresetKeyDef PRESET_KEY_DEFS[] = {
#define X(paths, group, label, flags, value_names) {paths, group, label, flags, value_names},
    PRESET_KEY_LIST(X)
#undef X
};

struct PresetRow {
    std::vector<std::string> paths;
    std::string label;
    std::string path_text;
    PresetGroup group;
    unsigned flags;
    bool changed;
    bool selected;
    std::string before;
    std::string after;
    bool has_color;
    ImVec4 before_color;
    ImVec4 after_color;
};

#define PRESET_IMPORT_POPUP_ID "###preset_import_popup"

static cJSON *s_preset_json = nullptr;
static cJSON *s_current_json = nullptr;
static std::vector<PresetRow> s_rows;
static char s_title[256] = "Load Preset" PRESET_IMPORT_POPUP_ID;
static bool s_open_requested = false;
static bool s_only_changes = true;
static char s_search[256] = "";
static bool s_focus_search = false;
static int s_last_clicked = -1;
static char s_error[256] = "";

static std::vector<std::string> split_paths(const char *paths) {
    std::vector<std::string> out;
    std::string current;
    for (const char *c = paths; *c; ++c) {
        if (*c == '|') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current += *c;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

static const cJSON *json_at(const cJSON *root, const std::string &path) {
    const cJSON *node = root;
    size_t start = 0;
    while (node && start <= path.size()) {
        size_t dot = path.find('.', start);
        std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!cJSON_IsObject(node)) return nullptr;
        node = cJSON_GetObjectItemCaseSensitive(node, key.c_str());
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return node;
}

static void json_set_at(cJSON *root, const std::string &path, cJSON *value) {
    cJSON *parent = root;
    size_t start = 0;
    size_t dot;
    while ((dot = path.find('.', start)) != std::string::npos) {
        std::string key = path.substr(start, dot - start);
        cJSON *child = cJSON_GetObjectItemCaseSensitive(parent, key.c_str());
        if (!cJSON_IsObject(child)) {
            cJSON_DeleteItemFromObjectCaseSensitive(parent, key.c_str());
            child = cJSON_CreateObject();
            cJSON_AddItemToObject(parent, key.c_str(), child);
        }
        parent = child;
        start = dot + 1;
    }
    std::string key = path.substr(start);
    cJSON_DeleteItemFromObjectCaseSensitive(parent, key.c_str());
    cJSON_AddItemToObject(parent, key.c_str(), value);
}

// cJSON_Compare treats a float saved as -1.2 and one saved as -1.2000000476837158 as different.
static bool json_equal(const cJSON *a, const cJSON *b) {
    if (!a || !b) return a == b;
    if (cJSON_IsNumber(a) && cJSON_IsNumber(b)) {
        double x = a->valuedouble, y = b->valuedouble;
        double scale = std::max(1.0, std::max(std::fabs(x), std::fabs(y)));
        return std::fabs(x - y) <= 1e-5 * scale;
    }
    if (cJSON_IsBool(a) && cJSON_IsBool(b)) return cJSON_IsTrue(a) == cJSON_IsTrue(b);
    if (cJSON_IsString(a) && cJSON_IsString(b)) return strcmp(a->valuestring, b->valuestring) == 0;
    if (cJSON_IsNull(a) && cJSON_IsNull(b)) return true;
    if (cJSON_IsArray(a) && cJSON_IsArray(b)) {
        if (cJSON_GetArraySize(a) != cJSON_GetArraySize(b)) return false;
        const cJSON *x = a->child;
        const cJSON *y = b->child;
        for (; x && y; x = x->next, y = y->next) {
            if (!json_equal(x, y)) return false;
        }
        return true;
    }
    if (cJSON_IsObject(a) && cJSON_IsObject(b)) {
        if (cJSON_GetArraySize(a) != cJSON_GetArraySize(b)) return false;
        for (const cJSON *x = a->child; x; x = x->next) {
            if (!x->string || !json_equal(x, cJSON_GetObjectItemCaseSensitive(b, x->string))) return false;
        }
        return true;
    }
    return false;
}

static std::string format_number(double v) {
    char buf[64];
    if (std::fabs(v - std::round(v)) < 1e-6) snprintf(buf, sizeof(buf), "%.0f", v);
    else snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

static bool json_color(const cJSON *node, ImVec4 *out) {
    if (!cJSON_IsObject(node) || cJSON_GetArraySize(node) != 4) return false;
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(node, "r");
    const cJSON *g = cJSON_GetObjectItemCaseSensitive(node, "g");
    const cJSON *b = cJSON_GetObjectItemCaseSensitive(node, "b");
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(node, "a");
    if (!cJSON_IsNumber(r) || !cJSON_IsNumber(g) || !cJSON_IsNumber(b) || !cJSON_IsNumber(a)) return false;
    if (out) {
        *out = ImVec4((float) r->valuedouble / 255.0f, (float) g->valuedouble / 255.0f,
                      (float) b->valuedouble / 255.0f, (float) a->valuedouble / 255.0f);
    }
    return true;
}

static std::string format_node(const cJSON *node, const char *value_names) {
    if (!node) return "(not set)";
    if (cJSON_IsBool(node)) return cJSON_IsTrue(node) ? "On" : "Off";
    if (cJSON_IsNull(node)) return "null";
    if (cJSON_IsNumber(node)) {
        double v = node->valuedouble;
        if (value_names && std::fabs(v - std::round(v)) < 1e-6 && v >= 0.0) {
            std::vector<std::string> names = split_paths(value_names);
            int idx = (int) std::lround(v);
            if (idx < (int) names.size()) return names[idx];
        }
        return format_number(v);
    }
    if (cJSON_IsString(node)) {
        if (node->valuestring[0] == '\0') return "(empty)";
        return std::string("\"") + node->valuestring + "\"";
    }
    if (cJSON_IsObject(node)) {
        if (json_color(node, nullptr)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "(%d, %d, %d, %d)",
                     cJSON_GetObjectItemCaseSensitive(node, "r")->valueint,
                     cJSON_GetObjectItemCaseSensitive(node, "g")->valueint,
                     cJSON_GetObjectItemCaseSensitive(node, "b")->valueint,
                     cJSON_GetObjectItemCaseSensitive(node, "a")->valueint);
            return buf;
        }
        const cJSON *key = cJSON_GetObjectItemCaseSensitive(node, "key");
        const cJSON *mods = cJSON_GetObjectItemCaseSensitive(node, "mods");
        if (cJSON_GetArraySize(node) == 2 && cJSON_IsString(key) && cJSON_IsNumber(mods)) {
            AppHotkey hk = {};
            strncpy(hk.key, key->valuestring, sizeof(hk.key) - 1);
            hk.mods = (Uint16) mods->valueint;
            char buf[96];
            return app_hotkey_display_label(&hk, buf, sizeof(buf));
        }
        const cJSON *x = cJSON_GetObjectItemCaseSensitive(node, "x");
        const cJSON *y = cJSON_GetObjectItemCaseSensitive(node, "y");
        const cJSON *w = cJSON_GetObjectItemCaseSensitive(node, "w");
        const cJSON *h = cJSON_GetObjectItemCaseSensitive(node, "h");
        if (cJSON_GetArraySize(node) == 4 && cJSON_IsNumber(x) && cJSON_IsNumber(y) && cJSON_IsNumber(w) &&
            cJSON_IsNumber(h)) {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s, %s (%s x %s)", x->valueint < 0 ? "auto" : format_number(x->valueint).c_str(),
                     y->valueint < 0 ? "auto" : format_number(y->valueint).c_str(),
                     w->valueint < 0 ? "auto" : format_number(w->valueint).c_str(),
                     h->valueint < 0 ? "auto" : format_number(h->valueint).c_str());
            return buf;
        }
        std::string out = "{";
        for (const cJSON *c = node->child; c; c = c->next) {
            if (c != node->child) out += ", ";
            out += c->string ? c->string : "?";
            out += ": ";
            out += format_node(c, nullptr);
        }
        return out + "}";
    }
    if (cJSON_IsArray(node)) {
        std::string out = "[";
        for (const cJSON *c = node->child; c; c = c->next) {
            if (c != node->child) out += ", ";
            out += format_node(c, nullptr);
        }
        return out + "]";
    }
    return "?";
}

static int count_entries(const cJSON *node) {
    if (!node) return 0;
    if (cJSON_IsObject(node) || cJSON_IsArray(node)) {
        int n = 0;
        for (const cJSON *c = node->child; c; c = c->next) n += count_entries(c);
        return n;
    }
    return 1;
}

// Copy without the objects that hold no entries, so an empty per-player block like {"<uuid>": {}}
// counts the same as no progress at all.
static cJSON *progress_without_empty(const cJSON *node) {
    if (!node || count_entries(node) == 0) return nullptr;
    if (!cJSON_IsObject(node)) return cJSON_Duplicate(node, true);
    cJSON *out = cJSON_CreateObject();
    for (const cJSON *c = node->child; c; c = c->next) {
        cJSON *kept = progress_without_empty(c);
        if (kept) cJSON_AddItemToObject(out, c->string ? c->string : "", kept);
    }
    return out;
}

static bool progress_equal(const cJSON *a, const cJSON *b) {
    cJSON *pa = progress_without_empty(a);
    cJSON *pb = progress_without_empty(b);
    bool equal = json_equal(pa, pb);
    if (pa) cJSON_Delete(pa);
    if (pb) cJSON_Delete(pb);
    return equal;
}

static std::string format_paths(const cJSON *root, const std::vector<std::string> &paths, unsigned flags,
                                const char *value_names) {
    if (flags & PRESET_KEY_PROGRESS) {
        const cJSON *node = json_at(root, paths[0]);
        int n = count_entries(node);
        char buf[64];
        snprintf(buf, sizeof(buf), "%d %s", n, n == 1 ? "entry" : "entries");
        return buf;
    }
    std::string out;
    for (size_t i = 0; i < paths.size(); i++) {
        if (i > 0) out += " / ";
        out += format_node(json_at(root, paths[i]), value_names);
    }
    return out;
}

static void add_row(const std::vector<std::string> &paths, const char *label, PresetGroup group, unsigned flags,
                    const char *value_names) {
    bool in_preset = false;
    bool changed = false;
    for (const auto &p: paths) {
        const cJSON *pre = json_at(s_preset_json, p);
        if (!pre) continue;
        in_preset = true;
        const cJSON *cur = json_at(s_current_json, p);
        if ((flags & PRESET_KEY_PROGRESS) ? !progress_equal(cur, pre) : !json_equal(cur, pre)) changed = true;
    }
    if (!in_preset) return;

    PresetRow row;
    row.paths = paths;
    row.label = label;
    for (size_t i = 0; i < paths.size(); i++) {
        if (i > 0) row.path_text += ", ";
        row.path_text += paths[i];
    }
    row.group = group;
    row.flags = flags;
    row.changed = changed;
    row.selected = !(flags & PRESET_KEY_USER);
    row.before = format_paths(s_current_json, paths, flags, value_names);
    row.after = format_paths(s_preset_json, paths, flags, value_names);
    if (changed && row.after == row.before) row.after += " (different values)";
    row.has_color = paths.size() == 1 &&
                    json_color(json_at(s_current_json, paths[0]), &row.before_color) &&
                    json_color(json_at(s_preset_json, paths[0]), &row.after_color);
    s_rows.push_back(row);
}

static void build_listed_sets(std::unordered_set<std::string> &listed,
                              std::unordered_set<std::string> &listed_parents) {
    for (const auto &def: PRESET_KEY_DEFS) {
        for (const auto &p: split_paths(def.paths)) {
            listed.insert(p);
            for (size_t dot = p.find('.'); dot != std::string::npos; dot = p.find('.', dot + 1)) {
                listed_parents.insert(p.substr(0, dot));
            }
        }
    }
}

static void collect_unlisted(const cJSON *obj, const std::string &prefix,
                             const std::unordered_set<std::string> &listed,
                             const std::unordered_set<std::string> &listed_parents, std::vector<std::string> &out) {
    for (const cJSON *c = obj->child; c; c = c->next) {
        if (!c->string) continue;
        std::string path = prefix.empty() ? std::string(c->string) : prefix + "." + c->string;
        if (listed.count(path)) continue;
        if (listed_parents.count(path) && cJSON_IsObject(c)) {
            collect_unlisted(c, path, listed, listed_parents, out);
            continue;
        }
        out.push_back(path);
    }
}

int preset_key_list_report_unlisted(const AppSettings *settings) {
    cJSON *json = settings_to_json(settings);
    if (!json) {
        log_message(LOG_ERROR, "[PRESET KEYS] Could not build the settings JSON to check PRESET_KEY_LIST.\n");
        return -1;
    }
    std::unordered_set<std::string> listed;
    std::unordered_set<std::string> listed_parents;
    build_listed_sets(listed, listed_parents);
    std::vector<std::string> unlisted;
    collect_unlisted(json, "", listed, listed_parents, unlisted);
    cJSON_Delete(json);

    for (const auto &path: unlisted) {
        log_message(LOG_ERROR, "[PRESET KEYS] settings.json key '%s' is missing from PRESET_KEY_LIST "
                    "(settings_preset_import.h).\n", path.c_str());
    }
    return (int) unlisted.size();
}

static void preset_import_release() {
    if (s_preset_json) cJSON_Delete(s_preset_json);
    if (s_current_json) cJSON_Delete(s_current_json);
    s_preset_json = nullptr;
    s_current_json = nullptr;
    s_rows.clear();
    s_search[0] = '\0';
    s_error[0] = '\0';
    s_last_clicked = -1;
}

bool preset_import_open(const char *preset_path, const char *preset_name, const AppSettings *current) {
    preset_import_release();
    s_preset_json = cJSON_from_file(preset_path);
    if (!s_preset_json || !cJSON_IsObject(s_preset_json)) {
        preset_import_release();
        return false;
    }
    s_current_json = settings_to_json(current);
    if (!s_current_json) {
        preset_import_release();
        return false;
    }

    std::unordered_set<std::string> listed;
    std::unordered_set<std::string> listed_parents;
    build_listed_sets(listed, listed_parents);

    for (const auto &def: PRESET_KEY_DEFS) {
        if (def.flags & PRESET_KEY_SKIP) continue;
        if (def.flags & PRESET_KEY_APP_HOTKEYS) {
            for (int i = 0; i < APP_HOTKEY_COUNT; i++) {
                std::string path = std::string(def.paths) + "." + APP_HOTKEY_DEFS[i].json_id;
                add_row({path}, APP_HOTKEY_DEFS[i].label, def.group, def.flags, nullptr);
            }
            continue;
        }
        add_row(split_paths(def.paths), def.label, def.group, def.flags, def.value_names);
    }
    // Keys the list does not know become their own row, so a setting nobody registered still shows up.
    std::vector<std::string> unlisted;
    collect_unlisted(s_preset_json, "", listed, listed_parents, unlisted);
    for (const auto &path: unlisted) add_row({path}, path.c_str(), PRESET_GROUP_OTHER, 0, nullptr);

    snprintf(s_title, sizeof(s_title), "Load Preset '%s'%s", preset_name, PRESET_IMPORT_POPUP_ID);
    s_only_changes = true;
    s_focus_search = true;
    s_open_requested = true;
    return true;
}

static bool row_matches_search(const PresetRow &row) {
    if (s_search[0] == '\0') return true;
    return str_contains_insensitive(row.label.c_str(), s_search) ||
           str_contains_insensitive(row.path_text.c_str(), s_search) ||
           str_contains_insensitive(PRESET_GROUP_NAMES[row.group], s_search);
}

static void apply_selection(AppSettings *settings, bool out_progress_sections[PRESET_PROGRESS_SECTION_COUNT]) {
    for (int k = 0; k < PRESET_PROGRESS_SECTION_COUNT; k++) out_progress_sections[k] = false;

    cJSON *merged = settings_to_json(settings);
    if (!merged) return;
    for (const auto &row: s_rows) {
        if (!row.selected) continue;
        if (row.flags & PRESET_KEY_PROGRESS) {
            for (int k = 0; k < PRESET_PROGRESS_SECTION_COUNT; k++) {
                if (row.paths[0] == PRESET_PROGRESS_SECTIONS[k]) out_progress_sections[k] = true;
            }
            continue;
        }
        for (const auto &p: row.paths) {
            const cJSON *pre = json_at(s_preset_json, p);
            if (pre) json_set_at(merged, p, cJSON_Duplicate(pre, true));
        }
    }
    settings_load_from_json(settings, merged);
    cJSON_Delete(merged);
}

static std::string truncate_value(const std::string &s) {
    const size_t max_len = 48;
    if (s.size() <= max_len) return s;
    return s.substr(0, max_len - 3) + "...";
}

static void draw_value(const std::string &text, bool has_color, const ImVec4 &color, const ImVec4 &text_color,
                       const char *swatch_id) {
    if (has_color) {
        float h = ImGui::GetTextLineHeight();
        ImGui::ColorButton(swatch_id, color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf,
                           ImVec2(h, h));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    }
    ImGui::TextColored(text_color, "%s", truncate_value(text).c_str());
}

static const char *group_description(PresetGroup g) {
    switch (g) {
        case PRESET_GROUP_TRACKER_VIEW:
            return "The tracker window's position and size and the map camera.\n"
                    "Advancely saves these on its own while you use the tracker.";
        case PRESET_GROUP_NOTES:
            return "Options of the notes window.";
        case PRESET_GROUP_STARTUP:
            return "The welcome window and what Advancely counts across launches.";
        case PRESET_GROUP_PROGRESS:
            return "Manually set progress: custom goal checkboxes and counters, manually\n"
                    "completed stats and stat stage starting points. The preset's copy\n"
                    "replaces yours on 'Apply Settings'.";
        case PRESET_GROUP_OTHER:
            return "Settings in the preset that this list does not know yet.\n"
                    "They are shown by their settings.json name.";
        default:
            return nullptr;
    }
}

bool preset_import_render(AppSettings *settings, bool out_progress_sections[PRESET_PROGRESS_SECTION_COUNT]) {
    if (s_open_requested) {
        ImGui::OpenPopup(s_title);
        s_open_requested = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(s_title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (s_preset_json && !ImGui::IsPopupOpen(s_title)) preset_import_release();
        return false;
    }

    bool loaded = false;

    if ((ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_LeftSuper)) &&
        ImGui::IsKeyPressed(ImGuiKey_F)) {
        s_focus_search = true;
    }

    std::vector<int> visible;
    for (int g = 0; g < PRESET_GROUP_COUNT; g++) {
        for (int i = 0; i < (int) s_rows.size(); i++) {
            const PresetRow &row = s_rows[i];
            if (row.group != g) continue;
            if (s_only_changes && !row.changed) continue;
            if (!row_matches_search(row)) continue;
            visible.push_back(i);
        }
    }

    // --- Left-aligned Controls ---
    if (ImGui::Button("Select All")) {
        for (int i: visible) s_rows[i].selected = true;
    }
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[256];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Checks every setting in the current list.\n\nYou can also Shift+Click to select a range.");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    ImGui::SameLine();
    if (ImGui::Button("Deselect All")) {
        for (int i: visible) s_rows[i].selected = false;
    }
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[256];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Unchecks every setting in the current list.\n\nYou can also Shift+Click to deselect a range.");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    float left_controls_end_x = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

    // --- Right-aligned Controls ---
    const float search_bar_width = 250.0f;
    const float clear_button_width = ImGui::GetFrameHeight();
    const char *only_changes_label = "Only Changes";
    const float only_changes_width = ImGui::CalcTextSize(only_changes_label).x + ImGui::GetFrameHeightWithSpacing();
    const float right_controls_width = search_bar_width + clear_button_width + only_changes_width +
                                       ImGui::GetStyle().ItemSpacing.x * 2;
    float right_controls_start_x = ImGui::GetWindowWidth() - right_controls_width - ImGui::GetStyle().WindowPadding.x;
    if (right_controls_start_x > left_controls_end_x + ImGui::GetStyle().ItemSpacing.x) {
        ImGui::SameLine(right_controls_start_x);
    } else {
        ImGui::SameLine();
    }

    if (ImGui::Checkbox(only_changes_label, &s_only_changes)) s_last_clicked = -1;
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[256];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "CHECKED: List only the settings the preset would change.\n"
                 "UNCHECKED: Also list the ones that already match.");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    ImGui::SameLine();
    if (s_search[0] != '\0') {
        if (ImGui::Button("X##ClearPresetImportSearch", ImVec2(clear_button_width, 0))) {
            s_search[0] = '\0';
            s_focus_search = true;
            s_last_clicked = -1;
        }
    } else {
        ImGui::Dummy(ImVec2(clear_button_width, 0));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(search_bar_width);
    if (s_focus_search) {
        ImGui::SetKeyboardFocusHere();
        s_focus_search = false;
    }
    if (ImGui::InputTextWithHint("##PresetImportSearch", "Search...", s_search, sizeof(s_search))) {
        s_last_clicked = -1;
    }
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[256];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Filter by setting name, settings.json name or tab (case-insensitive).\n"
                 "Press Ctrl+F or Cmd+F to focus.");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    ImGui::Separator();

    // --- Render List ---
    int total_changes = 0;
    for (const auto &row: s_rows) {
        if (row.changed) total_changes++;
    }

    const ImGuiStyle &style = ImGui::GetStyle();
    ImGui::BeginChild("PresetImportScrollingRegion", ImVec2(760, 440), true);
    if (visible.empty()) {
        if (s_search[0] == '\0' && s_only_changes && total_changes == 0) {
            ImGui::Text("This preset matches your current settings.");
        } else {
            ImGui::Text("No settings match the search.");
        }
    } else {
        float max_label_width = 0.0f;
        for (int i: visible) {
            max_label_width = std::max(max_label_width, ImGui::CalcTextSize(s_rows[i].label.c_str()).x);
        }
        const float value_x = style.WindowPadding.x + style.IndentSpacing + ImGui::GetFrameHeight() +
                              style.ItemInnerSpacing.x + max_label_width + 24.0f;
        const ImVec4 old_color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        const ImVec4 new_color = ImVec4(0.5f, 0.85f, 0.5f, 1.0f);

        size_t pos = 0;
        while (pos < visible.size()) {
            const PresetGroup group = s_rows[visible[pos]].group;
            size_t group_end = pos;
            int group_selected = 0;
            int group_changed = 0;
            while (group_end < visible.size() && s_rows[visible[group_end]].group == group) {
                if (s_rows[visible[group_end]].selected) group_selected++;
                if (s_rows[visible[group_end]].changed) group_changed++;
                group_end++;
            }
            const int group_count = (int) (group_end - pos);

            ImGui::PushID((int) group);
            bool all_selected = group_selected == group_count;
            bool mixed = group_selected > 0 && !all_selected;
            if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            if (ImGui::Checkbox(PRESET_GROUP_NAMES[group], &all_selected)) {
                for (size_t k = pos; k < group_end; k++) s_rows[visible[k]].selected = all_selected;
                s_last_clicked = -1;
            }
            if (mixed) ImGui::PopItemFlag();
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[512];
                const char *desc = group_description(group);
                if (desc) {
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer), "%s\n\nChecks or unchecks every setting listed here.",
                             desc);
                } else {
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                             "Settings from the '%s' tab.\n\nChecks or unchecks every setting listed here.",
                             PRESET_GROUP_NAMES[group]);
                }
                ImGui::SetTooltip("%s", tooltip_buffer);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%d %s)", group_changed, group_changed == 1 ? "change" : "changes");

            ImGui::Indent();
            for (size_t k = pos; k < group_end; k++) {
                PresetRow &row = s_rows[visible[k]];
                ImGui::PushID(visible[k]);
                if (ImGui::Checkbox(row.label.c_str(), &row.selected)) {
                    if (ImGui::GetIO().KeyShift && s_last_clicked != -1) {
                        int start = std::min((int) k, s_last_clicked);
                        int end = std::max((int) k, s_last_clicked);
                        for (int j = start; j <= end; ++j) s_rows[visible[j]].selected = row.selected;
                    }
                    s_last_clicked = (int) k;
                }
                if (ImGui::IsItemHovered()) {
                    char tooltip_buffer[2048];
                    snprintf(tooltip_buffer, sizeof(tooltip_buffer), "settings.json: %s\n\nCurrent: %s\nPreset: %s%s",
                             row.path_text.c_str(), row.before.c_str(), row.after.c_str(),
                             (row.flags & PRESET_KEY_USER)
                                 ? "\n\nUnchecked by default: this belongs to you rather than to the preset."
                                 : "");
                    ImGui::SetTooltip("%s", tooltip_buffer);
                }

                ImGui::SameLine(value_x);
                if (row.changed) {
                    draw_value(row.before, row.has_color, row.before_color, old_color, "##before");
                    ImGui::SameLine();
                    ImGui::TextDisabled("->");
                    ImGui::SameLine();
                    draw_value(row.after, row.has_color, row.after_color, new_color, "##after");
                } else {
                    draw_value(row.before, row.has_color, row.before_color, old_color, "##before");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(unchanged)");
                }
                ImGui::PopID();
            }
            ImGui::Unindent();
            ImGui::PopID();
            pos = group_end;
        }
    }
    ImGui::EndChild();

    // --- Bottom Controls ---
    if (s_error[0] != '\0') {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", s_error);
    }
    ImGui::TextDisabled("Unchecked settings keep their current values. Nothing changes until you click "
        "'Apply Settings'.");

    int selected_changes = 0;
    for (const auto &row: s_rows) {
        if (row.changed && row.selected) selected_changes++;
    }

    const bool enter_pressed = !ImGui::IsWindowAppearing() &&
                               (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
    if (ImGui::Button("Load", ImVec2(120, 0)) || enter_pressed) {
        if (selected_changes == 0) {
            snprintf(s_error, sizeof(s_error), "Error: No changed settings are selected.");
        } else {
            apply_selection(settings, out_progress_sections);
            loaded = true;
            preset_import_release();
            ImGui::CloseCurrentPopup();
        }
    }
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[256];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Fill the settings window with the checked settings of the preset.\n(You can also press ENTER)");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        preset_import_release();
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[256];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Keep your current settings and close this window.\n(You can also press ESCAPE)");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }

    // --- Display the counter, aligned to the right ---
    ImGui::SameLine();
    char counter_text[128];
    snprintf(counter_text, sizeof(counter_text), "Selected: %d / %d %s", selected_changes, total_changes,
             total_changes == 1 ? "Change" : "Changes");
    float text_width = ImGui::CalcTextSize(counter_text).x;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - text_width - style.WindowPadding.x);
    ImGui::Text("%s", counter_text);

    ImGui::EndPopup();
    return loaded;
}
