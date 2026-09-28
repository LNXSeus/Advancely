// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 28.09.2026.
//

#ifndef SETTINGS_PRESET_IMPORT_H
#define SETTINGS_PRESET_IMPORT_H

#include "settings_utils.h"

typedef enum {
    PRESET_GROUP_PATHS = 0,
    PRESET_GROUP_TRACKER_VISUALS,
    PRESET_GROUP_UI_VISUALS,
    PRESET_GROUP_OVERLAY,
    PRESET_GROUP_ACCOUNT,
    PRESET_GROUP_COOP,
    PRESET_GROUP_HOTKEYS,
    PRESET_GROUP_SYSTEM,
    PRESET_GROUP_TRACKER_VIEW,
    PRESET_GROUP_NOTES,
    PRESET_GROUP_STARTUP,
    PRESET_GROUP_PROGRESS,
    PRESET_GROUP_OTHER,
    PRESET_GROUP_COUNT
} PresetGroup;

// Unchecked by default: belongs to the user or their machine rather than to a look.
#define PRESET_KEY_USER (1u << 0)
// Never listed or applied (Apply keeps the live value anyway, or another process owns it).
#define PRESET_KEY_SKIP (1u << 1)
// Not part of AppSettings; copied into settings.json on Apply instead.
#define PRESET_KEY_PROGRESS (1u << 2)
// Expands into one row per Advancely hotkey, labeled from APP_HOTKEY_DEFS.
#define PRESET_KEY_APP_HOTKEYS (1u << 3)

