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
    // Create a ring buffer to hold history of midpoint crossing timestamps and slopes
    CrossingHistory history{};

    
    // Set up variables for resetting reference slope after certain time without matches
    uint32_t last_match_us = time_us_32();
    constexpr uint32_t ref_slope_reset_us = 25'000;
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
    CrossingEvent debug_event{0,0};

    while (true) 
    {
        // Continuously drain the queue and process midpoint crossing events
        CrossingEvent event;
        while (try_get_crossing_event(event))
        {
            history.push(event);
            debug_event = event;

            // Set a reference slope if one doesn't exist
            if (!have_reference)
            {
                prev_ref_slope = ref_slope;
                ref_slope = event.slope;
                prev_match = event;
                last_match_us = time_us_32();
                have_reference = true;
                ref_slope_updated = true;
                continue;
            }

            // If this event's slope matches the reference slope, print the estimated frequency
            if (std::abs(ref_slope - event.slope) <= ref_slope_tolerance)
            {
                // Calculate total elapsed samples as float
                const float elapsed_samples = static_cast<float>(event.sample_count - prev_match.sample_count);

                // Convert average period in ADC cycles to frequency
                latest_freq_hz = sample_rate_hz / elapsed_samples;
                freq_updated = true;

                // Update last matched event to this one
                prev_match = event;
                // Update last reference slope match timestamp
                last_match_us = time_us_32();

                // TODO: dynamically updating reference slope?
            }

        }

        // Get current time for upkeep tasks
        const uint32_t now_us = time_us_32();

        // Reset the reference slope if one exists and no matches occured over a certain timeframe
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

            if (ref_slope_updated &&
                (prev_ref_slope != ref_slope))
            {
                DEBUG_PRINT("Reset reference slope from %d\n", prev_ref_slope);
                ref_slope_updated = false;
            }

            if (freq_updated)
            {
                DEBUG_PRINT("Estimated frequency: %.5f Hz\n", latest_freq_hz);
                DEBUG_PRINT("Reference slope: %d \n", ref_slope);
                DEBUG_PRINT("Latest slope: %d \n", debug_event.slope);
                freq_updated = false;
            }    

            last_print_us = now_us;
        }

    }
}
