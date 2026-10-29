#include "menu_display.hpp"
#include "lcd_st7789.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr uint16_t background_color = 0x0000;
    constexpr uint16_t text_color = 0xFFFF;
    constexpr uint16_t hint_color = 0xC618;

    constexpr uint16_t selected_color = 0x3186;
    constexpr uint16_t editing_color = 0x001F;
    constexpr uint16_t editing_text_color = 0xFFE0;

    constexpr uint8_t text_scale = 2;

    constexpr uint16_t row_x = 12;
    constexpr uint16_t row_width = lcd::width - 2 * row_x;
    constexpr uint16_t first_row_y = 52;
    constexpr uint16_t row_height = 40;
    constexpr uint16_t row_pitch = 48;
    constexpr uint16_t row_padding = 8;

    constexpr uint16_t hints_y = 200;

    constexpr MenuItem menu_items[] = {
        MenuItem::Filter,
        MenuItem::ReferencePitch,
        MenuItem::ReturnToTuner
    };

    // Cache the state represented by the displayed menu
    MenuState previous_state{};
    bool menu_drawn = false;

    const char* filter_mode_text(FilterMode mode)
    {
        switch (mode)
        {
            case FilterMode::Off:
                return "OFF";

            case FilterMode::Hz50:
                return "50 Hz";

            case FilterMode::Hz60:
                return "60 Hz";
        }

        return "?";
    }

    /* 
     * Draws one complete row using its current selection and editing state
     */
    void draw_row(MenuItem item, const MenuState& state)
    {
        // Enum values correspond to rows zero through two
        const uint16_t y = static_cast<uint16_t>(
            first_row_y + static_cast<unsigned>(item) * row_pitch);

        const bool selected = state.selected_item == item;

        const uint16_t row_background = 
            selected
                ? (state.editing ? editing_color : selected_color)
                : background_color;

        const uint16_t value_color =
            selected && state.editing
                ? editing_text_color
                : text_color;

        const char* label = "";
        const char* value = "";

        // Storage remains alive until the value has been drawn
        char reference_text[12] = {};

        switch (item)
        {
            case MenuItem::Filter:
                label = "FILTER";
                value = filter_mode_text(state.settings.filter_mode);
                break;

            case MenuItem::ReferencePitch:
                label = "A4 REF";
                std::snprintf(
                    reference_text,
                    sizeof(reference_text),
                    "%u Hz",
                    static_cast<unsigned>(state.settings.a4_frequency_hz));
                value = reference_text;
                break;

            case MenuItem::ReturnToTuner:
                label = "RETURN";
                break;
        }

        // Clear row of old characters
        lcd::fill_rect(row_x, y, row_width, row_height, row_background);

        // Each character cell is eight scaled pixels tall
        const uint16_t text_y = y + (row_height - 8 * text_scale) / 2;

        lcd::draw_string(
            row_x + row_padding,
            text_y,
            label,
            text_color,
            row_background,
            text_scale);

        // Draw the row value if it has one
        if (value[0] != '\0')
        {
            // Each character advances six scaled pixels horizontally
            const uint16_t value_width = static_cast<uint16_t>(
                std::strlen(value) * 6 * text_scale);
            
            // Place the final character cell against the right padding
            const uint16_t value_x = row_x + row_width - row_padding - value_width;

            lcd::draw_string(
                value_x,
                text_y,
                value,
                value_color,
                row_background,
                text_scale);
        }
    }

    /*
     * Shows the controls appropriate to browsing or editing
     */
    void draw_hints(bool editing)
    {
        lcd::fill_rect(
            row_x, hints_y, row_width, 28, background_color);

        lcd::draw_string(
            row_x,
            hints_y,
            editing ? "LEFT RIGHT CHANGE" : "LEFT RIGHT MOVE",
            hint_color,
            background_color,
            1);

        lcd::draw_string(
            row_x,
            hints_y + 14,
            editing ? "BOTH ACCEPT" : "BOTH SELECT",
            hint_color,
            background_color,
            1);
    }
}

/*
 * Draws the complete menu whenever it becomes the active screen
 */
void menu_display::show(const MenuState& state)
{
    lcd::fill(background_color);

    lcd::draw_string(row_x, 12, "SETTINGS", text_color, background_color, 3);

    for (const MenuItem item: menu_items)
    {
        draw_row(item, state);
    }

    draw_hints(state.editing);

    previous_state = state;
    menu_drawn = true;
}

/* 
 * Redraws only rows and hints whose visible state changed
 */
void menu_display::update(const MenuState& state)
{
    if (!menu_drawn)
    {
        show(state);
        return;
    }

    for (const MenuItem item : menu_items)
    {
        const bool was_selected = previous_state.selected_item == item;

        const bool is_selected = state.selected_item == item;

        const bool selection_changed = was_selected != is_selected;

        // Editing changes the appearance of the selected row
        const bool editing_changed =
            previous_state.editing != state.editing &&
            (was_selected || is_selected);

        const bool value_changed =
            (item == MenuItem::Filter &&
             previous_state.settings.filter_mode !=
                state.settings.filter_mode) ||
            (item == MenuItem::ReferencePitch &&
             previous_state.settings.a4_frequency_hz !=
                state.settings.a4_frequency_hz);
        
        if (selection_changed || editing_changed || value_changed)
        {
            draw_row(item, state);
        }
    }

    if (previous_state.editing != state.editing)
    {
        draw_hints(state.editing);
    }

    // Cache the state now represented by the displayed menu
    previous_state = state;
}