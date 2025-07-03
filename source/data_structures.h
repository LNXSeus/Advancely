//
// Created by Linus on 30.06.2025.
//

// A core header file for the data structures used in tracker.h and other files

#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H

#include <stdbool.h>
#include <SDL3/SDL.h>

#include <cJSON.h>

// A generic struct for a sub-item, like and advancement's criterion or a stat
typedef struct TrackableItem {
    char root_name[192];    // The unique ID, e.g., "minecraft:husbandry/balanced_diet"
    char display_name[192]; // The user-facing name, e.g., "A Balanced Diet"
    char icon_path[256];    // Relative path to the icon, e.g., "items/apple.png"
    SDL_Texture *texture;   // The loaded texture for the icon.

    bool done;              // For advancements/unlocks: Is it completed?
    int progress;           // For stats: The current value, e.g., 5.
    int goal;               // For stats: The target value, e.g., 40.
} TrackableItem;


// A struct to hold a category of trackable items (e.g., all Advancements).
// This can be used for Advancements that have sub-criteria.
typedef struct TrackableCategory {
    char root_name[192];
    char display_name[192];
    char icon_path[256];
    SDL_Texture *texture;

    bool done;
    int progress;
    int goal;

    int criteria_count;
    int completed_criteria_count; // Track completed criteria for that advancement
    TrackableItem **criteria; // An array of sub-items
} TrackableCategory;


// --------- MULTI-STAGE LONG-TERM GOALS ---------

typedef enum {
    SUBGOAL_STAT,
    SUBGOAL_ADVANCEMENT,

    // For goals with no automatic trigger, used for final stages (displays once all previous stages are done)
    // TODO: Make subgoal_manual stages clickable in the UI, similar to custom_goals, NOT THAT IMPORTANT
    SUBGOAL_MANUAL // When it's not "stat" or "advancement"
} SubGoalType;

// Represents one step in a multi-stage goal
typedef struct SubGoal {
    char display_text[192];    // e.g., "Awaiting thunder"
    SubGoalType type;          // What kind of trigger to check for
    char root_name[192];       // The target, e.g., "minecraft:trident" or "minecraft:adventure/very_very_frightening"
    int required_progress;     // The value to reach, e.g., 1
    int current_stat_progress; // Current value of stat within multi-stage goal
} SubGoal;

// Represents a complete multi-stage goal
typedef struct MultiStageGoal {
    char display_name[192]; // The overall name, e.g., "Thunder advancements"
    char icon_path[256];    // The icon for the entire goal
    SDL_Texture *texture;   // The loaded icon texture

    int current_stage;      // Index of the currently active sub-goal
    int stage_count;        // How many stages there are
    SubGoal **stages;       // An array of the sub-goals
} MultiStageGoal;

// The main container for all data loaded from the template files.
typedef struct TemplateData {
    int advancement_count;
    int advancements_completed_count;
    TrackableCategory **advancements;

    int stat_count;
    TrackableItem **stats;

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
    float overall_progress_percentage; // Percentage score of everything BUT ADVANCEMENTS (have their own advancements_completed_count)

    cJSON *lang_json; // The loaded language file for display names
} TemplateData;

#endif //DATA_STRUCTURES_H
