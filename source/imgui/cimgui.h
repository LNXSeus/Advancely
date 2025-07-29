
// cimgui.h (Updated for modern Dear ImGui)
// This is a modified version of cimgui.h to be compatible with a newer version of Dear ImGui.
// Several functions were obsolete and their signatures have been updated or commented out.
// You will also need to update cimgui.cpp to match these changes.

#pragma once

#if defined _WIN32 || defined __CYGWIN__
    #ifdef CIMGUI_NO_EXPORT
        #define API
    #else
        #define API __declspec(dllexport)
    #endif
#else
    #define API
#endif

#if defined __cplusplus
#define EXTERN extern "C"
#else
#include <stdarg.h>
#include <stdbool.h>
#define EXTERN extern
#endif

#define CIMGUI_API EXTERN API
#define CONST const

//-----------------------------------------------------------------------------
// Forward declarations and basic types
//-----------------------------------------------------------------------------



typedef struct ImGuiIO ImGuiIO;
typedef struct ImGuiStyle ImGuiStyle;
typedef struct ImDrawData ImDrawData;
typedef struct ImVec2 ImVec2;
typedef struct ImVec4 ImVec4;
typedef struct ImGuiInputTextCallbackData ImGuiInputTextCallbackData;
typedef struct ImGuiSizeCallbackData ImGuiSizeCallbackData;
typedef struct ImDrawList ImDrawList;
typedef struct ImGuiStorage ImGuiStorage;
typedef struct ImFont ImFont;
typedef struct ImFontConfig ImFontConfig;
typedef struct ImFontAtlas ImFontAtlas;
typedef struct ImDrawCmd ImDrawCmd;
typedef struct ImGuiListClipper ImGuiListClipper;
typedef struct ImGuiTextFilter ImGuiTextFilter;
typedef struct ImGuiTextBuffer ImGuiTextBuffer;
typedef struct ImGuiPayload ImGuiPayload;
typedef struct ImGuiContext ImGuiContext;
typedef struct ImFontGlyph ImFontGlyph;
typedef struct ImGuiViewport ImGuiViewport;


// Define as int for regular C code, enums in C++
#if !defined(__cplusplus)
typedef int ImGuiDir;
typedef int ImGuiKey;
typedef int ImGuiComboFlags;
#endif


typedef unsigned short ImDrawIdx;
typedef unsigned int ImU32;
typedef unsigned short ImWchar;

// In C, we need to be explicit about the size of ImU64
#if !defined(__cplusplus)
#include <stdint.h>
typedef uint64_t ImU64;
#else
typedef unsigned long long ImU64;
#endif

typedef ImU64 ImTextureID;
typedef ImU32 ImGuiID;

// These are all enums in C++. In C, they are just ints.
typedef int ImGuiCol;
typedef int ImGuiStyleVar;
typedef int ImGuiColorEditFlags;
typedef int ImGuiMouseCursor;
typedef int ImGuiWindowFlags;
typedef int ImGuiCond;
typedef int ImGuiInputTextFlags;
typedef int ImGuiSelectableFlags;
typedef int ImGuiTreeNodeFlags;
typedef int ImGuiFocusedFlags;
typedef int ImGuiHoveredFlags;
typedef int ImGuiDragDropFlags;
typedef int ImGuiPopupFlags;
typedef int ImGuiMouseButton;
typedef int ImGuiTabBarFlags;
typedef int ImGuiTabItemFlags;
typedef int ImGuiSliderFlags;
typedef int ImGuiChildFlags;

typedef int (*ImGuiInputTextCallback)(struct ImGuiInputTextCallbackData *data);
typedef void (*ImGuiSizeCallback)(struct ImGuiSizeCallbackData *data);
typedef void* (*ImGuiMemAllocFunc)(size_t sz, void* user_data);
typedef void (*ImGuiMemFreeFunc)(void* ptr, void* user_data);

//-----------------------------------------------------------------------------
// Main API
//-----------------------------------------------------------------------------

CIMGUI_API ImGuiIO* igGetIO();
CIMGUI_API ImGuiStyle* igGetStyle();
CIMGUI_API ImDrawData* igGetDrawData();
CIMGUI_API void              igNewFrame();
CIMGUI_API void              igRender();
CIMGUI_API void              igEndFrame();

