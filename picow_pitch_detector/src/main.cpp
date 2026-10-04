#include <stdio.h>
#include "pico/stdlib.h"
#include "guitar_input.hpp"
#include "crossing_history.hpp"
#include "debug.hpp"

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

        // Try to get the oldest and newest events from the history
        CrossingEvent oldest;
        CrossingEvent newest;
        if (!history.try_get(0, oldest) ||
            !history.try_get(event_count - 1, newest))
        {
            continue; // TODO: If either retrievals fail, log error 
        }

        // Calculate total elapsed samples as float
        const float elapsed_samples = static_cast<float>(newest.sample_count - oldest.sample_count);
        // Calculate total number of periods as float
        const float period_count = static_cast<float>(event_count - 1);
        // Calculate average period in terms of ADC cycles
        const float avg_period_cycles = elapsed_samples / period_count;

        // TODO: If average period is under 11 cycles, log error due to noise or unusually frequent crossings

        // Convert average period in ADC cycles to frequency
        const float avg_freq_hz = sample_rate_hz / avg_period_cycles;

        DEBUG_PRINT("Estimated frequency: %.2f Hz\n", avg_freq_hz);

        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
