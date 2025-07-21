// cimgui.cpp (Updated for modern Dear ImGui)
// This is a modified version of cimgui.cpp to be compatible with the updated cimgui.h
// and a newer version of Dear ImGui. Obsolete functions have been removed or updated.

#include "imgui.h"
#include "cimgui.h"

// to use placement new
#define IMGUI_DEFINE_PLACEMENT_NEW
#include "imgui_internal.h"

#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

CIMGUI_API ImGuiIO* igGetIO()
{
    return &ImGui::GetIO();
}

CIMGUI_API ImGuiStyle* igGetStyle()
{
    return &ImGui::GetStyle();
}

CIMGUI_API ImDrawData* igGetDrawData()
{
    return ImGui::GetDrawData();
}

CIMGUI_API void igNewFrame()
{
    ImGui::NewFrame();
}

CIMGUI_API void igRender()
{
    ImGui::Render();
}

CIMGUI_API void igEndFrame()
{
    ImGui::EndFrame();
}

CIMGUI_API void igShowDemoWindow(bool* p_open)
{
    ImGui::ShowDemoWindow(p_open);
}

CIMGUI_API void igShowMetricsWindow(bool* p_open)
{
    ImGui::ShowMetricsWindow(p_open);
}

CIMGUI_API void igShowStyleEditor(ImGuiStyle* ref)
{
    ImGui::ShowStyleEditor(ref);
}

CIMGUI_API bool igShowStyleSelector(const char* label)
{
    return ImGui::ShowStyleSelector(label);
}

CIMGUI_API void igShowFontSelector(const char* label)
{
    ImGui::ShowFontSelector(label);
}

CIMGUI_API void igShowUserGuide()
{
    ImGui::ShowUserGuide();
}

CIMGUI_API const char* igGetVersion()
{
    return ImGui::GetVersion();
}

CIMGUI_API bool igBegin(const char* name, bool* p_open, ImGuiWindowFlags flags)
{
    return ImGui::Begin(name, p_open, flags);
}

CIMGUI_API void igEnd()
{
    ImGui::End();
}

