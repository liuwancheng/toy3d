#pragma once

#include "imgui.h"

namespace toy3d
{
    // Each row preserves its value item's ImGui activation state for the caller's history.
    bool begin_property_row(const char* label, float trailing_width = 0.0f);
    void end_property_row();
    bool property_bool(const char* label, bool* value);
    bool property_float(const char* label, float* value, float speed = 0.01f, float minimum = 0.0f,
                        float maximum = 0.0f, const char* format = "%.3f");
    bool property_float_n(const char* label, float* value, int count, float speed = 0.01f);
    bool property_int(const char* label, int* value, float speed = 1.0f);
    bool property_double(const char* label, double* value, double step = 0.1, double fast_step = 1.0,
                         const char* format = "%.2f");
    bool property_slider_float(const char* label, float* value, float minimum, float maximum,
                               const char* format = "%.3f");
    bool property_slider_int(const char* label, int* value, int minimum, int maximum);
    bool property_combo(const char* label, int* value, const char* const items[], int count);
    // Raw values remain linear. Only the bar converts RGB for display; alpha is coverage.
    bool property_color_value(const char* id, float* value, bool alpha, float width = 0.0f);
    bool property_color(const char* label, float* value, bool alpha = false);
    enum class PropertyAction
    {
        Use,
        Find,
        Clear,
        Reset
    };
    bool property_action_button(const char* id, PropertyAction action, const char* tooltip);
} // namespace toy3d
