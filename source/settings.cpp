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
#include <cctype> // For tolower() in preset-name comparison
#include <cstdio> // For remove() when deleting preset files

#include "logger.h"

#include <vector>

#include "dialog_utils.h"
#include "mojang_api.h"
#include "settings_utils.h" // ImGui imported through this
#include "global_event_handler.h" // For global variables
#include "global_hotkeys.h" // For per-row OS registration status in the Hotkeys tab
#include "path_utils.h" // For path_exists()
#include "template_scanner.h"
#include "temp_creator.h"
#include "update_checker.h"
#include "coop_net.h" // For co-op networking status display
#include "skin_cache.h" // For player face textures in lobby roster
#include "file_utils.h" // For cJSON_from_file when reading per-lang display_category
#include <SDL3/SDL_clipboard.h> // For room code copy/paste
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

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

// Preset sections holding per-world progress, not config. AppSettings doesn't carry
// these, so settings_save() leaves them untouched and a preset's captured progress
// would be lost on Apply; copy_preset_progress_to_settings() restores them. Add any
// future non-AppSettings progress sections here.
static const char *PRESET_PROGRESS_SECTIONS[] = {
    "custom_progress",
    "stat_progress_override",
};

// Overwrites the progress sections in settings.json with the given preset's versions.
// Suppresses the settings watcher for the write; the caller drives the reload via
// g_settings_changed. Safe to call right after settings_save().
static void copy_preset_progress_to_settings(const char *preset_path) {
    cJSON *preset_root = cJSON_from_file(preset_path);
    if (!preset_root) return;

    cJSON *settings_root = cJSON_from_file(get_settings_file_path());
    if (!settings_root) {
        cJSON_Delete(preset_root);
        return;
    }

    for (size_t i = 0; i < sizeof(PRESET_PROGRESS_SECTIONS) / sizeof(PRESET_PROGRESS_SECTIONS[0]); i++) {
        const char *section = PRESET_PROGRESS_SECTIONS[i];
        cJSON_DeleteItemFromObject(settings_root, section);
        cJSON *src = cJSON_GetObjectItem(preset_root, section);
        if (src) {
            cJSON_AddItemToObject(settings_root, section, cJSON_Duplicate(src, true));
        }
    }

    SDL_SetAtomicInt(&g_suppress_settings_watch, 1);
    cJSON_write_to_file_atomic(get_settings_file_path(), settings_root);

    cJSON_Delete(settings_root);
    cJSON_Delete(preset_root);
}

// Counts the non-recipe advancements/achievements in the template selected by the
// given settings (version/category/optional_flag). Used to clamp the completion
// advancement-count threshold to a valid maximum. Returns 0 if the template file
// is missing or has no advancements.
static int count_template_advancement_goals(const AppSettings *s) {
    if (!s || s->version_str[0] == '\0' || s->category[0] == '\0') return 0;

    char version_filename[64];
    strncpy(version_filename, s->version_str, sizeof(version_filename) - 1);
    version_filename[sizeof(version_filename) - 1] = '\0';
    for (char *p = version_filename; *p; p++) { if (*p == '.') *p = '_'; }

    char template_path[MAX_PATH_LENGTH];
    snprintf(template_path, sizeof(template_path), "%s/templates/%s/%s/%s_%s%s.json",
             get_resources_path(), s->version_str, s->category,
             version_filename, s->category, s->optional_flag);

    cJSON *json = cJSON_from_file(template_path);
    if (!json) return 0;

    int count = 0;
    cJSON *advancements = cJSON_GetObjectItem(json, "advancements");
    if (advancements && cJSON_IsObject(advancements)) {
        cJSON *adv = nullptr;
        cJSON_ArrayForEach(adv, advancements) {
            cJSON *recipe = cJSON_GetObjectItem(adv, "is_recipe");
            if (!(recipe && cJSON_IsTrue(recipe))) count++;
        }
    }
    cJSON_Delete(json);
    return count;
}

// True when the template picked in this window is the one the tracker currently has loaded.
// A loaded preset (or an edited version/category/flag) points at a different template that is
// only read on Apply, so until then the tracker's template says nothing about which goals the
// pending selection refers to.
static bool settings_template_is_loaded(const AppSettings *s, const Tracker *t) {
    if (!s || !t) return false;

    char base_path[MAX_PATH_LENGTH];
    construct_template_base_path(s, base_path, sizeof(base_path));
    char template_path[MAX_PATH_LENGTH];
    snprintf(template_path, sizeof(template_path), "%s.json", base_path);

    return strcmp(template_path, t->advancement_template_path) == 0;
}

// True if the two Compact-cycle selections differ (which whole-section type counts are enabled and
// which individual goals are selected, in order). Used by both settings-diff functions below.
static bool compact_cycle_different(const AppSettings *a, const AppSettings *b) {
    for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++) {
        if (a->compact_cycle_type[i] != b->compact_cycle_type[i]) return true;
    }
    if (a->compact_cycle_item_count != b->compact_cycle_item_count) return true;
    for (int i = 0; i < a->compact_cycle_item_count; i++) {
        if (a->compact_cycle_items[i].kind != b->compact_cycle_items[i].kind ||
            strcmp(a->compact_cycle_items[i].root_name, b->compact_cycle_items[i].root_name) != 0)
            return true;
    }
    return false;
}

// True if the two Compact pop-out-stack selections differ (which types may pop, what makes each type
// pop, and which individual goals are whitelisted, in order). Used by both settings-diff functions below.
static bool compact_stack_different(const AppSettings *a, const AppSettings *b) {
    for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++) {
        if (a->compact_stack_type[i] != b->compact_stack_type[i]) return true;
        if (a->compact_stack_pop_on_progress[i] != b->compact_stack_pop_on_progress[i]) return true;
    }
    if (a->compact_stack_item_count != b->compact_stack_item_count) return true;
    for (int i = 0; i < a->compact_stack_item_count; i++) {
        if (a->compact_stack_items[i].kind != b->compact_stack_items[i].kind ||
            strcmp(a->compact_stack_items[i].root_name, b->compact_stack_items[i].root_name) != 0)
            return true;
    }
    return false;
}

// The frame an animated icon is on right now, or the static texture when the goal has no .gif.
// Timed off SDL_GetTicks like the tracker's own GIF selection, so both animate in step.
static SDL_Texture *compact_icon_texture(SDL_Texture *tex, const AnimatedTexture *anim) {
    if (anim && anim->frame_count > 0) {
        if (anim->delays && anim->total_duration > 0) {
            Uint32 elapsed = (Uint32) (SDL_GetTicks() % anim->total_duration);
            Uint32 sum = 0;
            for (int i = 0; i < anim->frame_count; i++) {
                sum += anim->delays[i];
                if (elapsed < sum) return anim->frames[i];
            }
        }
        return anim->frames[0];
    }
    return tex;
}

// Height of one icon row, so the list clipper can skip rows without measuring them.
static float compact_icon_row_height() {
    return ImGui::GetTextLineHeight() * 1.5f + ImGui::GetStyle().ItemSpacing.y;
}

// One goal row inside a Compact selection combo: the goal's icon on the left, its text to the right.
// A full-width Selectable is the only layout item, so a click anywhere on the row (icon included)
// toggles it and the cursor advances normally; the icon and text are painted on top through the draw
// list, which keeps them out of the layout entirely (cursor rewinding here would extend the popup's
// bounds and trip ImGui's SetCursorPos error check). `id` only has to be unique within one combo (a
// root_name is), since each combo is its own popup.
static bool compact_icon_selectable(const char *id, const char *text, bool selected,
                                    SDL_Texture *tex, const AnimatedTexture *anim) {
    const float ico = ImGui::GetTextLineHeight() * 1.5f;
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    char sel_id[320];
    snprintf(sel_id, sizeof(sel_id), "##%s", id);
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::Selectable(sel_id, selected, ImGuiSelectableFlags_NoAutoClosePopups,
                                     ImVec2(0.0f, ico));
    if (ImGui::IsItemVisible()) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        SDL_Texture *draw_tex = compact_icon_texture(tex, anim);
        if (draw_tex)
            dl->AddImage((ImTextureID) draw_tex, row_min, ImVec2(row_min.x + ico, row_min.y + ico));
        // Goals with no icon keep the same text column, so the names stay lined up.
        dl->AddText(ImVec2(row_min.x + ico + gap, row_min.y + (ico - ImGui::GetTextLineHeight()) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_Text), text);
    }
    return clicked;
}

// Points at one of the two Compact goal-selection models (panel cycle or pop-out stack) so the
// shared selection UI below can edit either.
struct CompactSelTarget {
    bool *type; // COMPACT_COUNTER_TYPE_COUNT bools: which whole-section types are on
    CompactCycleItem *items; // individually selected goals
    int *count; // number of valid entries in items
};

// Renders the shared Compact goal-selection UI: a "goal types" multiselect over every type present
// in the loaded template plus one individual-goal combo per applicable category, editing `tgt` in
// place. Used by both the panel cycle and the pop-out stack. `suffix` disambiguates ImGui IDs
// between the two callers; `type_anchor`/`item_anchor` are the caller's own Shift+Click anchors;
// `is_cycle` picks cycle-vs-stack wording (and the cycle's "at least one must stay selected" note).
// Presence/label rules match compact_compute_type_counters and the tracker's section separators, so
// `cc` has to come from it with the caller's own hidden rule: real totals for the cycle, hidden-aware
// (`show_hidden`) for the stack, which only lists what can really pop.
static void compact_selection_ui(const char *suffix, const TemplateData *ctd, const CompactCounter *cc,
                                 bool modern, const char *types_label, CompactSelTarget tgt,
                                 int *type_anchor, int *item_anchor, bool is_cycle, bool show_hidden) {
    // A goal hidden in the template is normally not listed here; the "Show Hidden Goals" overlay
    // option surfaces it so it can be selected. hidden_now() folds that in (true = treat as hidden).
    auto hidden_now = [&](bool h) { return h && !show_hidden; };
    // --- Goal types (multiselect) ---
    // The panel cycle lists every present type (each is one big "label over count" cycle entry). The
    // pop-out stack lists ONLY the "pickerless" whole-goal types (advancements, recipes, unlocks) -
    // every other kind is chosen per goal in its own dropdown below, so listing it here too would
    // just be a confusing duplicate.
    auto type_shown = [&](int i) -> bool {
        if (cc[i].total <= 0) return false;
        if (is_cycle) return true;
        return i == COMPACT_COUNTER_ADVANCEMENTS || i == COMPACT_COUNTER_RECIPES ||
               i == COMPACT_COUNTER_UNLOCKS;
    };
    int sel_type_count = 0;
    for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++)
        if (tgt.type[i] && type_shown(i)) sel_type_count++;
    char types_preview[48];
    snprintf(types_preview, sizeof(types_preview), "%d selected", sel_type_count);
    if (ImGui::BeginCombo(types_label, types_preview)) {
        bool any_present = false;
        for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++) {
            if (!type_shown(i)) continue;
            any_present = true;
            bool sel = tgt.type[i];
            char row[64];
            snprintf(row, sizeof(row), "%s (%d/%d)", cc[i].label, cc[i].completed, cc[i].total);
            if (ImGui::Selectable(row, sel, ImGuiSelectableFlags_NoAutoClosePopups)) {
                bool new_state = !sel;
                if (ImGui::GetIO().KeyShift && *type_anchor >= 0 && *type_anchor != i) {
                    int lo = *type_anchor < i ? *type_anchor : i;
                    int hi = *type_anchor < i ? i : *type_anchor;
                    for (int k = lo; k <= hi; k++)
                        if (type_shown(k)) tgt.type[k] = new_state;
                } else {
                    tgt.type[i] = new_state;
                }
                *type_anchor = i;
            }
        }
        if (!any_present) ImGui::TextDisabled("No goal types in this template.");
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        char tip[800];
        if (is_cycle)
            snprintf(tip, sizeof(tip),
                     "Each checked goal type adds one big \"label over count\" entry to the panel's\n"
                     "cycle. Only types present in this template are listed. The totals are the real\n"
                     "counts, including goals hidden from the overlay. Shift+Click to range-select.\n"
                     "At least one entry across these and the individual-goal dropdowns must stay selected.\n"
                     "Default: Advancements / Achievements only.");
        else
            snprintf(tip, sizeof(tip),
                     "Whole-goal types that pop into the stack when they complete. Only kinds without their\n"
                     "own dropdown are listed here (simple advancements, simple recipes, unlocks). A SIMPLE\n"
                     "advancement / recipe has no trackable criteria; complex ones are chosen per goal in the\n"
                     "dropdowns below (so they pop once, via their criteria, instead of twice). Criteria,\n"
                     "stats, custom goals, multi-stage goals and counters are likewise chosen there.\n"
                     "The totals count only goals that can actually pop: goals hidden in the template are\n"
                     "left out unless \"Show Hidden Goals\" is on, and a type with none left is not listed.\n"
                     "Shift+Click to range-select. Default: Simple Advancements.");
        ImGui::SetTooltip("%s", tip);
    }
    // Select All / Deselect All for the type list (only affects the types actually shown).
    ImGui::SameLine();
    char type_all_id[32], type_none_id[32];
    snprintf(type_all_id, sizeof(type_all_id), "All##%stypeall", suffix);
    snprintf(type_none_id, sizeof(type_none_id), "None##%stypenone", suffix);
    if (ImGui::SmallButton(type_all_id))
        for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++)
            if (type_shown(i)) tgt.type[i] = true;
    ImGui::SameLine();
    if (ImGui::SmallButton(type_none_id))
        for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++)
            if (type_shown(i)) tgt.type[i] = false;

    // --- Individual goals, one combo per applicable category ---
    auto item_index = [&](OverlayCompactCounterType kind, const char *root) -> int {
        for (int i = 0; i < *tgt.count; i++)
            if (tgt.items[i].kind == kind && strcmp(tgt.items[i].root_name, root) == 0)
                return i;
        return -1;
    };
    auto item_toggle = [&](OverlayCompactCounterType kind, const char *root) {
        int idx = item_index(kind, root);
        if (idx >= 0) {
            for (int j = idx; j < *tgt.count - 1; j++) tgt.items[j] = tgt.items[j + 1];
            (*tgt.count)--;
        } else if (*tgt.count < MAX_COMPACT_CYCLE_ITEMS) {
            CompactCycleItem *ci = &tgt.items[(*tgt.count)++];
            ci->kind = kind;
            strncpy(ci->root_name, root, sizeof(ci->root_name) - 1);
            ci->root_name[sizeof(ci->root_name) - 1] = '\0';
        }
    };
    auto item_set = [&](OverlayCompactCounterType kind, const char *root, bool on) {
        if ((item_index(kind, root) >= 0) != on) item_toggle(kind, root);
    };
    // The root_name of the listed item at template index i for `kind`, or nullptr if i isn't a valid
    // (present, non-hidden, matching) row - mirrors item_combo's filters. Used for Shift+Click ranges.
    auto item_root_at = [&](OverlayCompactCounterType kind, int i) -> const char * {
        if (kind == COMPACT_COUNTER_CRITERIA || kind == COMPACT_COUNTER_RECIPE_CRITERIA) {
            if (i < ctd->advancement_count) {
                TrackableCategory *a = ctd->advancements[i];
                bool want_recipe = (kind == COMPACT_COUNTER_RECIPE_CRITERIA);
                if (a && a->is_recipe == want_recipe && a->criteria_count > 0 && !hidden_now(a->is_hidden))
                    return a->root_name;
            }
        } else if (kind == COMPACT_COUNTER_STATS) {
            if (i < ctd->stat_count) {
                TrackableCategory *st = ctd->stats[i];
                if (st && st->is_single_stat_category && !hidden_now(st->is_hidden) && st->criteria_count >= 1 &&
                    st->criteria[0] && (st->criteria[0]->goal > 0 || st->criteria[0]->goal == -1))
                    return st->root_name;
            }
        } else if (kind == COMPACT_COUNTER_SUB_STATS) {
            if (i < ctd->stat_count) {
                TrackableCategory *st = ctd->stats[i];
                if (st && !st->is_single_stat_category && !hidden_now(st->is_hidden)) return st->root_name;
            }
        } else if (kind == COMPACT_COUNTER_CUSTOM) {
            if (i < ctd->custom_goal_count) {
                TrackableItem *cg = ctd->custom_goals[i];
                if (cg && !hidden_now(cg->is_hidden)) return cg->root_name;
            }
        } else if (kind == COMPACT_COUNTER_MULTISTAGE) {
            if (i < ctd->multi_stage_goal_count) {
                MultiStageGoal *g = ctd->multi_stage_goals[i];
                if (g && !hidden_now(g->is_hidden)) return g->root_name;
            }
        } else if (kind == COMPACT_COUNTER_COUNTERS) {
            if (i < ctd->counter_goal_count) {
                CounterGoal *cg = ctd->counter_goals[i];
                if (cg && !hidden_now(cg->is_hidden)) return cg->root_name;
            }
        }
        return nullptr;
    };
    auto item_click = [&](OverlayCompactCounterType kind, int i, const char *root, bool selected) {
        bool new_state = !selected;
        int &anchor = item_anchor[kind];
        if (ImGui::GetIO().KeyShift && anchor >= 0 && anchor != i) {
            int lo = anchor < i ? anchor : i;
            int hi = anchor < i ? i : anchor;
            for (int k = lo; k <= hi; k++) {
                const char *kr = item_root_at(kind, k);
                if (kr) item_set(kind, kr, new_state);
            }
        } else {
            item_set(kind, root, new_state);
        }
        anchor = i;
    };
    // Far enough to cover whichever template array a kind maps to (item_root_at range-checks).
    int scan_max = ctd->advancement_count;
    if (ctd->stat_count > scan_max) scan_max = ctd->stat_count;
    if (ctd->custom_goal_count > scan_max) scan_max = ctd->custom_goal_count;
    if (ctd->multi_stage_goal_count > scan_max) scan_max = ctd->multi_stage_goal_count;
    if (ctd->counter_goal_count > scan_max) scan_max = ctd->counter_goal_count;

    // Select or deselect every present, non-hidden goal of a kind (the Select All / None buttons).
    auto select_all_kind = [&](OverlayCompactCounterType kind, bool on) {
        for (int i = 0; i < scan_max; i++) {
            const char *r = item_root_at(kind, i);
            if (r) item_set(kind, r, on);
        }
    };

    // Renders one row of a category combo: the goal at template index `i`, which item_root_at has
    // already vetted as present, non-hidden and of this kind, so the pointers here are known good.
    auto render_row = [&](OverlayCompactCounterType kind, int i) {
        if (kind == COMPACT_COUNTER_CRITERIA || kind == COMPACT_COUNTER_RECIPE_CRITERIA) {
            TrackableCategory *a = ctd->advancements[i];
            bool s = item_index(kind, a->root_name) >= 0;
            char row[224];
            snprintf(row, sizeof(row), "%s (%d/%d)", a->display_name[0] ? a->display_name : a->root_name,
                     a->completed_criteria_count, a->criteria_progress_total);
            if (compact_icon_selectable(a->root_name, row, s, a->texture, a->anim_texture))
                item_click(kind, i, a->root_name, s);
        } else if (kind == COMPACT_COUNTER_STATS) {
            TrackableCategory *st = ctd->stats[i];
            int goal = st->criteria[0]->goal;
            bool s = item_index(kind, st->root_name) >= 0;
            const char *nm = st->display_name[0] ? st->display_name : st->root_name;
            char row[224];
            if (goal > 0) snprintf(row, sizeof(row), "%s (%d/%d)", nm, st->criteria[0]->progress, goal);
            else snprintf(row, sizeof(row), "%s (%d)", nm, st->criteria[0]->progress);
            if (compact_icon_selectable(st->root_name, row, s, st->texture, st->anim_texture))
                item_click(kind, i, st->root_name, s);
        } else if (kind == COMPACT_COUNTER_SUB_STATS) {
            TrackableCategory *st = ctd->stats[i];
            bool s = item_index(kind, st->root_name) >= 0;
            char row[224];
            snprintf(row, sizeof(row), "%s (%d/%d)", st->display_name[0] ? st->display_name : st->root_name,
                     st->completed_criteria_count, st->criteria_count);
            if (compact_icon_selectable(st->root_name, row, s, st->texture, st->anim_texture))
                item_click(kind, i, st->root_name, s);
        } else if (kind == COMPACT_COUNTER_CUSTOM) {
            TrackableItem *cg = ctd->custom_goals[i];
            bool s = item_index(kind, cg->root_name) >= 0;
            if (compact_icon_selectable(cg->root_name, cg->display_name[0] ? cg->display_name : cg->root_name,
                                        s, cg->texture, cg->anim_texture))
                item_click(kind, i, cg->root_name, s);
        } else if (kind == COMPACT_COUNTER_MULTISTAGE) {
            MultiStageGoal *g = ctd->multi_stage_goals[i];
            bool s = item_index(kind, g->root_name) >= 0;
            int last_stage = g->stage_count > 0 ? g->stage_count - 1 : 0;
            char row[224];
            snprintf(row, sizeof(row), "%s (%d/%d)", g->display_name[0] ? g->display_name : g->root_name,
                     g->current_stage, last_stage);
            // With per-stage icons on, show the stage the goal is actually on, like the overlay
            // does; otherwise the goal's own icon.
            SDL_Texture *ms_tex = g->texture;
            AnimatedTexture *ms_anim = g->anim_texture;
            if (g->use_stage_icons && g->stages && g->current_stage >= 0 &&
                g->current_stage < g->stage_count && g->stages[g->current_stage] &&
                g->stages[g->current_stage]->icon_path[0]) {
                ms_tex = g->stages[g->current_stage]->texture;
                ms_anim = g->stages[g->current_stage]->anim_texture;
            }
            if (compact_icon_selectable(g->root_name, row, s, ms_tex, ms_anim))
                item_click(kind, i, g->root_name, s);
        } else if (kind == COMPACT_COUNTER_COUNTERS) {
            CounterGoal *cg = ctd->counter_goals[i];
            bool s = item_index(kind, cg->root_name) >= 0;
            char row[224];
            snprintf(row, sizeof(row), "%s (%d/%d)", cg->display_name[0] ? cg->display_name : cg->root_name,
                     cg->completed_count, cg->linked_goal_count);
            if (compact_icon_selectable(cg->root_name, row, s, cg->texture, cg->anim_texture))
                item_click(kind, i, cg->root_name, s);
        }
    };

    // Renders one category combo listing that category's items with checkboxes. `base_label` is the
    // visible text; `suffix` makes the ImGui ID unique between the cycle and stack copies. `tip`
    // describes this specific goal type for the combo's tooltip. The combo is skipped entirely when
    // the template has no goals of this kind.
    std::vector<int> rows;
    auto item_combo = [&](const char *base_label, OverlayCompactCounterType kind, const char *tip) {
        // Template indices this kind lists, so the clipper below has a contiguous row space and the
        // rows always agree with item_root_at (and so with Shift+Click ranges and Select All).
        rows.clear();
        for (int i = 0; i < scan_max; i++)
            if (item_root_at(kind, i)) rows.push_back(i);
        if (rows.empty()) return;
        int sel = 0;
        for (int i = 0; i < *tgt.count; i++)
            if (tgt.items[i].kind == kind) sel++;
        char preview[48];
        snprintf(preview, sizeof(preview), "%d selected", sel);
        char combo_label[96];
        snprintf(combo_label, sizeof(combo_label), "%s##%s", base_label, suffix);
        if (ImGui::BeginCombo(combo_label, preview)) {
            // Rows are a fixed height, so the clipper gets it up front and submits only the ones on
            // screen. A template with hundreds of goals then costs a few visible rows per frame.
            ImGuiListClipper clipper;
            clipper.Begin((int) rows.size(), compact_icon_row_height());
            while (clipper.Step())
                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; r++)
                    render_row(kind, rows[r]);
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            char item_combo_tooltip_buffer[512];
            if (is_cycle)
                snprintf(item_combo_tooltip_buffer, sizeof(item_combo_tooltip_buffer),
                         "%s\n"
                         "Shift+Click to range-select.\n"
                         "At least one entry across all these dropdowns must stay selected;\n"
                         "up to %d individual goals can be added in total.", tip, MAX_COMPACT_CYCLE_ITEMS);
            else
                snprintf(item_combo_tooltip_buffer, sizeof(item_combo_tooltip_buffer),
                         "%s\n"
                         "Only goals that can actually pop are listed: goals hidden in the template are\n"
                         "left out unless \"Show Hidden Goals\" is on.\n"
                         "Shift+Click to range-select.\n"
                         "Up to %d individual goals can be added in total.", tip, MAX_COMPACT_CYCLE_ITEMS);
            ImGui::SetTooltip("%s", item_combo_tooltip_buffer);
        }
        // Select All / Deselect All for this category, to the right of the dropdown.
        ImGui::SameLine();
        char all_id[48], none_id[48];
        snprintf(all_id, sizeof(all_id), "All##%s%dall", suffix, (int) kind);
        snprintf(none_id, sizeof(none_id), "None##%s%dnone", suffix, (int) kind);
        if (ImGui::SmallButton(all_id)) select_all_kind(kind, true);
        ImGui::SameLine();
        if (ImGui::SmallButton(none_id)) select_all_kind(kind, false);
    };

    char complex_adv_label[48];
    snprintf(complex_adv_label, sizeof(complex_adv_label), "Complex %s", modern ? "Advancements" : "Achievements");
    if (is_cycle) {
        item_combo(complex_adv_label, COMPACT_COUNTER_CRITERIA,
                   modern
                       ? "Add specific complex advancements (those with criteria) to the cycle, shown by\nname with their criteria progress. Recipes have their own dropdown."
                       : "Add specific complex achievements (those with criteria) to the cycle, shown by\nname with their criteria progress.");
        item_combo("Complex Recipes", COMPACT_COUNTER_RECIPE_CRITERIA,
                   "Add specific complex recipes (those with criteria) to the cycle, shown by\nname with their criteria progress.");
        item_combo("Simple Stats", COMPACT_COUNTER_STATS,
                   "Add specific simple stats to the cycle, shown by name with their value.");
        item_combo("Multi-Stats", COMPACT_COUNTER_SUB_STATS,
                   "Add specific multi-stats to the cycle, shown by name with their sub-stat progress.");
        item_combo("Custom Goals", COMPACT_COUNTER_CUSTOM,
                   "Add specific custom goals to the cycle, shown by name with their progress.");
        item_combo("Counters", COMPACT_COUNTER_COUNTERS,
                   "Add specific counters to the cycle, shown by name with their linked-goal progress.");
    } else {
        item_combo(complex_adv_label, COMPACT_COUNTER_CRITERIA,
                   modern
                       ? "Let specific complex advancements' criteria pop into the stack as they complete.\nRecipes have their own dropdown."
                       : "Let specific complex achievements' criteria pop into the stack as they complete.");
        item_combo("Complex Recipes", COMPACT_COUNTER_RECIPE_CRITERIA,
                   "Let specific complex recipes' criteria pop into the stack as they complete.");
        item_combo("Simple Stats", COMPACT_COUNTER_STATS,
                   "Let specific simple stats pop into the stack as their value climbs.");
        item_combo("Multi-Stats", COMPACT_COUNTER_SUB_STATS,
                   "Let specific multi-stats' sub-stats pop into the stack as their value climbs\nor they complete.");
        item_combo("Custom Goals", COMPACT_COUNTER_CUSTOM,
                   "Let specific custom goals pop into the stack as they progress or complete.");
        item_combo("Multi-Stage Goals", COMPACT_COUNTER_MULTISTAGE,
                   "Let specific multi-stage goals pop into the stack as they advance a stage\nor their current stat stage climbs.");
        item_combo("Counters", COMPACT_COUNTER_COUNTERS,
                   "Let specific counters pop into the stack as their linked-goal count climbs.");
    }
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
        strcmp(a->layout_flag, b->layout_flag) != 0 ||
        a->completion_use_adv_threshold != b->completion_use_adv_threshold ||
        a->completion_adv_threshold != b->completion_adv_threshold ||
        a->completion_use_percent_threshold != b->completion_use_percent_threshold ||
        a->completion_percent_threshold != b->completion_percent_threshold ||
        a->completion_threshold_require_both != b->completion_threshold_require_both ||
        a->enable_overlay != b->enable_overlay ||
        a->using_stats_per_world_legacy != b->using_stats_per_world_legacy ||
        a->using_hermes != b->using_hermes ||
        a->fps != b->fps ||
        a->overlay_fps != b->overlay_fps ||
        a->tracker_always_on_top != b->tracker_always_on_top ||
        a->goal_hiding_mode != b->goal_hiding_mode ||
        a->invert_hiding_mode != b->invert_hiding_mode ||
        a->print_debug_status != b->print_debug_status ||

        // Overlay settings
        a->overlay_render_mode != b->overlay_render_mode ||
        a->overlay_page_interval != b->overlay_page_interval ||
        a->overlay_page_align != b->overlay_page_align ||
        strcmp(a->compact_panel_path, b->compact_panel_path) != 0 ||
        a->compact_panel_inset_left != b->compact_panel_inset_left ||
        a->compact_panel_inset_right != b->compact_panel_inset_right ||
        a->compact_panel_inset_top != b->compact_panel_inset_top ||
        a->compact_panel_inset_bottom != b->compact_panel_inset_bottom ||
        a->compact_panel_pixel_scale != b->compact_panel_pixel_scale ||
        a->compact_panel_padding != b->compact_panel_padding ||
        a->compact_panel_align != b->compact_panel_align ||
        strcmp(a->compact_label_font_name, b->compact_label_font_name) != 0 ||
        strcmp(a->compact_count_font_name, b->compact_count_font_name) != 0 ||
        strcmp(a->compact_stack_font_name, b->compact_stack_font_name) != 0 ||
        a->compact_label_font_size != b->compact_label_font_size ||
        a->compact_count_font_size != b->compact_count_font_size ||
        a->compact_stack_font_size != b->compact_stack_font_size ||
        a->compact_panel_line_gap != b->compact_panel_line_gap ||
        compact_cycle_different(a, b) ||
        a->compact_cycle_interval != b->compact_cycle_interval ||
        a->compact_show_row1_icons != b->compact_show_row1_icons ||
        a->compact_icon_cycle_interval != b->compact_icon_cycle_interval ||
        a->compact_icon_row_gap != b->compact_icon_row_gap ||
        a->compact_row1_spacing != b->compact_row1_spacing ||
        a->compact_row1_clear_animation != b->compact_row1_clear_animation ||
        a->compact_row1_fade_enabled != b->compact_row1_fade_enabled ||
        a->compact_row1_fade_time != b->compact_row1_fade_time ||
        a->compact_icon_shared_size != b->compact_icon_shared_size ||
        compact_stack_different(a, b) ||
        a->compact_show_completion_markers != b->compact_show_completion_markers ||
        a->compact_stack_row_gap != b->compact_stack_row_gap ||
        a->compact_stack_max_lines != b->compact_stack_max_lines ||
        a->compact_stack_hold_time != b->compact_stack_hold_time ||
        a->compact_stack_rise_time != b->compact_stack_rise_time ||
        a->compact_stack_fade_enabled != b->compact_stack_fade_enabled ||
        a->compact_stack_fade_time != b->compact_stack_fade_time ||
        a->compact_pop_icon_size != b->compact_pop_icon_size ||
        a->compact_stack_shared_icon_size != b->compact_stack_shared_icon_size ||
        a->compact_stack_face_size != b->compact_stack_face_size ||
        a->compact_coop_panel_face_size != b->compact_coop_panel_face_size ||
        a->compact_coop_panel_face_offset_x != b->compact_coop_panel_face_offset_x ||
        a->compact_coop_panel_face_offset_y != b->compact_coop_panel_face_offset_y ||
        a->overlay_scroll_speed != b->overlay_scroll_speed ||
        a->overlay_progress_text_align != b->overlay_progress_text_align ||
        a->overlay_row1_spacing != b->overlay_row1_spacing ||
        a->compact_row1_icon_size != b->compact_row1_icon_size ||
        a->overlay_row1_shared_icon_size != b->overlay_row1_shared_icon_size ||
        a->overlay_row2_custom_spacing_enabled != b->overlay_row2_custom_spacing_enabled ||
        a->overlay_row2_custom_spacing != b->overlay_row2_custom_spacing ||
        a->overlay_row3_custom_spacing_enabled != b->overlay_row3_custom_spacing_enabled ||
        a->overlay_row3_custom_spacing != b->overlay_row3_custom_spacing ||
        a->overlay_row1_custom_scroll_speed_enabled != b->overlay_row1_custom_scroll_speed_enabled ||
        a->overlay_row1_scroll_speed != b->overlay_row1_scroll_speed ||
        a->overlay_row2_custom_scroll_speed_enabled != b->overlay_row2_custom_scroll_speed_enabled ||
        a->overlay_row2_scroll_speed != b->overlay_row2_scroll_speed ||
        a->overlay_row3_custom_scroll_speed_enabled != b->overlay_row3_custom_scroll_speed_enabled ||
        a->overlay_row3_scroll_speed != b->overlay_row3_scroll_speed ||
        a->overlay_row1_freeze_enabled != b->overlay_row1_freeze_enabled ||
        a->overlay_row1_freeze_align != b->overlay_row1_freeze_align ||
        a->overlay_row2_freeze_enabled != b->overlay_row2_freeze_enabled ||
        a->overlay_row2_freeze_align != b->overlay_row2_freeze_align ||
        a->overlay_row3_freeze_enabled != b->overlay_row3_freeze_enabled ||
        a->overlay_row3_freeze_align != b->overlay_row3_freeze_align ||
        a->overlay_row3_remove_completed != b->overlay_row3_remove_completed ||
        a->overlay_show_hidden_goals != b->overlay_show_hidden_goals ||
        a->overlay_stat_cycle_speed != b->overlay_stat_cycle_speed ||
        a->overlay_clear_animation != b->overlay_clear_animation ||
        a->overlay_clear_fade_enabled != b->overlay_clear_fade_enabled ||
        a->overlay_clear_fade_time != b->overlay_clear_fade_time ||
        a->tracker_vertical_spacing != b->tracker_vertical_spacing ||
        a->adv_icon_size != b->adv_icon_size ||
        a->adv_icon_offset_x != b->adv_icon_offset_x ||
        a->adv_icon_offset_y != b->adv_icon_offset_y ||
        a->tracker_shared_icon_size != b->tracker_shared_icon_size ||

        // LOD Settings
        a->lod_text_sub_threshold != b->lod_text_sub_threshold ||
        a->lod_text_main_threshold != b->lod_text_main_threshold ||
        a->lod_icon_detail_threshold != b->lod_icon_detail_threshold ||
        a->checkbox_reveal_enabled != b->checkbox_reveal_enabled ||
        a->checkbox_reveal_radius != b->checkbox_reveal_radius ||
        a->text_reveal_enabled != b->text_reveal_enabled ||

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
        a->igt_freeze_on_completion != b->igt_freeze_on_completion ||
        a->overlay_show_update_timer != b->overlay_show_update_timer ||
        strcmp(a->overlay_progress_separator, b->overlay_progress_separator) != 0 ||

        a->overlay_custom_vertical_spacing_enabled != b->overlay_custom_vertical_spacing_enabled ||
        a->overlay_gap_top_to_row1 != b->overlay_gap_top_to_row1 ||
        a->overlay_gap_row1_to_row2 != b->overlay_gap_row1_to_row2 ||
        a->overlay_gap_row2_to_row3 != b->overlay_gap_row2_to_row3 ||
        a->overlay_gap_row3_to_bottom != b->overlay_gap_row3_to_bottom ||

        strcmp(a->tracker_font_name, b->tracker_font_name) != 0 ||
        a->tracker_font_size != b->tracker_font_size ||
        a->tracker_sub_font_size != b->tracker_sub_font_size ||
        a->tracker_ui_font_size != b->tracker_ui_font_size ||
        strcmp(a->ui_font_name, b->ui_font_name) != 0 ||
        a->ui_font_size != b->ui_font_size ||
        strcmp(a->overlay_font_name, b->overlay_font_name) != 0 ||
        a->overlay_progress_font_size != b->overlay_progress_font_size ||
        a->overlay_row_font_size != b->overlay_row_font_size ||

        memcmp(&a->tracker_bg_color, &b->tracker_bg_color, sizeof(ColorRGBA)) != 0 ||
        memcmp(&a->overlay_bg_color, &b->overlay_bg_color, sizeof(ColorRGBA)) != 0 ||
        a->overlay_transparent != b->overlay_transparent ||
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
        a->coop_read_all_save_files != b->coop_read_all_save_files ||
        a->network_mode != b->network_mode ||
        a->coop_transport != b->coop_transport ||
        a->coop_stat_merge != b->coop_stat_merge ||
        a->coop_stat_checkbox != b->coop_stat_checkbox ||
        a->coop_custom_goal_mode != b->coop_custom_goal_mode ||
        a->coop_show_contributor_faces != b->coop_show_contributor_faces ||
        a->coop_face_corner != b->coop_face_corner ||
        a->coop_face_size != b->coop_face_size ||
        a->coop_face_lod_threshold != b->coop_face_lod_threshold ||
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

    // Compare hotkeys separately, skipping rows that hold no binding at all. A settings.json
    // written by the template resync contains one row per counter, most of them None/None, and
    // those must not read as a difference or "Revert Changes" would never go away.
    auto hotkey_row_is_empty = [](const HotkeyBinding *hb) -> bool {
        for (int slot = 0; slot < HOTKEY_SLOT_COUNT; ++slot) {
            const char *key = hotkey_binding_slot_key(hb, (HotkeySlot) slot);
            if (key[0] != '\0' && strcmp(key, "None") != 0) return false;
        }
        return !hb->is_global;
    };

    int ia = 0;
    int ib = 0;
    for (;;) {
        while (ia < a->hotkey_count && hotkey_row_is_empty(&a->hotkeys[ia])) ia++;
        while (ib < b->hotkey_count && hotkey_row_is_empty(&b->hotkeys[ib])) ib++;
        if (ia >= a->hotkey_count || ib >= b->hotkey_count) break;

        if (strcmp(a->hotkeys[ia].target_goal, b->hotkeys[ib].target_goal) != 0 ||
            strcmp(a->hotkeys[ia].increment_key, b->hotkeys[ib].increment_key) != 0 ||
            strcmp(a->hotkeys[ia].decrement_key, b->hotkeys[ib].decrement_key) != 0 ||
            strcmp(a->hotkeys[ia].toggle_key, b->hotkeys[ib].toggle_key) != 0 ||
            a->hotkeys[ia].increment_mods != b->hotkeys[ib].increment_mods ||
            a->hotkeys[ia].decrement_mods != b->hotkeys[ib].decrement_mods ||
            a->hotkeys[ia].toggle_mods != b->hotkeys[ib].toggle_mods ||
            a->hotkeys[ia].is_global != b->hotkeys[ib].is_global) {
            return true;
        }
        ia++;
        ib++;
    }
    // One side still has a real binding left over that the other does not.
    while (ia < a->hotkey_count && hotkey_row_is_empty(&a->hotkeys[ia])) ia++;
    while (ib < b->hotkey_count && hotkey_row_is_empty(&b->hotkeys[ib])) ib++;
    if ((ia < a->hotkey_count) != (ib < b->hotkey_count)) return true;

    if (app_hotkeys_different(a, b)) return true;

    return false;
}