CIMGUI_API bool igBeginChild(const char* str_id, const ImVec2 size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
{
    return ImGui::BeginChild(str_id, size, child_flags, window_flags);
}

CIMGUI_API bool igBeginChildID(ImGuiID id, const ImVec2 size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
{
    return ImGui::BeginChild(id, size, child_flags, window_flags);
}

CIMGUI_API void igEndChild()
{
    ImGui::EndChild();
}

CIMGUI_API bool igIsWindowAppearing()
{
    return ImGui::IsWindowAppearing();
}

CIMGUI_API bool igIsWindowCollapsed()
{
    return ImGui::IsWindowCollapsed();
}

CIMGUI_API bool igIsWindowFocused(ImGuiFocusedFlags flags)
{
    return ImGui::IsWindowFocused(flags);
}

CIMGUI_API bool igIsWindowHovered(ImGuiHoveredFlags flags)
{
    return ImGui::IsWindowHovered(flags);
}

CIMGUI_API ImDrawList* igGetWindowDrawList()
{
    return ImGui::GetWindowDrawList();
}

CIMGUI_API ImVec2 igGetWindowPos()
{
    return ImGui::GetWindowPos();
}

CIMGUI_API ImVec2 igGetWindowSize()
{
    return ImGui::GetWindowSize();
}

CIMGUI_API float igGetWindowWidth()
{
    return ImGui::GetWindowWidth();
}

CIMGUI_API float igGetWindowHeight()
{
    return ImGui::GetWindowHeight();
}

CIMGUI_API void igSetNextWindowPos(const ImVec2 pos, ImGuiCond cond, const ImVec2 pivot)
{
    ImGui::SetNextWindowPos(pos, cond, pivot);
}

CIMGUI_API void igSetNextWindowSize(const ImVec2 size, ImGuiCond cond)
{
    ImGui::SetNextWindowSize(size, cond);
}

CIMGUI_API void igSetNextWindowSizeConstraints(const ImVec2 size_min, const ImVec2 size_max, ImGuiSizeCallback custom_callback, void* custom_callback_data)
{
    ImGui::SetNextWindowSizeConstraints(size_min, size_max, custom_callback, custom_callback_data);
}

CIMGUI_API void igSetNextWindowContentSize(const ImVec2 size)
{
    ImGui::SetNextWindowContentSize(size);
}

CIMGUI_API void igSetNextWindowCollapsed(bool collapsed, ImGuiCond cond)
{
    ImGui::SetNextWindowCollapsed(collapsed, cond);
}

CIMGUI_API void igSetNextWindowFocus()
{
    ImGui::SetNextWindowFocus();
}

CIMGUI_API void igSetNextWindowBgAlpha(float alpha)
{
    ImGui::SetNextWindowBgAlpha(alpha);
}

CIMGUI_API void igSetWindowPosVec2(const ImVec2 pos, ImGuiCond cond)
{
    ImGui::SetWindowPos(pos, cond);
}

CIMGUI_API void igSetWindowSizeVec2(const ImVec2 size, ImGuiCond cond)
{
    ImGui::SetWindowSize(size, cond);
}

CIMGUI_API void igSetWindowCollapsedBool(bool collapsed, ImGuiCond cond)
{
    ImGui::SetWindowCollapsed(collapsed, cond);
}

CIMGUI_API void igSetWindowFocus()
{
    ImGui::SetWindowFocus();
}

CIMGUI_API void igSetWindowFontScale(float scale)
{
    ImGui::SetWindowFontScale(scale);
}

CIMGUI_API void igGetContentRegionAvail(ImVec2* pOut)
{
    *pOut = ImGui::GetContentRegionAvail();
}

CIMGUI_API void igGetWindowContentRegionMin(ImVec2* pOut)
{
    *pOut = ImGui::GetWindowContentRegionMin();
}

CIMGUI_API void igGetWindowContentRegionMax(ImVec2* pOut)
{
    *pOut = ImGui::GetWindowContentRegionMax();
}

CIMGUI_API float igGetScrollX()
{
    return ImGui::GetScrollX();
}

CIMGUI_API float igGetScrollY()
{
    return ImGui::GetScrollY();
}

CIMGUI_API float igGetScrollMaxX()
{
    return ImGui::GetScrollMaxX();
}

CIMGUI_API float igGetScrollMaxY()
{
    return ImGui::GetScrollMaxY();
}

CIMGUI_API void igSetScrollX(float scroll_x)
{
    ImGui::SetScrollX(scroll_x);
}

CIMGUI_API void igSetScrollY(float scroll_y)
{
    ImGui::SetScrollY(scroll_y);
}

CIMGUI_API void igSetScrollHereY(float center_y_ratio)
{
    ImGui::SetScrollHereY(center_y_ratio);
}

CIMGUI_API void igSetScrollFromPosY(float local_y, float center_y_ratio)
{
    ImGui::SetScrollFromPosY(local_y, center_y_ratio);
}

CIMGUI_API void igPushFont(ImFont* font)
{
    ImGui::PushFont(font);
}

CIMGUI_API void igPopFont()
{
    ImGui::PopFont();
}

CIMGUI_API void igPushStyleColorU32(ImGuiCol idx, ImU32 col)
{
    ImGui::PushStyleColor(idx, col);
}

CIMGUI_API void igPushStyleColorVec4(ImGuiCol idx, ImVec4 col)
{
    ImGui::PushStyleColor(idx, col);
}

CIMGUI_API void igPopStyleColor(int count)
{
    ImGui::PopStyleColor(count);
}

CIMGUI_API void igPushStyleVarFloat(ImGuiStyleVar idx, float val)
{
    ImGui::PushStyleVar(idx, val);
}

CIMGUI_API void igPushStyleVarVec2(ImGuiStyleVar idx, const ImVec2 val)
{
    ImGui::PushStyleVar(idx, val);
}

CIMGUI_API void igPopStyleVar(int count)
{
    ImGui::PopStyleVar(count);
}

CIMGUI_API const ImVec4* igGetStyleColorVec4(ImGuiCol idx)
{
    return &ImGui::GetStyleColorVec4(idx);
}

CIMGUI_API ImFont* igGetFont()
{
    return ImGui::GetFont();
}

CIMGUI_API float igGetFontSize()
{
    return ImGui::GetFontSize();
}

CIMGUI_API void igGetFontTexUvWhitePixel(ImVec2* pOut)
{
    *pOut = ImGui::GetFontTexUvWhitePixel();
}

CIMGUI_API ImU32 igGetColorU32Col(ImGuiCol idx, float alpha_mul)
{
    return ImGui::GetColorU32(idx, alpha_mul);
}

CIMGUI_API ImU32 igGetColorU32Vec4(const ImVec4 col)
{
    return ImGui::GetColorU32(col);
}

CIMGUI_API ImU32 igGetColorU32U32(ImU32 col)
{
    return ImGui::GetColorU32(col);
}

CIMGUI_API void igPushItemWidth(float item_width)
{
    ImGui::PushItemWidth(item_width);
}

CIMGUI_API void igPopItemWidth()
{
    ImGui::PopItemWidth();
}

CIMGUI_API void igSetNextItemWidth(float item_width)
{
    ImGui::SetNextItemWidth(item_width);
}

CIMGUI_API float igCalcItemWidth()
{
    return ImGui::CalcItemWidth();
}

CIMGUI_API void igPushTextWrapPos(float wrap_local_pos_x)
{
    ImGui::PushTextWrapPos(wrap_local_pos_x);
}

CIMGUI_API void igPopTextWrapPos()
{
    ImGui::PopTextWrapPos();
}

CIMGUI_API void igSeparator()
{
    ImGui::Separator();
}

CIMGUI_API void igSameLine(float offset_from_start_x, float spacing)
{
    ImGui::SameLine(offset_from_start_x, spacing);
}

CIMGUI_API void igNewLine()
{
    ImGui::NewLine();
}

CIMGUI_API void igSpacing()
{
    ImGui::Spacing();
}

CIMGUI_API void igDummy(const ImVec2 size)
{
    ImGui::Dummy(size);
}

CIMGUI_API void igIndent(float indent_w)
{
    ImGui::Indent(indent_w);
}

CIMGUI_API void igUnindent(float indent_w)
{
    ImGui::Unindent(indent_w);
}

CIMGUI_API void igBeginGroup()
{
    ImGui::BeginGroup();
}

CIMGUI_API void igEndGroup()
{
    ImGui::EndGroup();
}

CIMGUI_API void igGetCursorPos(ImVec2* pOut)
{
    *pOut = ImGui::GetCursorPos();
}

CIMGUI_API float igGetCursorPosX()
{
    return ImGui::GetCursorPosX();
}

CIMGUI_API float igGetCursorPosY()
{
    return ImGui::GetCursorPosY();
}

CIMGUI_API void igSetCursorPos(const ImVec2 local_pos)
{
    ImGui::SetCursorPos(local_pos);
}

CIMGUI_API void igSetCursorPosX(float local_x)
{
    ImGui::SetCursorPosX(local_x);
}

CIMGUI_API void igSetCursorPosY(float local_y)
{
    ImGui::SetCursorPosY(local_y);
}

CIMGUI_API void igGetCursorStartPos(ImVec2* pOut)
{
    *pOut = ImGui::GetCursorStartPos();
}

CIMGUI_API void igGetCursorScreenPos(ImVec2* pOut)
{
    *pOut = ImGui::GetCursorScreenPos();
}

CIMGUI_API void igSetCursorScreenPos(const ImVec2 pos)
{
    ImGui::SetCursorScreenPos(pos);
}

CIMGUI_API void igAlignTextToFramePadding()
{
    ImGui::AlignTextToFramePadding();
}

CIMGUI_API float igGetTextLineHeight()
{
    return ImGui::GetTextLineHeight();
}

CIMGUI_API float igGetTextLineHeightWithSpacing()
{
    return ImGui::GetTextLineHeightWithSpacing();
}

CIMGUI_API float igGetFrameHeight()
{
    return ImGui::GetFrameHeight();
}

CIMGUI_API float igGetFrameHeightWithSpacing()
{
    return ImGui::GetFrameHeightWithSpacing();
}

CIMGUI_API void igPushIDStr(const char* str_id)
{
    ImGui::PushID(str_id);
}

CIMGUI_API void igPushIDStrRange(const char* str_id_begin, const char* str_id_end)
{
    ImGui::PushID(str_id_begin, str_id_end);
}

CIMGUI_API void igPushIDPtr(const void* ptr_id)
{
    ImGui::PushID(ptr_id);
}

CIMGUI_API void igPushIDInt(int int_id)
{
    ImGui::PushID(int_id);
}

CIMGUI_API void igPopID()
{
    ImGui::PopID();
}

CIMGUI_API ImGuiID igGetIDStr(const char* str_id)
{
    return ImGui::GetID(str_id);
}

CIMGUI_API ImGuiID igGetIDStrRange(const char* str_id_begin, const char* str_id_end)
{
    return ImGui::GetID(str_id_begin, str_id_end);
}

CIMGUI_API ImGuiID igGetIDPtr(const void* ptr_id)
{
    return ImGui::GetID(ptr_id);
}

CIMGUI_API void igTextUnformatted(const char* text, const char* text_end)
{
    ImGui::TextUnformatted(text, text_end);
}

CIMGUI_API void igText(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

CIMGUI_API void igTextV(const char* fmt, va_list args)
{
    ImGui::TextV(fmt, args);
}

CIMGUI_API void igTextColored(const ImVec4 col, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::TextColoredV(col, fmt, args);
    va_end(args);
}

CIMGUI_API void igTextColoredV(const ImVec4 col, const char* fmt, va_list args)
{
    ImGui::TextColoredV(col, fmt, args);
}

CIMGUI_API void igTextDisabled(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::TextDisabledV(fmt, args);
    va_end(args);
}

CIMGUI_API void igTextDisabledV(const char* fmt, va_list args)
{
    ImGui::TextDisabledV(fmt, args);
}

CIMGUI_API void igTextWrapped(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::TextWrappedV(fmt, args);
    va_end(args);
}

CIMGUI_API void igTextWrappedV(const char* fmt, va_list args)
{
    ImGui::TextWrappedV(fmt, args);
}

CIMGUI_API void igLabelText(const char* label, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::LabelTextV(label, fmt, args);
    va_end(args);
}

CIMGUI_API void igLabelTextV(const char* label, const char* fmt, va_list args)
{
    ImGui::LabelTextV(label, fmt, args);
}

CIMGUI_API void igBulletText(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::BulletTextV(fmt, args);
    va_end(args);
}

CIMGUI_API void igBulletTextV(const char* fmt, va_list args)
{
    ImGui::BulletTextV(fmt, args);
}

CIMGUI_API bool igButton(const char* label, const ImVec2 size)
{
    return ImGui::Button(label, size);
}

CIMGUI_API bool igSmallButton(const char* label)
{
    return ImGui::SmallButton(label);
}

CIMGUI_API bool igInvisibleButton(const char* str_id, const ImVec2 size, int flags)
{
    return ImGui::InvisibleButton(str_id, size, (ImGuiButtonFlags)flags);
}

CIMGUI_API bool igArrowButton(const char* str_id, ImGuiDir dir)
{
    return ImGui::ArrowButton(str_id, dir);
}

CIMGUI_API void igImage(ImTextureID user_texture_id, const ImVec2 size, const ImVec2 uv0, const ImVec2 uv1, const ImVec4 tint_col, const ImVec4 border_col)
{
    ImGui::Image(user_texture_id, size, uv0, uv1, tint_col, border_col);
}

CIMGUI_API bool igImageButton(const char* str_id, ImTextureID user_texture_id, const ImVec2 size, const ImVec2 uv0, const ImVec2 uv1, const ImVec4 bg_col, const ImVec4 tint_col)
{
    return ImGui::ImageButton(str_id, user_texture_id, size, uv0, uv1, bg_col, tint_col);
}

CIMGUI_API bool igCheckbox(const char* label, bool* v)
{
    return ImGui::Checkbox(label, v);
}

CIMGUI_API bool igCheckboxFlagsIntPtr(const char* label, int* flags, int flags_value)
{
    return ImGui::CheckboxFlags(label, flags, flags_value);
}

CIMGUI_API bool igCheckboxFlagsUintPtr(const char* label, unsigned int* flags, unsigned int flags_value)
{
    return ImGui::CheckboxFlags(label, flags, flags_value);
}

CIMGUI_API bool igRadioButtonBool(const char* label, bool active)
{
    return ImGui::RadioButton(label, active);
}

CIMGUI_API bool igRadioButtonIntPtr(const char* label, int* v, int v_button)
{
    return ImGui::RadioButton(label, v, v_button);
}

CIMGUI_API void igProgressBar(float fraction, const ImVec2 size_arg, const char* overlay)
{
    ImGui::ProgressBar(fraction, size_arg, overlay);
}

CIMGUI_API void igBullet()
{
    ImGui::Bullet();
}

CIMGUI_API bool igBeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags)
{
    return ImGui::BeginCombo(label, preview_value, flags);
}

CIMGUI_API void igEndCombo()
{
    ImGui::EndCombo();
}

CIMGUI_API bool igCombo(const char* label, int* current_item, const char* const items[], int items_count, int popup_max_height_in_items)
{
    return ImGui::Combo(label, current_item, items, items_count, popup_max_height_in_items);
}

CIMGUI_API bool igComboStr(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items)
{
    return ImGui::Combo(label, current_item, items_separated_by_zeros, popup_max_height_in_items);
}

CIMGUI_API bool igComboFnPtr(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int popup_max_height_in_items)
{
    return ImGui::Combo(label, current_item, getter, user_data, items_count, popup_max_height_in_items);
}

CIMGUI_API bool igDragFloat(const char* label, float* v, float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::DragFloat(label, v, v_speed, v_min, v_max, format, flags);
}

CIMGUI_API bool igDragFloat2(const char* label, float v[2], float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::DragFloat2(label, v, v_speed, v_min, v_max, format, flags);
}

CIMGUI_API bool igDragFloat3(const char* label, float v[3], float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::DragFloat3(label, v, v_speed, v_min, v_max, format, flags);
}

CIMGUI_API bool igDragFloat4(const char* label, float v[4], float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::DragFloat4(label, v, v_speed, v_min, v_max, format, flags);
}

CIMGUI_API bool igDragFloatRange2(const char* label, float* v_current_min, float* v_current_max, float v_speed, float v_min, float v_max, const char* format, const char* format_max, ImGuiSliderFlags flags)
{
    return ImGui::DragFloatRange2(label, v_current_min, v_current_max, v_speed, v_min, v_max, format, format_max, flags);
}

CIMGUI_API bool igDragInt(const char* label, int* v, float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::DragInt(label, v, v_speed, v_min, v_max, format, flags);
}

CIMGUI_API bool igDragInt2(const char* label, int v[2], float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::DragInt2(label, v, v_speed, v_min, v_max, format, flags);
}

CIMGUI_API bool igDragInt3(const char* label, int v[3], float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::DragInt3(label, v, v_speed, v_min, v_max, format, flags);
}

CIMGUI_API bool igDragInt4(const char* label, int v[4], float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::DragInt4(label, v, v_speed, v_min, v_max, format, flags);
}

CIMGUI_API bool igDragIntRange2(const char* label, int* v_current_min, int* v_current_max, float v_speed, int v_min, int v_max, const char* format, const char* format_max, ImGuiSliderFlags flags)
{
    return ImGui::DragIntRange2(label, v_current_min, v_current_max, v_speed, v_min, v_max, format, format_max, flags);
}

CIMGUI_API bool igDragScalar(const char* label, int data_type, void* p_data, float v_speed, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::DragScalar(label, (ImGuiDataType)data_type, p_data, v_speed, p_min, p_max, format, flags);
}

CIMGUI_API bool igDragScalarN(const char* label, int data_type, void* p_data, int components, float v_speed, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::DragScalarN(label, (ImGuiDataType)data_type, p_data, components, v_speed, p_min, p_max, format, flags);
}

CIMGUI_API bool igSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::SliderFloat(label, v, v_min, v_max, format, flags);
}

CIMGUI_API bool igSliderFloat2(const char* label, float v[2], float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::SliderFloat2(label, v, v_min, v_max, format, flags);
}

CIMGUI_API bool igSliderFloat3(const char* label, float v[3], float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::SliderFloat3(label, v, v_min, v_max, format, flags);
}

CIMGUI_API bool igSliderFloat4(const char* label, float v[4], float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::SliderFloat4(label, v, v_min, v_max, format, flags);
}

CIMGUI_API bool igSliderAngle(const char* label, float* v_rad, float v_degrees_min, float v_degrees_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::SliderAngle(label, v_rad, v_degrees_min, v_degrees_max, format, flags);
}

CIMGUI_API bool igSliderInt(const char* label, int* v, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::SliderInt(label, v, v_min, v_max, format, flags);
}

CIMGUI_API bool igSliderInt2(const char* label, int v[2], int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::SliderInt2(label, v, v_min, v_max, format, flags);
}

CIMGUI_API bool igSliderInt3(const char* label, int v[3], int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::SliderInt3(label, v, v_min, v_max, format, flags);
}

CIMGUI_API bool igSliderInt4(const char* label, int v[4], int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::SliderInt4(label, v, v_min, v_max, format, flags);
}

CIMGUI_API bool igSliderScalar(const char* label, int data_type, void* p_data, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::SliderScalar(label, (ImGuiDataType)data_type, p_data, p_min, p_max, format, flags);
}

CIMGUI_API bool igSliderScalarN(const char* label, int data_type, void* p_data, int components, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::SliderScalarN(label, (ImGuiDataType)data_type, p_data, components, p_min, p_max, format, flags);
}

CIMGUI_API bool igVSliderFloat(const char* label, const ImVec2 size, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::VSliderFloat(label, size, v, v_min, v_max, format, flags);
}

CIMGUI_API bool igVSliderInt(const char* label, const ImVec2 size, int* v, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::VSliderInt(label, size, v, v_min, v_max, format, flags);
}

CIMGUI_API bool igVSliderScalar(const char* label, const ImVec2 size, int data_type, void* p_data, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
{
    return ImGui::VSliderScalar(label, size, (ImGuiDataType)data_type, p_data, p_min, p_max, format, flags);
}

CIMGUI_API bool igInputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
{
    return ImGui::InputText(label, buf, buf_size, flags, callback, user_data);
}

CIMGUI_API bool igInputTextMultiline(const char* label, char* buf, size_t buf_size, const ImVec2 size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
{
    return ImGui::InputTextMultiline(label, buf, buf_size, size, flags, callback, user_data);
}

CIMGUI_API bool igInputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
{
    return ImGui::InputTextWithHint(label, hint, buf, buf_size, flags, callback, user_data);
}

CIMGUI_API bool igInputFloat(const char* label, float* v, float step, float step_fast, const char* format, ImGuiInputTextFlags flags)
{
    return ImGui::InputFloat(label, v, step, step_fast, format, flags);
}

CIMGUI_API bool igInputFloat2(const char* label, float v[2], const char* format, ImGuiInputTextFlags flags)
{
    return ImGui::InputFloat2(label, v, format, flags);
}

CIMGUI_API bool igInputFloat3(const char* label, float v[3], const char* format, ImGuiInputTextFlags flags)
{
    return ImGui::InputFloat3(label, v, format, flags);
}

CIMGUI_API bool igInputFloat4(const char* label, float v[4], const char* format, ImGuiInputTextFlags flags)
{
    return ImGui::InputFloat4(label, v, format, flags);
}

CIMGUI_API bool igInputInt(const char* label, int* v, int step, int step_fast, ImGuiInputTextFlags flags)
{
    return ImGui::InputInt(label, v, step, step_fast, flags);
}

CIMGUI_API bool igInputInt2(const char* label, int v[2], ImGuiInputTextFlags flags)
{
    return ImGui::InputInt2(label, v, flags);
}

CIMGUI_API bool igInputInt3(const char* label, int v[3], ImGuiInputTextFlags flags)
{
    return ImGui::InputInt3(label, v, flags);
}

CIMGUI_API bool igInputInt4(const char* label, int v[4], ImGuiInputTextFlags flags)
{
    return ImGui::InputInt4(label, v, flags);
}

CIMGUI_API bool igInputDouble(const char* label, double* v, double step, double step_fast, const char* format, ImGuiInputTextFlags flags)
{
    return ImGui::InputDouble(label, v, step, step_fast, format, flags);
}

CIMGUI_API bool igInputScalar(const char* label, int data_type, void* p_data, const void* p_step, const void* p_step_fast, const char* format, ImGuiInputTextFlags flags)
{
    return ImGui::InputScalar(label, (ImGuiDataType)data_type, p_data, p_step, p_step_fast, format, flags);
}

CIMGUI_API bool igInputScalarN(const char* label, int data_type, void* p_data, int components, const void* p_step, const void* p_step_fast, const char* format, ImGuiInputTextFlags flags)
{
    return ImGui::InputScalarN(label, (ImGuiDataType)data_type, p_data, components, p_step, p_step_fast, format, flags);
}

CIMGUI_API bool igColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags)
{
    return ImGui::ColorEdit3(label, col, flags);
}

CIMGUI_API bool igColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags)
{
    return ImGui::ColorEdit4(label, col, flags);
}

CIMGUI_API bool igColorPicker3(const char* label, float col[3], ImGuiColorEditFlags flags)
{
    return ImGui::ColorPicker3(label, col, flags);
}

CIMGUI_API bool igColorPicker4(const char* label, float col[4], ImGuiColorEditFlags flags, const float* ref_col)
{
    return ImGui::ColorPicker4(label, col, flags, ref_col);
}

CIMGUI_API bool igColorButton(const char* desc_id, const ImVec4 col, ImGuiColorEditFlags flags, ImVec2 size)
{
    return ImGui::ColorButton(desc_id, col, flags, size);
}

CIMGUI_API void igSetColorEditOptions(ImGuiColorEditFlags flags)
{
    ImGui::SetColorEditOptions(flags);
}

CIMGUI_API bool igTreeNodeStr(const char* label)
{
    return ImGui::TreeNode(label);
}

CIMGUI_API bool igTreeNodeStrStr(const char* str_id, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    bool ret = ImGui::TreeNodeV(str_id, fmt, args);
    va_end(args);
    return ret;
}

CIMGUI_API bool igTreeNodePtr(const void* ptr_id, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    bool ret = ImGui::TreeNodeV(ptr_id, fmt, args);
    va_end(args);
    return ret;
}

CIMGUI_API bool igTreeNodeV(const char* str_id, const char* fmt, va_list args)
{
    return ImGui::TreeNodeV(str_id, fmt, args);
}

CIMGUI_API bool igTreeNodeVPtr(const void* ptr_id, const char* fmt, va_list args)
{
    return ImGui::TreeNodeV(ptr_id, fmt, args);
}

CIMGUI_API bool igTreeNodeExStr(const char* label, ImGuiTreeNodeFlags flags)
{
    return ImGui::TreeNodeEx(label, flags);
}

CIMGUI_API bool igTreeNodeExStrStr(const char* str_id, ImGuiTreeNodeFlags flags, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    bool ret = ImGui::TreeNodeExV(str_id, flags, fmt, args);
    va_end(args);
    return ret;
}

CIMGUI_API bool igTreeNodeExPtr(const void* ptr_id, ImGuiTreeNodeFlags flags, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    bool ret = ImGui::TreeNodeExV(ptr_id, flags, fmt, args);
    va_end(args);
    return ret;
}

CIMGUI_API bool igTreeNodeExV(const char* str_id, ImGuiTreeNodeFlags flags, const char* fmt, va_list args)
{
    return ImGui::TreeNodeExV(str_id, flags, fmt, args);
}

CIMGUI_API bool igTreeNodeExVPtr(const void* ptr_id, ImGuiTreeNodeFlags flags, const char* fmt, va_list args)
{
    return ImGui::TreeNodeExV(ptr_id, flags, fmt, args);
}

CIMGUI_API void igTreePushStr(const char* str_id)
{
    ImGui::TreePush(str_id);
}

CIMGUI_API void igTreePushPtr(const void* ptr_id)
{
    ImGui::TreePush(ptr_id);
}

CIMGUI_API void igTreePop()
{
    ImGui::TreePop();
}

CIMGUI_API float igGetTreeNodeToLabelSpacing()
{
    return ImGui::GetTreeNodeToLabelSpacing();
}

CIMGUI_API void igSetNextItemOpen(bool is_open, ImGuiCond cond)
{
    ImGui::SetNextItemOpen(is_open, cond);
}

CIMGUI_API bool igCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags)
{
    return ImGui::CollapsingHeader(label, flags);
}