CIMGUI_API void              igShowDemoWindow(bool* p_open);
CIMGUI_API void              igShowMetricsWindow(bool* p_open);
CIMGUI_API void              igShowStyleEditor(ImGuiStyle* ref);
CIMGUI_API bool              igShowStyleSelector(const char* label);
CIMGUI_API void              igShowFontSelector(const char* label);
CIMGUI_API void              igShowUserGuide();
CIMGUI_API const char* igGetVersion();

CIMGUI_API ImGuiContext* igCreateContext(ImFontAtlas* shared_font_atlas);
CIMGUI_API void              igDestroyContext(ImGuiContext* ctx);
CIMGUI_API ImGuiContext* igGetCurrentContext();
CIMGUI_API void              igSetCurrentContext(ImGuiContext* ctx);

CIMGUI_API bool              igBegin(const char* name, bool* p_open, ImGuiWindowFlags flags);
CIMGUI_API void              igEnd();

CIMGUI_API bool              igBeginChild(const char* str_id, struct ImVec2 size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags);
CIMGUI_API bool              igBeginChildID(ImGuiID id, struct ImVec2 size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags);
CIMGUI_API void              igEndChild();

CIMGUI_API bool              igIsWindowAppearing();
CIMGUI_API bool              igIsWindowCollapsed();
CIMGUI_API bool              igIsWindowFocused(ImGuiFocusedFlags flags);
CIMGUI_API bool              igIsWindowHovered(ImGuiHoveredFlags flags);
CIMGUI_API ImDrawList* igGetWindowDrawList();
CIMGUI_API float             igGetWindowWidth();
CIMGUI_API float             igGetWindowHeight();
CIMGUI_API struct ImVec2     igGetWindowPos();
CIMGUI_API struct ImVec2     igGetWindowSize();
CIMGUI_API void              igSetWindowPosVec2(struct ImVec2 pos, ImGuiCond cond);
CIMGUI_API void              igSetWindowSizeVec2(struct ImVec2 size, ImGuiCond cond);
CIMGUI_API void              igSetWindowCollapsedBool(bool collapsed, ImGuiCond cond);
CIMGUI_API void              igSetWindowFocus();
CIMGUI_API void              igSetWindowFontScale(float scale);
CIMGUI_API void              igSetNextWindowPos(struct ImVec2 pos, ImGuiCond cond, struct ImVec2 pivot);
CIMGUI_API void              igSetNextWindowSize(struct ImVec2 size, ImGuiCond cond);
CIMGUI_API void              igSetNextWindowContentSize(struct ImVec2 size);
CIMGUI_API void              igSetNextWindowCollapsed(bool collapsed, ImGuiCond cond);
CIMGUI_API void              igSetNextWindowFocus();
CIMGUI_API void              igSetNextWindowBgAlpha(float alpha);
CIMGUI_API void              igSetNextWindowSizeConstraints(struct ImVec2 size_min, struct ImVec2 size_max, ImGuiSizeCallback custom_callback, void* custom_callback_data);

CIMGUI_API void              igGetContentRegionAvail(struct ImVec2* pOut);
CIMGUI_API void              igGetWindowContentRegionMin(struct ImVec2* pOut);
CIMGUI_API void              igGetWindowContentRegionMax(struct ImVec2* pOut);

CIMGUI_API float             igGetScrollX();
CIMGUI_API float             igGetScrollY();
CIMGUI_API float             igGetScrollMaxX();
CIMGUI_API float             igGetScrollMaxY();
CIMGUI_API void              igSetScrollX(float scroll_x);
CIMGUI_API void              igSetScrollY(float scroll_y);
CIMGUI_API void              igSetScrollHereY(float center_y_ratio);
CIMGUI_API void              igSetScrollFromPosY(float local_y, float center_y_ratio);

CIMGUI_API void              igPushFont(ImFont* font);
CIMGUI_API void              igPopFont();
CIMGUI_API void              igPushStyleColorU32(ImGuiCol idx, ImU32 col);
CIMGUI_API void              igPushStyleColorVec4(ImGuiCol idx, struct ImVec4 col);
CIMGUI_API void              igPopStyleColor(int count);
CIMGUI_API void              igPushStyleVarFloat(ImGuiStyleVar idx, float val);
CIMGUI_API void              igPushStyleVarVec2(ImGuiStyleVar idx, struct ImVec2 val);
CIMGUI_API void              igPopStyleVar(int count);
CIMGUI_API const struct ImVec4* igGetStyleColorVec4(ImGuiCol idx);
CIMGUI_API ImFont* igGetFont();
CIMGUI_API float             igGetFontSize();
CIMGUI_API void              igGetFontTexUvWhitePixel(struct ImVec2* pOut);
CIMGUI_API ImU32             igGetColorU32Col(ImGuiCol idx, float alpha_mul);
CIMGUI_API ImU32             igGetColorU32Vec4(struct ImVec4 in);
CIMGUI_API ImU32             igGetColorU32U32(ImU32 col);

