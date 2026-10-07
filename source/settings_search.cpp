// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 07.10.2026.
//

#include "settings_search.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui/imgui_internal.h"

// How the index is built: when the settings window opens it stays hidden (but still lays out its
// widgets) while every tab is shown for one frame. During those frames:
//  - ImGui's test-engine hooks report the label of every labeled widget (IMGUI_ENABLE_TEST_ENGINE is
//    set in CMakeLists.txt; the hooks only run while TestEngineHookItems is on),
//  - ImGui's text logging records what each tab draws, which names the widgets the hooks don't
//    (combos, color pickers, plain text) and holds the text of the rich tooltips,
//  - every settings_tooltip_wanted() returns true, so each tooltip is built and recorded instead
//    of shown.

#define SEARCH_MAX_INDEX_FRAMES 60
#define SEARCH_MAX_RESULTS 100
#define SEARCH_BAR_WIDTH 300.0f
#define SEARCH_DROPDOWN_MIN_WIDTH 480.0f
#define SEARCH_DROPDOWN_MAX_HEIGHT 420.0f

struct SearchEntry {
    std::string tab;
    std::string label;
    std::string tooltip;
    ImGuiID id;
    ImVec2 rel_min; // item rect relative to the settings window's content (for jumping to it)
    ImVec2 rel_max;
};

struct ItemRef {
    ImGuiID id;
    std::string label;
    ImVec2 rel_min;
    ImVec2 rel_max;
};

struct TabRef {
    std::string label;
    ImGuiID id;
};

enum IndexPhase {
    INDEX_IDLE,
    INDEX_WAIT, // the opening frame: lets a tab selection requested on open settle first
    INDEX_RUN, // one tab per frame
    INDEX_RESTORE, // back to the tab that was open
};

static IndexPhase s_phase = INDEX_IDLE;
static int s_index_frames = 0;
static bool s_retry_allowed = false;
static bool s_retry_pending = false;
static std::vector<SearchEntry> s_building;
static std::vector<SearchEntry> s_index;
static std::vector<std::string> s_indexed_tabs;
static std::string s_original_tab;

static std::vector<TabRef> s_tabs;
static ImGuiID s_tab_bar_id = 0;
static std::string s_open_tab; // the tab whose contents were drawn this frame
static std::string s_select_tab; // tab to switch to after this frame's tab bar

static std::string s_capture_tab;
static ImGuiWindow *s_capture_root = nullptr;
static bool s_capture_logging = false;
static int s_item_log_offset = 0;
static ImGuiID s_info_id = 0;
static std::string s_info_label;
static ImRect s_info_rect;
static bool s_use_info_item = false;
static bool s_in_rich = false;
static ItemRef s_rich_item;
static int s_rich_log_offset = 0;

static char s_query[256] = "";
static bool s_focus_query = false;
static bool s_dropdown_suppressed = false;
static bool s_dropdown_hovered = false;
static int s_highlight = 0;
static bool s_scroll_to_highlight = false;

// ---------------------------------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------------------------------

static std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char) s[a])) a++;
    while (b > a && isspace((unsigned char) s[b - 1])) b--;
    return s.substr(a, b - a);
}

static std::string to_lower(const std::string &s) {
    std::string out = s;
    for (char &c: out) c = (char) tolower((unsigned char) c);
    return out;
}

static bool is_useful_label(const std::string &s) {
    int alnum = 0;
    for (char c: s) {
        if (isalnum((unsigned char) c)) alnum++;
    }
    return alnum >= 2;
}

static std::string first_line(const std::string &s) {
    size_t nl = s.find('\n');
    return trim(nl == std::string::npos ? s : s.substr(0, nl));
}

// The visible part of an ImGui label ("Name##id" -> "Name").
static std::string visible_label(const char *label) {
    const char *end = ImGui::FindRenderedTextEnd(label);
    return trim(std::string(label, end));
}

static std::string log_since(int offset) {
    ImGuiContext &g = *GImGui;
    if (!s_capture_logging || offset < 0 || offset > g.LogBuffer.size()) return "";
    return std::string(g.LogBuffer.begin() + offset, g.LogBuffer.end());
}

