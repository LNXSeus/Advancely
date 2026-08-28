// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 27.06.2025.
//

#ifndef SETTINGS_UTILS_H
#define SETTINGS_UTILS_H

#include "path_utils.h" // For MAX_PATH_LENGTH
#include "data_structures.h" // For MC_Version and PathMode enums
#include <SDL3/SDL_stdinc.h> // Add this for Uint32


#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAVE_CONTEXT_ALL, // Saving everything from "Settings Apply" butotn
    SAVE_CONTEXT_TRACKER_GEOM, // Save only tracker window's position/size
    SAVE_CONTEXT_OVERLAY_GEOM // Save only overlay window's position/size
} SettingsSaveContext;

enum GoalHidingMode {
    HIDE_ALL_COMPLETED, // Hides both "done" items and hidden within template
    HIDE_ONLY_TEMPLATE_HIDDEN, // Hides only hidden within template, but shows completed
    SHOW_ALL, // Shows hidden and completed
    SHOW_ONLY_INCOMPLETE // Hides only completed items, every incomplete item shows (even hidden ones)
};

// Co-op networking mode
enum NetworkMode {
    NETWORK_SINGLEPLAYER, // Default: no networking, local tracking only
    NETWORK_HOST, // Host: reads game files, broadcasts co-op state
    NETWORK_RECEIVER // Receiver: connects to host, receives co-op state
};

// Co-op transport: where do host/receiver actually connect?
// RELAY (default) routes through the Advancely relay server. DIRECT means a
// raw TCP socket on the LAN/VPN like the original implementation.
enum CoopTransport {
    COOP_TRANSPORT_RELAY,
    COOP_TRANSPORT_DIRECT
};

// Co-op merge settings (per goal type)
enum CoopStatMerge {
    COOP_STAT_HIGHEST, // Use whichever player has the highest stat value
    COOP_STAT_CUMULATIVE // Sum stat values across all players
};

enum CoopStatCheckbox {
    COOP_STAT_CHECKBOX_HOST_ONLY, // Only host can check off stats
    COOP_STAT_CHECKBOX_ANY_PLAYER // Any player can check off stats
};

enum CoopCustomGoalMode {
    COOP_CUSTOM_HOST_ONLY, // Only host modifies custom goals
    COOP_CUSTOM_ANY_PLAYER // Any player can modify custom goals
};

// Corner placement for the main-goal contributor face (advancements, simple
// stats, custom-goal counter contributors). Top-left is intentionally absent
// because the manual-completion checkbox already lives there for stats and
// custom goals.
enum CoopFaceCorner {
    COOP_FACE_CORNER_TOP_RIGHT,
    COOP_FACE_CORNER_BOTTOM_LEFT,
    COOP_FACE_CORNER_BOTTOM_RIGHT // Default
};

// Account type: online (Mojang API lookup) vs offline (manual UUID entry)
enum AccountType {
    ACCOUNT_ONLINE, // UUID fetched from Mojang API
    ACCOUNT_OFFLINE // UUID entered manually (offline/cracked accounts)
};

// Enum to identify the tracker sections
enum TrackerSection {
    SECTION_COUNTERS, // Counter goals (completion counters) - above other sections by default
    SECTION_ADVANCEMENTS,
    SECTION_RECIPES, // Modern advancements with is_recipe flag set to true in template
    SECTION_UNLOCKS, // Exclusive to 25w14craftmine
    SECTION_STATS,
    SECTION_CUSTOM,
    SECTION_MULTISTAGE,
    SECTION_COUNT // Currently 7
};

// Helper array of names for the settings UI
extern const char *TRACKER_SECTION_NAMES[SECTION_COUNT];

#include <cJSON.h>

#define MAX_WORLD_NOTES 32 // Limit for amount of per-world notes until they delete itself
#define MAX_HOTKEYS 32 // Limit for amount of hotkeys
#define MAX_COOP_PLAYERS 32 // Maximum number of players in a co-op session

// DEFAULT values
#define DEFAULT_HOTKEY_IS_GLOBAL false // Hotkeys are window-focused unless explicitly made global
#define DEFAULT_ENABLE_OVERLAY false // Stream overlay will be off by default
#define DEFAULT_USING_STATS_PER_WORLD_LEGACY true
#define DEFAULT_USING_HERMES true
#define DEFAULT_PATH_MODE PATH_MODE_INSTANCE
#define DEFAULT_FIXED_WORLD_PATH ""
#define DEFAULT_FPS 60
#define DEFAULT_OVERLAY_FPS 60
#define DEFAULT_TRACKER_ALWAYS_ON_TOP false
#define DEFAULT_OVERLAY_SCROLL_SPEED 1.0f
#define DEFAULT_OVERLAY_RENDER_MODE OVERLAY_RENDER_MODE_BELT // Classic scrolling belt by default
#define DEFAULT_OVERLAY_PAGE_INTERVAL 8.0f // Seconds each static page is shown before flipping (Page mode)
#define DEFAULT_OVERLAY_PAGE_ALIGN OVERLAY_PROGRESS_TEXT_ALIGN_LEFT // Partial (not-full) pages left-align by default
#define DEFAULT_OVERLAY_ROW_CUSTOM_SCROLL_SPEED_ENABLED false // Per-row custom scroll speed off by default
#define DEFAULT_OVERLAY_ROW_FREEZE_ENABLED true // Per-row freeze-when-items-fit on by default
#define DEFAULT_OVERLAY_ROW_FREEZE_ALIGN OVERLAY_PROGRESS_TEXT_ALIGN_LEFT // Frozen items left-aligned by default
#define DEFAULT_GOAL_HIDING_MODE HIDE_ONLY_TEMPLATE_HIDDEN
#define DEFAULT_INVERT_HIDING_MODE false
#define DEFAULT_OVERLAY_TRANSPARENT false // Color keying stays the default, transparency is opt-in
#define DEFAULT_PRINT_DEBUG_STATUS false

// Overlay Settings
#define DEFAULT_OVERLAY_PROGRESS_TEXT_ALIGN OVERLAY_PROGRESS_TEXT_ALIGN_LEFT
#define DEFAULT_OVERLAY_ROW1_SPACING 8.0f // Default spacing in pixels between row 1 icons
// Compact-mode row-1 icon strip size. Defaults to the same size as the pop-out stack icons
// (DEFAULT_COMPACT_POP_ICON_SIZE) so the strip and the stack match out of the box.
#define DEFAULT_COMPACT_ROW1_ICON_SIZE DEFAULT_COMPACT_POP_ICON_SIZE
#define COMPACT_ROW1_ICON_SIZE_MIN 8.0f
#define COMPACT_ROW1_ICON_SIZE_MAX 96.0f
#define DEFAULT_OVERLAY_ROW1_SHARED_ICON_SIZE 32.0f // Default shared icon size in pixels for row 1
#define DEFAULT_OVERLAY_ROW2_CUSTOM_SPACING_ENABLED false
#define DEFAULT_OVERLAY_ROW2_CUSTOM_SPACING 192.0f // 96px icon + more (took spacing from 1.16 AA template)
#define DEFAULT_OVERLAY_ROW3_CUSTOM_SPACING_ENABLED false
#define DEFAULT_OVERLAY_ROW3_CUSTOM_SPACING 256.0f // 96px icon + more (took spacing from 1.16 AA template)
#define DEFAULT_OVERLAY_ROW3_REMOVE_COMPLETED false
#define DEFAULT_OVERLAY_SHOW_HIDDEN_GOALS false // If true, goals marked hidden in the template still show in the overlay
#define DEFAULT_IGT_FREEZE_ON_COMPLETION true // If true, the IGT freezes at the final time once the run is completed

#define DEFAULT_OVERLAY_STAT_CYCLE_SPEED 3.0f // Default: cycle every 3 seconds
#define DEFAULT_OVERLAY_CLEAR_ANIMATION (-1.2f) // Seconds for the clear (crop) animation; 0 = instant, sign = direction
#define DEFAULT_OVERLAY_CLEAR_FADE_ENABLED false // Items only crop away unless the fade is turned on
#define DEFAULT_OVERLAY_CLEAR_FADE_TIME DEFAULT_COMPACT_STACK_FADE_TIME // Seconds a leaving item takes to fade out

// Overlay custom vertical spacing. Each value is added on top of the default,
// font-driven layout, so the defaults are 0 and reproduce the stock spacing exactly.
// Larger values push the rows below apart and grow the window height to match.
#define DEFAULT_OVERLAY_CUSTOM_VERTICAL_SPACING_ENABLED false
#define DEFAULT_OVERLAY_GAP_TOP_TO_ROW1 0.0f
#define DEFAULT_OVERLAY_GAP_ROW1_TO_ROW2 0.0f
#define DEFAULT_OVERLAY_GAP_ROW2_TO_ROW3 0.0f
#define DEFAULT_OVERLAY_GAP_ROW3_TO_BOTTOM 0.0f
#define OVERLAY_GAP_MIN 0.0f
#define OVERLAY_GAP_MAX 2000.0f

