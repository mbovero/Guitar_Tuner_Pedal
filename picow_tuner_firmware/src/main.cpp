#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "pico/stdlib.h"

#include "debug.hpp"
#include "tuner_config.hpp"
#include "frequency/yin_estimator.hpp"
#include "input/adc_input.hpp"
#include "input/buttons.hpp"
#include "tuning/tuning_feedback.hpp"
#include "filters/hum_filter.hpp"
#include "display/tuner_display.hpp"
#include "display/menu_display.hpp"
#include "display/lcd_st7789.hpp"
#include "tuner_settings.hpp"
#include "ui/menu_controller.hpp"
#include "storage/settings_storage.hpp"
#include "assets/idle_gus.hpp"

namespace
{
    enum class PowerMode
    {
        On,
        Idle,
        Off
    };

    // Draw once on entry to Idle
    void show_idle_screen()
    {
        lcd::draw_image_rgb565(idle_image::pixels, idle_image::byte_count);
    }
}

int main()
{
    stdio_init_all();

    // Configure menu inputs and reset gesture tracking
    initialize_buttons();

    // Configure ADC, DMA, and queue for continuous input signal frame acquisition
    initialize_sample_input();

    // Initialize our YIN frequency estimator
    static YinEstimator estimator{SampleInputConfig::sample_rate_hz};
    if (!estimator.supports_frame(SampleInputConfig::frame_sample_count))
    {
        panic("YIN sample rate, frequency range, and frame size are incompatible");
    }

    // Made static to keep array out of normal stack
    static SampleFrame frame{}; // The current frame of ADC samples being processed

    // Configure hum filter modes
    static HumFilter hum_filter;
    if (!hum_filter.configure())
    {
        panic("Failed to configure hum filters");
    }

    // Initialize feedback display
    tuner_display::init();


    // Array for centering ADC samples near 0 V
    static std::array<int16_t, SampleInputConfig::frame_sample_count> centered_samples{};
    // Array to hold filtered ADC samples
    static std::array<int16_t, SampleInputConfig::frame_sample_count> filtered_samples{};

    // Whether the notch filters have been initialized and have usable history
    bool filter_initialized = false;

    // Restore settings before configuring tuning feedback and filter mode
    TunerSettings selected_settings{};
    const bool settings_loaded = settings_storage::load(selected_settings);
    DEBUG_PRINT(
        "%s\n",
        settings_loaded
            ? "Settings loaded from flash"
            : "No valid saved settings, using defaults");

    // Create tuning feedback config
    TuningConfig tuning_config 
    {
        static_cast<float>(selected_settings.a4_frequency_hz), 
        1.0f    // +/- range from target pitch that is considered "In Tune"
    };

    // Most recent tuning feedback
    TuningFeedback latest_feedback{};

    // Menu navigation and working settings
    MenuController menu;
    // Whether the application is currently displaying the menu
    bool menu_open = false;

    PowerMode power_mode = PowerMode::On;
    // Apply each transition in one place while acquisition is stopped
    auto set_power_mode = [&](PowerMode next_mode)
    {
        if (next_mode == power_mode)
        {
            return;
        }

        stop_sample_input();

        // A mode change closes the menu without accepting pending edits
        menu_open = false;
        filter_initialized = false;
        latest_feedback = {};

        lcd::set_backlight(false);
        power_mode = next_mode;

        if (power_mode == PowerMode::Off)
        {
            lcd::sleep();
            return;
        }

        // Wake LCD for On and Idle modes
        lcd::wake();

        if (power_mode == PowerMode::On)
        {
            reset_menu_buttons();

            tuner_display::show();
            tuner_display::update(latest_feedback, false);
        }
        else    // Idle mode
        {
            show_idle_screen();
        }

        // Enable backlight after display is updated for On and Idle modes
        lcd::set_backlight(true);

        // Resume input processing only after display is on for On mode
        if (power_mode == PowerMode::On)
        {
            start_sample_input();
        }
    };

    // Apply the initial filter selection before acquisition begins
    hum_filter.set_mode(selected_settings.filter_mode);

    DEBUG_PRINT(
        "Sample rate: %lu Hz, expected frame: %.1f us\n"
        "Comb delays: 50 Hz=%u samples, 60 Hz=%u samples\n",
        static_cast<unsigned long>(SampleInputConfig::sample_rate_hz),
        SampleInputConfig::expected_frame_time_us,
        static_cast<unsigned>(HumFilterConfig::delay_50_samples),
        static_cast<unsigned>(HumFilterConfig::delay_60_samples));

    // Begin input signal frame acquisition
    start_sample_input();


    // Main while loop
    while (true)
    {
        /* Handle footswitch events and power mode switching before menu or sample processing */
        const FootswitchEvent footswitch_event = poll_footswitch();

        // If footswitch pressed while Off, turn back On
        if (power_mode == PowerMode::Off)
        {
            if (footswitch_event == FootswitchEvent::Pressed)
            {
                // Do not produce an extra click or hold event
                consume_footswitch_press();
                set_power_mode(PowerMode::On);
                continue;
            }
        }
        else if (footswitch_event == FootswitchEvent::Held)   // If footswitch held in On or Idle, turn Off
        {
            consume_footswitch_press();
            set_power_mode(PowerMode::Off);
            continue;
        }
        else if (footswitch_event == FootswitchEvent::Click)    // If footswitch clicked in On or Idle, toggle between modes
        {
            set_power_mode(
                power_mode == PowerMode::On
                    ? PowerMode::Idle
                    : PowerMode::On);
            continue;
        }

        // In Off and Idle modes, put processor to sleep and poll footswitch input
        if (power_mode != PowerMode::On)
        {
            wait_for_footswitch();
            continue;   // Idle and Off skip tuner pipeline
        }


        /* Poll button inputs before processing sample frames */
        const ButtonEvent button_event = poll_buttons();

        // If the menu is open, handle user interactions; stop signal processing, feedback, etc.
        if (menu_open)
        {
            const MenuAction action = menu.handle_event(button_event);

            if (action == MenuAction::Redraw)
            {
                menu_display::update(menu.state());
            }
            else if (action == MenuAction::ReturnToTuner)
            {
                const TunerSettings requested_settings = menu.state().settings;

                // Acquisition is already stopped while the menu is open; save settings to flash memory
                const settings_storage::SaveResult save_result =
                    settings_storage::save(requested_settings);

                if (save_result == settings_storage::SaveResult::Failed)
                {
                    DEBUG_PRINT("Settings save failed\n");

                    // Keep the menu open so the user can retry
                    lcd::fill_rect(12, 198, 216, 32, 0x0000);
                    lcd::draw_string(12, 200, "SAVE FAILED", 0xF800, 0x0000, 2);
                    lcd::draw_string(12, 220, "BOTH ON RETURN TO RETRY", 0xFFFF, 0x0000, 1);

                    continue;
                }

                DEBUG_PRINT(
                    "%s\n",
                    save_result == settings_storage::SaveResult::Saved
                        ? "Settings saved"
                        : "Settings unchanged, no flash write");

                // Accept settings only after successful save
                selected_settings = requested_settings;

                // Apply the selected reference frequency to note mapping and cents calculation
                tuning_config.a4_frequency_hz = static_cast<float>(selected_settings.a4_frequency_hz);

                // Select the filter while acquisition is stopped
                hum_filter.set_mode(selected_settings.filter_mode);

                // Require fresh filter history and feedback after the pause
                filter_initialized = false;
                latest_feedback = {};

                // Restore tuner feedback layout and invalidate previous display cache
                tuner_display::show();
                // Show idle feedback while waiting for a fresh estimate
                tuner_display::update(latest_feedback, false);

                // Finish drawing before restarting acquisition
                menu_open = false;
                start_sample_input();

                DEBUG_PRINT("Returned to tuner\n");
            }

            // While the menu is open, only run input and menu handling
            // After exiting, begin sample processing on the next iteration
            continue;
        }

        if (button_event == ButtonEvent::BothPressed)
        {
            // Stop acquisition before drawing menu
            stop_sample_input();

            // Clear feedback that no longer represents current measurement
            filter_initialized = false;
            latest_feedback = {};

            // Begin browsing with a copy of the accepted settings
            menu.open(selected_settings);
            menu_open = true;

            // Replace the tuner screen with the complete menu
            menu_display::show(menu.state());

            // Skip the tuner pipeline on the menu entry iteration
            continue;
        }

        /* Process sample frames and display feedback */
        // Attempt to retrieve a sample frame
        if(!try_get_sample_frame(frame))
        {
            // Skip processing if no frame was available
            continue;
        }

        // Initialize default estimate
        YinEstimate estimate{};
        // Initialize default filtered frame stats
        FilteredFrameStats filtered_stats{};

        const bool use_filter = hum_filter.enabled();

        // Initialize total filtering time
        uint32_t filter_time_us = 0;
        // Initialize total processing time
        uint32_t processing_time_us = 0;

        // If ADC sample frame is invalid, require filter to be reinitialized (continuous stream is required)
        if (!frame.capture_valid)
        {
            filter_initialized = false;
        }
        else    // Otherwise frame is valid, so process it
        {
            // Center all samples
            for (size_t i = 0; i < frame.samples.size(); ++i)
            {
                // Remove approximate DC offset from each sample for intuitive calculations
                const int32_t centered = static_cast<int32_t>(frame.samples[i]) - midpoint_adc;
                // Update centered samples array
                centered_samples[i] = static_cast<int16_t>(centered);
            }

            // Only filter if enabled
            if (use_filter)
            {
                // If filter has not been initialized, or if a discontinuity occurred, reinitialize it
                if (!filter_initialized || frame.discontinuous)
                {
                    // Reset the notch filter using an estimated baseline as the initial value
                    const double initial_value = static_cast<double>(frame.mean_adc) - static_cast<double>(midpoint_adc);
                    hum_filter.reset(initial_value);

                    // Mark filter as initialized
                    filter_initialized = true;
                }

                // Record current timestamp to measure time spent filtering
                const uint32_t filter_start_us = time_us_32();
                // Filter this frame and record filtered frame statistics
                filtered_stats = hum_filter.filter_frame(
                    centered_samples.data(),
                    filtered_samples.data(),
                    centered_samples.size());
                // Calculate total filtering time
                filter_time_us = time_us_32() - filter_start_us;

                // If filtered results are invalid, require filter to be reset
                if (!filtered_stats.valid)
                {
                    filter_initialized = false;
                }
            }
            else
            {
                // Do not maintain comb filter history while filter is bypassed
                filter_initialized = false;
            }
        }

        // Feed estimator different samples based on whether notch filter was enabled
        const int16_t* estimator_samples =
            use_filter
                ? filtered_samples.data()
                : centered_samples.data();
        // Calculate pk2pk based on whether notch filter was enabled
        const uint32_t estimator_peak_to_peak = 
            use_filter
                ? filtered_stats.peak_to_peak_adc
                : frame.peak_to_peak_adc;

        // Determine if input to the estimator is valid
        const bool estimator_input_valid =
            frame.capture_valid &&      // Original sample frame must be valid
            (!use_filter || filtered_stats.valid);   // And must either not be using filter, or filtered results are valid

        // Determine if an input signal is present
        const bool signal_present = estimator_peak_to_peak >= minimum_signal_peak_to_peak;

        // Check that the input to the estimator is valid and a useful input was captured
        if (estimator_input_valid && signal_present)
        {
            // Track YIN computation time
            const uint32_t processing_start_us = time_us_32();
            // Perform estimation of signal frequency
            estimate = estimator.estimate(
                estimator_samples,
                SampleInputConfig::frame_sample_count);
            // Calculate elapsed calculation time
            processing_time_us = time_us_32() - processing_start_us;
        }
        
        // Default to no valid feedback for this frame
        latest_feedback = {};
        // Only get feedback for valid estimates
        if (estimator_input_valid &&
            signal_present &&
            estimate.valid)
        {
            // Get tuning feedback based on estimated frequency
            latest_feedback = analyze_frequency(estimate.freq_hz, tuning_config);
        }
        
        // Determine if input signal was clipping
        const bool clipping = (frame.near_rail_count >= 20);

        // Display feedback on LCD and track time taken to do so
        const uint32_t display_start_us = time_us_32();
        tuner_display::update(latest_feedback, clipping);
        const uint32_t display_time_us = time_us_32() - display_start_us;

        // Print results/statistics
        DEBUG_PRINT("display time=%lu \n", static_cast<unsigned long>(display_time_us));
        DEBUG_PRINT(
            "\nframe=%lu, gap=%d, dropped=%lu\n"
            "ADC: min=%u max=%u mean=%.1f \n"
            "capture=%lu errors=%lu overflow=%d rails=%lu \n",
            static_cast<unsigned long>(frame.sequence_number),
            static_cast<int>(frame.discontinuous),
            static_cast<unsigned long>(dropped_sample_frames()),
            static_cast<unsigned>(frame.minimum_adc),
            static_cast<unsigned>(frame.maximum_adc),
            frame.mean_adc,
            static_cast<unsigned long>(frame.capture_time_us),
            static_cast<unsigned long>(frame.conversion_error_count),
            static_cast<int>(frame.fifo_overflowed),
            static_cast<unsigned long>(frame.near_rail_count));
        DEBUG_PRINT(
            "raw RMS=%.2f, filtered RMS=%.2f \n",
            frame.rms_adc,
            filtered_stats.rms_adc);
        DEBUG_PRINT(
            "raw pk2pk=%u, filtered pk2pk=%lu \n"
            "filter: %lu us, YIN: %lu us \n",
            static_cast<unsigned>(frame.peak_to_peak_adc),
            static_cast<unsigned long>(filtered_stats.peak_to_peak_adc),
            static_cast<unsigned long>(filter_time_us),
            static_cast<unsigned long>(processing_time_us));

        if (!frame.capture_valid)
        {
            DEBUG_PRINT("Capture invalid\n");
        }
        else if (use_filter && !filtered_stats.valid)
        {
            DEBUG_PRINT("Filter output invalid\n");
        }
        else if (!signal_present)
        {
            DEBUG_PRINT("Input quiet\n");
        }
        else if (!estimate.valid)
        {
            DEBUG_PRINT("No accepted pitch\n");
        }
        else
        {
            DEBUG_PRINT(
                "Frequency: %.3f Hz, Period: %.4f samples, "
                "Difference: %.6f \n",
                estimate.freq_hz,
                estimate.period_samples,
                estimate.normalized_difference);

            // Print valid tuning feedback
            if (latest_feedback.valid)
            {
                DEBUG_PRINT("Note: %s%d \n", latest_feedback.note_name, latest_feedback.octave);
                DEBUG_PRINT("Measured: %.2f Hz, Target: %.2f Hz \n", 
                    latest_feedback.measured_freq_hz, latest_feedback.target_freq_hz);
                DEBUG_PRINT("Error: %+.1f cents \n", latest_feedback.cents_error);
                DEBUG_PRINT("Status: %s \n", tuning_status_text(latest_feedback.status));
            }
            else
            {
                DEBUG_PRINT("No valid note estimation \n");
            }

        }
    }
}