// Every settings.json key the "Load Preset" popup lists, in the order the settings window shows them.
// When a setting moves in the window (or a new one is added), move or add its line here too. Keys
// missing from this list still show up, under "Other" with their raw settings.json path.
// Columns: settings.json path ('.' between levels, '|' between keys that only make sense together),
// group, label, PRESET_KEY_* flags, names for integer values ('|' separated, index = value) or NULL.
#define PRESET_KEY_LIST(X) \
    X("path_mode", PRESET_GROUP_PATHS, "Path Mode", PRESET_KEY_USER, NULL) \
    X("fixed_world_path", PRESET_GROUP_PATHS, "Fixed World Path", PRESET_KEY_USER, NULL) \
    X("manual_saves_path", PRESET_GROUP_PATHS, "Custom Saves Folder", PRESET_KEY_USER, NULL) \
    X("version|category|optional_flag", PRESET_GROUP_PATHS, "Template (Version, Category, Flag)", 0, NULL) \
    X("display_version", PRESET_GROUP_PATHS, "Display Version", 0, NULL) \
    X("general.using_stats_per_world_legacy", PRESET_GROUP_PATHS, "Using StatsPerWorld Mod", 0, NULL) \
    X("general.using_hermes", PRESET_GROUP_PATHS, "Using Hermes Mod (Live Tracking)", 0, NULL) \
    X("lang_flag", PRESET_GROUP_PATHS, "Language", 0, NULL) \
    X("layout_flag", PRESET_GROUP_PATHS, "Layout", 0, NULL) \
    X("category_display_name", PRESET_GROUP_PATHS, "Display Category", 0, NULL) \
    X("lock_category_display_name", PRESET_GROUP_PATHS, "Lock Display Category", 0, NULL) \
    \
    X("general.always_on_top", PRESET_GROUP_TRACKER_VISUALS, "Always On Top", 0, NULL) \
    X("general.fps", PRESET_GROUP_TRACKER_VISUALS, "Tracker FPS Limit", 0, NULL) \
    X("visuals.lod_text_sub_threshold", PRESET_GROUP_TRACKER_VISUALS, "Hide Sub-Item Text At", 0, NULL) \
    X("visuals.lod_text_main_threshold", PRESET_GROUP_TRACKER_VISUALS, "Hide Main Text/Checkbox At", 0, NULL) \
    X("visuals.lod_icon_detail_threshold", PRESET_GROUP_TRACKER_VISUALS, "Simplify Icons At", 0, NULL) \
    X("visuals.checkbox_reveal_enabled", PRESET_GROUP_TRACKER_VISUALS, "Reveal Checkboxes Near Cursor", 0, NULL) \
    X("visuals.checkbox_reveal_radius", PRESET_GROUP_TRACKER_VISUALS, "Cursor Reveal Radius", 0, NULL) \
    X("visuals.text_reveal_enabled", PRESET_GROUP_TRACKER_VISUALS, "Also Reveal Text Near Cursor", 0, NULL) \
    X("visuals.scrollable_list_threshold", PRESET_GROUP_TRACKER_VISUALS, "Scrollable List Threshold", 0, NULL) \
    X("visuals.tracker_list_scroll_speed", PRESET_GROUP_TRACKER_VISUALS, "List Scroll Speed", 0, NULL) \
    X("visuals.tracker_list_incomplete_first", PRESET_GROUP_TRACKER_VISUALS, "Incomplete Sub-Goals First", 0, NULL) \
    X("general.section_order", PRESET_GROUP_TRACKER_VISUALS, "Section Order", 0, NULL) \
    X("visuals.tracker_vertical_spacing", PRESET_GROUP_TRACKER_VISUALS, "Tracker Vertical Spacing", 0, NULL) \
    X("visuals.tracker_criteria_vertical_spacing", PRESET_GROUP_TRACKER_VISUALS, "Criteria Vertical Spacing", 0, \
      NULL) \
    X("visuals.tracker_section_custom_width_enabled|visuals.tracker_section_custom_item_width", \
      PRESET_GROUP_TRACKER_VISUALS, "Custom Section Item Width", 0, NULL) \
    X("general.tracker_font_name", PRESET_GROUP_TRACKER_VISUALS, "Tracker Font", 0, NULL) \
    X("general.tracker_font_size", PRESET_GROUP_TRACKER_VISUALS, "Tracker Font Size", 0, NULL) \
    X("general.tracker_sub_font_size", PRESET_GROUP_TRACKER_VISUALS, "Sub-Item Font Size", 0, NULL) \
    X("general.tracker_ui_font_size", PRESET_GROUP_TRACKER_VISUALS, "Tracker UI Font Size", 0, NULL) \
    X("visuals.tracker_bg_color", PRESET_GROUP_TRACKER_VISUALS, "Tracker Background Color", 0, NULL) \
    X("visuals.text_color", PRESET_GROUP_TRACKER_VISUALS, "Tracker Text Color", 0, NULL) \
    X("visuals.adv_bg_path", PRESET_GROUP_TRACKER_VISUALS, "Default Background Texture", 0, NULL) \
    X("visuals.adv_bg_half_done_path", PRESET_GROUP_TRACKER_VISUALS, "Half-Done Background Texture", 0, NULL) \
    X("visuals.adv_bg_done_path", PRESET_GROUP_TRACKER_VISUALS, "Done Background Texture", 0, NULL) \
    X("visuals.adv_icon_size", PRESET_GROUP_TRACKER_VISUALS, "Icon Size", 0, NULL) \
    X("visuals.adv_icon_offset_x", PRESET_GROUP_TRACKER_VISUALS, "Icon X Position", 0, NULL) \
    X("visuals.adv_icon_offset_y", PRESET_GROUP_TRACKER_VISUALS, "Icon Y Position", 0, NULL) \
    X("visuals.tracker_shared_icon_size", PRESET_GROUP_TRACKER_VISUALS, "Shared Icon Size", 0, NULL) \
    X("visuals.tracker_shared_icon_keep_redundant", PRESET_GROUP_TRACKER_VISUALS, "Keep Redundant Shared Icons", \
      0, NULL) \
    \
    X("general.ui_font_name", PRESET_GROUP_UI_VISUALS, "Settings/UI Font", 0, NULL) \
    X("general.ui_font_size", PRESET_GROUP_UI_VISUALS, "Settings/UI Font Size", 0, NULL) \
    X("visuals.ui_text_color", PRESET_GROUP_UI_VISUALS, "UI Text", 0, NULL) \
    X("visuals.ui_window_bg_color", PRESET_GROUP_UI_VISUALS, "Window Background", 0, NULL) \
    X("visuals.ui_frame_bg_color", PRESET_GROUP_UI_VISUALS, "Frame Background", 0, NULL) \
    X("visuals.ui_frame_bg_hovered_color", PRESET_GROUP_UI_VISUALS, "Frame Bg Hovered", 0, NULL) \
    X("visuals.ui_frame_bg_active_color", PRESET_GROUP_UI_VISUALS, "Frame Bg Active", 0, NULL) \
    X("visuals.ui_title_bg_active_color", PRESET_GROUP_UI_VISUALS, "Active Title Bar", 0, NULL) \
    X("visuals.ui_button_color", PRESET_GROUP_UI_VISUALS, "Button", 0, NULL) \
    X("visuals.ui_button_hovered_color", PRESET_GROUP_UI_VISUALS, "Button Hovered", 0, NULL) \
    X("visuals.ui_button_active_color", PRESET_GROUP_UI_VISUALS, "Button Active", 0, NULL) \
    X("visuals.ui_header_color", PRESET_GROUP_UI_VISUALS, "Header", 0, NULL) \
    X("visuals.ui_header_hovered_color", PRESET_GROUP_UI_VISUALS, "Header Hovered", 0, NULL) \
    X("visuals.ui_header_active_color", PRESET_GROUP_UI_VISUALS, "Header Active", 0, NULL) \
    X("visuals.ui_check_mark_color", PRESET_GROUP_UI_VISUALS, "Check Mark", 0, NULL) \
    \
    X("general.enable_overlay", PRESET_GROUP_OVERLAY, "Enable Overlay", 0, NULL) \
    X("general.overlay_fps", PRESET_GROUP_OVERLAY, "Overlay FPS Limit", 0, NULL) \
    X("general.overlay_render_mode", PRESET_GROUP_OVERLAY, "Mode", 0, "Scrolling Belt|Page|Compact") \
    X("general.overlay_show_hidden_goals", PRESET_GROUP_OVERLAY, "Show Hidden Goals", 0, NULL) \
    X("general.igt_freeze_on_completion", PRESET_GROUP_OVERLAY, "Freeze Timer on Completion", 0, NULL) \
    X("general.igt_unit_spacing", PRESET_GROUP_OVERLAY, "Timers Unit Spacing", 0, NULL) \
    X("general.igt_always_show_ms", PRESET_GROUP_OVERLAY, "IGT Always Show ms", 0, NULL) \
    X("general.overlay_show_world", PRESET_GROUP_OVERLAY, "Show World", 0, NULL) \
    X("general.overlay_show_run_details", PRESET_GROUP_OVERLAY, "Show Run Details", 0, NULL) \
    X("general.overlay_show_progress", PRESET_GROUP_OVERLAY, "Show Progress", 0, NULL) \
    X("general.overlay_show_igt", PRESET_GROUP_OVERLAY, "Show IGT", 0, NULL) \
    X("general.overlay_show_update_timer", PRESET_GROUP_OVERLAY, "Show Update Timer", 0, NULL) \
    X("general.overlay_progress_separator", PRESET_GROUP_OVERLAY, "Segment Separator", 0, NULL) \
    X("general.overlay_row3_remove_completed", PRESET_GROUP_OVERLAY, "Hide Completed Row 3 Goals", 0, NULL) \
    X("general.overlay_stat_cycle_speed", PRESET_GROUP_OVERLAY, "Sub-Stat Cycle Interval (s)", 0, NULL) \
    X("general.overlay_clear_animation", PRESET_GROUP_OVERLAY, "Clear Animation (s)", 0, NULL) \
    X("general.overlay_clear_fade_enabled", PRESET_GROUP_OVERLAY, "Clear Fade Out", 0, NULL) \
    X("general.overlay_clear_fade_time", PRESET_GROUP_OVERLAY, "Clear Fade Out Time (s)", 0, NULL) \
    X("general.overlay_settle_time", PRESET_GROUP_OVERLAY, "Settle Animation (s)", 0, NULL) \
    X("general.overlay_page_interval", PRESET_GROUP_OVERLAY, "Page Switch Interval (s)", 0, NULL) \
    X("general.overlay_page_align", PRESET_GROUP_OVERLAY, "Page Alignment", 0, "Left|Center|Right") \
    X("visuals.compact_show_row1_icons", PRESET_GROUP_OVERLAY, "Compact Row 1: Show Icons", 0, NULL) \
    X("visuals.compact_row1_icon_size", PRESET_GROUP_OVERLAY, "Compact Row 1: Icon Size", 0, NULL) \
    X("visuals.compact_icon_shared_size", PRESET_GROUP_OVERLAY, "Compact Row 1: Shared Icon Size", 0, NULL) \
    X("visuals.compact_row1_spacing", PRESET_GROUP_OVERLAY, "Compact Row 1: Horizontal Icon Spacing", 0, NULL) \
    X("visuals.compact_icon_row_gap", PRESET_GROUP_OVERLAY, "Compact Row 1: Icon Gap Below", 0, NULL) \
    X("visuals.compact_icon_cycle_interval", PRESET_GROUP_OVERLAY, "Compact Row 1: Icon Cycle Interval", 0, NULL) \
    X("visuals.compact_row1_clear_animation", PRESET_GROUP_OVERLAY, "Compact Row 1: Clear Animation (s)", 0, NULL) \
    X("visuals.compact_row1_fade_enabled", PRESET_GROUP_OVERLAY, "Compact Row 1: Fade Out", 0, NULL) \
    X("visuals.compact_row1_fade_time", PRESET_GROUP_OVERLAY, "Compact Row 1: Fade Out Time (s)", 0, NULL) \
    X("visuals.compact_row1_settle_time", PRESET_GROUP_OVERLAY, "Compact Row 1: Settle Animation (s)", 0, NULL) \
    X("visuals.compact_panel_path", PRESET_GROUP_OVERLAY, "Compact Panel: Texture", 0, NULL) \
    X("visuals.compact_panel_pixel_scale", PRESET_GROUP_OVERLAY, "Compact Panel: Pixel Scale", 0, NULL) \
    X("visuals.compact_panel_inset_left|visuals.compact_panel_inset_right|visuals.compact_panel_inset_top|" \
      "visuals.compact_panel_inset_bottom", PRESET_GROUP_OVERLAY, "Compact Panel: Border (L/R/T/B)", 0, NULL) \
    X("visuals.compact_panel_padding", PRESET_GROUP_OVERLAY, "Compact Panel: Padding", 0, NULL) \
    X("visuals.compact_panel_align", PRESET_GROUP_OVERLAY, "Compact Panel: Alignment", 0, "Left|Center|Right") \
    X("visuals.compact_cycle_types|visuals.compact_cycle_type_orders|visuals.compact_cycle_items|" \
      "visuals.compact_cycle_run_counter|visuals.compact_cycle_run_percent|" \
      "visuals.compact_cycle_run_counter_order|visuals.compact_cycle_run_percent_order|" \
      "visuals.compact_cycle_customized", PRESET_GROUP_OVERLAY, "Compact Panel: Content", 0, NULL) \
    X("visuals.compact_chain_entries", PRESET_GROUP_OVERLAY, "Compact Panel: Chain All Entries", 0, NULL) \
    X("visuals.compact_chain_separator", PRESET_GROUP_OVERLAY, "Compact Panel: Chain Separator", 0, NULL) \
    X("visuals.compact_cycle_interval", PRESET_GROUP_OVERLAY, "Compact Panel: Cycle Interval", 0, NULL) \
    X("visuals.compact_stack_types|visuals.compact_stack_items", PRESET_GROUP_OVERLAY, "Compact Stack: Content", 0, \
      NULL) \
    X("visuals.compact_stack_pop_on_progress", PRESET_GROUP_OVERLAY, "Compact Stack: Pop On Progress", 0, NULL) \
    X("visuals.compact_show_completion_markers", PRESET_GROUP_OVERLAY, "Compact Stack: Show Completion Markers", 0, \
      NULL) \
    X("visuals.compact_stack_row_gap", PRESET_GROUP_OVERLAY, "Compact Stack: Stack Gap Below", 0, NULL) \
    X("visuals.compact_stack_max_lines", PRESET_GROUP_OVERLAY, "Compact Stack: Max Stack Lines", 0, NULL) \
    X("visuals.compact_stack_hold_time", PRESET_GROUP_OVERLAY, "Compact Stack: Hold Time", 0, NULL) \
    X("visuals.compact_stack_rise_time", PRESET_GROUP_OVERLAY, "Compact Stack: Animation Time", 0, NULL) \
    X("visuals.compact_stack_fade_enabled", PRESET_GROUP_OVERLAY, "Compact Stack: Fade Out", 0, NULL) \
    X("visuals.compact_stack_fade_time", PRESET_GROUP_OVERLAY, "Compact Stack: Fade Out Time (s)", 0, NULL) \
    X("visuals.compact_pop_icon_size", PRESET_GROUP_OVERLAY, "Compact Stack: Pop Icon Size", 0, NULL) \
    X("visuals.compact_stack_shared_icon_size", PRESET_GROUP_OVERLAY, "Compact Stack: Shared Icon Size", 0, NULL) \
    X("general.overlay_scroll_speed", PRESET_GROUP_OVERLAY, "Overlay Scroll Speed", 0, NULL) \
    X("visuals.overlay_row1_custom_scroll_speed_enabled", PRESET_GROUP_OVERLAY, "Row 1 Custom Speed", 0, NULL) \
    X("visuals.overlay_row1_scroll_speed", PRESET_GROUP_OVERLAY, "Row 1 Scroll Speed", 0, NULL) \
    X("visuals.overlay_row1_freeze_enabled", PRESET_GROUP_OVERLAY, "Row 1 Auto-Freeze", 0, NULL) \
    X("visuals.overlay_row1_freeze_align", PRESET_GROUP_OVERLAY, "Row 1 Freeze Alignment", 0, "Left|Center|Right") \
    X("visuals.overlay_row2_custom_scroll_speed_enabled", PRESET_GROUP_OVERLAY, "Row 2 Custom Speed", 0, NULL) \
    X("visuals.overlay_row2_scroll_speed", PRESET_GROUP_OVERLAY, "Row 2 Scroll Speed", 0, NULL) \
    X("visuals.overlay_row2_freeze_enabled", PRESET_GROUP_OVERLAY, "Row 2 Auto-Freeze", 0, NULL) \
    X("visuals.overlay_row2_freeze_align", PRESET_GROUP_OVERLAY, "Row 2 Freeze Alignment", 0, "Left|Center|Right") \
    X("visuals.overlay_row3_custom_scroll_speed_enabled", PRESET_GROUP_OVERLAY, "Row 3 Custom Speed", 0, NULL) \
    X("visuals.overlay_row3_scroll_speed", PRESET_GROUP_OVERLAY, "Row 3 Scroll Speed", 0, NULL) \
    X("visuals.overlay_row3_freeze_enabled", PRESET_GROUP_OVERLAY, "Row 3 Auto-Freeze", 0, NULL) \
    X("visuals.overlay_row3_freeze_align", PRESET_GROUP_OVERLAY, "Row 3 Freeze Alignment", 0, "Left|Center|Right") \
    X("visuals.overlay_window.w", PRESET_GROUP_OVERLAY, "Overlay Width", 0, NULL) \
    X("visuals.overlay_window.x|visuals.overlay_window.y|visuals.overlay_window.h", PRESET_GROUP_OVERLAY, \
      "Overlay Position & Height", PRESET_KEY_SKIP, NULL) \
    X("general.overlay_progress_text_align", PRESET_GROUP_OVERLAY, "Overlay Title Alignment", 0, NULL) \
    X("visuals.overlay_row1_icon_size", PRESET_GROUP_OVERLAY, "Row 1 Icon Size", 0, NULL) \
    X("visuals.overlay_row1_spacing", PRESET_GROUP_OVERLAY, "Row 1 Icon Spacing", 0, NULL) \
    X("visuals.overlay_row1_shared_icon_size", PRESET_GROUP_OVERLAY, "Row 1 Shared Icon Size", 0, NULL) \
    X("visuals.overlay_shared_icon_keep_redundant", PRESET_GROUP_OVERLAY, "Keep Redundant Shared Icons", 0, NULL) \
    X("visuals.overlay_row2_bg_size", PRESET_GROUP_OVERLAY, "Row 2 Background Size", 0, NULL) \
    X("visuals.overlay_row3_bg_size", PRESET_GROUP_OVERLAY, "Row 3 Background Size", 0, NULL) \
    X("visuals.overlay_row2_custom_spacing_enabled", PRESET_GROUP_OVERLAY, "Custom Row 2 Spacing", 0, NULL) \
    X("visuals.overlay_row2_custom_spacing", PRESET_GROUP_OVERLAY, "Row 2 Item Width", 0, NULL) \
    X("visuals.overlay_row3_custom_spacing_enabled", PRESET_GROUP_OVERLAY, "Custom Row 3 Spacing", 0, NULL) \
    X("visuals.overlay_row3_custom_spacing", PRESET_GROUP_OVERLAY, "Row 3 Item Width", 0, NULL) \
    X("visuals.overlay_custom_vertical_spacing_enabled", PRESET_GROUP_OVERLAY, "Custom Vertical Spacing", 0, NULL) \
    X("visuals.overlay_gap_top_to_row1", PRESET_GROUP_OVERLAY, "Top Bar -> Row 1 Gap", 0, NULL) \
    X("visuals.overlay_gap_row1_to_row2", PRESET_GROUP_OVERLAY, "Row 1 -> Row 2 Gap", 0, NULL) \
    X("visuals.overlay_gap_row2_to_row3", PRESET_GROUP_OVERLAY, "Row 2 -> Row 3 Gap", 0, NULL) \
    X("visuals.overlay_gap_row3_to_bottom", PRESET_GROUP_OVERLAY, "Row 3 -> Bottom Gap", 0, NULL) \
    X("general.overlay_font_name", PRESET_GROUP_OVERLAY, "Overlay Font", 0, NULL) \
    X("general.overlay_progress_font_size", PRESET_GROUP_OVERLAY, "Top Text Size", 0, NULL) \
    X("general.overlay_row_font_size", PRESET_GROUP_OVERLAY, "Row Text Size", 0, NULL) \
    X("visuals.compact_label_font_name", PRESET_GROUP_OVERLAY, "Compact: Label Font", 0, NULL) \
    X("visuals.compact_label_font_size", PRESET_GROUP_OVERLAY, "Compact: Label Text Size", 0, NULL) \
    X("visuals.compact_count_font_name", PRESET_GROUP_OVERLAY, "Compact: Count Font", 0, NULL) \
    X("visuals.compact_count_font_size", PRESET_GROUP_OVERLAY, "Compact: Count Text Size", 0, NULL) \
    X("visuals.compact_panel_line_gap", PRESET_GROUP_OVERLAY, "Compact: Line Spacing", 0, NULL) \
    X("visuals.compact_stack_font_name", PRESET_GROUP_OVERLAY, "Compact: Stack Font", 0, NULL) \
    X("visuals.compact_stack_font_size", PRESET_GROUP_OVERLAY, "Compact: Stack Text Size", 0, NULL) \
    X("visuals.overlay_bg_color", PRESET_GROUP_OVERLAY, "Overlay Background Color", 0, NULL) \
    X("visuals.overlay_transparent", PRESET_GROUP_OVERLAY, "Transparent", 0, NULL) \
    X("visuals.overlay_text_color", PRESET_GROUP_OVERLAY, "Overlay Text Color", 0, NULL) \
    \
    X("account.type", PRESET_GROUP_ACCOUNT, "Account Type", PRESET_KEY_USER, NULL) \
    X("account.username", PRESET_GROUP_ACCOUNT, "Username", PRESET_KEY_USER, NULL) \
    X("account.uuid", PRESET_GROUP_ACCOUNT, "UUID", PRESET_KEY_USER, NULL) \
    X("account.display_name", PRESET_GROUP_ACCOUNT, "Display Name", PRESET_KEY_USER, NULL) \
    \
    X("coop.enabled", PRESET_GROUP_COOP, "Enable Co-op", PRESET_KEY_USER, NULL) \
    X("coop.network_mode", PRESET_GROUP_COOP, "Role", PRESET_KEY_USER, NULL) \
    X("coop.transport", PRESET_GROUP_COOP, "Connection (Relay / LAN)", PRESET_KEY_USER, NULL) \
    X("coop.auto_accept", PRESET_GROUP_COOP, "Auto-accept Join Requests", PRESET_KEY_USER, NULL) \
    X("coop.read_all_save_files", PRESET_GROUP_COOP, "Track Disconnected / Offline Players", 0, NULL) \
    X("coop.host_ip", PRESET_GROUP_COOP, "IP Address", PRESET_KEY_USER, NULL) \
    X("coop.host_public_ip", PRESET_GROUP_COOP, "Public IP", PRESET_KEY_USER, NULL) \
    X("coop.host_port", PRESET_GROUP_COOP, "Port", PRESET_KEY_USER, NULL) \
    X("coop.merge_settings.stat_merge", PRESET_GROUP_COOP, "Stats / Sub-Stats Merging", 0, NULL) \
    X("coop.merge_settings.stat_checkbox", PRESET_GROUP_COOP, "Stat Checkboxes", 0, NULL) \
    X("coop.merge_settings.custom_goal_mode", PRESET_GROUP_COOP, "Custom Goals", 0, NULL) \
    X("coop.contributor_faces.show", PRESET_GROUP_COOP, "Show Contributor Faces", 0, NULL) \
    X("coop.contributor_faces.corner", PRESET_GROUP_COOP, "Face Corner", 0, NULL) \
    X("coop.contributor_faces.size", PRESET_GROUP_COOP, "Main-Goal Face Size", 0, NULL) \
    X("coop.contributor_faces.lod_threshold", PRESET_GROUP_COOP, "Hide Contributor Faces At", 0, NULL) \
    X("visuals.compact_coop_panel_face_size", PRESET_GROUP_COOP, "Panel Face Size", 0, NULL) \
    X("visuals.compact_coop_panel_face_offset_x", PRESET_GROUP_COOP, "Panel Face Offset X", 0, NULL) \
    X("visuals.compact_coop_panel_face_offset_y", PRESET_GROUP_COOP, "Panel Face Offset Y", 0, NULL) \
    X("visuals.compact_stack_face_size", PRESET_GROUP_COOP, "Stack Face Size", 0, NULL) \
    X("coop.player_roster|coop.advancement_assignments", PRESET_GROUP_COOP, "Lobby Roster & Assignments", \
      PRESET_KEY_SKIP, NULL) \
    \
    X("hotkeys", PRESET_GROUP_HOTKEYS, "Custom Goal Hotkeys", PRESET_KEY_USER, NULL) \
    X("app_hotkeys", PRESET_GROUP_HOTKEYS, "Advancely Hotkeys", PRESET_KEY_USER | PRESET_KEY_APP_HOTKEYS, NULL) \
    \
    X("general.check_for_updates", PRESET_GROUP_SYSTEM, "Auto-Check for Updates", 0, NULL) \
    X("general.print_debug_status", PRESET_GROUP_SYSTEM, "Print Debug To Console", 0, NULL) \
    \
    X("visuals.tracker_window", PRESET_GROUP_TRACKER_VIEW, "Tracker Window Position & Size", PRESET_KEY_USER, NULL) \
    X("view_state.pan_x|view_state.pan_y|view_state.zoom", PRESET_GROUP_TRACKER_VIEW, "Camera Position & Zoom", \
      PRESET_KEY_USER, NULL) \
    X("view_state.camera_locked", PRESET_GROUP_TRACKER_VIEW, "Lock Camera", PRESET_KEY_USER, NULL) \
    X("view_state.locked|view_state.locked_width", PRESET_GROUP_TRACKER_VIEW, "Lock Layout", PRESET_KEY_USER, NULL) \
    X("view_state.sweep_enabled", PRESET_GROUP_TRACKER_VIEW, "Selection Rectangle", PRESET_KEY_USER, NULL) \
    X("visuals.tracker_fullscreen|view_state.use_manual_layout|general.goal_hiding_mode|" \
      "general.invert_hiding_mode", PRESET_GROUP_TRACKER_VIEW, "Live Tracker State", PRESET_KEY_SKIP, NULL) \
    \
    X("general.per_world_notes", PRESET_GROUP_NOTES, "Per-World Notes", 0, NULL) \
    X("general.notes_use_roboto_font", PRESET_GROUP_NOTES, "Use Roboto Font", 0, NULL) \
    \
    X("general.show_welcome_on_startup", PRESET_GROUP_STARTUP, "Show Welcome Window on Startup", PRESET_KEY_USER, \
      NULL) \
    X("general.launch_count", PRESET_GROUP_STARTUP, "Launch Count", PRESET_KEY_USER, NULL) \
    X("general.support_prompt_shown", PRESET_GROUP_STARTUP, "Support Prompt Shown", PRESET_KEY_USER, NULL) \
    \
    X("custom_progress", PRESET_GROUP_PROGRESS, "Custom Goal Progress", PRESET_KEY_USER | PRESET_KEY_PROGRESS, NULL) \
    X("stat_progress_override", PRESET_GROUP_PROGRESS, "Manually Completed Stats", \
      PRESET_KEY_USER | PRESET_KEY_PROGRESS, NULL) \
    X("stat_stage_baselines", PRESET_GROUP_PROGRESS, "Stat Stage Starting Points", \
      PRESET_KEY_USER | PRESET_KEY_PROGRESS, NULL)

