#pragma once

enum class ButtonEvent
{
    None,
    LeftClick,
    RightClick,
    BothPressed
};

enum class FootswitchEvent
{
    None,
    Pressed,    // Used to wake from sleep
    Click,
    Held
};

// Configure menu inputs and reset button tracking
void initialize_buttons();

// Discard menu gestures when returning from another operating mode
void reset_menu_buttons();

// Update button tracking and return any newly recognized event
ButtonEvent poll_buttons();

// Recognize footswitch presses, clicks, and holds
FootswitchEvent poll_footswitch();

// Ignore the remained of the current footswitch gesture
void consume_footswitch_press();

// Wait efficiently while acquisition is stopped
void wait_for_footswitch();