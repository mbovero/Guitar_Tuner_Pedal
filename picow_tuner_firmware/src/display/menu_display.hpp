#pragma once

#include "ui/menu_controller.hpp"

namespace menu_display
{
    // Replace the current screen with the complete menu
    void show(const MenuState& state);

    // Redraw menu regions affected by state changes
    void update(const MenuState& state);
}