// Overlay Compact Mode Settings
#define DEFAULT_COMPACT_PANEL_INSET 2 // 9-slice source-pixel border on each edge of the default 5x5 panel
#define DEFAULT_COMPACT_PANEL_PIXEL_SCALE 4 // On-screen px per source px, matching the 24x24 -> 96px backgrounds
#define DEFAULT_COMPACT_PANEL_PADDING 12.0f // On-screen px between the panel text and its border
#define DEFAULT_COMPACT_PANEL_ALIGN OVERLAY_PROGRESS_TEXT_ALIGN_LEFT // Panel alignment within the overlay window
#define DEFAULT_COMPACT_CYCLE_INTERVAL 3.0f // Seconds each selected entry shows before the cycle advances
#define COMPACT_CYCLE_INTERVAL_MIN 0.5f
#define COMPACT_CYCLE_INTERVAL_MAX 60.0f

// Compact row-1 icon strip (first-row icons shown above the panel, paged like Page mode).
#define DEFAULT_COMPACT_SHOW_ROW1_ICONS false // Off by default so existing compact layouts are unchanged
#define DEFAULT_COMPACT_ICON_CYCLE_INTERVAL 2.0f // Seconds each page of icons shows before flipping to the next
#define COMPACT_ICON_CYCLE_INTERVAL_MIN 0.5f
#define COMPACT_ICON_CYCLE_INTERVAL_MAX 60.0f
#define DEFAULT_COMPACT_ICON_ROW_GAP 6.0f // On-screen px between the icon strip and the panel below it
#define COMPACT_ICON_ROW_GAP_MIN 0.0f
#define COMPACT_ICON_ROW_GAP_MAX 512.0f
#define DEFAULT_COMPACT_ICON_SHARED_SIZE 24.0f // Shared-parent overlay icon size on a strip icon (matches the stack default)

#define COMPACT_ICON_SHARED_SIZE_MIN 0.0f // Upper bound is the strip icon it is drawn on (compact_row1_icon_size)
#define DEFAULT_COMPACT_ROW1_SPACING 8.0f // Horizontal px between icons in the compact-mode row-1 strip
#define DEFAULT_COMPACT_ROW1_CLEAR_ANIMATION (-1.2f) // Seconds for the compact row-1 icon clear (crop) animation; 0 = instant, sign = direction
#define DEFAULT_COMPACT_ROW1_FADE_ENABLED false // Strip icons only crop away unless the fade is turned on
#define DEFAULT_COMPACT_ROW1_FADE_TIME DEFAULT_COMPACT_STACK_FADE_TIME // Seconds a leaving strip icon takes to fade out


// Compact pop-out stack (goals slide out from under the panel as they progress/complete).
#define DEFAULT_COMPACT_STACK_ROW_GAP 8.0f // On-screen px between the panel and the pop-out stack below it
#define COMPACT_STACK_ROW_GAP_MIN 0.0f
#define COMPACT_STACK_ROW_GAP_MAX 512.0f
#define DEFAULT_COMPACT_STACK_MAX_LINES 6 // Line budget below the panel; a 2-line group uses 2 lines
#define COMPACT_STACK_MAX_LINES_MIN 1
#define COMPACT_STACK_MAX_LINES_MAX 64
#define DEFAULT_COMPACT_STACK_HOLD_TIME 60.0f // Seconds a pop-out holds before it leaves the stack
#define COMPACT_STACK_HOLD_TIME_MIN 0.5f
#define COMPACT_STACK_HOLD_TIME_MAX 900.0f
#define DEFAULT_COMPACT_STACK_RISE_TIME 0.25f // Seconds a pop-out takes to slide into place
#define COMPACT_STACK_RISE_TIME_MIN 0.0f
#define COMPACT_STACK_RISE_TIME_MAX 5.0f
#define DEFAULT_COMPACT_STACK_FADE_ENABLED false // Pop-outs disappear instantly unless the fade is turned on
#define DEFAULT_COMPACT_STACK_FADE_TIME 1.2f // Seconds a leaving pop-out takes to fade out
#define COMPACT_STACK_FADE_TIME_MIN 0.1f
#define COMPACT_STACK_FADE_TIME_MAX 15.0f
#define DEFAULT_COMPACT_POP_ICON_SIZE 36.0f // Pop-out line icon size
#define COMPACT_POP_ICON_SIZE_MIN 8.0f
#define COMPACT_POP_ICON_SIZE_MAX 96.0f
#define DEFAULT_COMPACT_STACK_SHARED_ICON_SIZE 24.0f // Shared-parent overlay icon size on a pop-out line
#define COMPACT_STACK_SHARED_ICON_SIZE_MIN 0.0f // Upper bound is the pop icon it is drawn on (compact_pop_icon_size)
#define DEFAULT_COMPACT_STACK_POP_ON_PROGRESS true // Counting goal types pop on every increment, not just completion
#define DEFAULT_COMPACT_SHOW_COMPLETION_MARKERS true // Show [o]/[a]/[x] completion markers on Compact lines
// Co-op pinned player face on the Compact panel (specific-player/ghost view). Size 0 hides it.
#define DEFAULT_COMPACT_COOP_PANEL_FACE_SIZE 42.0f
#define COMPACT_COOP_PANEL_FACE_SIZE_MIN 0.0f
#define COMPACT_COOP_PANEL_FACE_SIZE_MAX 256.0f
#define DEFAULT_COMPACT_COOP_PANEL_FACE_OFFSET_X (-10.0f) // Inset of the face's bottom-right from the panel's
#define DEFAULT_COMPACT_COOP_PANEL_FACE_OFFSET_Y (-10.0f)
#define COMPACT_COOP_PANEL_FACE_OFFSET_MIN (-256.0f) // Negative overhangs the panel edge
#define COMPACT_COOP_PANEL_FACE_OFFSET_MAX 256.0f

// Tracker Section Item Width
#define DEFAULT_TRACKER_VERTICAL_SPACING 8.0f // Default vertical spacing in pixels between goals globally
#define DEFAULT_TRACKER_SECTION_CUSTOM_WIDTH_ENABLED false // Default for *each* section's checkbox
#define DEFAULT_TRACKER_SECTION_ITEM_WIDTH 128.0f // Default item width in pixels

#define DEFAULT_SCROLLABLE_LIST_THRESHOLD 16 // Items before scrolling kicks in
#define DEFAULT_TRACKER_LIST_SCROLL_SPEED 36.0f // Default pixels per scroll step

#define DEFAULT_NOTES_USE_ROBOTO false // Default: use the standard Minecraft font for notes otherwise roboto
#define DEFAULT_PER_WORLD_NOTES true // When true the notes are per world, otherwise per template
#define DEFAULT_CHECK_FOR_UPDATES true
#define DEFAULT_SHOW_WELCOME_ON_STARTUP true
#define DEFAULT_LAUNCH_COUNT 0
#define DEFAULT_SUPPORT_PROMPT_SHOWN false
#define SUPPORT_PROMPT_LAUNCH_THRESHOLD 10 // Launches before the one-time support ask appears

// Account Defaults
#define DEFAULT_ACCOUNT_TYPE ACCOUNT_ONLINE

// Co-op Defaults
#define DEFAULT_COOP_ENABLED false
#define DEFAULT_COOP_AUTO_ACCEPT false
#define DEFAULT_COOP_READ_ALL_SAVE_FILES true
#define DEFAULT_NETWORK_MODE NETWORK_SINGLEPLAYER
#define DEFAULT_COOP_TRANSPORT COOP_TRANSPORT_RELAY
#define DEFAULT_COOP_STAT_MERGE COOP_STAT_CUMULATIVE
#define DEFAULT_COOP_STAT_CHECKBOX COOP_STAT_CHECKBOX_ANY_PLAYER
#define DEFAULT_COOP_CUSTOM_GOAL_MODE COOP_CUSTOM_ANY_PLAYER
#define DEFAULT_COOP_SHOW_FACES true
#define DEFAULT_COOP_FACE_CORNER COOP_FACE_CORNER_BOTTOM_RIGHT
#define DEFAULT_COOP_FACE_SIZE 28.0f
#define DEFAULT_COOP_FACE_LOD_THRESHOLD 0.25f
#define DEFAULT_HOST_PORT "12345"

// DEFINE DEFAULT SETTINGS
#define DEFAULT_VERSION "1.16.1"  // Also needs to be changed in settings_load()
#define DEFAULT_CATEGORY "all_advancements" // Also needs to be changed in settings_load()
#define DEFAULT_OPTIONAL_FLAG "_aatool_optimized"  // Also needs to be changed in settings_load()
#define DEFAULT_DISPLAY_CATEGORY "All Advancements"
#define DEFAULT_LOCK_CATEGORY_DISPLAY_NAME false // Fixing Display name when changing templates

// Run Completion Threshold defaults (reset whenever the selected template changes)
#define DEFAULT_COMPLETION_USE_ADV_THRESHOLD false
#define DEFAULT_COMPLETION_ADV_THRESHOLD 1
#define DEFAULT_COMPLETION_USE_PERCENT_THRESHOLD false
#define DEFAULT_COMPLETION_PERCENT_THRESHOLD 100.0f
#define DEFAULT_COMPLETION_THRESHOLD_REQUIRE_BOTH false // false = either target (OR), true = both (AND)

