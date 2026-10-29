#pragma once

enum class ButtonEvent
{
    None,
    LeftClick,
    RightClick,
    BothPressed
};

// Configure menu inputs and reset button tracking
void initialize_buttons();

// Update button tracking and return any newly recognized event
ButtonEvent poll_buttons();