#include "buttons.hpp"
#include "tuner_config.hpp"

#include <cstdint>

#include "pico/stdlib.h"
#include "hardware/sync.h"

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

    DebouncedButton footswitch{FootswitchConfig::gpio};

    uint32_t footswitch_pressed_ms = 0;
    bool footswitch_hold_reported = false;

    // Suppresses a gesture after waking or entering Off state
    bool footswitch_consumed = false;

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
        button.raw_changed_ms = to_ms_since_boot(get_absolute_time());
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

    // Wake the main loop without modifying debounce or application state
    void footswitch_irq_handler(uint gpio, uint32_t)
    {
        if (gpio == FootswitchConfig::gpio)
        {
            __sev();    // Wake up processor
        }
    }
}

void reset_menu_buttons()
{
    initialize_button(left_button);
    initialize_button(right_button);

    // Require released menu inputs before accepting another gesture
    waiting_for_release = true;
}

void initialize_buttons()
{
    reset_menu_buttons();
    initialize_button(footswitch);

    footswitch_pressed_ms = 0;
    footswitch_hold_reported = false;

    // Ignore a switch held during startup until it has been released
    footswitch_consumed = true;

    // Falling edge wakes to begin debounce, rising edge wakes to finish gesture
    gpio_set_irq_enabled_with_callback(
        FootswitchConfig::gpio,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
        true,
        footswitch_irq_handler);
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
        if(!left_pressed && !right_pressed &&
           !left_button.raw_pressed && !right_button.raw_pressed)
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

/*
 * Recognized a debounced press, short click, or single hold event
 */
FootswitchEvent poll_footswitch()
{
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    const bool was_pressed = footswitch.stable_pressed;

    update_button(footswitch, now_ms);

    const bool pressed = footswitch.stable_pressed;

    if (footswitch_consumed)
    {
        // Keep suppressing this gesture until release has passed debounce
        if (!pressed && !footswitch.raw_pressed)
        {
            footswitch_consumed = false;
        }

        return FootswitchEvent::None;
    }

    if (!was_pressed && pressed)
    {
        footswitch_pressed_ms = now_ms;
        footswitch_hold_reported = false;

        return FootswitchEvent::Pressed;
    }

    if (pressed &&
        footswitch.raw_pressed &&
        !footswitch_hold_reported &&
        now_ms - footswitch_pressed_ms >= FootswitchConfig::hold_ms)
    {
        footswitch_hold_reported = true;
        return FootswitchEvent::Held;
    }

    if (was_pressed && !pressed && !footswitch_hold_reported)
    {
        return FootswitchEvent::Click;
    }

    return FootswitchEvent::None;
}

/* 
 * Suppresses both the hold and click belonging to the current footswitch press
 */
void consume_footswitch_press()
{
    footswitch_consumed = true;
}

/* 
 * Waits efficiently while acquisition is stopped
 */
void wait_for_footswitch()
{
    const bool physical_pressed = !gpio_get(FootswitchConfig::gpio);

    const bool needs_polling =
        footswitch.stable_pressed ||
        footswitch.raw_changed_ms != footswitch.stable_pressed ||
        physical_pressed != footswitch.raw_pressed;

    // When footswitch signal transitions, continue recognizing the gesture
    if (needs_polling)
    {
        // Continue checking while a press, release, or hold is in progress
        sleep_ms(FootswitchConfig::poll_interval_ms);
    }
    else    // While everythin is released and settled, wait for an event
    {
        // Put core to sleep
        __wfe();
    }
}