#define DEFAULT_TRACKER_FONT "Minecraft.ttf" // The overlay also uses this font
#define DEFAULT_TRACKER_FONT_SIZE 16.0f
#define DEFAULT_TRACKER_SUB_FONT_SIZE 14.0f // (DEFAULT_TRACKER_FONT_SIZE * 0.875f)
#define DEFAULT_TRACKER_UI_FONT_SIZE 16.0f
#define DEFAULT_OVERLAY_FONT "Minecraft.ttf"
#define DEFAULT_UI_FONT "Roboto-Regular.ttf"
#define DEFAULT_UI_FONT_SIZE 16.0f
#define DEFAULT_ADV_BG_PATH "advancement_background.png"
#define DEFAULT_ADV_BG_HALF_DONE_PATH "advancement_background_half_done.png"
#define DEFAULT_ADV_BG_DONE_PATH "advancement_background_done.png"

// Icon size and position within the 96x96 background texture (tracker + overlay, not compact mode).
#define DEFAULT_ADV_ICON_SIZE 64.0f
#define DEFAULT_ADV_ICON_OFFSET_X 16.0f
#define DEFAULT_ADV_ICON_OFFSET_Y 16.0f
#define ADV_ICON_MIN_SIZE 8.0f
#define ADV_ICON_BG_SIZE 96.0f

// Compact mode 9-slice counter panel texture (inset / pixel scale / padding live in the
// "Overlay Compact Mode Settings" block above).
#define DEFAULT_COMPACT_PANEL_PATH "compact_panel_default.png" // 9-slice panel texture in resources/gui/

// Compact mode fonts: the goal-type label, the big count, and the pop-out stack are each configurable.
#define DEFAULT_COMPACT_LABEL_FONT "Minecraft.ttf" // Goal-type label (regular Minecraft font)
#define DEFAULT_COMPACT_COUNT_FONT "MinecraftBold.otf" // Big progress count (bold Minecraft font)
#define DEFAULT_COMPACT_STACK_FONT "Minecraft.ttf" // Pop-out stack below the panel (regular Minecraft font)
#define DEFAULT_COMPACT_LABEL_FONT_SIZE 24.0f
#define DEFAULT_COMPACT_COUNT_FONT_SIZE 48.0f
#define DEFAULT_COMPACT_STACK_FONT_SIZE 20.0f
#define DEFAULT_COMPACT_PANEL_LINE_GAP 0.0f // Vertical px between the label line and the count line inside the panel.
#define COMPACT_PANEL_LINE_GAP_MIN (-32.0f)
#define COMPACT_PANEL_LINE_GAP_MAX 128.0f
// Co-op contributor face size on a pop-out stack line. Independent of the pop icon size; defaults to
// the stack text size so a face matches the line's text out of the box.
#define DEFAULT_COMPACT_STACK_FACE_SIZE DEFAULT_COMPACT_STACK_FONT_SIZE
#define COMPACT_STACK_FACE_SIZE_MIN 8.0f
#define COMPACT_STACK_FACE_SIZE_MAX 96.0f

#define DEFAULT_OVERLAY_FONT_SIZE 24.0f // Default point size for both overlay text sizes (top bar and rows)
#define OVERLAY_FONT_SIZE_MIN 8.0f
#define OVERLAY_FONT_SIZE_MAX 96.0f

// LOD Defaults
#define DEFAULT_LOD_TEXT_SUB_THRESHOLD 0.25f
#define DEFAULT_LOD_TEXT_MAIN_THRESHOLD 0.25f
#define DEFAULT_LOD_ICON_DETAIL_THRESHOLD 0.25f

// Cursor Reveal Defaults
#define DEFAULT_CHECKBOX_REVEAL_ENABLED false
#define DEFAULT_CHECKBOX_REVEAL_RADIUS 120.0f
#define DEFAULT_TEXT_REVEAL_ENABLED false

// TrackerMap Defaults
#define DEFAULT_TRACKER_VIEW_PAN_X 0.0f
#define DEFAULT_TRACKER_VIEW_PAN_Y 0.0f
#define DEFAULT_TRACKER_VIEW_ZOOM 1.0f
#define DEFAULT_TRACKER_VIEW_LOCKED false
#define DEFAULT_TRACKER_VIEW_CAMERA_LOCKED false
#define DEFAULT_TRACKER_VIEW_LOCKED_WIDTH 0.0f
#define DEFAULT_TRACKER_USE_MANUAL_LAYOUT true

// Default window positions/sizes. -1 means centered or default size.
#define DEFAULT_WINDOW_POS (-1)
#define DEFAULT_WINDOW_SIZE (-1)


struct TemplateData;

// A player in the co-op roster (Host tracks these)
typedef struct {
    char username[64]; // Minecraft username e.g., Notch
    char uuid[48]; // UUID from Mojang API (with hyphens, e.g., "069a79f4-44e9-4726-a5be-fca90e38aaf5")
    char display_name[64]; // Optional custom display name (empty = use username)
} CoopPlayer;

// CoopLobbyPlayer is defined in coop_net.h (used for lobby display)

// A per-advancement owner assignment for the coop "All Players" merged view.
// When an advancement is assigned, only the owner's progress drives it in the
// merged view (instead of the default "player with the most criteria" rule).
typedef struct {
    char advancement_root_name[192]; // e.g. "minecraft:adventure/adventuring_time"
    char owner_uuid[48]; // Roster player UUID that owns this advancement
} CoopAdvAssignment;

#define MAX_COOP_ADV_ASSIGNMENTS 1024

// Platform-neutral modifier bits for hotkey bindings. Stored in settings.json as a plain
// integer so one binding means the same thing on every OS; the label shown in the UI is
// localized per-platform (Alt reads as "Option" on macOS).
// Super/Win/Cmd is deliberately absent: window managers commonly swallow it before the
// application ever sees it, so it cannot be captured reliably.
#define HOTKEY_MOD_NONE 0x00u
#define HOTKEY_MOD_CTRL 0x01u
#define HOTKEY_MOD_SHIFT 0x02u
#define HOTKEY_MOD_ALT 0x04u

typedef struct {
    char target_goal[192];
    char increment_key[32];
    char decrement_key[32];
    char toggle_key[32]; // Only used by custom goals with a target value of 0 (plain checkboxes).
    Uint16 increment_mods; // HOTKEY_MOD_* bitmask held alongside increment_key.
    Uint16 decrement_mods; // HOTKEY_MOD_* bitmask held alongside decrement_key.
    Uint16 toggle_mods; // HOTKEY_MOD_* bitmask held alongside toggle_key.
    bool is_global; // If true, the OS delivers this binding even when Advancely is not focused.
} HotkeyBinding;

// The three keys a binding can carry. A counter uses increment and decrement, a plain custom goal
// uses the toggle, and no goal ever uses both kinds, so they share one binding row.
typedef enum {
    HOTKEY_SLOT_INCREMENT = 0,
    HOTKEY_SLOT_DECREMENT,
    HOTKEY_SLOT_TOGGLE,
    HOTKEY_SLOT_COUNT
} HotkeySlot;

// The stored key name of one slot, always a valid string ("None" when unbound).
const char *hotkey_binding_slot_key(const HotkeyBinding *hb, HotkeySlot slot);

// The modifier mask of one slot.
Uint16 hotkey_binding_slot_mods(const HotkeyBinding *hb, HotkeySlot slot);

// "Increment", "Decrement" or "Toggle", for messages that name the offending slot.
const char *hotkey_slot_name(HotkeySlot slot);

// Writes a human-readable modifier prefix ("Ctrl+Shift+") into buf, or an empty string when
// mods is HOTKEY_MOD_NONE. Returns buf so it can be used inline in snprintf calls.
const char *hotkey_mods_to_prefix(Uint16 mods, char *buf, size_t buf_size);

// Reduces an SDL modifier state to the platform-neutral HOTKEY_MOD_* mask. Left and right variants
// collapse into one bit, and the lock keys (Caps, Num, Scroll) are dropped entirely so a binding
// does not silently stop matching when Num Lock is on.
Uint16 hotkey_mods_from_sdl(SDL_Keymod sdl_mods);

// True for the scancode names "F13" through "F24". These keys are absent from real keyboards
// and are what macro pads and Stream Decks emit, so they are the one safe exception to the
// "a global hotkey must carry a modifier" rule.
bool hotkey_key_is_macro_function_key(const char *key_name);

// True when a key/mods pair collides with a shortcut Advancely handles itself, in which case
// binding it would fire both actions at once. Applies to every binding, global or not.
// Writes an explanation into out_reason when it returns true.
bool hotkey_slot_is_reserved(const char *key_name, Uint16 mods, char *out_reason, size_t reason_size);

// Validates a single key/mods pair for use as a global (OS-registered) hotkey. An unbound ("None")
// slot is always valid. Reserved combinations are rejected here too, so a global-only caller needs
// just this one check. A missing modifier does not fail: see hotkey_global_slot_is_bare().
// Writes a reason into out_reason on failure.
bool hotkey_global_slot_is_valid(const char *key_name, Uint16 mods, char *out_reason, size_t reason_size);

// True when a bound slot would be registered globally without Ctrl or Alt. Such a binding works,
// but it takes the bare key away from every other program and keeps firing while you type, so the
// UI warns about it instead of blocking it. F13-F24 are exempt.
// Writes the warning text into out_warning when it returns true.
bool hotkey_global_slot_is_bare(const char *key_name, Uint16 mods, char *out_warning, size_t warning_size);

// ------------------- ADVANCELY APP HOTKEYS -------------------

