#include "guitar_input.hpp"

#include "hardware/adc.h"
#include "hardware/irq.h"
#include "pico/util/queue.h"
#include <algorithm>

// Midpoint crossing event queue for passing data from ADC ISR to pitch estimation process
static queue_t crossing_queue;
// Midpoint crossing event queue's maximum number of entries
static constexpr unsigned crossing_queue_cap = 64;

// ADC stats queue for passing statistics from ADC ISR to main while loop
static queue_t adc_stats_queue;
// ADC stats queue's maximum number of entries
static constexpr unsigned adc_stats_queue_cap = 4;
// Number of ADC samples taken per block
static constexpr uint32_t adc_block_samples = 4096;
// Struct to track the statistics of the current ADC block
static ADCBlockStats current_adc_block
{
    4095,   // Min: highest possible ADC value
    0,      // Max: lowest possible ADC value
    0,      // Sum
    0,      // Number of samples
    0,
    0
};

// Threshold to go from idle state to active state
static constexpr unsigned active_threshold = 300;
// Threshold to go from active state to idle state 
static constexpr unsigned idle_threshold = 200;
// Current state of the signal input
static InputState state = InputState::Idle;

// The estimated midpoint voltage measured by the ADC during idle input
static int16_t midpoint = 2234;


/*
 * Updates the current ADC block statistics with the provided sample.
 * If the current block's sampling is complete, the statistics are pushed to the queue
 * and a new block is started.
 * Also performs state updates and midpoint adjustments after each block.
 */
static void record_adc_sample(uint16_t sample)
{
    // Update statistics with provided sample
    current_adc_block.min = std::min(current_adc_block.min, sample);
    current_adc_block.max = std::max(current_adc_block.max, sample);
    current_adc_block.sum += sample;
    ++current_adc_block.samples;

    // Check if block sampling is complete
    if (current_adc_block.samples >= adc_block_samples)
    {
        // Calculate peak to peak amplitude and raw mean input
        const unsigned peak_to_peak = current_adc_block.max - current_adc_block.min;
        const float mean = static_cast<float>(current_adc_block.sum) / current_adc_block.samples;

        // After each block, update input state
        if (peak_to_peak <= idle_threshold)
        {
            state = InputState::Idle;
        } 
        else if (peak_to_peak >= active_threshold)
        {
            state = InputState::Active;
        }

        // Gradually adjust midpoint while idle
        if (state == InputState::Idle)
        {
            midpoint += 0.125f * (mean - midpoint);
        }

        // Update final stats
        current_adc_block.p2p = peak_to_peak;
        current_adc_block.mean = mean;
        current_adc_block.midpoint = midpoint;

        // Push statistics to queue; discard if queue is full
        (void)queue_try_add(&adc_stats_queue, &current_adc_block);
        // Reset current ADC block stats to begin new block
        current_adc_block = {4095, 0, 0, 0, 0, 0};
    }    
}


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

        // Handle ADC sample errors
        if ((raw_sample & 0x8000u) != 0)
        {
            // Still track sample count to maintain period accuracy
            ++sample_count;
            // Invalidate this sample, initialize next sample as previous
            have_prev = false;
            // Reset ADC block statistics
            current_adc_block = {4095, 0, 0, 0};

            // TODO: notify frequency estimator of discontinuity
            continue;
        }

        // Isolate the 12-bit sample data as an unsigned integer
        uint16_t usample = (raw_sample & 0x0FFFu);
        // Update ADC block statistics
        record_adc_sample(usample);

        // Convert sample data to a signed integer
        int16_t sample = static_cast<int16_t>(usample);
        // Remove the VBIAS offset from the sample to center it around 0
        sample -= midpoint; // 2048 is half of the 12-bit ADC resolution representing 1.65 V

        
        if (state == InputState::Active &&      // If there is an active input,
            have_prev &&                        // there is a previous sample to compare to,
            (prev_sample < 0 && sample >= 0))   // and this is a midpoint crossing:
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

    // Initialize the ADC block statistics queue
    queue_init(
        &adc_stats_queue,
        sizeof(ADCBlockStats),
        adc_stats_queue_cap
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

/*
 * Non-blocking removal of the oldest ADC block statistics queue entry if non empty.
 * If non empty, returns true and copies the removed entry into the provided location.
 * Otherwise, returns false.
*/
bool try_get_adc_block_stats(ADCBlockStats& stats)
{
    return queue_try_remove(&adc_stats_queue, &stats);
}
