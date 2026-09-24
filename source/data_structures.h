// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 30.06.2025.
//

// A core header file for the data structures used in tracker.h and other files

#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H


#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include <cJSON.h>
}

// TODO: Modify socials (or timing) at the top of overlay.cpp

// The X-Macro list of all supported Minecraft versions.
// This is the single source of truth for versions.
#define VERSION_LIST \
    /* Era 1: Legacy Stats (.dat file), counts playtime in Ticks ID: 1100 */ \
    X(MC_VERSION_1_0, "1.0") \
    X(MC_VERSION_1_1, "1.1") \
    X(MC_VERSION_1_2_1, "1.2.1") \
    X(MC_VERSION_1_2_2, "1.2.2") \
    X(MC_VERSION_1_2_3, "1.2.3") \
    X(MC_VERSION_1_2_4, "1.2.4") \
    X(MC_VERSION_1_2_5, "1.2.5") \
    X(MC_VERSION_1_3_1, "1.3.1") \
    X(MC_VERSION_1_3_2, "1.3.2") \
    X(MC_VERSION_1_4_2, "1.4.2") \
    X(MC_VERSION_1_4_4, "1.4.4") \
    X(MC_VERSION_1_4_5, "1.4.5") \
    X(MC_VERSION_1_4_6, "1.4.6") \
    X(MC_VERSION_1_4_7, "1.4.7") \
    X(MC_VERSION_1_5_1, "1.5.1") \
    X(MC_VERSION_1_5_2, "1.5.2") \
    X(MC_VERSION_1_6_1, "1.6.1") \
    X(MC_VERSION_1_6_2, "1.6.2") \
    X(MC_VERSION_1_6_4, "1.6.4") \
    /* Era 2: Mid-era Achievements/Stats (per-world JSON), stat.playOneMinute is in ticks */ \
    X(MC_VERSION_1_7_2, "1.7.2") \
    X(MC_VERSION_1_7_3, "1.7.3") \
    X(MC_VERSION_1_7_4, "1.7.4") \
    X(MC_VERSION_1_7_5, "1.7.5") \
    X(MC_VERSION_1_7_6, "1.7.6") \
    X(MC_VERSION_1_7_7, "1.7.7") \
    X(MC_VERSION_1_7_8, "1.7.8") \
    X(MC_VERSION_1_7_9, "1.7.9") \
    X(MC_VERSION_1_7_10, "1.7.10") \
    X(MC_VERSION_1_8, "1.8") \
    X(MC_VERSION_1_8_1, "1.8.1") \
    X(MC_VERSION_1_8_2, "1.8.2") \
    X(MC_VERSION_1_8_3, "1.8.3") \
    X(MC_VERSION_15W14A, "15w14a") \
    X(MC_VERSION_1_8_4, "1.8.4") \
    X(MC_VERSION_1_8_5, "1.8.5") \
    X(MC_VERSION_1_8_6, "1.8.6") \
    X(MC_VERSION_1_8_7, "1.8.7") \
    X(MC_VERSION_1_8_8, "1.8.8") \
    X(MC_VERSION_1_8_9, "1.8.9") \
    X(MC_VERSION_1_9, "1.9") \
    X(MC_VERSION_1_9_1, "1.9.1") \
    X(MC_VERSION_1_9_2, "1.9.2") \
    X(MC_VERSION_1_RV_PRE1, "1.rv-pre1") \
    X(MC_VERSION_1_9_3, "1.9.3") \
    X(MC_VERSION_1_9_4, "1.9.4") \
    X(MC_VERSION_1_10, "1.10") \
    X(MC_VERSION_1_10_1, "1.10.1") \
    X(MC_VERSION_1_10_2, "1.10.2") \
    X(MC_VERSION_1_11, "1.11") \
    X(MC_VERSION_1_11_1, "1.11.1") \
    X(MC_VERSION_1_11_2, "1.11.2") \
    /* Era 3: Modern Advancements (separate per-world JSONs), stat.playOneMinute is in ticks */ \
    X(MC_VERSION_1_12, "1.12") \
    X(MC_VERSION_1_12_1, "1.12.1") \
    X(MC_VERSION_1_12_2, "1.12.2") \