// Where an application hotkey is allowed to fire. Two bindings only collide when their contexts
// overlap, so the same key can mean one thing on the normal tracker map and another one while the
// Visual Layout Editor is running.
#define APP_HOTKEY_CTX_TRACKER 0x01u // Tracker map, Visual Layout Editor off.
#define APP_HOTKEY_CTX_VISUAL 0x02u // Tracker map, Visual Layout Editor on.
#define APP_HOTKEY_CTX_EDITOR 0x04u // Template editor window.
#define APP_HOTKEY_CTX_SETTINGS 0x08u // Settings window.
#define APP_HOTKEY_CTX_OVERLAY 0x10u // Overlay window.
#define APP_HOTKEY_CTX_ALL (APP_HOTKEY_CTX_TRACKER | APP_HOTKEY_CTX_VISUAL | APP_HOTKEY_CTX_EDITOR | \
                            APP_HOTKEY_CTX_SETTINGS | APP_HOTKEY_CTX_OVERLAY)

// The settings and template editor windows live inside the tracker window, and a counter hotkey
// fires there as long as no text field has focus. Only visual layout editing switches them off.
#define APP_HOTKEY_CTX_COUNTER_HOTKEYS (APP_HOTKEY_CTX_TRACKER | APP_HOTKEY_CTX_EDITOR | \
                                        APP_HOTKEY_CTX_SETTINGS)

// Headings the Hotkeys tab groups the rows under.
typedef enum {
    APP_HOTKEY_GROUP_TRACKER = 0,
    APP_HOTKEY_GROUP_SETTINGS,
    APP_HOTKEY_GROUP_OVERLAY,
    APP_HOTKEY_GROUP_EDITOR,
    APP_HOTKEY_GROUP_VISUAL,
    APP_HOTKEY_GROUP_COUNT
} AppHotkeyGroup;

extern const char *APP_HOTKEY_GROUP_NAMES[APP_HOTKEY_GROUP_COUNT];

// What the collapsing header of each group explains on hover.
extern const char *APP_HOTKEY_GROUP_TOOLTIPS[APP_HOTKEY_GROUP_COUNT];

// Every configurable Advancely shortcut, in the order the Hotkeys tab lists them.
// Columns: enum id, settings.json id, default key (US-layout scancode name), default modifiers,
// contexts it fires in, group heading, UI label, tooltip description.
// The json id is written to settings.json and must never change once released.
#define APP_HOTKEY_LIST(X) \
    X(APP_HOTKEY_TOGGLE_VISUAL_EDITING, "toggle_visual_editing", "V", HOTKEY_MOD_SHIFT, \
      APP_HOTKEY_CTX_TRACKER | APP_HOTKEY_CTX_VISUAL | APP_HOTKEY_CTX_EDITOR, APP_HOTKEY_GROUP_TRACKER, \
      "Toggle Visual Layout Editor", \
      "Starts or stops visual layout editing, exactly like the button in the template editor.\n" \
      "From anywhere it opens the template editor, puts the template applied in Settings into\n" \
      "\"Edit Template\", and starts editing, as long as a valid Minecraft saves folder is tracked.\n" \
      "An editing session that is already open is never interrupted, so with unsaved changes or a\n" \
      "different template open the editor just comes to the front with the button greyed out,\n" \
      "explaining what is missing.\n" \
      "Does nothing during a Co-op session, where template editing is disabled.") \
    X(APP_HOTKEY_TOGGLE_TEMPLATE_EDITOR, "toggle_template_editor", "E", HOTKEY_MOD_SHIFT, \
      APP_HOTKEY_CTX_TRACKER | APP_HOTKEY_CTX_VISUAL | APP_HOTKEY_CTX_EDITOR, APP_HOTKEY_GROUP_TRACKER, \
      "Toggle Template Editor Window", \
      "Opens or closes the template editor window.\n" \
      "Cannot open it during a Co-op session, where template editing is disabled.") \
    X(APP_HOTKEY_TOGGLE_NOTES, "toggle_notes", "N", HOTKEY_MOD_SHIFT, \
      APP_HOTKEY_CTX_TRACKER | APP_HOTKEY_CTX_VISUAL | APP_HOTKEY_CTX_EDITOR, APP_HOTKEY_GROUP_TRACKER, \
      "Toggle Notes Window", \
      "Opens or closes the notes window, exactly like the \"Notes\" checkbox in the control bar.\n" \
      "Whether the window shows per-world or per-template notes stays whatever it was set to\n" \
      "inside the window itself.") \
    X(APP_HOTKEY_LOCK_CAMERA, "lock_camera", "C", HOTKEY_MOD_SHIFT, \
      APP_HOTKEY_CTX_TRACKER | APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_TRACKER, \
      "Lock Camera (Zoom & Pan)", \
      "Flips the \"Lock Camera\" checkbox in the View menu, which makes the map ignore mouse\n" \
      "wheel zooming and right/middle mouse dragging.\n" \
      "The Zoom slider and \"Reset Camera\" keep working while locked.\n" \
      "Opens the View menu so the new state is visible, which fades out again after a few\n" \
      "seconds unless the mouse reaches it.\n" \
      "Only fires while the tracker map itself is focused, not the settings or editor windows.") \
    X(APP_HOTKEY_LOCK_LAYOUT, "lock_layout", "Space", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_TRACKER | APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_TRACKER, \
      "Lock Layout", \
      "Flips the \"Lock Layout\" checkbox in the View menu, freezing the width the automatic\n" \
      "grid wraps at so goals stop rearranging when the window is resized or zoomed.\n" \
      "Opens the View menu so the new state is visible, which fades out again after a few\n" \
      "seconds unless the mouse reaches it.\n" \
      "Only fires while the tracker map itself is focused, not the settings or editor windows.") \
    X(APP_HOTKEY_TOGGLE_MANUAL_LAYOUT, "toggle_manual_layout", "M", HOTKEY_MOD_SHIFT, \
      APP_HOTKEY_CTX_TRACKER | APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_TRACKER, \
      "Toggle Manual Layout", \
      "Flips the \"Manual Layout\" checkbox in the View menu, switching between the procedural\n" \
      "grid and the positions stored in the template's layout file.\n" \
      "Opens the View menu so the new state is visible, which fades out again after a few\n" \
      "seconds unless the mouse reaches it.\n" \
      "Does nothing while the Visual Layout Editor is running, which forces Manual Layout on.\n" \
      "Only fires while the tracker map itself is focused, not the settings or editor windows.") \
    X(APP_HOTKEY_SETTINGS_APPLY, "settings_apply", "S", HOTKEY_MOD_CTRL, \
      APP_HOTKEY_CTX_SETTINGS, APP_HOTKEY_GROUP_SETTINGS, \
      "Apply Settings", \
      "Applies the settings, like the \"Apply Settings\" button.\n" \
      "Pressing Enter with the settings window focused applies as well and is not configurable.") \
    X(APP_HOTKEY_SETTINGS_REVERT, "settings_revert", "Z", HOTKEY_MOD_CTRL, \
      APP_HOTKEY_CTX_SETTINGS, APP_HOTKEY_GROUP_SETTINGS, \
      "Revert Changes", \
      "Puts every setting back to the last applied state, like the \"Revert Changes\" button.") \
    X(APP_HOTKEY_OVERLAY_ADVANCE, "overlay_advance", "Space", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_OVERLAY, APP_HOTKEY_GROUP_OVERLAY, \
      "Advance / Speed Up", \
      "Belt mode: hold to scroll faster.\n" \
      "Page and Compact mode: press to cut to the next page or cycle entry.\n" \
      "In Compact mode it advances the Row 1 icon strip along with the panel.\n" \
      "Only fires while the overlay window itself is focused.") \
    X(APP_HOTKEY_EDITOR_NEXT_GOAL, "editor_next_goal", "Tab", HOTKEY_MOD_CTRL, \
      APP_HOTKEY_CTX_EDITOR, APP_HOTKEY_GROUP_EDITOR, \
      "Select Next Goal in List", \
      "Moves the selection one entry down in the list of the open tab, following the search\n" \
      "filter and stopping at the last entry. Holding repeats.") \
    X(APP_HOTKEY_EDITOR_PREV_GOAL, "editor_prev_goal", "Tab", HOTKEY_MOD_CTRL | HOTKEY_MOD_SHIFT, \
      APP_HOTKEY_CTX_EDITOR, APP_HOTKEY_GROUP_EDITOR, \
      "Select Previous Goal in List", \
      "Moves the selection one entry up in the list of the open tab, following the search\n" \
      "filter and stopping at the first entry. Holding repeats.") \
    X(APP_HOTKEY_EDITOR_SAVE, "editor_save", "S", HOTKEY_MOD_CTRL, \
      APP_HOTKEY_CTX_EDITOR, APP_HOTKEY_GROUP_EDITOR, \
      "Save Template", \
      "Saves the template open in the editor, like the \"Save\" button.\n" \
      "Pressing Enter with the editor focused saves as well and is not configurable.") \
    X(APP_HOTKEY_EDITOR_REVERT, "editor_revert", "Z", HOTKEY_MOD_CTRL, \
      APP_HOTKEY_CTX_EDITOR, APP_HOTKEY_GROUP_EDITOR, \
      "Revert Changes", \
      "Discards the editor's unsaved changes and reloads the last saved state,\n" \
      "like the \"Revert Changes\" button.") \
    X(APP_HOTKEY_NUDGE_LEFT, "nudge_left", "Left", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Nudge Selection Left (1 px)", "Moves every selected element one pixel to the left. Holding repeats.") \
    X(APP_HOTKEY_NUDGE_RIGHT, "nudge_right", "Right", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Nudge Selection Right (1 px)", "Moves every selected element one pixel to the right. Holding repeats.") \
    X(APP_HOTKEY_NUDGE_UP, "nudge_up", "Up", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Nudge Selection Up (1 px)", "Moves every selected element one pixel up. Holding repeats.") \
    X(APP_HOTKEY_NUDGE_DOWN, "nudge_down", "Down", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Nudge Selection Down (1 px)", "Moves every selected element one pixel down. Holding repeats.") \
    X(APP_HOTKEY_MOVE_LEFT, "move_left", "A", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Move Selection Left (10 px)", "Moves every selected element ten pixels to the left. Holding repeats.") \
    X(APP_HOTKEY_MOVE_RIGHT, "move_right", "D", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Move Selection Right (10 px)", "Moves every selected element ten pixels to the right. Holding repeats.") \
    X(APP_HOTKEY_MOVE_UP, "move_up", "W", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Move Selection Up (10 px)", "Moves every selected element ten pixels up. Holding repeats.") \
    X(APP_HOTKEY_MOVE_DOWN, "move_down", "S", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Move Selection Down (10 px)", "Moves every selected element ten pixels down. Holding repeats.") \
    X(APP_HOTKEY_TOGGLE_LAYOUT_HIDDEN, "toggle_layout_hidden", "V", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Toggle Layout Visibility of Selection", \
      "Flips the manual layout \"Hide\" checkbox of every selected element (icon, text and\n" \
      "progress are separate elements).\n" \
      "If any of them is still visible, all of them are hidden; otherwise all are shown again.\n" \
      "The change lands in the template editor, so it needs a save like every other edit.\n" \
      "Layout editing forces \"Show All\", so hidden elements stay on screen while you work.") \
    X(APP_HOTKEY_TOGGLE_GOAL_HIDDEN, "toggle_goal_hidden", "H", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Toggle Overlay Visibility of Selection", \
      "Flips the \"Hidden\" checkbox of every goal in the selection, which controls the overlay\n" \
      "and the automatic layout.\n" \
      "If any of them is still visible, all of them are hidden; otherwise all are shown again.\n" \
      "The change lands in the template editor, so it needs a save like every other edit.") \
    X(APP_HOTKEY_DELETE_SELECTION, "delete_selection", "Delete", HOTKEY_MOD_NONE, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Delete Selection", \
      "Removes every selected goal, criterion, sub-stat and decoration from the template.\n" \
      "Selecting any part of a goal (icon, text or progress) removes the whole goal.\n" \
      "The change lands in the template editor, so the map only catches up once you save,\n" \
      "and \"Revert Changes\" undoes it until then.") \
    X(APP_HOTKEY_COPY_SELECTION, "copy_selection", "C", HOTKEY_MOD_CTRL, \
      APP_HOTKEY_CTX_VISUAL, APP_HOTKEY_GROUP_VISUAL, \
      "Copy Selection", \
      "Duplicates every selected goal, criterion, sub-stat and decoration, layout coordinates\n" \
      "and all, so the copy starts out exactly on top of the original.\n" \
      "Each copy gets the usual \"_copy\" id, counting up when that one is taken.\n" \
      "The copies appear in the template editor and show up on the map once you save.")

