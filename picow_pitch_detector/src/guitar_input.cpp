#include "guitar_input.hpp"

#include "hardware/adc.h"
#include "hardware/irq.h"
#include "pico/util/queue.h"

// Midpoint crossing event queue for passing data from ADC ISR to pitch estimation process
static queue_t crossing_queue;
// Midpoint crossing event queue can hold a maximum of 64 events
constexpr unsigned crossing_queue_cap = 64;

/*
 * Interrupt service routine for handling ADC samples:
 * Watches ADC input for midpoint crossings and pushes their timestamps and slope to the 
 * midpoint crossing event queue for further processing.
 */
static void guitar_input_isr()
{
    // Initialize sample count for midpoint crossing timestamping
    static uint32_t sample_count = 0;
    // Initialize previous sample to 0
    static int16_t prev_sample = 0;
    // Indicate that there is initially no previous sample
    static bool have_prev = false;
    
    // While the FIFO buffer has samples, process them
    while (!adc_fifo_is_empty())
    {
        // Retrieve a raw FIFO sample
        const uint16_t raw_sample = adc_fifo_get();

        // If an error occured, reset the midpoint crossing history and skip processing this sample
        if ((raw_sample & 0x8000u) != 0)
        {
            // TODO: reset midpoint crossing history

            continue;
        }

        // Isolate the 12-bit sample data and convert it to a signed integer
        int16_t sample = static_cast<int16_t>((raw_sample & 0x0FFF));

        // Remove the VBIAS offset from the sample to center it around 0
        sample -= 2048; // 2048 is half of the 12-bit ADC resolution representing 1.65 V

        // If there is a previous sample to compare to, and this is a midpoint crossing:
        if (have_prev && 
            (prev_sample < 0 && sample >= 0))
        {
            // Create a midpoint crossing event
            const CrossingEvent event
            {
                sample_count,           // Store the current sample count as a timestamp
                (sample - prev_sample)  // Store the approximate slope at this timestamp
            };
            
            // Try to push it to the midpoint crossing event queue to be processed
            if (!queue_try_add(&crossing_queue, &event))
            {
                // TODO: queue is full, record error and/or signal to pitch estimator
            }
        }

        // Increment the sample count
        ++sample_count; // Note: this eventually overflows, but applied operations still work properly
        // Store current sample as the previous sample
        prev_sample = sample;
        // Indicate that there is a previous sample
        have_prev = true;
    }
}


/*
 * Initializes the midpoint crossing event queue then configures the ADC into free-running sampling 
 * mode to continuously read the guitar input signal, save conversions into an 8 sample FIFO buffer, 
 * and trigger an interrupt that is handled by guitar_in_isr() each time a new sample is recorded.
 */
void initialize_guitar_input()
{
    // Initialize the midpoint crossing event queue
    queue_init(
        &crossing_queue,        // Pointer to the queue to be initialized
        sizeof(CrossingEvent),  // Size of each entry in the queue
        crossing_queue_cap      // Maximum number of entries
    );

    // Initialize ADC hardware
    adc_init();

    // Initialize GPIO26 for use as an ADC pin (high-impedance, no pullups, etc.)
    adc_gpio_init(26);
    // Select input 0 from the MUX (routed to GPIO26)
    adc_select_input(0);

    // Set up an ADC FIFO that holds 8 samples; have each conversion written to the FIFO, 
    // disable DMA requests, trigger FIFO IRQ each time a sample is received, enable 
    // error bit, and disable DMA byte shifting 
    adc_fifo_setup(true, false, 1, true, false);

    // Set the ADC clock divisor to achieve the desired sampling rate
    adc_set_clkdiv((48'000'000.0f / sample_rate_hz) - 1.0f);

    // Register custom interrupt service routine for handling ADC samples
    irq_set_exclusive_handler(ADC_IRQ_FIFO, guitar_input_isr);
    // Enable ADC interrupts
    adc_irq_set_enabled(true);
    // Enable ADC FIFO interrupt on this core
    irq_set_enabled(ADC_IRQ_FIFO, true);

    // Enable free-running sampling mode
    adc_run(true);
}

/*
 * Non-blocking removal of the oldest midpoint crossing event queue entry if non empty.
 * If non empty, returns true and copies the removed entry into the provided location.
 * Otherwise, returns false.
*/
bool try_get_crossing_event(CrossingEvent& event)
{
    return queue_try_remove(&crossing_queue, &event);
}
