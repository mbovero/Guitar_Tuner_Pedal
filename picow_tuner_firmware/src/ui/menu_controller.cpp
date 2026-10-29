#include "menu_controller.hpp"

/*
 * Starts browsing from the first row with a working copy of the settings
 */
void MenuController::open(const TunerSettings& settings)
{
    // Restore the default selection and browsing mode
    state_ = {};

    // Begin editing from the currently accepted settings
    state_.settings = settings;
}

/* 
 * Interprets button events according to the selected row and editing mode
 */
MenuAction MenuController::handle_event(ButtonEvent event)
{
    if (event == ButtonEvent::None)
    {
        return MenuAction::None;
    }

    // Pressing both buttons selects a row or finished editing current setting
    if (event == ButtonEvent::BothPressed)
    {
        // If currently editing a setting, finish and redraw
        if (state_.editing)
        {
            state_.editing = false;
            return MenuAction::Redraw;
        }

        // If return row is selected, return to the tuner feedback screen
        if (state_.selected_item == MenuItem::ReturnToTuner)
        {
            return MenuAction::ReturnToTuner;
        }

        // Otherwise, begin editing the selected setting
        state_.editing = true;
        return MenuAction::Redraw;
    }

    // Left moves backward or decreases a value
    // Right moves forward or increases a value
    const int direction = event == ButtonEvent::LeftClick ? -1 : 1;

    // If a setting isn't being edited, then update selected row
    if (!state_.editing)
    {
        // MenuItem values correspond to the three rows in display order
        constexpr int item_count = 3;

        const int current_item = static_cast<int>(state_.selected_item);

        // Adding item_count keeps the value nonnegative before modulo
        const int next_item = (current_item + direction + item_count) % item_count;

        state_.selected_item = static_cast<MenuItem>(next_item);

        return MenuAction::Redraw;
    }

    // If currently editing the filter setting, update selected option
    if (state_.selected_item == MenuItem::Filter)
    {
        // FilterMode values follow the order Off, 50 Hz, 60 Hz
        constexpr int mode_count = 3;

        const int current_mode = static_cast<int>(state_.settings.filter_mode);

        const int next_mode = (current_mode + direction + mode_count) % mode_count;

        state_.settings.filter_mode = static_cast<FilterMode>(next_mode);

        return MenuAction::Redraw;
    }

    // If currently editing reference pitch, update selected value
    if (state_.selected_item == MenuItem::ReferencePitch)
    {
        // Perform arithmetic as a signed integer before checking limits
        const int next_frequency = static_cast<int>(state_.settings.a4_frequency_hz) + direction;

        // Keep the reference frequency within its allowed range
        if (next_frequency < ReferencePitchLimits::min_hz ||
            next_frequency > ReferencePitchLimits::max_hz)
        {
            return MenuAction::None;
        }

        state_.settings.a4_frequency_hz = static_cast<uint16_t>(next_frequency);

        return MenuAction::Redraw;
    }

    return MenuAction::None;
}

/* 
 * Exposes the current state without allowing external modification
 */
const MenuState& MenuController::state() const
{
    return state_;
}