typedef enum {
#define X(enum_id, ...) enum_id,
    APP_HOTKEY_LIST(X)
#undef X
    APP_HOTKEY_COUNT
} AppHotkeyAction;

typedef struct {
    const char *json_id;
    const char *default_key;
    Uint16 default_mods;
    Uint16 contexts;
    AppHotkeyGroup group;
    const char *label;
    const char *description;
} AppHotkeyDef;

extern const AppHotkeyDef APP_HOTKEY_DEFS[APP_HOTKEY_COUNT];

// One configured binding. "None" (or an empty string) means the action has no key at all.
// Unlike the counter hotkeys, which store the physical key (a US-layout scancode name), these store
// the key as it is labeled on the user's keyboard. Ctrl+Z stays Ctrl+Z on a QWERTZ layout instead of
// wandering to the key next to it, which is what matters for shortcuts everybody knows by name.
typedef struct {
    char key[32]; // SDL key name, layout-dependent (SDL_GetKeyName / SDL_GetKeyFromName).
    Uint16 mods; // HOTKEY_MOD_* bitmask.
} AppHotkey;

// Data structures for settings
typedef struct {
    int x, y, w, h;
} WindowRect;

typedef struct {
    Uint8 r, g, b, a;
} ColorRGBA;

// Enum for overlay progress text alignment (always fully at the top)
enum OverlayProgressTextAlignment {
    OVERLAY_PROGRESS_TEXT_ALIGN_LEFT,
    OVERLAY_PROGRESS_TEXT_ALIGN_CENTER,
    OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT
};

// How the overlay lays out its item rows. New modes can be appended here; the
// overlay render code and settings UI switch on this value. BELT is the classic
// scrolling conveyor (with optional per-row auto-freeze); PAGE shows a static,
// centered slice of items and flips between slices like the pages of a book.
// COMPACT replaces the 3-row layout entirely with a tall/narrow counter panel
// (Zesskyo-style) and pop-out goals. COMPACT must stay the LAST value: the
// settings loader validates the saved index against this range.
enum OverlayRenderMode {
    OVERLAY_RENDER_MODE_BELT,
    OVERLAY_RENDER_MODE_PAGE,
    OVERLAY_RENDER_MODE_COMPACT
};

// Goal-type categories the Compact-mode counter panel can cycle through. The user selects any
// number of these; each selected-and-present type contributes one "label over count" entry to the
// cycle. These mirror the tracker's section breakdown (criteria and sub-stats are their own
// categories, like the section separators), and the version-correct label/presence rules are
// applied at render time. COMPACT_COUNTER_TYPE_COUNT must stay last: it sizes the selection mask
// and validates saved indices.
enum OverlayCompactCounterType {
    COMPACT_COUNTER_ADVANCEMENTS, // Advancements (>= 1.12) / Achievements (<= 1.11.2), recipes excluded
    COMPACT_COUNTER_RECIPES, // Recipes only (>= 1.12)
    COMPACT_COUNTER_CRITERIA, // Advancement / achievement criteria (non-recipe advancements only)
    COMPACT_COUNTER_RECIPE_CRITERIA, // Recipe criteria (>= 1.12)
    COMPACT_COUNTER_STATS, // Stat categories
    COMPACT_COUNTER_SUB_STATS, // Individual sub-stats (stat criteria)
    COMPACT_COUNTER_UNLOCKS, // Unlocks
    COMPACT_COUNTER_CUSTOM, // Custom goals
    COMPACT_COUNTER_MULTISTAGE, // Multi-stage goals
    COMPACT_COUNTER_COUNTERS, // Completion counters
    COMPACT_COUNTER_TYPE_COUNT
};

// A single individual goal selected to appear in the Compact cycle by its own name (as opposed to
// a whole-section type count). Stored globally by goal ID (root_name), mirroring stat_progress_override
// / custom_progress; the overlay skips any item not present in the current template. `kind` selects
// how the count is computed: CRITERIA = complex advancement (its criteria progress),
// SUB_STATS = complex stat category / multi-stat (its sub-stat progress), CUSTOM = custom goal,
// COUNTERS = completion counter.
typedef struct {
    OverlayCompactCounterType kind;
    char root_name[192];
} CompactCycleItem;

#define MAX_COMPACT_CYCLE_ITEMS 1024 // High enough to be effectively unlimited for any real template

// One whole-section counter category for the Compact panel: a version-correct display label and its
// completed/total. total == 0 means the category is not present in the template.
typedef struct {
    char label[40];
    int completed;
    int total;
} CompactCounter;

// Default colors when it's just {} in settings.json, so no r, g, b, a values
extern const ColorRGBA DEFAULT_TRACKER_BG_COLOR;
extern const ColorRGBA DEFAULT_OVERLAY_BG_COLOR;
extern const ColorRGBA DEFAULT_TEXT_COLOR;
extern const ColorRGBA DEFAULT_OVERLAY_TEXT_COLOR;

// Default UI Colors
extern const ColorRGBA DEFAULT_UI_TEXT_COLOR;
extern const ColorRGBA DEFAULT_UI_WINDOW_BG_COLOR;
extern const ColorRGBA DEFAULT_UI_FRAME_BG_COLOR;
extern const ColorRGBA DEFAULT_UI_FRAME_BG_HOVERED_COLOR;
extern const ColorRGBA DEFAULT_UI_FRAME_BG_ACTIVE_COLOR;
extern const ColorRGBA DEFAULT_UI_TITLE_BG_ACTIVE_COLOR;
extern const ColorRGBA DEFAULT_UI_BUTTON_COLOR;
extern const ColorRGBA DEFAULT_UI_BUTTON_HOVERED_COLOR;
extern const ColorRGBA DEFAULT_UI_BUTTON_ACTIVE_COLOR;
extern const ColorRGBA DEFAULT_UI_HEADER_COLOR;
extern const ColorRGBA DEFAULT_UI_HEADER_HOVERED_COLOR;
extern const ColorRGBA DEFAULT_UI_HEADER_ACTIVE_COLOR;
extern const ColorRGBA DEFAULT_UI_CHECK_MARK_COLOR;