// The name of a widget from what it logged: the last line, without the "{value}" ImGui logs for
// combos, sliders and drags or the "[x]" of checkboxes.
static std::string label_from_log(const std::string &logged) {
    std::string cleaned;
    int brace_depth = 0;
    for (char c: logged) {
        if (c == '{') brace_depth++;
        else if (c == '}' && brace_depth > 0) brace_depth--;
        else if (brace_depth == 0) cleaned += c;
    }
    std::string last;
    size_t start = 0;
    while (start <= cleaned.size()) {
        size_t nl = cleaned.find('\n', start);
        std::string line = trim(cleaned.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (line.rfind("[x]", 0) == 0 || line.rfind("[ ]", 0) == 0 || line.rfind("[~]", 0) == 0) {
            line = trim(line.substr(3));
        }
        if (!line.empty()) last = line;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    std::string collapsed;
    for (char c: last) {
        if (c == ' ' && !collapsed.empty() && collapsed.back() == ' ') continue;
        collapsed += c;
    }
    return collapsed;
}

// A rich tooltip's logged text, one trimmed line per line, without separator lines.
static std::string rich_text_from_log(const std::string &logged) {
    std::string out;
    size_t start = 0;
    while (start <= logged.size()) {
        size_t nl = logged.find('\n', start);
        std::string line = trim(logged.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        bool only_dashes = !line.empty() && line.find_first_not_of('-') == std::string::npos;
        if (!line.empty() && !only_dashes) {
            if (!out.empty()) out += '\n';
            out += line;
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------
// Index building
// ---------------------------------------------------------------------------------------------------

static void rect_relative_to_root(const ImRect &rect, ImVec2 *out_min, ImVec2 *out_max) {
    ImVec2 origin(0.0f, 0.0f);
    if (s_capture_root) {
        origin = ImVec2(s_capture_root->Pos.x - s_capture_root->Scroll.x,
                        s_capture_root->Pos.y - s_capture_root->Scroll.y);
    }
    *out_min = ImVec2(rect.Min.x - origin.x, rect.Min.y - origin.y);
    *out_max = ImVec2(rect.Max.x - origin.x, rect.Max.y - origin.y);
}

// The widget the tooltip being recorded belongs to.
static ItemRef resolve_item() {
    ItemRef ref;
    if (s_use_info_item) {
        // settings_tooltip_wanted_if(): the tooltip covers several items, named by the labeled one.
        s_use_info_item = false;
        ref.id = s_info_id;
        ref.label = s_info_label;
        rect_relative_to_root(s_info_rect, &ref.rel_min, &ref.rel_max);
        return ref;
    }
    ImGuiContext &g = *GImGui;
    ref.id = g.LastItemData.ID;
    rect_relative_to_root(g.LastItemData.Rect, &ref.rel_min, &ref.rel_max);
    if (ref.id != 0 && ref.id == s_info_id) ref.label = s_info_label;
    else ref.label = label_from_log(log_since(s_item_log_offset));
    return ref;
}

static void record_tooltip(const ItemRef &item, const std::string &text) {
    if (item.id != 0) {
        for (auto it = s_building.rbegin(); it != s_building.rend(); ++it) {
            if (it->tab != s_capture_tab) break;
            if (it->id == item.id) {
                if (it->tooltip.empty()) {
                    it->tooltip = text;
                    return;
                }
                break;
            }
        }
    }
    SearchEntry entry;
    entry.tab = s_capture_tab;
    entry.label = is_useful_label(item.label) ? item.label : first_line(text);
    entry.tooltip = text;
    entry.id = item.id;
    entry.rel_min = item.rel_min;
    entry.rel_max = item.rel_max;
    if (!entry.label.empty()) s_building.push_back(entry);
}

static void begin_tab_capture(const char *label) {
    ImGuiContext &g = *GImGui;
    s_capture_tab = label;
    s_indexed_tabs.push_back(label);
    s_capture_root = g.CurrentWindow ? g.CurrentWindow->RootWindow : nullptr;
    s_info_id = 0;
    s_info_label.clear();
    s_info_rect = ImRect();
    s_use_info_item = false;
    s_item_log_offset = 0;
    s_capture_logging = false;
    if (!g.LogEnabled) {
        // Logging also opens collapsed tree nodes for the frame, so collapsed sections get indexed.
        ImGui::LogToBuffer();
        s_capture_logging = g.LogEnabled;
    }
    g.TestEngineHookItems = true;
}

static void end_tab_capture() {
    if (s_capture_tab.empty()) return;
    ImGuiContext &g = *GImGui;
    if (s_in_rich) {
        ImGui::EndGroup();
        s_in_rich = false;
    }
    if (s_capture_logging) ImGui::LogFinish();
    s_capture_logging = false;
    g.TestEngineHookItems = false;
    s_capture_tab.clear();
    s_capture_root = nullptr;
}

static void finish_index() {
    s_index.clear();
    for (const auto &entry: s_building) {
        bool duplicate = false;
        for (const auto &kept: s_index) {
            if (kept.tab == entry.tab && kept.label == entry.label && kept.tooltip == entry.tooltip) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) s_index.push_back(entry);
    }
    // A labeled widget without a tooltip is dropped when the same name already has one in that tab.
    std::vector<std::string> named_with_tooltip;
    for (const auto &entry: s_index) {
        if (!entry.tooltip.empty()) named_with_tooltip.push_back(entry.tab + '\n' + entry.label);
    }
    s_index.erase(std::remove_if(s_index.begin(), s_index.end(), [&](const SearchEntry &entry) {
        return entry.tooltip.empty() &&
               std::find(named_with_tooltip.begin(), named_with_tooltip.end(), entry.tab + '\n' + entry.label) !=
               named_with_tooltip.end();
    }), s_index.end());
    s_building.clear();
    s_phase = INDEX_IDLE;
    // No tab was drawn (the window opened collapsed): try again once a tab shows up.
    if (s_index.empty() && s_retry_allowed) s_retry_pending = true;
    s_retry_allowed = false;
}

static void queue_tab_selection() {
    if (s_select_tab.empty()) return;
    ImGuiTabBar *bar = s_tab_bar_id ? ImGui::TabBarFindByID(s_tab_bar_id) : nullptr;
    for (const auto &tab: s_tabs) {
        if (tab.label != s_select_tab) continue;
        ImGuiTabItem *item = bar ? ImGui::TabBarFindTabByID(bar, tab.id) : nullptr;
        if (item) ImGui::TabBarQueueFocus(bar, item);
        break;
    }
    s_select_tab.clear();
}

static void start_index() {
    end_tab_capture();
    s_building.clear();
    s_indexed_tabs.clear();
    s_original_tab.clear();
    s_index_frames = 0;
    s_phase = INDEX_WAIT;
}

void settings_search_on_open() {
    s_retry_allowed = true;
    s_retry_pending = false;
    start_index();
}

void settings_search_prepare_window(const char *window_name) {
    if (s_phase == INDEX_IDLE) return;
    ImGuiWindow *window = ImGui::FindWindowByName(window_name);
    // Begin() counts this down by one before using it, so 2 keeps the window hidden for this frame
    // while its widgets are still laid out.
    if (window) window->HiddenFramesCannotSkipItems = 2;
}

bool settings_search_tab_begin(const char *label, ImGuiTabItemFlags flags) {
    end_tab_capture();
    bool open = ImGui::BeginTabItem(label, nullptr, flags);

    ImGuiTabBar *bar = GImGui->CurrentTabBar;
    if (bar && bar->LastTabItemIdx >= 0 && bar->LastTabItemIdx < bar->Tabs.Size) {
        s_tab_bar_id = bar->ID;
        ImGuiID tab_id = bar->Tabs[bar->LastTabItemIdx].ID;
        bool known = false;
        for (auto &tab: s_tabs) {
            if (tab.label == label) {
                tab.id = tab_id;
                known = true;
                break;
            }
        }
        if (!known) s_tabs.push_back({label, tab_id});
    }

    if (open) {
        s_open_tab = label;
        if (s_phase == INDEX_RUN &&
            std::find(s_indexed_tabs.begin(), s_indexed_tabs.end(), label) == s_indexed_tabs.end()) {
            begin_tab_capture(label);
        }
    }
    return open;
}

void settings_search_tabs_end() {
    end_tab_capture();
    const std::string open_tab = s_open_tab;
    s_open_tab.clear();

    switch (s_phase) {
        case INDEX_IDLE:
            if (s_retry_pending && !open_tab.empty()) {
                s_retry_pending = false;
                start_index();
            }
            break;
        case INDEX_WAIT:
            s_phase = INDEX_RUN;
            break;
        case INDEX_RUN: {
            s_index_frames++;
            if (s_original_tab.empty()) s_original_tab = open_tab;
            std::string next;
            for (const auto &tab: s_tabs) {
                if (std::find(s_indexed_tabs.begin(), s_indexed_tabs.end(), tab.label) == s_indexed_tabs.end()) {
                    next = tab.label;
                    break;
                }
            }
            if (!next.empty() && s_index_frames < SEARCH_MAX_INDEX_FRAMES) {
                s_select_tab = next;
            } else {
                s_select_tab = s_original_tab;
                s_phase = INDEX_RESTORE;
            }
            break;
        }
        case INDEX_RESTORE:
            s_index_frames++;
            if (open_tab == s_original_tab || s_original_tab.empty() || s_index_frames >= SEARCH_MAX_INDEX_FRAMES) {
                finish_index();
            }
            break;
    }
    queue_tab_selection();
}

// ---------------------------------------------------------------------------------------------------
// Tooltip helpers
// ---------------------------------------------------------------------------------------------------

bool settings_tooltip_wanted(ImGuiHoveredFlags flags) {
    if (!s_capture_tab.empty()) {
        s_use_info_item = false;
        return true;
    }
    return ImGui::IsItemHovered(flags);
}

bool settings_tooltip_wanted_if(bool hovered) {
    if (!s_capture_tab.empty()) {
        s_use_info_item = true;
        return true;
    }
    return hovered;
}

void settings_tooltip(const char *text) {
    if (s_capture_tab.empty()) {
        ImGui::SetTooltip("%s", text);
        return;
    }
    record_tooltip(resolve_item(), text ? trim(text) : "");
}

bool settings_rich_tooltip_begin() {
    if (s_capture_tab.empty()) return ImGui::BeginTooltip();
    // While indexing, the tooltip's widgets are drawn into the hidden settings window instead, where
    // the log picks up their text.
    s_rich_item = resolve_item();
    s_rich_log_offset = GImGui->LogBuffer.size();
    s_in_rich = true;
    ImGui::BeginGroup();
    return true;
}

void settings_rich_tooltip_end() {
    if (!s_in_rich) {
        ImGui::EndTooltip();
        return;
    }
    ImGui::EndGroup();
    s_in_rich = false;
    record_tooltip(s_rich_item, rich_text_from_log(log_since(s_rich_log_offset)));
}

// ---------------------------------------------------------------------------------------------------
// ImGui test-engine hooks (declared in imgui_internal.h under IMGUI_ENABLE_TEST_ENGINE)
// ---------------------------------------------------------------------------------------------------

void ImGuiTestEngineHook_ItemAdd(ImGuiContext *ctx, ImGuiID id, const ImRect &, const ImGuiLastItemData *) {
    if (!s_capture_logging || id == 0) return;
    s_item_log_offset = ctx->LogBuffer.size();
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext *ctx, ImGuiID id, const char *label, ImGuiItemStatusFlags) {
    if (s_capture_tab.empty() || label == nullptr || id == 0) return;
    ImGuiWindow *window = ctx->CurrentWindow;
    if (window == nullptr || window->RootWindow != s_capture_root || id == window->ID) return;
    std::string text = visible_label(label);
    if (!is_useful_label(text)) return;
    s_info_id = id;
    s_info_label = text;
    s_info_rect = ctx->LastItemData.Rect;

    SearchEntry entry;
    entry.tab = s_capture_tab;
    entry.label = text;
    entry.id = id;
    rect_relative_to_root(s_info_rect, &entry.rel_min, &entry.rel_max);
    s_building.push_back(entry);
}

void ImGuiTestEngineHook_Log(ImGuiContext *, const char *, ...) {
}

const char *ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *, ImGuiID) {
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------
// Search bar + dropdown
// ---------------------------------------------------------------------------------------------------

static std::vector<std::string> query_words() {
    std::vector<std::string> words;
    std::string lower = to_lower(s_query);
    size_t start = 0;
    while (start < lower.size()) {
        size_t space = lower.find(' ', start);
        std::string word = lower.substr(start, space == std::string::npos ? std::string::npos : space - start);
        if (!word.empty()) words.push_back(word);
        if (space == std::string::npos) break;
        start = space + 1;
    }
    return words;
}

// Every word has to appear in the name, tooltip or tab. Name matches come first.
static std::vector<int> find_results(const std::vector<std::string> &words) {
    std::vector<int> name_hits;
    std::vector<int> other_hits;
    for (int i = 0; i < (int) s_index.size(); i++) {
        const SearchEntry &entry = s_index[i];
        std::string label = to_lower(entry.label);
        std::string tooltip = to_lower(entry.tooltip);
        std::string tab = to_lower(entry.tab);
        bool all = true;
        bool all_in_label = true;
        for (const auto &word: words) {
            bool in_label = label.find(word) != std::string::npos;
            if (!in_label) all_in_label = false;
            if (!in_label && tooltip.find(word) == std::string::npos && tab.find(word) == std::string::npos) {
                all = false;
                break;
            }
        }
        if (!all) continue;
        (all_in_label ? name_hits : other_hits).push_back(i);
    }
    name_hits.insert(name_hits.end(), other_hits.begin(), other_hits.end());
    return name_hits;
}

// The tooltip line to show under a result: the first one containing a search word, else the first.
static std::string snippet_for(const SearchEntry &entry, const std::vector<std::string> &words) {
    if (entry.tooltip.empty()) return "";
    size_t start = 0;
    while (start <= entry.tooltip.size()) {
        size_t nl = entry.tooltip.find('\n', start);
        std::string line = entry.tooltip.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string lower = to_lower(line);
        for (const auto &word: words) {
            if (lower.find(word) != std::string::npos) return trim(line);
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return first_line(entry.tooltip);
}

static void choose_result(const SearchEntry &entry) {
    s_select_tab = entry.tab;
    queue_tab_selection();
    s_dropdown_suppressed = true;
    ImGui::ClearActiveID();
}

static void draw_dropdown(const ImVec2 &pos, float width, bool input_active, bool enter_pressed) {
    std::vector<std::string> words = query_words();
    std::vector<int> results = find_results(words);
    const int shown = std::min((int) results.size(), SEARCH_MAX_RESULTS);
    if (s_highlight >= shown) s_highlight = shown > 0 ? shown - 1 : 0;

    if (input_active && shown > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            s_highlight = (s_highlight + 1) % shown;
            s_scroll_to_highlight = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            s_highlight = (s_highlight + shown - 1) % shown;
            s_scroll_to_highlight = true;
        }
    }

    const ImGuiStyle &style = ImGui::GetStyle();
    const float line_h = ImGui::GetTextLineHeight();
    const float row_h = line_h * 2.0f + style.ItemInnerSpacing.y;
    float content_h = shown > 0 ? shown * (row_h + style.ItemSpacing.y) : line_h + style.ItemSpacing.y;
    if ((int) results.size() > shown) content_h += line_h + style.ItemSpacing.y;
    const float height = std::min(content_h + style.WindowPadding.y * 2.0f, SEARCH_DROPDOWN_MAX_HEIGHT);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(width, height));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("##SettingsSearchResults", nullptr, flags);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    s_dropdown_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    int chosen = -1;
    if (shown == 0) {
        ImGui::TextDisabled("No settings match.");
    }
    const ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    for (int r = 0; r < shown; r++) {
        const SearchEntry &entry = s_index[results[r]];
        ImGui::PushID(r);
        const ImVec2 top_left = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##result", r == s_highlight, ImGuiSelectableFlags_None, ImVec2(0.0f, row_h))) {
            chosen = r;
        }
        if (ImGui::IsItemHovered()) {
            if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f) s_highlight = r;
            if (!entry.tooltip.empty()) ImGui::SetTooltip("%s", entry.tooltip.c_str());
        }
        if (r == s_highlight && s_scroll_to_highlight) {
            ImGui::SetScrollHereY(0.5f);
            s_scroll_to_highlight = false;
        }

        const float right = ImGui::GetItemRectMax().x;
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        const float tab_w = ImGui::CalcTextSize(entry.tab.c_str()).x;
        const float label_right = right - tab_w - style.ItemSpacing.x * 2.0f;
        ImGui::RenderTextEllipsis(draw_list, top_left, ImVec2(label_right, top_left.y + line_h), label_right,
                                  entry.label.c_str(), nullptr, nullptr);
        draw_list->AddText(ImVec2(right - tab_w, top_left.y), ImGui::GetColorU32(dim), entry.tab.c_str());
        std::string snippet = snippet_for(entry, words);
        if (!snippet.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, dim);
            const ImVec2 line2(top_left.x + style.IndentSpacing * 0.5f, top_left.y + line_h + style.ItemInnerSpacing.y);
            ImGui::RenderTextEllipsis(draw_list, line2, ImVec2(right, line2.y + line_h), right, snippet.c_str(),
                                      nullptr, nullptr);
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
    if ((int) results.size() > shown) {
        ImGui::TextDisabled("%d more - type more to narrow it down.", (int) results.size() - shown);
    }

    if (chosen < 0 && enter_pressed && shown > 0) chosen = s_highlight;
    ImGui::End();

    if (chosen >= 0) choose_result(s_index[results[chosen]]);
}

void settings_search_bar() {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) &&
        (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_LeftSuper)) &&
        ImGui::IsKeyPressed(ImGuiKey_F)) {
        s_focus_query = true;
    }

    ImGui::SetNextItemWidth(SEARCH_BAR_WIDTH);
    if (s_focus_query) {
        ImGui::SetKeyboardFocusHere();
        s_focus_query = false;
    }
    // ENTER deactivates the field during this call, so it is read from the return value.
    const bool enter_pressed = ImGui::InputTextWithHint("##SettingsSearch", "Search settings...", s_query,
                                                        sizeof(s_query), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited()) {
        s_highlight = 0;
        s_dropdown_suppressed = false;
    }
    const bool input_active = ImGui::IsItemActive();
    if (ImGui::IsItemActivated()) s_dropdown_suppressed = false;
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[512];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Search every setting by its name, its tooltip or its tab (case-insensitive).\n"
                 "Several words must all match. Use the arrow keys and ENTER, or click a result,\n"
                 "to go to its tab.\n"
                 "Press Ctrl+F or Cmd+F to focus.");
        ImGui::SetTooltip("%s", tooltip_buffer);
    }
    const ImVec2 input_min = ImGui::GetItemRectMin();
    const ImVec2 input_max = ImGui::GetItemRectMax();

    if (s_query[0] != '\0') {
        ImGui::SameLine();
        if (ImGui::Button("X##ClearSettingsSearch")) {
            s_query[0] = '\0';
            s_highlight = 0;
            s_focus_query = true;
        }
    }

    const bool show = s_query[0] != '\0' && !s_dropdown_suppressed && s_phase == INDEX_IDLE &&
                      (input_active || enter_pressed || s_dropdown_hovered);
    if (show) {
        const float width = std::max(SEARCH_DROPDOWN_MIN_WIDTH, input_max.x - input_min.x);
        draw_dropdown(ImVec2(input_min.x, input_max.y + 2.0f), width, input_active, enter_pressed);
    } else {
        s_dropdown_hovered = false;
    }
}
