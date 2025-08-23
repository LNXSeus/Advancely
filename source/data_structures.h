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
    /* Era 3: Modern Advancements/Stats (separate per-world JSONs), minecraft:play_one_minute is in ticks */ \
    X(MC_VERSION_1_12, "1.12") \
    X(MC_VERSION_1_12_1, "1.12.1") \
    X(MC_VERSION_1_12_2, "1.12.2") \
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
    X(MC_VERSION_1_21_9, "1.21.9")

#ifdef __cplusplus
extern "C" {
#endif


// If MAX_PATH_LENGTH is not defined, define it here
#ifndef MAX_PATH_LENGTH
#define MAX_PATH_LENGTH 1024
#endif

typedef struct {
    int frame_count;          // How many frames are in the animation
    SDL_Texture **frames;     // An array of textures, one for each frame
    int *delays;              // An array of delays (in ms) for each frame
    Uint32 total_duration;    // The sum of all delays, for looping
} AnimatedTexture;

// A generic struct for a sub-item, like and advancement's criterion or a stat
struct TrackableItem {
    char root_name[192]; // The unique ID, e.g., "minecraft:husbandry/balanced_diet"
    char display_name[192]; // The user-facing name, e.g., "A Balanced Diet"
    char icon_path[256]; // Relative path to the icon, e.g., "items/apple.png"
    SDL_Texture *texture; // The loaded texture for the icon.
    AnimatedTexture *anim_texture; // To support .gif files

    // Pre-parsed keys for modern stat lookups
    char stat_category_key[192]; // e.g., "minecraft:custom"
    char stat_item_key[192]; // e.g., "minecraft:jump"

    bool done; // For advancements/unlocks: Is it completed?
    int progress; // For stats: The current value, e.g., 5.
    int goal; // For stats: The target value, e.g., 40.

    // For legacy stat snapshotting
    int initial_progress;

    // Flag to allow "conflicting" criteria to overlay parent advancements icon (e.g., hoglin), init with false, cause of calloc
    bool is_shared;
    bool is_manually_completed; // Allow manually overriding sub-stats (NOT FOR ACHIEVEMENTS/ADVANCEMENTS)

    // Animation State
    float alpha;                 // Current transparency (1.0f = opaque, 0.0f = transparent)
    bool is_visible_on_overlay;  // Tracks if the item should be rendered
    float fade_timer;            // Timer for the fade-out animation
};


// A struct to hold a category of trackable items (e.g., all Advancements).
// This can be used for Advancements that have sub-criteria.
struct TrackableCategory {
    char root_name[192];
    char display_name[192];
    char icon_path[256];
    SDL_Texture *texture; // Main icon texture for category/advancement
    AnimatedTexture *anim_texture; // To support .gif files

    // If stat category has no "criteria": {} it's single stat.
    // If one criteria is defined it's still treated as a multi-stat in terms of rendering.
    bool is_single_stat_category;

    SDL_Texture *texture_bg;
    SDL_Texture *texture_bg_half_done;
    SDL_Texture *texture_bg_done;

    bool done;
    bool is_manually_completed; // For manually overriding stats (as they have criteria now with sub-stats)
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
    int completed_criteria_count; // Track completed criteria for that advancement
    TrackableItem **criteria; // An array of sub-items

    // Animation State
    float alpha; // Current transparency (1.0f = opaque, 0.0f = transparent)
    bool is_visible_on_overlay; // Tracks if the category should be rendered
    float fade_timer; // Timer for the fade-out animation
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
    AnimatedTexture *anim_texture; // To support .gif files

    int current_stage; // Index of the currently active sub-goal
    int stage_count; // How many stages there are
    SubGoal **stages; // An array of the sub-goals

    // Animation State
    float alpha; // Current transparency (1.0f = opaque, 0.0f = transparent)
    bool is_visible_on_overlay; // Tracks if the item should be rendered
    float fade_timer; // Timer for the fade-out animation
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

    // TODO: Remove this later
    // cJSON *lang_json; // The loaded language file for display names
};

// PATHMODE AND VERSION STUFF

/**
 * @brief Enum to determine how the saves path is obtained.
 */
enum PathMode {
    PATH_MODE_AUTO, // Automatically detect the path from standard locations.
    PATH_MODE_MANUAL // Use a user-provided path.
};


enum  MC_Version {
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