// A Struct to hold all application settings in one place
struct AppSettings {
    // --- Template Configuration ---
    char version_str[64]; // The selected Minecraft version string, e.g., "1.21.6".
    char display_version_str[64]; // The version string to display, e.g., "1.21.10". (same advancements)
    PathMode path_mode; // The mode for finding the saves path (auto or manual).
    char manual_saves_path[MAX_PATH_LENGTH]; // The user-defined path to the saves folder if path_mode is manual.
    char fixed_world_path[MAX_PATH_LENGTH]; // Full path to the fixed world folder for PATH_MODE_FIXED_WORLD
    char category[MAX_PATH_LENGTH]; // The speedrun or goal category, used to build the template file name.
    char optional_flag[MAX_PATH_LENGTH]; // An optional string appended to the template file name for variants.
    char category_display_name[MAX_PATH_LENGTH]; // The user-configurable display name for the category.
    bool lock_category_display_name; // If true, the category display name cannot be changed
    char lang_flag[64]; // Selected language flag (e.g., "eng", "pl") empty for default
    char layout_flag[64]; // Selected manual-layout flag (empty = default _layout file or inline positions)

    // --- Run Completion Threshold (per active template; reset on template change) ---
    // Optional early-completion criteria (e.g. Half%). When neither toggle is enabled the
    // run only completes at full 100% (default behaviour).
    bool completion_use_adv_threshold; // Enable the advancement/achievement count target
    int completion_adv_threshold; // Completed advancements needed (clamped 1..template goal count)
    bool completion_use_percent_threshold; // Enable the overall progress percentage target
    float completion_percent_threshold; // Overall progress percentage needed (0.00..100.00)
    bool completion_threshold_require_both; // true = both targets required (AND), false = either (OR)

    // --- Section Order ---
    int section_order[SECTION_COUNT]; // Stores the display order of the tracker sections.

    // --- Constructed Paths (from above settings) ---
    char template_path[MAX_PATH_LENGTH]; // The final, constructed path to the template .json file.
    char lang_path[MAX_PATH_LENGTH]; // The final, constructed path to the language .json file.
    char layout_path[MAX_PATH_LENGTH]; // The final, constructed path to the manual-layout .json file.
    char notes_path[MAX_PATH_LENGTH]; // The final, constructed path to the notes .txt file.

    // --- Hotkeys ---
    int hotkey_count; // The number of active hotkey bindings.
    HotkeyBinding hotkeys[MAX_HOTKEYS]; // Array of hotkey bindings for custom goals.
    AppHotkey app_hotkeys[APP_HOTKEY_COUNT]; // Advancely's own shortcuts, indexed by AppHotkeyAction.

    // --- General Settings ---
    bool enable_overlay; // If true, the overlay window is created and rendered.
    bool using_stats_per_world_legacy;
    // If true, legacy versions look for per-world .dat files (for StatsPerWorld mod).
    bool using_hermes; // true if Hermes Mod integration is active
    float fps; // The target frames per second for the application loop.
    float overlay_fps; // The target frames per second for the overlay loop.
    bool tracker_always_on_top; // If true, the main tracker window stays above other windows.

    // If false only error messages are printed to console and log file
    // Logic is used in logger.cpp in log_message() function
    bool print_debug_status;
    OverlayRenderMode overlay_render_mode; // How the overlay lays out item rows (scrolling belt vs static pages).
    float overlay_page_interval; // Seconds each static page is shown before flipping (Page mode only).
    OverlayProgressTextAlignment overlay_page_align; // How a not-full page is aligned within the window (Page mode).

    // --- Compact mode (Zesskyo-style counter panel + pop-outs) ---
    char compact_panel_path[MAX_PATH_LENGTH]; // 9-slice panel texture in resources/gui/ (Compact mode).
    int compact_panel_inset_left; // 9-slice source-pixel border widths; these corners stay fixed while the
    int compact_panel_inset_right; // middle stretches to fit the counter text.
    int compact_panel_inset_top;
    int compact_panel_inset_bottom;
    int compact_panel_pixel_scale; // On-screen pixels per source pixel (keeps the border pixel size consistent).
    float compact_panel_padding; // On-screen px between the counter text and the panel border.
    OverlayProgressTextAlignment compact_panel_align; // Panel alignment (left/center/right) within the window.
    char compact_label_font_name[256]; // Font face for the goal-type label (e.g. "Advancements:").
    char compact_count_font_name[256]; // Font face for the big progress count (e.g. "70/80").
    char compact_stack_font_name[256]; // Font face for the pop-out stack text below the panel.
    float compact_label_font_size; // Point size for the goal-type label.
    float compact_count_font_size; // Point size for the big progress count.
    float compact_stack_font_size; // Point size for the pop-out stack text.
    float compact_panel_line_gap; // Vertical px between the label line and the count line inside the panel.
    bool compact_cycle_type[COMPACT_COUNTER_TYPE_COUNT]; // Which whole-section type counts are in the cycle.
    CompactCycleItem compact_cycle_items[MAX_COMPACT_CYCLE_ITEMS]; // Individual goals selected into the cycle by name.
    int compact_cycle_item_count; // Number of valid entries in compact_cycle_items.
    float compact_cycle_interval; // Seconds each selected entry shows before the cycle advances.

    // Row-1 icon strip above the panel: the first-row icons (advancement criteria + sub-stats), paged
    // to fit the panel width and flipped on their own interval. Aligned with compact_panel_align.
    bool compact_show_row1_icons; // Master toggle for the icon strip above the panel.
    float compact_icon_cycle_interval; // Seconds each page of icons shows before flipping to the next.
    float compact_icon_row_gap; // On-screen px between the icon strip and the panel below it.
    float compact_row1_spacing; // Horizontal px between icons in the compact-mode row-1 strip.
    float compact_row1_clear_animation;
    // Seconds for the compact row-1 icon clear (crop) animation; 0 = instant, sign = direction.
    bool compact_row1_fade_enabled; // Fade a leaving strip icon out on top of the crop (needs a transparent overlay).
    float compact_row1_fade_time; // Seconds a leaving strip icon takes to fade out.
    float compact_icon_shared_size; // Shared-parent overlay icon size on a strip icon (capped by the strip icon size).
    // Pop-out stack selection (independent of the panel cycle): which goals may slide out below the
    // panel as they progress or complete. Same additive model as the cycle (type OR individual goal).
    bool compact_stack_type[COMPACT_COUNTER_TYPE_COUNT]; // Which whole-section types may pop into the stack.
    CompactCycleItem compact_stack_items[MAX_COMPACT_CYCLE_ITEMS]; // Individual goals allowed into the stack by name.
    int compact_stack_item_count; // Number of valid entries in compact_stack_items.
    // Per-type pop trigger: true pops the type on every progress increment, false only on completion.
    // Only meaningful for the counting types (stats, sub-stats, custom, multi-stage, counters); the
    // rest have nothing but a done flag and always pop on completion.
    bool compact_stack_pop_on_progress[COMPACT_COUNTER_TYPE_COUNT];
    // Draw the [o]/[a]/[x] completion markers on manually- and auto-completable goals (both the panel's
    // count line and the pop-out stack). Off also stops a bare completion from popping a line, since with
    // no marker the line looks identical before and after; value changes still pop when Pop On Progress is on.
    bool compact_show_completion_markers;
    float compact_stack_row_gap; // On-screen px between the panel and the pop-out stack below it.
    int compact_stack_max_lines; // Line budget for the stack below the panel (a 2-line group uses 2).
    float compact_stack_hold_time; // Seconds a pop-out holds before it leaves the stack.
    float compact_stack_rise_time; // Seconds a pop-out takes to slide into place.
    bool compact_stack_fade_enabled; // Fade leaving pop-outs out instead of removing them instantly.
    float compact_stack_fade_time; // Seconds the fade-out takes. Only used when the fade is enabled.
    float compact_pop_icon_size; // Pop-out line icon size.
    float compact_stack_shared_icon_size; // Shared-parent overlay icon size on a pop-out line.
    float compact_stack_face_size; // Co-op contributor face size on a pop-out line.
    // Co-op: pinned player face for a specific-player/ghost view, drawn at the panel's bottom-right.
    // Size 0 hides it; offsets inset the face's bottom-right corner from the panel's bottom-right.
    float compact_coop_panel_face_size;
    float compact_coop_panel_face_offset_x;
    float compact_coop_panel_face_offset_y;
    float overlay_scroll_speed; // The global speed and direction of the scrolling animation in the overlay.
    bool overlay_row1_custom_scroll_speed_enabled; // If true, row 1 ignores the global speed and uses its own.
    float overlay_row1_scroll_speed; // Custom scroll speed and direction for row 1.
    bool overlay_row2_custom_scroll_speed_enabled; // If true, row 2 ignores the global speed and uses its own.
    float overlay_row2_scroll_speed; // Custom scroll speed and direction for row 2.
    bool overlay_row3_custom_scroll_speed_enabled; // If true, row 3 ignores the global speed and uses its own.
    float overlay_row3_scroll_speed; // Custom scroll speed and direction for row 3.
    bool overlay_row1_freeze_enabled; // If true, row 1 stops scrolling once its visible items fit the window.
    OverlayProgressTextAlignment overlay_row1_freeze_align; // Alignment for row 1's frozen (static) items.
    bool overlay_row2_freeze_enabled; // If true, row 2 stops scrolling once its visible items fit the window.
    OverlayProgressTextAlignment overlay_row2_freeze_align; // Alignment for row 2's frozen (static) items.
    bool overlay_row3_freeze_enabled; // If true, row 3 stops scrolling once its visible items fit the window.
    OverlayProgressTextAlignment overlay_row3_freeze_align; // Alignment for row 3's frozen (static) items.
    GoalHidingMode goal_hiding_mode; // 3 Stages of hiding goals
    bool invert_hiding_mode; // Inverts each hiding mode: hide/fade incomplete instead of completed
    OverlayProgressTextAlignment overlay_progress_text_align; // Alignment for the progress text in the overlay.
    float overlay_row1_spacing; // Horizontal spacing between icons in Row 1.
    float compact_row1_icon_size; // Icon size for the compact-mode row-1 icon strip.
    float overlay_row1_shared_icon_size; // Shared icon size for Row 1
    bool overlay_row2_custom_spacing_enabled; // If true, use custom spacing for row 2
    float overlay_row2_custom_spacing; // The custom spacing value for row 2
    bool overlay_row3_custom_spacing_enabled; // If true, use custom spacing for row 3
    float overlay_row3_custom_spacing; // The custom spacing value for row 3
    bool overlay_row3_remove_completed; // If true, the third row will also hide completed goals as row 2 does.
    bool overlay_show_hidden_goals;
    // If true, goals marked hidden in the template are still shown in the overlay (all modes).
    bool overlay_custom_vertical_spacing_enabled;
    // If true, the overlay row gaps below are added to the default layout.
    float overlay_gap_top_to_row1; // Extra vertical gap between the top info bar and row 1 (pixels).
    float overlay_gap_row1_to_row2; // Extra vertical gap between row 1 and row 2 (pixels).
    float overlay_gap_row2_to_row3; // Extra vertical gap between row 2 and row 3 (pixels).
    float overlay_gap_row3_to_bottom; // Extra vertical gap between row 3 and the bottom window edge (pixels).
    float overlay_stat_cycle_speed; // Time in seconds between cycling sub-stats on the overlay.
    float overlay_clear_animation; // Seconds for the item clear (crop) animation; 0 = instant, sign = direction.
    bool overlay_clear_fade_enabled; // Fade a leaving item out on top of the crop (needs a transparent overlay).
    float overlay_clear_fade_time; // Seconds a leaving item takes to fade out.