// Returns true if any setting the overlay process reads from its own settings copy
// differs. The overlay only picks these up on a restart, so this decides whether an
// Apply needs to bounce the overlay window. Anything NOT listed here (tracker visuals,
// UI, hotkeys, path/coop config, live progress via IPC, etc.) leaves the overlay
// untouched. Keep this in sync with the settings->... reads in overlay.cpp.
static bool overlay_settings_different(const AppSettings *a, const AppSettings *b) {
    return
            // Whether the overlay process should exist / how fast it runs / its width.
            a->enable_overlay != b->enable_overlay ||
            a->overlay_fps != b->overlay_fps ||
            a->overlay_window.w != b->overlay_window.w ||

            // The overlay's own shortcut, read from its settings copy at startup.
            strcmp(a->app_hotkeys[APP_HOTKEY_OVERLAY_ADVANCE].key,
                   b->app_hotkeys[APP_HOTKEY_OVERLAY_ADVANCE].key) != 0 ||
            a->app_hotkeys[APP_HOTKEY_OVERLAY_ADVANCE].mods !=
            b->app_hotkeys[APP_HOTKEY_OVERLAY_ADVANCE].mods ||

            // Config read from the overlay's own copy (mod flags).
            // NOTE: version_str / display_version_str / category_display_name are intentionally
            // NOT here: they're pushed live via the IPC header (fill_overlay_ipc_labels), so a
            // template/version/category change updates the overlay without a restart.
            a->using_hermes != b->using_hermes ||
            a->print_debug_status != b->print_debug_status ||

            // Top info bar content/formatting.
            a->overlay_show_world != b->overlay_show_world ||
            a->overlay_show_run_details != b->overlay_show_run_details ||
            a->overlay_show_progress != b->overlay_show_progress ||
            a->overlay_show_igt != b->overlay_show_igt ||
            a->overlay_show_update_timer != b->overlay_show_update_timer ||
            a->igt_unit_spacing != b->igt_unit_spacing ||
            a->igt_always_show_ms != b->igt_always_show_ms ||
            a->igt_freeze_on_completion != b->igt_freeze_on_completion ||
            a->overlay_progress_text_align != b->overlay_progress_text_align ||
            strcmp(a->overlay_progress_separator, b->overlay_progress_separator) != 0 ||

            // Scrolling / freeze behavior.
            a->overlay_render_mode != b->overlay_render_mode ||
            a->overlay_page_interval != b->overlay_page_interval ||
            a->overlay_page_align != b->overlay_page_align ||
            strcmp(a->compact_panel_path, b->compact_panel_path) != 0 ||
            a->compact_panel_inset_left != b->compact_panel_inset_left ||
            a->compact_panel_inset_right != b->compact_panel_inset_right ||
            a->compact_panel_inset_top != b->compact_panel_inset_top ||
            a->compact_panel_inset_bottom != b->compact_panel_inset_bottom ||
            a->compact_panel_pixel_scale != b->compact_panel_pixel_scale ||
            a->compact_panel_padding != b->compact_panel_padding ||
            a->compact_panel_align != b->compact_panel_align ||
            strcmp(a->compact_label_font_name, b->compact_label_font_name) != 0 ||
            strcmp(a->compact_count_font_name, b->compact_count_font_name) != 0 ||
            strcmp(a->compact_stack_font_name, b->compact_stack_font_name) != 0 ||
            a->compact_label_font_size != b->compact_label_font_size ||
            a->compact_count_font_size != b->compact_count_font_size ||
            a->compact_stack_font_size != b->compact_stack_font_size ||
            a->compact_panel_line_gap != b->compact_panel_line_gap ||
            compact_cycle_different(a, b) ||
            a->compact_cycle_interval != b->compact_cycle_interval ||
            a->compact_show_row1_icons != b->compact_show_row1_icons ||
            a->compact_icon_cycle_interval != b->compact_icon_cycle_interval ||
            a->compact_icon_row_gap != b->compact_icon_row_gap ||
            a->compact_row1_spacing != b->compact_row1_spacing ||
            a->compact_row1_clear_animation != b->compact_row1_clear_animation ||
            a->compact_row1_fade_enabled != b->compact_row1_fade_enabled ||
            a->compact_row1_fade_time != b->compact_row1_fade_time ||
            a->compact_icon_shared_size != b->compact_icon_shared_size ||
            compact_stack_different(a, b) ||
            a->compact_show_completion_markers != b->compact_show_completion_markers ||
            a->compact_stack_row_gap != b->compact_stack_row_gap ||
            a->compact_stack_max_lines != b->compact_stack_max_lines ||
            a->compact_stack_hold_time != b->compact_stack_hold_time ||
            a->compact_stack_rise_time != b->compact_stack_rise_time ||
            a->compact_stack_fade_enabled != b->compact_stack_fade_enabled ||
            a->compact_stack_fade_time != b->compact_stack_fade_time ||
            a->compact_pop_icon_size != b->compact_pop_icon_size ||
            a->compact_stack_shared_icon_size != b->compact_stack_shared_icon_size ||
            a->compact_stack_face_size != b->compact_stack_face_size ||
            a->compact_coop_panel_face_size != b->compact_coop_panel_face_size ||
            a->compact_coop_panel_face_offset_x != b->compact_coop_panel_face_offset_x ||
            a->compact_coop_panel_face_offset_y != b->compact_coop_panel_face_offset_y ||
            // Now read by the Compact overlay for contributor faces, so a change must restart the overlay.
            a->coop_show_contributor_faces != b->coop_show_contributor_faces ||
            a->coop_stat_merge != b->coop_stat_merge ||
            a->overlay_scroll_speed != b->overlay_scroll_speed ||
            a->overlay_row1_custom_scroll_speed_enabled != b->overlay_row1_custom_scroll_speed_enabled ||
            a->overlay_row1_scroll_speed != b->overlay_row1_scroll_speed ||
            a->overlay_row2_custom_scroll_speed_enabled != b->overlay_row2_custom_scroll_speed_enabled ||
            a->overlay_row2_scroll_speed != b->overlay_row2_scroll_speed ||
            a->overlay_row3_custom_scroll_speed_enabled != b->overlay_row3_custom_scroll_speed_enabled ||
            a->overlay_row3_scroll_speed != b->overlay_row3_scroll_speed ||
            a->overlay_row1_freeze_enabled != b->overlay_row1_freeze_enabled ||
            a->overlay_row1_freeze_align != b->overlay_row1_freeze_align ||
            a->overlay_row2_freeze_enabled != b->overlay_row2_freeze_enabled ||
            a->overlay_row2_freeze_align != b->overlay_row2_freeze_align ||
            a->overlay_row3_freeze_enabled != b->overlay_row3_freeze_enabled ||
            a->overlay_row3_freeze_align != b->overlay_row3_freeze_align ||
            a->overlay_clear_animation != b->overlay_clear_animation ||
            a->overlay_clear_fade_enabled != b->overlay_clear_fade_enabled ||
            a->overlay_clear_fade_time != b->overlay_clear_fade_time ||
            a->overlay_stat_cycle_speed != b->overlay_stat_cycle_speed ||

            // Row spacing / sizing.
            a->overlay_row1_spacing != b->overlay_row1_spacing ||
            a->compact_row1_icon_size != b->compact_row1_icon_size ||
            a->overlay_row1_shared_icon_size != b->overlay_row1_shared_icon_size ||
            a->overlay_row2_custom_spacing_enabled != b->overlay_row2_custom_spacing_enabled ||
            a->overlay_row2_custom_spacing != b->overlay_row2_custom_spacing ||
            a->overlay_row3_custom_spacing_enabled != b->overlay_row3_custom_spacing_enabled ||
            a->overlay_row3_custom_spacing != b->overlay_row3_custom_spacing ||
            a->overlay_row3_remove_completed != b->overlay_row3_remove_completed ||
            a->overlay_show_hidden_goals != b->overlay_show_hidden_goals ||

            // Vertical spacing (row gaps) feed the layout math computed once at overlay init.
            a->overlay_custom_vertical_spacing_enabled != b->overlay_custom_vertical_spacing_enabled ||
            a->overlay_gap_top_to_row1 != b->overlay_gap_top_to_row1 ||
            a->overlay_gap_row1_to_row2 != b->overlay_gap_row1_to_row2 ||
            a->overlay_gap_row2_to_row3 != b->overlay_gap_row2_to_row3 ||
            a->overlay_gap_row3_to_bottom != b->overlay_gap_row3_to_bottom ||

            // Fonts / colors / background textures loaded at overlay init.
            strcmp(a->overlay_font_name, b->overlay_font_name) != 0 ||
            a->overlay_progress_font_size != b->overlay_progress_font_size ||
            a->overlay_row_font_size != b->overlay_row_font_size ||
            memcmp(&a->overlay_bg_color, &b->overlay_bg_color, sizeof(ColorRGBA)) != 0 ||
            a->overlay_transparent != b->overlay_transparent || // Window creation flag, needs a fresh window.
            memcmp(&a->overlay_text_color, &b->overlay_text_color, sizeof(ColorRGBA)) != 0 ||
            strcmp(a->adv_bg_path, b->adv_bg_path) != 0 ||
            strcmp(a->adv_bg_half_done_path, b->adv_bg_half_done_path) != 0 ||
            strcmp(a->adv_bg_done_path, b->adv_bg_done_path) != 0 ||
            a->adv_icon_size != b->adv_icon_size ||
            a->adv_icon_offset_x != b->adv_icon_offset_x ||
            a->adv_icon_offset_y != b->adv_icon_offset_y;
}

// Robustly opens a URL or local folder using SDL, falling back to system commands if needed.
/**
 * @brief Robustly opens a URL or local folder using SDL, falling back to system commands if needed.
 *
 * @param target The URL or local folder path to open.
 */
static void open_content(const char *target) {
    if (!target || target[0] == '\0') return;

    bool is_url = (strncmp(target, "http://", 7) == 0 || strncmp(target, "https://", 8) == 0);

    // URLs go through SDL_OpenURL on every platform.
    if (is_url) {
        SDL_OpenURL(target);
        return;
    }

    // Local paths use the native opener directly. On Linux SDL_OpenURL may
    // launch xdg-open AND return nonzero (based on xdg-open's exit code),
    // which previously caused the fallback to fire and open the folder twice.
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", target, nullptr, nullptr, SW_SHOW);
#elif defined(__APPLE__)
    char command[MAX_PATH_LENGTH + 16];
    snprintf(command, sizeof(command), "open \"%s\"", target);
    system(command);
#else
    char command[MAX_PATH_LENGTH + 16];
    snprintf(command, sizeof(command), "xdg-open \"%s\"", target);
    system(command);
#endif
}

// Every hotkey clash in one place. The Hotkeys tab still marks the offending rows, but a clash that
// only shows up there is invisible from another tab or behind a collapsed group header, so the
// settings window prints this list above the tabs and blocks Apply from the same flags.
struct HotkeyConflictReport {
    std::vector<std::string> messages;
    bool counter_duplicate = false; // Two goal hotkeys share a key.
    bool reserved = false; // A combination Advancely handles itself.
    bool app_conflict = false; // An Advancely shortcut collides with a shortcut or a goal hotkey.
};

// True for a slot that actually carries a key.
static bool hotkey_slot_bound(const char *key) {
    return key && key[0] != '\0' && strcmp(key, "None") != 0;
}

// Goal hotkeys store the physical key (a US-layout scancode name) while the app shortcuts store the
// keycap, so one of the two has to be translated before they can be compared. Without it a real
// clash on a non-US layout would go unnoticed, and an imaginary one would be reported.
static const char *hotkey_counter_key_as_keycap(const char *stored, char *buf, size_t buf_size) {
    snprintf(buf, buf_size, "%s", stored ? stored : "");
    SDL_Scancode sc = SDL_GetScancodeFromName(buf);
    if (sc == SDL_SCANCODE_UNKNOWN) return buf;
    const char *name = SDL_GetKeyName(SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false));
    if (name && name[0] != '\0') snprintf(buf, buf_size, "%s", name);
    return buf;
}

// The name the user sees for a goal hotkey's target, falling back to the root name when the
// template holding it is not the one currently loaded.
static const char *hotkey_goal_display_name(const Tracker *t, const char *root_name) {
    if (!t || !t->template_data || !t->template_data->custom_goals || !root_name) return root_name;
    for (int i = 0; i < t->template_data->custom_goal_count; ++i) {
        const TrackableItem *goal = t->template_data->custom_goals[i];
        if (goal && strcmp(goal->root_name, root_name) == 0 && goal->display_name[0] != '\0') {
            return goal->display_name;
        }
    }
    return root_name;
}

// Which contexts a goal hotkey can fire in. A global one is handed to the OS, so it reaches every
// window except visual layout editing, where hotkey_apply_counter_action() refuses to run. That
// refusal is also what keeps the visual editor's plain W, A, S and D usable.
static Uint16 hotkey_counter_contexts(const HotkeyBinding *hb) {
    return hb->is_global ? (Uint16) (APP_HOTKEY_CTX_ALL & ~APP_HOTKEY_CTX_VISUAL)
                         : (Uint16) APP_HOTKEY_CTX_COUNTER_HOTKEYS;
}