CIMGUI_API bool igCollapsingHeaderBoolPtr(const char* label, bool* p_open, ImGuiTreeNodeFlags flags)
{
    return ImGui::CollapsingHeader(label, p_open, flags);
}

CIMGUI_API bool igSelectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2 size)
{
    return ImGui::Selectable(label, selected, flags, size);
}

CIMGUI_API bool igSelectableBoolPtr(const char* label, bool* p_selected, ImGuiSelectableFlags flags, const ImVec2 size)
{
    return ImGui::Selectable(label, p_selected, flags, size);
}

CIMGUI_API bool igBeginListBox(const char* label, const ImVec2 size)
{
    return ImGui::BeginListBox(label, size);
}

CIMGUI_API void igEndListBox()
{
    ImGui::EndListBox();
}

CIMGUI_API bool igListBoxStr_arr(const char* label, int* current_item, const char* const items[], int items_count, int height_in_items)
{
    return ImGui::ListBox(label, current_item, items, items_count, height_in_items);
}

CIMGUI_API bool igListBoxFnPtr(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int height_in_items)
{
    return ImGui::ListBox(label, current_item, getter, user_data, items_count, height_in_items);
}

CIMGUI_API void igPlotLines(const char* label, const float* values, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, ImVec2 graph_size, int stride)
{
    ImGui::PlotLines(label, values, values_count, values_offset, overlay_text, scale_min, scale_max, graph_size, stride);
}

CIMGUI_API void igPlotLinesFnPtr(const char* label, float (*values_getter)(void* data, int idx), void* data, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, ImVec2 graph_size)
{
    ImGui::PlotLines(label, values_getter, data, values_count, values_offset, overlay_text, scale_min, scale_max, graph_size);
}

CIMGUI_API void igPlotHistogramFloatPtr(const char* label, const float* values, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, ImVec2 graph_size, int stride)
{
    ImGui::PlotHistogram(label, values, values_count, values_offset, overlay_text, scale_min, scale_max, graph_size, stride);
}

CIMGUI_API void igPlotHistogramFnPtr(const char* label, float (*values_getter)(void* data, int idx), void* data, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, ImVec2 graph_size)
{
    ImGui::PlotHistogram(label, values_getter, data, values_count, values_offset, overlay_text, scale_min, scale_max, graph_size);
}

CIMGUI_API void igValueBool(const char* prefix, bool b)
{
    ImGui::Value(prefix, b);
}

CIMGUI_API void igValueInt(const char* prefix, int v)
{
    ImGui::Value(prefix, v);
}

CIMGUI_API void igValueUint(const char* prefix, unsigned int v)
{
    ImGui::Value(prefix, v);
}

CIMGUI_API void igValueFloat(const char* prefix, float v, const char* float_format)
{
    ImGui::Value(prefix, v, float_format);
}

CIMGUI_API bool igBeginMenuBar()
{
    return ImGui::BeginMenuBar();
}

CIMGUI_API void igEndMenuBar()
{
    ImGui::EndMenuBar();
}

CIMGUI_API bool igBeginMainMenuBar()
{
    return ImGui::BeginMainMenuBar();
}

CIMGUI_API void igEndMainMenuBar()
{
    ImGui::EndMainMenuBar();
}

CIMGUI_API bool igBeginMenu(const char* label, bool enabled)
{
    return ImGui::BeginMenu(label, enabled);
}

CIMGUI_API void igEndMenu()
{
    ImGui::EndMenu();
}

CIMGUI_API bool igMenuItemBool(const char* label, const char* shortcut, bool selected, bool enabled)
{
    return ImGui::MenuItem(label, shortcut, selected, enabled);
}

CIMGUI_API bool igMenuItemBoolPtr(const char* label, const char* shortcut, bool* p_selected, bool enabled)
{
    return ImGui::MenuItem(label, shortcut, p_selected, enabled);
}

CIMGUI_API bool igBeginTooltip()
{
    return ImGui::BeginTooltip();
}

CIMGUI_API void igEndTooltip()
{
    ImGui::EndTooltip();
}

CIMGUI_API void igSetTooltip(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::SetTooltipV(fmt, args);
    va_end(args);
}

CIMGUI_API void igSetTooltipV(const char* fmt, va_list args)
{
    ImGui::SetTooltipV(fmt, args);
}

CIMGUI_API bool igBeginPopup(const char* str_id, ImGuiWindowFlags flags)
{
    return ImGui::BeginPopup(str_id, flags);
}

CIMGUI_API bool igBeginPopupModal(const char* name, bool* p_open, ImGuiWindowFlags flags)
{
    return ImGui::BeginPopupModal(name, p_open, flags);
}

CIMGUI_API void igEndPopup()
{
    ImGui::EndPopup();
}

CIMGUI_API void igOpenPopup(const char* str_id, ImGuiPopupFlags popup_flags)
{
    ImGui::OpenPopup(str_id, popup_flags);
}

CIMGUI_API void igOpenPopupOnItemClick(const char* str_id, ImGuiPopupFlags popup_flags)
{
    ImGui::OpenPopupOnItemClick(str_id, popup_flags);
}

CIMGUI_API void igCloseCurrentPopup()
{
    ImGui::CloseCurrentPopup();
}

CIMGUI_API bool igBeginPopupContextItem(const char* str_id, ImGuiPopupFlags popup_flags)
{
    return ImGui::BeginPopupContextItem(str_id, popup_flags);
}

CIMGUI_API bool igBeginPopupContextWindow(const char* str_id, ImGuiPopupFlags popup_flags)
{
    return ImGui::BeginPopupContextWindow(str_id, popup_flags);
}

CIMGUI_API bool igBeginPopupContextVoid(const char* str_id, ImGuiPopupFlags popup_flags)
{
    return ImGui::BeginPopupContextVoid(str_id, popup_flags);
}

CIMGUI_API bool igIsPopupOpenStr(const char* str_id, ImGuiPopupFlags flags)
{
    return ImGui::IsPopupOpen(str_id, flags);
}

CIMGUI_API void igColumns(int count, const char* id, bool border)
{
    ImGui::Columns(count, id, border);
}

CIMGUI_API void igNextColumn()
{
    ImGui::NextColumn();
}

CIMGUI_API int igGetColumnIndex()
{
    return ImGui::GetColumnIndex();
}

CIMGUI_API float igGetColumnWidth(int column_index)
{
    return ImGui::GetColumnWidth(column_index);
}

CIMGUI_API void igSetColumnWidth(int column_index, float width)
{
    ImGui::SetColumnWidth(column_index, width);
}

CIMGUI_API float igGetColumnOffset(int column_index)
{
    return ImGui::GetColumnOffset(column_index);
}

CIMGUI_API void igSetColumnOffset(int column_index, float offset_x)
{
    ImGui::SetColumnOffset(column_index, offset_x);
}

CIMGUI_API int igGetColumnsCount()
{
    return ImGui::GetColumnsCount();
}

CIMGUI_API bool igBeginTabBar(const char* str_id, ImGuiTabBarFlags flags)
{
    return ImGui::BeginTabBar(str_id, flags);
}

CIMGUI_API void igEndTabBar()
{
    ImGui::EndTabBar();
}

CIMGUI_API bool igBeginTabItem(const char* label, bool* p_open, ImGuiTabItemFlags flags)
{
    return ImGui::BeginTabItem(label, p_open, flags);
}

CIMGUI_API void igEndTabItem()
{
    ImGui::EndTabItem();
}

CIMGUI_API bool igTabItemButton(const char* label, ImGuiTabItemFlags flags)
{
    return ImGui::TabItemButton(label, flags);
}

CIMGUI_API void igSetTabItemClosed(const char* tab_or_docked_window_label)
{
    ImGui::SetTabItemClosed(tab_or_docked_window_label);
}

CIMGUI_API void igLogToTTY(int auto_open_depth)
{
    ImGui::LogToTTY(auto_open_depth);
}

CIMGUI_API void igLogToFile(int auto_open_depth, const char* filename)
{
    ImGui::LogToFile(auto_open_depth, filename);
}

CIMGUI_API void igLogToClipboard(int auto_open_depth)
{
    ImGui::LogToClipboard(auto_open_depth);
}

CIMGUI_API void igLogFinish()
{
    ImGui::LogFinish();
}

CIMGUI_API void igLogButtons()
{
    ImGui::LogButtons();
}

CIMGUI_API void igLogText(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::LogTextV(fmt, args);
    va_end(args);
}

CIMGUI_API bool igBeginDragDropSource(ImGuiDragDropFlags flags)
{
    return ImGui::BeginDragDropSource(flags);
}

CIMGUI_API bool igSetDragDropPayload(const char* type, const void* data, size_t sz, ImGuiCond cond)
{
    return ImGui::SetDragDropPayload(type, data, sz, cond);
}

CIMGUI_API void igEndDragDropSource()
{
    ImGui::EndDragDropSource();
}

CIMGUI_API bool igBeginDragDropTarget()
{
    return ImGui::BeginDragDropTarget();
}

CIMGUI_API const ImGuiPayload* igAcceptDragDropPayload(const char* type, ImGuiDragDropFlags flags)
{
    return ImGui::AcceptDragDropPayload(type, flags);
}

CIMGUI_API void igEndDragDropTarget()
{
    ImGui::EndDragDropTarget();
}

CIMGUI_API const ImGuiPayload* igGetDragDropPayload()
{
    return ImGui::GetDragDropPayload();
}

CIMGUI_API void igPushClipRect(const ImVec2 clip_rect_min, const ImVec2 clip_rect_max, bool intersect_with_current_clip_rect)
{
    ImGui::PushClipRect(clip_rect_min, clip_rect_max, intersect_with_current_clip_rect);
}

CIMGUI_API void igPopClipRect()
{
    ImGui::PopClipRect();
}

CIMGUI_API void igSetItemDefaultFocus()
{
    ImGui::SetItemDefaultFocus();
}

CIMGUI_API void igSetKeyboardFocusHere(int offset)
{
    ImGui::SetKeyboardFocusHere(offset);
}

CIMGUI_API bool igIsItemHovered(ImGuiHoveredFlags flags)
{
    return ImGui::IsItemHovered(flags);
}

CIMGUI_API bool igIsItemActive()
{
    return ImGui::IsItemActive();
}

CIMGUI_API bool igIsItemFocused()
{
    return ImGui::IsItemFocused();
}

CIMGUI_API bool igIsItemClicked(ImGuiMouseButton mouse_button)
{
    return ImGui::IsItemClicked(mouse_button);
}

CIMGUI_API bool igIsItemVisible()
{
    return ImGui::IsItemVisible();
}

CIMGUI_API bool igIsItemEdited()
{
    return ImGui::IsItemEdited();
}

CIMGUI_API bool igIsItemActivated()
{
    return ImGui::IsItemActivated();
}

CIMGUI_API bool igIsItemDeactivated()
{
    return ImGui::IsItemDeactivated();
}

CIMGUI_API bool igIsItemDeactivatedAfterEdit()
{
    return ImGui::IsItemDeactivatedAfterEdit();
}

CIMGUI_API bool igIsItemToggledOpen()
{
    return ImGui::IsItemToggledOpen();
}

CIMGUI_API bool igIsAnyItemHovered()
{
    return ImGui::IsAnyItemHovered();
}

CIMGUI_API bool igIsAnyItemActive()
{
    return ImGui::IsAnyItemActive();
}

CIMGUI_API bool igIsAnyItemFocused()
{
    return ImGui::IsAnyItemFocused();
}

CIMGUI_API void igGetItemRectMin(ImVec2* pOut)
{
    *pOut = ImGui::GetItemRectMin();
}

CIMGUI_API void igGetItemRectMax(ImVec2* pOut)
{
    *pOut = ImGui::GetItemRectMax();
}

CIMGUI_API void igGetItemRectSize(ImVec2* pOut)
{
    *pOut = ImGui::GetItemRectSize();
}

CIMGUI_API void igSetItemAllowOverlap()
{
    ImGui::SetItemAllowOverlap();
}

CIMGUI_API ImGuiViewport* igGetMainViewport()
{
    return ImGui::GetMainViewport();
}

CIMGUI_API ImDrawList* igGetBackgroundDrawList()
{
    return ImGui::GetBackgroundDrawList();
}

CIMGUI_API ImDrawList* igGetForegroundDrawList()
{
    return ImGui::GetForegroundDrawList();
}

CIMGUI_API bool igIsRectVisible(const ImVec2 size)
{
    return ImGui::IsRectVisible(size);
}

CIMGUI_API bool igIsRectVisibleVec2(const ImVec2 rect_min, const ImVec2 rect_max)
{
    return ImGui::IsRectVisible(rect_min, rect_max);
}

CIMGUI_API double igGetTime()
{
    return ImGui::GetTime();
}

CIMGUI_API int igGetFrameCount()
{
    return ImGui::GetFrameCount();
}

CIMGUI_API void* igGetDrawListSharedData()
{
    return (void*)ImGui::GetDrawListSharedData();
}

