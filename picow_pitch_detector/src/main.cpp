#include <stdio.h>
#include "pico/stdlib.h"
#include "guitar_input.hpp"
#include "crossing_history.hpp"
#include "debug.hpp"
#include <cstdlib>

int main()
{
    stdio_init_all();

    // Set up the ADC to intake the guitar signal and queue midpoint crossing events
    initialize_guitar_input();

    
    // Set up variables for resetting reference slope after certain time without matches
    uint32_t last_match_us = time_us_32();
    constexpr uint32_t ref_slope_reset_us = 40'000;
    // Reference slope used to identify beginning/end of waveform period
    int32_t ref_slope = 0;
    // Whether a valid reference slope has been set; helps initialize/reset ref_slope
    bool have_reference = false;
    // Variables used to monitor changing reference slope
    int32_t prev_ref_slope = ref_slope;
    bool ref_slope_updated = false;
    // The latest event with a slope matching the reference slope
    CrossingEvent prev_match{};
    // How much a compared slope can vary from reference slope and still be considered matching
    constexpr uint16_t ref_slope_tolerance = 3;

    // Set up variables for regularly printing status updates
    uint32_t last_print_us = time_us_32();
    constexpr uint32_t print_interval_us = 500'000; // 2 Hz
    float latest_freq_hz = 0.0f;
    bool freq_updated = false;
    CrossingEvent debug_crossing{0,0};

    // Latest statistics from the ADC ISR
    ADCBlockStats latest_adc_stats{};
    // Flag to initialize latest ADC statistics struct
    bool have_adc_stats = false;

    // Current input handler state
    InputState input_state = InputState::Idle;

    while (true) 
    {
        // Continuously drain the midpoint crossing event queue and process events
        InputEvent event;
        while (try_get_input_event(event))
        {
            // Handle state change events
            if (event.type == InputEventType::StateChanged)
            {
                // Store current state
                input_state = event.state;

                // Reset reference slope
                have_reference = false;
                ref_slope_updated = false;
                // Reset estimated frequency
                latest_freq_hz = 0.0f;
                freq_updated = false;

                // Process next event
                continue;
            }

            // If the input is not active, don't process other events
            if (input_state != InputState::Active)
            {
                continue;
            }

            // Otherwise, handle event as a crossing event
            const CrossingEvent& crossing = event.crossing;
            // Copy of the crossing event for debug prints
            debug_crossing = crossing;

            // Set a reference slope if one doesn't exist
            if (!have_reference)
            {
                prev_ref_slope = ref_slope;
                ref_slope = crossing.slope;
                ref_slope_updated = true;
                last_match_us = time_us_32();
                prev_match = crossing;
                have_reference = true;
                continue;
            }

            // If this event's slope matches the reference slope, print the estimated frequency
            if (std::abs(ref_slope - crossing.slope) <= ref_slope_tolerance)
            {
                // Interpolate crossing data to get more accurate timestamps
                // Calculate how far back true midpoint crossing is from previous crossing's positive ADC value
                const float prev_backtrack = static_cast<float>(prev_match.sample_after) / prev_match.slope;
                // Calculate how far back true midpoint crossing is from current crossing's positive ADC value
                const float current_backtrack = static_cast<float>(crossing.sample_after) / crossing.slope;
                // Calculate total elasped whole integer samples
                const uint32_t whole_samples = crossing.sample_count - prev_match.sample_count;

                // Backtrack from each crossing's positive ADC value to calculate more precise elapsed samples
                const float elapsed_samples = static_cast<float>(whole_samples) + prev_backtrack - current_backtrack;

                // Skip impossible periods
                if (elapsed_samples <= 0.0f)
                {
                    continue;
                }

                // Convert average period in ADC cycles to frequency
                latest_freq_hz = sample_rate_hz / elapsed_samples;
                freq_updated = true;

                // Update last matched event to this one
                prev_match = crossing;
                // Update last reference slope match timestamp
                last_match_us = time_us_32();

                // TODO: dynamically updating reference slope?
            }

        }

        // Continuously drain the ADC block stats queue and store stats
        ADCBlockStats stats;
        while (try_get_adc_block_stats(stats))
        {
            latest_adc_stats = stats;
            have_adc_stats = true;
        }


        // Get current time for upkeep tasks
        const uint32_t now_us = time_us_32();

        // Reset the reference slope if it's initialized and no matches occured over a certain timeframe
        if (have_reference &&
            now_us - last_match_us > ref_slope_reset_us)
        {
            // Invalidate current reference so it is reset when the next event is processed
            have_reference = false;
            // Prevent current frequency estimate from being outputted
            freq_updated = false;
        }


        // Regularly print status updates
        if ((now_us - last_print_us) >= print_interval_us)
        {
            DEBUG_PRINT("\n");

            if (input_state == InputState::Active)
            {
                DEBUG_PRINT("State: Active\n");
            }else
            {
                DEBUG_PRINT("State: Idle\n");
            }

            if (ref_slope_updated &&
                (prev_ref_slope != ref_slope))
            {
                DEBUG_PRINT("Reset reference slope from %d\n", prev_ref_slope);
                ref_slope_updated = false;
            }

            if (input_state == InputState::Active &&
                freq_updated)
            {
                DEBUG_PRINT("Estimated frequency: %.5f Hz\n", latest_freq_hz);
                DEBUG_PRINT("Reference slope: %d \n", ref_slope);
                DEBUG_PRINT("Latest slope: %d \n", debug_crossing.slope);
                freq_updated = false;
            }    

            if (have_adc_stats)
            {
                DEBUG_PRINT("ADC: P2P=%u, mean=%.2f, min=%d, max=%d \n",
                latest_adc_stats.p2p,
                latest_adc_stats.mean,
                latest_adc_stats.min,
                latest_adc_stats.max);

                DEBUG_PRINT("Midpoint: %d \n", latest_adc_stats.midpoint);
                // DEBUG_PRINT("ADC Errors: %d \n", latest_adc_stats.adc_errors);
                // DEBUG_PRINT("Queue Errors: %d \n", latest_adc_stats.queue_errors);
            }

            last_print_us = now_us;
        }

    }
}