static void collect_hotkey_conflicts(const AppSettings &settings, const Tracker *t,
                                     HotkeyConflictReport &out) {
    out.messages.clear();
    out.counter_duplicate = false;
    out.reserved = false;
    out.app_conflict = false;

    char line[512];
    char reason[192];

    int counter_count = settings.hotkey_count;
    if (counter_count > MAX_HOTKEYS) counter_count = MAX_HOTKEYS;
    const int counter_slots = counter_count * HOTKEY_SLOT_COUNT;

    // --- Goal hotkey against goal hotkey ---
    // Both sides store the key the same way, so the stored names can be compared directly. A clash
    // needs the modifiers to match too, which is why Ctrl+G and a bare G can live on two goals.
    for (int a = 0; a < counter_slots; ++a) {
        const HotkeyBinding *hb_a = &settings.hotkeys[a / HOTKEY_SLOT_COUNT];
        HotkeySlot slot_a = (HotkeySlot) (a % HOTKEY_SLOT_COUNT);
        const char *key_a = hotkey_binding_slot_key(hb_a, slot_a);
        if (!hotkey_slot_bound(key_a)) continue;
        Uint16 mods_a = hotkey_binding_slot_mods(hb_a, slot_a);

        char label_a[96];
        char keycap_a[64];
        hotkey_counter_key_as_keycap(key_a, keycap_a, sizeof(keycap_a));
        char prefix_a[64];
        snprintf(label_a, sizeof(label_a), "%s%s", hotkey_mods_to_prefix(mods_a, prefix_a, sizeof(prefix_a)),
                 keycap_a);

        if (hotkey_slot_is_reserved(key_a, mods_a, reason, sizeof(reason))) {
            out.reserved = true;
            snprintf(line, sizeof(line), "\"%s\" %s %s.",
                     hotkey_goal_display_name(t, hb_a->target_goal), hotkey_slot_name(slot_a), reason);
            out.messages.push_back(line);
        }

        for (int b = a + 1; b < counter_slots; ++b) {
            const HotkeyBinding *hb_b = &settings.hotkeys[b / HOTKEY_SLOT_COUNT];
            HotkeySlot slot_b = (HotkeySlot) (b % HOTKEY_SLOT_COUNT);
            const char *key_b = hotkey_binding_slot_key(hb_b, slot_b);
            if (!hotkey_slot_bound(key_b)) continue;
            if (strcmp(key_a, key_b) != 0 || mods_a != hotkey_binding_slot_mods(hb_b, slot_b)) continue;

            out.counter_duplicate = true;
            snprintf(line, sizeof(line), "\"%s\" %s and \"%s\" %s both use %s.",
                     hotkey_goal_display_name(t, hb_a->target_goal), hotkey_slot_name(slot_a),
                     hotkey_goal_display_name(t, hb_b->target_goal), hotkey_slot_name(slot_b), label_a);
            out.messages.push_back(line);
        }

        // --- Goal hotkey against Advancely shortcut ---
        Uint16 contexts_a = hotkey_counter_contexts(hb_a);
        for (int action = 0; action < APP_HOTKEY_COUNT; ++action) {
            const AppHotkeyDef *def = &APP_HOTKEY_DEFS[action];
            const AppHotkey *hk = &settings.app_hotkeys[action];
            if (!hotkey_slot_bound(hk->key)) continue;
            if ((contexts_a & def->contexts) == 0) continue;
            if (mods_a != hk->mods || strcmp(keycap_a, hk->key) != 0) continue;

            out.app_conflict = true;
            snprintf(line, sizeof(line), "\"%s\" %s collides with the shortcut \"%s\" on %s.",
                     hotkey_goal_display_name(t, hb_a->target_goal), hotkey_slot_name(slot_a),
                     def->label, label_a);
            out.messages.push_back(line);
        }
    }

    // --- Advancely shortcut against Advancely shortcut ---
    // Two of them may share a key when they belong to windows or modes that are never active at the
    // same time, which is why the editor and the settings window can both use Ctrl+S.
    for (int a = 0; a < APP_HOTKEY_COUNT; ++a) {
        const AppHotkeyDef *def_a = &APP_HOTKEY_DEFS[a];
        const AppHotkey *hk_a = &settings.app_hotkeys[a];
        if (!hotkey_slot_bound(hk_a->key)) continue;

        char label_a[96];
        app_hotkey_display_label(hk_a, label_a, sizeof(label_a));

        if (hotkey_slot_is_reserved(hk_a->key, hk_a->mods, reason, sizeof(reason))) {
            out.reserved = true;
            snprintf(line, sizeof(line), "\"%s\" %s.", def_a->label, reason);
            out.messages.push_back(line);
        }

        for (int b = a + 1; b < APP_HOTKEY_COUNT; ++b) {
            const AppHotkeyDef *def_b = &APP_HOTKEY_DEFS[b];
            const AppHotkey *hk_b = &settings.app_hotkeys[b];
            if (!hotkey_slot_bound(hk_b->key)) continue;
            if ((def_a->contexts & def_b->contexts) == 0) continue;
            if (hk_a->mods != hk_b->mods || strcmp(hk_a->key, hk_b->key) != 0) continue;

            out.app_conflict = true;
            snprintf(line, sizeof(line), "The shortcuts \"%s\" and \"%s\" both use %s.",
                     def_a->label, def_b->label, label_a);
            out.messages.push_back(line);
        }
    }
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

    // Co-op error flag (block Apply when host IP/port is invalid)
    static bool coop_host_input_error = false;

    // Hotkey duplicate error flag (block Apply when two goals share the same key)
    static bool hotkey_duplicate_error = false;

    // Advancely shortcut error flag (block Apply when one clashes with another shortcut,
    // a counter hotkey, or a combination Advancely reserves)
    static bool hotkey_app_error = false;

    // Hotkey reserved error flag (block Apply when a binding collides with a built-in shortcut)
    static bool hotkey_reserved_error = false;

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

    // --- Settings presets (the bar above the tabs) ---
    static const int MAX_SETTING_PRESETS_UI = 256;
    static char preset_names[MAX_SETTING_PRESETS_UI][SETTING_PRESET_NAME_LEN];
    static int preset_count = 0;
    static int preset_selected = -1;
    static bool presets_need_rescan = true;
    static char new_preset_name[SETTING_PRESET_NAME_LEN] = "";
    static char preset_status_msg[256] = "";
    static bool preset_status_is_error = false;
    // Path of a preset loaded but not yet applied. Non-empty means its progress
    // sections should be restored into settings.json on the next Apply.
    static char pending_preset_progress_path[MAX_PATH_LENGTH] = "";

    // Helper lambda to auto-select a language (and layout) for the currently selected template
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

            // Keep a valid language; otherwise prefer the default, then the first available one.
            if (!current_is_valid) {
                bool default_lang_found = false;
                for (const auto &flag: selected_template->available_lang_flags) {
                    if (flag.empty()) {
                        // ".empty()" is true for the default "" flag
                        default_lang_found = true;
                        break;
                    }
                }

                if (default_lang_found || selected_template->available_lang_flags.empty()) {
                    // Prioritize default language (and fall back to it if none were found)
                    temp_settings.lang_flag[0] = '\0';
                } else {
                    // If no default, pick the first available one
                    strncpy(temp_settings.lang_flag, selected_template->available_lang_flags[0].c_str(),
                            sizeof(temp_settings.lang_flag) - 1);
                    temp_settings.lang_flag[sizeof(temp_settings.lang_flag) - 1] = '\0';
                }
            }

            // Keep a valid layout flag; otherwise fall back to the default ("" = _layout.json or
            // the template's built-in positions), which is always available when it has layout.
            bool layout_is_valid = false;
            for (const auto &flag: selected_template->available_layout_flags) {
                if (flag == temp_settings.layout_flag) {
                    layout_is_valid = true;
                    break;
                }
            }
            if (!layout_is_valid) {
                temp_settings.layout_flag[0] = '\0';
            }
        } else {
            // No matching template found, reset to default
            temp_settings.lang_flag[0] = '\0';
            temp_settings.layout_flag[0] = '\0';
        }
    };

    if (!version_counts_generated) {
        // This can run more than once (regenerated when templates change). Clear first so we rebuild
        // from scratch instead of appending a second set, and so the c_str pointers below don't dangle
        // when version_display_names reallocates.
        version_display_names.clear();
        version_display_c_strs.clear();
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
        // Also regenerate the per-version template counts shown in the version dropdown
        // so the "(N)" next to each version reflects added/removed templates without a restart.
        // Skip during visual layout editing: that drag raises g_templates_changed every frame,
        // and rescanning all 100+ versions each frame spikes disk I/O and halves the frame rate.
        // Templates can't be added or removed while layout editing, so the counts can't change.
        if (!t || !t->is_visual_layout_editing) {
            version_counts_generated = false;
        }
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

        // First, try to honor a per-language override stored in the selected lang file.
        if (temp_settings.version_str[0] != '\0' && temp_settings.category[0] != '\0') {
            char version_filename[64];
            strncpy(version_filename, temp_settings.version_str, sizeof(version_filename) - 1);
            version_filename[sizeof(version_filename) - 1] = '\0';
            for (char *p = version_filename; *p; p++) { if (*p == '.') *p = '_'; }

            char lang_suffix[80];
            if (temp_settings.lang_flag[0] != '\0') {
                snprintf(lang_suffix, sizeof(lang_suffix), "_%s", temp_settings.lang_flag);
            } else {
                lang_suffix[0] = '\0';
            }

            char lang_path[MAX_PATH_LENGTH];
            snprintf(lang_path, sizeof(lang_path), "%s/templates/%s/%s/%s_%s%s_lang%s.json",
                     get_resources_path(), temp_settings.version_str, temp_settings.category,
                     version_filename, temp_settings.category, temp_settings.optional_flag, lang_suffix);

            cJSON *lang_json = cJSON_from_file(lang_path);
            if (lang_json) {
                cJSON *dc = cJSON_GetObjectItem(lang_json, "display_category");
                if (dc && cJSON_IsString(dc) && dc->valuestring && dc->valuestring[0] != '\0') {
                    strncpy(temp_settings.category_display_name, dc->valuestring,
                            sizeof(temp_settings.category_display_name) - 1);
                    temp_settings.category_display_name[sizeof(temp_settings.category_display_name) - 1] = '\0';
                    cJSON_Delete(lang_json);
                    return;
                }
                cJSON_Delete(lang_json);
            }
        }

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
        presets_need_rescan = true; // Refresh the preset list every time the window opens
        new_preset_name[0] = '\0';
        preset_status_msg[0] = '\0';
        pending_preset_progress_path[0] = '\0';
        show_applied_message = false; // Reset message visibility
        show_defaults_applied_message = false; // Reset "Defaults Applied" message visibility
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

    // Mirror an app_settings identity field into BOTH editing buffers, so it reflects the live value
    // without showing up as an unsaved diff. Shared by the editor live-sync and the app-initiated resync.
    auto mirror_field = [](char *dst_a, char *dst_b, const char *src, size_t n) {
        strncpy(dst_a, src, n - 1);
        dst_a[n - 1] = '\0';
        strncpy(dst_b, src, n - 1);
        dst_b[n - 1] = '\0';
    };

    // An automatic, app-initiated change (e.g. the Template Creator deleting the in-use
    // template/language/layout and falling back to default) writes straight into app_settings. Re-seed
    // the affected identity fields in both editing buffers so the open Settings window follows WITHOUT a
    // spurious "unsaved changes" diff (reverting which would point back at the now-deleted file). Only the
    // identity fields are touched, preserving any unrelated unsaved edits the user has in this window.
    if (SDL_GetAtomicInt(&g_settings_resync_from_app)) {
        SDL_SetAtomicInt(&g_settings_resync_from_app, 0);
        mirror_field(temp_settings.version_str, saved_settings.version_str, app_settings->version_str,
                     sizeof(temp_settings.version_str));
        mirror_field(temp_settings.category, saved_settings.category, app_settings->category,
                     sizeof(temp_settings.category));
        mirror_field(temp_settings.optional_flag, saved_settings.optional_flag, app_settings->optional_flag,
                     sizeof(temp_settings.optional_flag));
        mirror_field(temp_settings.lang_flag, saved_settings.lang_flag, app_settings->lang_flag,
                     sizeof(temp_settings.lang_flag));
        mirror_field(temp_settings.layout_flag, saved_settings.layout_flag, app_settings->layout_flag,
                     sizeof(temp_settings.layout_flag));
        mirror_field(temp_settings.category_display_name, saved_settings.category_display_name,
                     app_settings->category_display_name, sizeof(temp_settings.category_display_name));
    }

    // While a template is open in the editor, the editor owns the applied template: it pushes its
    // selection straight into app_settings. Mirror those template-identity fields into both editing
    // buffers every frame so the (now disabled) Version/Category/Optional Flag/Language/Layout and the
    // derived Display Category control reflect the editor live, without raising a spurious "unsaved
    // changes" diff or letting Apply revert the editor's choice. (Display Version is left untouched.)
    bool template_editor_is_editing = t && t->template_editor_is_editing;
    if (template_editor_is_editing) {
        mirror_field(temp_settings.version_str, saved_settings.version_str, app_settings->version_str,
                     sizeof(temp_settings.version_str));
        mirror_field(temp_settings.category, saved_settings.category, app_settings->category,
                     sizeof(temp_settings.category));
        mirror_field(temp_settings.optional_flag, saved_settings.optional_flag, app_settings->optional_flag,
                     sizeof(temp_settings.optional_flag));
        mirror_field(temp_settings.lang_flag, saved_settings.lang_flag, app_settings->lang_flag,
                     sizeof(temp_settings.lang_flag));
        mirror_field(temp_settings.layout_flag, saved_settings.layout_flag, app_settings->layout_flag,
                     sizeof(temp_settings.layout_flag));
        mirror_field(temp_settings.category_display_name, saved_settings.category_display_name,
                     app_settings->category_display_name, sizeof(temp_settings.category_display_name));
    }
    const char *template_editor_lock_tooltip =
            "Locked while a template is open in the Template Creator.\n"
            "The editor controls the applied template, language and layout,\n"
            "so close the editor (or stop editing) to change this here.";

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

    // Revert Changes (Ctrl+Z / Cmd+Z by default, rebindable in the Hotkeys tab)
    if (t && t->settings_revert_pressed && has_unsaved_changes &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
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
    // achievement/advancement (singular, lowercase)
    const char *advancement_label_lowercase = (selected_version <= MC_VERSION_1_11_2) ? "achievement" : "advancement";

    // Adv/Ach
    const char *advancements_label_short_upper = (selected_version <= MC_VERSION_1_11_2) ? "Ach" : "Adv";

    // --- Settings Presets (always visible above the tabs) ---
    // Presets are full snapshots of settings.json stored next to it in resources/config/.
    // preset_loaded_this_frame is set when a preset is loaded so the completion-threshold
    // state below adopts the loaded values instead of resetting them on the template change.
    bool preset_loaded_this_frame = false; {
        // Some settings clash with the synchronised lobby state, so lock the section while active.
        bool preset_lobby_locked = g_coop_ctx &&
                                   (coop_net_get_state(g_coop_ctx) == COOP_NET_LISTENING ||
                                    coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED ||
                                    coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTING);

        if (presets_need_rescan) {
            preset_count = list_setting_presets(preset_names, MAX_SETTING_PRESETS_UI);
            if (preset_selected >= preset_count) preset_selected = preset_count - 1;
            presets_need_rescan = false;
        }

        auto name_to_lower = [](const char *s, char *out, size_t n) {
            size_t i = 0;
            for (; s[i] != '\0' && i + 1 < n; ++i) out[i] = (char) tolower((unsigned char) s[i]);
            out[i] = '\0';
        };

        ImGui::Text("Settings Presets");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            char preset_help_buffer[768];
            snprintf(preset_help_buffer, sizeof(preset_help_buffer),
                     "Save and switch between full snapshots of your settings.\n"
                     "Presets are stored as .json files in %s/, right next to\n"
                     "settings.json. Advancely always reads only settings.json itself, so\n"
                     "loading a preset just fills this window with its values - click\n"
                     "'Apply Settings' afterwards to actually use them.",
                     get_config_display_path());
            ImGui::SetTooltip("%s", preset_help_buffer);
        }

        if (preset_lobby_locked) ImGui::BeginDisabled();

        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##new_preset_name", "New preset name", new_preset_name, sizeof(new_preset_name));
        ImGui::SameLine();

        bool create_disabled = has_unsaved_changes;
        if (create_disabled) ImGui::BeginDisabled();
        if (ImGui::Button("Create Preset")) {
            char name[SETTING_PRESET_NAME_LEN];
            strncpy(name, new_preset_name, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            char *start = name;
            while (*start == ' ' || *start == '\t') start++;
            size_t end = strlen(start);
            while (end > 0 && (start[end - 1] == ' ' || start[end - 1] == '\t')) start[--end] = '\0';

            bool ok = true;
            if (start[0] == '\0') {
                snprintf(preset_status_msg, sizeof(preset_status_msg), "Enter a name for the preset.");
                preset_status_is_error = true;
                ok = false;
            }
            if (ok && strpbrk(start, "/\\:*?\"<>|") != nullptr) {
                snprintf(preset_status_msg, sizeof(preset_status_msg),
                         "Preset name cannot contain any of: / \\ : * ? \" < > |");
                preset_status_is_error = true;
                ok = false;
            }
            if (ok) {
                char lower[SETTING_PRESET_NAME_LEN];
                name_to_lower(start, lower, sizeof(lower));
                if (strcmp(lower, "settings") == 0) {
                    snprintf(preset_status_msg, sizeof(preset_status_msg),
                             "'settings' is reserved for the main settings file. Choose another name.");
                    preset_status_is_error = true;
                    ok = false;
                }
                for (int i = 0; ok && i < preset_count; ++i) {
                    char existing_lower[SETTING_PRESET_NAME_LEN];
                    name_to_lower(preset_names[i], existing_lower, sizeof(existing_lower));
                    if (strcmp(lower, existing_lower) == 0) {
                        snprintf(preset_status_msg, sizeof(preset_status_msg),
                                 "A preset named '%s' already exists.", preset_names[i]);
                        preset_status_is_error = true;
                        ok = false;
                    }
                }
            }
            if (ok) {
                // Copy the entire current settings.json verbatim into the new preset file.
                cJSON *root = cJSON_from_file(get_settings_file_path());
                if (!root) {
                    snprintf(preset_status_msg, sizeof(preset_status_msg),
                             "Could not read settings.json to create the preset.");
                    preset_status_is_error = true;
                } else {
                    char preset_path[MAX_PATH_LENGTH];
                    snprintf(preset_path, sizeof(preset_path), "%s/config/%s.json", get_resources_path(), start);
                    if (cJSON_write_to_file_atomic(preset_path, root)) {
                        snprintf(preset_status_msg, sizeof(preset_status_msg), "Created preset '%s'.", start);
                        preset_status_is_error = false;
                        new_preset_name[0] = '\0';
                        presets_need_rescan = true;
                    } else {
                        snprintf(preset_status_msg, sizeof(preset_status_msg), "Failed to write the preset file.");
                        preset_status_is_error = true;
                    }
                    cJSON_Delete(root);
                }
            }
        }
        if (create_disabled) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            char create_tooltip_buffer[768];
            if (has_unsaved_changes) {
                snprintf(create_tooltip_buffer, sizeof(create_tooltip_buffer),
                         "Disabled because there are unsaved changes.\n"
                         "Apply or revert your changes first so the preset matches the\n"
                         "settings.json file, then create the preset.");
            } else {
                snprintf(create_tooltip_buffer, sizeof(create_tooltip_buffer),
                         "Save the current settings as a new preset file in %s/.\n"
                         "The name cannot be 'settings' or match an existing preset.",
                         get_config_display_path());
            }
            ImGui::SetTooltip("%s", create_tooltip_buffer);
        }

        ImGui::SameLine();
        if (ImGui::Button("Open Settings Folder")) {
            char config_path[MAX_PATH_LENGTH];
            snprintf(config_path, sizeof(config_path), "%s/config", get_resources_path());
#ifdef _WIN32
            path_to_windows_native(config_path);
#endif
            open_content(config_path);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            char open_config_tooltip_buffer[256];
            snprintf(open_config_tooltip_buffer, sizeof(open_config_tooltip_buffer),
                     "Opens the '%s' folder in your file explorer,\n"
                     "where settings.json and your preset files are stored.",
                     get_config_display_path());
            ImGui::SetTooltip("%s", open_config_tooltip_buffer);
        }

        if (preset_count > 0) {
            ImGui::SetNextItemWidth(200.0f);
            const char *preview = (preset_selected >= 0 && preset_selected < preset_count)
                                      ? preset_names[preset_selected]
                                      : "Select a preset";
            if (ImGui::BeginCombo("##preset_select", preview)) {
                for (int i = 0; i < preset_count; ++i) {
                    bool is_sel = (i == preset_selected);
                    if (ImGui::Selectable(preset_names[i], is_sel)) preset_selected = i;
                    if (is_sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            bool have_selection = (preset_selected >= 0 && preset_selected < preset_count);

            ImGui::SameLine();
            if (!have_selection) ImGui::BeginDisabled();
            if (ImGui::Button("Load Preset")) {
                char preset_path[MAX_PATH_LENGTH];
                snprintf(preset_path, sizeof(preset_path), "%s/config/%s.json", get_resources_path(),
                         preset_names[preset_selected]);
                if (settings_load_from_file(&temp_settings, preset_path)) {
                    // Force the template list to rescan and the completion thresholds to
                    // adopt (not reset) the loaded values, so the tabs refresh in place.
                    last_scanned_version[0] = '\0';
                    preset_loaded_this_frame = true;
                    // Remember the source so Apply can restore its captured progress.
                    strncpy(pending_preset_progress_path, preset_path, sizeof(pending_preset_progress_path) - 1);
                    pending_preset_progress_path[sizeof(pending_preset_progress_path) - 1] = '\0';
                    snprintf(preset_status_msg, sizeof(preset_status_msg),
                             "Loaded preset '%s'. Click 'Apply Settings' to use it.", preset_names[preset_selected]);
                    preset_status_is_error = false;
                } else {
                    snprintf(preset_status_msg, sizeof(preset_status_msg),
                             "Failed to read preset '%s'.", preset_names[preset_selected]);
                    preset_status_is_error = true;
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                char load_tooltip_buffer[768];
                snprintf(load_tooltip_buffer, sizeof(load_tooltip_buffer),
                         "Fill this window with the selected preset's values.\n"
                         "Nothing is applied yet - review the tabs, then click 'Apply Settings'\n"
                         "to switch to the preset (or 'Revert Changes' to discard it).");
                ImGui::SetTooltip("%s", load_tooltip_buffer);
            }

            ImGui::SameLine();
            if (ImGui::Button("Remove Preset")) {
                ImGui::OpenPopup("Delete Preset?");
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                char remove_tooltip_buffer[512];
                snprintf(remove_tooltip_buffer, sizeof(remove_tooltip_buffer),
                         "Permanently delete the selected preset file from %s/.\n"
                         "This does not affect your current settings.",
                         get_config_display_path());
                ImGui::SetTooltip("%s", remove_tooltip_buffer);
            }
            if (!have_selection) ImGui::EndDisabled();
        } else {
            ImGui::TextDisabled("No presets saved yet.");
        }

        if (preset_lobby_locked) ImGui::EndDisabled();

        // Removal confirmation popup (only opens via the enabled "Remove Preset" button).
        ImVec2 delete_popup_center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(delete_popup_center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Delete Preset?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            const char *del_name = (preset_selected >= 0 && preset_selected < preset_count)
                                       ? preset_names[preset_selected]
                                       : "";
            char delete_prompt_buffer[256];
            snprintf(delete_prompt_buffer, sizeof(delete_prompt_buffer),
                     "Permanently delete the preset '%s'?", del_name);
            ImGui::Text("%s", delete_prompt_buffer);
            ImGui::Spacing();
            ImGui::TextDisabled("This cannot be undone.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
            if (ImGui::Button("Delete") || enter_pressed) {
                if (preset_selected >= 0 && preset_selected < preset_count) {
                    char preset_path[MAX_PATH_LENGTH];
                    snprintf(preset_path, sizeof(preset_path), "%s/config/%s.json", get_resources_path(),
                             preset_names[preset_selected]);
                    if (remove(preset_path) == 0) {
                        snprintf(preset_status_msg, sizeof(preset_status_msg), "Removed preset '%s'.",
                                 preset_names[preset_selected]);
                        preset_status_is_error = false;
                    } else {
                        snprintf(preset_status_msg, sizeof(preset_status_msg), "Failed to remove preset '%s'.",
                                 preset_names[preset_selected]);
                        preset_status_is_error = true;
                    }
                    presets_need_rescan = true;
                }
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buf[128];
                snprintf(tooltip_buf, sizeof(tooltip_buf),
                         "Permanently delete this preset file.\n"
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
                         "Keep the preset.\n"
                         "You can also press 'ESCAPE'.");
                ImGui::SetTooltip("%s", tooltip_buf);
            }

            ImGui::EndPopup();
        }

        if (preset_status_msg[0] != '\0') {
            ImVec4 status_color = preset_status_is_error
                                      ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                      : ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            ImGui::TextColored(status_color, "%s", preset_status_msg);
        }

        if (preset_lobby_locked) {
            ImGui::TextDisabled("Presets are locked while a co-op lobby is active.");
        }
    }
    ImGui::Separator();
    ImGui::Spacing();

    // --- Hotkey conflicts ---
    // Computed here rather than in the Hotkeys tab so a clash is reported no matter which tab is
    // open or which group headers are collapsed, and so Apply stays blocked either way.
    static HotkeyConflictReport hotkey_conflicts;
    collect_hotkey_conflicts(temp_settings, t, hotkey_conflicts);
    hotkey_duplicate_error = hotkey_conflicts.counter_duplicate;
    hotkey_reserved_error = hotkey_conflicts.reserved;
    hotkey_app_error = hotkey_conflicts.app_conflict;

    if (!hotkey_conflicts.messages.empty()) {
        // One group, so hovering any of the lines explains the whole block.
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                           "Hotkey conflicts (%d) - 'Apply Settings' stays disabled until they are resolved:",
                           (int) hotkey_conflicts.messages.size());
        for (const std::string &message: hotkey_conflicts.messages) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "    %s", message.c_str());
        }
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) {
            char hotkey_conflict_tooltip[512];
            snprintf(hotkey_conflict_tooltip, sizeof(hotkey_conflict_tooltip),
                     "Two bindings clash when the same key and modifiers can fire both at once.\n"
                     "Rebind either side in the Hotkeys tab, where the rows involved are marked\n"
                     "in red as well.");
            ImGui::SetTooltip("%s", hotkey_conflict_tooltip);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

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
                         "This is Path Mode: %d\n"
                         "Default: Auto-Track Active Instance", PATH_MODE_AUTO);
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
                         "While Minecraft is closed there is nothing to read, so progress simply\n"
                         "stops updating until an instance is running again.\n"
                         "This is Path Mode: %d\n"
                         "Default: Auto-Track Active Instance (this mode)", PATH_MODE_INSTANCE);
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
                         "This is Path Mode: %d\n"
                         "Default: Auto-Track Active Instance", PATH_MODE_FIXED_WORLD);
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
                         "This is Path Mode: %d\n"
                         "Default: Auto-Track Active Instance", PATH_MODE_MANUAL);
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
                         "%s/Version/Category/Version_CategoryOptionalFlag.json\n\n"
                         "Each template has one or more language files (e.g., ..._lang.json for default, ..._lang_eng.json for English)\n"
                         "that store all the display names shown in the UI.\n\n"
                         "Use the 'Open Template Editor' button to build new templates,\n"
                         "edit existing ones, and manage their language files.",
                         get_templates_display_path());
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
            bool version_disabled = coop_template_locked || template_editor_is_editing;
            if (version_disabled) ImGui::BeginDisabled();
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
                if (template_editor_is_editing) {
                    snprintf(version_tooltip_buffer, sizeof(version_tooltip_buffer), "%s",
                             template_editor_lock_tooltip);
                } else if (coop_template_locked) {
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
                             "Click on '(Version Support)' to see the version ranges that functionally equal.\n"
                             "Default: %s", DEFAULT_VERSION);
                }
                ImGui::SetTooltip("%s", version_tooltip_buffer);
            }
            if (version_disabled) ImGui::EndDisabled();

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
                         "By default, this matches the 'Template Version'.\n"
                         "Default: %s", DEFAULT_VERSION);
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
                                 "progress, and achievements will indicate if they were completed on a previous world.\n"
                                 "Default: Enabled");
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
                if (selected_version > MC_VERSION_1_6_4) {
                    ImGui::BulletText(
                        "Co-op: only the Host's Hermes log is read. Everyone else keeps this on\n"
                        "  purely so their overlay shows 'Synced:' instead of 'Upd:'.");
                }
                ImGui::Spacing();
                ImGui::TextDisabled("Requires Hermes to be installed and a world to be loaded.");
                ImGui::TextDisabled("Harmless to leave on without Hermes: the log is simply never found.");
                ImGui::TextDisabled("Default: %s", DEFAULT_USING_HERMES ? "Enabled" : "Disabled");
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

            bool category_disabled = coop_template_locked || template_editor_is_editing;
            if (category_disabled) ImGui::BeginDisabled();
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
                if (template_editor_is_editing) {
                    snprintf(category_tooltip_buffer, sizeof(category_tooltip_buffer), "%s",
                             template_editor_lock_tooltip);
                } else if (coop_template_locked) {
                    snprintf(category_tooltip_buffer, sizeof(category_tooltip_buffer),
                             "%s", coop_template_locked_tooltip);
                } else {
                    snprintf(category_tooltip_buffer, sizeof(category_tooltip_buffer),
                             "Choose between available categories for the selected version.\n"
                             "If the category you're looking for isn't available you can create it\n"
                             "by clicking the 'Open Template Editor' button or view the list of officially added\n"
                             "templates by clicking the '(Learn more)' button next to the 'Template Settings'.\n\n"
                             "Templates marked with '(has layout)' include pre-defined positions for goals.\n"
                             "Enable the 'Manual Layout' checkbox to use them.\n"
                             "Default: %s", DEFAULT_CATEGORY);
                }
                ImGui::SetTooltip("%s", category_tooltip_buffer);
            }
            if (category_disabled) ImGui::EndDisabled();


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

            bool flag_disabled = coop_template_locked || template_editor_is_editing;
            if (flag_disabled) ImGui::BeginDisabled();
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
                if (template_editor_is_editing) {
                    snprintf(flag_tooltip_buffer, sizeof(flag_tooltip_buffer), "%s",
                             template_editor_lock_tooltip);
                } else if (coop_template_locked) {
                    snprintf(flag_tooltip_buffer, sizeof(flag_tooltip_buffer),
                             "%s", coop_template_locked_tooltip);
                } else {
                    snprintf(flag_tooltip_buffer, sizeof(flag_tooltip_buffer),
                             "Choose between available optional flags for the selected version and category.\n"
                             "The optional flag is used to differentiate between different alterations of the same template.\n\n"
                             "Templates marked with '(has layout)' include pre-defined positions for goals.\n"
                             "Enable the 'Manual Layout' checkbox to use them.\n"
                             "Default: %s", DEFAULT_OPTIONAL_FLAG);
                }
                ImGui::SetTooltip("%s", flag_tooltip_buffer);
            }
            if (flag_disabled) ImGui::EndDisabled();

            // --- LANGUAGE DROPDOWN ---
            // (Display Category InputText now lives below the Language dropdown, since the
            //  language file can provide a per-language override.)
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

                    if (template_editor_is_editing) ImGui::BeginDisabled();
                    if (ImGui::Combo("Language", &lang_idx, lang_display_names.data(),
                                     (int) lang_display_names.size())) {
                        if (lang_idx >= 0 && (size_t) lang_idx < selected_template->available_lang_flags.size()) {
                            const std::string &selected_flag_str = selected_template->available_lang_flags[lang_idx];
                            strncpy(temp_settings.lang_flag, selected_flag_str.c_str(),
                                    sizeof(temp_settings.lang_flag) - 1);
                            temp_settings.lang_flag[sizeof(temp_settings.lang_flag) - 1] = '\0';

                            // Language change can swap the per-lang Display Category override
                            update_temp_display_category();
                        }
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        char lang_tooltip_buffer[1024];
                        if (template_editor_is_editing) {
                            snprintf(lang_tooltip_buffer, sizeof(lang_tooltip_buffer), "%s",
                                     template_editor_lock_tooltip);
                        } else {
                            snprintf(lang_tooltip_buffer, sizeof(lang_tooltip_buffer),
                                     "Choose between available language files for the selected template.\n"
                                     "The Default `_lang.json` is usually english and comes with every template.\n\n"
                                     "If a language file declares a 'display_category' field, that value is used\n"
                                     "to pre-fill the 'Display Category' field below (unless it is locked).\n"
                                     "Default: Default (_lang.json)");
                        }
                        ImGui::SetTooltip("%s", lang_tooltip_buffer);
                    }
                    if (template_editor_is_editing) ImGui::EndDisabled();

                    // --- Layout file dropdown (only shown when the template has layout data) ---
                    // Lives directly below the Language dropdown and mirrors its behavior.
                    if (selected_template->has_layout && !selected_template->available_layout_flags.empty()) {
                        std::vector<const char *> layout_display_names;
                        for (const auto &flag: selected_template->available_layout_flags) {
                            layout_display_names.push_back(flag.empty() ? "Default" : flag.c_str());
                        }

                        int layout_idx = -1;
                        for (size_t i = 0; i < selected_template->available_layout_flags.size(); ++i) {
                            if (selected_template->available_layout_flags[i] == temp_settings.layout_flag) {
                                layout_idx = (int) i;
                                break;
                            }
                        }

                        if (template_editor_is_editing) ImGui::BeginDisabled();
                        if (ImGui::Combo("Layout", &layout_idx, layout_display_names.data(),
                                         (int) layout_display_names.size())) {
                            if (layout_idx >= 0 &&
                                (size_t) layout_idx < selected_template->available_layout_flags.size()) {
                                const std::string &selected_flag_str =
                                        selected_template->available_layout_flags[layout_idx];
                                strncpy(temp_settings.layout_flag, selected_flag_str.c_str(),
                                        sizeof(temp_settings.layout_flag) - 1);
                                temp_settings.layout_flag[sizeof(temp_settings.layout_flag) - 1] = '\0';
                            }
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            char layout_tooltip_buffer[1024];
                            if (template_editor_is_editing) {
                                snprintf(layout_tooltip_buffer, sizeof(layout_tooltip_buffer), "%s",
                                         template_editor_lock_tooltip);
                            } else {
                                snprintf(layout_tooltip_buffer, sizeof(layout_tooltip_buffer),
                                         "Choose between available layout files for the selected template.\n"
                                         "A layout file stores manual positions and decorations separately from the\n"
                                         "template, so a custom layout can survive official template updates.\n\n"
                                         "'Default' uses the template's `_layout.json`, or its built-in positions\n"
                                         "if it has no separate layout file.\n"
                                         "Default: Default");
                            }
                            ImGui::SetTooltip("%s", layout_tooltip_buffer);
                        }
                        if (template_editor_is_editing) ImGui::EndDisabled();
                    }
                }
            }

            // --- Category Display Name Text Input (placed below the Language dropdown
            //     because the language can pre-fill / override it) ---
            {
                float standard_width = ImGui::CalcItemWidth();
                ImGui::SetNextItemWidth(standard_width);

                bool display_name_disabled = temp_settings.lock_category_display_name || template_editor_is_editing;
                if (display_name_disabled) ImGui::BeginDisabled();
                ImGui::InputText("Display Category", temp_settings.category_display_name,
                                 sizeof(temp_settings.category_display_name));
                if (display_name_disabled) ImGui::EndDisabled();

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    char tooltip_buffer[768];
                    if (template_editor_is_editing) {
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer), "%s", template_editor_lock_tooltip);
                    } else if (temp_settings.lock_category_display_name) {
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Display Name is currently locked.\n"
                                 "Uncheck the box to edit or auto-update.");
                    } else {
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "This is the name used for display on the tracker, overlay, and in debug logs.\n"
                                 "Pre-filled from the selected language file's 'display_category' field if present,\n"
                                 "otherwise automatically formatted from the Category and Optional Flag.\n"
                                 "You can override it with any custom text here.\n"
                                 "Default: auto-filled from the template/language (e.g. \"%s\")",
                                 DEFAULT_DISPLAY_CATEGORY);
                    }
                    ImGui::SetTooltip("%s", tooltip_buffer);
                }

                ImGui::SameLine();
                ImGui::Checkbox("Lock", &temp_settings.lock_category_display_name);
                if (ImGui::IsItemHovered()) {
                    char lock_display_name_tooltip_buffer[256];
                    snprintf(lock_display_name_tooltip_buffer, sizeof(lock_display_name_tooltip_buffer),
                             "Prevent the Display Name from changing automatically when switching templates or languages.\n"
                             "Default: Off");
                    ImGui::SetTooltip("%s", lock_display_name_tooltip_buffer);
                }
            }

            // --- Run Completion Threshold ---
            // Optional early-completion criteria (e.g. Half%). These reset to defaults
            // whenever the selected template changes, and the advancement-count maximum
            // tracks the currently selected template's goal count.
            {
                static char completion_last_template_sig[512] = {0};
                static int completion_template_goal_count = 0;

                char completion_sig[512];
                snprintf(completion_sig, sizeof(completion_sig), "%s|%s|%s",
                         temp_settings.version_str, temp_settings.category, temp_settings.optional_flag);

                if (just_opened || preset_loaded_this_frame) {
                    // Adopt the current selection on open (or right after a preset load)
                    // without wiping the thresholds the preset/settings file carried.
                    strncpy(completion_last_template_sig, completion_sig,
                            sizeof(completion_last_template_sig) - 1);
                    completion_last_template_sig[sizeof(completion_last_template_sig) - 1] = '\0';
                    completion_template_goal_count = count_template_advancement_goals(&temp_settings);
                } else if (strcmp(completion_sig, completion_last_template_sig) != 0) {
                    // Template changed: reset thresholds to defaults, refresh the count maximum.
                    strncpy(completion_last_template_sig, completion_sig,
                            sizeof(completion_last_template_sig) - 1);
                    completion_last_template_sig[sizeof(completion_last_template_sig) - 1] = '\0';
                    completion_template_goal_count = count_template_advancement_goals(&temp_settings);
                    temp_settings.completion_use_adv_threshold = DEFAULT_COMPLETION_USE_ADV_THRESHOLD;
                    temp_settings.completion_adv_threshold = DEFAULT_COMPLETION_ADV_THRESHOLD;
                    temp_settings.completion_use_percent_threshold = DEFAULT_COMPLETION_USE_PERCENT_THRESHOLD;
                    temp_settings.completion_percent_threshold = DEFAULT_COMPLETION_PERCENT_THRESHOLD;
                    temp_settings.completion_threshold_require_both = DEFAULT_COMPLETION_THRESHOLD_REQUIRE_BOTH;
                }

                int max_adv = completion_template_goal_count > 0 ? completion_template_goal_count : 1;
                // Keep the stored count within valid bounds for the current template.
                if (temp_settings.completion_adv_threshold < 1) temp_settings.completion_adv_threshold = 1;
                if (temp_settings.completion_adv_threshold > max_adv)
                    temp_settings.completion_adv_threshold = max_adv;

                const char *goal_word = (selected_version <= MC_VERSION_1_6_4) ? "achievements" : "advancements";

                // Niche feature tucked behind a collapsing header (collapsed by default).
                bool run_completion_open = ImGui::CollapsingHeader("Run Completion (Stopping Criteria)");
                if (ImGui::IsItemHovered()) {
                    char run_completion_tooltip_buffer[1024];
                    snprintf(run_completion_tooltip_buffer, sizeof(run_completion_tooltip_buffer),
                             "Optionally end the run (and freeze the IGT timer) before full 100%% completion.\n"
                             "Useful for categories like Half%% where only a fraction of the goals completes the run.\n\n"
                             "Enable a target %s count and/or a target overall progress percentage.\n"
                             "When neither is enabled the run only completes at full 100%%.\n\n"
                             "These settings reset to defaults whenever you change the selected template.",
                             goal_word);
                    ImGui::SetTooltip("%s", run_completion_tooltip_buffer);
                }

                if (run_completion_open) {
                    // Target advancement/achievement count
                    char adv_threshold_label[64];
                    snprintf(adv_threshold_label, sizeof(adv_threshold_label), "Complete at %s count", goal_word);
                    ImGui::Checkbox(adv_threshold_label, &temp_settings.completion_use_adv_threshold);
                    if (ImGui::IsItemHovered()) {
                        char adv_threshold_tooltip_buffer[512];
                        snprintf(adv_threshold_tooltip_buffer, sizeof(adv_threshold_tooltip_buffer),
                                 "When enabled, the run completes once this many %s are done.\n"
                                 "The maximum (%d) is the number of %s in the selected template.\n"
                                 "Default: Off (resets when the template changes)",
                                 goal_word, max_adv, goal_word);
                        ImGui::SetTooltip("%s", adv_threshold_tooltip_buffer);
                    }

                    // Inline count input, shown only while the checkbox is ticked.
                    if (temp_settings.completion_use_adv_threshold) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::InputInt("##completion_target_count", &temp_settings.completion_adv_threshold)) {
                            if (temp_settings.completion_adv_threshold < 1) temp_settings.completion_adv_threshold = 1;
                            if (temp_settings.completion_adv_threshold > max_adv)
                                temp_settings.completion_adv_threshold = max_adv;
                        }
                        if (ImGui::IsItemHovered()) {
                            char target_count_tooltip_buffer[256];
                            snprintf(target_count_tooltip_buffer, sizeof(target_count_tooltip_buffer),
                                     "Number of completed %s required (1 to %d).\n"
                                     "Default: %d", goal_word, max_adv, DEFAULT_COMPLETION_ADV_THRESHOLD);
                            ImGui::SetTooltip("%s", target_count_tooltip_buffer);
                        }
                    }

                    // Target overall progress percentage
                    ImGui::Checkbox("Complete at progress percentage", &temp_settings.completion_use_percent_threshold);
                    if (ImGui::IsItemHovered()) {
                        char pct_threshold_tooltip_buffer[512];
                        snprintf(pct_threshold_tooltip_buffer, sizeof(pct_threshold_tooltip_buffer),
                                 "When enabled, the run completes once overall progress reaches this percentage.\n"
                                 "This is the same overall progress shown in the tracker and overlay\n"
                                 "(every goal type except advancements contributes to it).\n"
                                 "Default: Off (resets when the template changes)");
                        ImGui::SetTooltip("%s", pct_threshold_tooltip_buffer);
                    }

                    // Inline percentage input, shown only while the checkbox is ticked.
                    if (temp_settings.completion_use_percent_threshold) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::InputFloat("##completion_target_percentage",
                                              &temp_settings.completion_percent_threshold, 0.0f, 0.0f, "%.2f")) {
                            if (temp_settings.completion_percent_threshold < 0.0f)
                                temp_settings.completion_percent_threshold = 0.0f;
                            if (temp_settings.completion_percent_threshold > 100.0f)
                                temp_settings.completion_percent_threshold = 100.0f;
                        }
                        if (ImGui::IsItemHovered()) {
                            char target_pct_tooltip_buffer[256];
                            snprintf(target_pct_tooltip_buffer, sizeof(target_pct_tooltip_buffer),
                                     "Overall progress percentage required (0.00 to 100.00).\n"
                                     "Default: %.2f", DEFAULT_COMPLETION_PERCENT_THRESHOLD);
                            ImGui::SetTooltip("%s", target_pct_tooltip_buffer);
                        }
                    }

                    // AND/OR logic, only meaningful when both targets are enabled
                    bool both_targets_enabled = temp_settings.completion_use_adv_threshold &&
                                                temp_settings.completion_use_percent_threshold;
                    if (!both_targets_enabled) ImGui::BeginDisabled();
                    ImGui::Checkbox("Require both targets (AND)", &temp_settings.completion_threshold_require_both);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        char require_both_tooltip_buffer[512];
                        if (!both_targets_enabled) {
                            snprintf(require_both_tooltip_buffer, sizeof(require_both_tooltip_buffer),
                                     "Disabled because only one (or no) target is enabled.\n"
                                     "Enable BOTH the %s count and the progress percentage targets above\n"
                                     "to choose whether both must be met (AND) or just either one (OR).",
                                     goal_word);
                        } else {
                            snprintf(require_both_tooltip_buffer, sizeof(require_both_tooltip_buffer),
                                     "Checked: the run completes only when BOTH targets are met (AND).\n"
                                     "Unchecked: the run completes as soon as EITHER target is met (OR).\n"
                                     "Default: Off (either target, OR)");
                        }
                        ImGui::SetTooltip("%s", require_both_tooltip_buffer);
                    }
                    if (!both_targets_enabled) ImGui::EndDisabled();
                } // end run_completion_open
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
                         "Opens the '%s' folder in your file explorer.",
                         get_templates_display_path());
                ImGui::SetTooltip("%s", open_templates_folder_tooltip_buffer);
            }

            // Place Template Creator Button in same line
            ImGui::SameLine();

            bool coop_session_active = g_coop_ctx &&
                                       (coop_net_get_state(g_coop_ctx) == COOP_NET_LISTENING ||
                                        coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTED ||
                                        coop_net_get_state(g_coop_ctx) == COOP_NET_CONNECTING);
            if (coop_session_active) ImGui::BeginDisabled();
            if (ImGui::Button("Open Template Editor")) {
                *p_temp_creator_open = true; // Open the template creator window
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                char open_template_creator_tooltip_buffer[1024];
                if (coop_session_active) {
                    snprintf(open_template_creator_tooltip_buffer, sizeof(open_template_creator_tooltip_buffer),
                             "Template editing is disabled during a Co-op session");
                } else {
                    snprintf(open_template_creator_tooltip_buffer, sizeof(open_template_creator_tooltip_buffer),
                             "Open the Template Editor to modify or build a new template, language or layout.");
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
                         "Forces the tracker window to always display above any other window.\n"
                         "Default: %s", DEFAULT_TRACKER_ALWAYS_ON_TOP ? "On" : "Off");
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
                         "Limits the frames per second of the tracker window.\n"
                         "Higher values may result in higher CPU usage.\n"
                         "Default: %d FPS", DEFAULT_FPS);
                ImGui::SetTooltip("%s", tracker_fps_limit_tooltip_buffer);
            }

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Performance");

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

            // --- Cursor Reveal Settings ---
            ImGui::Checkbox("Reveal Checkboxes Near Cursor", &temp_settings.checkbox_reveal_enabled);
            if (ImGui::IsItemHovered()) {
                char cb_reveal_tooltip[512];
                snprintf(cb_reveal_tooltip, sizeof(cb_reveal_tooltip),
                         "When enabled, manual-completion checkboxes only appear within a radius of the\n"
                         "mouse cursor instead of being drawn for every goal.\n"
                         "Completion is still shown by the goal background, so the checkbox is only an\n"
                         "input affordance. Useful for very large templates.\n"
                         "Default: %s", DEFAULT_CHECKBOX_REVEAL_ENABLED ? "On" : "Off");
                ImGui::SetTooltip("%s", cb_reveal_tooltip);
            }

            ImGui::BeginDisabled(!temp_settings.checkbox_reveal_enabled && !temp_settings.text_reveal_enabled);
            if (ImGui::DragFloat("Cursor Reveal Radius", &temp_settings.checkbox_reveal_radius, 1.0f, 20.0f, 1024.0f,
                                 "%.0f px")) {
                if (temp_settings.checkbox_reveal_radius < 20.0f) temp_settings.checkbox_reveal_radius = 20.0f;
                if (temp_settings.checkbox_reveal_radius > 1000.0f) temp_settings.checkbox_reveal_radius = 1000.0f;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                char cb_radius_tooltip[512];
                snprintf(cb_radius_tooltip, sizeof(cb_radius_tooltip),
                         "Radius around the cursor within which checkboxes (and text, if enabled) are\n"
                         "revealed. The radius is in template pixels, so it scales with the zoom level and\n"
                         "covers the same area regardless of how far you are zoomed in or out.\n"
                         "A faint ring shows the current radius while the mouse moves.\n"
                         "Default: %.0f px", DEFAULT_CHECKBOX_REVEAL_RADIUS);
                ImGui::SetTooltip("%s", cb_radius_tooltip);
            }

            ImGui::Checkbox("Also Reveal Text Near Cursor", &temp_settings.text_reveal_enabled);
            if (ImGui::IsItemHovered()) {
                char text_reveal_tooltip[512];
                snprintf(text_reveal_tooltip, sizeof(text_reveal_tooltip),
                         "When enabled, item names, progress text, and text headers also only appear\n"
                         "within the same Cursor Reveal Radius, using the radius above.\n"
                         "Each text reveals once the cursor reaches its anchor point (the reference\n"
                         "point its coordinates use in the template editor), not its mid-point.\n"
                         "Default: %s", DEFAULT_TEXT_REVEAL_ENABLED ? "On" : "Off");
                ImGui::SetTooltip("%s", text_reveal_tooltip);
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
                         "if 'Manual Layout' is active and any of its criteria/sub-stats use manual coordinates.\n"
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

            ImGui::Text("Layout & Spacing");

            // --- Section Order ---
            ImGui::SeparatorText("Section Order");
            if (ImGui::IsItemHovered()) {
                char section_order_tooltip_buffer[256];
                snprintf(section_order_tooltip_buffer, sizeof(section_order_tooltip_buffer),
                         "Drag and drop to reorder the sections in the main tracker window.\n"
                         "This doesn't affect the 'Manual Layout'.\n"
                         "Drop items between others to insert them at that position.\n"
                         "Default order: Counters, Advancements, Recipes, Unlocks, Stats, Custom, Multi-Stage");
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
                         "Sections not available in the selected template version will be hidden.\n"
                         "Default: Off for every section (%.0fpx when enabled)", DEFAULT_TRACKER_SECTION_ITEM_WIDTH);
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
                                 "This allows you to set a fixed, uniform total width for all items in this row.\n"
                                 "Default: Off",
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
                         "Only choose fonts within the %s directory.\n\n"
                         "A restart is required to properly apply changes.\n"
                         "Default: %s", get_fonts_display_path(), DEFAULT_TRACKER_FONT);
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
                         "Configure the color of the tracker background.\n"
                         "Default: Dark theme (13, 17, 23)");
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
                         "the controls in the bottom right.\n"
                         "Default: White (255, 255, 255)");
                ImGui::SetTooltip("%s", tracker_bg_tooltip_buffer);
            }

            if (font_settings_changed) {
#ifdef _WIN32
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "Click 'Restart Advancely' to properly apply these font/size changes.");
#else
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "Manually restart Advancely to properly apply these font/size changes.");
#endif
            }

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Background Textures");

            // --- Background Texture Settings ---
            // Helper lambda for Browse button and text display
            auto RenderBackgroundSetting = [&
                    ](const char *label, char *path_buffer, size_t buffer_size, const char *setting_id,
                      const char *default_path) {
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
                             "Must be a .png or .gif file located inside the %s folder.\n"
                             "Default: %s", label, get_gui_display_path(), default_path);
                    ImGui::SetTooltip("%s", tooltip_buffer);
                }
            };

            RenderBackgroundSetting("Default", temp_settings.adv_bg_path, sizeof(temp_settings.adv_bg_path),
                                    "DefaultBg", DEFAULT_ADV_BG_PATH);
            RenderBackgroundSetting("Half-Done", temp_settings.adv_bg_half_done_path,
                                    sizeof(temp_settings.adv_bg_half_done_path), "HalfDoneBg",
                                    DEFAULT_ADV_BG_HALF_DONE_PATH);
            RenderBackgroundSetting("Done", temp_settings.adv_bg_done_path, sizeof(temp_settings.adv_bg_done_path),
                                    "DoneBg", DEFAULT_ADV_BG_DONE_PATH);

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

            // --- Icon Size & Position (within the 96x96 background texture) ---
            ImGui::Separator();
            ImGui::Text("Icon Size & Position");

            // Icon Size
            if (ImGui::DragFloat("Icon Size", &temp_settings.adv_icon_size, 0.5f, ADV_ICON_MIN_SIZE, ADV_ICON_BG_SIZE,
                                 "%.0f px")) {
                if (temp_settings.adv_icon_size < ADV_ICON_MIN_SIZE) temp_settings.adv_icon_size = ADV_ICON_MIN_SIZE;
                if (temp_settings.adv_icon_size > ADV_ICON_BG_SIZE) temp_settings.adv_icon_size = ADV_ICON_BG_SIZE;
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[512];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Edge length of the icon inside the %.0fx%.0f background texture.\n"
                         "Applies to the tracker and overlay (not compact mode).\n"
                         "Cannot exceed the background size.\n"
                         "Default: %.0f px.",
                         ADV_ICON_BG_SIZE, ADV_ICON_BG_SIZE, DEFAULT_ADV_ICON_SIZE);
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            // Keep offsets within bounds after a size change (icon box must stay inside the background).
            const float icon_max_off = ADV_ICON_BG_SIZE - temp_settings.adv_icon_size;
            if (temp_settings.adv_icon_offset_x > icon_max_off) temp_settings.adv_icon_offset_x = icon_max_off;
            if (temp_settings.adv_icon_offset_y > icon_max_off) temp_settings.adv_icon_offset_y = icon_max_off;

            // Icon X Position
            if (ImGui::DragFloat("Icon X Position", &temp_settings.adv_icon_offset_x, 0.5f, 0.0f, icon_max_off,
                                 "%.0f px")) {
                if (temp_settings.adv_icon_offset_x < 0.0f) temp_settings.adv_icon_offset_x = 0.0f;
                if (temp_settings.adv_icon_offset_x > icon_max_off) temp_settings.adv_icon_offset_x = icon_max_off;
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[512];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Horizontal offset of the icon from the background's left edge.\n"
                         "The icon always stays inside the %.0fx%.0f background.\n"
                         "Range: 0 - %.0f px (depends on Icon Size). Default: %.0f px.",
                         ADV_ICON_BG_SIZE, ADV_ICON_BG_SIZE, icon_max_off, DEFAULT_ADV_ICON_OFFSET_X);
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            // Icon Y Position
            if (ImGui::DragFloat("Icon Y Position", &temp_settings.adv_icon_offset_y, 0.5f, 0.0f, icon_max_off,
                                 "%.0f px")) {
                if (temp_settings.adv_icon_offset_y < 0.0f) temp_settings.adv_icon_offset_y = 0.0f;
                if (temp_settings.adv_icon_offset_y > icon_max_off) temp_settings.adv_icon_offset_y = icon_max_off;
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[512];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Vertical offset of the icon from the background's top edge.\n"
                         "The icon always stays inside the %.0fx%.0f background.\n"
                         "Range: 0 - %.0f px (depends on Icon Size). Default: %.0f px.",
                         ADV_ICON_BG_SIZE, ADV_ICON_BG_SIZE, icon_max_off, DEFAULT_ADV_ICON_OFFSET_Y);
                ImGui::SetTooltip("%s", tooltip_buffer);
            }

            // Shared Icon Size (parent icon overlaid on a criterion/sub-stat icon)
            if (ImGui::DragFloat("Shared Icon Size", &temp_settings.tracker_shared_icon_size, 0.5f,
                                 TRACKER_SHARED_ICON_SIZE_MIN, TRACKER_SUB_ICON_BOX_SIZE, "%.0f px")) {
                if (temp_settings.tracker_shared_icon_size < TRACKER_SHARED_ICON_SIZE_MIN)
                    temp_settings.tracker_shared_icon_size = TRACKER_SHARED_ICON_SIZE_MIN;
                if (temp_settings.tracker_shared_icon_size > TRACKER_SUB_ICON_BOX_SIZE)
                    temp_settings.tracker_shared_icon_size = TRACKER_SUB_ICON_BOX_SIZE;
            }
            if (ImGui::IsItemHovered()) {
                char tooltip_buffer[512];
                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                         "Size of the small parent icon drawn on a criterion/sub-stat whose icon is shared\n"
                         "with a criterion/sub-stat of another goal. It tells overlapping icons apart.\n"
                         "Drawn in the corner of the %.0fx%.0f sub-item icon box, so it can never exceed it.\n"
                         "Set it to 0 to hide it.\n"
                         "Range: %.0f - %.0f px. Default: %.0f px.",
                         TRACKER_SUB_ICON_BOX_SIZE, TRACKER_SUB_ICON_BOX_SIZE, TRACKER_SHARED_ICON_SIZE_MIN,
                         TRACKER_SUB_ICON_BOX_SIZE, DEFAULT_TRACKER_SHARED_ICON_SIZE);
                ImGui::SetTooltip("%s", tooltip_buffer);
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
                         "Only choose fonts within the %s directory.\n\n"
                         "IMPORTANT: Requires restarting Advancely to apply.\n"
                         "Default: %s", get_fonts_display_path(), DEFAULT_UI_FONT);
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
#ifdef _WIN32
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "Click 'Restart Advancely' to properly apply these font/size changes.");
#else
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "Manually restart Advancely to properly apply these font/size changes.");
#endif
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
int _ttlen = snprintf(tooltip_buffer, sizeof(tooltip_buffer), tooltip_fmt, ##__VA_ARGS__); \
if (_ttlen > 0 && _ttlen < (int)sizeof(tooltip_buffer)) \
snprintf(tooltip_buffer + _ttlen, sizeof(tooltip_buffer) - _ttlen, "\nDefault: Default Dark Theme"); \
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
#ifdef _WIN32
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "Click 'Restart Advancely' to properly apply these theme color changes.");
#else
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "Manually restart Advancely to properly apply these theme color changes.");
#endif
            }

            ImGui::EndTabItem();
        } // End of UI Visuals Tab

        if (ImGui::BeginTabItem("Overlay")) {
            // The overlay's advance/speed-up key is rebindable, so every tooltip that mentions it
            // shows whatever it is currently bound to instead of a hardcoded "SPACE".
            char overlay_advance_label[96];
            app_hotkey_display_label(&temp_settings.app_hotkeys[APP_HOTKEY_OVERLAY_ADVANCE],
                                     overlay_advance_label, sizeof(overlay_advance_label));

            // The row layout only applies to the Belt and Page modes, so it's built once here and
            // reused by both of their tooltips. Compact mode doesn't use rows at all.
            // The overlay is spawned as its own process, so it can also be started on its own.
            const char *overlay_process_tip =
                    " • The overlay runs as its own process: starting Advancely with the '--overlay'\n"
                    "   flag launches only the overlay (Advancely itself has to be running already).\n"
#ifdef __linux__
                    "   Handy in Waywall, where it might be useful to attach the overlay as a separate process.\n"
#endif
                    ;

            char overlay_row_layout[768];
            if (selected_version <= MC_VERSION_1_6_4) {
                snprintf(overlay_row_layout, sizeof(overlay_row_layout),
                         "Row Layout:\n"
                         " • Row 1: Sub-stats of complex stats (if not template hidden).\n"
                         "   (If two visible items share an icon, the parent's icon is overlaid.)\n"
                         " • Row 2: Main %s (Default).\n"
                         " • Row 3: Stats, custom goals, multi-stage goals, and counters (Default).\n"
                         "   (Goals can be forced between Row 2 and Row 3 in the Template Editor.)\n",
                         advancements_label_plural_lowercase
                );
            } else if (selected_version <= MC_VERSION_1_11_2) {
                snprintf(overlay_row_layout, sizeof(overlay_row_layout),
                         "Row Layout:\n"
                         " • Row 1: %s criteria and sub-stats of complex stats (if not template hidden).\n"
                         "   (If two visible items share an icon, the parent's icon is overlaid.)\n"
                         " • Row 2: Main %s (Default).\n"
                         " • Row 3: Stats, custom goals, multi-stage goals, and counters (Default).\n"
                         "   (Goals can be forced between Row 2 and Row 3 in the Template Editor.)\n",
                         advancement_label_uppercase, advancements_label_plural_lowercase
                );
            } else if (selected_version == MC_VERSION_25W14CRAFTMINE) {
                snprintf(overlay_row_layout, sizeof(overlay_row_layout),
                         "Row Layout:\n"
                         " • Row 1: %s criteria and sub-stats of complex stats (if not template hidden).\n"
                         "   (If two visible items share an icon, the parent's icon is overlaid.)\n"
                         " • Row 2: Main %s, recipes and unlocks (Default).\n"
                         " • Row 3: Stats, custom goals, multi-stage goals, and counters (Default).\n"
                         "   (Goals can be forced between Row 2 and Row 3 in the Template Editor.)\n",
                         advancement_label_uppercase, advancements_label_plural_lowercase
                );
            } else {
                snprintf(overlay_row_layout, sizeof(overlay_row_layout),
                         "Row Layout:\n"
                         " • Row 1: %s criteria and sub-stats of complex stats.\n"
                         "   (If two items share an icon, the parent's icon is overlaid.)\n"
                         " • Row 2: Main %s and recipes (Default).\n"
                         " • Row 3: Stats, custom goals, multi-stage goals, and counters (Default).\n"
                         "   (Goals can be forced between Row 2 and Row 3 in the Template Editor.)\n",
                         advancement_label_uppercase, advancements_label_plural_lowercase
                );
            }

            // General Settings
            ImGui::Text("General");

            ImGui::Checkbox("Enable Overlay", &temp_settings.enable_overlay);
            bool enable_overlay_hovered = ImGui::IsItemHovered();
#ifdef _WIN32
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.0f, 1.0f), " (use Game Capture)");
            enable_overlay_hovered = enable_overlay_hovered || ImGui::IsItemHovered();
