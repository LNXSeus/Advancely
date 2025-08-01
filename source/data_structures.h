//
// Created by Linus on 30.06.2025.
//

// A core header file for the data structures used in tracker.h and other files

#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H

#include <SDL3/SDL.h>

extern "C" {
#include <cJSON.h>
}

// If MAX_PATH_LENGTH is not defined, define it here
#ifndef MAX_PATH_LENGTH
#define MAX_PATH_LENGTH 1024
#endif

// A generic struct for a sub-item, like and advancement's criterion or a stat
struct TrackableItem {
    char root_name[192]; // The unique ID, e.g., "minecraft:husbandry/balanced_diet"
    char display_name[192]; // The user-facing name, e.g., "A Balanced Diet"
    char icon_path[256]; // Relative path to the icon, e.g., "items/apple.png"
    SDL_Texture *texture; // The loaded texture for the icon.

    bool done; // For advancements/unlocks: Is it completed?
    int progress; // For stats: The current value, e.g., 5.
    int goal; // For stats: The target value, e.g., 40.

    // For legacy stat snapshotting
    int initial_progress;

    // Flag to allow "conflicting" criteria to overlay parent advancements icon (e.g., hoglin), init with false, cause of calloc
    bool is_shared;
    bool is_manually_completed; // Allow manually overriding sub-stats (NOT FOR ACHIEVEMENTS/ADVANCEMENTS)
};


// A struct to hold a category of trackable items (e.g., all Advancements).
// This can be used for Advancements that have sub-criteria.
struct TrackableCategory {
    char root_name[192];
    char display_name[192];
    char icon_path[256];
    SDL_Texture *texture;

    bool done;
    bool is_manually_completed; // For manually overriding stats (as they have criteria now with sub-stats)
    bool done_in_snapshot; // For legacy stat snapshotting (for achievements)
    int progress;
    int goal;

    int criteria_count;
    int completed_criteria_count; // Track completed criteria for that advancement
    TrackableItem **criteria; // An array of sub-items
};


// --------- MULTI-STAGE LONG-TERM GOALS ---------

enum SubGoalType{
    SUBGOAL_STAT,
    SUBGOAL_ADVANCEMENT,
    SUBGOAL_UNLOCK, // Allows to also complete a stage based on a specific unlock
    SUBGOAL_CRITERION,
    // Allows to complete a stage based on a specific advancement/achievement criterion, e.g., visit plains biome
    // For goals with no automatic trigger, used for final stages (displays once all previous stages are done)
    SUBGOAL_MANUAL // When it's not "stat" or "advancement"
};

// Represents one step in a multi-stage goal
struct SubGoal {
    char stage_id[64]; // Unique ID for every stage e.g., "0", "1", "final_stage"
    char display_text[192]; // e.g., "Awaiting thunder"
    SubGoalType type; // What kind of trigger to check for
    char parent_advancement[192]; // Used for "criterion" stage of multi-stage goal
    char root_name[192]; // The target, e.g., "minecraft:trident" or "minecraft:adventure/very_very_frightening"
    int required_progress; // The value to reach, e.g., 1
    int current_stat_progress; // Current value of stat within multi-stage goal
};

// Represents a complete multi-stage goal
struct MultiStageGoal {
    char root_name[192]; // Unique ID for every multi-stage goal e.g., "ms_goal:getting_started"
    char display_name[192]; // The overall name, e.g., "Thunder advancements"
    char icon_path[256]; // The icon for the entire goal
    SDL_Texture *texture; // The loaded icon texture

    int current_stage; // Index of the currently active sub-goal
    int stage_count; // How many stages there are
    SubGoal **stages; // An array of the sub-goals
};

// The main container for all data loaded from the template files.
struct TemplateData {
    int advancement_count;
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

    // Overall Progress Metrics
    int total_criteria_count;
    int completed_criteria_count;
    float overall_progress_percentage;
    // Percentage score of everything BUT ADVANCEMENTS (have their own advancements_completed_count)

    long long play_time_ticks; // Store the player's total playtime in ticks

    // Taking snapshot for legacy versions when world is changed to track changes per world
    long long playtime_snapshot; // Stores playtime at world load for legacy versions
    char snapshot_world_name[MAX_PATH_LENGTH]; // The world the current snapshot belongs to

    cJSON *lang_json; // The loaded language file for display names
};

// PATHMODE AND VERSION STUFF

/**
 * @brief Enum to determine how the saves path is obtained.
 */
enum PathMode {
    PATH_MODE_AUTO, // Automatically detect the path from standard locations.
    PATH_MODE_MANUAL // Use a user-provided path.
};