/* Era 3: Modern Stats(separate per-world JSONs), minecraft:play_one_minute is in ticks */ \
    X(MC_VERSION_1_13, "1.13") \
    X(MC_VERSION_1_13_1, "1.13.1") \
    X(MC_VERSION_1_13_2, "1.13.2") \
    X(MC_VERSION_3D_SHAREWARE_V1_34, "3d_shareware_v1.34") \
    X(MC_VERSION_1_14, "1.14") \
    X(MC_VERSION_1_14_1, "1.14.1") \
    X(MC_VERSION_1_14_2, "1.14.2") \
    X(MC_VERSION_1_14_3, "1.14.3") \
    X(MC_VERSION_1_14_4, "1.14.4") \
    X(MC_VERSION_1_15, "1.15") \
    X(MC_VERSION_1_15_1, "1.15.1") \
    X(MC_VERSION_1_15_2, "1.15.2") \
    X(MC_VERSION_20W14INFINITE, "20w14infinite") \
    X(MC_VERSION_1_16, "1.16") \
    X(MC_VERSION_1_16_1, "1.16.1") \
    X(MC_VERSION_1_16_2, "1.16.2") \
    X(MC_VERSION_1_16_3, "1.16.3") \
    X(MC_VERSION_1_16_4, "1.16.4") \
    X(MC_VERSION_1_16_5, "1.16.5") \
    /* minecraft:play_one_minute FINALLY renamed to minecraft:play_time */ \
    X(MC_VERSION_1_17, "1.17") \
    X(MC_VERSION_1_17_1, "1.17.1") \
    X(MC_VERSION_1_18, "1.18") \
    X(MC_VERSION_1_18_1, "1.18.1") \
    X(MC_VERSION_1_18_2, "1.18.2") \
    X(MC_VERSION_22W13ONEBLOCKATATIME, "22w13oneblockatatime") \
    X(MC_VERSION_1_19, "1.19") \
    X(MC_VERSION_1_19_1, "1.19.1") \
    X(MC_VERSION_1_19_2, "1.19.2") \
    X(MC_VERSION_1_19_3, "1.19.3") \
    X(MC_VERSION_1_19_4, "1.19.4") \
    X(MC_VERSION_23W13A_OR_B, "23w13a_or_b") \
    X(MC_VERSION_1_20, "1.20") \
    X(MC_VERSION_1_20_1, "1.20.1") \
    X(MC_VERSION_1_20_2, "1.20.2") \
    X(MC_VERSION_1_20_3, "1.20.3") \
    X(MC_VERSION_1_20_4, "1.20.4") \
    X(MC_VERSION_24W14POTATO, "24w14potato") \
    X(MC_VERSION_1_20_5, "1.20.5") \
    X(MC_VERSION_1_20_6, "1.20.6") \
    X(MC_VERSION_1_21, "1.21") \
    X(MC_VERSION_1_21_1, "1.21.1") \
    X(MC_VERSION_1_21_2, "1.21.2") \
    X(MC_VERSION_1_21_3, "1.21.3") \
    X(MC_VERSION_1_21_4, "1.21.4") \
    X(MC_VERSION_1_21_5, "1.21.5") \
    X(MC_VERSION_25W14CRAFTMINE, "25w14craftmine") \
    X(MC_VERSION_1_21_6, "1.21.6") \
    X(MC_VERSION_1_21_7, "1.21.7") \
    X(MC_VERSION_1_21_8, "1.21.8") \
    X(MC_VERSION_1_21_9, "1.21.9") \
    X(MC_VERSION_1_21_10, "1.21.10") \
    X(MC_VERSION_1_21_11, "1.21.11") \
    /* year.big_release.minor_hotfix, `players/advancements` and `players/stats` */ \
    X(MC_VERSION_26_1, "26.1") \
    X(MC_VERSION_26_1_1, "26.1.1") \
    /* 2026 April Fools Version HERDCRAFT */ \
    X(MC_VERSION_26W14A, "26w14a") \
    X(MC_VERSION_26_1_2, "26.1.2") \
    X(MC_VERSION_26_2, "26.2") \
    X(MC_VERSION_26_3, "26.3") \
    X(MC_VERSION_26_4, "26.4") \
    X(MC_VERSION_27_1, "27.1")

