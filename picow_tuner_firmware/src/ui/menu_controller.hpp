#pragma once

#include "input/buttons.hpp"
#include "tuner_settings.hpp"

enum class MenuItem
{
    Filter,
    ReferencePitch,
    ReturnToTuner
};

enum class MenuAction
{
    None,           // Nothing visible changed
    Redraw,         // Selection, editing mode, or value changed: redraw the menu
    ReturnToTuner   // Return row was selected
};

struct MenuState
{
    // Working settings being edited
    TunerSettings settings{};

    // Currently selected menu row
    MenuItem selected_item = MenuItem::Filter;

    // Whether buttons adjust a value instead of moving between rows
    bool editing = false;
};

class MenuController
{
public:
    // Begin a menu session using the current settings
    void open(const TunerSettings& settings);

    // Interpret one button event and report the resulting action
    MenuAction handle_event(ButtonEvent event);

    // Provide read-only access for rendering or accepting settings
    const MenuState& state() const;

private:
    MenuState state_{};
};