    // Level of Detail (LOD)
    float lod_text_sub_threshold; // Zoom level below which sub-item text/progress is hidden
    float lod_text_main_threshold; // Zoom level below which main item text/checkboxes are hidden
    float lod_icon_detail_threshold; // Zoom level below which icons become simple squares
    bool checkbox_reveal_enabled; // If true, manual-completion checkboxes only render near the mouse cursor
    float checkbox_reveal_radius; // Screen-pixel radius around the cursor within which checkboxes appear
    bool text_reveal_enabled; // If true, item text (names/progress) also only renders within the reveal radius

    // --- Custom Tracker Spacing ---
    int scrollable_list_threshold; // Number of items before list becomes scrollable
    float tracker_list_scroll_speed; // Configurable speed
    float tracker_vertical_spacing; // Vertical spacing in pixels between goals globally
    bool tracker_section_custom_width_enabled[SECTION_COUNT]; // An array of bools, one for each section
    float tracker_section_custom_item_width[SECTION_COUNT]; // An array of item widths, one for each section


    bool notes_use_roboto_font; // If true, the notes window uses the Roboto font instead of the default.
    bool per_world_notes; // If true, notes are saved per world instead of per template

    // --- Font Settings --- (require restart)
    char tracker_font_name[256]; // Filename of the font for the trackermap/overlay.
    float tracker_font_size; // Base size for the main trackermap font. Overlay is fixed.
    float tracker_sub_font_size; // Size for the sub-fonts (e.g., criteria) in the trackermap.
    float tracker_ui_font_size; // Size for the info bar and bottom controls.
    char ui_font_name[256]; // Filename of the font for ImGui UI (settings, etc.).
    float ui_font_size; // Size for the UI font, may cause non-destructive overlap of buttons.
    char overlay_font_name[256]; // Filename of the font for the overlay.
    float overlay_progress_font_size; // Point size for the top info bar text (version, progress, IGT, socials).
    float overlay_row_font_size; // Point size for the row 2 & 3 item text (name + progress).

    // --- Window Geometry ---
    WindowRect tracker_window; // The saved position and size of the main tracker window.
    WindowRect overlay_window; // The saved position and size of the overlay window.

    // --- Colors ---
    ColorRGBA tracker_bg_color; // Background color for the main tracker window.
    ColorRGBA overlay_bg_color; // Background color for the overlay window.
    bool overlay_transparent; // If true, the overlay window uses a transparent buffer instead of a background color.
    ColorRGBA text_color; // Global text color for UI elements.
    ColorRGBA overlay_text_color; // Text color for the overlay window.

    // --- Background Texture Paths ---
    char adv_bg_path[MAX_PATH_LENGTH]; // Relative path in resources/gui/
    char adv_bg_half_done_path[MAX_PATH_LENGTH]; // Relative path in resources/gui/
    char adv_bg_done_path[MAX_PATH_LENGTH]; // Relative path in resources/gui/

    // --- Icon Size & Position within the 96x96 background (tracker + overlay, excludes compact mode) ---
    float adv_icon_size; // Icon box edge length in 96x96 background space (ADV_ICON_MIN_SIZE..ADV_ICON_BG_SIZE)
    float adv_icon_offset_x; // Icon box left offset in 96x96 background space (0..ADV_ICON_BG_SIZE - adv_icon_size)
    float adv_icon_offset_y; // Icon box top offset in 96x96 background space (0..ADV_ICON_BG_SIZE - adv_icon_size)

    // --- UI Theme Colors (collapsible section) ---
    ColorRGBA ui_text_color; // Text color for the UI elements.
    ColorRGBA ui_window_bg_color; // Background color of UI windows
    ColorRGBA ui_frame_bg_color; // Background color for input fields, checkboxes, sliders etc.
    ColorRGBA ui_frame_bg_hovered_color; // Background color for frames when hovered
    ColorRGBA ui_frame_bg_active_color; // Background color for frames when active
    ColorRGBA ui_title_bg_active_color; // Background color of the title bar when active
    ColorRGBA ui_button_color; // Color of buttons
    ColorRGBA ui_button_hovered_color; // Background color of buttons when hovered
    ColorRGBA ui_button_active_color; // Background color of buttons when clicked
    ColorRGBA ui_header_color; // Background color of collapsable headers
    ColorRGBA ui_header_hovered_color; // Background color of headers when hovered
    ColorRGBA ui_header_active_color; // background color of headers when active/open
    ColorRGBA ui_check_mark_color; // Color of the checkmark inside checkboxes

    // --- View State (Pan, Zoom, Layout) ---
    float view_pan_x;
    float view_pan_y;
    float view_zoom;
    bool view_locked;
    bool view_camera_locked; // Zooming and panning the tracker map are ignored while this is set.
    float view_locked_width;
    bool use_manual_layout; // Manual Layout or Auto Layout

    // --- Overlay Text Sections ---
    bool overlay_show_world; // If true, the world name is shown in the overlay.
    bool overlay_show_run_details; // If true, the run details are shown in the overlay.
    bool overlay_show_progress; // If true, the progress bar is shown in the overlay.
    bool overlay_show_igt; // If true, the in-game time is shown in the overlay.
    bool igt_unit_spacing; // If true, a space is inserted before every time unit suffix (e.g. "02 m 04 s").
    bool igt_always_show_ms; // If true, milliseconds are always shown regardless of the time magnitude.
    bool igt_freeze_on_completion; // If true, the IGT freezes at the final time once the run is completed.
    bool overlay_show_update_timer; // If true, the update timer is shown in the overlay.
    char overlay_progress_separator[9];
    // Separator string drawn between top-bar segments. Default "|". Up to 8 visible characters.
    bool check_for_updates; // If true, checks for new versions on startup
    bool show_welcome_on_startup; // If true, shows the welcome message on startup
    int launch_count; // Number of times Advancely has been launched
    bool support_prompt_shown; // If true, the one-time launch-milestone support ask has been shown

    // --- Account Settings ---
    AccountType account_type; // Online (Mojang API) or Offline (manual UUID)
    CoopPlayer local_player; // This user's own Minecraft identity