CIMGUI_API void              igPushItemWidth(float item_width);
CIMGUI_API void              igPopItemWidth();
CIMGUI_API void              igSetNextItemWidth(float item_width);
CIMGUI_API float             igCalcItemWidth();
CIMGUI_API void              igPushTextWrapPos(float wrap_local_pos_x);
CIMGUI_API void              igPopTextWrapPos();

CIMGUI_API void              igPushItemFlag(int option, bool enabled);
CIMGUI_API void              igPopItemFlag();

CIMGUI_API void              igSeparator();
CIMGUI_API void              igSameLine(float offset_from_start_x, float spacing);
CIMGUI_API void              igNewLine();
CIMGUI_API void              igSpacing();
CIMGUI_API void              igDummy(struct ImVec2 size);
CIMGUI_API void              igIndent(float indent_w);
CIMGUI_API void              igUnindent(float indent_w);
CIMGUI_API void              igBeginGroup();
CIMGUI_API void              igEndGroup();
CIMGUI_API void              igGetCursorPos(struct ImVec2* pOut);
CIMGUI_API float             igGetCursorPosX();
CIMGUI_API float             igGetCursorPosY();
CIMGUI_API void              igSetCursorPos(struct ImVec2 local_pos);
CIMGUI_API void              igSetCursorPosX(float local_x);
CIMGUI_API void              igSetCursorPosY(float local_y);
CIMGUI_API void              igGetCursorStartPos(struct ImVec2* pOut);
CIMGUI_API void              igGetCursorScreenPos(struct ImVec2* pOut);
CIMGUI_API void              igSetCursorScreenPos(struct ImVec2 pos);
CIMGUI_API void              igAlignTextToFramePadding();
CIMGUI_API float             igGetTextLineHeight();
CIMGUI_API float             igGetTextLineHeightWithSpacing();
CIMGUI_API float             igGetFrameHeight();
CIMGUI_API float             igGetFrameHeightWithSpacing();

CIMGUI_API void              igPushIDStr(const char* str_id);
CIMGUI_API void              igPushIDStrRange(const char* str_id_begin, const char* str_id_end);
CIMGUI_API void              igPushIDPtr(const void* ptr_id);
CIMGUI_API void              igPushIDInt(int int_id);
CIMGUI_API void              igPopID();
CIMGUI_API ImGuiID           igGetIDStr(const char* str_id);
CIMGUI_API ImGuiID           igGetIDStrRange(const char* str_id_begin, const char* str_id_end);
CIMGUI_API ImGuiID           igGetIDPtr(const void* ptr_id);

CIMGUI_API void              igTextUnformatted(const char* text, const char* text_end);
CIMGUI_API void              igText(const char* fmt, ...);
CIMGUI_API void              igTextV(const char* fmt, va_list args);
CIMGUI_API void              igTextColored(struct ImVec4 col, const char* fmt, ...);
CIMGUI_API void              igTextColoredV(struct ImVec4 col, const char* fmt, va_list args);
CIMGUI_API void              igTextDisabled(const char* fmt, ...);
CIMGUI_API void              igTextDisabledV(const char* fmt, va_list args);
CIMGUI_API void              igTextWrapped(const char* fmt, ...);
CIMGUI_API void              igTextWrappedV(const char* fmt, va_list args);
CIMGUI_API void              igLabelText(const char* label, const char* fmt, ...);
CIMGUI_API void              igLabelTextV(const char* label, const char* fmt, va_list args);
CIMGUI_API void              igBulletText(const char* fmt, ...);
CIMGUI_API void              igBulletTextV(const char* fmt, va_list args);

