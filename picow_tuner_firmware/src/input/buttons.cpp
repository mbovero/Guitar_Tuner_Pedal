#include "buttons.hpp"
#include "tuner_config.hpp"

#include <cstdint>

#include "pico/stdlib.h"

namespace
{
    struct DebouncedButton
    {
        unsigned gpio;

        // Most recently observed electrical state
        bool raw_pressed = false;

        // State accepted after the debounce interval
        bool stable_pressed = false;

        // Timestamp of the most recent observed electrical change
        uint32_t raw_changed_ms = 0;
    };

    DebouncedButton left_button{ButtonInputConfig::left_gpio};
    DebouncedButton right_button{ButtonInputConfig::right_gpio};

    // Suppress actions after a both-button gesture until both are released
    bool waiting_for_release = false;

    /*
     * Configures one active-low input and resets its debounce state
     */
    void initialize_button(DebouncedButton& button)
    {
        gpio_init(button.gpio);
        gpio_set_dir(button.gpio, GPIO_IN);
        gpio_pull_up(button.gpio);

        // A low electrical level represents a press
        button.raw_pressed = !gpio_get(button.gpio);

        // Require a held input to pass debounce before accepting it
        button.stable_pressed = false;
        button.raw_changed_ms = to_ms_64_since_boot(get_absolute_time());
    }

    /* 
     * Accepts a new state only after it remains unchanged long enough
     */
    void update_button(DebouncedButton& button, uint32_t now_ms)
    {
        const bool pressed = !gpio_get(button.gpio);

        // Restart the debounce interval whenever the raw input changes
        if (pressed != button.raw_pressed)
        {
            button.raw_pressed = pressed;
            button.raw_changed_ms = now_ms;
        }

        // Update the accepted state once the raw input has settled
        if (button.stable_pressed != button.raw_pressed &&
            now_ms - button.raw_changed_ms >= ButtonInputConfig::debounce_ms)
        {
            button.stable_pressed = button.raw_pressed;
        }
    }
}

/* 
 * Initialized both button inputs before polling begins
 */
void initialize_buttons()
{
    initialize_button(left_button);
    initialize_button(right_button);

    waiting_for_release = false;
}

/*
 * Updates debounce state and recognizes individual or combined actions
 */
ButtonEvent poll_buttons()
{
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    // Preserve the previous accepted states to detect edges
    const bool left_was_pressed = left_button.stable_pressed;
    const bool right_was_pressed = right_button.stable_pressed;

    update_button(left_button, now_ms);
    update_button(right_button, now_ms);

    const bool left_pressed = left_button.stable_pressed;
    const bool right_pressed = right_button.stable_pressed;

    // A combined gesture consumes all activity until both inputs are released
    if (waiting_for_release)
    {
        if(!left_pressed && !right_pressed)
        {
            waiting_for_release = false;
        }

        return ButtonEvent::None;
    }

    // Recognize overlapping presses before considering individual clicks
    if (left_pressed && right_pressed)
    {
        waiting_for_release = true;
        return ButtonEvent::BothPressed;
    }

    // Individual clicks occur when an accepted press is released
    if (left_was_pressed && !left_pressed)
    {
        return ButtonEvent::LeftClick;
    }

    if (right_was_pressed && !right_pressed)
    {
        return ButtonEvent::RightClick;
    }

    return ButtonEvent::None;
}