// TODO: Define versions here and in VersionMapEntry in settings_utils.c
enum  MC_Version {
    // Puts vaLue starting at 0, allows for comparisons
    // Era 1: Legacy Stats (.dat file), counts playtime in Ticks ID: 1100
    // TODO: (103 versions + unknown as of 2025-08-01)
    MC_VERSION_1_0,
    MC_VERSION_1_1,
    MC_VERSION_1_2_1,
    MC_VERSION_1_2_2,
    MC_VERSION_1_2_3,
    MC_VERSION_1_2_4,
    MC_VERSION_1_2_5,
    MC_VERSION_1_3_1,
    MC_VERSION_1_3_2,
    MC_VERSION_1_4_2,
    MC_VERSION_1_4_4,
    MC_VERSION_1_4_5,
    MC_VERSION_1_4_6,
    MC_VERSION_1_4_7,
    MC_VERSION_1_5_1,
    MC_VERSION_1_5_2,
    MC_VERSION_1_6_1,
    MC_VERSION_1_6_2,
    MC_VERSION_1_6_4,
    // Era 2: Mid-era Achievements/Stats (per-world JSON), stat.playOneMinute is in ticks
    MC_VERSION_1_7_2,
    MC_VERSION_1_7_3,
    MC_VERSION_1_7_4,
    MC_VERSION_1_7_5,
    MC_VERSION_1_7_6,
    MC_VERSION_1_7_7,
    MC_VERSION_1_7_8,
    MC_VERSION_1_7_9,
    MC_VERSION_1_7_10,
    MC_VERSION_1_8,
    MC_VERSION_1_8_1,
    MC_VERSION_1_8_2,
    MC_VERSION_1_8_3,
    MC_VERSION_15W14A, // 2015 APRIL FOOLS VERSION, SNAPSHOT is a fork of 1.8.3, The Love and Hugs Update
    MC_VERSION_1_8_4,
    MC_VERSION_1_8_5,
    MC_VERSION_1_8_6,
    MC_VERSION_1_8_7,
    MC_VERSION_1_8_8,
    MC_VERSION_1_8_9,
    MC_VERSION_1_9,
    MC_VERSION_1_9_1,
    MC_VERSION_1_9_2,
    MC_VERSION_1_RV_PRE1, // 2016 APRIL FOOLS VERSION, Trendy Update
    MC_VERSION_1_9_3,
    MC_VERSION_1_9_4,
    MC_VERSION_1_10,
    MC_VERSION_1_10_1,
    MC_VERSION_1_10_2,
    MC_VERSION_1_11,
    MC_VERSION_1_11_1,
    MC_VERSION_1_11_2,
    // Era 3: Modern Advancements/Stats (separate per-world JSONs), minecraft:play_one_minute is in ticks
    MC_VERSION_1_12,
    MC_VERSION_1_12_1,
    MC_VERSION_1_12_2,
    MC_VERSION_1_13,
    MC_VERSION_1_13_1,
    MC_VERSION_1_13_2,
    MC_VERSION_3D_SHAREWARE_V1_34, // 2019 APRIL FOOLS VERSION, MineCraft [sic] 3D: Memory Block Edition
    MC_VERSION_1_14,
    MC_VERSION_1_14_1,
    MC_VERSION_1_14_2,
    MC_VERSION_1_14_3,
    MC_VERSION_1_14_4,
    MC_VERSION_1_15,
    MC_VERSION_1_15_1,
    MC_VERSION_1_15_2,
    MC_VERSION_20W14INFINITE, // 2020 APRIL FOOLS VERSION, Java Edition 20w14∞, Infinity Snapshot
    MC_VERSION_1_16,
    MC_VERSION_1_16_1,
    MC_VERSION_1_16_2,
    MC_VERSION_1_16_3,
    MC_VERSION_1_16_4,
    MC_VERSION_1_16_5,
    // minecraft:play_one_minute FINALLY renamed to minecraft:play_time
    MC_VERSION_1_17,
    MC_VERSION_1_17_1,
    MC_VERSION_1_18,
    MC_VERSION_1_18_1,
    MC_VERSION_1_18_2,
    MC_VERSION_22W13ONEBLOCKATATIME, // 2022 APRIL FOOLS VERSION, One Block at a Time Update
    MC_VERSION_1_19,
    MC_VERSION_1_19_1,
    MC_VERSION_1_19_2,
    MC_VERSION_1_19_3,
    MC_VERSION_1_19_4,
    MC_VERSION_23W13A_OR_B, // 2023 APRIL FOOLS VERSION, The Vote Update
    MC_VERSION_1_20,
    MC_VERSION_1_20_1,
    MC_VERSION_1_20_2,
    MC_VERSION_1_20_3,
    MC_VERSION_1_20_4,
    MC_VERSION_24W14POTATO, // 2024 APRIL FOOLS VERSION, Poisonous Potato Update
    MC_VERSION_1_20_5,
    MC_VERSION_1_20_6,
    MC_VERSION_1_21,
    MC_VERSION_1_21_1,
    MC_VERSION_1_21_2,
    MC_VERSION_1_21_3,
    MC_VERSION_1_21_4,
    MC_VERSION_1_21_5,
    MC_VERSION_25W14CRAFTMINE, // 2025 APRIL FOOLS VERSION, Craftmine Update
    MC_VERSION_1_21_6,
    MC_VERSION_1_21_7,
    MC_VERSION_1_21_8,
    MC_VERSION_UNKNOWN // For error handling
};

#endif //DATA_STRUCTURES_H