    // --- Co-op Settings ---
    bool coop_enabled; // Master toggle for co-op mode
    bool coop_auto_accept; // Host: auto-approve incoming join requests without showing an approval prompt
    bool coop_read_all_save_files;
    // Host: keep reading on-disk player files for UUIDs not in the live lobby (ghost players)
    NetworkMode network_mode; // Runtime state: Singleplayer, Host, or Receiver (set programmatically)
    CoopTransport coop_transport; // RELAY (default, via relay server) or DIRECT (LAN/VPN)
    CoopStatMerge coop_stat_merge; // How to merge stat values: highest or cumulative
    CoopStatCheckbox coop_stat_checkbox; // Who can check off stats: host only or any player
    CoopCustomGoalMode coop_custom_goal_mode; // Who can modify custom goals: host only or any player
    // Per-user contributor-face display preferences (local visual only; not
    // synchronised with other lobby members).
    bool coop_show_contributor_faces; // Master toggle for all contributor faces in coop
    CoopFaceCorner coop_face_corner; // Corner placement for main-goal faces
    float coop_face_size; // Logical pixel size of main-goal faces (16..48)
    float coop_face_lod_threshold; // Zoom threshold below which non-checkbox faces hide
    char host_ip[64]; // The IP address to bind the server socket on (local/VPN)
    char host_public_ip[64]; // Optional public IP used for the room code (port forwarding)
    char host_port[16]; // The port the host listens on (default "12345")

    // --- Player Roster (Host only) ---
    int coop_player_count; // Number of players in the roster
    CoopPlayer coop_players[MAX_COOP_PLAYERS]; // The player roster

    // --- Advancement Assignments (Host only, All-Players merged view) ---
    int coop_adv_assignment_count; // Number of assigned advancements
    CoopAdvAssignment coop_adv_assignments[MAX_COOP_ADV_ASSIGNMENTS];
};


/**
 * @brief Converts a version string (e.g., "1.12") to an MC_Version enum (e.g., MC_VERSION_1_12).
 *
 * @param version_str The string to convert.
 * @return The corresponding MC_Version enum, or MC_VERSION_UNKNOWN.
 */
MC_Version settings_get_version_from_string(const char *version_str);

// Fills out[COMPACT_COUNTER_TYPE_COUNT] (indexed by OverlayCompactCounterType) with each Compact
// counter category's version-correct label and completed/total; total == 0 marks a category absent.
// Shared by the Compact settings UI and the overlay so their counts never drift. `count_hidden` true
// counts template-hidden goals (the panel's real totals), false leaves them out (what the pop-out
// stack can actually show) - pass the "Show Hidden Goals" setting for the latter.
void compact_compute_type_counters(const TemplateData *td, MC_Version version, CompactCounter *out,
                                   bool count_hidden);

// True if a Compact goal type counts up toward its target rather than just flipping done, so the
// pop-out stack can show it mid-progress and compact_stack_pop_on_progress applies to it. Advancements,
// recipes, unlocks and criteria carry nothing but a done flag, so they always pop on completion only.
// Shared by the settings UI (which types to list) and the overlay (which to gate) so the two can't drift.
bool compact_type_has_progress(OverlayCompactCounterType kind);

// Drops any selected Compact individual-goal items that no longer exist in `td`, so switching
// templates (including via the template editor) doesn't leave stale selections in the dropdowns.
// A null template clears them all. Type-count selections (universal categories) are left untouched.
void settings_prune_compact_cycle_items(AppSettings *settings, const TemplateData *td);

// Same as settings_prune_compact_cycle_items, for the independent pop-out-stack item selection.
void settings_prune_compact_stack_items(AppSettings *settings, const TemplateData *td);

/**
 * @brief Returns the owner UUID assigned to an advancement, or NULL if unassigned.
 *
 * In the coop "All Players" merged view, an assigned advancement is driven solely
 * by the owner's progress instead of the default "player with the most criteria" rule.
 *
 * @param settings The application settings holding the assignment list.
 * @param adv_root_name The advancement root_name to look up.
 * @return The owner UUID string, or NULL if the advancement has no assignment.
 */
const char *coop_get_advancement_owner(const AppSettings *settings, const char *adv_root_name);

/**
 * @brief Assigns an advancement to a player, or clears the assignment.
 *
 * @param settings The application settings holding the assignment list.
 * @param adv_root_name The advancement root_name to assign.
 * @param owner_uuid The owner's UUID, or NULL/empty to remove the assignment.
 */
void coop_set_advancement_owner(AppSettings *settings, const char *adv_root_name, const char *owner_uuid);

/**
 * @brief Converts a path mode string ("auto" or "manual") to a PathMode enum.
 * @param mode_str The string to convert.
 * @return The corresponding PathMode enum.
 */
PathMode settings_get_path_mode_from_string(const char *mode_str);


/**
 * @brief Populates an AppSettings struct with the default application settings.
 *
 * @param settings A pointer to the AppSettings struct to be populated.
 */
void settings_set_defaults(AppSettings *settings);

/**
 * @brief Resets every Advancely shortcut to the default from APP_HOTKEY_DEFS.
 */
void app_hotkeys_set_defaults(AppSettings *settings);

/**
 * @brief True when any Advancely shortcut differs between the two settings copies.
 */
bool app_hotkeys_different(const AppSettings *a, const AppSettings *b);

/**
 * @brief True when this action is bound to the given key and exact modifier set.
 *
 * Modifiers must match exactly, including the empty set, so a bare "V" binding does not also
 * swallow Shift+V. An unbound ("None") action never matches.
 *
 * @param settings  The settings copy holding the bindings.
 * @param action    The action to test.
 * @param key       The SDL keycode of the pressed key, straight from the key event.
 * @param held_mods HOTKEY_MOD_* bitmask of the modifiers held down.
 */
bool app_hotkey_matches(const AppSettings *settings, AppHotkeyAction action,
                        SDL_Keycode key, Uint16 held_mods);

/**
 * @brief Writes the label shown on a binding's button ("Shift+V", "None") into buf. Returns buf.
 */
const char *app_hotkey_display_label(const AppHotkey *hk, char *buf, size_t buf_size);

/**
 * @brief Loads settings from the settings.json file.
 *
 * If the file doesn't exist or a setting is missing, it populates
 * the struct with safe, default values. After loading, it calls
 * `construct_template_paths` to build the final file paths.
 *
 * @param settings A pointer to the AppSettings struct to be populated.
 * @return true if any default values were used (signaling a need to re-save), false otherwise.
 */
bool settings_load(AppSettings *settings);

/**
 * @brief Loads settings from an arbitrary file (e.g. a settings preset) into `settings`.
 *
 * Behaves like settings_load() but reads from `path` instead of settings.json, and
 * skips the .bak recovery and the "corrupted" popup that are specific to the primary
 * settings file. It never writes anything to disk. After parsing it calls
 * `construct_template_paths` to build the final file paths.
 *
 * @param settings A pointer to the AppSettings struct to be populated.
 * @param path The full path to the settings file to read.
 * @return true if the file was read and parsed, false if it could not be read.
 */
bool settings_load_from_file(AppSettings *settings, const char *path);

/**
 * @brief Saves settings to settings.json based on a specific context.
 * It reads the existing file, updates values according to the context, and writes it back.
 * @param settings A pointer to the AppSettings struct containing the data to save.
 * @param td A pointer to the TemplateData struct to save custom progress. Can be NULL.
 * @param context The context determining which parts of the settings to save.
 */
void settings_save(const AppSettings *settings, const TemplateData *td, SettingsSaveContext context);

void settings_save_overlay_width_only(int width);

/**
 * @brief Returns the per-UUID sub-object from a `custom_progress` or
 * `stat_progress_override` parent object, migrating legacy flat schema in-place
 * if primitive children are found (moves them under migration_uuid).
 * Creates the sub-object if missing. Returns NULL on bad input.
 */
cJSON *settings_get_player_progress_subobj(cJSON *parent_obj, const char *uuid, const char *migration_uuid);

/**
 * @brief Removes UUID entries from `custom_progress` and `stat_progress_override`
 * in settings.json that do not belong to any player currently in the roster.
 * Called whenever the lobby player list changes so stale player data does not
 * accumulate on disk.
 */
void settings_prune_stale_coop_progress(const AppSettings *settings);

/**
 * @brief Constructs the full paths to the template, language, snapshot JSON and notes TXT files. Does NOT CREATE the files or load them.
 *
 * Based on the version, category, and optional flag settings, this function
 * builds the final, relative paths to the required data files and stores them
 * in the `template_path` and `lang_path` fields of the AppSettings struct.
 *
 * @param settings A pointer to the AppSettings struct containing the base settings
 * and which will be updated with the constructed paths.
 */
void construct_template_paths(AppSettings *settings);

/**
 * @brief Builds the base template path (without extension) that all template-related
 * files share. Used as the prefix for template .json, lang, notes, and per-player
 * legacy snapshot files.
 *
 * Output form: "<resources>/templates/<version>/<category>/<version_fmt>_<category><optional_flag>"
 *
 * @param settings The app settings (reads version_str, category, optional_flag).
 * @param out      Output buffer.
 * @param out_size Size of the output buffer.
 */
void construct_template_base_path(const AppSettings *settings, char *out, size_t out_size);

/**
 * @brief Builds a per-player legacy snapshot file path: "<template_base>_<lowercase_username>_snapshot.json".
 * Username is lowercased to match the on-disk .dat filename convention and to avoid
 * case-sensitivity issues on some filesystems. Case-insensitive uniqueness of usernames
 * is enforced at co-op join time, so collisions in-session are impossible.
 *
 * @param template_base Base path (from construct_template_base_path).
 * @param username      Player username (any case).
 * @param out           Output buffer.
 * @param out_size      Size of the output buffer.
 */
void build_player_snapshot_path(const char *template_base, const char *username, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif //SETTINGS_UTILS_H
