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

    while (true) 
    {
        // Continuously drain the queue and push midpoint crossing events to the history ring buffer
        CrossingEvent event;
        while (try_get_crossing_event(event))
        {
            history.push(event);
        }

        // Once all queued crossing events have been recorded:
        // If there is only one recorded crossing event, no comparisons can be made
        const size_t event_count = history.size();
        if (event_count < 2)
        {
            continue;
        }

        // The latest event with a slope matching the max slope (initially 0, 0)
        static CrossingEvent prev_match{0, 0};

        // Number of consecutive comparisons made to the max slope without resulting in a match (initially 0)
        static uint16_t failed_matches = 0;
        // Maximum number of consecutive failed match attempts that can occur before max slope resets
        static constexpr uint16_t max_failed_matches = 5;
        // How much a compared slope can vary from max slope and still be considered matching
        static constexpr uint16_t max_slope_tolerance = 15;

        // Maximum identified crossing event slope (initially 0)
        static int32_t max_slope = 0;

        // Iterate through midpoint crossing history to find similar entry slopes and mark periods
        for (size_t i = 0; i < event_count; ++i)
        {
            // Attempt to retrieve event from history
            if (!history.try_get(i, event))
            {
                // If a retrieval fails, print error and skip frequency estimation
                DEBUG_PRINT("ERROR: Failed to retrieve crossing event from history!");
                break;
            }

            //DEBUG_PRINT("Slope: %d \n", event.slope);

            // If this event's slope matches the max slope, print the estimated frequency
            if (std::abs(max_slope - event.slope) <= max_slope_tolerance)
            {
                // Calculate total elapsed samples as float
                const float elapsed_samples = static_cast<float>(event.sample_count - prev_match.sample_count);

                // Convert average period in ADC cycles to frequency
                const float avg_freq_hz = sample_rate_hz / elapsed_samples;

                DEBUG_PRINT("Estimated frequency: %.5f Hz\n", avg_freq_hz);
                //DEBUG_PRINT("Max Slope: %d \n", max_slope);

                // Update last matched event to this one
                prev_match = event;
                // Reset failed event slope match counter
                failed_matches = 0;
                // TODO: dynamically updating max slope?
                max_slope = event.slope;
                continue;
            }

            if (failed_matches >= max_failed_matches)
            {
                DEBUG_PRINT("Reset max slope: %d \n", max_slope);
            }

            // If maxmimum number of max failed matches has been reached, reset variables
            // OR if this event's slope is greater than max slope, update variables
            if ((failed_matches >= max_failed_matches) ||
                (event.slope > max_slope))
            {
                // Reset max slope and reset failed match count
                max_slope = event.slope;
                failed_matches = 0;
                // Reset last matched event to this one
                prev_match = event;
                // No need for comparison to itself, skip to next event
                continue;
            }

            // Otherwise, current event slope does not match, so increment failed slope match count
            ++failed_matches;
        } 






    }
}