CIMGUI_API bool              igButton(const char* label, struct ImVec2 size);
CIMGUI_API bool              igSmallButton(const char* label);
CIMGUI_API bool              igInvisibleButton(const char* str_id, struct ImVec2 size, int flags);
CIMGUI_API bool              igArrowButton(const char* str_id, ImGuiDir dir);
CIMGUI_API void              igImage(ImTextureID user_texture_id, struct ImVec2 size, struct ImVec2 uv0, struct ImVec2 uv1, struct ImVec4 tint_col, struct ImVec4 border_col);
CIMGUI_API bool              igImageButton(const char* str_id, ImTextureID user_texture_id, struct ImVec2 size, struct ImVec2 uv0, struct ImVec2 uv1, struct ImVec4 bg_col, struct ImVec4 tint_col);
CIMGUI_API bool              igCheckbox(const char* label, bool* v);
CIMGUI_API bool              igCheckboxFlagsIntPtr(const char* label, int* flags, int flags_value);
CIMGUI_API bool              igCheckboxFlagsUintPtr(const char* label, unsigned int* flags, unsigned int flags_value);
CIMGUI_API bool              igRadioButtonBool(const char* label, bool active);
CIMGUI_API bool              igRadioButtonIntPtr(const char* label, int* v, int v_button);
CIMGUI_API void              igProgressBar(float fraction, struct ImVec2 size_arg, const char* overlay);
CIMGUI_API void              igBullet();

CIMGUI_API bool              igBeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags);
CIMGUI_API void              igEndCombo();
CIMGUI_API bool              igCombo(const char* label, int* current_item, const char* const items[], int items_count, int popup_max_height_in_items);
CIMGUI_API bool              igComboStr(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items);
CIMGUI_API bool              igComboFnPtr(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int popup_max_height_in_items);

