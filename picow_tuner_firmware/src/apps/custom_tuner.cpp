#include <stdio.h>
#include "pico/stdlib.h"
#include "tuning/tuning_feedback.hpp"
#include "pitch/custom_estimator.hpp"
#include "input/custom_pulse_input.hpp"
#include "debug.hpp"

#include "hardware/adc.h"

/* 
 * This is an attempt at implementing a custom frequency estimation algorithm. This tuner firmware 
 * runs an ADC interrupt handler that provides a stream of waveform pulse and midpoint crossing
 * data. The custom estimator then compares sequential waveform features to identify potential
 * signal periods. Once a stable period has been identified, the estimator gradually improves
 * its frequency estimation and outputs it. Finally, the estimated frequency is converted into
 * tuning feedback and displayed to the user.
 */
int main()
{
    stdio_init_all();

    // Initialize custom frequency estimator instance
    CustomEstimator estimator{sample_rate_hz};

    // Set up the ADC to intake the guitar signal and queue midpoint crossing events
    initialize_pulse_input();

    // Current input handler state
    InputState input_state = InputState::Idle;

    // Variables for regularly printing status updates
    uint32_t last_print_us = time_us_32();
    constexpr uint32_t print_interval_us = 500'000; // 2 Hz
    bool freq_updated = false;

    // Latest statistics from the ADC ISR
    ADCBlockStats latest_adc_stats{};
    // Flag to initialize latest ADC statistics struct
    bool have_adc_stats = false;

    // Create tuning feedback config
    const TuningConfig note_config {
        440.0f, // Modern standard pitch (A4 = 440 Hz)
        3.0f    // +/- range from target pitch that is considered "In Tune"
    };
    // Most recent tuning feedback
    TuningResult latest_note{};

    /* Main while loop */
    while (true) 
    {
        // Continuously drain the event queue and process events
        InputEvent event;
        while (try_get_input_event(event))
        {
            // Handle state change events
            if (event.type == InputEventType::StateChanged)
            {
                // Store current state
                input_state = event.state;
                // Reset the frequency estimator
                estimator.reset();

                // Don't print reset frequency
                freq_updated = false;
                // Reset tuning feedback
                latest_note = {};

                // Process next event (no crossing event to handle)
                continue;
            }

            // If the input is not active, don't process crossing events
            if (input_state != InputState::Active)
            {
                continue;
            }

            // Otherwise, handle event as a crossing event
            if (estimator.process(event.crossing, time_us_32()))
            {
                // If processing the crossing produced a frequency estimation, print it
                freq_updated = true;
            }
        }


        // Continuously drain the ADC block stats queue and store stats
        ADCBlockStats stats;
        while (try_get_adc_block_stats(stats))
        {
            latest_adc_stats = stats;
            have_adc_stats = true;
        }


        // Get current time for estimator resets and debug printing
        const uint32_t now_us = time_us_32();

        // Update estimator using current time
        if (estimator.update(now_us))
        {
            // If a reset occured, don't print frequency estimate
            freq_updated = false;
            // And reset tuning feedback
            latest_note = {};
        }


        // Regularly print debug updates
        if ((now_us - last_print_us) >= print_interval_us)
        {
            DEBUG_PRINT("\n");
            
            // Always print input state
            if (input_state == InputState::Active)
            {
                DEBUG_PRINT("State: Active\n");
            }else
            {
                DEBUG_PRINT("State: Idle\n");
            }

            // Only print frequency estimator and tuning feedback when input is active
            if (input_state == InputState::Active)
            {
                // Print data during acquisition
                if (estimator.state() == CustomEstimatorState::Acquiring)
                {
                    DEBUG_PRINT("Acquiring: %u/%u consistent intervals\n", 
                        estimator.interval_count(), 
                        estimator.required_intervals());
                }
                else if (freq_updated)
                {
                    // Retrieve latest frequency estimate
                    const PitchResult pitch = estimator.result();

                    // Get tuning feedback based on latest frequency
                    latest_note = analyze_frequency(pitch.freq_hz, note_config);

                    // Print valid tuning feedback
                    if (latest_note.valid)
                    {
                        DEBUG_PRINT("Note: %s%d \n", latest_note.note_name, latest_note.octave);
                        DEBUG_PRINT("Measured: %.2f Hz, Target: %.2f Hz \n", 
                            latest_note.measured_freq_hz, latest_note.target_freq_hz);
                        DEBUG_PRINT("Error: %+.1f cents \n", latest_note.cents_error);
                        DEBUG_PRINT("Status: %s \n", tuning_status_text(latest_note.status));
                    }
                    else
                    {
                        DEBUG_PRINT("No valid note estimation \n");
                    }

                    // Detailed period estimation statistics
                    // const WaveformEvent pulse = estimator.latest_pulse();
                    // DEBUG_PRINT("Estimated Frequency: %.3f Hz \n", pitch.freq_hz);
                    // DEBUG_PRINT("Mean Period: %.3f samples \n", estimator.mean_period());
                    // DEBUG_PRINT("Reference Slope: %.2f, Latest Slope: %d \n", 
                    //     estimator.reference_slope(), 
                    //     pulse.slope);
                    // DEBUG_PRINT("Reference Height: %.2f, Latest Height: %u \n", 
                    //     estimator.reference_height(), 
                    //     pulse.pulse_height);
                    // DEBUG_PRINT("Reference Width: %.3f ms, Latest Width: %.3f ms \n",
                    //     1000.0f * estimator.reference_width() / sample_rate_hz,
                    //     1000.0f * pulse.pulse_width / sample_rate_hz);

                    freq_updated = false;
                }
            }

            // Always print ADC stats if they are available
            if (have_adc_stats)
            {
                // DEBUG_PRINT("ADC: P2P=%u, mean=%.2f, min=%d, max=%d \n",
                // latest_adc_stats.p2p,
                // latest_adc_stats.mean,
                // latest_adc_stats.min,
                // latest_adc_stats.max);

                // // Check if ADC FIFO has overflowed
                // const bool fifo_overflowed =
                //     (adc_hw->fcs & ADC_FCS_OVER_BITS) != 0;

                // if (fifo_overflowed)
                // {
                //     // Write 1 to clear overflow, preserving FIFO configuration.
                //     adc_hw->fcs |= ADC_FCS_OVER_BITS;
                // }

                // DEBUG_PRINT("FIFO overflow since last check: %d\n",
                //             static_cast<int>(fifo_overflowed));

                // DEBUG_PRINT("Midpoint: %d \n", latest_adc_stats.midpoint);
                // DEBUG_PRINT("ADC Errors: %d \n", latest_adc_stats.adc_errors);
                // DEBUG_PRINT("WaveForm Errors: %d \n", latest_adc_stats.waveform_queue_errors);
                // DEBUG_PRINT("StateChange Errors: %d \n", latest_adc_stats.state_change_queue_errors);
                // DEBUG_PRINT("Stats Errors: %d \n", latest_adc_stats.stats_queue_errors);
            }

            last_print_us = now_us;
        }

    }
}
