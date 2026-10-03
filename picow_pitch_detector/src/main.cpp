#include <stdio.h>
#include "pico/stdlib.h"
#include "guitar_input.hpp"

int main()
{
    stdio_init_all();

    // Set up the ADC to intake the guitar signal
    initialize_guitar_input();

    while (true) 
    {
        // Continuously try to retrieve the latest crossing event
        CrossingEvent event;
        while (try_get_crossing_event(event))
        {
            // queue try remove to take crossing events from the queue and add them to pitch estimator history
            // compare candidate slopes, periods, etc.
        }

        

        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