CIMGUI_API bool              igDragFloat(const char* label, float* v, float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igDragFloat2(const char* label, float v[2], float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igDragFloat3(const char* label, float v[3], float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igDragFloat4(const char* label, float v[4], float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igDragFloatRange2(const char* label, float* v_current_min, float* v_current_max, float v_speed, float v_min, float v_max, const char* format, const char* format_max, ImGuiSliderFlags flags);
CIMGUI_API bool              igDragInt(const char* label, int* v, float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igDragInt2(const char* label, int v[2], float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igDragInt3(const char* label, int v[3], float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igDragInt4(const char* label, int v[4], float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igDragIntRange2(const char* label, int* v_current_min, int* v_current_max, float v_speed, int v_min, int v_max, const char* format, const char* format_max, ImGuiSliderFlags flags);
CIMGUI_API bool              igDragScalar(const char* label, int data_type, void* p_data, float v_speed, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igDragScalarN(const char* label, int data_type, void* p_data, int components, float v_speed, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags);

CIMGUI_API bool              igSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igSliderFloat2(const char* label, float v[2], float v_min, float v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igSliderFloat3(const char* label, float v[3], float v_min, float v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igSliderFloat4(const char* label, float v[4], float v_min, float v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igSliderAngle(const char* label, float* v_rad, float v_degrees_min, float v_degrees_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igSliderInt(const char* label, int* v, int v_min, int v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igSliderInt2(const char* label, int v[2], int v_min, int v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igSliderInt3(const char* label, int v[3], int v_min, int v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igSliderInt4(const char* label, int v[4], int v_min, int v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igSliderScalar(const char* label, int data_type, void* p_data, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igSliderScalarN(const char* label, int data_type, void* p_data, int components, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igVSliderFloat(const char* label, struct ImVec2 size, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igVSliderInt(const char* label, struct ImVec2 size, int* v, int v_min, int v_max, const char* format, ImGuiSliderFlags flags);
CIMGUI_API bool              igVSliderScalar(const char* label, struct ImVec2 size, int data_type, void* p_data, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags);

CIMGUI_API bool              igInputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data);
CIMGUI_API bool              igInputTextMultiline(const char* label, char* buf, size_t buf_size, struct ImVec2 size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data);
CIMGUI_API bool              igInputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data);
CIMGUI_API bool              igInputFloat(const char* label, float* v, float step, float step_fast, const char* format, ImGuiInputTextFlags flags);
CIMGUI_API bool              igInputFloat2(const char* label, float v[2], const char* format, ImGuiInputTextFlags flags);
CIMGUI_API bool              igInputFloat3(const char* label, float v[3], const char* format, ImGuiInputTextFlags flags);
CIMGUI_API bool              igInputFloat4(const char* label, float v[4], const char* format, ImGuiInputTextFlags flags);
CIMGUI_API bool              igInputInt(const char* label, int* v, int step, int step_fast, ImGuiInputTextFlags flags);
CIMGUI_API bool              igInputInt2(const char* label, int v[2], ImGuiInputTextFlags flags);
CIMGUI_API bool              igInputInt3(const char* label, int v[3], ImGuiInputTextFlags flags);
CIMGUI_API bool              igInputInt4(const char* label, int v[4], ImGuiInputTextFlags flags);
CIMGUI_API bool              igInputDouble(const char* label, double* v, double step, double step_fast, const char* format, ImGuiInputTextFlags flags);
CIMGUI_API bool              igInputScalar(const char* label, int data_type, void* p_data, const void* p_step, const void* p_step_fast, const char* format, ImGuiInputTextFlags flags);
CIMGUI_API bool              igInputScalarN(const char* label, int data_type, void* p_data, int components, const void* p_step, const void* p_step_fast, const char* format, ImGuiInputTextFlags flags);

CIMGUI_API bool              igColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags);
CIMGUI_API bool              igColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags);
CIMGUI_API bool              igColorPicker3(const char* label, float col[3], ImGuiColorEditFlags flags);
CIMGUI_API bool              igColorPicker4(const char* label, float col[4], ImGuiColorEditFlags flags, const float* ref_col);
CIMGUI_API bool              igColorButton(const char* desc_id, struct ImVec4 col, ImGuiColorEditFlags flags, struct ImVec2 size);
CIMGUI_API void              igSetColorEditOptions(ImGuiColorEditFlags flags);

CIMGUI_API bool              igTreeNodeStr(const char* label);
CIMGUI_API bool              igTreeNodeStrStr(const char* str_id, const char* fmt, ...);
CIMGUI_API bool              igTreeNodePtr(const void* ptr_id, const char* fmt, ...);
CIMGUI_API bool              igTreeNodeV(const char* str_id, const char* fmt, va_list args);
CIMGUI_API bool              igTreeNodeVPtr(const void* ptr_id, const char* fmt, va_list args);
CIMGUI_API bool              igTreeNodeExStr(const char* label, ImGuiTreeNodeFlags flags);
CIMGUI_API bool              igTreeNodeExStrStr(const char* str_id, ImGuiTreeNodeFlags flags, const char* fmt, ...);
CIMGUI_API bool              igTreeNodeExPtr(const void* ptr_id, ImGuiTreeNodeFlags flags, const char* fmt, ...);
CIMGUI_API bool              igTreeNodeExV(const char* str_id, ImGuiTreeNodeFlags flags, const char* fmt, va_list args);
CIMGUI_API bool              igTreeNodeExVPtr(const void* ptr_id, ImGuiTreeNodeFlags flags, const char* fmt, va_list args);
CIMGUI_API void              igTreePushStr(const char* str_id);
CIMGUI_API void              igTreePushPtr(const void* ptr_id);
CIMGUI_API void              igTreePop();
CIMGUI_API float             igGetTreeNodeToLabelSpacing();
CIMGUI_API void              igSetNextItemOpen(bool is_open, ImGuiCond cond);
CIMGUI_API bool              igCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags);
CIMGUI_API bool              igCollapsingHeaderBoolPtr(const char* label, bool* p_open, ImGuiTreeNodeFlags flags);

CIMGUI_API bool              igSelectable(const char* label, bool selected, ImGuiSelectableFlags flags, struct ImVec2 size);
CIMGUI_API bool              igSelectableBoolPtr(const char* label, bool* p_selected, ImGuiSelectableFlags flags, struct ImVec2 size);

CIMGUI_API bool              igBeginListBox(const char* label, struct ImVec2 size);
CIMGUI_API void              igEndListBox();
CIMGUI_API bool              igListBoxStr_arr(const char* label, int* current_item, const char* const items[], int items_count, int height_in_items);
CIMGUI_API bool              igListBoxFnPtr(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int height_in_items);

CIMGUI_API void              igPlotLines(const char* label, const float* values, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, struct ImVec2 graph_size, int stride);
CIMGUI_API void              igPlotLinesFnPtr(const char* label, float (*values_getter)(void* data, int idx), void* data, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, struct ImVec2 graph_size);
CIMGUI_API void              igPlotHistogramFloatPtr(const char* label, const float* values, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, struct ImVec2 graph_size, int stride);
CIMGUI_API void              igPlotHistogramFnPtr(const char* label, float (*values_getter)(void* data, int idx), void* data, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, struct ImVec2 graph_size);

CIMGUI_API void              igValueBool(const char* prefix, bool b);
CIMGUI_API void              igValueInt(const char* prefix, int v);
CIMGUI_API void              igValueUint(const char* prefix, unsigned int v);
CIMGUI_API void              igValueFloat(const char* prefix, float v, const char* float_format);

CIMGUI_API bool              igBeginMenuBar();
CIMGUI_API void              igEndMenuBar();
CIMGUI_API bool              igBeginMainMenuBar();
CIMGUI_API void              igEndMainMenuBar();
CIMGUI_API bool              igBeginMenu(const char* label, bool enabled);
CIMGUI_API void              igEndMenu();
CIMGUI_API bool              igMenuItemBool(const char* label, const char* shortcut, bool selected, bool enabled);
CIMGUI_API bool              igMenuItemBoolPtr(const char* label, const char* shortcut, bool* p_selected, bool enabled);

CIMGUI_API bool              igBeginTooltip();
CIMGUI_API void              igEndTooltip();
CIMGUI_API void              igSetTooltip(const char* fmt, ...);
CIMGUI_API void              igSetTooltipV(const char* fmt, va_list args);

CIMGUI_API bool              igBeginPopup(const char* str_id, ImGuiWindowFlags flags);
CIMGUI_API bool              igBeginPopupModal(const char* name, bool* p_open, ImGuiWindowFlags flags);
CIMGUI_API void              igEndPopup();
CIMGUI_API void              igOpenPopup(const char* str_id, ImGuiPopupFlags popup_flags);
CIMGUI_API void              igOpenPopupOnItemClick(const char* str_id, ImGuiPopupFlags popup_flags);
CIMGUI_API void              igCloseCurrentPopup();
CIMGUI_API bool              igBeginPopupContextItem(const char* str_id, ImGuiPopupFlags popup_flags);
CIMGUI_API bool              igBeginPopupContextWindow(const char* str_id, ImGuiPopupFlags popup_flags);
CIMGUI_API bool              igBeginPopupContextVoid(const char* str_id, ImGuiPopupFlags popup_flags);
CIMGUI_API bool              igIsPopupOpenStr(const char* str_id, ImGuiPopupFlags flags);

CIMGUI_API void              igColumns(int count, const char* id, bool border);
CIMGUI_API void              igNextColumn();
CIMGUI_API int               igGetColumnIndex();
CIMGUI_API float             igGetColumnWidth(int column_index);
CIMGUI_API void              igSetColumnWidth(int column_index, float width);
CIMGUI_API float             igGetColumnOffset(int column_index);
CIMGUI_API void              igSetColumnOffset(int column_index, float offset_x);
CIMGUI_API int               igGetColumnsCount();

CIMGUI_API bool              igBeginTabBar(const char* str_id, ImGuiTabBarFlags flags);
CIMGUI_API void              igEndTabBar();
CIMGUI_API bool              igBeginTabItem(const char* label, bool* p_open, ImGuiTabItemFlags flags);
CIMGUI_API void              igEndTabItem();
CIMGUI_API bool              igTabItemButton(const char* label, ImGuiTabItemFlags flags);
CIMGUI_API void              igSetTabItemClosed(const char* tab_or_docked_window_label);

CIMGUI_API void              igLogToTTY(int auto_open_depth);
CIMGUI_API void              igLogToFile(int auto_open_depth, const char* filename);
CIMGUI_API void              igLogToClipboard(int auto_open_depth);
CIMGUI_API void              igLogFinish();
CIMGUI_API void              igLogButtons();
CIMGUI_API void              igLogText(const char* fmt, ...);

CIMGUI_API bool              igBeginDragDropSource(ImGuiDragDropFlags flags);
CIMGUI_API bool              igSetDragDropPayload(const char* type, const void* data, size_t sz, ImGuiCond cond);
CIMGUI_API void              igEndDragDropSource();
CIMGUI_API bool              igBeginDragDropTarget();
CIMGUI_API const ImGuiPayload* igAcceptDragDropPayload(const char* type, ImGuiDragDropFlags flags);
CIMGUI_API void              igEndDragDropTarget();
CIMGUI_API const ImGuiPayload* igGetDragDropPayload();

CIMGUI_API void              igPushClipRect(struct ImVec2 clip_rect_min, struct ImVec2 clip_rect_max, bool intersect_with_current_clip_rect);
CIMGUI_API void              igPopClipRect();

CIMGUI_API void              igSetItemDefaultFocus();
CIMGUI_API void              igSetKeyboardFocusHere(int offset);

CIMGUI_API bool              igIsItemHovered(ImGuiHoveredFlags flags);
CIMGUI_API bool              igIsItemActive();
CIMGUI_API bool              igIsItemFocused();
CIMGUI_API bool              igIsItemClicked(ImGuiMouseButton mouse_button);
CIMGUI_API bool              igIsItemVisible();
CIMGUI_API bool              igIsItemEdited();
CIMGUI_API bool              igIsItemActivated();
CIMGUI_API bool              igIsItemDeactivated();
CIMGUI_API bool              igIsItemDeactivatedAfterEdit();
CIMGUI_API bool              igIsItemToggledOpen();
CIMGUI_API bool              igIsAnyItemHovered();
CIMGUI_API bool              igIsAnyItemActive();
CIMGUI_API bool              igIsAnyItemFocused();
CIMGUI_API void              igGetItemRectMin(struct ImVec2* pOut);
CIMGUI_API void              igGetItemRectMax(struct ImVec2* pOut);
CIMGUI_API void              igGetItemRectSize(struct ImVec2* pOut);
CIMGUI_API void              igSetItemAllowOverlap();

CIMGUI_API struct ImGuiViewport* igGetMainViewport();
CIMGUI_API ImDrawList* igGetBackgroundDrawList();
CIMGUI_API ImDrawList* igGetForegroundDrawList();

CIMGUI_API bool              igIsRectVisible(struct ImVec2 size);
CIMGUI_API bool              igIsRectVisibleVec2(struct ImVec2 rect_min, struct ImVec2 rect_max);
CIMGUI_API double            igGetTime();
CIMGUI_API int               igGetFrameCount();
CIMGUI_API void* igGetDrawListSharedData(); // ImDrawListSharedData*
CIMGUI_API const char* igGetStyleColorName(ImGuiCol idx);

CIMGUI_API void              igSetStateStorage(ImGuiStorage* storage);
CIMGUI_API ImGuiStorage* igGetStateStorage();

CIMGUI_API bool              igBeginChildFrame(ImGuiID id, struct ImVec2 size, ImGuiWindowFlags flags);
CIMGUI_API void              igEndChildFrame();

CIMGUI_API void              igCalcTextSize(struct ImVec2* pOut, const char* text, const char* text_end, bool hide_text_after_double_hash, float wrap_width);
CIMGUI_API void              igColorConvertU32ToFloat4(struct ImVec4* pOut, ImU32 in);
CIMGUI_API ImU32             igColorConvertFloat4ToU32(struct ImVec4 in);
CIMGUI_API void              igColorConvertRGBtoHSV(float r, float g, float b, float* out_h, float* out_s, float* out_v);
CIMGUI_API void              igColorConvertHSVtoRGB(float h, float s, float v, float* out_r, float* out_g, float* out_b);

CIMGUI_API bool              igIsKeyDown(ImGuiKey key);
CIMGUI_API bool              igIsKeyPressed(ImGuiKey key, bool repeat);
CIMGUI_API bool              igIsKeyReleased(ImGuiKey key);
CIMGUI_API int               igGetKeyPressedAmount(ImGuiKey key, float repeat_delay, float rate);
CIMGUI_API const char* igGetKeyName(ImGuiKey key);

CIMGUI_API bool              igIsMouseDown(ImGuiMouseButton button);
CIMGUI_API bool              igIsMouseClicked(ImGuiMouseButton button, bool repeat);
CIMGUI_API bool              igIsMouseReleased(ImGuiMouseButton button);
CIMGUI_API bool              igIsMouseDoubleClicked(ImGuiMouseButton button);
CIMGUI_API int               igGetMouseClickedCount(ImGuiMouseButton button);
CIMGUI_API bool              igIsMouseHoveringRect(struct ImVec2 r_min, struct ImVec2 r_max, bool clip);
CIMGUI_API bool              igIsMousePosValid(const struct ImVec2* mouse_pos);
CIMGUI_API bool              igIsAnyMouseDown();
CIMGUI_API void              igGetMousePos(struct ImVec2* pOut);
CIMGUI_API void              igGetMousePosOnOpeningCurrentPopup(struct ImVec2* pOut);
CIMGUI_API bool              igIsMouseDragging(ImGuiMouseButton button, float lock_threshold);
CIMGUI_API void              igGetMouseDragDelta(struct ImVec2* pOut, ImGuiMouseButton button, float lock_threshold);
CIMGUI_API void              igResetMouseDragDelta(ImGuiMouseButton button);
CIMGUI_API ImGuiMouseCursor  igGetMouseCursor();
CIMGUI_API void              igSetMouseCursor(ImGuiMouseCursor cursor_type);

CIMGUI_API const char* igGetClipboardText();
CIMGUI_API void              igSetClipboardText(const char* text);

CIMGUI_API void              igLoadIniSettingsFromDisk(const char* ini_filename);
CIMGUI_API void              igLoadIniSettingsFromMemory(const char* ini_data, size_t ini_size);
CIMGUI_API void              igSaveIniSettingsToDisk(const char* ini_filename);
CIMGUI_API const char* igSaveIniSettingsToMemory(size_t* out_ini_size);

CIMGUI_API void* igMemAlloc(size_t size);
CIMGUI_API void              igMemFree(void* ptr);

// ImGuiTextFilter
CIMGUI_API void ImGuiTextFilter_Create(struct ImGuiTextFilter* filter, const char* default_filter);
CIMGUI_API void ImGuiTextFilter_Destroy(struct ImGuiTextFilter* filter);
CIMGUI_API bool ImGuiTextFilter_Draw(struct ImGuiTextFilter* filter, const char* label, float width);
CIMGUI_API bool ImGuiTextFilter_PassFilter(struct ImGuiTextFilter* filter, const char* text, const char* text_end);
CIMGUI_API void ImGuiTextFilter_Build(struct ImGuiTextFilter* filter);
CIMGUI_API void ImGuiTextFilter_Clear(struct ImGuiTextFilter* filter);
CIMGUI_API bool ImGuiTextFilter_IsActive(struct ImGuiTextFilter* filter);

// ImGuiTextBuffer
CIMGUI_API void ImGuiTextBuffer_Create(struct ImGuiTextBuffer* buffer);
CIMGUI_API void ImGuiTextBuffer_Destroy(struct ImGuiTextBuffer* buffer);
CIMGUI_API const char* ImGuiTextBuffer_begin(struct ImGuiTextBuffer* buffer);
CIMGUI_API const char* ImGuiTextBuffer_end(struct ImGuiTextBuffer* buffer);
CIMGUI_API int ImGuiTextBuffer_size(struct ImGuiTextBuffer* buffer);
CIMGUI_API bool ImGuiTextBuffer_empty(struct ImGuiTextBuffer* buffer);
CIMGUI_API void ImGuiTextBuffer_clear(struct ImGuiTextBuffer* buffer);
CIMGUI_API const char* ImGuiTextBuffer_c_str(struct ImGuiTextBuffer* buffer);
CIMGUI_API void ImGuiTextBuffer_append(struct ImGuiTextBuffer* buffer, const char* str, const char* str_end);

// ImGuiStorage
CIMGUI_API void ImGuiStorage_BuildSortByKey(struct ImGuiStorage* storage);

// ImGuiListClipper
CIMGUI_API void ImGuiListClipper_Create(struct ImGuiListClipper* clipper);
CIMGUI_API void ImGuiListClipper_Destroy(struct ImGuiListClipper* clipper);
CIMGUI_API void ImGuiListClipper_Begin(struct ImGuiListClipper* clipper, int items_count, float items_height);
CIMGUI_API void ImGuiListClipper_End(struct ImGuiListClipper* clipper);
CIMGUI_API bool ImGuiListClipper_Step(struct ImGuiListClipper* clipper);

// ImGui_ImplSDL3
CIMGUI_API bool ImGui_ImplSDL3_InitForOpenGL(void* window, void* gl_context);
CIMGUI_API bool ImGui_ImplSDL3_InitForVulkan(void* window);
CIMGUI_API bool ImGui_ImplSDL3_InitForD3D(void* window);
CIMGUI_API bool ImGui_ImplSDL3_InitForMetal(void* window);
CIMGUI_API bool ImGui_ImplSDL3_InitForSDLRenderer(void* window, void* renderer);
CIMGUI_API void cImGui_ImplSDL3_Shutdown();
CIMGUI_API void cImGui_ImplSDL3_NewFrame();
CIMGUI_API bool ImGui_ImplSDL3_ProcessEvent(const void* event);

// ImGui_ImplSDLRenderer3
CIMGUI_API bool ImGui_ImplSDLRenderer3_Init(void* renderer);
CIMGUI_API void cImGui_ImplSDLRenderer3_Shutdown();
CIMGUI_API void cImGui_ImplSDLRenderer3_NewFrame();
CIMGUI_API void ImGui_ImplSDLRenderer3_RenderDrawData(ImDrawData* draw_data, void* renderer);
CIMGUI_API void cImGui_ImplSDLRenderer3_CreateDeviceObjects();
CIMGUI_API void cImGui_ImplSDLRenderer3_DestroyDeviceObjects();