CIMGUI_API const char* igGetStyleColorName(ImGuiCol idx)
{
    return ImGui::GetStyleColorName(idx);
}

CIMGUI_API void igSetStateStorage(ImGuiStorage* storage)
{
    ImGui::SetStateStorage(storage);
}

CIMGUI_API ImGuiStorage* igGetStateStorage()
{
    return ImGui::GetStateStorage();
}

CIMGUI_API bool igBeginChildFrame(ImGuiID id, const ImVec2 size, ImGuiWindowFlags flags)
{
    return ImGui::BeginChildFrame(id, size, flags);
}

CIMGUI_API void igEndChildFrame()
{
    ImGui::EndChildFrame();
}

CIMGUI_API void igCalcTextSize(ImVec2* pOut, const char* text, const char* text_end, bool hide_text_after_double_hash, float wrap_width)
{
    *pOut = ImGui::CalcTextSize(text, text_end, hide_text_after_double_hash, wrap_width);
}

CIMGUI_API void igColorConvertU32ToFloat4(ImVec4* pOut, ImU32 in)
{
    *pOut = ImGui::ColorConvertU32ToFloat4(in);
}

CIMGUI_API ImU32 igColorConvertFloat4ToU32(const ImVec4 in)
{
    return ImGui::ColorConvertFloat4ToU32(in);
}

CIMGUI_API void igColorConvertRGBtoHSV(float r, float g, float b, float* out_h, float* out_s, float* out_v)
{
    ImGui::ColorConvertRGBtoHSV(r, g, b, *out_h, *out_s, *out_v);
}

CIMGUI_API void igColorConvertHSVtoRGB(float h, float s, float v, float* out_r, float* out_g, float* out_b)
{
    ImGui::ColorConvertHSVtoRGB(h, s, v, *out_r, *out_g, *out_b);
}

CIMGUI_API bool igIsKeyDown(ImGuiKey key)
{
    return ImGui::IsKeyDown(key);
}

CIMGUI_API bool igIsKeyPressed(ImGuiKey key, bool repeat)
{
    return ImGui::IsKeyPressed(key, repeat);
}

CIMGUI_API bool igIsKeyReleased(ImGuiKey key)
{
    return ImGui::IsKeyReleased(key);
}

CIMGUI_API int igGetKeyPressedAmount(ImGuiKey key, float repeat_delay, float rate)
{
    return ImGui::GetKeyPressedAmount(key, repeat_delay, rate);
}

CIMGUI_API const char* igGetKeyName(ImGuiKey key)
{
    return ImGui::GetKeyName(key);
}

CIMGUI_API bool igIsMouseDown(ImGuiMouseButton button)
{
    return ImGui::IsMouseDown(button);
}

CIMGUI_API bool igIsMouseClicked(ImGuiMouseButton button, bool repeat)
{
    return ImGui::IsMouseClicked(button, repeat);
}

CIMGUI_API bool igIsMouseReleased(ImGuiMouseButton button)
{
    return ImGui::IsMouseReleased(button);
}

CIMGUI_API bool igIsMouseDoubleClicked(ImGuiMouseButton button)
{
    return ImGui::IsMouseDoubleClicked(button);
}

CIMGUI_API int igGetMouseClickedCount(ImGuiMouseButton button)
{
    return ImGui::GetMouseClickedCount(button);
}

CIMGUI_API bool igIsMouseHoveringRect(const ImVec2 r_min, const ImVec2 r_max, bool clip)
{
    return ImGui::IsMouseHoveringRect(r_min, r_max, clip);
}

CIMGUI_API bool igIsMousePosValid(const ImVec2* mouse_pos)
{
    return ImGui::IsMousePosValid(mouse_pos);
}

CIMGUI_API bool igIsAnyMouseDown()
{
    return ImGui::IsAnyMouseDown();
}

CIMGUI_API void igGetMousePos(ImVec2* pOut)
{
    *pOut = ImGui::GetMousePos();
}

CIMGUI_API void igGetMousePosOnOpeningCurrentPopup(ImVec2* pOut)
{
    *pOut = ImGui::GetMousePosOnOpeningCurrentPopup();
}

CIMGUI_API bool igIsMouseDragging(ImGuiMouseButton button, float lock_threshold)
{
    return ImGui::IsMouseDragging(button, lock_threshold);
}

CIMGUI_API void igGetMouseDragDelta(ImVec2* pOut, ImGuiMouseButton button, float lock_threshold)
{
    *pOut = ImGui::GetMouseDragDelta(button, lock_threshold);
}

CIMGUI_API void igResetMouseDragDelta(ImGuiMouseButton button)
{
    ImGui::ResetMouseDragDelta(button);
}

CIMGUI_API ImGuiMouseCursor igGetMouseCursor()
{
    return ImGui::GetMouseCursor();
}

CIMGUI_API void igSetMouseCursor(ImGuiMouseCursor cursor_type)
{
    ImGui::SetMouseCursor(cursor_type);
}

CIMGUI_API const char* igGetClipboardText()
{
    return ImGui::GetClipboardText();
}

CIMGUI_API void igSetClipboardText(const char* text)
{
    ImGui::SetClipboardText(text);
}

CIMGUI_API void igLoadIniSettingsFromDisk(const char* ini_filename)
{
    ImGui::LoadIniSettingsFromDisk(ini_filename);
}

CIMGUI_API void igLoadIniSettingsFromMemory(const char* ini_data, size_t ini_size)
{
    ImGui::LoadIniSettingsFromMemory(ini_data, ini_size);
}

CIMGUI_API void igSaveIniSettingsToDisk(const char* ini_filename)
{
    ImGui::SaveIniSettingsToDisk(ini_filename);
}

CIMGUI_API const char* igSaveIniSettingsToMemory(size_t* out_ini_size)
{
    return ImGui::SaveIniSettingsToMemory(out_ini_size);
}

CIMGUI_API void* igMemAlloc(size_t size)
{
    return ImGui::MemAlloc(size);
}

CIMGUI_API void igMemFree(void* ptr)
{
    ImGui::MemFree(ptr);
}

CIMGUI_API void ImGuiTextFilter_Create(ImGuiTextFilter* filter, const char* default_filter)
{
    IM_PLACEMENT_NEW(filter) ImGuiTextFilter(default_filter);
}

CIMGUI_API void ImGuiTextFilter_Destroy(ImGuiTextFilter* filter)
{
    filter->~ImGuiTextFilter();
}

CIMGUI_API bool ImGuiTextFilter_Draw(ImGuiTextFilter* filter, const char* label, float width)
{
    return filter->Draw(label, width);
}

CIMGUI_API bool ImGuiTextFilter_PassFilter(ImGuiTextFilter* filter, const char* text, const char* text_end)
{
    return filter->PassFilter(text, text_end);
}

CIMGUI_API void ImGuiTextFilter_Build(ImGuiTextFilter* filter)
{
    filter->Build();
}

CIMGUI_API void ImGuiTextFilter_Clear(ImGuiTextFilter* filter)
{
    filter->Clear();
}

CIMGUI_API bool ImGuiTextFilter_IsActive(ImGuiTextFilter* filter)
{
    return filter->IsActive();
}

CIMGUI_API void ImGuiTextBuffer_Create(ImGuiTextBuffer* buffer)
{
    IM_PLACEMENT_NEW(buffer) ImGuiTextBuffer();
}

CIMGUI_API void ImGuiTextBuffer_Destroy(ImGuiTextBuffer* buffer)
{
    buffer->~ImGuiTextBuffer();
}

CIMGUI_API const char* ImGuiTextBuffer_begin(ImGuiTextBuffer* buffer)
{
    return buffer->begin();
}

CIMGUI_API const char* ImGuiTextBuffer_end(ImGuiTextBuffer* buffer)
{
    return buffer->end();
}

CIMGUI_API int ImGuiTextBuffer_size(ImGuiTextBuffer* buffer)
{
    return buffer->size();
}

CIMGUI_API bool ImGuiTextBuffer_empty(ImGuiTextBuffer* buffer)
{
    return buffer->empty();
}

CIMGUI_API void ImGuiTextBuffer_clear(ImGuiTextBuffer* buffer)
{
    buffer->clear();
}

CIMGUI_API const char* ImGuiTextBuffer_c_str(ImGuiTextBuffer* buffer)
{
    return buffer->c_str();
}

CIMGUI_API void ImGuiTextBuffer_append(ImGuiTextBuffer* buffer, const char* str, const char* str_end)
{
    buffer->append(str, str_end);
}

CIMGUI_API void ImGuiStorage_BuildSortByKey(ImGuiStorage* storage)
{
    storage->BuildSortByKey();
}

CIMGUI_API void ImGuiListClipper_Create(ImGuiListClipper* clipper)
{
    IM_PLACEMENT_NEW(clipper) ImGuiListClipper();
}

CIMGUI_API void ImGuiListClipper_Destroy(ImGuiListClipper* clipper)
{
    clipper->~ImGuiListClipper();
}

CIMGUI_API void ImGuiListClipper_Begin(ImGuiListClipper* clipper, int items_count, float items_height)
{
    clipper->Begin(items_count, items_height);
}

CIMGUI_API void ImGuiListClipper_End(ImGuiListClipper* clipper)
{
    clipper->End();
}

CIMGUI_API bool ImGuiListClipper_Step(ImGuiListClipper* clipper)
{
    return clipper->Step();
}

CIMGUI_API bool ImGui_ImplSDL3_InitForOpenGL(void* window, void* gl_context)
{
    return ImGui_ImplSDL3_InitForOpenGL((SDL_Window*)window, gl_context);
}

CIMGUI_API bool ImGui_ImplSDL3_InitForVulkan(void* window)
{
    return ImGui_ImplSDL3_InitForVulkan((SDL_Window*)window);
}

CIMGUI_API bool ImGui_ImplSDL3_InitForD3D(void* window)
{
    return ImGui_ImplSDL3_InitForD3D((SDL_Window*)window);
}

CIMGUI_API bool ImGui_ImplSDL3_InitForMetal(void* window)
{
    return ImGui_ImplSDL3_InitForMetal((SDL_Window*)window);
}

CIMGUI_API bool ImGui_ImplSDL3_InitForSDLRenderer(void* window, void* renderer)
{
    return ImGui_ImplSDL3_InitForSDLRenderer((SDL_Window*)window, (SDL_Renderer*)renderer);
}

CIMGUI_API void cImGui_ImplSDL3_Shutdown()
{
    ImGui_ImplSDL3_Shutdown();
}

CIMGUI_API void cImGui_ImplSDL3_NewFrame()
{
    ImGui_ImplSDL3_NewFrame();
}

CIMGUI_API bool ImGui_ImplSDL3_ProcessEvent(const void* event)
{
    return ImGui_ImplSDL3_ProcessEvent((const SDL_Event*)event);
}

CIMGUI_API bool ImGui_ImplSDLRenderer3_Init(void* renderer)
{
    return ImGui_ImplSDLRenderer3_Init((SDL_Renderer*)renderer);
}

CIMGUI_API void cImGui_ImplSDLRenderer3_Shutdown()
{
    ImGui_ImplSDLRenderer3_Shutdown();
}

CIMGUI_API void cImGui_ImplSDLRenderer3_NewFrame()
{
    ImGui_ImplSDLRenderer3_NewFrame();
}

CIMGUI_API void ImGui_ImplSDLRenderer3_RenderDrawData(ImDrawData* draw_data, void* renderer)
{
    ImGui_ImplSDLRenderer3_RenderDrawData(draw_data, (SDL_Renderer*)renderer);
}

CIMGUI_API void cImGui_ImplSDLRenderer3_CreateDeviceObjects()
{
    ImGui_ImplSDLRenderer3_CreateDeviceObjects();
}

CIMGUI_API void cImGui_ImplSDLRenderer3_DestroyDeviceObjects()
{
    ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
}

// cimgui.cpp (Updated for modern Dear ImGui)
// This is a modified version of cimgui.cpp to be compatible with the updated cimgui.h
// and a newer version of Dear ImGui. Obsolete functions have been removed or updated.