// The settings.json sections holding per-world progress rather than configuration, i.e. every
// PRESET_KEY_PROGRESS entry above. AppSettings does not carry them, so a preset's copy is written
// straight into settings.json on Apply.
#define PRESET_PROGRESS_SECTION_COUNT 3
extern const char *PRESET_PROGRESS_SECTIONS[PRESET_PROGRESS_SECTION_COUNT];

/**
 * @brief Opens the "Load Preset" popup, which lists every setting the preset would change.
 *
 * Diffs the preset file against `current` (the values in the settings window). Call once when
 * the user asks to load a preset; preset_import_render() then draws the popup every frame.
 *
 * @param preset_path Full path of the preset .json file.
 * @param preset_name Name shown in the popup title.
 * @param current The settings the preset is compared against.
 * @return false if the preset file could not be read (nothing is opened).
 */
bool preset_import_open(const char *preset_path, const char *preset_name, const AppSettings *current);

/**
 * @brief Draws the "Load Preset" popup opened by preset_import_open(). Call every frame.
 *
 * On the frame the user confirms, the selected settings are written into `settings` (everything
 * else keeps its value) and `out_progress_sections` tells, per PRESET_PROGRESS_SECTIONS entry,
 * whether the preset's copy should replace the user's on Apply.
 *
 * @return true on the frame the selection was loaded into `settings`.
 */
bool preset_import_render(AppSettings *settings, bool out_progress_sections[PRESET_PROGRESS_SECTION_COUNT]);

#endif //SETTINGS_PRESET_IMPORT_H