#ifdef __cplusplus
extern "C" {
#endif


// If MAX_PATH_LENGTH is not defined, define it here
#ifndef MAX_PATH_LENGTH
#define MAX_PATH_LENGTH 1024
#endif

typedef struct {
    int frame_count; // How many frames are in the animation
    SDL_Texture **frames; // An array of textures, one for each frame
    int *delays; // An array of delays (in ms) for each frame
    Uint32 total_duration; // The sum of all delays, for looping
} AnimatedTexture;

// Defines the reference point on an element's bounding box for manual positioning.
// The anchor determines which point of the element the coordinates of the template editor refer to.
enum AnchorPoint {
    ANCHOR_TOP_LEFT = 0,
    ANCHOR_TOP_CENTER,
    ANCHOR_TOP_RIGHT,
    ANCHOR_CENTER_LEFT,
    ANCHOR_CENTER,
    ANCHOR_CENTER_RIGHT,
    ANCHOR_BOTTOM_LEFT,
    ANCHOR_BOTTOM_CENTER,
    ANCHOR_BOTTOM_RIGHT
};

// Maximum absolute value for manual position coordinates (well within float integer precision)
#define MANUAL_POS_MAX 10000000.0f

// Represents a manually set position for an element on the tracker map
struct ManualPos {
    float x;
    float y;
    bool is_set; // True if the user has manually placed this, false to fallback to auto-layout
    bool is_hidden_in_layout; // If true, this element is hidden in the manual layout (does not affect auto layout)
    AnchorPoint anchor = ANCHOR_TOP_LEFT; // The reference point for the coordinates

    // Runtime only (never serialized): the world-space rect this element last rendered at.
    // Lets the template editor seed manual coordinates with the element's current position.
    float auto_x = 0.0f;
    float auto_y = 0.0f;
    float auto_w = 0.0f;
    float auto_h = 0.0f;
    bool auto_valid = false;
};

// --------- STAT AUTO-COMPLETION VIA LINKED GOALS ---------

enum LinkedGoalMode {
    LINKED_GOAL_AND = 0, // All linked goals must be completed
    LINKED_GOAL_OR = 1 // At least one linked goal must be completed
};

// Which template section a linked goal points at. LINK_TYPE_ANY (the default when a
// link omits the "type" field) preserves the legacy resolution order (advancements first,
// then stats, etc.) for backwards compatibility. Any other value restricts resolution to
// that single section so identically-named goals in different sections cannot collide
// (e.g. the "pufferfish" stat vs. the "pufferfish" balanced_diet advancement criterion).
enum LinkedGoalType {
    LINK_TYPE_ANY = 0,
    LINK_TYPE_ADVANCEMENT,
    LINK_TYPE_STAT,
    LINK_TYPE_UNLOCK,
    LINK_TYPE_CUSTOM,
    LINK_TYPE_MULTI_STAGE,
    LINK_TYPE_COUNTER
};

// Maps a "type" string from a template's linked goal entry to LinkedGoalType.
// Unknown or missing strings resolve to LINK_TYPE_ANY (legacy behavior).
inline LinkedGoalType linked_goal_type_from_string(const char *s) {
    if (!s || s[0] == '\0') return LINK_TYPE_ANY;
    if (strcmp(s, "advancement") == 0) return LINK_TYPE_ADVANCEMENT;
    if (strcmp(s, "stat") == 0) return LINK_TYPE_STAT;
    if (strcmp(s, "unlock") == 0) return LINK_TYPE_UNLOCK;
    if (strcmp(s, "custom") == 0) return LINK_TYPE_CUSTOM;
    if (strcmp(s, "multi_stage") == 0) return LINK_TYPE_MULTI_STAGE;
    if (strcmp(s, "counter") == 0) return LINK_TYPE_COUNTER;
    return LINK_TYPE_ANY;
}

// Inverse of linked_goal_type_from_string. Returns nullptr for LINK_TYPE_ANY so callers
// can omit the "type" field entirely when it carries no disambiguation.
inline const char *linked_goal_type_to_string(LinkedGoalType t) {
    switch (t) {
        case LINK_TYPE_ADVANCEMENT: return "advancement";
        case LINK_TYPE_STAT: return "stat";
        case LINK_TYPE_UNLOCK: return "unlock";
        case LINK_TYPE_CUSTOM: return "custom";
        case LINK_TYPE_MULTI_STAGE: return "multi_stage";
        case LINK_TYPE_COUNTER: return "counter";
        default: return nullptr;
    }
}

// Represents a reference to another goal in the template: what a counter counts, what
// auto-completes a stat or custom goal, and what a multi-stage stage mirrors.
struct CounterLinkedGoal {
    char root_name[192]; // The root_name of the linked goal
    char stage_id[64]; // For multi-stage goal stages (empty = whole goal)
    char parent_root[192]; // Parent root_name for sub-items (criteria, sub-stats) (empty = top-level)
    LinkedGoalType type; // Which section to resolve root_name in (LINK_TYPE_ANY = legacy search order)
};

// A generic struct for a sub-item, like and advancement's criterion or a stat
struct TrackableItem {
    char root_name[192]; // The unique ID, e.g., "minecraft:husbandry/balanced_diet"
    char display_name[192]; // The user-facing name, e.g., "A Balanced Diet"
    char icon_path[256]; // Relative path to the icon, e.g., "items/apple.png"

    uint64_t icon_hash; // Cache for the image hash to prevent lag

    SDL_Texture *texture; // The loaded texture for the icon.
    AnimatedTexture *anim_texture; // To support .gif files

    // Pre-parsed keys for modern stat lookups
    char stat_category_key[192]; // e.g., "minecraft:custom"
    char stat_item_key[192]; // e.g., "minecraft:jump"

    // Per-advancement criteria grouping. Criteria sharing the same non-empty
    // group ID inside one advancement collapse into a single progress unit:
    // one done means the whole group is done. Empty = ungrouped.
    char group[64];
    // Set on every grouped criterion except the first member of its group (when grouping is
    // active on the parent). The first member stands in for the whole group on the tracker map,
    // the overlay rows and the compact stack, so these are skipped wherever criteria are shown.
    bool hidden_by_group;

    bool done; // For advancements/unlocks: Is it completed?
    int progress; // For stats: The current value, e.g., 5.
    int goal; // For stats: The target value, e.g., 40.
    // Template "hide_progress": with a target of exactly 1 the "(0/1)" is left out everywhere the
    // goal shows. On unless the template says otherwise. Read it through item_progress_hidden().
    bool hide_progress;

    // For legacy stat snapshotting
    int initial_progress;

    // Flag to allow "conflicting" criteria to overlay parent advancements icon (e.g., hoglin), init with false, cause of calloc
    bool is_shared;
    // Set with is_shared when another criterion has this icon under a parent with the identical icon
    // (the same goal or a look-alike one), so the shared icon would look the same on both and tells
    // nothing apart. The tracker and the overlay each have a setting that decides whether it still shows.
    bool is_shared_same_parent;
    bool is_manually_completed; // Allow manually overriding sub-stats (NOT FOR ACHIEVEMENTS/ADVANCEMENTS)
    bool is_hidden; // If true, this item is hidden unless "Remove Completed Goals" is off
    bool in_2nd_row; // Forces custom goals (or potentially stats) to the 2nd overlay row
    bool in_3rd_row; // Forces unlocks to the 3rd overlay row (only meaningful for default Row 2 items)

    // Auto-completion via linked goals (used for sub-stats and manual custom goals)
    int linked_goal_count;
    CounterLinkedGoal *linked_goals; // Dynamically allocated array of linked goals
    LinkedGoalMode linked_goal_mode; // AND (all) or OR (any) for auto-completion

    // Animation State
    float alpha; // Current transparency (1.0f = opaque, 0.0f = transparent)
    bool is_visible_on_overlay; // Tracks if the item should be rendered

    // Manual Layout Positions
    ManualPos icon_pos;
    ManualPos text_pos;
    ManualPos progress_pos; // For custom goals to separate progress, not for unlocks

    // Coop (sub-stats, HIGHEST mode only): UUID of the current highest-value
    // player. Empty when no player has contributed yet. Cleared on reset.
    char highest_contributor_uuid[48];

    // Coop: UUID of the sole player who manually checked this sub-stat's
    // manual-completion checkbox. Empty when nobody manually completed it
    // OR when more than one player did (multi-manual = no face).
    char manual_completer_uuid[48];

    // Coop (custom goals only): UUID of the sole player who contributed to
    // this custom goal (progress > 0 or done). HOST_ONLY mode: host UUID iff
    // host contributed. ANY_PLAYER mode: lone contributor or empty if 0/2+.
    char custom_contributor_uuid[48];

    // Cached display-name width for the auto-layout width pre-pass. display_name is
    // immutable, so this is only re-measured when the font size changes.
    float cached_name_w;
    float cached_name_w_font;

    // Cached progress-text width for the pre-pass (re-measured when text or font changes).
    char cached_prog_text[32];
    float cached_prog_w;
    float cached_prog_font;
};

// True when a goal's "(0/1)" is left out: its target is exactly 1 and the template hides it.
inline bool item_progress_hidden(const TrackableItem *item) {
    return item && item->goal == 1 && item->hide_progress;
}


// A struct to hold a category of trackable items (e.g., all Advancements).
// This can be used for Advancements that have sub-criteria.
struct TrackableCategory {
    char root_name[192];
    char display_name[192];
    char icon_path[256];
    SDL_Texture *texture; // Main icon texture for category/advancement
    AnimatedTexture *anim_texture; // To support .gif files
    uint64_t icon_hash; // Cache for the image hash (shared icon detection compares parent icons by it)

    // Recipe flag for modern version advancements
    bool is_recipe;

    // If stat category has no "criteria": {} it's single stat.
    // If one criteria is defined it's still treated as a multi-stat in terms of rendering.
    bool is_single_stat_category;

    SDL_Texture *texture_bg;
    SDL_Texture *texture_bg_half_done;
    SDL_Texture *texture_bg_done;

    bool done;
    bool is_manually_completed; // For manually overriding stats (as they have criteria now with sub-stats)
    bool is_hidden; // If true, this category is hidden unless "Remove Completed Goals" is off.
    bool in_2nd_row; // Forces this stat category (does not apply to complex adv.) to 2nd row of overlay
    bool in_3rd_row; // Forces advancements/recipes to the 3rd overlay row (only meaningful for default Row 2 items)
    // Keeps this multi-stat's sub-stats out of the overlay's 1st row (only meaningful for multi-stat categories).
    // They still cycle as the category's sub-text in the other rows.
    bool hide_substats_in_row1;
    bool groups_enabled; // When false, criterion "group" fields are ignored (no collapse), even if present.

    // Stat auto-completion via linked goals (only used for stat categories)
    int linked_goal_count;
    CounterLinkedGoal *linked_goals; // Dynamically allocated array of linked goals
    LinkedGoalMode linked_goal_mode; // AND (all) or OR (any) for auto-completion
    // To set an advancement/achievement to done when all the template criteria are met.
    // When game says advancement is done, then the advancement gets visually marked as done with the done background.
    // There could be a mistake in the template file, that an advancement has criteria that don't exist in the game,
    // then it should keep the advancement completed, but still display it even if "remove completed goals" is on.
    // It will then continue displaying with the other incorrect criteria for debugging.
    bool all_template_criteria_met;
    bool done_in_snapshot; // For legacy stat snapshotting (for achievements)
    int progress;
    int goal;

    int criteria_count;
    int completed_criteria_count; // Group-collapsed numerator (matches criteria_progress_total when fully done).
    // Group-collapsed denominator: (distinct group IDs) + (ungrouped criteria).
    // Equals criteria_count when no criteria have a group ID set.
    int criteria_progress_total;
    TrackableItem **criteria; // An array of sub-items

    // Coop: UUID of the player who first completed this goal in the merged "All Players" view.
    // Empty when not yet completed by any player. Cleared on goal reset / world change.
    // For simple advancements (criteria_count == 0) only; complex advancements use their own logic.
    char first_contributor_uuid[48];

    // Coop: UUID of the player this advancement is assigned to in the "All Players" merged view.
    // When set (host-only), only the owner's progress drives this advancement instead of the
    // default "player with the most criteria" rule. Empty = unassigned (Auto). Resolved from
    // settings each merge cycle; only meaningful for complex advancements (criteria_count > 0).
    char assigned_owner_uuid[48];

    // Coop (stat categories): UUID of the sole player who manually checked the
    // full-goal manual-completion checkbox. Empty when nobody manually completed
    // it OR when more than one player did (multi-manual = no face).
    char manual_completer_uuid[48];

    // Animation State
    float alpha; // Current transparency (1.0f = opaque, 0.0f = transparent)
    bool is_visible_on_overlay; // Tracks if the category should be rendered

    // Scroll state for long lists
    float scroll_y;

    // Manual Layout Positions
    ManualPos icon_pos;
    ManualPos text_pos;
    ManualPos progress_pos;

    // Cached display-name width for the auto-layout width pre-pass (see TrackableItem).
    float cached_name_w;
    float cached_name_w_font;

    // Cached progress-text width for the pre-pass (see TrackableItem).
    char cached_prog_text[32];
    float cached_prog_w;
    float cached_prog_font;
};


// --------- MULTI-STAGE LONG-TERM GOALS ---------

enum SubGoalType {
    SUBGOAL_STAT,
    SUBGOAL_ADVANCEMENT,
    SUBGOAL_UNLOCK, // Allows to also complete a stage based on a specific unlock
    SUBGOAL_CRITERION,
    // Allows to complete a stage based on a specific advancement/achievement criterion, e.g., visit plains biome
    // For goals with no automatic trigger, used for final stages (displays once all previous stages are done)
    SUBGOAL_MANUAL, // When it's the final stage, so not "stat", "advancement", "unlock", "criterion"
    // The stage simply reflects another goal in the template (see SubGoal::mirror_target). Runtime
    // only: the template file keeps whatever trigger type the stage had, and the presence of the
    // stage's "mirror_goal" object is what turns it into this on load, so unticking the editor's
    // mirror checkbox brings the old type back.
    SUBGOAL_MIRROR
};

// Represents one step in a multi-stage goal
struct SubGoal {
    char stage_id[64]; // Unique ID for every stage e.g., "0", "1", "final_stage"
    char display_text[192]; // e.g., "Awaiting thunder"
    SubGoalType type; // What kind of trigger to check for
    char parent_advancement[192]; // Used for "criterion" stage of multi-stage goal
    char root_name[192]; // The target, e.g., "minecraft:trident" or "minecraft:adventure/very_very_frightening"
    int required_progress; // The value to reach, e.g., 1
    bool hide_progress; // Stat stages: same as TrackableItem::hide_progress, applied in ms_stage_shown_target()
    int current_stat_progress; // Current value of stat within multi-stage goal

    // Stat stages only: count from the value the stat held when this stage was reached, instead of
    // from the stat's absolute value. current_stat_progress then starts at 0 when the stage begins,
    // and a stage that has not been reached yet reports 0 rather than its raw value, so an
    // already-high stat can never satisfy a future stage before the goal gets there.
    bool count_from_stage;
    // Whether current_stat_progress is a real count yet, i.e. whoever it is being shown for has a
    // zero point. The zero points themselves are per player and live in the tracker's own store, NOT
    // here: in co-op this struct is a display buffer that is swapped between views, so anything kept
    // here belongs to whichever view was rendered last. This flag is carried alongside the progress
    // it describes (see merge_coop_progress) precisely so the two can never disagree.
    bool stat_counting;
    bool coop_completed; // Co-op: true if any player completed this stage (non-stat types)
    bool game_trigger_met; // True if this stage's natural trigger (advancement/criterion/unlock) is met.
    // Stored so recalculation paths (e.g. manual custom-goal toggles) that lack player files can re-derive
    // current_stage from (game trigger OR linked goals) and regress as well as advance. Stat stages instead
    // re-derive live from current_stat_progress, so this flag is only authoritative for non-stat stages.

    // Auto-completion via linked goals (used for non-final stages, like sub-stats/custom goals)
    int linked_goal_count;
    CounterLinkedGoal *linked_goals; // Dynamically allocated array of linked goals
    LinkedGoalMode linked_goal_mode; // AND (all) or OR (any) for auto-completion

    // When true, this (non-final) stage is also considered satisfied if the next stage is satisfied.
    // Propagated backward, so a completed later stage pulls earlier opted-in stages forward.
    bool complete_with_next;

    // --- Mirror stages (type == SUBGOAL_MIRROR) ---
    // The goal this stage reflects. Completion comes from it alone: the loader puts it in
    // linked_goals as the stage's only entry, so every path that already resolves linked goals
    // satisfies a mirror stage too. The stage's own trigger fields are left as the template had
    // them and ignored.
    CounterLinkedGoal mirror_target;
    // The mirrored goal's numbers, re-resolved on every update so they reach the overlay with the
    // rest of the stage (see tracker_update_mirror_stages). mirror_required of 0 means the mirrored
    // goal shows no number at all, -1 that it counts up without a target.
    int mirror_progress;
    int mirror_required;

    char icon_path[256];
    SDL_Texture *texture;
    AnimatedTexture *anim_texture;
    uint64_t icon_hash;
};

// The value a stage puts after its display text, and the target it is counting towards. A stat
// stage counts towards its own target; a mirror stage borrows the numbers of the goal it reflects.
// A target of 0 means the stage shows no number, -1 that it counts up without one.
inline int ms_stage_shown_progress(const SubGoal *stage) {
    if (!stage) return 0;
    return (stage->type == SUBGOAL_MIRROR) ? stage->mirror_progress : stage->current_stat_progress;
}

inline int ms_stage_shown_target(const SubGoal *stage) {
    if (!stage) return 0;
    if (stage->type == SUBGOAL_MIRROR) return stage->mirror_required;
    if (stage->type == SUBGOAL_STAT) {
        // A stage whose target is exactly 1 can leave its "(0/1)" out, as if it had no number.
        if (stage->required_progress == 1 && stage->hide_progress) return 0;
        return stage->required_progress;
    }
    return 0;
}

// Formats the " (3/10)" or " (7)" that follows a stage's display text, or an empty string when the
// stage shows no number. Every width calculation and every draw of a stage goes through this, so
// the tracker, the overlay rows and the compact stack all measure and show the same text.
inline void ms_stage_progress_suffix(char *out, size_t out_size, const SubGoal *stage) {
    if (!out || out_size == 0) return;
    const int target = ms_stage_shown_target(stage);
    if (target > 0) snprintf(out, out_size, " (%d/%d)", ms_stage_shown_progress(stage), target);
    else if (target == -1) snprintf(out, out_size, " (%d)", ms_stage_shown_progress(stage));
    else out[0] = '\0';
}

// The widest that suffix can ever get, for the layout passes that reserve room up front instead of
// measuring the value a stage happens to show right now.
inline void ms_stage_progress_suffix_widest(char *out, size_t out_size, const SubGoal *stage) {
    if (!out || out_size == 0) return;
    const int target = ms_stage_shown_target(stage);
    if (target > 0) snprintf(out, out_size, " (%d/%d)", target, target);
    else if (target == -1) snprintf(out, out_size, " (%d)", ms_stage_shown_progress(stage));
    else out[0] = '\0';
}

// Represents a complete multi-stage goal
struct MultiStageGoal {
    char root_name[192]; // Unique ID for every multi-stage goal e.g., "ms_goal:getting_started"
    char display_name[192]; // The overall name, e.g., "Thunder advancements"
    char icon_path[256]; // The icon for the entire goal
    SDL_Texture *texture; // The loaded icon texture
    AnimatedTexture *anim_texture; // To support .gif files

    bool use_stage_icons; // Flag to toggle per-stage icons

    int current_stage; // Index of the currently active sub-goal
    int stage_count; // How many stages there are
    SubGoal **stages; // An array of the sub-goals

    bool is_hidden; // IF true, this goal is hidden unless "Remove Completed Goals" is off.
    bool in_2nd_row; // Forces this MS Goal to the 2nd overlay row

    // Animation State
    float alpha; // Current transparency (1.0f = opaque, 0.0f = transparent)
    bool is_visible_on_overlay; // Tracks if the item should be rendered

    // Manual Layout Positions
    ManualPos icon_pos;
    ManualPos text_pos;
    ManualPos progress_pos;

    // Cached display-name width for the auto-layout width pre-pass (see TrackableItem).
    float cached_name_w;
    float cached_name_w_font;

    // Cached stage-text width for the pre-pass (re-measured when text or font changes).
    char cached_prog_text[256];
    float cached_prog_w;
    float cached_prog_font;
};

// --------- COUNTER GOALS (Completion Counters) ---------

// Represents a counter goal that tracks how many of a set of goals are completed
struct CounterGoal {
    char root_name[192]; // Unique ID, e.g., "counter:nether_progress"
    char display_name[192]; // The user-facing name
    char icon_path[256]; // Relative path to the icon
    SDL_Texture *texture; // The loaded icon texture
    AnimatedTexture *anim_texture; // To support .gif files

    int linked_goal_count; // Number of linked goals
    CounterLinkedGoal *linked_goals; // Dynamically allocated array of linked goals

    int completed_count; // How many linked goals are currently completed
    bool done; // True when all linked goals are completed (completed_count == linked_goal_count)

    bool is_hidden; // If true, hidden unless "Remove Completed Goals" is off
    bool in_2nd_row; // Forces this counter to the 2nd overlay row

    // Animation State
    float alpha; // Current transparency (1.0f = opaque, 0.0f = transparent)
    bool is_visible_on_overlay; // Tracks if the item should be rendered

    // Manual Layout Positions
    ManualPos icon_pos;
    ManualPos text_pos;
    ManualPos progress_pos;

    // Cached display-name width for the auto-layout width pre-pass (see TrackableItem).
    float cached_name_w;
    float cached_name_w_font;

    // Cached progress-text width for the pre-pass (see TrackableItem).
    char cached_prog_text[32];
    float cached_prog_w;
    float cached_prog_font;
};

// --------- DECORATIONS (Manual Layout Elements) ---------

enum DecorationType {
    DECORATION_TEXT_HEADER = 0,
    DECORATION_LINE,
    DECORATION_ARROW
};

#define MAX_ARROW_BENDS 16 // Max amount of bends an arrow decoration can have

// A decoration element for manual layout positioning (text headers, lines, arrows, etc.)
struct DecorationElement {
    char id[64]; // Unique ID, e.g., "header_1", "line_1", "arrow_1"
    DecorationType type;

    // Text Header fields
    char display_text[192]; // The user-facing text to render

    ManualPos pos; // Position on the tracker map (text headers use this; lines/arrows use as tail/endpoint 1)

    // Line fields
    ManualPos pos2; // Second endpoint for lines, tip for arrows
    float thickness; // Line/arrow thickness in pixels (before zoom)
    float opacity; // Line opacity 0.0-1.0

    // Arrow fields
    float arrowhead_size; // Arrowhead size in pixels (before zoom), default 12.0
    int bend_count; // Number of bend points between tail (pos) and tip (pos2)
    ManualPos bends[MAX_ARROW_BENDS]; // Intermediate bend points

    // Arrow goal linking: when start_goal_root is set, arrow opacity changes based on goal completion
    char start_goal_root[192]; // root_name of the starting goal (empty = no link)
    char start_goal_stage[64]; // stage_id if linking to a specific multi-stage goal stage (empty = whole goal)
    char end_goal_root[192]; // root_name of the ending goal (empty = no link)
    char end_goal_stage[64]; // stage_id if linking to a specific multi-stage goal stage (empty = whole goal)
    float opacity_before; // Arrow opacity before start goal is completed (default = ADVANCELY_FADED_ALPHA/255)
    float opacity_after; // Arrow opacity after start goal is completed (default = 1.0)

    // Text header goal linking: linked items remain visible when searching for the header's display text
    int linked_goal_count; // Number of linked goals (text headers only)
    CounterLinkedGoal *linked_goals; // Dynamically allocated array of linked goals (text headers only)
};

// --------- RUN COMPLETION (per-template stopping criteria) ---------

// Whole goal types a template can require for the run to count as completed. Stored in the
// template's "run_completion" section by key (see run_completion_type_key). RC_TYPE_COUNT stays last.
enum RunCompletionType {
    RC_TYPE_ADVANCEMENTS = 0, // Advancements (>= 1.12) / Achievements (<= 1.11.2), recipes excluded
    RC_TYPE_RECIPES, // Recipes (>= 1.12)
    RC_TYPE_STATS, // Stat categories (simple and multi)
    RC_TYPE_UNLOCKS,
    RC_TYPE_CUSTOM, // Custom goals
    RC_TYPE_MULTISTAGE, // Multi-stage goals
    RC_TYPE_COUNTERS, // Completion counters
    RC_TYPE_COUNT
};

// Which template list an individually required goal lives in.
enum RunCompletionGoalKind {
    RC_GOAL_ADVANCEMENT = 0, // Advancement, achievement or recipe
    RC_GOAL_STAT,
    RC_GOAL_UNLOCK,
    RC_GOAL_CUSTOM,
    RC_GOAL_MULTISTAGE,
    RC_GOAL_COUNTER,
    RC_GOAL_KIND_COUNT
};

struct RunCompletionGoalRef {
    RunCompletionGoalKind kind;
    char root_name[192];
};

// A template's completion rule. The required set is every goal of every checked type plus every
// individually listed goal; an empty set means "everything in the template". The run completes when
// the whole set is done, or (count/percent targets) when `count` goals of the set are done and/or the
// overall progress percentage reaches `percent`, combined with AND or OR via require_both.
struct RunCompletionRule {
    bool types[RC_TYPE_COUNT];
    int goal_ref_count;
    RunCompletionGoalRef *goal_refs; // Dynamically allocated (tracker side), NULL when empty
    bool use_count;
    int count; // Goals of the required set that must be done (1..set size)
    bool use_percent;
    float percent; // Overall progress percentage required (0.00..100.00)
    bool require_both; // Both count and percent targets must be met (AND) instead of either (OR)
};

// The main container for all data loaded from the template files.
struct TemplateData {
    int advancement_count; // Amount of advancements defined in the template under "advancements"
    int advancement_goal_count; // Count of actual advancements (non-recipes) for the UI counter
    int advancements_completed_count;
    TrackableCategory **advancements;

    // Stats support criteria like advancements
    int stat_count; // Number of stat goals
    int stats_completed_count; // For progress tracking
    int stat_total_criteria_count; // For progress tracking
    int stats_completed_criteria_count; // So individual stat criteria count towards percentage progress
    TrackableCategory **stats; // Stats can now be categories

    int unlock_count; // Number of unlocks
    TrackableItem **unlocks;
    int unlocks_completed_count; // Number of completed unlocks

    // Custom goals
    int custom_goal_count; // Number of custom goals
    TrackableItem **custom_goals;

    int multi_stage_goal_count; // Number of multi-stage goals
    MultiStageGoal **multi_stage_goals;

    int counter_goal_count; // Number of counter goals
    CounterGoal **counter_goals;

    int decoration_count; // Number of decoration elements (text headers, lines, arrows)
    DecorationElement **decorations;

    // Overall Progress Metrics
    int total_criteria_count;
    int total_progress_steps; // To disable percentage progress text if nothing contributes to it
    int completed_criteria_count;
    float overall_progress_percentage;
    // Percentage score of everything BUT ADVANCEMENTS (have their own advancements_completed_count)

    // Run completion rule from the template's "run_completion" section and the goals it requires.
    // completion_goal_count is 0 when the rule requires everything (the counter then falls back to
    // the advancement counter above). completion_label is the lang file's "run_completion.label",
    // empty when it has none; the counter then derives a name from completion_type_mask (bit per
    // RunCompletionType present in the required set: one type = its name, a mix = "Goals").
    RunCompletionRule run_completion;
    int completion_goal_count; // Size of the required goal set (0 = everything)
    int completion_goals_completed; // Done goals within that set
    int completion_type_mask;
    char completion_label[128];

    float host_time_since_last_update; // Co-op: host's update timer, mirrored by receivers

    long long play_time_ticks; // Store the player's total playtime in ticks
    long long frozen_play_time_ticks; // IGT frozen at the moment of run completion
    bool run_completed; // True once the run hits 100%; latches until reset
    // True when the run completed on a live Hermes event. Hermes carries no IGT, so the value
    // frozen above came from the previous game save. The first save that reports a higher play
    // time re-latches the real final time and clears this.
    bool frozen_ticks_pending;
    // IGT in milliseconds, read from the SpeedrunIGT mod's "<world>/speedrunigt/record.json".
    // The mod rewrites that file on every game save and when its own timer stops, and it is
    // millisecond-precise instead of being rounded to a 50 ms game tick, so it is preferred
    // over the tick counts above for every IGT Advancely displays. 0 when the mod isn't in use.
    long long speedrunigt_ms;

    // Taking snapshot for legacy versions when world is changed to track changes per world
    long long playtime_snapshot; // Stores playtime at world load for legacy versions
    char snapshot_world_name[MAX_PATH_LENGTH]; // The world the current snapshot belongs to
    char last_known_world_name[MAX_PATH_LENGTH]; // The last world the tracker was active in, to detect changes.
};

// PATHMODE AND VERSION STUFF

/**
 * @brief Enum to determine how the saves path is obtained.
 */
enum PathMode {
    PATH_MODE_AUTO, // Automatically detect the path from standard locations.
    PATH_MODE_MANUAL, // Use a user-provided path.
    PATH_MODE_INSTANCE, // Automatically track the active MultiMC/Prism instance
    PATH_MODE_FIXED_WORLD // Lock to one specific world folder; never changes
};


enum MC_Version {
#define X(e, s) e,
    VERSION_LIST
#undef X
    MC_VERSION_COUNT, // Automatically corresponds to the number of versions
    MC_VERSION_UNKNOWN // For error handling
};


extern const char *VERSION_STRINGS[];
extern const int VERSION_STRINGS_COUNT;

#ifdef __cplusplus
}
#endif

#endif //DATA_STRUCTURES_H