// #include "imgui.h"
// #include "cimgui.h"
//
// // to use placement new
// #define IMGUI_DEFINE_PLACEMENT_NEW
// #include "imgui_internal.h"
//
// #include "imgui_impl_sdl3.h"
// #include "imgui_impl_sdlrenderer3.h"
//
// CIMGUI_API ImGuiIO* igGetIO()
// {
//     return &ImGui::GetIO();
// }
//
// CIMGUI_API ImGuiStyle* igGetStyle()
// {
//     return &ImGui::GetStyle();
// }
//
// CIMGUI_API ImDrawData* igGetDrawData()
// {
//     return ImGui::GetDrawData();
// }
//
// CIMGUI_API void igNewFrame()
// {
//     ImGui::NewFrame();
// }
//
// CIMGUI_API void igRender()
// {
//     ImGui::Render();
// }
//
// CIMGUI_API void igEndFrame()
// {
//     ImGui::EndFrame();
// }
//
// CIMGUI_API void igShowDemoWindow(bool* p_open)
// {
//     ImGui::ShowDemoWindow(p_open);
// }
//
// CIMGUI_API void igShowMetricsWindow(bool* p_open)
// {
//     ImGui::ShowMetricsWindow(p_open);
// }
//
// CIMGUI_API void igShowStyleEditor(ImGuiStyle* ref)
// {
//     ImGui::ShowStyleEditor(ref);
// }
//
// CIMGUI_API bool igShowStyleSelector(const char* label)
// {
//     return ImGui::ShowStyleSelector(label);
// }
//
// CIMGUI_API void igShowFontSelector(const char* label)
// {
//     ImGui::ShowFontSelector(label);
// }
//
// CIMGUI_API void igShowUserGuide()
// {
//     ImGui::ShowUserGuide();
// }
//
// CIMGUI_API const char* igGetVersion()
// {
//     return ImGui::GetVersion();
// }
//
// CIMGUI_API bool igBegin(const char* name, bool* p_open, ImGuiWindowFlags flags)
// {
//     return ImGui::Begin(name, p_open, flags);
// }
//
// CIMGUI_API void igEnd()
// {
//     ImGui::End();
// }
//
// CIMGUI_API bool igBeginChild(const char* str_id, const ImVec2 size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
// {
//     return ImGui::BeginChild(str_id, size, child_flags, window_flags);
// }
//
// CIMGUI_API bool igBeginChildID(ImGuiID id, const ImVec2 size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
// {
//     return ImGui::BeginChild(id, size, child_flags, window_flags);
// }
//
// CIMGUI_API void igEndChild()
// {
//     ImGui::EndChild();
// }
//
// CIMGUI_API bool igIsWindowAppearing()
// {
//     return ImGui::IsWindowAppearing();
// }
//
// CIMGUI_API bool igIsWindowCollapsed()
// {
//     return ImGui::IsWindowCollapsed();
// }
//
// CIMGUI_API bool igIsWindowFocused(ImGuiFocusedFlags flags)
// {
//     return ImGui::IsWindowFocused(flags);
// }
//
// CIMGUI_API bool igIsWindowHovered(ImGuiHoveredFlags flags)
// {
//     return ImGui::IsWindowHovered(flags);
// }
//
// CIMGUI_API ImDrawList* igGetWindowDrawList()
// {
//     return ImGui::GetWindowDrawList();
// }
//
// CIMGUI_API ImVec2 igGetWindowPos()
// {
//     return ImGui::GetWindowPos();
// }
//
// CIMGUI_API ImVec2 igGetWindowSize()
// {
//     return ImGui::GetWindowSize();
// }
//
// CIMGUI_API float igGetWindowWidth()
// {
//     return ImGui::GetWindowWidth();
// }
//
// CIMGUI_API float igGetWindowHeight()
// {
//     return ImGui::GetWindowHeight();
// }
//
// CIMGUI_API void igSetNextWindowPos(const ImVec2 pos, ImGuiCond cond, const ImVec2 pivot)
// {
//     ImGui::SetNextWindowPos(pos, cond, pivot);
// }
//
// CIMGUI_API void igSetNextWindowSize(const ImVec2 size, ImGuiCond cond)
// {
//     ImGui::SetNextWindowSize(size, cond);
// }
//
// CIMGUI_API void igSetNextWindowSizeConstraints(const ImVec2 size_min, const ImVec2 size_max, ImGuiSizeCallback custom_callback, void* custom_callback_data)
// {
//     ImGui::SetNextWindowSizeConstraints(size_min, size_max, custom_callback, custom_callback_data);
// }
//
// CIMGUI_API void igSetNextWindowContentSize(const ImVec2 size)
// {
//     ImGui::SetNextWindowContentSize(size);
// }
//
// CIMGUI_API void igSetNextWindowCollapsed(bool collapsed, ImGuiCond cond)
// {
//     ImGui::SetNextWindowCollapsed(collapsed, cond);
// }
//
// CIMGUI_API void igSetNextWindowFocus()
// {
//     ImGui::SetNextWindowFocus();
// }
//
// CIMGUI_API void igSetNextWindowBgAlpha(float alpha)
// {
//     ImGui::SetNextWindowBgAlpha(alpha);
// }
//
// CIMGUI_API void igSetWindowPosVec2(const ImVec2 pos, ImGuiCond cond)
// {
//     ImGui::SetWindowPos(pos, cond);
// }
//
// CIMGUI_API void igSetWindowSizeVec2(const ImVec2 size, ImGuiCond cond)
// {
//     ImGui::SetWindowSize(size, cond);
// }
//
// CIMGUI_API void igSetWindowCollapsedBool(bool collapsed, ImGuiCond cond)
// {
//     ImGui::SetWindowCollapsed(collapsed, cond);
// }
//
// CIMGUI_API void igSetWindowFocus()
// {
//     ImGui::SetWindowFocus();
// }
//
// CIMGUI_API void igSetWindowFontScale(float scale)
// {
//     ImGui::SetWindowFontScale(scale);
// }
//
// CIMGUI_API void igGetContentRegionAvail(ImVec2* pOut)
// {
//     *pOut = ImGui::GetContentRegionAvail();
// }
//
// CIMGUI_API void igGetWindowContentRegionMin(ImVec2* pOut)
// {
//     *pOut = ImGui::GetWindowContentRegionMin();
// }
//
// CIMGUI_API void igGetWindowContentRegionMax(ImVec2* pOut)
// {
//     *pOut = ImGui::GetWindowContentRegionMax();
// }
//
// CIMGUI_API float igGetScrollX()
// {
//     return ImGui::GetScrollX();
// }
//
// CIMGUI_API float igGetScrollY()
// {
//     return ImGui::GetScrollY();
// }
//
// CIMGUI_API float igGetScrollMaxX()
// {
//     return ImGui::GetScrollMaxX();
// }
//
// CIMGUI_API float igGetScrollMaxY()
// {
//     return ImGui::GetScrollMaxY();
// }
//
// CIMGUI_API void igSetScrollX(float scroll_x)
// {
//     ImGui::SetScrollX(scroll_x);
// }
//
// CIMGUI_API void igSetScrollY(float scroll_y)
// {
//     ImGui::SetScrollY(scroll_y);
// }
//
// CIMGUI_API void igSetScrollHereY(float center_y_ratio)
// {
//     ImGui::SetScrollHereY(center_y_ratio);
// }
//
// CIMGUI_API void igSetScrollFromPosY(float local_y, float center_y_ratio)
// {
//     ImGui::SetScrollFromPosY(local_y, center_y_ratio);
// }
//
// CIMGUI_API void igPushFont(ImFont* font)
// {
//     ImGui::PushFont(font);
// }
//
// CIMGUI_API void igPopFont()
// {
//     ImGui::PopFont();
// }
//
// CIMGUI_API void igPushStyleColorU32(ImGuiCol idx, ImU32 col)
// {
//     ImGui::PushStyleColor(idx, col);
// }
//
// CIMGUI_API void igPushStyleColorVec4(const ImVec4 col, ImGuiCol idx)
// {
//     ImGui::PushStyleColor(idx, col);
// }
//
// CIMGUI_API void igPopStyleColor(int count)
// {
//     ImGui::PopStyleColor(count);
// }
//
// CIMGUI_API void igPushStyleVarFloat(ImGuiStyleVar idx, float val)
// {
//     ImGui::PushStyleVar(idx, val);
// }
//
// CIMGUI_API void igPushStyleVarVec2(ImGuiStyleVar idx, const ImVec2 val)
// {
//     ImGui::PushStyleVar(idx, val);
// }
//
// CIMGUI_API void igPopStyleVar(int count)
// {
//     ImGui::PopStyleVar(count);
// }
//
// CIMGUI_API const ImVec4* igGetStyleColorVec4(ImGuiCol idx)
// {
//     return &ImGui::GetStyleColorVec4(idx);
// }
//
// CIMGUI_API ImFont* igGetFont()
// {
//     return ImGui::GetFont();
// }
//
// CIMGUI_API float igGetFontSize()
// {
//     return ImGui::GetFontSize();
// }
//
// CIMGUI_API void igGetFontTexUvWhitePixel(ImVec2* pOut)
// {
//     *pOut = ImGui::GetFontTexUvWhitePixel();
// }
//
// CIMGUI_API ImU32 igGetColorU32Col(ImGuiCol idx, float alpha_mul)
// {
//     return ImGui::GetColorU32(idx, alpha_mul);
// }
//
// CIMGUI_API ImU32 igGetColorU32Vec4(const ImVec4 col)
// {
//     return ImGui::GetColorU32(col);
// }
//
// CIMGUI_API ImU32 igGetColorU32U32(ImU32 col)
// {
//     return ImGui::GetColorU32(col);
// }
//
// CIMGUI_API void igPushItemWidth(float item_width)
// {
//     ImGui::PushItemWidth(item_width);
// }
//
// CIMGUI_API void igPopItemWidth()
// {
//     ImGui::PopItemWidth();
// }
//
// CIMGUI_API void igSetNextItemWidth(float item_width)
// {
//     ImGui::SetNextItemWidth(item_width);
// }
//
// CIMGUI_API float igCalcItemWidth()
// {
//     return ImGui::CalcItemWidth();
// }
//
// CIMGUI_API void igPushTextWrapPos(float wrap_local_pos_x)
// {
//     ImGui::PushTextWrapPos(wrap_local_pos_x);
// }
//
// CIMGUI_API void igPopTextWrapPos()
// {
//     ImGui::PopTextWrapPos();
// }
//
// CIMGUI_API void igSeparator()
// {
//     ImGui::Separator();
// }
//
// CIMGUI_API void igSameLine(float offset_from_start_x, float spacing)
// {
//     ImGui::SameLine(offset_from_start_x, spacing);
// }
//
// CIMGUI_API void igNewLine()
// {
//     ImGui::NewLine();
// }
//
// CIMGUI_API void igSpacing()
// {
//     ImGui::Spacing();
// }
//
// CIMGUI_API void igDummy(const ImVec2 size)
// {
//     ImGui::Dummy(size);
// }
//
// CIMGUI_API void igIndent(float indent_w)
// {
//     ImGui::Indent(indent_w);
// }
//
// CIMGUI_API void igUnindent(float indent_w)
// {
//     ImGui::Unindent(indent_w);
// }
//
// CIMGUI_API void igBeginGroup()
// {
//     ImGui::BeginGroup();
// }
//
// CIMGUI_API void igEndGroup()
// {
//     ImGui::EndGroup();
// }
//
// CIMGUI_API void igGetCursorPos(ImVec2* pOut)
// {
//     *pOut = ImGui::GetCursorPos();
// }
//
// CIMGUI_API float igGetCursorPosX()
// {
//     return ImGui::GetCursorPosX();
// }
//
// CIMGUI_API float igGetCursorPosY()
// {
//     return ImGui::GetCursorPosY();
// }
//
// CIMGUI_API void igSetCursorPos(const ImVec2 local_pos)
// {
//     ImGui::SetCursorPos(local_pos);
// }
//
// CIMGUI_API void igSetCursorPosX(float local_x)
// {
//     ImGui::SetCursorPosX(local_x);
// }
//
// CIMGUI_API void igSetCursorPosY(float local_y)
// {
//     ImGui::SetCursorPosY(local_y);
// }
//
// CIMGUI_API void igGetCursorStartPos(ImVec2* pOut)
// {
//     *pOut = ImGui::GetCursorStartPos();
// }
//
// CIMGUI_API void igGetCursorScreenPos(ImVec2* pOut)
// {
//     *pOut = ImGui::GetCursorScreenPos();
// }
//
// CIMGUI_API void igSetCursorScreenPos(const ImVec2 pos)
// {
//     ImGui::SetCursorScreenPos(pos);
// }
//
// CIMGUI_API void igAlignTextToFramePadding()
// {
//     ImGui::AlignTextToFramePadding();
// }
//
// CIMGUI_API float igGetTextLineHeight()
// {
//     return ImGui::GetTextLineHeight();
// }
//
// CIMGUI_API float igGetTextLineHeightWithSpacing()
// {
//     return ImGui::GetTextLineHeightWithSpacing();
// }
//
// CIMGUI_API float igGetFrameHeight()
// {
//     return ImGui::GetFrameHeight();
// }
//
// CIMGUI_API float igGetFrameHeightWithSpacing()
// {
//     return ImGui::GetFrameHeightWithSpacing();
// }
//
// CIMGUI_API void igPushIDStr(const char* str_id)
// {
//     ImGui::PushID(str_id);
// }
//
// CIMGUI_API void igPushIDStrRange(const char* str_id_begin, const char* str_id_end)
// {
//     ImGui::PushID(str_id_begin, str_id_end);
// }
//
// CIMGUI_API void igPushIDPtr(const void* ptr_id)
// {
//     ImGui::PushID(ptr_id);
// }
//
// CIMGUI_API void igPushIDInt(int int_id)
// {
//     ImGui::PushID(int_id);
// }
//
// CIMGUI_API void igPopID()
// {
//     ImGui::PopID();
// }
//
// CIMGUI_API ImGuiID igGetIDStr(const char* str_id)
// {
//     return ImGui::GetID(str_id);
// }
//
// CIMGUI_API ImGuiID igGetIDStrRange(const char* str_id_begin, const char* str_id_end)
// {
//     return ImGui::GetID(str_id_begin, str_id_end);
// }
//
// CIMGUI_API ImGuiID igGetIDPtr(const void* ptr_id)
// {
//     return ImGui::GetID(ptr_id);
// }
//
// CIMGUI_API void igTextUnformatted(const char* text, const char* text_end)
// {
//     ImGui::TextUnformatted(text, text_end);
// }
//
// CIMGUI_API void igText(const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     ImGui::TextV(fmt, args);
//     va_end(args);
// }
//
// CIMGUI_API void igTextV(const char* fmt, va_list args)
// {
//     ImGui::TextV(fmt, args);
// }
//
// CIMGUI_API void igTextColored(const ImVec4 col, const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     ImGui::TextColoredV(col, fmt, args);
//     va_end(args);
// }
//
// CIMGUI_API void igTextColoredV(const ImVec4 col, const char* fmt, va_list args)
// {
//     ImGui::TextColoredV(col, fmt, args);
// }
//
// CIMGUI_API void igTextDisabled(const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     ImGui::TextDisabledV(fmt, args);
//     va_end(args);
// }
//
// CIMGUI_API void igTextDisabledV(const char* fmt, va_list args)
// {
//     ImGui::TextDisabledV(fmt, args);
// }
//
// CIMGUI_API void igTextWrapped(const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     ImGui::TextWrappedV(fmt, args);
//     va_end(args);
// }
//
// CIMGUI_API void igTextWrappedV(const char* fmt, va_list args)
// {
//     ImGui::TextWrappedV(fmt, args);
// }
//
// CIMGUI_API void igLabelText(const char* label, const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     ImGui::LabelTextV(label, fmt, args);
//     va_end(args);
// }
//
// CIMGUI_API void igLabelTextV(const char* label, const char* fmt, va_list args)
// {
//     ImGui::LabelTextV(label, fmt, args);
// }
//
// CIMGUI_API void igBulletText(const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     ImGui::BulletTextV(fmt, args);
//     va_end(args);
// }
//
// CIMGUI_API void igBulletTextV(const char* fmt, va_list args)
// {
//     ImGui::BulletTextV(fmt, args);
// }
//
// CIMGUI_API bool igButton(const char* label, const ImVec2 size)
// {
//     return ImGui::Button(label, size);
// }
//
// CIMGUI_API bool igSmallButton(const char* label)
// {
//     return ImGui::SmallButton(label);
// }
//
// CIMGUI_API bool igInvisibleButton(const char* str_id, const ImVec2 size, int flags)
// {
//     return ImGui::InvisibleButton(str_id, size, (ImGuiButtonFlags)flags);
// }
//
// CIMGUI_API bool igArrowButton(const char* str_id, ImGuiDir dir)
// {
//     return ImGui::ArrowButton(str_id, dir);
// }
//
// CIMGUI_API void igImage(ImTextureID user_texture_id, const ImVec2 size, const ImVec2 uv0, const ImVec2 uv1, const ImVec4 tint_col, const ImVec4 border_col)
// {
//     ImGui::Image(user_texture_id, size, uv0, uv1, tint_col, border_col);
// }
//
// CIMGUI_API bool igImageButton(const char* str_id, ImTextureID user_texture_id, const ImVec2 size, const ImVec2 uv0, const ImVec2 uv1, const ImVec4 bg_col, const ImVec4 tint_col)
// {
//     return ImGui::ImageButton(str_id, user_texture_id, size, uv0, uv1, bg_col, tint_col);
// }
//
// CIMGUI_API bool igCheckbox(const char* label, bool* v)
// {
//     return ImGui::Checkbox(label, v);
// }
//
// CIMGUI_API bool igCheckboxFlagsIntPtr(const char* label, int* flags, int flags_value)
// {
//     return ImGui::CheckboxFlags(label, flags, flags_value);
// }
//
// CIMGUI_API bool igCheckboxFlagsUintPtr(const char* label, unsigned int* flags, unsigned int flags_value)
// {
//     return ImGui::CheckboxFlags(label, flags, flags_value);
// }
//
// CIMGUI_API bool igRadioButtonBool(const char* label, bool active)
// {
//     return ImGui::RadioButton(label, active);
// }
//
// CIMGUI_API bool igRadioButtonIntPtr(const char* label, int* v, int v_button)
// {
//     return ImGui::RadioButton(label, v, v_button);
// }
//
// CIMGUI_API void igProgressBar(float fraction, const ImVec2 size_arg, const char* overlay)
// {
//     ImGui::ProgressBar(fraction, size_arg, overlay);
// }
//
// CIMGUI_API void igBullet()
// {
//     ImGui::Bullet();
// }
//
// CIMGUI_API bool igBeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags)
// {
//     return ImGui::BeginCombo(label, preview_value, flags);
// }
//
// CIMGUI_API void igEndCombo()
// {
//     ImGui::EndCombo();
// }
//
// CIMGUI_API bool igCombo(const char* label, int* current_item, const char* const items[], int items_count, int popup_max_height_in_items)
// {
//     return ImGui::Combo(label, current_item, items, items_count, popup_max_height_in_items);
// }
//
// CIMGUI_API bool igComboStr(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items)
// {
//     return ImGui::Combo(label, current_item, items_separated_by_zeros, popup_max_height_in_items);
// }
//
// CIMGUI_API bool igComboFnPtr(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int popup_max_height_in_items)
// {
//     return ImGui::Combo(label, current_item, getter, user_data, items_count, popup_max_height_in_items);
// }
//
// CIMGUI_API bool igDragFloat(const char* label, float* v, float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::DragFloat(label, v, v_speed, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igDragFloat2(const char* label, float v[2], float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::DragFloat2(label, v, v_speed, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igDragFloat3(const char* label, float v[3], float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::DragFloat3(label, v, v_speed, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igDragFloat4(const char* label, float v[4], float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::DragFloat4(label, v, v_speed, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igDragFloatRange2(const char* label, float* v_current_min, float* v_current_max, float v_speed, float v_min, float v_max, const char* format, const char* format_max, ImGuiSliderFlags flags)
// {
//     return ImGui::DragFloatRange2(label, v_current_min, v_current_max, v_speed, v_min, v_max, format, format_max, flags);
// }
//
// CIMGUI_API bool igDragInt(const char* label, int* v, float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::DragInt(label, v, v_speed, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igDragInt2(const char* label, int v[2], float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::DragInt2(label, v, v_speed, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igDragInt3(const char* label, int v[3], float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::DragInt3(label, v, v_speed, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igDragInt4(const char* label, int v[4], float v_speed, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::DragInt4(label, v, v_speed, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igDragIntRange2(const char* label, int* v_current_min, int* v_current_max, float v_speed, int v_min, int v_max, const char* format, const char* format_max, ImGuiSliderFlags flags)
// {
//     return ImGui::DragIntRange2(label, v_current_min, v_current_max, v_speed, v_min, v_max, format, format_max, flags);
// }
//
// CIMGUI_API bool igDragScalar(const char* label, int data_type, void* p_data, float v_speed, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::DragScalar(label, (ImGuiDataType)data_type, p_data, v_speed, p_min, p_max, format, flags);
// }
//
// CIMGUI_API bool igDragScalarN(const char* label, int data_type, void* p_data, int components, float v_speed, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::DragScalarN(label, (ImGuiDataType)data_type, p_data, components, v_speed, p_min, p_max, format, flags);
// }
//
// CIMGUI_API bool igSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::SliderFloat(label, v, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igSliderFloat2(const char* label, float v[2], float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::SliderFloat2(label, v, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igSliderFloat3(const char* label, float v[3], float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::SliderFloat3(label, v, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igSliderFloat4(const char* label, float v[4], float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::SliderFloat4(label, v, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igSliderAngle(const char* label, float* v_rad, float v_degrees_min, float v_degrees_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::SliderAngle(label, v_rad, v_degrees_min, v_degrees_max, format, flags);
// }
//
// CIMGUI_API bool igSliderInt(const char* label, int* v, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::SliderInt(label, v, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igSliderInt2(const char* label, int v[2], int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::SliderInt2(label, v, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igSliderInt3(const char* label, int v[3], int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::SliderInt3(label, v, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igSliderInt4(const char* label, int v[4], int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::SliderInt4(label, v, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igSliderScalar(const char* label, int data_type, void* p_data, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::SliderScalar(label, (ImGuiDataType)data_type, p_data, p_min, p_max, format, flags);
// }
//
// CIMGUI_API bool igSliderScalarN(const char* label, int data_type, void* p_data, int components, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::SliderScalarN(label, (ImGuiDataType)data_type, p_data, components, p_min, p_max, format, flags);
// }
//
// CIMGUI_API bool igVSliderFloat(const char* label, const ImVec2 size, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::VSliderFloat(label, size, v, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igVSliderInt(const char* label, const ImVec2 size, int* v, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::VSliderInt(label, size, v, v_min, v_max, format, flags);
// }
//
// CIMGUI_API bool igVSliderScalar(const char* label, const ImVec2 size, int data_type, void* p_data, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
// {
//     return ImGui::VSliderScalar(label, size, (ImGuiDataType)data_type, p_data, p_min, p_max, format, flags);
// }
//
// CIMGUI_API bool igInputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
// {
//     return ImGui::InputText(label, buf, buf_size, flags, callback, user_data);
// }
//
// CIMGUI_API bool igInputTextMultiline(const char* label, char* buf, size_t buf_size, const ImVec2 size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
// {
//     return ImGui::InputTextMultiline(label, buf, buf_size, size, flags, callback, user_data);
// }
//
// CIMGUI_API bool igInputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
// {
//     return ImGui::InputTextWithHint(label, hint, buf, buf_size, flags, callback, user_data);
// }
//
// CIMGUI_API bool igInputFloat(const char* label, float* v, float step, float step_fast, const char* format, ImGuiInputTextFlags flags)
// {
//     return ImGui::InputFloat(label, v, step, step_fast, format, flags);
// }
//
// CIMGUI_API bool igInputFloat2(const char* label, float v[2], const char* format, ImGuiInputTextFlags flags)
// {
//     return ImGui::InputFloat2(label, v, format, flags);
// }
//
// CIMGUI_API bool igInputFloat3(const char* label, float v[3], const char* format, ImGuiInputTextFlags flags)
// {
//     return ImGui::InputFloat3(label, v, format, flags);
// }
//
// CIMGUI_API bool igInputFloat4(const char* label, float v[4], const char* format, ImGuiInputTextFlags flags)
// {
//     return ImGui::InputFloat4(label, v, format, flags);
// }
//
// CIMGUI_API bool igInputInt(const char* label, int* v, int step, int step_fast, ImGuiInputTextFlags flags)
// {
//     return ImGui::InputInt(label, v, step, step_fast, flags);
// }
//
// CIMGUI_API bool igInputInt2(const char* label, int v[2], ImGuiInputTextFlags flags)
// {
//     return ImGui::InputInt2(label, v, flags);
// }
//
// CIMGUI_API bool igInputInt3(const char* label, int v[3], ImGuiInputTextFlags flags)
// {
//     return ImGui::InputInt3(label, v, flags);
// }
//
// CIMGUI_API bool igInputInt4(const char* label, int v[4], ImGuiInputTextFlags flags)
// {
//     return ImGui::InputInt4(label, v, flags);
// }
//
// CIMGUI_API bool igInputDouble(const char* label, double* v, double step, double step_fast, const char* format, ImGuiInputTextFlags flags)
// {
//     return ImGui::InputDouble(label, v, step, step_fast, format, flags);
// }
//
// CIMGUI_API bool igInputScalar(const char* label, int data_type, void* p_data, const void* p_step, const void* p_step_fast, const char* format, ImGuiInputTextFlags flags)
// {
//     return ImGui::InputScalar(label, (ImGuiDataType)data_type, p_data, p_step, p_step_fast, format, flags);
// }
//
// CIMGUI_API bool igInputScalarN(const char* label, int data_type, void* p_data, int components, const void* p_step, const void* p_step_fast, const char* format, ImGuiInputTextFlags flags)
// {
//     return ImGui::InputScalarN(label, (ImGuiDataType)data_type, p_data, components, p_step, p_step_fast, format, flags);
// }
//
// CIMGUI_API bool igColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags)
// {
//     return ImGui::ColorEdit3(label, col, flags);
// }
//
// CIMGUI_API bool igColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags)
// {
//     return ImGui::ColorEdit4(label, col, flags);
// }
//
// CIMGUI_API bool igColorPicker3(const char* label, float col[3], ImGuiColorEditFlags flags)
// {
//     return ImGui::ColorPicker3(label, col, flags);
// }
//
// CIMGUI_API bool igColorPicker4(const char* label, float col[4], ImGuiColorEditFlags flags, const float* ref_col)
// {
//     return ImGui::ColorPicker4(label, col, flags, ref_col);
// }
//
// CIMGUI_API bool igColorButton(const char* desc_id, const ImVec4 col, ImGuiColorEditFlags flags, ImVec2 size)
// {
//     return ImGui::ColorButton(desc_id, col, flags, size);
// }
//
// CIMGUI_API void igSetColorEditOptions(ImGuiColorEditFlags flags)
// {
//     ImGui::SetColorEditOptions(flags);
// }
//
// CIMGUI_API bool igTreeNodeStr(const char* label)
// {
//     return ImGui::TreeNode(label);
// }
//
// CIMGUI_API bool igTreeNodeStrStr(const char* str_id, const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     bool ret = ImGui::TreeNodeV(str_id, fmt, args);
//     va_end(args);
//     return ret;
// }
//
// CIMGUI_API bool igTreeNodePtr(const void* ptr_id, const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     bool ret = ImGui::TreeNodeV(ptr_id, fmt, args);
//     va_end(args);
//     return ret;
// }
//
// CIMGUI_API bool igTreeNodeV(const char* str_id, const char* fmt, va_list args)
// {
//     return ImGui::TreeNodeV(str_id, fmt, args);
// }
//
// CIMGUI_API bool igTreeNodeVPtr(const void* ptr_id, const char* fmt, va_list args)
// {
//     return ImGui::TreeNodeV(ptr_id, fmt, args);
// }
//
// CIMGUI_API bool igTreeNodeExStr(const char* label, ImGuiTreeNodeFlags flags)
// {
//     return ImGui::TreeNodeEx(label, flags);
// }
//
// CIMGUI_API bool igTreeNodeExStrStr(const char* str_id, ImGuiTreeNodeFlags flags, const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     bool ret = ImGui::TreeNodeExV(str_id, flags, fmt, args);
//     va_end(args);
//     return ret;
// }
//
// CIMGUI_API bool igTreeNodeExPtr(const void* ptr_id, ImGuiTreeNodeFlags flags, const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     bool ret = ImGui::TreeNodeExV(ptr_id, flags, fmt, args);
//     va_end(args);
//     return ret;
// }
//
// CIMGUI_API bool igTreeNodeExV(const char* str_id, ImGuiTreeNodeFlags flags, const char* fmt, va_list args)
// {
//     return ImGui::TreeNodeExV(str_id, flags, fmt, args);
// }
//
// CIMGUI_API bool igTreeNodeExVPtr(const void* ptr_id, ImGuiTreeNodeFlags flags, const char* fmt, va_list args)
// {
//     return ImGui::TreeNodeExV(ptr_id, flags, fmt, args);
// }
//
// CIMGUI_API void igTreePushStr(const char* str_id)
// {
//     ImGui::TreePush(str_id);
// }
//
// CIMGUI_API void igTreePushPtr(const void* ptr_id)
// {
//     ImGui::TreePush(ptr_id);
// }
//
// CIMGUI_API void igTreePop()
// {
//     ImGui::TreePop();
// }
//
// CIMGUI_API float igGetTreeNodeToLabelSpacing()
// {
//     return ImGui::GetTreeNodeToLabelSpacing();
// }
//
// CIMGUI_API void igSetNextItemOpen(bool is_open, ImGuiCond cond)
// {
//     ImGui::SetNextItemOpen(is_open, cond);
// }
//
// CIMGUI_API bool igCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags)
// {
//     return ImGui::CollapsingHeader(label, flags);
// }
//
// CIMGUI_API bool igCollapsingHeaderBoolPtr(const char* label, bool* p_open, ImGuiTreeNodeFlags flags)
// {
//     return ImGui::CollapsingHeader(label, p_open, flags);
// }
//
// CIMGUI_API bool igSelectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2 size)
// {
//     return ImGui::Selectable(label, selected, flags, size);
// }
//
// CIMGUI_API bool igSelectableBoolPtr(const char* label, bool* p_selected, ImGuiSelectableFlags flags, const ImVec2 size)
// {
//     return ImGui::Selectable(label, p_selected, flags, size);
// }
//
// CIMGUI_API bool igBeginListBox(const char* label, const ImVec2 size)
// {
//     return ImGui::BeginListBox(label, size);
// }
//
// CIMGUI_API void igEndListBox()
// {
//     ImGui::EndListBox();
// }
//
// CIMGUI_API bool igListBoxStr_arr(const char* label, int* current_item, const char* const items[], int items_count, int height_in_items)
// {
//     return ImGui::ListBox(label, current_item, items, items_count, height_in_items);
// }
//
// CIMGUI_API bool igListBoxFnPtr(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int height_in_items)
// {
//     return ImGui::ListBox(label, current_item, getter, user_data, items_count, height_in_items);
// }
//
// CIMGUI_API void igPlotLines(const char* label, const float* values, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, ImVec2 graph_size, int stride)
// {
//     ImGui::PlotLines(label, values, values_count, values_offset, overlay_text, scale_min, scale_max, graph_size, stride);
// }
//
// CIMGUI_API void igPlotLinesFnPtr(const char* label, float (*values_getter)(void* data, int idx), void* data, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, ImVec2 graph_size)
// {
//     ImGui::PlotLines(label, values_getter, data, values_count, values_offset, overlay_text, scale_min, scale_max, graph_size);
// }
//
// CIMGUI_API void igPlotHistogramFloatPtr(const char* label, const float* values, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, ImVec2 graph_size, int stride)
// {
//     ImGui::PlotHistogram(label, values, values_count, values_offset, overlay_text, scale_min, scale_max, graph_size, stride);
// }
//
// CIMGUI_API void igPlotHistogramFnPtr(const char* label, float (*values_getter)(void* data, int idx), void* data, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, ImVec2 graph_size)
// {
//     ImGui::PlotHistogram(label, values_getter, data, values_count, values_offset, overlay_text, scale_min, scale_max, graph_size);
// }
//
// CIMGUI_API void igValueBool(const char* prefix, bool b)
// {
//     ImGui::Value(prefix, b);
// }
//
// CIMGUI_API void igValueInt(const char* prefix, int v)
// {
//     ImGui::Value(prefix, v);
// }
//
// CIMGUI_API void igValueUint(const char* prefix, unsigned int v)
// {
//     ImGui::Value(prefix, v);
// }
//
// CIMGUI_API void igValueFloat(const char* prefix, float v, const char* float_format)
// {
//     ImGui::Value(prefix, v, float_format);
// }
//
// CIMGUI_API bool igBeginMenuBar()
// {
//     return ImGui::BeginMenuBar();
// }
//
// CIMGUI_API void igEndMenuBar()
// {
//     ImGui::EndMenuBar();
// }
//
// CIMGUI_API bool igBeginMainMenuBar()
// {
//     return ImGui::BeginMainMenuBar();
// }
//
// CIMGUI_API void igEndMainMenuBar()
// {
//     ImGui::EndMainMenuBar();
// }
//
// CIMGUI_API bool igBeginMenu(const char* label, bool enabled)
// {
//     return ImGui::BeginMenu(label, enabled);
// }
//
// CIMGUI_API void igEndMenu()
// {
//     ImGui::EndMenu();
// }
//
// CIMGUI_API bool igMenuItemBool(const char* label, const char* shortcut, bool selected, bool enabled)
// {
//     return ImGui::MenuItem(label, shortcut, selected, enabled);
// }
//
// CIMGUI_API bool igMenuItemBoolPtr(const char* label, const char* shortcut, bool* p_selected, bool enabled)
// {
//     return ImGui::MenuItem(label, shortcut, p_selected, enabled);
// }
//
// CIMGUI_API bool igBeginTooltip()
// {
//     return ImGui::BeginTooltip();
// }
//
// CIMGUI_API void igEndTooltip()
// {
//     ImGui::EndTooltip();
// }
//
// CIMGUI_API void igSetTooltip(const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     ImGui::SetTooltipV(fmt, args);
//     va_end(args);
// }
//
// CIMGUI_API void igSetTooltipV(const char* fmt, va_list args)
// {
//     ImGui::SetTooltipV(fmt, args);
// }
//
// CIMGUI_API bool igBeginPopup(const char* str_id, ImGuiWindowFlags flags)
// {
//     return ImGui::BeginPopup(str_id, flags);
// }
//
// CIMGUI_API bool igBeginPopupModal(const char* name, bool* p_open, ImGuiWindowFlags flags)
// {
//     return ImGui::BeginPopupModal(name, p_open, flags);
// }
//
// CIMGUI_API void igEndPopup()
// {
//     ImGui::EndPopup();
// }
//
// CIMGUI_API void igOpenPopup(const char* str_id, ImGuiPopupFlags popup_flags)
// {
//     ImGui::OpenPopup(str_id, popup_flags);
// }
//
// CIMGUI_API void igOpenPopupOnItemClick(const char* str_id, ImGuiPopupFlags popup_flags)
// {
//     ImGui::OpenPopupOnItemClick(str_id, popup_flags);
// }
//
// CIMGUI_API void igCloseCurrentPopup()
// {
//     ImGui::CloseCurrentPopup();
// }
//
// CIMGUI_API bool igBeginPopupContextItem(const char* str_id, ImGuiPopupFlags popup_flags)
// {
//     return ImGui::BeginPopupContextItem(str_id, popup_flags);
// }
//
// CIMGUI_API bool igBeginPopupContextWindow(const char* str_id, ImGuiPopupFlags popup_flags)
// {
//     return ImGui::BeginPopupContextWindow(str_id, popup_flags);
// }
//
// CIMGUI_API bool igBeginPopupContextVoid(const char* str_id, ImGuiPopupFlags popup_flags)
// {
//     return ImGui::BeginPopupContextVoid(str_id, popup_flags);
// }
//
// CIMGUI_API bool igIsPopupOpenStr(const char* str_id, ImGuiPopupFlags flags)
// {
//     return ImGui::IsPopupOpen(str_id, flags);
// }
//
// CIMGUI_API void igColumns(int count, const char* id, bool border)
// {
//     ImGui::Columns(count, id, border);
// }
//
// CIMGUI_API void igNextColumn()
// {
//     ImGui::NextColumn();
// }
//
// CIMGUI_API int igGetColumnIndex()
// {
//     return ImGui::GetColumnIndex();
// }
//
// CIMGUI_API float igGetColumnWidth(int column_index)
// {
//     return ImGui::GetColumnWidth(column_index);
// }
//
// CIMGUI_API void igSetColumnWidth(int column_index, float width)
// {
//     ImGui::SetColumnWidth(column_index, width);
// }
//
// CIMGUI_API float igGetColumnOffset(int column_index)
// {
//     return ImGui::GetColumnOffset(column_index);
// }
//
// CIMGUI_API void igSetColumnOffset(int column_index, float offset_x)
// {
//     ImGui::SetColumnOffset(column_index, offset_x);
// }
//
// CIMGUI_API int igGetColumnsCount()
// {
//     return ImGui::GetColumnsCount();
// }
//
// CIMGUI_API bool igBeginTabBar(const char* str_id, ImGuiTabBarFlags flags)
// {
//     return ImGui::BeginTabBar(str_id, flags);
// }
//
// CIMGUI_API void igEndTabBar()
// {
//     ImGui::EndTabBar();
// }
//
// CIMGUI_API bool igBeginTabItem(const char* label, bool* p_open, ImGuiTabItemFlags flags)
// {
//     return ImGui::BeginTabItem(label, p_open, flags);
// }
//
// CIMGUI_API void igEndTabItem()
// {
//     ImGui::EndTabItem();
// }
//
// CIMGUI_API bool igTabItemButton(const char* label, ImGuiTabItemFlags flags)
// {
//     return ImGui::TabItemButton(label, flags);
// }
//
// CIMGUI_API void igSetTabItemClosed(const char* tab_or_docked_window_label)
// {
//     ImGui::SetTabItemClosed(tab_or_docked_window_label);
// }
//
// CIMGUI_API void igLogToTTY(int auto_open_depth)
// {
//     ImGui::LogToTTY(auto_open_depth);
// }
//
// CIMGUI_API void igLogToFile(int auto_open_depth, const char* filename)
// {
//     ImGui::LogToFile(auto_open_depth, filename);
// }
//
// CIMGUI_API void igLogToClipboard(int auto_open_depth)
// {
//     ImGui::LogToClipboard(auto_open_depth);
// }
//
// CIMGUI_API void igLogFinish()
// {
//     ImGui::LogFinish();
// }
//
// CIMGUI_API void igLogButtons()
// {
//     ImGui::LogButtons();
// }
//
// CIMGUI_API void igLogText(const char* fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     ImGui::LogTextV(fmt, args);
//     va_end(args);
// }
//
// CIMGUI_API bool igBeginDragDropSource(ImGuiDragDropFlags flags)
// {
//     return ImGui::BeginDragDropSource(flags);
// }
//
// CIMGUI_API bool igSetDragDropPayload(const char* type, const void* data, size_t sz, ImGuiCond cond)
// {
//     return ImGui::SetDragDropPayload(type, data, sz, cond);
// }
//
// CIMGUI_API void igEndDragDropSource()
// {
//     ImGui::EndDragDropSource();
// }
//
// CIMGUI_API bool igBeginDragDropTarget()
// {
//     return ImGui::BeginDragDropTarget();
// }
//
// CIMGUI_API const ImGuiPayload* igAcceptDragDropPayload(const char* type, ImGuiDragDropFlags flags)
// {
//     return ImGui::AcceptDragDropPayload(type, flags);
// }
//
// CIMGUI_API void igEndDragDropTarget()
// {
//     ImGui::EndDragDropTarget();
// }
//
// CIMGUI_API const ImGuiPayload* igGetDragDropPayload()
// {
//     return ImGui::GetDragDropPayload();
// }
//
// CIMGUI_API void igPushClipRect(const ImVec2 clip_rect_min, const ImVec2 clip_rect_max, bool intersect_with_current_clip_rect)
// {
//     ImGui::PushClipRect(clip_rect_min, clip_rect_max, intersect_with_current_clip_rect);
// }
//
// CIMGUI_API void igPopClipRect()
// {
//     ImGui::PopClipRect();
// }
//
// CIMGUI_API void igSetItemDefaultFocus()
// {
//     ImGui::SetItemDefaultFocus();
// }
//
// CIMGUI_API void igSetKeyboardFocusHere(int offset)
// {
//     ImGui::SetKeyboardFocusHere(offset);
// }
//
// CIMGUI_API bool igIsItemHovered(ImGuiHoveredFlags flags)
// {
//     return ImGui::IsItemHovered(flags);
// }
//
// CIMGUI_API bool igIsItemActive()
// {
//     return ImGui::IsItemActive();
// }
//
// CIMGUI_API bool igIsItemFocused()
// {
//     return ImGui::IsItemFocused();
// }
//
// CIMGUI_API bool igIsItemClicked(ImGuiMouseButton mouse_button)
// {
//     return ImGui::IsItemClicked(mouse_button);
// }
//
// CIMGUI_API bool igIsItemVisible()
// {
//     return ImGui::IsItemVisible();
// }
//
// CIMGUI_API bool igIsItemEdited()
// {
//     return ImGui::IsItemEdited();
// }
//
// CIMGUI_API bool igIsItemActivated()
// {
//     return ImGui::IsItemActivated();
// }
//
// CIMGUI_API bool igIsItemDeactivated()
// {
//     return ImGui::IsItemDeactivated();
// }
//
// CIMGUI_API bool igIsItemDeactivatedAfterEdit()
// {
//     return ImGui::IsItemDeactivatedAfterEdit();
// }
//
// CIMGUI_API bool igIsItemToggledOpen()
// {
//     return ImGui::IsItemToggledOpen();
// }
//
// CIMGUI_API bool igIsAnyItemHovered()
// {
//     return ImGui::IsAnyItemHovered();
// }
//
// CIMGUI_API bool igIsAnyItemActive()
// {
//     return ImGui::IsAnyItemActive();
// }
//
// CIMGUI_API bool igIsAnyItemFocused()
// {
//     return ImGui::IsAnyItemFocused();
// }
//
// CIMGUI_API void igGetItemRectMin(ImVec2* pOut)
// {
//     *pOut = ImGui::GetItemRectMin();
// }
//
// CIMGUI_API void igGetItemRectMax(ImVec2* pOut)
// {
//     *pOut = ImGui::GetItemRectMax();
// }
//
// CIMGUI_API void igGetItemRectSize(ImVec2* pOut)
// {
//     *pOut = ImGui::GetItemRectSize();
// }
//
// CIMGUI_API void igSetItemAllowOverlap()
// {
//     ImGui::SetItemAllowOverlap();
// }
//
// CIMGUI_API ImGuiViewport* igGetMainViewport()
// {
//     return ImGui::GetMainViewport();
// }
//
// CIMGUI_API ImDrawList* igGetBackgroundDrawList()
// {
//     return ImGui::GetBackgroundDrawList();
// }
//
// CIMGUI_API ImDrawList* igGetForegroundDrawList()
// {
//     return ImGui::GetForegroundDrawList();
// }
//
// CIMGUI_API bool igIsRectVisible(const ImVec2 size)
// {
//     return ImGui::IsRectVisible(size);
// }
//
// CIMGUI_API bool igIsRectVisibleVec2(const ImVec2 rect_min, const ImVec2 rect_max)
// {
//     return ImGui::IsRectVisible(rect_min, rect_max);
// }
//
// CIMGUI_API double igGetTime()
// {
//     return ImGui::GetTime();
// }
//
// CIMGUI_API int igGetFrameCount()
// {
//     return ImGui::GetFrameCount();
// }
//
// CIMGUI_API void* igGetDrawListSharedData()
// {
//     return (void*)ImGui::GetDrawListSharedData();
// }
//
// CIMGUI_API const char* igGetStyleColorName(ImGuiCol idx)
// {
//     return ImGui::GetStyleColorName(idx);
// }
//
// CIMGUI_API void igSetStateStorage(ImGuiStorage* storage)
// {
//     ImGui::SetStateStorage(storage);
// }
//
// CIMGUI_API ImGuiStorage* igGetStateStorage()
// {
//     return ImGui::GetStateStorage();
// }
//
// CIMGUI_API bool igBeginChildFrame(ImGuiID id, const ImVec2 size, ImGuiWindowFlags flags)
// {
//     return ImGui::BeginChildFrame(id, size, flags);
// }
//
// CIMGUI_API void igEndChildFrame()
// {
//     ImGui::EndChildFrame();
// }
//
// CIMGUI_API void igCalcTextSize(ImVec2* pOut, const char* text, const char* text_end, bool hide_text_after_double_hash, float wrap_width)
// {
//     *pOut = ImGui::CalcTextSize(text, text_end, hide_text_after_double_hash, wrap_width);
// }
//
// CIMGUI_API void igColorConvertU32ToFloat4(ImVec4* pOut, ImU32 in)
// {
//     *pOut = ImGui::ColorConvertU32ToFloat4(in);
// }
//
// CIMGUI_API ImU32 igColorConvertFloat4ToU32(const ImVec4 in)
// {
//     return ImGui::ColorConvertFloat4ToU32(in);
// }
//
// CIMGUI_API void igColorConvertRGBtoHSV(float r, float g, float b, float* out_h, float* out_s, float* out_v)
// {
//     ImGui::ColorConvertRGBtoHSV(r, g, b, *out_h, *out_s, *out_v);
// }
//
// CIMGUI_API void igColorConvertHSVtoRGB(float h, float s, float v, float* out_r, float* out_g, float* out_b)
// {
//     ImGui::ColorConvertHSVtoRGB(h, s, v, *out_r, *out_g, *out_b);
// }
//
// CIMGUI_API bool igIsKeyDown(ImGuiKey key)
// {
//     return ImGui::IsKeyDown(key);
// }
//
// CIMGUI_API bool igIsKeyPressed(ImGuiKey key, bool repeat)
// {
//     return ImGui::IsKeyPressed(key, repeat);
// }
//
// CIMGUI_API bool igIsKeyReleased(ImGuiKey key)
// {
//     return ImGui::IsKeyReleased(key);
// }
//
// CIMGUI_API int igGetKeyPressedAmount(ImGuiKey key, float repeat_delay, float rate)
// {
//     return ImGui::GetKeyPressedAmount(key, repeat_delay, rate);
// }
//
// CIMGUI_API const char* igGetKeyName(ImGuiKey key)
// {
//     return ImGui::GetKeyName(key);
// }
//
// CIMGUI_API bool igIsMouseDown(ImGuiMouseButton button)
// {
//     return ImGui::IsMouseDown(button);
// }
//
// CIMGUI_API bool igIsMouseClicked(ImGuiMouseButton button, bool repeat)
// {
//     return ImGui::IsMouseClicked(button, repeat);
// }
//
// CIMGUI_API bool igIsMouseReleased(ImGuiMouseButton button)
// {
//     return ImGui::IsMouseReleased(button);
// }
//
// CIMGUI_API bool igIsMouseDoubleClicked(ImGuiMouseButton button)
// {
//     return ImGui::IsMouseDoubleClicked(button);
// }
//
// CIMGUI_API int igGetMouseClickedCount(ImGuiMouseButton button)
// {
//     return ImGui::GetMouseClickedCount(button);
// }
//
// CIMGUI_API bool igIsMouseHoveringRect(const ImVec2 r_min, const ImVec2 r_max, bool clip)
// {
//     return ImGui::IsMouseHoveringRect(r_min, r_max, clip);
// }
//
// CIMGUI_API bool igIsMousePosValid(const ImVec2* mouse_pos)
// {
//     return ImGui::IsMousePosValid(mouse_pos);
// }
//
// CIMGUI_API bool igIsAnyMouseDown()
// {
//     return ImGui::IsAnyMouseDown();
// }
//
// CIMGUI_API void igGetMousePos(ImVec2* pOut)
// {
//     *pOut = ImGui::GetMousePos();
// }
//
// CIMGUI_API void igGetMousePosOnOpeningCurrentPopup(ImVec2* pOut)
// {
//     *pOut = ImGui::GetMousePosOnOpeningCurrentPopup();
// }
//
// CIMGUI_API bool igIsMouseDragging(ImGuiMouseButton button, float lock_threshold)
// {
//     return ImGui::IsMouseDragging(button, lock_threshold);
// }
//
// CIMGUI_API void igGetMouseDragDelta(ImVec2* pOut, ImGuiMouseButton button, float lock_threshold)
// {
//     *pOut = ImGui::GetMouseDragDelta(button, lock_threshold);
// }
//
// CIMGUI_API void igResetMouseDragDelta(ImGuiMouseButton button)
// {
//     ImGui::ResetMouseDragDelta(button);
// }
//
// CIMGUI_API ImGuiMouseCursor igGetMouseCursor()
// {
//     return ImGui::GetMouseCursor();
// }
//
// CIMGUI_API void igSetMouseCursor(ImGuiMouseCursor cursor_type)
// {
//     ImGui::SetMouseCursor(cursor_type);
// }
//
// CIMGUI_API const char* igGetClipboardText()
// {
//     return ImGui::GetClipboardText();
// }
//
// CIMGUI_API void igSetClipboardText(const char* text)
// {
//     ImGui::SetClipboardText(text);
// }
//
// CIMGUI_API void igLoadIniSettingsFromDisk(const char* ini_filename)
// {
//     ImGui::LoadIniSettingsFromDisk(ini_filename);
// }
//
// CIMGUI_API void igLoadIniSettingsFromMemory(const char* ini_data, size_t ini_size)
// {
//     ImGui::LoadIniSettingsFromMemory(ini_data, ini_size);
// }
//
// CIMGUI_API void igSaveIniSettingsToDisk(const char* ini_filename)
// {
//     ImGui::SaveIniSettingsToDisk(ini_filename);
// }
//
// CIMGUI_API const char* igSaveIniSettingsToMemory(size_t* out_ini_size)
// {
//     return ImGui::SaveIniSettingsToMemory(out_ini_size);
// }
//
// CIMGUI_API void* igMemAlloc(size_t size)
// {
//     return ImGui::MemAlloc(size);
// }
//
// CIMGUI_API void igMemFree(void* ptr)
// {
//     ImGui::MemFree(ptr);
// }
//
// CIMGUI_API void ImGuiTextFilter_Create(ImGuiTextFilter* filter, const char* default_filter)
// {
//     IM_PLACEMENT_NEW(filter) ImGuiTextFilter(default_filter);
// }
//
// CIMGUI_API void ImGuiTextFilter_Destroy(ImGuiTextFilter* filter)
// {
//     filter->~ImGuiTextFilter();
// }
//
// CIMGUI_API bool ImGuiTextFilter_Draw(ImGuiTextFilter* filter, const char* label, float width)
// {
//     return filter->Draw(label, width);
// }
//
// CIMGUI_API bool ImGuiTextFilter_PassFilter(ImGuiTextFilter* filter, const char* text, const char* text_end)
// {
//     return filter->PassFilter(text, text_end);
// }
//
// CIMGUI_API void ImGuiTextFilter_Build(ImGuiTextFilter* filter)
// {
//     filter->Build();
// }
//
// CIMGUI_API void ImGuiTextFilter_Clear(ImGuiTextFilter* filter)
// {
//     filter->Clear();
// }
//
// CIMGUI_API bool ImGuiTextFilter_IsActive(ImGuiTextFilter* filter)
// {
//     return filter->IsActive();
// }
//
// CIMGUI_API void ImGuiTextBuffer_Create(ImGuiTextBuffer* buffer)
// {
//     IM_PLACEMENT_NEW(buffer) ImGuiTextBuffer();
// }
//
// CIMGUI_API void ImGuiTextBuffer_Destroy(ImGuiTextBuffer* buffer)
// {
//     buffer->~ImGuiTextBuffer();
// }
//
// CIMGUI_API const char* ImGuiTextBuffer_begin(ImGuiTextBuffer* buffer)
// {
//     return buffer->begin();
// }
//
// CIMGUI_API const char* ImGuiTextBuffer_end(ImGuiTextBuffer* buffer)
// {
//     return buffer->end();
// }
//
// CIMGUI_API int ImGuiTextBuffer_size(ImGuiTextBuffer* buffer)
// {
//     return buffer->size();
// }
//
// CIMGUI_API bool ImGuiTextBuffer_empty(ImGuiTextBuffer* buffer)
// {
//     return buffer->empty();
// }
//
// CIMGUI_API void ImGuiTextBuffer_clear(ImGuiTextBuffer* buffer)
// {
//     buffer->clear();
// }
//
// CIMGUI_API const char* ImGuiTextBuffer_c_str(ImGuiTextBuffer* buffer)
// {
//     return buffer->c_str();
// }
//
// CIMGUI_API void ImGuiTextBuffer_append(ImGuiTextBuffer* buffer, const char* str, const char* str_end)
// {
//     buffer->append(str, str_end);
// }
//
// CIMGUI_API void ImGuiStorage_BuildSortByKey(ImGuiStorage* storage)
// {
//     storage->BuildSortByKey();
// }
//
// CIMGUI_API void ImGuiListClipper_Create(ImGuiListClipper* clipper)
// {
//     IM_PLACEMENT_NEW(clipper) ImGuiListClipper();
// }
//
// CIMGUI_API void ImGuiListClipper_Destroy(ImGuiListClipper* clipper)
// {
//     clipper->~ImGuiListClipper();
// }
//
// CIMGUI_API void ImGuiListClipper_Begin(ImGuiListClipper* clipper, int items_count, float items_height)
// {
//     clipper->Begin(items_count, items_height);
// }
//
// CIMGUI_API void ImGuiListClipper_End(ImGuiListClipper* clipper)
// {
//     clipper->End();
// }
//
// CIMGUI_API bool ImGuiListClipper_Step(ImGuiListClipper* clipper)
// {
//     return clipper->Step();
// }
//
// CIMGUI_API bool ImGui_ImplSDL3_InitForOpenGL(void* window, void* gl_context)
// {
//     return ImGui_ImplSDL3_InitForOpenGL((SDL_Window*)window, gl_context);
// }
//
// CIMGUI_API bool ImGui_ImplSDL3_InitForVulkan(void* window)
// {
//     return ImGui_ImplSDL3_InitForVulkan((SDL_Window*)window);
// }
//
// CIMGUI_API bool ImGui_ImplSDL3_InitForD3D(void* window)
// {
//     return ImGui_ImplSDL3_InitForD3D((SDL_Window*)window);
// }
//
// CIMGUI_API bool ImGui_ImplSDL3_InitForMetal(void* window)
// {
//     return ImGui_ImplSDL3_InitForMetal((SDL_Window*)window);
// }
//
// CIMGUI_API bool ImGui_ImplSDL3_InitForSDLRenderer(void* window, void* renderer)
// {
//     return ImGui_ImplSDL3_InitForSDLRenderer((SDL_Window*)window, (SDL_Renderer*)renderer);
// }
//
// CIMGUI_API void ImGui_ImplSDL3_Shutdown()
// {
//     ImGui_ImplSDL3_Shutdown();
// }
//
// CIMGUI_API void ImGui_ImplSDL3_NewFrame()
// {
//     ImGui_ImplSDL3_NewFrame();
// }
//
// CIMGUI_API bool ImGui_ImplSDL3_ProcessEvent(const void* event)
// {
//     return ImGui_ImplSDL3_ProcessEvent((const SDL_Event*)event);
// }
//
// CIMGUI_API bool ImGui_ImplSDLRenderer3_Init(void* renderer)
// {
//     return ImGui_ImplSDLRenderer3_Init((SDL_Renderer*)renderer);
// }
//
// CIMGUI_API void ImGui_ImplSDLRenderer3_Shutdown()
// {
//     ImGui_ImplSDLRenderer3_Shutdown();
// }
//
// CIMGUI_API void ImGui_ImplSDLRenderer3_NewFrame()
// {
//     ImGui_ImplSDLRenderer3_NewFrame();
// }
//
// CIMGUI_API void ImGui_ImplSDLRenderer3_RenderDrawData(ImDrawData* draw_data, void* renderer)
// {
//     ImGui_ImplSDLRenderer3_RenderDrawData(draw_data, (SDL_Renderer*)renderer);
// }
//
// CIMGUI_API void ImGui_ImplSDLRenderer3_CreateDeviceObjects()
// {
//     ImGui_ImplSDLRenderer3_CreateDeviceObjects();
// }
//
// CIMGUI_API void ImGui_ImplSDLRenderer3_DestroyDeviceObjects()
// {
//     ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
// }