#endif
            if (enable_overlay_hovered) {
                char enable_overlay_tooltip_buffer[2048];
                if (selected_version <= MC_VERSION_1_6_4) {
                    // Legacy
                    snprintf(enable_overlay_tooltip_buffer, sizeof(enable_overlay_tooltip_buffer),
                             "Enables a separate, customizable window to show your progress, perfect for streaming.\n"
                             "More overlay-related settings become visible.\n\n"
                             "Tips:\n"
                             " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                             " • A negative scroll speed animates items from right to left.\n"
                             " • Horizontal spacing depends on the length of the display text.\n"
                             "%s\n"
                             "IMPORTANT FOR STREAMERS:\n"
                             "On Windows you MUST use GAME CAPTURE for the overlay (NOT window capture).\n"
                             "Applying overlay-related changes will restart the overlay window.\n"
                             "You may need to reselect it in your streaming software (e.g., OBS).\n"
                             "Default: Off",
                             overlay_process_tip
                    );
                } else if (selected_version <= MC_VERSION_1_11_2) {
                    // Mid-era
                    snprintf(enable_overlay_tooltip_buffer, sizeof(enable_overlay_tooltip_buffer),
                             "Enables a separate, customizable window to show your progress, perfect for streaming.\n\n"
                             "Tips:\n"
                             " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                             " • A negative scroll speed animates items from right to left.\n"
                             " • Horizontal spacing depends on the length of the display text.\n"
                             "%s\n"
                             "IMPORTANT FOR STREAMERS:\n"
                             "Applying overlay-related changes will restart the overlay window.\n"
                             "You may need to reselect it in your streaming software (e.g., OBS).\n"
                             "Default: Off",
                             overlay_process_tip
                    );
                } else if (selected_version == MC_VERSION_25W14CRAFTMINE) {
                    // Craftmine
                    snprintf(enable_overlay_tooltip_buffer, sizeof(enable_overlay_tooltip_buffer),
                             "Enables a separate, customizable window to show your progress, perfect for streaming.\n\n"
                             "Tips:\n"
                             " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                             " • A negative scroll speed animates items from right to left.\n"
                             " • Horizontal spacing depends on the length of the display text.\n"
                             "%s\n"
                             "IMPORTANT FOR STREAMERS:\n"
                             "Applying overlay-related changes will restart the overlay window.\n"
                             "You may need to reselect it in your streaming software (e.g., OBS).\n"
                             "Default: Off",
                             overlay_process_tip
                    );
                } else {
                    // Modern
                    snprintf(enable_overlay_tooltip_buffer, sizeof(enable_overlay_tooltip_buffer),
                             "Enables a separate, customizable window to show your progress, perfect for streaming.\n\n"
                             "Tips:\n"
                             " • Use a color key filter in your streaming software on the 'Overlay Background Color'.\n"
                             " • A negative scroll speed animates items from right to left.\n"
                             " • Horizontal spacing depends on the length of the display text.\n"
                             "%s\n"
                             "IMPORTANT FOR STREAMERS:\n"
                             "Applying overlay-related changes will restart the overlay window.\n"
                             "You may need to reselect it in your streaming software (e.g., OBS).\n"
                             "Default: Off",
                             overlay_process_tip
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
                             "Limits the frames per second of the overlay window.\n"
                             "Higher values may result in higher GPU/CPU usage.\n"
                             "Default: %d FPS", DEFAULT_OVERLAY_FPS);
                    ImGui::SetTooltip("%s", overlay_fps_limit_tooltip_buffer);
                }

                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Text("Mode");

                // Overlay layout mode. Belt is the classic scrolling conveyor; Page shows a
                // static, centered slice of items and flips between slices like a book.
                int overlay_mode = (int) temp_settings.overlay_render_mode;
                if (ImGui::RadioButton("Scrolling Belt", overlay_mode == OVERLAY_RENDER_MODE_BELT)) {
                    overlay_mode = OVERLAY_RENDER_MODE_BELT;
                }
                // For displaying the default mode
                const char *overlay_mode_names[] = {"Scrolling Belt", "Page", "Compact"};
                if (ImGui::IsItemHovered()) {
                    char overlay_mode_belt_tooltip_buffer[1536];
                    snprintf(overlay_mode_belt_tooltip_buffer, sizeof(overlay_mode_belt_tooltip_buffer),
                             "Items continuously scroll across the overlay as a conveyor belt.\n"
                             "Enables the scroll speed, per-row custom speed and auto-freeze options below.\n"
                             "Hold %s while the overlay window is focused to speed up the scroll 5x.\n\n"
                             "%s\n"
                             "Default: %s", overlay_advance_label, overlay_row_layout,
                             overlay_mode_names[DEFAULT_OVERLAY_RENDER_MODE]);
                    ImGui::SetTooltip("%s", overlay_mode_belt_tooltip_buffer);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Page", overlay_mode == OVERLAY_RENDER_MODE_PAGE)) {
                    overlay_mode = OVERLAY_RENDER_MODE_PAGE;
                }
                if (ImGui::IsItemHovered()) {
                    char overlay_mode_page_tooltip_buffer[1536];
                    snprintf(overlay_mode_page_tooltip_buffer, sizeof(overlay_mode_page_tooltip_buffer),
                             "Items are shown statically, centered, fitting as many as the overlay width allows.\n"
                             "After the interval below the overlay cuts to the next page of items (like a book).\n"
                             "Press %s while the overlay window is focused to flip to the next page.\n\n"
                             "%s\n"
                             "Default: %s", overlay_advance_label, overlay_row_layout,
                             overlay_mode_names[DEFAULT_OVERLAY_RENDER_MODE]);
                    ImGui::SetTooltip("%s", overlay_mode_page_tooltip_buffer);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Compact", overlay_mode == OVERLAY_RENDER_MODE_COMPACT)) {
                    overlay_mode = OVERLAY_RENDER_MODE_COMPACT;
                }
                if (ImGui::IsItemHovered()) {
                    char overlay_mode_compact_tooltip_buffer[768];
                    snprintf(overlay_mode_compact_tooltip_buffer, sizeof(overlay_mode_compact_tooltip_buffer),
                             "A tall, compact counter panel that cycles through goal types,\n"
                             "with completed goals popping out beneath it. Press %s while the\n"
                             "overlay window is focused to cycle to the next goal on the panel.\n"
                             "Meant to be used with the Hermes mod, so the pop-outs appear the\n"
                             "moment a goal completes: install the mod and check\n"
                             "'Using Hermes Mod (Live Tracking)' in the Paths & Templates tab.\n"
                             "Inspired by Zesskyo.\n"
                             "Default: %s", overlay_advance_label, overlay_mode_names[DEFAULT_OVERLAY_RENDER_MODE]);
                    ImGui::SetTooltip("%s", overlay_mode_compact_tooltip_buffer);
                }
                temp_settings.overlay_render_mode = (OverlayRenderMode) overlay_mode;

                const bool overlay_page_mode = (temp_settings.overlay_render_mode == OVERLAY_RENDER_MODE_PAGE);
                const bool overlay_compact_mode = (temp_settings.overlay_render_mode == OVERLAY_RENDER_MODE_COMPACT);

                // Applies to every render mode, so it lives here (outside the mode-specific sections).
                ImGui::Checkbox("Show Hidden Goals", &temp_settings.overlay_show_hidden_goals);
                if (ImGui::IsItemHovered()) {
                    char show_hidden_tooltip_buffer[512];
                    snprintf(show_hidden_tooltip_buffer, sizeof(show_hidden_tooltip_buffer),
                             "Show goals that are marked hidden in the template anyway, in every overlay mode\n"
                             "(scrolling belt, page and compact). Turn this on to also select hidden goals in\n"
                             "the Compact panel/stack dropdowns above.\n"
                             "The Stack Content dropdowns list and count only goals that can pop, so an\n"
                             "optimized template shows fewer of them there until this is on. The Panel Content\n"
                             "counts are the real section totals and don't change with this.\n"
                             "Default: %s", DEFAULT_OVERLAY_SHOW_HIDDEN_GOALS ? "On" : "Off");
                    ImGui::SetTooltip("%s", show_hidden_tooltip_buffer);
                }

                // Timer formatting is not tied to a render mode: it drives the tracker's own timers, the
                // belt/page top bar, and the Compact mode's final time on the run-completed panel. So it
                // lives here, outside the mode-specific sections, and stays reachable in every mode.
                ImGui::Checkbox("Freeze Timer on Completion", &temp_settings.igt_freeze_on_completion);
                if (ImGui::IsItemHovered()) {
                    char igt_freeze_tooltip_buffer[512];
                    snprintf(igt_freeze_tooltip_buffer, sizeof(igt_freeze_tooltip_buffer),
                             "Freezes the IGT at the final time once the run is completed, in the\n"
                             "tracker info window, the debug print output and the overlay.\n"
                             "When off, those timers keep counting up like the tracker window title.\n"
                             "With the SpeedrunIGT mod every IGT is millisecond precise instead of\n"
                             "stepping in 50 ms game ticks.\n"
                             "Default: %s", DEFAULT_IGT_FREEZE_ON_COMPLETION ? "On" : "Off");
                    ImGui::SetTooltip("%s", igt_freeze_tooltip_buffer);
                }
                ImGui::SameLine();
                ImGui::Checkbox("Timers Unit Spacing", &temp_settings.igt_unit_spacing);
                if (ImGui::IsItemHovered()) {
                    char igt_spacing_tooltip_buffer[256];
                    snprintf(igt_spacing_tooltip_buffer, sizeof(igt_spacing_tooltip_buffer),
                             "Adds a space between every number and its unit in the IGT\n"
                             "and Update Timer display.\n"
                             "Example: \"02m 04.500s\" becomes \"02 m 04 s 500 ms\".\n"
                             "Default: Off");
                    ImGui::SetTooltip("%s", igt_spacing_tooltip_buffer);
                }
                ImGui::SameLine();
                ImGui::Checkbox("IGT Always Show ms", &temp_settings.igt_always_show_ms);
                if (ImGui::IsItemHovered()) {
                    char igt_ms_tooltip_buffer[384];
                    snprintf(igt_ms_tooltip_buffer, sizeof(igt_ms_tooltip_buffer),
                             "Always shows milliseconds in the IGT display,\n"
                             "even when the time exceeds one minute.\n"
                             "Example: \"02m 04.500s\" instead of \"02m 04s\".\n"
                             "Times from the stats file step in 50 ms game ticks; only times\n"
                             "read from the SpeedrunIGT mod are millisecond exact.\n"
                             "Default: Off");
                    ImGui::SetTooltip("%s", igt_ms_tooltip_buffer);
                }

                // Content & Behavior drives the belt/page top info bar and 3-row layout (text sections,
                // separator, sub-stat cycling, clear animation). Compact mode uses none of these, so hide
                // the whole section while it is active.
                if (temp_settings.overlay_render_mode != OVERLAY_RENDER_MODE_COMPACT) {
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
                                 "or 'Syncing for <Player>' depending on the player dropdown selection.\n"
                                 "Default: On");
                        ImGui::SetTooltip("%s", overlay_text_world_tooltip_buffer);
                    }
                    ImGui::SameLine();
                    ImGui::Checkbox("Run Details", &temp_settings.overlay_show_run_details);
                    if (ImGui::IsItemHovered()) {
                        char overlay_text_run_tooltip_buffer[1024];
                        snprintf(overlay_text_run_tooltip_buffer, sizeof(overlay_text_run_tooltip_buffer),
                                 "Shows the selected Template Version & Template Category.\n"
                                 "Default: On");
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

                        ImGui::Separator();
                        ImGui::TextUnformatted("Default: On");

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
                                 "and only updated when the game saves.\n"
                                 "Default: On");
                        ImGui::SetTooltip("%s", overlay_text_igt_tooltip_buffer);
                    }
                    ImGui::SameLine();
                    ImGui::Checkbox("Update Timer", &temp_settings.overlay_show_update_timer);
                    if (ImGui::IsItemHovered()) {
                        char overlay_text_timer_tooltip_buffer[1024];
                        snprintf(overlay_text_timer_tooltip_buffer, sizeof(overlay_text_timer_tooltip_buffer),
                                 "Shows the time since the last game file update.\n"
                                 "When Hermes is active this timer only represents the time\n"
                                 "since the last full game-save sync from disk.\n"
                                 "Default: On");
                        ImGui::SetTooltip("%s", overlay_text_timer_tooltip_buffer);
                    }

                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::InputText("Segment Separator", temp_settings.overlay_progress_separator,
                                     sizeof(temp_settings.overlay_progress_separator));
                    if (ImGui::IsItemHovered()) {
                        char separator_tooltip_buffer[512];
                        snprintf(separator_tooltip_buffer, sizeof(separator_tooltip_buffer),
                                 "The character(s) drawn between segments anywhere a separator is shown:\n"
                                 "the overlay's top bar, the tracker info bar, the tracker info window's\n"
                                 "title bar, and the OS window title.\n"
                                 "Replace it if your tracker/overlay font does not have\n"
                                 "the pipe glyph. Up to %zu characters.\n"
                                 "Default: \"|\"",
                                 sizeof(temp_settings.overlay_progress_separator) - 1);
                        ImGui::SetTooltip("%s", separator_tooltip_buffer);
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
                                 "ignoring this setting.\n"
                                 "Default: Off", advancements_label_plural_uppercase);

                        ImGui::SetTooltip("%s", hide_completed_row_3_tooltip_buffer);
                    }

                    if (ImGui::DragFloat("Sub-Stat Cycle Interval (s)", &temp_settings.overlay_stat_cycle_speed, 0.1f,
                                         0.1f,
                                         60.0f,
                                         "%.3f s")) {
                        if (temp_settings.overlay_stat_cycle_speed < 0.1f)
                            temp_settings.overlay_stat_cycle_speed = 0.1f;
                        if (temp_settings.overlay_stat_cycle_speed > 60.0f)
                            temp_settings.overlay_stat_cycle_speed = 60.0f;
                    }
                    if (ImGui::IsItemHovered()) {
                        char substat_cycling_interval_tooltip_buffer[256];
                        snprintf(substat_cycling_interval_tooltip_buffer,
                                 sizeof(substat_cycling_interval_tooltip_buffer),
                                 "The time in seconds before cycling to the next sub-stat on a multi-stat goal on the overlay.\n"
                                 "Default: %.1f s", DEFAULT_OVERLAY_STAT_CYCLE_SPEED);
                        ImGui::SetTooltip("%s", substat_cycling_interval_tooltip_buffer);
                    }

                    // A cleared goal plays either the crop or the fade, never both, so the crop
                    // duration is locked while Fade Out below is on.
                    ImGui::BeginDisabled(temp_settings.overlay_clear_fade_enabled);
                    if (ImGui::DragFloat("Clear Animation (s)", &temp_settings.overlay_clear_animation, 0.01f, -10.0f,
                                         10.0f,
                                         "%.2f s")) {
                        if (temp_settings.overlay_clear_animation < -10.0f)
                            temp_settings.overlay_clear_animation = -10.0f;
                        if (temp_settings.overlay_clear_animation > 10.0f)
                            temp_settings.overlay_clear_animation = 10.0f;
                    }
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        char clear_animation_tooltip_buffer[640];
                        if (temp_settings.overlay_clear_fade_enabled) {
                            snprintf(clear_animation_tooltip_buffer, sizeof(clear_animation_tooltip_buffer),
                                     "Disabled because Fade Out is on. A cleared goal plays either the crop\n"
                                     "or the fade, never both. Uncheck Fade Out to crop again.\n"
                                     "Default: %.2f s", DEFAULT_OVERLAY_CLEAR_ANIMATION);
                        } else {
                            snprintf(clear_animation_tooltip_buffer, sizeof(clear_animation_tooltip_buffer),
                                     "How long a goal takes to crop away when it is cleared, instead of vanishing instantly.\n"
                                     "0.0 is instant. Positive values clear the icon upwards, negative values clear it downwards.\n"
                                     "Default: %.2f s", DEFAULT_OVERLAY_CLEAR_ANIMATION);
                        }
                        ImGui::SetTooltip("%s", clear_animation_tooltip_buffer);
                    }

                    // A fade needs real alpha to fade into, which only a transparent overlay has. On a
                    // solid background the half-faded pixels blend with the key color and survive the
                    // color key filter as a ghost, so the fade stays locked until Transparent is on.
                    ImGui::BeginDisabled(!temp_settings.overlay_transparent);
                    ImGui::Checkbox("Fade Out", &temp_settings.overlay_clear_fade_enabled);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        char clear_fade_tooltip_buffer[768];
                        if (temp_settings.overlay_transparent) {
                            snprintf(clear_fade_tooltip_buffer, sizeof(clear_fade_tooltip_buffer),
                                     "Fade a cleared goal out instead of cropping it away, replacing the\n"
                                     "Clear Animation above (a goal plays one or the other, never both).\n"
                                     "Applies to all three rows. Works like the pop-out stack's Fade Out\n"
                                     "in Compact Mode Settings.\n"
                                     "Default: %s", DEFAULT_OVERLAY_CLEAR_FADE_ENABLED ? "On" : "Off");
                        } else {
                            snprintf(clear_fade_tooltip_buffer, sizeof(clear_fade_tooltip_buffer),
                                     "Disabled because the overlay background is not transparent.\n"
                                     "Fading needs real transparency to fade into. Over a solid background\n"
                                     "the half-faded pixels blend with the background color and a color key\n"
                                     "filter leaves them behind as a ghost, so no fade is applied right now.\n"
                                     "Check Transparent next to the Overlay\n"
                                     "Background Color to unlock this again.\n"
                                     "Default: %s", DEFAULT_OVERLAY_CLEAR_FADE_ENABLED ? "On" : "Off");
                        }
                        ImGui::SetTooltip("%s", clear_fade_tooltip_buffer);
                    }

                    if (temp_settings.overlay_clear_fade_enabled) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(150.0f);
                        ImGui::BeginDisabled(!temp_settings.overlay_transparent);
                        if (ImGui::DragFloat("##overlay_clear_fade_time", &temp_settings.overlay_clear_fade_time,
                                             0.01f, COMPACT_STACK_FADE_TIME_MIN, COMPACT_STACK_FADE_TIME_MAX,
                                             "%.2f s")) {
                            if (temp_settings.overlay_clear_fade_time < COMPACT_STACK_FADE_TIME_MIN)
                                temp_settings.overlay_clear_fade_time = COMPACT_STACK_FADE_TIME_MIN;
                            if (temp_settings.overlay_clear_fade_time > COMPACT_STACK_FADE_TIME_MAX)
                                temp_settings.overlay_clear_fade_time = COMPACT_STACK_FADE_TIME_MAX;
                        }
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            char clear_fade_time_tooltip_buffer[512];
                            snprintf(clear_fade_time_tooltip_buffer, sizeof(clear_fade_time_tooltip_buffer),
                                     "How long the fade-out takes. It replaces the Clear Animation above,\n"
                                     "so the goal is gone once the fade has finished.\n"
                                     "Default: %.2f s", DEFAULT_OVERLAY_CLEAR_FADE_TIME);
                            ImGui::SetTooltip("%s", clear_fade_time_tooltip_buffer);
                        }
                    }
                } // End of Content & Behavior (belt/page only; hidden in Compact mode)

                // Only relevant to Page mode; reveal the page-flip interval when it is selected.
                if (overlay_page_mode) {
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::Text("Page Mode Settings");
                    if (ImGui::DragFloat("Page Switch Interval (s)", &temp_settings.overlay_page_interval, 0.1f, 0.1f,
                                         120.0f, "%.1f s")) {
                        if (temp_settings.overlay_page_interval < 0.1f) temp_settings.overlay_page_interval = 0.1f;
                        if (temp_settings.overlay_page_interval > 120.0f) temp_settings.overlay_page_interval = 120.0f;
                    }
                    if (ImGui::IsItemHovered()) {
                        char page_interval_tooltip_buffer[512];
                        snprintf(page_interval_tooltip_buffer, sizeof(page_interval_tooltip_buffer),
                                 "How long each static page of items is shown before the overlay cuts\n"
                                 "to the next page. A page holds as many items as fit the overlay width.\n"
                                 "Pressing %s while the overlay window is focused cuts to the next page immediately.\n"
                                 "Default: %.1f s", overlay_advance_label, DEFAULT_OVERLAY_PAGE_INTERVAL);
                        ImGui::SetTooltip("%s", page_interval_tooltip_buffer);
                    }

                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::Combo("Page Alignment", (int *) &temp_settings.overlay_page_align, "Left\0Center\0Right\0");
                    if (ImGui::IsItemHovered()) {
                        char page_align_tooltip_buffer[640];
                        snprintf(page_align_tooltip_buffer, sizeof(page_align_tooltip_buffer),
                                 "While more items remain than fit one page, pages repeat so each is full\n"
                                 "(no empty space). Once every remaining item fits a single page they stop\n"
                                 "repeating and clear as they complete; this sets how that not-full page is\n"
                                 "aligned. Left keeps the same left padding a full, centered page would have,\n"
                                 "so items stay put as the page empties. Center centers the remaining items.\n"
                                 "Right pushes them to where a full page's right edge would be.\n"
                                 "Default: Left");
                        ImGui::SetTooltip("%s", page_align_tooltip_buffer);
                    }
                }

                // Compact mode: the counter panel's 9-slice texture and geometry. Only relevant to
                // Compact mode, so shown only while it is selected.
                if (overlay_compact_mode) {
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::Text("Compact Mode Settings");

                    // Row-1 icon strip above the panel. Listed first to mirror the on-screen order
                    // (icons, then panel, then stack). Alignment follows the panel alignment below;
                    // horizontal spacing uses the compact Row 1 Icon Spacing control below.
                    ImGui::Text("Row 1 Icons");
                    ImGui::Checkbox("Show Row 1 Icons", &temp_settings.compact_show_row1_icons);
                    if (ImGui::IsItemHovered()) {
                        char compact_show_icons_tooltip_buffer[512];
                        snprintf(compact_show_icons_tooltip_buffer, sizeof(compact_show_icons_tooltip_buffer),
                                 "Shows the first-row icons (advancement criteria and sub-stats) in a\n"
                                 "strip above the panel, paged to fit the panel width and flipped on\n"
                                 "the interval below. Icons align with the panel and respect hidden\n"
                                 "goals (including the 'Show Hidden Goals' override). Horizontal spacing\n"
                                 "is set by the Row 1 Icon Spacing control below.");
                        ImGui::SetTooltip("%s", compact_show_icons_tooltip_buffer);
                    }

                    if (temp_settings.compact_show_row1_icons) {
                        if (ImGui::DragFloat("Icon Size##CompactRow1Icons", &temp_settings.compact_row1_icon_size,
                                             1.0f, COMPACT_ROW1_ICON_SIZE_MIN, COMPACT_ROW1_ICON_SIZE_MAX, "%.0f px")) {
                            if (temp_settings.compact_row1_icon_size < COMPACT_ROW1_ICON_SIZE_MIN)
                                temp_settings.compact_row1_icon_size = COMPACT_ROW1_ICON_SIZE_MIN;
                            if (temp_settings.compact_row1_icon_size > COMPACT_ROW1_ICON_SIZE_MAX)
                                temp_settings.compact_row1_icon_size = COMPACT_ROW1_ICON_SIZE_MAX;
                        }
                        if (ImGui::IsItemHovered()) {
                            char compact_icon_size_tooltip_buffer[384];
                            snprintf(compact_icon_size_tooltip_buffer, sizeof(compact_icon_size_tooltip_buffer),
                                     "Size in on-screen pixels of each icon in the strip above the panel.\n"
                                     "Defaults to the pop-out stack icon size so the two match.\n"
                                     "Default: %.0f px", DEFAULT_COMPACT_ROW1_ICON_SIZE);
                            ImGui::SetTooltip("%s", compact_icon_size_tooltip_buffer);
                        }

                        // The shared icon is drawn on top of the strip icon, so it can never be bigger
                        // than it: that is its upper bound, and shrinking Icon Size above drags it down too.
                        if (temp_settings.compact_icon_shared_size > temp_settings.compact_row1_icon_size)
                            temp_settings.compact_icon_shared_size = temp_settings.compact_row1_icon_size;
                        if (ImGui::DragFloat("Shared Icon Size##CompactRow1Icons",
                                             &temp_settings.compact_icon_shared_size,
                                             0.5f, COMPACT_ICON_SHARED_SIZE_MIN, temp_settings.compact_row1_icon_size,
                                             "%.0f")) {
                            if (temp_settings.compact_icon_shared_size < COMPACT_ICON_SHARED_SIZE_MIN)
                                temp_settings.compact_icon_shared_size = COMPACT_ICON_SHARED_SIZE_MIN;
                            if (temp_settings.compact_icon_shared_size > temp_settings.compact_row1_icon_size)
                                temp_settings.compact_icon_shared_size = temp_settings.compact_row1_icon_size;
                        }
                        if (ImGui::IsItemHovered()) {
                            char compact_icon_shared_tooltip_buffer[512];
                            snprintf(compact_icon_shared_tooltip_buffer, sizeof(compact_icon_shared_tooltip_buffer),
                                     "Size of the small parent icon overlaid on a shared criterion in the\n"
                                     "strip (a criterion belonging to more than one advancement). 0 hides it.\n"
                                     "It sits on the strip icon, so Icon Size (%.0f) is its upper bound and\n"
                                     "lowering that lowers this with it.\n"
                                     "Default: %.0f", temp_settings.compact_row1_icon_size,
                                     DEFAULT_COMPACT_ICON_SHARED_SIZE);
                            ImGui::SetTooltip("%s", compact_icon_shared_tooltip_buffer);
                        }

                        if (ImGui::DragFloat("Horizontal Icon Spacing##CompactRow1Icons",
                                             &temp_settings.compact_row1_spacing, 1.0f, 0.0f, 7680.0f, "%.0f px")) {
                            if (temp_settings.compact_row1_spacing < 0.0f) temp_settings.compact_row1_spacing = 0.0f;
                            if (temp_settings.compact_row1_spacing > 7680.0f)
                                temp_settings.compact_row1_spacing = 7680.0f;
                        }
                        if (ImGui::IsItemHovered()) {
                            char compact_row1_spacing_tooltip_buffer[256];
                            snprintf(compact_row1_spacing_tooltip_buffer, sizeof(compact_row1_spacing_tooltip_buffer),
                                     "Adjusts the horizontal gap (in pixels) between icons in the\n"
                                     "strip above the panel.\n"
                                     "Default: %.0f px", DEFAULT_COMPACT_ROW1_SPACING);
                            ImGui::SetTooltip("%s", compact_row1_spacing_tooltip_buffer);
                        }

                        if (ImGui::DragFloat("Icon Gap Below", &temp_settings.compact_icon_row_gap, 0.5f,
                                             COMPACT_ICON_ROW_GAP_MIN, COMPACT_ICON_ROW_GAP_MAX, "%.0f px")) {
                            if (temp_settings.compact_icon_row_gap < COMPACT_ICON_ROW_GAP_MIN)
                                temp_settings.compact_icon_row_gap = COMPACT_ICON_ROW_GAP_MIN;
                            if (temp_settings.compact_icon_row_gap > COMPACT_ICON_ROW_GAP_MAX)
                                temp_settings.compact_icon_row_gap = COMPACT_ICON_ROW_GAP_MAX;
                        }
                        if (ImGui::IsItemHovered()) {
                            char compact_icon_gap_tooltip_buffer[384];
                            snprintf(compact_icon_gap_tooltip_buffer, sizeof(compact_icon_gap_tooltip_buffer),
                                     "Vertical space in on-screen pixels between the icon strip and the\n"
                                     "panel below it. The overlay window grows to fit the icons.\n"
                                     "Default: %.0f px", DEFAULT_COMPACT_ICON_ROW_GAP);
                            ImGui::SetTooltip("%s", compact_icon_gap_tooltip_buffer);
                        }

                        if (ImGui::DragFloat("Icon Cycle Interval", &temp_settings.compact_icon_cycle_interval, 0.1f,
                                             COMPACT_ICON_CYCLE_INTERVAL_MIN, COMPACT_ICON_CYCLE_INTERVAL_MAX,
                                             "%.1f s")) {
                            if (temp_settings.compact_icon_cycle_interval < COMPACT_ICON_CYCLE_INTERVAL_MIN)
                                temp_settings.compact_icon_cycle_interval = COMPACT_ICON_CYCLE_INTERVAL_MIN;
                            if (temp_settings.compact_icon_cycle_interval > COMPACT_ICON_CYCLE_INTERVAL_MAX)
                                temp_settings.compact_icon_cycle_interval = COMPACT_ICON_CYCLE_INTERVAL_MAX;
                        }
                        if (ImGui::IsItemHovered()) {
                            char compact_icon_cycle_tooltip_buffer[384];
                            snprintf(compact_icon_cycle_tooltip_buffer, sizeof(compact_icon_cycle_tooltip_buffer),
                                     "How long each page of icons stays before flipping to the next set.\n"
                                     "Independent of the panel's Cycle Interval, but %s still\n"
                                     "advances the icons and the panel together.\n"
                                     "Default: %.1f s", overlay_advance_label, DEFAULT_COMPACT_ICON_CYCLE_INTERVAL);
                            ImGui::SetTooltip("%s", compact_icon_cycle_tooltip_buffer);
                        }

                        // A cleared icon plays either the crop or the fade, never both, so the crop
                        // duration is locked while Fade Out below is on.
                        ImGui::BeginDisabled(temp_settings.compact_row1_fade_enabled);
                        if (ImGui::DragFloat("Clear Animation (s)##CompactRow1Icons",
                                             &temp_settings.compact_row1_clear_animation, 0.01f, -10.0f, 10.0f,
                                             "%.2f s")) {
                            if (temp_settings.compact_row1_clear_animation < -10.0f)
                                temp_settings.compact_row1_clear_animation = -10.0f;
                            if (temp_settings.compact_row1_clear_animation > 10.0f)
                                temp_settings.compact_row1_clear_animation = 10.0f;
                        }
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            char compact_clear_anim_tooltip_buffer[640];
                            if (temp_settings.compact_row1_fade_enabled) {
                                snprintf(compact_clear_anim_tooltip_buffer, sizeof(compact_clear_anim_tooltip_buffer),
                                         "Disabled because Fade Out is on. A cleared icon plays either the\n"
                                         "crop or the fade, never both. Uncheck Fade Out to crop again.\n"
                                         "Default: %.2f s", DEFAULT_COMPACT_ROW1_CLEAR_ANIMATION);
                            } else {
                                snprintf(compact_clear_anim_tooltip_buffer, sizeof(compact_clear_anim_tooltip_buffer),
                                         "How long a strip icon takes to crop away when its goal is cleared,\n"
                                         "instead of vanishing instantly. 0.0 is instant. Positive values clear\n"
                                         "the icon upwards, negative values clear it downwards. Independent of\n"
                                         "the Belt/Page Clear Animation.\n"
                                         "Default: %.2f s", DEFAULT_COMPACT_ROW1_CLEAR_ANIMATION);
                            }
                            ImGui::SetTooltip("%s", compact_clear_anim_tooltip_buffer);
                        }

                        // Same gate as the pop-out stack's fade: half-faded pixels only survive on a
                        // transparent overlay, otherwise a color key filter leaves them behind as a ghost.
                        ImGui::BeginDisabled(!temp_settings.overlay_transparent);
                        ImGui::Checkbox("Fade Out##CompactRow1Icons", &temp_settings.compact_row1_fade_enabled);
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            char compact_row1_fade_tooltip_buffer[768];
                            if (temp_settings.overlay_transparent) {
                                snprintf(compact_row1_fade_tooltip_buffer, sizeof(compact_row1_fade_tooltip_buffer),
                                         "Fade a cleared strip icon out instead of cropping it away, replacing\n"
                                         "the Clear Animation above (an icon plays one or the other, never\n"
                                         "both). Works like the pop-out stack's Fade Out below.\n"
                                         "Default: %s", DEFAULT_COMPACT_ROW1_FADE_ENABLED ? "On" : "Off");
                            } else {
                                snprintf(compact_row1_fade_tooltip_buffer, sizeof(compact_row1_fade_tooltip_buffer),
                                         "Disabled because the overlay background is not transparent.\n"
                                         "Fading needs real transparency to fade into. Over a solid background\n"
                                         "the half-faded pixels blend with the background color and a color key\n"
                                         "filter leaves them behind as a ghost, so no fade is applied right now.\n"
                                         "Check Transparent next to the Overlay\n"
                                         "Background Color to unlock this again.\n"
                                         "Default: %s", DEFAULT_COMPACT_ROW1_FADE_ENABLED ? "On" : "Off");
                            }
                            ImGui::SetTooltip("%s", compact_row1_fade_tooltip_buffer);
                        }

                        if (temp_settings.compact_row1_fade_enabled) {
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(150.0f);
                            ImGui::BeginDisabled(!temp_settings.overlay_transparent);
                            if (ImGui::DragFloat("##compact_row1_fade_time", &temp_settings.compact_row1_fade_time,
                                                 0.01f, COMPACT_STACK_FADE_TIME_MIN, COMPACT_STACK_FADE_TIME_MAX,
                                                 "%.2f s")) {
                                if (temp_settings.compact_row1_fade_time < COMPACT_STACK_FADE_TIME_MIN)
                                    temp_settings.compact_row1_fade_time = COMPACT_STACK_FADE_TIME_MIN;
                                if (temp_settings.compact_row1_fade_time > COMPACT_STACK_FADE_TIME_MAX)
                                    temp_settings.compact_row1_fade_time = COMPACT_STACK_FADE_TIME_MAX;
                            }
                            ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                char compact_row1_fade_time_tooltip_buffer[512];
                                snprintf(compact_row1_fade_time_tooltip_buffer,
                                         sizeof(compact_row1_fade_time_tooltip_buffer),
                                         "How long the fade-out takes. It replaces the Clear Animation above,\n"
                                         "so the icon is gone once the fade has finished.\n"
                                         "Default: %.2f s", DEFAULT_COMPACT_ROW1_FADE_TIME);
                                ImGui::SetTooltip("%s", compact_row1_fade_time_tooltip_buffer);
                            }
                        }
                    }

                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::Text("Panel Texture:");
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", temp_settings.compact_panel_path);
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##CompactPanel")) {
                        char selected_file[MAX_PATH_LENGTH];
                        if (open_gui_texture_dialog(selected_file, sizeof(selected_file))) {
                            strncpy(temp_settings.compact_panel_path, selected_file,
                                    sizeof(temp_settings.compact_panel_path) - 1);
                            temp_settings.compact_panel_path[sizeof(temp_settings.compact_panel_path) - 1] = '\0';
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_panel_tex_tooltip_buffer[512];
                        snprintf(compact_panel_tex_tooltip_buffer, sizeof(compact_panel_tex_tooltip_buffer),
                                 "The 9-slice panel texture drawn behind the counter.\n"
                                 "Use a small square texture; its border pixels repeat to fit any size.\n"
                                 "Must be a .png or .gif inside the %s folder.\n"
                                 "Default: %s", get_gui_display_path(), DEFAULT_COMPACT_PANEL_PATH);
                        ImGui::SetTooltip("%s", compact_panel_tex_tooltip_buffer);
                    }

                    if (ImGui::DragInt("Panel Pixel Scale", &temp_settings.compact_panel_pixel_scale, 0.1f, 1, 16)) {
                        if (temp_settings.compact_panel_pixel_scale < 1) temp_settings.compact_panel_pixel_scale = 1;
                        if (temp_settings.compact_panel_pixel_scale > 16) temp_settings.compact_panel_pixel_scale = 16;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_panel_scale_tooltip_buffer[512];
                        snprintf(compact_panel_scale_tooltip_buffer, sizeof(compact_panel_scale_tooltip_buffer),
                                 "On-screen pixels drawn per source texture pixel, so the panel border\n"
                                 "matches the pixel size of the item backgrounds.\n"
                                 "Default: %d", DEFAULT_COMPACT_PANEL_PIXEL_SCALE);
                        ImGui::SetTooltip("%s", compact_panel_scale_tooltip_buffer);
                    }

                    int compact_insets[4] = {
                        temp_settings.compact_panel_inset_left, temp_settings.compact_panel_inset_right,
                        temp_settings.compact_panel_inset_top, temp_settings.compact_panel_inset_bottom
                    };
                    if (ImGui::DragInt4("Panel Border (L/R/T/B)", compact_insets, 0.1f, 0, 64)) {
                        for (int i = 0; i < 4; i++) {
                            if (compact_insets[i] < 0) compact_insets[i] = 0;
                            if (compact_insets[i] > 64) compact_insets[i] = 64;
                        }
                        temp_settings.compact_panel_inset_left = compact_insets[0];
                        temp_settings.compact_panel_inset_right = compact_insets[1];
                        temp_settings.compact_panel_inset_top = compact_insets[2];
                        temp_settings.compact_panel_inset_bottom = compact_insets[3];
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_panel_border_tooltip_buffer[768];
                        snprintf(compact_panel_border_tooltip_buffer, sizeof(compact_panel_border_tooltip_buffer),
                                 "Border thickness in the texture's OWN pixels, per edge (Left / Right / Top / Bottom).\n"
                                 "The texture is cut into a 3x3 grid: the four corners of this size are drawn without\n"
                                 "stretching, the top/bottom edges repeat sideways, the left/right edges repeat down,\n"
                                 "and the leftover middle fills the rest. That keeps the border crisp while the panel\n"
                                 "grows to fit the text. Example: the default 5x5 panel uses 2, leaving a 1px center.\n"
                                 "Each of these pixels is then multiplied on screen by Panel Pixel Scale.\n"
                                 "Default: %d on every edge", DEFAULT_COMPACT_PANEL_INSET);
                        ImGui::SetTooltip("%s", compact_panel_border_tooltip_buffer);
                    }

                    if (ImGui::DragFloat("Panel Padding", &temp_settings.compact_panel_padding, 0.5f, 0.0f, 128.0f,
                                         "%.0f px")) {
                        if (temp_settings.compact_panel_padding < 0.0f) temp_settings.compact_panel_padding = 0.0f;
                        if (temp_settings.compact_panel_padding > 128.0f) temp_settings.compact_panel_padding = 128.0f;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_panel_pad_tooltip_buffer[512];
                        snprintf(compact_panel_pad_tooltip_buffer, sizeof(compact_panel_pad_tooltip_buffer),
                                 "Space in on-screen pixels between the counter text and the panel border.\n"
                                 "Default: %.0f px", DEFAULT_COMPACT_PANEL_PADDING);
                        ImGui::SetTooltip("%s", compact_panel_pad_tooltip_buffer);
                    }

                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::Combo("Panel Alignment", (int *) &temp_settings.compact_panel_align,
                                 "Left\0Center\0Right\0");
                    if (ImGui::IsItemHovered()) {
                        const char *compact_align_names[] = {"Left", "Center", "Right"};
                        char compact_panel_align_tooltip_buffer[640];
                        snprintf(compact_panel_align_tooltip_buffer, sizeof(compact_panel_align_tooltip_buffer),
                                 "How the panel sits within the auto-fitted overlay window.\n"
                                 "Left keeps the panel's left edge fixed as the background grows (best for\n"
                                 "left-aligning the overlay in OBS), Center keeps it centered, Right keeps its\n"
                                 "right edge fixed. The pop-out stack below always left-aligns to the panel's left edge.\n"
                                 "Default: %s", compact_align_names[DEFAULT_COMPACT_PANEL_ALIGN]);
                        ImGui::SetTooltip("%s", compact_panel_align_tooltip_buffer);
                    }

                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::Text("Panel Content");

                    // What the panel cycles through: any number of whole-section type counts, plus any
                    // number of individual goals shown by name. Only what's present in the loaded template
                    // is selectable; presence and labels follow the same MC-version rules as the tracker's
                    // section separators (Advancement Criteria and Recipe Criteria are separate categories).
                    const TemplateData *ctd = (t && t->template_data) ? t->template_data : nullptr;
                    // While a different template is picked but not applied yet (a loaded preset, or an
                    // edited version/category/flag), the selections belong to THAT template. Pruning them
                    // against the still-loaded one would silently drop every goal it doesn't contain.
                    bool compact_template_pending = ctd && !settings_template_is_loaded(&temp_settings, t);
                    if (!ctd) {
                        ImGui::TextDisabled("Load a template to choose what the panel cycles through.");
                    } else if (compact_template_pending) {
                        int pending_cycle_types = 0;
                        for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++)
                            if (temp_settings.compact_cycle_type[i]) pending_cycle_types++;
                        char pending_cycle_buffer[320];
                        snprintf(pending_cycle_buffer, sizeof(pending_cycle_buffer),
                                 "%d goal %s and %d individual %s selected for the template you picked.\n"
                                 "Click 'Apply Settings' to load that template, then edit the selection here.",
                                 pending_cycle_types, pending_cycle_types == 1 ? "type" : "types",
                                 temp_settings.compact_cycle_item_count,
                                 temp_settings.compact_cycle_item_count == 1 ? "goal" : "goals");
                        ImGui::TextDisabled("%s", pending_cycle_buffer);
                    } else {
                        // Keep the selection lists free of goals that aren't in the currently loaded
                        // template, so a template switch (here or via the editor) doesn't leave stale
                        // items in the dropdowns. Prune the working copy AND the saved baseline so this
                        // cleanup never registers as an unsaved change.
                        settings_prune_compact_cycle_items(&temp_settings, ctd);
                        settings_prune_compact_cycle_items(&saved_settings, ctd);

                        MC_Version cver = settings_get_version_from_string(temp_settings.version_str);
                        bool modern = (cver >= MC_VERSION_1_12);
                        CompactCounter cc[COMPACT_COUNTER_TYPE_COUNT];
                        // The panel cycles whole-section counts, which are the real totals: a hidden
                        // goal is still part of "Advancements 12/80", so it counts here either way.
                        compact_compute_type_counters(ctd, cver, cc, true);

                        // Per-dropdown range-select anchors for Shift+Click (template index of the last
                        // row clicked without shift). -1 = no anchor yet.
                        static int s_type_anchor = -1;
                        static int s_item_anchor[COMPACT_COUNTER_TYPE_COUNT];
                        static bool s_anchors_init = false;
                        if (!s_anchors_init) {
                            for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++) s_item_anchor[i] = -1;
                            s_anchors_init = true;
                        }

                        // Goal-type multiselect + per-category individual-goal combos (shared with the
                        // pop-out stack below).
                        CompactSelTarget cycle_tgt = {
                            temp_settings.compact_cycle_type,
                            temp_settings.compact_cycle_items,
                            &temp_settings.compact_cycle_item_count
                        };
                        compact_selection_ui("cycle", ctd, cc, modern, "Main Goal Types", cycle_tgt,
                                             &s_type_anchor, s_item_anchor, true,
                                             temp_settings.overlay_show_hidden_goals);

                        // Never allow an empty cycle across ALL these dropdowns. If no goal type is
                        // selected AND no individual goal is selected, keep the first present type on so
                        // the cycle always shows something (and the dropdown shows a check). If an
                        // individual goal IS selected, the goal types may be emptied entirely (to show
                        // only that goal). The item list was already pruned to present goals above, so
                        // compact_cycle_item_count reflects only real selections. Applied to the saved
                        // baseline too so this correction never registers as an "unsaved change".
                        auto ensure_something_selected = [&](AppSettings *s) {
                            if (s->compact_cycle_item_count > 0) return;
                            bool any = false;
                            int first = -1;
                            for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++) {
                                if (cc[i].total <= 0) continue;
                                if (first < 0) first = i;
                                if (s->compact_cycle_type[i]) {
                                    any = true;
                                    break;
                                }
                            }
                            if (!any && first >= 0) s->compact_cycle_type[first] = true;
                        };
                        ensure_something_selected(&temp_settings);
                        ensure_something_selected(&saved_settings);
                    }

                    if (ImGui::DragFloat("Cycle Interval", &temp_settings.compact_cycle_interval, 0.1f,
                                         COMPACT_CYCLE_INTERVAL_MIN, COMPACT_CYCLE_INTERVAL_MAX, "%.1f s")) {
                        if (temp_settings.compact_cycle_interval < COMPACT_CYCLE_INTERVAL_MIN)
                            temp_settings.compact_cycle_interval = COMPACT_CYCLE_INTERVAL_MIN;
                        if (temp_settings.compact_cycle_interval > COMPACT_CYCLE_INTERVAL_MAX)
                            temp_settings.compact_cycle_interval = COMPACT_CYCLE_INTERVAL_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_cycle_tooltip_buffer[512];
                        snprintf(compact_cycle_tooltip_buffer, sizeof(compact_cycle_tooltip_buffer),
                                 "How long each selected entry stays on the panel before the\n"
                                 "cycle advances to the next one. With a single entry selected the\n"
                                 "panel is static. Press %s while the overlay window is focused\n"
                                 "to jump to the next goal, which also flips the Row 1 icons.\n"
                                 "Default: %.1f s", overlay_advance_label, DEFAULT_COMPACT_CYCLE_INTERVAL);
                        ImGui::SetTooltip("%s", compact_cycle_tooltip_buffer);
                    }

                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::Text("Stack Content");

                    // Independent of the panel cycle: which goals may slide out below the panel as they
                    // progress or complete. Same additive selection model as the cycle (type OR goal).
                    const TemplateData *sctd = (t && t->template_data) ? t->template_data : nullptr;
                    if (!sctd) {
                        ImGui::TextDisabled("Load a template to choose what pops into the stack.");
                    } else if (compact_template_pending) {
                        int pending_stack_types = 0;
                        for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++)
                            if (temp_settings.compact_stack_type[i]) pending_stack_types++;
                        char pending_stack_buffer[320];
                        snprintf(pending_stack_buffer, sizeof(pending_stack_buffer),
                                 "%d goal %s and %d individual %s selected for the template you picked.\n"
                                 "Click 'Apply Settings' to load that template, then edit the selection here.",
                                 pending_stack_types, pending_stack_types == 1 ? "type" : "types",
                                 temp_settings.compact_stack_item_count,
                                 temp_settings.compact_stack_item_count == 1 ? "goal" : "goals");
                        ImGui::TextDisabled("%s", pending_stack_buffer);
                    } else {
                        settings_prune_compact_stack_items(&temp_settings, sctd);
                        settings_prune_compact_stack_items(&saved_settings, sctd);

                        MC_Version sver = settings_get_version_from_string(temp_settings.version_str);
                        bool smodern = (sver >= MC_VERSION_1_12);
                        CompactCounter scc[COMPACT_COUNTER_TYPE_COUNT];
                        // The stack pops individual goals, and a hidden goal never pops, so these
                        // counts leave hidden goals out: an optimized template shows how many
                        // advancements can really appear, and "Show Hidden Goals" brings the rest back.
                        compact_compute_type_counters(sctd, sver, scc, temp_settings.overlay_show_hidden_goals);

                        // In the stack, a whole advancement / recipe only pops when it is SIMPLE (no
                        // trackable criteria); complex ones pop through their own criteria dropdown below.
                        // So recompute these two type rows to count only simple goals and relabel them,
                        // instead of the real totals the cycle uses. Mirrors the counter's hidden rule
                        // (a hidden goal counts only when "Show Hidden Goals" is on).
                        {
                            bool count_hidden = temp_settings.overlay_show_hidden_goals;
                            int simple_adv_done = 0, simple_adv_total = 0;
                            int simple_rec_done = 0, simple_rec_total = 0;
                            for (int i = 0; i < sctd->advancement_count; i++) {
                                const TrackableCategory *a = sctd->advancements[i];
                                if (!a || a->criteria_count > 0) continue; // complex -> chosen in the dropdown below
                                if (!count_hidden && a->is_hidden) continue;
                                if (a->is_recipe) {
                                    simple_rec_total++;
                                    if (a->done) simple_rec_done++;
                                } else {
                                    simple_adv_total++;
                                    if (a->done) simple_adv_done++;
                                }
                            }
                            CompactCounter *sadv = &scc[COMPACT_COUNTER_ADVANCEMENTS];
                            sadv->completed = simple_adv_done;
                            sadv->total = simple_adv_total;
                            snprintf(sadv->label, sizeof(sadv->label), "Simple %s",
                                     smodern ? "Advancements" : "Achievements");
                            if (smodern) {
                                CompactCounter *srec = &scc[COMPACT_COUNTER_RECIPES];
                                srec->completed = simple_rec_done;
                                srec->total = simple_rec_total;
                                snprintf(srec->label, sizeof(srec->label), "Simple Recipes");
                            }
                        }

                        static int s_stack_type_anchor = -1;
                        static int s_stack_item_anchor[COMPACT_COUNTER_TYPE_COUNT];
                        static bool s_stack_anchors_init = false;
                        if (!s_stack_anchors_init) {
                            for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++) s_stack_item_anchor[i] = -1;
                            s_stack_anchors_init = true;
                        }

                        CompactSelTarget stack_tgt = {
                            temp_settings.compact_stack_type,
                            temp_settings.compact_stack_items,
                            &temp_settings.compact_stack_item_count
                        };
                        compact_selection_ui("stack", sctd, scc, smodern, "Stack Goal Types", stack_tgt,
                                             &s_stack_type_anchor, s_stack_item_anchor, false,
                                             temp_settings.overlay_show_hidden_goals);

                        // Per-type pop trigger. Only the counting types can pop mid-progress, so the
                        // rest (advancements, recipes, unlocks, criteria) are left out: they have
                        // nothing but a done flag and always pop on completion.
                        auto progress_type_shown = [&](int i) -> bool {
                            return scc[i].total > 0 && compact_type_has_progress((OverlayCompactCounterType) i);
                        };
                        int progress_sel_count = 0, progress_present = 0;
                        for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++) {
                            if (!progress_type_shown(i)) continue;
                            progress_present++;
                            if (temp_settings.compact_stack_pop_on_progress[i]) progress_sel_count++;
                        }
                        if (progress_present > 0) {
                            static int s_stack_progress_anchor = -1;
                            char progress_preview[48];
                            snprintf(progress_preview, sizeof(progress_preview), "%d selected", progress_sel_count);
                            if (ImGui::BeginCombo("Pop On Progress", progress_preview)) {
                                for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++) {
                                    if (!progress_type_shown(i)) continue;
                                    bool sel = temp_settings.compact_stack_pop_on_progress[i];
                                    if (ImGui::Selectable(scc[i].label, sel, ImGuiSelectableFlags_NoAutoClosePopups)) {
                                        bool new_state = !sel;
                                        if (ImGui::GetIO().KeyShift && s_stack_progress_anchor >= 0 &&
                                            s_stack_progress_anchor != i) {
                                            int lo = s_stack_progress_anchor < i ? s_stack_progress_anchor : i;
                                            int hi = s_stack_progress_anchor < i ? i : s_stack_progress_anchor;
                                            for (int k = lo; k <= hi; k++)
                                                if (progress_type_shown(k))
                                                    temp_settings.compact_stack_pop_on_progress[k] = new_state;
                                        } else {
                                            temp_settings.compact_stack_pop_on_progress[i] = new_state;
                                        }
                                        s_stack_progress_anchor = i;
                                    }
                                }
                                ImGui::EndCombo();
                            }
                            if (ImGui::IsItemHovered()) {
                                char compact_pop_progress_tooltip_buffer[700];
                                snprintf(compact_pop_progress_tooltip_buffer,
                                         sizeof(compact_pop_progress_tooltip_buffer),
                                         "Checked: the type pops every time it counts up, so the stack follows a\n"
                                         "goal as it climbs. Unchecked: it only pops once the goal completes -\n"
                                         "for a multi-stage goal, each time it clears a stage.\n"
                                         "Only types that count toward a target are listed. Advancements, recipes,\n"
                                         "unlocks and criteria are either done or not, so they always pop on\n"
                                         "completion. Shift+Click to range-select.\n"
                                         "Default: %s", DEFAULT_COMPACT_STACK_POP_ON_PROGRESS
                                                            ? "all types pop on progress"
                                                            : "completion only");
                                ImGui::SetTooltip("%s", compact_pop_progress_tooltip_buffer);
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("All##stackprogall"))
                                for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++)
                                    if (progress_type_shown(i)) temp_settings.compact_stack_pop_on_progress[i] = true;
                            ImGui::SameLine();
                            if (ImGui::SmallButton("None##stackprognone"))
                                for (int i = 0; i < COMPACT_COUNTER_TYPE_COUNT; i++)
                                    if (progress_type_shown(i)) temp_settings.compact_stack_pop_on_progress[i] = false;
                        }
                    }

                    ImGui::Checkbox("Show Completion Markers",
                                    &temp_settings.compact_show_completion_markers);
                    if (ImGui::IsItemHovered()) {
                        char compact_show_markers_tooltip_buffer[700];
                        snprintf(compact_show_markers_tooltip_buffer, sizeof(compact_show_markers_tooltip_buffer),
                                 "Show the [o]/[a]/[x] completion markers on manually- and auto-completable\n"
                                 "goals (stats, custom goals, counters), both on the panel's count line and\n"
                                 "on the pop-out stack: [o] not done, [a] auto-completed, [x] checked off by hand.\n"
                                 "Off also stops a bare completion from popping a line.\n"
                                 "Default: %s", DEFAULT_COMPACT_SHOW_COMPLETION_MARKERS ? "On" : "Off");
                        ImGui::SetTooltip("%s", compact_show_markers_tooltip_buffer);
                    }

                    if (ImGui::DragFloat("Stack Gap Below", &temp_settings.compact_stack_row_gap, 0.5f,
                                         COMPACT_STACK_ROW_GAP_MIN, COMPACT_STACK_ROW_GAP_MAX, "%.0f px")) {
                        if (temp_settings.compact_stack_row_gap < COMPACT_STACK_ROW_GAP_MIN)
                            temp_settings.compact_stack_row_gap = COMPACT_STACK_ROW_GAP_MIN;
                        if (temp_settings.compact_stack_row_gap > COMPACT_STACK_ROW_GAP_MAX)
                            temp_settings.compact_stack_row_gap = COMPACT_STACK_ROW_GAP_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_stack_gap_tooltip_buffer[384];
                        snprintf(compact_stack_gap_tooltip_buffer, sizeof(compact_stack_gap_tooltip_buffer),
                                 "Vertical space in on-screen pixels between the panel and\n"
                                 "the pop-out stack below it.\n"
                                 "Default: %.0f px", DEFAULT_COMPACT_STACK_ROW_GAP);
                        ImGui::SetTooltip("%s", compact_stack_gap_tooltip_buffer);
                    }

                    if (ImGui::DragInt("Max Stack Lines", &temp_settings.compact_stack_max_lines, 0.1f,
                                       COMPACT_STACK_MAX_LINES_MIN, COMPACT_STACK_MAX_LINES_MAX)) {
                        if (temp_settings.compact_stack_max_lines < COMPACT_STACK_MAX_LINES_MIN)
                            temp_settings.compact_stack_max_lines = COMPACT_STACK_MAX_LINES_MIN;
                        if (temp_settings.compact_stack_max_lines > COMPACT_STACK_MAX_LINES_MAX)
                            temp_settings.compact_stack_max_lines = COMPACT_STACK_MAX_LINES_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_stack_lines_tooltip_buffer[640];
                        snprintf(compact_stack_lines_tooltip_buffer, sizeof(compact_stack_lines_tooltip_buffer),
                                 "How many pop-out lines can show below the panel at once. A completed\n"
                                 "criterion or sub-stat shows a 2-line group (parent then criterion), so it\n"
                                 "uses 2 of these lines. The window reserves this much height below the\n"
                                 "panel; when the stack is full the oldest line leaves at the bottom.\n"
                                 "Default: %d", DEFAULT_COMPACT_STACK_MAX_LINES);
                        ImGui::SetTooltip("%s", compact_stack_lines_tooltip_buffer);
                    }

                    if (ImGui::DragFloat("Hold Time", &temp_settings.compact_stack_hold_time, 0.1f,
                                         COMPACT_STACK_HOLD_TIME_MIN, COMPACT_STACK_HOLD_TIME_MAX, "%.1f s")) {
                        if (temp_settings.compact_stack_hold_time < COMPACT_STACK_HOLD_TIME_MIN)
                            temp_settings.compact_stack_hold_time = COMPACT_STACK_HOLD_TIME_MIN;
                        if (temp_settings.compact_stack_hold_time > COMPACT_STACK_HOLD_TIME_MAX)
                            temp_settings.compact_stack_hold_time = COMPACT_STACK_HOLD_TIME_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_stack_hold_tooltip_buffer[512];
                        snprintf(compact_stack_hold_tooltip_buffer, sizeof(compact_stack_hold_tooltip_buffer),
                                 "How long a pop-out line stays before it leaves the stack.\n"
                                 "A fresh increment on a line already showing resets this timer.\n"
                                 "Default: %.1f s", DEFAULT_COMPACT_STACK_HOLD_TIME);
                        ImGui::SetTooltip("%s", compact_stack_hold_tooltip_buffer);
                    }

                    if (ImGui::DragFloat("Animation Time", &temp_settings.compact_stack_rise_time, 0.01f,
                                         COMPACT_STACK_RISE_TIME_MIN, COMPACT_STACK_RISE_TIME_MAX, "%.2f s")) {
                        if (temp_settings.compact_stack_rise_time < COMPACT_STACK_RISE_TIME_MIN)
                            temp_settings.compact_stack_rise_time = COMPACT_STACK_RISE_TIME_MIN;
                        if (temp_settings.compact_stack_rise_time > COMPACT_STACK_RISE_TIME_MAX)
                            temp_settings.compact_stack_rise_time = COMPACT_STACK_RISE_TIME_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_stack_rise_tooltip_buffer[512];
                        snprintf(compact_stack_rise_tooltip_buffer, sizeof(compact_stack_rise_tooltip_buffer),
                                 "How long a pop-out takes to slide out from under the panel into its\n"
                                 "place in the stack. 0 shows it instantly (no slide).\n"
                                 "Default: %.2f s", DEFAULT_COMPACT_STACK_RISE_TIME);
                        ImGui::SetTooltip("%s", compact_stack_rise_tooltip_buffer);
                    }

                    // A fade needs real alpha to fade into, which only a transparent overlay has. On a
                    // solid background the half-faded pixels blend with the key color and survive the
                    // color key filter as a ghost, so the fade stays locked until Transparent is on.
                    ImGui::BeginDisabled(!temp_settings.overlay_transparent);
                    ImGui::Checkbox("Fade Out", &temp_settings.compact_stack_fade_enabled);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        char compact_stack_fade_tooltip_buffer[768];
                        if (temp_settings.overlay_transparent) {
                            snprintf(compact_stack_fade_tooltip_buffer, sizeof(compact_stack_fade_tooltip_buffer),
                                     "Fade a pop-out out when it leaves the stack instead of removing it\n"
                                     "instantly. The lines below it move up while it fades.\n"
                                     "Default: %s", DEFAULT_COMPACT_STACK_FADE_ENABLED ? "On" : "Off");
                        } else {
                            snprintf(compact_stack_fade_tooltip_buffer, sizeof(compact_stack_fade_tooltip_buffer),
                                     "Disabled because the overlay background is not transparent.\n"
                                     "Fading needs real transparency to fade into. Over a solid background\n"
                                     "the half-faded pixels blend with the background color and a color key\n"
                                     "filter leaves them behind as a ghost, so no fade is applied right now.\n"
                                     "Check Transparent next to the Overlay\n"
                                     "Background Color to unlock this again.\n"
                                     "Default: %s", DEFAULT_COMPACT_STACK_FADE_ENABLED ? "On" : "Off");
                        }
                        ImGui::SetTooltip("%s", compact_stack_fade_tooltip_buffer);
                    }

                    if (temp_settings.compact_stack_fade_enabled) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(150.0f);
                        ImGui::BeginDisabled(!temp_settings.overlay_transparent);
                        if (ImGui::DragFloat("##compact_stack_fade_time", &temp_settings.compact_stack_fade_time, 0.01f,
                                             COMPACT_STACK_FADE_TIME_MIN, COMPACT_STACK_FADE_TIME_MAX, "%.2f s")) {
                            if (temp_settings.compact_stack_fade_time < COMPACT_STACK_FADE_TIME_MIN)
                                temp_settings.compact_stack_fade_time = COMPACT_STACK_FADE_TIME_MIN;
                            if (temp_settings.compact_stack_fade_time > COMPACT_STACK_FADE_TIME_MAX)
                                temp_settings.compact_stack_fade_time = COMPACT_STACK_FADE_TIME_MAX;
                        }
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            char compact_stack_fade_time_tooltip_buffer[512];
                            snprintf(compact_stack_fade_time_tooltip_buffer,
                                     sizeof(compact_stack_fade_time_tooltip_buffer),
                                     "How long the fade-out takes. The Hold Time runs first, then the line\n"
                                     "fades for this long before it is gone.\n"
                                     "Default: %.2f s", DEFAULT_COMPACT_STACK_FADE_TIME);
                            ImGui::SetTooltip("%s", compact_stack_fade_time_tooltip_buffer);
                        }
                    }

                    if (ImGui::DragFloat("Pop Icon Size", &temp_settings.compact_pop_icon_size, 0.5f,
                                         COMPACT_POP_ICON_SIZE_MIN, COMPACT_POP_ICON_SIZE_MAX, "%.0f")) {
                        if (temp_settings.compact_pop_icon_size < COMPACT_POP_ICON_SIZE_MIN)
                            temp_settings.compact_pop_icon_size = COMPACT_POP_ICON_SIZE_MIN;
                        if (temp_settings.compact_pop_icon_size > COMPACT_POP_ICON_SIZE_MAX)
                            temp_settings.compact_pop_icon_size = COMPACT_POP_ICON_SIZE_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_pop_icon_tooltip_buffer[512];
                        snprintf(compact_pop_icon_tooltip_buffer, sizeof(compact_pop_icon_tooltip_buffer),
                                 "On-screen size of the icon on each pop-out line (and the line height).\n"
                                 "Default: %.0f", DEFAULT_COMPACT_POP_ICON_SIZE);
                        ImGui::SetTooltip("%s", compact_pop_icon_tooltip_buffer);
                    }

                    // The shared icon is drawn on top of the pop-out icon, so it can never be bigger than
                    // it: that is its upper bound, and shrinking Pop Icon Size above drags it down too.
                    if (temp_settings.compact_stack_shared_icon_size > temp_settings.compact_pop_icon_size)
                        temp_settings.compact_stack_shared_icon_size = temp_settings.compact_pop_icon_size;
                    if (ImGui::DragFloat("Shared Icon Size", &temp_settings.compact_stack_shared_icon_size, 0.5f,
                                         COMPACT_STACK_SHARED_ICON_SIZE_MIN, temp_settings.compact_pop_icon_size,
                                         "%.0f")) {
                        if (temp_settings.compact_stack_shared_icon_size < COMPACT_STACK_SHARED_ICON_SIZE_MIN)
                            temp_settings.compact_stack_shared_icon_size = COMPACT_STACK_SHARED_ICON_SIZE_MIN;
                        if (temp_settings.compact_stack_shared_icon_size > temp_settings.compact_pop_icon_size)
                            temp_settings.compact_stack_shared_icon_size = temp_settings.compact_pop_icon_size;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_stack_shared_tooltip_buffer[512];
                        snprintf(compact_stack_shared_tooltip_buffer, sizeof(compact_stack_shared_tooltip_buffer),
                                 "Size of the small parent icon overlaid on a shared criterion's pop-out\n"
                                 "line (a criterion belonging to more than one advancement). 0 hides it.\n"
                                 "It sits on the pop-out icon, so Pop Icon Size (%.0f) is its upper bound\n"
                                 "and lowering that lowers this with it.\n"
                                 "Default: %.0f", temp_settings.compact_pop_icon_size,
                                 DEFAULT_COMPACT_STACK_SHARED_ICON_SIZE);
                        ImGui::SetTooltip("%s", compact_stack_shared_tooltip_buffer);
                    }
                }

                // The scroll speed, per-row custom speeds and auto-freeze toggles only affect the
                // scrolling belt mode, so hide the whole Scrolling section for Page and Compact modes.
                if (!overlay_page_mode && !overlay_compact_mode) {
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::Text("Scrolling");

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
                                 "A value of 1.0 scrolls 1440 pixels (default width) in 24 seconds.\n"
                                 "Holding %s while the overlay window is focused speeds up the animation.\n"
                                 "Default: %.2f", overlay_advance_label, DEFAULT_OVERLAY_SCROLL_SPEED);
                        ImGui::SetTooltip("%s", overlay_scroll_speed_tooltip_buffer);
                    }

                    // --- Row 1 Custom Speed + Freeze ---
                    ImGui::Checkbox("Row 1 Custom Speed", &temp_settings.overlay_row1_custom_scroll_speed_enabled);
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[512];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Give Row 1 (criteria and sub-stat icons) its own scroll speed,\n"
                                 "ignoring the global Overlay Scroll Speed above.\n"
                                 "Negative scrolls right-to-left; 0.0 is static.\n"
                                 "Default: Off (custom value default %.2f).",
                                 DEFAULT_OVERLAY_SCROLL_SPEED);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    if (temp_settings.overlay_row1_custom_scroll_speed_enabled) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(150.0f);
                        if (ImGui::DragFloat("##row1_speed", &temp_settings.overlay_row1_scroll_speed, 0.001f,
                                             -25.00f, 25.00f, "%.3f")) {
                            if (temp_settings.overlay_row1_scroll_speed < -25.0f)
                                temp_settings.overlay_row1_scroll_speed = -25.0f;
                            if (temp_settings.overlay_row1_scroll_speed > 25.0f)
                                temp_settings.overlay_row1_scroll_speed = 25.0f;
                        }
                    }

                    ImGui::Checkbox("Row 1 Auto-Freeze", &temp_settings.overlay_row1_freeze_enabled);
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[512];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "When Row 1's still-visible items fit within the overlay width, stop scrolling\n"
                                 "and show each item once, statically (measured with text width). Useful once only\n"
                                 "a few items remain so they no longer repeat across the row.\n"
                                 "Default: On");
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    if (temp_settings.overlay_row1_freeze_enabled) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::Combo("##row1_align", (int *) &temp_settings.overlay_row1_freeze_align,
                                     "Left\0Center\0Right\0");
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[256];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "How to align Row 1's frozen items within the overlay width.\n"
                                     "Default: Left");
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }
                    }

                    // --- Row 2 Custom Speed + Freeze ---
                    ImGui::Checkbox("Row 2 Custom Speed", &temp_settings.overlay_row2_custom_scroll_speed_enabled);
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[512];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Give Row 2 (%s, Unlocks, and items forced to Row 2) its own scroll speed,\n"
                                 "ignoring the global Overlay Scroll Speed above.\n"
                                 "Negative scrolls right-to-left; 0.0 is static.\n"
                                 "Default: Off (custom value default %.2f).",
                                 advancements_label_plural_uppercase, DEFAULT_OVERLAY_SCROLL_SPEED);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    if (temp_settings.overlay_row2_custom_scroll_speed_enabled) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(150.0f);
                        if (ImGui::DragFloat("##row2_speed", &temp_settings.overlay_row2_scroll_speed, 0.001f,
                                             -25.00f, 25.00f, "%.3f")) {
                            if (temp_settings.overlay_row2_scroll_speed < -25.0f)
                                temp_settings.overlay_row2_scroll_speed = -25.0f;
                            if (temp_settings.overlay_row2_scroll_speed > 25.0f)
                                temp_settings.overlay_row2_scroll_speed = 25.0f;
                        }
                    }

                    ImGui::Checkbox("Row 2 Auto-Freeze", &temp_settings.overlay_row2_freeze_enabled);
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[512];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "When Row 2's still-visible items fit within the overlay width, stop scrolling\n"
                                 "and show each item once, statically (measured with text width). Useful once only\n"
                                 "a few items remain so they no longer repeat across the row.\n"
                                 "Default: On");
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    if (temp_settings.overlay_row2_freeze_enabled) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::Combo("##row2_align", (int *) &temp_settings.overlay_row2_freeze_align,
                                     "Left\0Center\0Right\0");
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[256];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "How to align Row 2's frozen items within the overlay width.\n"
                                     "Default: Left");
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }
                    }

                    // --- Row 3 Custom Speed + Freeze ---
                    ImGui::Checkbox("Row 3 Custom Speed", &temp_settings.overlay_row3_custom_scroll_speed_enabled);
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[512];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Give Row 3 (Stats, Goals, and items forced to Row 3) its own scroll speed,\n"
                                 "ignoring the global Overlay Scroll Speed above.\n"
                                 "Negative scrolls right-to-left; 0.0 is static.\n"
                                 "Default: Off (custom value default %.2f).",
                                 DEFAULT_OVERLAY_SCROLL_SPEED);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    if (temp_settings.overlay_row3_custom_scroll_speed_enabled) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(150.0f);
                        if (ImGui::DragFloat("##row3_speed", &temp_settings.overlay_row3_scroll_speed, 0.001f,
                                             -25.00f, 25.00f, "%.3f")) {
                            if (temp_settings.overlay_row3_scroll_speed < -25.0f)
                                temp_settings.overlay_row3_scroll_speed = -25.0f;
                            if (temp_settings.overlay_row3_scroll_speed > 25.0f)
                                temp_settings.overlay_row3_scroll_speed = 25.0f;
                        }
                    }

                    ImGui::Checkbox("Row 3 Auto-Freeze", &temp_settings.overlay_row3_freeze_enabled);
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[512];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "When Row 3's still-visible items fit within the overlay width, stop scrolling\n"
                                 "and show each item once, statically (measured with text width). Useful once only\n"
                                 "a few items remain so they no longer repeat across the row.\n"
                                 "Works best with 'Hide Completed Row 3 Goals' enabled, so the row actually\n"
                                 "clears out its completed goals and shrinks down to a static few.\n"
                                 "Default: On");
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                    if (temp_settings.overlay_row3_freeze_enabled) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::Combo("##row3_align", (int *) &temp_settings.overlay_row3_freeze_align,
                                     "Left\0Center\0Right\0");
                        if (ImGui::IsItemHovered()) {
                            char tooltip_buffer[256];
                            snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                     "How to align Row 3's frozen items within the overlay width.\n"
                                     "Default: Left");
                            ImGui::SetTooltip("%s", tooltip_buffer);
                        }
                    }
                } // End of belt-only scrolling / freeze options (hidden in Page mode)

                // Layout & Spacing controls the belt/page 3-row overlay (width, row spacing, alignment).
                // Compact auto-fits its window and has no rows, so hide the whole section while active.
                if (!overlay_compact_mode) {
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
                                 "Adjusts the horizontal positioning of the progress text on the overlay.\n"
                                 "Default: Left");

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

                    if (ImGui::DragFloat("Row 1 Shared Icon Size", &temp_settings.overlay_row1_shared_icon_size, 1.0f,
                                         0.0f,
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
                                 "and any Stats/Goals forced to Row 2.\n"
                                 "Default: Off (%.0fpx when enabled)",
                                 advancements_label_plural_uppercase, DEFAULT_OVERLAY_ROW2_CUSTOM_SPACING);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }

                    if (temp_settings.overlay_row2_custom_spacing_enabled) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(150.0f); // Give the slider a fixed width
                        if (ImGui::DragFloat("Row 2 Item Width", &temp_settings.overlay_row2_custom_spacing, 1.0f,
                                             96.0f,
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
                                 "and any %s/Unlocks forced to Row 3.\n"
                                 "Default: Off (%.0fpx when enabled)",
                                 advancements_label_plural_uppercase, DEFAULT_OVERLAY_ROW3_CUSTOM_SPACING);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }

                    if (temp_settings.overlay_row3_custom_spacing_enabled) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(150.0f); // Give the slider a fixed width
                        if (ImGui::DragFloat("Row 3 Item Width", &temp_settings.overlay_row3_custom_spacing, 1.0f,
                                             96.0f,
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

                    // --- Custom Vertical Spacing (overlay row gaps) ---
                    ImGui::Checkbox("Custom Vertical Spacing",
                                    &temp_settings.overlay_custom_vertical_spacing_enabled);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s",
                                          "Adjust the vertical gaps between the overlay rows individually.\n"
                                          "Each gap is added on top of the default, font-driven layout and\n"
                                          "resizes the overlay window height to match.\n"
                                          "When disabled, the overlay uses the stock spacing.\n"
                                          "Default: off.");
                    }

                    // Only reveal the individual gap controls when the feature is enabled,
                    // keeping the settings window compact when it is off.
                    if (temp_settings.overlay_custom_vertical_spacing_enabled) {
                        auto vspacing_gap = [&
                                ](const char *label, float *value, float default_value, const char *desc) {
                            if (ImGui::DragFloat(label, value, 1.0f, OVERLAY_GAP_MIN, OVERLAY_GAP_MAX, "%.0f px")) {
                                if (*value < OVERLAY_GAP_MIN) *value = OVERLAY_GAP_MIN;
                                if (*value > OVERLAY_GAP_MAX) *value = OVERLAY_GAP_MAX;
                            }
                            if (ImGui::IsItemHovered()) {
                                char tooltip_buffer[512];
                                snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                         "%s\n"
                                         "Larger values add space and grow the overlay window height to match.\n"
                                         "Default: %.0f px.", desc, default_value);
                                ImGui::SetTooltip("%s", tooltip_buffer);
                            }
                        };

                        vspacing_gap("Top Bar -> Row 1 Gap", &temp_settings.overlay_gap_top_to_row1,
                                     DEFAULT_OVERLAY_GAP_TOP_TO_ROW1,
                                     "Extra vertical space between the top info bar and the first row.");
                        vspacing_gap("Row 1 -> Row 2 Gap", &temp_settings.overlay_gap_row1_to_row2,
                                     DEFAULT_OVERLAY_GAP_ROW1_TO_ROW2,
                                     "Extra vertical space between the first and second rows.");
                        vspacing_gap("Row 2 -> Row 3 Gap", &temp_settings.overlay_gap_row2_to_row3,
                                     DEFAULT_OVERLAY_GAP_ROW2_TO_ROW3,
                                     "Extra vertical space between the second and third rows.");
                        vspacing_gap("Row 3 -> Bottom Gap", &temp_settings.overlay_gap_row3_to_bottom,
                                     DEFAULT_OVERLAY_GAP_ROW3_TO_BOTTOM,
                                     "Extra vertical space below the third row, at the window's bottom edge.");
                    }
                } // End of Layout & Spacing (belt/page only; hidden in Compact mode)

                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Text("Aesthetics");

                // The single overlay font and the Top/Row text sizes drive the belt/page 3-row layout.
                // Compact mode has its own per-element fonts below, so hide these while it is active.
                if (!overlay_compact_mode) {
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
                                 "Only choose fonts within the %s directory.\n"
                                 "Changing the font may change the overlay window height.\n"
                                 "Default: %s", get_fonts_display_path(), DEFAULT_OVERLAY_FONT);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }

                    if (ImGui::DragFloat("Top Text Size", &temp_settings.overlay_progress_font_size, 0.5f,
                                         OVERLAY_FONT_SIZE_MIN, OVERLAY_FONT_SIZE_MAX, "%.0f px")) {
                        if (temp_settings.overlay_progress_font_size < OVERLAY_FONT_SIZE_MIN)
                            temp_settings.overlay_progress_font_size = OVERLAY_FONT_SIZE_MIN;
                        if (temp_settings.overlay_progress_font_size > OVERLAY_FONT_SIZE_MAX)
                            temp_settings.overlay_progress_font_size = OVERLAY_FONT_SIZE_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[512];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Font size for the top info bar (version, progress, IGT and socials).\n"
                                 "A larger size increases the overlay window height to fit the taller text.\n"
                                 "Default: %.0f px.", DEFAULT_OVERLAY_FONT_SIZE);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }

                    if (ImGui::DragFloat("Row Text Size", &temp_settings.overlay_row_font_size, 0.5f,
                                         OVERLAY_FONT_SIZE_MIN, OVERLAY_FONT_SIZE_MAX, "%.0f px")) {
                        if (temp_settings.overlay_row_font_size < OVERLAY_FONT_SIZE_MIN)
                            temp_settings.overlay_row_font_size = OVERLAY_FONT_SIZE_MIN;
                        if (temp_settings.overlay_row_font_size > OVERLAY_FONT_SIZE_MAX)
                            temp_settings.overlay_row_font_size = OVERLAY_FONT_SIZE_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char tooltip_buffer[512];
                        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                                 "Font size for the item text under rows 2 and 3 (name and progress).\n"
                                 "A larger size increases the overlay window height to fit the taller text.\n"
                                 "Default: %.0f px.", DEFAULT_OVERLAY_FONT_SIZE);
                        ImGui::SetTooltip("%s", tooltip_buffer);
                    }
                } // End of belt/page font + text-size controls (hidden in Compact mode)

                // Compact mode: the goal-type label, the big count and the pop-out stack each have their
                // own font face and size. Shown only while Compact mode is active.
                if (overlay_compact_mode) {
                    ImGui::Text("Label Font: %s", temp_settings.compact_label_font_name);
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##CompactLabelFont")) {
                        char selected_font[256];
                        if (open_font_file_dialog(selected_font, sizeof(selected_font))) {
                            strncpy(temp_settings.compact_label_font_name, selected_font,
                                    sizeof(temp_settings.compact_label_font_name) - 1);
                            temp_settings.compact_label_font_name[sizeof(temp_settings.compact_label_font_name) - 1] =
                                    '\0';
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_label_font_tooltip_buffer[512];
                        snprintf(compact_label_font_tooltip_buffer, sizeof(compact_label_font_tooltip_buffer),
                                 "Font for the goal-type label (e.g. 'Advancements:').\n"
                                 "Only choose fonts within the %s directory.\n"
                                 "Default: %s", get_fonts_display_path(), DEFAULT_COMPACT_LABEL_FONT);
                        ImGui::SetTooltip("%s", compact_label_font_tooltip_buffer);
                    }
                    if (ImGui::DragFloat("Label Text Size", &temp_settings.compact_label_font_size, 0.5f,
                                         OVERLAY_FONT_SIZE_MIN, OVERLAY_FONT_SIZE_MAX, "%.0f px")) {
                        if (temp_settings.compact_label_font_size < OVERLAY_FONT_SIZE_MIN)
                            temp_settings.compact_label_font_size = OVERLAY_FONT_SIZE_MIN;
                        if (temp_settings.compact_label_font_size > OVERLAY_FONT_SIZE_MAX)
                            temp_settings.compact_label_font_size = OVERLAY_FONT_SIZE_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_label_size_tooltip_buffer[512];
                        snprintf(compact_label_size_tooltip_buffer, sizeof(compact_label_size_tooltip_buffer),
                                 "Point size of the goal-type label.\n"
                                 "Default: %.0f px", DEFAULT_COMPACT_LABEL_FONT_SIZE);
                        ImGui::SetTooltip("%s", compact_label_size_tooltip_buffer);
                    }

                    ImGui::Text("Count Font: %s", temp_settings.compact_count_font_name);
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##CompactCountFont")) {
                        char selected_font[256];
                        if (open_font_file_dialog(selected_font, sizeof(selected_font))) {
                            strncpy(temp_settings.compact_count_font_name, selected_font,
                                    sizeof(temp_settings.compact_count_font_name) - 1);
                            temp_settings.compact_count_font_name[sizeof(temp_settings.compact_count_font_name) - 1] =
                                    '\0';
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_count_font_tooltip_buffer[512];
                        snprintf(compact_count_font_tooltip_buffer, sizeof(compact_count_font_tooltip_buffer),
                                 "Font for the big progress count (e.g. '70/80').\n"
                                 "Only choose fonts within the %s directory.\n"
                                 "Default: %s", get_fonts_display_path(), DEFAULT_COMPACT_COUNT_FONT);
                        ImGui::SetTooltip("%s", compact_count_font_tooltip_buffer);
                    }
                    if (ImGui::DragFloat("Count Text Size", &temp_settings.compact_count_font_size, 0.5f,
                                         OVERLAY_FONT_SIZE_MIN, OVERLAY_FONT_SIZE_MAX, "%.0f px")) {
                        if (temp_settings.compact_count_font_size < OVERLAY_FONT_SIZE_MIN)
                            temp_settings.compact_count_font_size = OVERLAY_FONT_SIZE_MIN;
                        if (temp_settings.compact_count_font_size > OVERLAY_FONT_SIZE_MAX)
                            temp_settings.compact_count_font_size = OVERLAY_FONT_SIZE_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_count_size_tooltip_buffer[512];
                        snprintf(compact_count_size_tooltip_buffer, sizeof(compact_count_size_tooltip_buffer),
                                 "Point size of the big progress count.\n"
                                 "Default: %.0f px", DEFAULT_COMPACT_COUNT_FONT_SIZE);
                        ImGui::SetTooltip("%s", compact_count_size_tooltip_buffer);
                    }

                    if (ImGui::DragFloat("Line Spacing", &temp_settings.compact_panel_line_gap, 0.5f,
                                         COMPACT_PANEL_LINE_GAP_MIN, COMPACT_PANEL_LINE_GAP_MAX, "%.0f px")) {
                        if (temp_settings.compact_panel_line_gap < COMPACT_PANEL_LINE_GAP_MIN)
                            temp_settings.compact_panel_line_gap = COMPACT_PANEL_LINE_GAP_MIN;
                        if (temp_settings.compact_panel_line_gap > COMPACT_PANEL_LINE_GAP_MAX)
                            temp_settings.compact_panel_line_gap = COMPACT_PANEL_LINE_GAP_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_line_gap_tooltip_buffer[512];
                        snprintf(compact_line_gap_tooltip_buffer, sizeof(compact_line_gap_tooltip_buffer),
                                 "Vertical spacing between the label line and the count line inside the panel.\n"
                                 "The panel background and window size grow to fit.\n"
                                 "Default: %.0f px", DEFAULT_COMPACT_PANEL_LINE_GAP);
                        ImGui::SetTooltip("%s", compact_line_gap_tooltip_buffer);
                    }

                    ImGui::Text("Stack Font: %s", temp_settings.compact_stack_font_name);
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##CompactStackFont")) {
                        char selected_font[256];
                        if (open_font_file_dialog(selected_font, sizeof(selected_font))) {
                            strncpy(temp_settings.compact_stack_font_name, selected_font,
                                    sizeof(temp_settings.compact_stack_font_name) - 1);
                            temp_settings.compact_stack_font_name[sizeof(temp_settings.compact_stack_font_name) - 1] =
                                    '\0';
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_stack_font_tooltip_buffer[512];
                        snprintf(compact_stack_font_tooltip_buffer, sizeof(compact_stack_font_tooltip_buffer),
                                 "Font for the pop-out goals stacked below the panel.\n"
                                 "Only choose fonts within the %s directory.\n"
                                 "Default: %s", get_fonts_display_path(), DEFAULT_COMPACT_STACK_FONT);
                        ImGui::SetTooltip("%s", compact_stack_font_tooltip_buffer);
                    }
                    if (ImGui::DragFloat("Stack Text Size", &temp_settings.compact_stack_font_size, 0.5f,
                                         OVERLAY_FONT_SIZE_MIN, OVERLAY_FONT_SIZE_MAX, "%.0f px")) {
                        if (temp_settings.compact_stack_font_size < OVERLAY_FONT_SIZE_MIN)
                            temp_settings.compact_stack_font_size = OVERLAY_FONT_SIZE_MIN;
                        if (temp_settings.compact_stack_font_size > OVERLAY_FONT_SIZE_MAX)
                            temp_settings.compact_stack_font_size = OVERLAY_FONT_SIZE_MAX;
                    }
                    if (ImGui::IsItemHovered()) {
                        char compact_stack_size_tooltip_buffer[512];
                        snprintf(compact_stack_size_tooltip_buffer, sizeof(compact_stack_size_tooltip_buffer),
                                 "Point size of the pop-out stack text.\n"
                                 "Default: %.0f px", DEFAULT_COMPACT_STACK_FONT_SIZE);
                        ImGui::SetTooltip("%s", compact_stack_size_tooltip_buffer);
                    }
                }

                static float overlay_bg[4];
                overlay_bg[0] = (float) temp_settings.overlay_bg_color.r / 255.0f;
                overlay_bg[1] = (float) temp_settings.overlay_bg_color.g / 255.0f;
                overlay_bg[2] = (float) temp_settings.overlay_bg_color.b / 255.0f;
                overlay_bg[3] = (float) temp_settings.overlay_bg_color.a / 255.0f;

                // Conditionally display overlay background color picker
                if (temp_settings.enable_overlay) {
                    ImGui::BeginDisabled(temp_settings.overlay_transparent);
                    if (ImGui::ColorEdit3("Overlay Background Color", overlay_bg)) {
                        temp_settings.overlay_bg_color = {
                            (Uint8) (overlay_bg[0] * 255), (Uint8) (overlay_bg[1] * 255), (Uint8) (overlay_bg[2] * 255),
                            (Uint8) (overlay_bg[3] * 255)
                        };
                    }
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        char overlay_bg_tooltip_buffer[1024];
                        if (temp_settings.overlay_transparent) {
                            snprintf(overlay_bg_tooltip_buffer, sizeof(overlay_bg_tooltip_buffer),
                                     "Disabled because Transparent is checked.\n"
                                     "A transparent overlay has no background color to key out, so this picker\n"
                                     "has no effect. Uncheck Transparent to color key again.\n"
                                     "Default: (%d, %d, %d)", DEFAULT_OVERLAY_BG_COLOR.r,
                                     DEFAULT_OVERLAY_BG_COLOR.g, DEFAULT_OVERLAY_BG_COLOR.b);
                        } else {
                            snprintf(overlay_bg_tooltip_buffer, sizeof(overlay_bg_tooltip_buffer),
                                     "Configure the color of the overlay background.\n"
                                     "This is the color you'll need to color key in your streaming software (e.g., OBS).\n"
                                     "Good settings to start within the color key filter: Similarity: 1, Smoothness: 210.\n"
                                     "Default: (%d, %d, %d)", DEFAULT_OVERLAY_BG_COLOR.r,
                                     DEFAULT_OVERLAY_BG_COLOR.g, DEFAULT_OVERLAY_BG_COLOR.b);
                        }
                        ImGui::SetTooltip("%s", overlay_bg_tooltip_buffer);
                    }

                    ImGui::SameLine();
                    ImGui::Checkbox("Transparent", &temp_settings.overlay_transparent);
                    if (ImGui::IsItemHovered()) {
                        char overlay_transparent_tooltip_buffer[1024];
                        snprintf(overlay_transparent_tooltip_buffer, sizeof(overlay_transparent_tooltip_buffer),
                                 "Renders the overlay window with a transparent background instead of a\n"
                                 "solid color. Changing this restarts the overlay.\n"
#ifdef _WIN32
                                 "Capture the overlay with an OBS 'Game Capture' source set to 'Capture\n"
                                 "specific window', and enable 'Allow Transparency' on it.\n"
#elif defined(__APPLE__)
                                 "Capture the overlay with an OBS 'Window Capture' source.\n"
#else
                                 "Keep a compositor running, otherwise the background turns black.\n"
#endif
                                 "Default: %s", DEFAULT_OVERLAY_TRANSPARENT ? "On" : "Off");
                        ImGui::SetTooltip("%s", overlay_transparent_tooltip_buffer);
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
                                 "Configure the text color of the overlay window.\n"
                                 "Default: White (255, 255, 255)");
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
                             "from the Mojang API based on your username.\n"
                             "Your Minecraft skin face is shown next to you in Co-op views.\n"
                             "Default: Online");
                }
                ImGui::SetTooltip("%s", tooltip_buf);
            }
            ImGui::SameLine();
            ImGui::RadioButton("Offline##acc_type", &acc_type, ACCOUNT_OFFLINE);
            if (ImGui::IsItemHovered(acc_net_active ? ImGuiHoveredFlags_AllowWhenDisabled : 0)) {
                char tooltip_buf[320];
                if (acc_net_active) {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Cannot change while a lobby is active.");
                } else {
                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                             "Offline/cracked account. You must enter your UUID manually.\n"
                             "Find it through your world's advancements or stats files.\n"
                             "Offline accounts have no Mojang skin, so the Notch face is\n"
                             "shown next to you in Co-op views.");
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
                // Show the linked player's face once a valid UUID is set.
                // Offline accounts skip the network round-trip and show Notch.
                SDL_Texture *acc_face = skin_cache_get_face(temp_settings.local_player.uuid,
                                                            temp_settings.account_type);
                if (acc_face) {
                    ImGui::Image((ImTextureID) acc_face, ImVec2(32, 32));
                    ImGui::SameLine();
                }
                ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Account configured.");
                ImGui::TextDisabled("Advancely will use your UUID to read the correct player files.");
                ImGui::EndGroup();
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
                             "Defaults to the Advancely server (no port forwarding required).\n"
                             "Toggle 'Host locally (LAN / VPN)' below to opt into direct hosting instead.\n"
                             "Default: Off");
                }
                ImGui::SetTooltip("%s", tooltip_buf);
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
                                     "You read game files for all players and share a room code.\n"
                                     "Default: Singleplayer (co-op disabled)");
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
                                         "Off (default): connect through the Advancely server (hosted in New York).\n"
                                         "On: classic LAN / VPN hosting with a direct IP + port.\n"
                                         "Use this if everyone is on the same network or VPN.\n"
                                         "Default: Off (Advancely server)");
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
                                             "Useful for trusted local groups.\n"
                                             "Default: Off");
                                }
                                ImGui::SetTooltip("%s", aa_tip);
                            }
                        }
                        ImGui::Spacing();

                        // Ghost players: keep reading save files for UUIDs that
                        // aren't in the live lobby (mid-run disconnects, or players
                        // who never joined via Advancely). Host-local file behavior,
                        // so it applies on both transports and can be toggled live.
                        ImGui::Checkbox("Track disconnected / offline players (ghosts)",
                                        &temp_settings.coop_read_all_save_files);
                        if (ImGui::IsItemHovered()) {
                            char ghost_tip[448];
                            snprintf(ghost_tip, sizeof(ghost_tip),
                                     "When enabled, the host keeps reading player files in the\n"
                                     "world save for UUIDs that aren't in the live lobby. This\n"
                                     "covers players who disconnect mid-run or who never joined\n"
                                     "via Advancely, so their progress still counts toward 100%%.\n"
                                     "Only files touched within the last 7 days are picked up.\n"
                                     "Works on modern, mid, and hybrid versions (not legacy).\n"
                                     "Default: On");
                            ImGui::SetTooltip("%s", ghost_tip);
                        }
                        ImGui::TextDisabled(
                            "Note: Only players whose save files were touched within the last 7 days are counted as ghosts.");
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
                            ImGuiInputTextFlags pub_ip_flags = coop_public_ip_revealed
                                                                   ? 0
                                                                   : ImGuiInputTextFlags_Password;
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
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "The port the host listens on for direct (LAN / VPN) connections.\n"
                                             "Default: %s", DEFAULT_HOST_PORT);
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
                                         "Hashed locally before being sent to the server.\n"
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
                                             "Use whichever player has the highest value for each stat. "
                                             "In the All Players view, their face is shown next to the sub-stat checkbox.\n"
                                             "Default: Cumulative (Sum)");
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
                                             "Sum stat values across all players.\n"
                                             "Default: Cumulative (Sum)");
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
                                             "Only the host can manually check off stats.\n"
                                             "Default: Any Player");
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
                                             "Any player can manually check off stats.\n"
                                             "Default: Any Player");
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
                                             "Only the host can modify custom goals and checkboxes.\n"
                                             "Default: Any Player");
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
                                             "Any player can modify custom goals and checkboxes.\n"
                                             "Default: Any Player");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                            temp_settings.coop_custom_goal_mode = (CoopCustomGoalMode) custom_mode;
                            ImGui::PopID();

                            if (merge_locked) ImGui::EndDisabled();
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        // --- Contributor Faces (per-user, local visual prefs) ---
                        // Not locked by net_is_active because each user controls
                        // their own face display; nothing here is sent over the wire.
                        {
                            char tooltip_buf[320];
                            bool show_faces = temp_settings.coop_show_contributor_faces;
                            ImGui::Checkbox("Show Contributor Faces", &show_faces);
                            if (ImGui::IsItemHovered()) {
                                snprintf(tooltip_buf, sizeof(tooltip_buf),
                                         "Show Minecraft skin faces next to goals in the All Players view to\n"
                                         "indicate which player contributed. Local visual preference; not shared\n"
                                         "with other lobby members.\n"
                                         "Default: On");
                                ImGui::SetTooltip("%s", tooltip_buf);
                            }
                            temp_settings.coop_show_contributor_faces = show_faces;

                            if (show_faces) {
                                // Corner placement (main-goal faces)
                                ImGui::Text("Face Corner:");
                                ImGui::SameLine();
                                int corner = temp_settings.coop_face_corner;
                                ImGui::PushID("coop_face_corner");
                                ImGui::RadioButton("Top-Right", &corner, COOP_FACE_CORNER_TOP_RIGHT);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Render the contributor face in the top-right corner of advancements,\n"
                                             "simple stats, and counter custom goals.\n"
                                             "Default: Bottom-Right");
                                    ImGui::SetTooltip("%s", tooltip_buf);
                                }
                                ImGui::SameLine();
                                ImGui::RadioButton("Bottom-Left", &corner, COOP_FACE_CORNER_BOTTOM_LEFT);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Render the contributor face in the bottom-left corner of advancements,\n"
                                             "simple stats, and counter custom goals.\n"
                                             "Default: Bottom-Right");
                                    ImGui::SetTooltip("%s", tooltip_buf);
                                }
                                ImGui::SameLine();
                                ImGui::RadioButton("Bottom-Right", &corner, COOP_FACE_CORNER_BOTTOM_RIGHT);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Render the contributor face in the bottom-right corner of advancements,\n"
                                             "simple stats, and counter custom goals.\n"
                                             "Default: Bottom-Right");
                                    ImGui::SetTooltip("%s", tooltip_buf);
                                }
                                temp_settings.coop_face_corner = (CoopFaceCorner) corner;
                                ImGui::PopID();

                                // Face size (DragFloat to match the other numeric sliders)
                                if (ImGui::DragFloat("Main-Goal Face Size",
                                                     &temp_settings.coop_face_size, 1.0f, 16.0f, 48.0f, "%.0f px")) {
                                    if (temp_settings.coop_face_size < 16.0f) temp_settings.coop_face_size = 16.0f;
                                    if (temp_settings.coop_face_size > 48.0f) temp_settings.coop_face_size = 48.0f;
                                }
                                if (ImGui::IsItemHovered()) {
                                    char face_size_tooltip[1024];
                                    snprintf(face_size_tooltip, sizeof(face_size_tooltip),
                                             "Logical pixel size of the contributor face on main goals (advancements,\n"
                                             "simple stats, counter custom goals). Range 16-48 px.\n"
                                             "Sub-stat and checkbox faces use a fixed size that matches the checkbox.\n"
                                             "Default: %.0f px", DEFAULT_COOP_FACE_SIZE);
                                    ImGui::SetTooltip("%s", face_size_tooltip);
                                }

                                // Face LOD threshold (matches the other LOD sliders in Visuals)
                                if (ImGui::DragFloat("Hide Contributor Faces At",
                                                     &temp_settings.coop_face_lod_threshold, 0.001f, 0.05f, 10.0f,
                                                     "%.3f")) {
                                    if (temp_settings.coop_face_lod_threshold < 0.05f)
                                        temp_settings.coop_face_lod_threshold = 0.05f;
                                    if (temp_settings.coop_face_lod_threshold > 10.0f)
                                        temp_settings.coop_face_lod_threshold = 10.0f;
                                }
                                if (ImGui::IsItemHovered()) {
                                    char lod_face_tooltip[1024];
                                    snprintf(lod_face_tooltip, sizeof(lod_face_tooltip),
                                             "The zoom threshold below which non-checkbox contributor faces are hidden.\n"
                                             "Higher values are more zoomed in.\n"
                                             "Affects:\n"
                                             " - Main-goal corner faces (advancements, simple stats, counter custom goals).\n"
                                             " - Sub-stat highest-value faces.\n"
                                             "Faces drawn behind manual-completion checkboxes follow the checkbox LOD instead.\n"
                                             "Default: %.3f", DEFAULT_COOP_FACE_LOD_THRESHOLD);
                                    ImGui::SetTooltip("%s", lod_face_tooltip);
                                }
                            } // end if (show_faces)
                        }

                        // Compact overlay's pinned co-op player face. Only relevant when the overlay is in
                        // Compact mode, so the whole section is hidden in the other render modes.
                        if (temp_settings.overlay_render_mode == OVERLAY_RENDER_MODE_COMPACT) {
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();
                            ImGui::Text("Compact Overlay Player Face");

                            // Pinned face shown at the panel's bottom-right in a specific-player/ghost view. 0 hides.
                            if (ImGui::DragFloat("Panel Face Size", &temp_settings.compact_coop_panel_face_size, 0.5f,
                                                 COMPACT_COOP_PANEL_FACE_SIZE_MIN, COMPACT_COOP_PANEL_FACE_SIZE_MAX,
                                                 "%.0f")) {
                                if (temp_settings.compact_coop_panel_face_size < COMPACT_COOP_PANEL_FACE_SIZE_MIN)
                                    temp_settings.compact_coop_panel_face_size = COMPACT_COOP_PANEL_FACE_SIZE_MIN;
                                if (temp_settings.compact_coop_panel_face_size > COMPACT_COOP_PANEL_FACE_SIZE_MAX)
                                    temp_settings.compact_coop_panel_face_size = COMPACT_COOP_PANEL_FACE_SIZE_MAX;
                            }
                            if (ImGui::IsItemHovered()) {
                                char compact_coop_face_tooltip_buffer[512];
                                snprintf(compact_coop_face_tooltip_buffer, sizeof(compact_coop_face_tooltip_buffer),
                                         "Compact overlay only: the selected player's face, pinned at the panel's\n"
                                         "bottom-right when you view a single player (or a spectated ghost) instead\n"
                                         "of All Players. 0 hides it. In the All Players view each pop-out line shows\n"
                                         "its own contributor face instead (needs Show Contributor Faces on, above).\n"
                                         "Default: %.0f", DEFAULT_COMPACT_COOP_PANEL_FACE_SIZE);
                                ImGui::SetTooltip("%s", compact_coop_face_tooltip_buffer);
                            }

                            if (ImGui::DragFloat("Panel Face Offset X", &temp_settings.compact_coop_panel_face_offset_x,
                                                 0.5f, COMPACT_COOP_PANEL_FACE_OFFSET_MIN,
                                                 COMPACT_COOP_PANEL_FACE_OFFSET_MAX, "%.0f")) {
                                if (temp_settings.compact_coop_panel_face_offset_x < COMPACT_COOP_PANEL_FACE_OFFSET_MIN)
                                    temp_settings.compact_coop_panel_face_offset_x = COMPACT_COOP_PANEL_FACE_OFFSET_MIN;
                                if (temp_settings.compact_coop_panel_face_offset_x > COMPACT_COOP_PANEL_FACE_OFFSET_MAX)
                                    temp_settings.compact_coop_panel_face_offset_x = COMPACT_COOP_PANEL_FACE_OFFSET_MAX;
                            }
                            if (ImGui::IsItemHovered()) {
                                char compact_coop_face_x_tooltip_buffer[512];
                                snprintf(compact_coop_face_x_tooltip_buffer, sizeof(compact_coop_face_x_tooltip_buffer),
                                         "Horizontal inset of the pinned face from the panel's right edge (pixels).\n"
                                         "Higher moves it further inside; negative overhangs the edge. The face is\n"
                                         "always kept fully inside the overlay window.\n"
                                         "Default: %.0f", DEFAULT_COMPACT_COOP_PANEL_FACE_OFFSET_X);
                                ImGui::SetTooltip("%s", compact_coop_face_x_tooltip_buffer);
                            }

                            if (ImGui::DragFloat("Panel Face Offset Y", &temp_settings.compact_coop_panel_face_offset_y,
                                                 0.5f, COMPACT_COOP_PANEL_FACE_OFFSET_MIN,
                                                 COMPACT_COOP_PANEL_FACE_OFFSET_MAX, "%.0f")) {
                                if (temp_settings.compact_coop_panel_face_offset_y < COMPACT_COOP_PANEL_FACE_OFFSET_MIN)
                                    temp_settings.compact_coop_panel_face_offset_y = COMPACT_COOP_PANEL_FACE_OFFSET_MIN;
                                if (temp_settings.compact_coop_panel_face_offset_y > COMPACT_COOP_PANEL_FACE_OFFSET_MAX)
                                    temp_settings.compact_coop_panel_face_offset_y = COMPACT_COOP_PANEL_FACE_OFFSET_MAX;
                            }
                            if (ImGui::IsItemHovered()) {
                                char compact_coop_face_y_tooltip_buffer[512];
                                snprintf(compact_coop_face_y_tooltip_buffer, sizeof(compact_coop_face_y_tooltip_buffer),
                                         "Vertical inset of the pinned face from the panel's bottom edge (pixels).\n"
                                         "Higher moves it further inside; negative overhangs the edge. The face is\n"
                                         "always kept fully inside the overlay window.\n"
                                         "Default: %.0f", DEFAULT_COMPACT_COOP_PANEL_FACE_OFFSET_Y);
                                ImGui::SetTooltip("%s", compact_coop_face_y_tooltip_buffer);
                            }

                            // Per-line contributor face shown on each pop-out stack line in the All Players view.
                            if (ImGui::DragFloat("Stack Face Size", &temp_settings.compact_stack_face_size, 0.5f,
                                                 COMPACT_STACK_FACE_SIZE_MIN, COMPACT_STACK_FACE_SIZE_MAX, "%.0f")) {
                                if (temp_settings.compact_stack_face_size < COMPACT_STACK_FACE_SIZE_MIN)
                                    temp_settings.compact_stack_face_size = COMPACT_STACK_FACE_SIZE_MIN;
                                if (temp_settings.compact_stack_face_size > COMPACT_STACK_FACE_SIZE_MAX)
                                    temp_settings.compact_stack_face_size = COMPACT_STACK_FACE_SIZE_MAX;
                            }
                            if (ImGui::IsItemHovered()) {
                                char compact_stack_face_tooltip_buffer[512];
                                snprintf(compact_stack_face_tooltip_buffer, sizeof(compact_stack_face_tooltip_buffer),
                                         "Compact overlay only: the size of the contributor face that rides each\n"
                                         "pop-out stack line in the All Players view (needs Show Contributor Faces\n"
                                         "on, above). Independent of the pop-out icon size; a line reserves room for\n"
                                         "the face only when it credits a single player.\n"
                                         "Default: %.0f", DEFAULT_COMPACT_STACK_FACE_SIZE);
                                ImGui::SetTooltip("%s", compact_stack_face_tooltip_buffer);
                            }
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        // Pre-host reminder: template/mod options bake into the lobby at Start Lobby. Hidden once hosting.
                        if (!net_is_active) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f)); // Yellow text
                            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                            if (selected_version <= MC_VERSION_1_6_4) {
                                ImGui::TextWrapped(
                                    "Before starting the lobby, set your template (version, category, optional flag), "
                                    "the Hermes Mod toggle, and the StatsPerWorld Mod toggle. These cannot be changed "
                                    "once the lobby is being hosted.");
                            } else {
                                ImGui::TextWrapped(
                                    "Before starting the lobby, set your template (version, category, optional flag) "
                                    "and the Hermes Mod toggle. These cannot be changed once the lobby is being hosted.");
                            }
                            ImGui::PopTextWrapPos();
                            ImGui::PopStyleColor();
                            ImGui::Spacing();
                        }

                        // Start Lobby button (disabled when already hosting, invalid input, unsaved changes, or template editor open)
                        {
                            bool editor_open = p_temp_creator_open && *p_temp_creator_open;
                            bool inputs_ok = transport_direct
                                                 ? (ip_valid && port_valid && pub_ip_valid && !pub_ip_duplicate)
                                                 : true; // Relay: no ip/port; password is optional.
                            bool relay_outdated = !transport_direct && g_latest_known_version[0] != '\0'
                                                  && compare_version_strings(ADVANCELY_VERSION,
                                                                             g_latest_known_version) < 0;
                            bool can_start = inputs_ok && !relay_outdated
                                             && g_coop_ctx && !net_is_active && !has_unsaved_changes && !editor_open;
                            if (!can_start) ImGui::BeginDisabled();
                            if (ImGui::Button("Start Lobby")) {
                                if (transport_direct) {
                                    int port = atoi(temp_settings.host_port);
                                    if (coop_net_start_host(g_coop_ctx, temp_settings.host_ip, port,
                                                            temp_settings.local_player.username,
                                                            temp_settings.local_player.uuid,
                                                            temp_settings.local_player.display_name,
                                                            temp_settings.account_type == ACCOUNT_OFFLINE,
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
                                                                  temp_settings.local_player.display_name,
                                                                  temp_settings.account_type == ACCOUNT_OFFLINE)) {
                                        update_coop_template_sync(&temp_settings);
                                        // Surface the relay-assigned room code in the same UI slot the
                                        // direct path uses, so the existing Copy / display logic works.
                                        coop_net_get_room_code(g_coop_ctx, coop_room_code_buf,
                                                               sizeof(coop_room_code_buf));
                                    }
                                }
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !can_start) {
                                char tooltip_buf[256] = "";
                                if (net_is_active) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf), "The lobby has already been started.");
                                } else if (relay_outdated) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Update Advancely to %s before hosting a server lobby.\n"
                                             "(Direct connections still work on any matching version.)",
                                             g_latest_known_version);
                                } else if (has_unsaved_changes) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "Apply settings before starting a lobby.");
                                } else if (transport_direct && !ip_valid && !port_valid) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "A valid IP address and port are required to start a lobby.");
                                } else if (transport_direct && !ip_valid) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "A valid IP address is required to start a lobby.");
                                } else if (transport_direct && !port_valid) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "A valid port is required to start a lobby.");
                                } else if (transport_direct && !pub_ip_valid) {
                                    snprintf(tooltip_buf, sizeof(tooltip_buf),
                                             "The public IP/domain is not valid.");
                                } else if (transport_direct && pub_ip_duplicate) {
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
                                }
                                ImGui::Spacing();
                                char status_buf[256];
                                coop_net_get_status_msg(g_coop_ctx, status_buf, sizeof(status_buf));
                                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", status_buf);
                            } else if (!transport_direct) {
                                bool join_editor_open = p_temp_creator_open && *p_temp_creator_open;
                                bool relay_outdated_recv = g_latest_known_version[0] != '\0'
                                                           && compare_version_strings(ADVANCELY_VERSION,
                                                               g_latest_known_version) < 0;
                                bool can_join_relay = !has_unsaved_changes && !join_editor_open
                                                      && !relay_outdated_recv
                                                      && coop_relay_room_code_recv[0] != '\0';

                                ImGui::SetNextItemWidth(120.0f);
                                ImGui::InputTextWithHint("Room Code##relay_recv", "ABC123",
                                                         coop_relay_room_code_recv,
                                                         sizeof(coop_relay_room_code_recv),
                                                         ImGuiInputTextFlags_CharsUppercase);
                                trim_room_code(coop_relay_room_code_recv, sizeof(coop_relay_room_code_recv));
                                
                                if (ImGui::IsItemHovered()) {
                                    char tt[256];
                                    snprintf(tt, sizeof(tt),
                                             "6-character code shared by the host.\n"
                                             "Get it from whoever is hosting the room.");
                                    ImGui::SetTooltip("%s", tt);
                                }

                                size_t rc_len = strlen(coop_relay_room_code_recv);
                                bool rc_chars_valid = true;
                                for (size_t ci = 0; ci < rc_len; ci++) {
                                    char c = coop_relay_room_code_recv[ci];
                                    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
                                        rc_chars_valid = false;
                                        break;
                                    }
                                }
                                if (rc_len > 0 && (!rc_chars_valid || rc_len != 6)) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                                       "Invalid room code (6 characters, A-Z and 0-9)");
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
                                             "Hashed locally before sending to the server.");
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
                                if (ImGui::Button("Join via Server")) {
                                    if (g_coop_ctx) {
                                        coop_net_start_receiver_relay(g_coop_ctx,
                                                                      coop_relay_room_code_recv,
                                                                      coop_relay_password_recv,
                                                                      temp_settings.local_player.username,
                                                                      temp_settings.local_player.uuid,
                                                                      temp_settings.local_player.display_name,
                                                                      temp_settings.account_type == ACCOUNT_OFFLINE);
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
                                        else if (relay_outdated_recv)
                                            snprintf(tt, sizeof(tt),
                                                     "Update Advancely to %s before joining a server lobby.\n"
                                                     "(Direct connections still work on any matching version.)",
                                                     g_latest_known_version);
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
                                        char cleaned[256];
                                        snprintf(cleaned, sizeof(cleaned), "%s", clipboard);
                                        trim_room_code(cleaned, sizeof(cleaned));
                                        char decoded_ip[64];
                                        int decoded_port;
                                        if (coop_decode_room_code(cleaned, decoded_ip, sizeof(decoded_ip),
                                                                  &decoded_port)) {
                                            if (g_coop_ctx) {
                                                coop_net_start_receiver(g_coop_ctx, decoded_ip, decoded_port,
                                                                        temp_settings.local_player.username,
                                                                        temp_settings.local_player.uuid,
                                                                        temp_settings.local_player.display_name,
                                                                        temp_settings.account_type == ACCOUNT_OFFLINE);
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
                                SDL_SetAtomicInt(&g_settings_changed, 1);
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

                            // Player face (8x8 native, rendered at 16x16 for visibility).
                            // account_type is carried in the lobby protocol so offline remote
                            // players skip the Mojang fetch and resolve directly to Notch.
                            AccountType type = lobby[i].is_offline ? ACCOUNT_OFFLINE : ACCOUNT_ONLINE;
                            SDL_Texture *face = skin_cache_get_face(lobby[i].uuid, type);
                            if (face) {
                                ImGui::Image((ImTextureID) face, ImVec2(16, 16));
                            } else {
                                ImGui::Dummy(ImVec2(16, 16));
                            }
                            ImGui::SameLine();

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

                        // --- Advancement Assignments (host-only, All-Players merged view) ---
                        // Shown below the lobby roster: players join first, then the host
                        // divides the complex advancements among them. Assigning one scopes
                        // the merged view to that single player for it (instead of the default
                        // "player with the most criteria" rule). Edited live in app_settings
                        // and saved immediately; the re-merge is triggered via g_settings_changed.
                        if (temp_settings.network_mode == NETWORK_HOST) {
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();

                            // Snapshot the ghost roster so disconnected / non-Advancely
                            // players can be assigned too. Assignment is keyed by UUID and
                            // the merge tracks a ghost owner identically to a live player.
                            struct AssignGhost {
                                char uuid[48];
                                char name[64];
                            };
                            AssignGhost assign_ghosts[COOP_MAX_LOBBY];
                            int assign_ghost_count = 0;
                            if (g_coop_ctx) {
                                SDL_LockMutex(g_coop_ctx->lobby_mutex);
                                for (int gi = 0; gi < g_coop_ctx->ghost_player_count &&
                                                 assign_ghost_count < COOP_MAX_LOBBY; gi++) {
                                    const CoopGhostPlayer *gp = &g_coop_ctx->ghost_players[gi];
                                    AssignGhost *ag = &assign_ghosts[assign_ghost_count];
                                    strncpy(ag->uuid, gp->uuid, sizeof(ag->uuid) - 1);
                                    ag->uuid[sizeof(ag->uuid) - 1] = '\0';
                                    const char *gname = gp->display_name[0] ? gp->display_name : gp->username;
                                    snprintf(ag->name, sizeof(ag->name), "%s", gname);
                                    assign_ghost_count++;
                                }
                                SDL_UnlockMutex(g_coop_ctx->lobby_mutex);
                            }

                            // Count assignable (complex, non-recipe) advancements in the template.
                            int complex_total = 0;
                            if (t && t->template_data) {
                                for (int i = 0; i < t->template_data->advancement_count; i++) {
                                    TrackableCategory *adv = t->template_data->advancements[i];
                                    if (adv->criteria_count > 0 && !adv->is_recipe) complex_total++;
                                }
                            }

                            char assign_header[96];
                            snprintf(assign_header, sizeof(assign_header),
                                     "%s Assignments (%d/%d assigned)",
                                     advancement_label_uppercase,
                                     app_settings->coop_adv_assignment_count, complex_total);
                            ImGui::Text("%s", assign_header);
                            char assign_desc[160];
                            snprintf(assign_desc, sizeof(assign_desc),
                                     "Assign a complex %s to one player; The Merged 'All Players' view then tracks only them for it.",
                                     advancement_label_lowercase);
                            ImGui::TextDisabled("%s", assign_desc);
                            if (ImGui::IsItemHovered()) {
                                char tip[512];
                                snprintf(tip, sizeof(tip),
                                         "When an %s is assigned, the merged All Players view uses only the\n"
                                         "owner's criteria for it, instead of the player with the most criteria.\n"
                                         "Note: if the assigned player never completes every criterion, the %s\n"
                                         "will not complete even if another player did more. This trades opportunistic\n"
                                         "merging for predictable ownership when dividing work.",
                                         advancement_label_lowercase, advancement_label_lowercase);
                                ImGui::SetTooltip("%s", tip);
                            }
                            ImGui::Spacing();

                            if (app_settings->coop_player_count <= 0 && assign_ghost_count <= 0) {
                                char wait_msg[128];
                                snprintf(wait_msg, sizeof(wait_msg),
                                         "Waiting for players to join before %ss can be assigned.",
                                         advancement_label_lowercase);
                                ImGui::TextDisabled("%s", wait_msg);
                            } else if (complex_total == 0) {
                                char none_msg[128];
                                snprintf(none_msg, sizeof(none_msg),
                                         "This template has no multi-criteria %ss to assign.",
                                         advancement_label_lowercase);
                                ImGui::TextDisabled("%s", none_msg);
                            } else {
                                ImGui::BeginChild("AdvAssignmentList", ImVec2(0, 220), true);
                                for (int i = 0; i < t->template_data->advancement_count; i++) {
                                    TrackableCategory *adv = t->template_data->advancements[i];
                                    if (adv->criteria_count == 0 || adv->is_recipe) continue;

                                    ImGui::PushID(4200 + i);

                                    // Advancement icon
                                    if (adv->texture) {
                                        ImGui::Image((ImTextureID) adv->texture, ImVec2(20, 20));
                                    } else {
                                        ImGui::Dummy(ImVec2(20, 20));
                                    }
                                    ImGui::SameLine();

                                    const char *adv_label = adv->display_name[0] ? adv->display_name : adv->root_name;
                                    ImGui::TextUnformatted(adv_label);
                                    ImGui::SameLine();

                                    // Current owner (NULL = Auto).
                                    const char *owner = coop_get_advancement_owner(app_settings, adv->root_name);

                                    char preview[128];
                                    if (!owner) {
                                        snprintf(preview, sizeof(preview), "Auto (most criteria)");
                                    } else {
                                        const char *oname = nullptr;
                                        bool owner_is_ghost = false;
                                        for (int p = 0; p < app_settings->coop_player_count; p++) {
                                            if (strcmp(app_settings->coop_players[p].uuid, owner) == 0) {
                                                oname = app_settings->coop_players[p].display_name[0]
                                                            ? app_settings->coop_players[p].display_name
                                                            : app_settings->coop_players[p].username;
                                                break;
                                            }
                                        }
                                        if (!oname) {
                                            for (int g = 0; g < assign_ghost_count; g++) {
                                                if (strcmp(assign_ghosts[g].uuid, owner) == 0) {
                                                    oname = assign_ghosts[g].name;
                                                    owner_is_ghost = true;
                                                    break;
                                                }
                                            }
                                        }
                                        if (oname && owner_is_ghost) {
                                            snprintf(preview, sizeof(preview), "%s (ghost)", oname);
                                        } else {
                                            snprintf(preview, sizeof(preview), "%s", oname ? oname : "Unknown player");
                                        }
                                    }

                                    ImGui::SetNextItemWidth(220.0f);
                                    if (ImGui::BeginCombo("##assign", preview)) {
                                        bool is_auto = (owner == nullptr);
                                        if (ImGui::Selectable("Auto (most criteria)", is_auto)) {
                                            coop_set_advancement_owner(app_settings, adv->root_name, nullptr);
                                            settings_save(app_settings, nullptr, SAVE_CONTEXT_ALL);
                                            SDL_SetAtomicInt(&g_settings_changed, 1);
                                        }
                                        for (int p = 0; p < app_settings->coop_player_count; p++) {
                                            const CoopPlayer *pl = &app_settings->coop_players[p];
                                            const char *pname = pl->display_name[0] ? pl->display_name : pl->username;
                                            bool is_sel = (owner && strcmp(pl->uuid, owner) == 0);
                                            ImGui::PushID(p);
                                            if (ImGui::Selectable(pname, is_sel)) {
                                                coop_set_advancement_owner(app_settings, adv->root_name, pl->uuid);
                                                settings_save(app_settings, nullptr, SAVE_CONTEXT_ALL);
                                                SDL_SetAtomicInt(&g_settings_changed, 1);
                                            }
                                            ImGui::PopID();
                                        }
                                        for (int g = 0; g < assign_ghost_count; g++) {
                                            const AssignGhost *ag = &assign_ghosts[g];
                                            char glabel[96];
                                            snprintf(glabel, sizeof(glabel), "%s (ghost)", ag->name);
                                            bool is_sel = (owner && strcmp(ag->uuid, owner) == 0);
                                            ImGui::PushID(10000 + g);
                                            if (ImGui::Selectable(glabel, is_sel)) {
                                                coop_set_advancement_owner(app_settings, adv->root_name, ag->uuid);
                                                settings_save(app_settings, nullptr, SAVE_CONTEXT_ALL);
                                                SDL_SetAtomicInt(&g_settings_changed, 1);
                                            }
                                            ImGui::PopID();
                                        }
                                        ImGui::EndCombo();
                                    }

                                    ImGui::PopID();
                                }
                                ImGui::EndChild();
                            }
                        }
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
                const char *popup_id = relay_pw_idx == 0
                                           ? "Reveal Password?##coop_relay_host"
                                           : "Reveal Password?##coop_relay_recv";
                bool *revealed_ptr = relay_pw_idx == 0
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
            // The three conflict flags are owned by collect_hotkey_conflicts() above the tab bar,
            // which runs every frame. The rows below only mark themselves; they no longer decide
            // whether Apply is blocked, so a clash behind a collapsed header still counts.
            // --- Hotkey Settings ---

            // Capture state: which goal+slot is currently waiting for a key press.
            // The slot is a HotkeySlot value, or -1 while nothing is being captured.
            static char capturing_target_goal[192] = "";
            static int capturing_slot = -1; // -1 = idle

            // The Advancely-shortcut rows below share the same capture atomics, so only one of the
            // two can ever be waiting for a key. -1 = idle.
            static int capturing_app_action = -1;

            // Helper: convert a stored scancode-name (US layout) plus its modifier mask to a
            // layout-aware label so EU keyboards display the key the user actually presses.
            auto display_label_for_key = [](const char *stored, Uint16 mods) -> std::string {
                if (!stored || stored[0] == '\0' || strcmp(stored, "None") == 0) return "None";

                std::string key_label = stored; // Fallback to whatever we stored
                SDL_Scancode sc = SDL_GetScancodeFromName(stored);
                if (sc != SDL_SCANCODE_UNKNOWN) {
                    SDL_Keycode kc = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
                    const char *name = SDL_GetKeyName(kc);
                    if (name && name[0] != '\0') key_label = name;
                }

                char mod_prefix[64];
                hotkey_mods_to_prefix(mods, mod_prefix, sizeof(mod_prefix));
                return std::string(mod_prefix) + key_label;
            };

            // Custom goals split by what a hotkey can do to them: counters take increment and
            // decrement keys, plain goals (target value 0) take a single toggle key.
            std::vector<TrackableItem *> custom_counters;
            std::vector<TrackableItem *> custom_toggles;
            if (t && t->template_data) {
                for (int i = 0; i < t->template_data->custom_goal_count; ++i) {
                    TrackableItem *item = t->template_data->custom_goals[i];
                    if (!item) continue;
                    if (item->goal > 0 || item->goal == -1) {
                        custom_counters.push_back(item);
                    } else {
                        custom_toggles.push_back(item);
                    }
                }
            }

            // Explains the missing custom goal block above Advancely's own shortcuts.
            if (custom_counters.empty() && custom_toggles.empty()) {
                ImGui::TextDisabled(
                    "Select a template with custom goals to adjust their hotkeys here.");
                ImGui::Spacing();
            }

            // If a capture is in flight and the event handler has reported a scancode,
            // apply it to the binding for the captured goal+slot.
            if (capturing_slot >= 0 && SDL_GetAtomicInt(&g_hotkey_capture_armed) == 0) {
                int captured = SDL_GetAtomicInt(&g_hotkey_captured_scancode);

                // Resolve the new key name (US-layout scancode name, or "None").
                const char *new_key = "None";
                Uint16 new_mods = HOTKEY_MOD_NONE;
                if (captured != 0) {
                    const char *sc_name = SDL_GetScancodeName((SDL_Scancode) captured);
                    if (sc_name && sc_name[0] != '\0') {
                        new_key = sc_name;
                        new_mods = (Uint16) SDL_GetAtomicInt(&g_hotkey_captured_mods);
                    }
                }

                HotkeyBinding *binding = nullptr;
                for (int i = 0; i < temp_settings.hotkey_count; ++i) {
                    if (strcmp(temp_settings.hotkeys[i].target_goal, capturing_target_goal) == 0) {
                        binding = &temp_settings.hotkeys[i];
                        break;
                    }
                }

                HotkeySlot captured_slot = (HotkeySlot) capturing_slot;

                // Skip the write entirely if the new value matches the current one,
                // so binding "None" over an already-None slot doesn't dirty the form.
                const char *prev_key = binding ? hotkey_binding_slot_key(binding, captured_slot) : "None";
                Uint16 prev_mods = binding ? hotkey_binding_slot_mods(binding, captured_slot) : (Uint16) HOTKEY_MOD_NONE;

                if (strcmp(prev_key, new_key) != 0 || prev_mods != new_mods) {
                    if (!binding && temp_settings.hotkey_count < MAX_HOTKEYS) {
                        binding = &temp_settings.hotkeys[temp_settings.hotkey_count++];
                        memset(binding, 0, sizeof(*binding));
                        strncpy(binding->target_goal, capturing_target_goal, sizeof(binding->target_goal) - 1);
                        binding->target_goal[sizeof(binding->target_goal) - 1] = '\0';
                        strcpy(binding->increment_key, "None");
                        strcpy(binding->decrement_key, "None");
                        strcpy(binding->toggle_key, "None");
                    }
                    if (binding) {
                        char *target = binding->increment_key;
                        Uint16 *target_mods = &binding->increment_mods;
                        if (captured_slot == HOTKEY_SLOT_DECREMENT) {
                            target = binding->decrement_key;
                            target_mods = &binding->decrement_mods;
                        } else if (captured_slot == HOTKEY_SLOT_TOGGLE) {
                            target = binding->toggle_key;
                            target_mods = &binding->toggle_mods;
                        }
                        // All three key buffers are the same size, so one bound works for each.
                        strncpy(target, new_key, sizeof(binding->increment_key) - 1);
                        target[sizeof(binding->increment_key) - 1] = '\0';
                        *target_mods = new_mods;
                    }
                }
                capturing_target_goal[0] = '\0';
                capturing_slot = -1;
            }

            if (!custom_counters.empty() || !custom_toggles.empty()) {
                ImGui::Text("Hotkey Settings for Custom Goals");
                if (ImGui::IsItemHovered()) {
                    char hotkey_settings_tooltip_buffer[1024];
                    snprintf(hotkey_settings_tooltip_buffer, sizeof(hotkey_settings_tooltip_buffer),
                             "IMPORTANT: Hotkeys are remembered between templates.\n\n"
                             "Click a button and press a key to bind it. Hold Ctrl, Alt or Shift\n"
                             "while pressing the key to bind a combination. Press Escape,\n"
                             "Backspace, or Delete during capture to clear the binding back to None.\n\n"
                             "Counters get an increment and a decrement key, custom goals with a\n"
                             "target value of 0 get a single key that ticks them on and off.\n\n"
                             "Without 'Global', a hotkey only works when tabbed into the tracker.\n"
                             "Maximum of %d hotkeys are supported.",
                             MAX_HOTKEYS);
                    ImGui::SetTooltip("%s", hotkey_settings_tooltip_buffer);
                }

                // How 'Global' behaves depends entirely on the OS, so the explanation is written
                // per-platform rather than describing three systems at once.
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
#ifdef _WIN32
                    const char *global_os_name = "Windows";
                    const char *global_os_help =
                            "Advancely registers the combination with Windows itself, so it fires\n"
                            "while Minecraft is focused. Windows then reserves it system-wide: no\n"
                            "other program receives that combination while Advancely is running.\n"
                            "If another program claimed it first, that row says so and the hotkey\n"
                            "keeps working as a normal window-focused one.\n\n"
                            "A conflict can only be reported when the other program reserved the\n"
                            "key the same way. Programs that read the keyboard at a lower level,\n"
                            "such as Discord or macro tools, cause no conflict message; Advancely\n"
                            "simply takes priority over them for as long as it is running.";
#elif defined(__APPLE__)
                    const char *global_os_name = "macOS";
                    const char *global_os_help =
                            "Advancely registers the combination with the system hotkey API, so it\n"
                            "fires while Minecraft is focused, and no Accessibility permission\n"
                            "prompt is needed. macOS then reserves the combination system-wide.\n"
                            "System shortcuts win: if macOS or another program already owns it,\n"
                            "that row says so and the hotkey keeps working as a window-focused one.\n\n"
                            "A conflict can only be reported when the other program reserved the\n"
                            "key the same way. Programs that read the keyboard at a lower level,\n"
                            "such as Discord or macro tools, cause no conflict message; Advancely\n"
                            "simply takes priority over them for as long as it is running.";
#else
                    const char *global_os_name = "Linux";
                    const char *global_os_help =
                            "Advancely grabs the combination from the X server, so it fires while\n"
                            "Minecraft is focused. On an X11 session this always works. On a\n"
                            "Wayland session it works while the focused window is an X11 or\n"
                            "XWayland window, which Minecraft normally is; Wayland itself offers no\n"
                            "way for a program to reserve a shortcut. With no X server reachable at\n"
                            "all, that row says so and the hotkey keeps working as a window-focused one.";
#endif
                    char global_support_buffer[1024];
                    snprintf(global_support_buffer, sizeof(global_support_buffer),
                             "How 'Global' works on %s:\n\n%s", global_os_name, global_os_help);
                    ImGui::SetTooltip("%s", global_support_buffer);
                }

                // One row per custom goal. Counters show the decrement and increment slots, plain
                // goals show the toggle slot, and everything else about the row is identical.
                auto render_goal_hotkey_row = [&](const TrackableItem *goal, bool is_counter) {
                    HotkeyBinding *binding = nullptr;
                    for (int i = 0; i < temp_settings.hotkey_count; ++i) {
                        if (strcmp(temp_settings.hotkeys[i].target_goal, goal->root_name) == 0) {
                            binding = &temp_settings.hotkeys[i];
                            break;
                        }
                    }

                    // Creating the row lazily keeps untouched goals out of settings.json, which
                    // matters because a template can hold far more goals than MAX_HOTKEYS.
                    auto ensure_binding = [&]() {
                        if (binding || temp_settings.hotkey_count >= MAX_HOTKEYS) return;
                        binding = &temp_settings.hotkeys[temp_settings.hotkey_count++];
                        memset(binding, 0, sizeof(*binding));
                        strncpy(binding->target_goal, goal->root_name, sizeof(binding->target_goal) - 1);
                        binding->target_goal[sizeof(binding->target_goal) - 1] = '\0';
                        strcpy(binding->increment_key, "None");
                        strcpy(binding->decrement_key, "None");
                        strcpy(binding->toggle_key, "None");
                    };

                    ImGui::Text("%s", goal->display_name);
                    ImGui::SameLine();

                    auto render_slot_button = [&](HotkeySlot slot, const char *caption, const char *id_prefix) {
                        ImGui::TextDisabled("%s", caption);
                        ImGui::SameLine();

                        bool capturing_here = (capturing_slot == (int) slot &&
                                               strcmp(capturing_target_goal, goal->root_name) == 0);

                        char btn_label[256];
                        if (capturing_here) {
                            snprintf(btn_label, sizeof(btn_label),
                                     "Press a key...##%s_%s", id_prefix, goal->root_name);
                        } else {
                            std::string label = display_label_for_key(
                                binding ? hotkey_binding_slot_key(binding, slot) : "None",
                                binding ? hotkey_binding_slot_mods(binding, slot) : (Uint16) HOTKEY_MOD_NONE);
                            snprintf(btn_label, sizeof(btn_label),
                                     "%s##%s_%s", label.c_str(), id_prefix, goal->root_name);
                        }
                        if (ImGui::Button(btn_label, ImVec2(170, 0))) {
                            // Arm capture for this slot.
                            strncpy(capturing_target_goal, goal->root_name,
                                    sizeof(capturing_target_goal) - 1);
                            capturing_target_goal[sizeof(capturing_target_goal) - 1] = '\0';
                            capturing_slot = (int) slot;
                            capturing_app_action = -1;
                            SDL_SetAtomicInt(&g_hotkey_captured_scancode, 0);
                            SDL_SetAtomicInt(&g_hotkey_captured_mods, HOTKEY_MOD_NONE);
                            SDL_SetAtomicInt(&g_hotkey_capture_armed, 1);
                        }
                    };

                    if (is_counter) {
                        // --- Decrement first, then Increment (per UX request) ---
                        render_slot_button(HOTKEY_SLOT_DECREMENT, "Decr.", "dec");
                        ImGui::SameLine();
                        render_slot_button(HOTKEY_SLOT_INCREMENT, "Incr.", "inc");
                    } else {
                        render_slot_button(HOTKEY_SLOT_TOGGLE, "Toggle", "tog");
                        if (ImGui::IsItemHovered()) {
                            char toggle_tooltip_buffer[512];
                            snprintf(toggle_tooltip_buffer, sizeof(toggle_tooltip_buffer),
                                     "This goal has a target value of 0, so it is a plain checkbox.\n"
                                     "The key ticks it on and off, exactly like clicking it on the map.");
                            ImGui::SetTooltip("%s", toggle_tooltip_buffer);
                        }
                    }

                    // --- Per-binding Global toggle ---
                    ImGui::SameLine();
                    bool row_is_global = binding ? binding->is_global : false;
                    char global_cb_label[256];
                    snprintf(global_cb_label, sizeof(global_cb_label), "Global##global_%s", goal->root_name);
                    if (ImGui::Checkbox(global_cb_label, &row_is_global)) {
                        ensure_binding();
                        if (binding) binding->is_global = row_is_global;
                    }
                    if (ImGui::IsItemHovered()) {
                        char global_tooltip_buffer[1024];
                        snprintf(global_tooltip_buffer, sizeof(global_tooltip_buffer),
                                 "Off: the hotkey only fires while the Advancely tracker window is focused.\n"
                                 "Any key works, and other programs keep receiving it normally.\n\n"
                                 "On: the operating system reserves the key for Advancely, so it fires while you are\n"
                                 "playing Minecraft. Because the key is taken away from every other program, Ctrl or\n"
                                 "Alt is strongly recommended. A bare key is allowed, but it also fires while you type\n"
                                 "in chat or a text field. F13 to F24 are the exception, since no keyboard has those\n"
                                 "physically and macro pads are the only thing that sends them.\n\n"
                                 "Default: %s", DEFAULT_HOTKEY_IS_GLOBAL ? "On" : "Off");
                        ImGui::SetTooltip("%s", global_tooltip_buffer);
                    }

                    // Explain exactly which slot is at fault rather than a generic complaint.
                    // Reserved combinations and collisions block Apply; a global row without a
                    // modifier only earns an amber warning, since it does work.
                    if (binding) {
                        auto validate_slot = [&](HotkeySlot slot) {
                            const char *key = hotkey_binding_slot_key(binding, slot);
                            Uint16 mods = hotkey_binding_slot_mods(binding, slot);
                            char reason[192];
                            if (hotkey_slot_is_reserved(key, mods, reason, sizeof(reason))) {
                                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                                   "    %s %s", hotkey_slot_name(slot), reason);
                                return;
                            }

                            // An Advancely shortcut that can fire in the same context takes the
                            // key away from this goal, so the row has to say so too. The shortcut
                            // rows further down report the same clash from their side.
                            if (hotkey_slot_bound(key)) {
                                char keycap[64];
                                hotkey_counter_key_as_keycap(key, keycap, sizeof(keycap));
                                Uint16 counter_contexts = hotkey_counter_contexts(binding);
                                for (int action = 0; action < APP_HOTKEY_COUNT; ++action) {
                                    const AppHotkeyDef *clash_def = &APP_HOTKEY_DEFS[action];
                                    const AppHotkey *clash_hk = &temp_settings.app_hotkeys[action];
                                    if (!hotkey_slot_bound(clash_hk->key)) continue;
                                    if ((counter_contexts & clash_def->contexts) == 0) continue;
                                    if (mods != clash_hk->mods || strcmp(keycap, clash_hk->key) != 0) continue;

                                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                                       "    %s collides with the shortcut \"%s\"",
                                                       hotkey_slot_name(slot), clash_def->label);
                                    return;
                                }
                            }

                            if (row_is_global &&
                                hotkey_global_slot_is_bare(key, mods, reason, sizeof(reason))) {
                                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                                                   "    %s %s", hotkey_slot_name(slot), reason);
                            }
                        };
                        if (is_counter) {
                            validate_slot(HOTKEY_SLOT_DECREMENT);
                            validate_slot(HOTKEY_SLOT_INCREMENT);
                        } else {
                            validate_slot(HOTKEY_SLOT_TOGGLE);
                        }
                    }

                    // Registration status comes from the APPLIED settings, not this form: a row
                    // just ticked Global is not registered until 'Apply Settings' is clicked. The
                    // matching binding is found by goal name, since the two arrays can differ.
                    int applied_idx = -1;
                    for (int i = 0; i < app_settings->hotkey_count; ++i) {
                        if (strcmp(app_settings->hotkeys[i].target_goal, goal->root_name) == 0) {
                            applied_idx = i;
                            break;
                        }
                    }
                    if (applied_idx >= 0 && app_settings->hotkeys[applied_idx].is_global) {
                        auto report_slot = [&](HotkeySlot slot) {
                            const char *err = global_hotkeys_slot_error(applied_idx, slot);
                            if (!err) return;
                            // Amber, not red: the binding still works, just not globally.
                            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                                               "    %s is not global: %s Window-focused only.",
                                               hotkey_slot_name(slot), err);
                        };
                        if (is_counter) {
                            report_slot(HOTKEY_SLOT_DECREMENT);
                            report_slot(HOTKEY_SLOT_INCREMENT);
                        } else {
                            report_slot(HOTKEY_SLOT_TOGGLE);
                        }
                    }
                };

                // Loop through the goals provided by the LIVE TEMPLATE to build the UI rows
                if (!custom_counters.empty()) {
                    ImGui::Spacing();
                    ImGui::SeparatorText("Counters");
                    for (const auto &counter: custom_counters) {
                        render_goal_hotkey_row(counter, true);
                    }
                }

                if (!custom_toggles.empty()) {
                    ImGui::Spacing();
                    ImGui::SeparatorText("Toggles (Target Value 0)");
                    for (const auto &toggle_goal: custom_toggles) {
                        render_goal_hotkey_row(toggle_goal, false);
                    }
                }
                ImGui::Spacing();

                // --- Prune bindings that carry no information ---
                // Binding a key and then clearing it back to None, or ticking Global and
                // unticking it again, would otherwise leave a hollow row behind. That row makes
                // hotkey_count differ from the saved settings, so "Revert Changes" would stay
                // visible even though nothing actually changed.
                int kept = 0;
                for (int i = 0; i < temp_settings.hotkey_count; ++i) {
                    const HotkeyBinding *hb = &temp_settings.hotkeys[i];
                    bool is_empty = !hb->is_global;
                    for (int slot = 0; slot < HOTKEY_SLOT_COUNT && is_empty; ++slot) {
                        const char *key = hotkey_binding_slot_key(hb, (HotkeySlot) slot);
                        if (key[0] != '\0' && strcmp(key, "None") != 0) is_empty = false;
                    }
                    if (is_empty) continue;
                    if (kept != i) temp_settings.hotkeys[kept] = temp_settings.hotkeys[i];
                    kept++;
                }
                for (int i = kept; i < temp_settings.hotkey_count; ++i) {
                    memset(&temp_settings.hotkeys[i], 0, sizeof(temp_settings.hotkeys[i]));
                }
                temp_settings.hotkey_count = kept;

                // Duplicate detection itself lives in collect_hotkey_conflicts(), which runs above
                // the tab bar and names both goals in the list up there.
                if (hotkey_duplicate_error) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       "Error: Two or more goals share the same hotkey. Each key can only be used once.");
                }

                ImGui::Spacing();
                if (!global_hotkeys_platform_supported()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                                       "Global hotkeys are unavailable on this system. Every hotkey behaves as window-focused.");
                } else {
                    ImGui::TextDisabled(
                        "Note: 'Global' rows are handed to the operating system when you click 'Apply Settings'.");
                }

                // Only separate the two blocks when the custom goal block above actually rendered.
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
            }

            // --- Advancely's own shortcuts ---
            // Apply a finished capture. Unlike counter hotkeys, only Escape clears a row here, so
            // Delete and Backspace stay bindable (Delete is a default binding).
            if (capturing_app_action >= 0 && SDL_GetAtomicInt(&g_hotkey_capture_armed) == 0) {
                // The keycap, not the physical key: these bindings are named ones like Ctrl+Z, and
                // they should stay on the key that carries that letter on any keyboard layout.
                int captured = SDL_GetAtomicInt(&g_hotkey_captured_keycode);
                AppHotkey *hk = &temp_settings.app_hotkeys[capturing_app_action];
                if (captured == 0) {
                    strcpy(hk->key, "None");
                    hk->mods = HOTKEY_MOD_NONE;
                } else {
                    const char *key_name = SDL_GetKeyName((SDL_Keycode) captured);
                    if (key_name && key_name[0] != '\0') {
                        strncpy(hk->key, key_name, sizeof(hk->key) - 1);
                        hk->key[sizeof(hk->key) - 1] = '\0';
                        hk->mods = (Uint16) SDL_GetAtomicInt(&g_hotkey_captured_mods);
                    }
                }
                capturing_app_action = -1;
            }

            ImGui::Text("Advancely Hotkeys");
            if (ImGui::IsItemHovered()) {
                char app_hotkey_tooltip_buffer[1024];
                snprintf(app_hotkey_tooltip_buffer, sizeof(app_hotkey_tooltip_buffer),
                         "Shortcuts for Advancely itself, grouped by the window they belong to.\n\n"
                         "Click a button and press a key to bind it. Hold Ctrl, Alt or Shift while\n"
                         "pressing the key to bind a combination. Press Escape during capture to\n"
                         "clear the binding, which turns that shortcut off.\n\n"
                         "These are always window-focused: unlike the custom counter hotkeys above,\n"
                         "they cannot be handed to the operating system.\n\n"
                         "They follow the letter printed on the key, so Ctrl+Z is the same keycap on\n"
                         "every keyboard layout. The counter hotkeys above bind the physical key instead.\n\n"
                         "Two shortcuts may share a key as long as they belong to windows or modes\n"
                         "that are never active at the same time.");
                ImGui::SetTooltip("%s", app_hotkey_tooltip_buffer);
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Reset All Hotkeys")) {
                app_hotkeys_set_defaults(&temp_settings);
                capturing_app_action = -1;
            }
            if (ImGui::IsItemHovered()) {
                char reset_all_app_tooltip[256];
                snprintf(reset_all_app_tooltip, sizeof(reset_all_app_tooltip),
                         "Puts every shortcut in this list back to its default key.\n"
                         "Custom counter hotkeys are not affected.");
                ImGui::SetTooltip("%s", reset_all_app_tooltip);
            }

            // A shortcut clashes with another binding only when both can fire at the same moment,
            // so the comparison is gated on overlapping contexts. Counter hotkeys live in the
            // tracker window, or in every context once the row is marked Global, since the OS then
            // delivers them regardless of which window has focus.
            auto app_slot_bound = [](const char *key) -> bool {
                return key && key[0] != '\0' && strcmp(key, "None") != 0;
            };
            auto app_same_combo = [](const char *key_a, Uint16 mods_a,
                                     const char *key_b, Uint16 mods_b) -> bool {
                return strcmp(key_a, key_b) == 0 && mods_a == mods_b;
            };
            // Counter hotkeys store the physical key while the shortcuts below store the keycap, so
            // one of the two has to be translated before they can be compared. Without it a real
            // clash on a non-US layout would go unnoticed, and an imaginary one would be reported.
            auto counter_key_as_keycap = [](const char *stored, char *buf, size_t buf_size) -> const char * {
                snprintf(buf, buf_size, "%s", stored ? stored : "");
                SDL_Scancode sc = SDL_GetScancodeFromName(buf);
                if (sc == SDL_SCANCODE_UNKNOWN) return buf;
                const char *name = SDL_GetKeyName(SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false));
                if (name && name[0] != '\0') snprintf(buf, buf_size, "%s", name);
                return buf;
            };

            AppHotkeyGroup current_group = APP_HOTKEY_GROUP_COUNT;
            // Each group folds away behind its own header, collapsed by default so the tab opens
            // as a short list of windows instead of every shortcut at once.
            bool group_open = false;
            for (int action = 0; action < APP_HOTKEY_COUNT; ++action) {
                const AppHotkeyDef *def = &APP_HOTKEY_DEFS[action];
                AppHotkey *hk = &temp_settings.app_hotkeys[action];

                if (def->group != current_group) {
                    current_group = def->group;
                    ImGui::Spacing();
                    group_open = ImGui::CollapsingHeader(APP_HOTKEY_GROUP_NAMES[current_group]);
                    if (ImGui::IsItemHovered()) {
                        char group_tooltip[1024];
                        snprintf(group_tooltip, sizeof(group_tooltip), "%s",
                                 APP_HOTKEY_GROUP_TOOLTIPS[current_group]);
                        ImGui::SetTooltip("%s", group_tooltip);
                    }
                }

                // Only the rendering is skipped while a group is folded away. A clash hidden behind
                // a closed header is still listed above the tab bar and still blocks Apply, since
                // collect_hotkey_conflicts() looks at the bindings rather than at what is drawn.
                if (group_open) {
                    ImGui::Text("%s", def->label);
                    if (ImGui::IsItemHovered()) {
                        char row_tooltip[1024];
                        snprintf(row_tooltip, sizeof(row_tooltip), "%s", def->description);
                        ImGui::SetTooltip("%s", row_tooltip);
                    }

                    ImGui::SameLine(300.0f);

                    char app_btn_label[256];
                    if (capturing_app_action == action) {
                        snprintf(app_btn_label, sizeof(app_btn_label), "Press a key...##app_hk_%d", action);
                    } else {
                        char key_label[96];
                        app_hotkey_display_label(hk, key_label, sizeof(key_label));
                        snprintf(app_btn_label, sizeof(app_btn_label), "%s##app_hk_%d", key_label, action);
                    }
                    if (ImGui::Button(app_btn_label, ImVec2(170, 0))) {
                        capturing_app_action = action;
                        capturing_slot = -1;
                        capturing_target_goal[0] = '\0';
                        SDL_SetAtomicInt(&g_hotkey_captured_scancode, 0);
                        SDL_SetAtomicInt(&g_hotkey_captured_keycode, 0);
                        SDL_SetAtomicInt(&g_hotkey_captured_mods, HOTKEY_MOD_NONE);
                        // Armed as 2 so the event handler knows Delete and Backspace are bindable here.
                        SDL_SetAtomicInt(&g_hotkey_capture_armed, 2);
                    }
                }

                bool is_default = (strcmp(hk->key, def->default_key) == 0 && hk->mods == def->default_mods);
                if (group_open && !is_default) {
                    ImGui::SameLine();
                    char reset_label[64];
                    snprintf(reset_label, sizeof(reset_label), "Reset##app_hk_reset_%d", action);
                    if (ImGui::SmallButton(reset_label)) {
                        strncpy(hk->key, def->default_key, sizeof(hk->key) - 1);
                        hk->key[sizeof(hk->key) - 1] = '\0';
                        hk->mods = def->default_mods;
                    }
                    if (ImGui::IsItemHovered()) {
                        AppHotkey default_hk = {};
                        strncpy(default_hk.key, def->default_key, sizeof(default_hk.key) - 1);
                        default_hk.mods = def->default_mods;
                        char default_label[96];
                        app_hotkey_display_label(&default_hk, default_label, sizeof(default_label));
                        char reset_tooltip[192];
                        snprintf(reset_tooltip, sizeof(reset_tooltip), "Back to the default: %s", default_label);
                        ImGui::SetTooltip("%s", reset_tooltip);
                    }
                }

                if (!app_slot_bound(hk->key)) continue;

                char conflict[256];
                conflict[0] = '\0';

                char reserved_reason[192];
                if (hotkey_slot_is_reserved(hk->key, hk->mods, reserved_reason, sizeof(reserved_reason))) {
                    snprintf(conflict, sizeof(conflict), "%s", reserved_reason);
                }

                for (int other = 0; other < APP_HOTKEY_COUNT && conflict[0] == '\0'; ++other) {
                    if (other == action) continue;
                    const AppHotkey *other_hk = &temp_settings.app_hotkeys[other];
                    if (!app_slot_bound(other_hk->key)) continue;
                    if ((APP_HOTKEY_DEFS[other].contexts & def->contexts) == 0) continue;
                    if (app_same_combo(hk->key, hk->mods, other_hk->key, other_hk->mods)) {
                        snprintf(conflict, sizeof(conflict), "collides with \"%s\"", APP_HOTKEY_DEFS[other].label);
                    }
                }

                for (int k = 0; k < temp_settings.hotkey_count && conflict[0] == '\0'; ++k) {
                    const HotkeyBinding *hb = &temp_settings.hotkeys[k];
                    // hotkey_apply_counter_action() refuses to run while the Visual Layout Editor
                    // is open, so a goal hotkey can never collide with its shortcuts, global or
                    // not. That is what keeps the editor's plain W, A, S and D usable.
                    Uint16 counter_contexts = hb->is_global
                                                  ? (Uint16) (APP_HOTKEY_CTX_ALL & ~APP_HOTKEY_CTX_VISUAL)
                                                  : (Uint16) APP_HOTKEY_CTX_COUNTER_HOTKEYS;
                    if ((counter_contexts & def->contexts) == 0) continue;

                    // Show the goal under the name the user sees in the list above when the
                    // template is loaded, and fall back to the root name when it is not.
                    const char *goal_name = hb->target_goal;
                    for (const auto &counter: custom_counters) {
                        if (strcmp(counter->root_name, hb->target_goal) == 0) {
                            goal_name = counter->display_name;
                            break;
                        }
                    }
                    if (goal_name == hb->target_goal) {
                        for (const auto &toggle_goal: custom_toggles) {
                            if (strcmp(toggle_goal->root_name, hb->target_goal) == 0) {
                                goal_name = toggle_goal->display_name;
                                break;
                            }
                        }
                    }

                    for (int slot = 0; slot < HOTKEY_SLOT_COUNT && conflict[0] == '\0'; ++slot) {
                        const char *stored = hotkey_binding_slot_key(hb, (HotkeySlot) slot);
                        if (!app_slot_bound(stored)) continue;

                        char keycap[64];
                        counter_key_as_keycap(stored, keycap, sizeof(keycap));
                        if (app_same_combo(hk->key, hk->mods, keycap,
                                           hotkey_binding_slot_mods(hb, (HotkeySlot) slot))) {
                            char slot_word[32];
                            snprintf(slot_word, sizeof(slot_word), "%s",
                                     hotkey_slot_name((HotkeySlot) slot));
                            for (char *c = slot_word; *c != '\0'; ++c) *c = (char) tolower((unsigned char) *c);
                            snprintf(conflict, sizeof(conflict),
                                     "collides with the %s hotkey of the custom goal \"%s\"",
                                     slot_word, goal_name);
                        }
                    }
                }

                // Blocking Apply is decided by collect_hotkey_conflicts() above the tab bar, which
                // sees this clash whether or not the group holding the row is open.
                if (conflict[0] != '\0' && group_open) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "    %s", conflict);
                }
            }

            ImGui::EndTabItem();
        } // End of Hotkeys Tab

        if (ImGui::BeginTabItem("System & Debug")) {
            ImGui::Text("System");

            ImGui::Checkbox("Auto-Check for Updates", &temp_settings.check_for_updates);
            if (ImGui::IsItemHovered()) {
                char auto_update_tooltip_buffer[1024];
                int auto_update_len = snprintf(auto_update_tooltip_buffer, sizeof(auto_update_tooltip_buffer),
                                               "If enabled, Advancely will check for a new version on startup and notify you if one is available.\n"
                                               "You can see your current version (vX.X.X) in the title of the main Advancely window.\n"
                                               "Through that notification you'll then be able to automatically install the update\n"
                                               "for your operating system. You can find more instructions on that popup.\n"
                                               "Default: On");
#ifdef __linux__
                if (auto_update_len > 0 && (size_t) auto_update_len < sizeof(auto_update_tooltip_buffer)) {
                    snprintf(auto_update_tooltip_buffer + auto_update_len,
                             sizeof(auto_update_tooltip_buffer) - (size_t) auto_update_len,
                             "\n\nLinux: the built-in updater only works for the portable build.\n"
                             "If you installed Advancely from a package (.deb/.rpm/AUR/NixOS),\n"
                             "update it through that installer or your package manager instead.");
                }
#else
                (void) auto_update_len;
#endif
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
                         "Everything the application prints to a console (like MSYS2 MINGW64) can also be found in advancely_log.txt.\n"
                         "Default: Off");
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

    // Apply Settings (Ctrl+S / Cmd+S by default, rebindable in the Hotkeys tab)
    const bool ctrl_s_pressed = (t && t->settings_apply_pressed);
    const bool enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsAnyItemActive();
    const bool window_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool no_popup_open = !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);

    // Disable "Apply Settings" button on visual editing mode or unsaved template editor changes
    bool visual_editing = t && t->is_visual_layout_editing;
    bool template_unsaved = t && t->template_editor_has_unsaved_changes;
    bool apply_disabled = visual_editing || template_unsaved || coop_host_input_error || hotkey_duplicate_error ||
                          hotkey_reserved_error || hotkey_app_error ||
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

                // Preserve runtime state that is managed outside the settings UI
                temp_settings.use_manual_layout = app_settings->use_manual_layout;

                // Preserve the goal hiding mode — it's now driven by the tracker's
                // dropdown, not the settings UI. Without this, Apply Settings reverts
                // the hiding mode to whatever it was when the window was opened.
                temp_settings.goal_hiding_mode = app_settings->goal_hiding_mode;
                temp_settings.invert_hiding_mode = app_settings->invert_hiding_mode;

                // Preserve the coop_players roster — it's managed by the lobby sync in main.cpp,
                // not the settings UI. Without this, Apply Settings wipes the roster to 0 players
                // and the host broadcasts empty (0%) progress.
                temp_settings.coop_player_count = app_settings->coop_player_count;
                memcpy(temp_settings.coop_players, app_settings->coop_players,
                       sizeof(app_settings->coop_players));

                // Advancement assignments are edited live in app_settings (like the
                // roster), so preserve them across the temp->app copy below.
                temp_settings.coop_adv_assignment_count = app_settings->coop_adv_assignment_count;
                memcpy(temp_settings.coop_adv_assignments, app_settings->coop_adv_assignments,
                       sizeof(app_settings->coop_adv_assignments));

                // Decide whether the overlay must restart BEFORE app_settings is
                // overwritten: only bounce it when a setting the overlay actually
                // reads changed (app_settings still holds the pre-Apply values here).
                const bool overlay_changed = overlay_settings_different(app_settings, &temp_settings);

                // Copy temp settings to the real settings, save, and trigger a reload
                memcpy(app_settings, &temp_settings, sizeof(AppSettings));
                memcpy(&saved_settings, &temp_settings, sizeof(AppSettings)); // Update clean snapshot
                SDL_SetWindowAlwaysOnTop(t->window, app_settings->tracker_always_on_top);
                settings_save(app_settings, nullptr, SAVE_CONTEXT_ALL);
                // settings_save left the progress sections as-is; if this Apply is for a
                // loaded preset, restore that preset's captured progress before the reload.
                if (pending_preset_progress_path[0] != '\0') {
                    copy_preset_progress_to_settings(pending_preset_progress_path);
                    pending_preset_progress_path[0] = '\0';
                }
                SDL_SetAtomicInt(&g_settings_changed, 1); // Trigger a reload
                // Only restart the overlay when a setting it reads actually changed.
                // Tracker/UI/hotkey/coop-only changes leave the overlay running.
                if (overlay_changed) {
                    SDL_SetAtomicInt(&g_apply_button_clicked, 1);
                }

                // Update template sync for connected receivers if hosting
                if (app_settings->network_mode == NETWORK_HOST && g_coop_ctx) {
                    update_coop_template_sync(app_settings);
                    coop_net_broadcast_template_sync(g_coop_ctx);
                }

                show_applied_message = true;
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
        } else if (hotkey_reserved_error) {
            snprintf(apply_button_tooltip_buffer, sizeof(apply_button_tooltip_buffer),
                     "Disabled because a hotkey uses a combination Advancely reserves\n"
                     "for itself. Rebind the highlighted slot in the Hotkeys tab.");
        } else if (hotkey_app_error) {
            snprintf(apply_button_tooltip_buffer, sizeof(apply_button_tooltip_buffer),
                     "Disabled because an Advancely hotkey collides with another\n"
                     "hotkey that can fire at the same time. Rebind the highlighted\n"
                     "row in the Hotkeys tab.");
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
            pending_preset_progress_path[0] = '\0';
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
        // Resetting to defaults discards any loaded preset, so don't restore its progress.
        pending_preset_progress_path[0] = '\0';

        // Preserve current window geometry before resetting other settings
        WindowRect current_tracker_window = temp_settings.tracker_window;
        WindowRect current_overlay_window = temp_settings.overlay_window;

        // Preserve the goal hiding mode — it's driven by the tracker's dropdown,
        // not the settings UI, so Reset To Defaults must not touch it.
        GoalHidingMode current_hiding_mode = temp_settings.goal_hiding_mode;
        bool current_invert_hiding = temp_settings.invert_hiding_mode;

        // Reset the temporary settings struct to the default values
        settings_set_defaults(&temp_settings);

        // Restore the preserved window geometry
        temp_settings.tracker_window = current_tracker_window;
        temp_settings.overlay_window = current_overlay_window;

        // Restore the preserved goal hiding mode
        temp_settings.goal_hiding_mode = current_hiding_mode;
        temp_settings.invert_hiding_mode = current_invert_hiding;
    }
    if (ImGui::IsItemHovered()) {
        char tooltip_buffer[512];
        snprintf(tooltip_buffer, sizeof(tooltip_buffer),
                 "Resets all settings (besides window size/position & hotkeys) in this window to their\n"
                 "default values. This does not modify your template files.\n\n"
                 "Hover over any individual setting to see the default value it will be reset to.\n"
                 "The full default set also lives in %s/settings.json.", get_reference_files_display_path());
        ImGui::SetTooltip("%s", tooltip_buffer);
    }

    ImGui::SameLine();

    // Windows-only: the in-app relaunch is unreliable on Linux/macOS, so the button is
    // omitted there and users restart manually (the warnings above adjust their wording).
#ifdef _WIN32
    if (apply_disabled) ImGui::BeginDisabled();
    if (ImGui::Button("Restart Advancely")) {
        // 1. Save any pending changes from the settings window first.
        temp_settings.use_manual_layout = app_settings->use_manual_layout;
        temp_settings.goal_hiding_mode = app_settings->goal_hiding_mode;
        temp_settings.invert_hiding_mode = app_settings->invert_hiding_mode;
        temp_settings.coop_player_count = app_settings->coop_player_count;
        memcpy(temp_settings.coop_players, app_settings->coop_players,
               sizeof(app_settings->coop_players));
        temp_settings.coop_adv_assignment_count = app_settings->coop_adv_assignment_count;
        memcpy(temp_settings.coop_adv_assignments, app_settings->coop_adv_assignments,
               sizeof(app_settings->coop_adv_assignments));
        memcpy(app_settings, &temp_settings, sizeof(AppSettings));
        settings_save(app_settings, nullptr, SAVE_CONTEXT_ALL);
        // Restore a loaded preset's captured progress before the relaunch reads settings.json.
        if (pending_preset_progress_path[0] != '\0') {
            copy_preset_progress_to_settings(pending_preset_progress_path);
            pending_preset_progress_path[0] = '\0';
        }
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
        } else if (hotkey_reserved_error) {
            snprintf(restart_button_tooltip_buffer, sizeof(restart_button_tooltip_buffer),
                     "Disabled because a hotkey uses a combination Advancely reserves\n"
                     "for itself. Rebind the highlighted slot in the Hotkeys tab.");
        } else {
            snprintf(restart_button_tooltip_buffer, sizeof(restart_button_tooltip_buffer),
                     "Saves all current settings and restarts the application.\n"
                     "This is required to apply changes to fonts within the tracker window.");
        }
        ImGui::SetTooltip("%s", restart_button_tooltip_buffer);
    }
    if (apply_disabled) ImGui::EndDisabled();
#endif // _WIN32: Restart Advancely button (Linux/macOS restart manually)

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

    ImGui::SameLine();

    if (ImGui::Button("Report Issue")) {
        open_content("https://discord.gg/TyNgXDz");
    }
    if (ImGui::IsItemHovered()) {
        char report_tooltip[512];
        snprintf(report_tooltip, sizeof(report_tooltip),
                 "Opens the official Advancely Discord.\n\n"
                 "There's a channel dedicated to the Advancely tracker where\n"
                 "issues, feedback, and criticism are welcome. It's the most\n"
                 "direct way to be in touch with LNXS.");
        ImGui::SetTooltip("%s", report_tooltip);
    }

    if (roboto_font) {
        ImGui::PopFont();
    }

    ImGui::End();
}
