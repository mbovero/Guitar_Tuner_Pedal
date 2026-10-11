#include "adc_input_handler.hpp"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "pico/util/queue.h"
#include <algorithm>
#include <cmath>

// Midpoint crossing event queue for passing data from ADC ISR to pitch estimation process
static queue_t input_event_queue;
// Midpoint crossing event queue's maximum number of entries
static constexpr unsigned input_event_queue_cap = 64;

// ADC stats queue for passing statistics from ADC ISR to main while loop
static queue_t adc_stats_queue;
// ADC stats queue's maximum number of entries
static constexpr unsigned adc_stats_queue_cap = 4;
// Number of ADC samples taken per block
static constexpr uint32_t adc_block_samples = 16'000;
// Struct to track the statistics of the current ADC block
static ADCBlockStats current_adc_block
{
    4095,   // Min: highest possible ADC value
    0,      // Max: lowest possible ADC value
    0,      // Sum
    0      // Number of samples
};

// Threshold to go from idle state to active state
static constexpr unsigned active_threshold = 300;
// Threshold to go from active state to idle state 
static constexpr unsigned idle_threshold = 200;
// Current state of the signal input
static InputState state = InputState::Idle;

// Number of consecutive ADC blocks with p2p below the idle threshold
static uint32_t quiet_blocks = 0;
// Number of consecutive quiet ADC blocks before midpoint can be adjusted
static constexpr uint16_t quiet_blocks_threshold = 16;
// The midpoint voltage adjusted by ADC measurements when the input is idle and quiet
static float midpoint_estimate = 2048.0f;
// The rounded integer midpoint used to remove offset from raw ADC samples
static int16_t midpoint = 2048;

// Number of ADC counts signal must drop below midpoint to accept the next rising crossing
static constexpr int16_t crossing_hysteresis = 50;

// Counts for errors
static int32_t adc_error_count = 0;
static int32_t waveform_queue_errors = 0;
static int32_t state_change_queue_errors = 0;
static int32_t stats_queue_errors = 0;


/*
 * Updates the current ADC block statistics with the provided sample.
 * If the current block's sampling is complete, the statistics are pushed to the queue
 * and a new block is started.
 * Also performs state updates and midpoint adjustments after each block.
 */
static void process_adc_sample(uint16_t sample)
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

        // After each block, update state
        if (peak_to_peak < idle_threshold)
        {
            state = InputState::Idle;

            // Also track number of consecutive quiet blocks
            ++quiet_blocks;
        } 
        else if (peak_to_peak >= active_threshold)
        {
            state = InputState::Active;

            // Reset quiet blocks count
            quiet_blocks = 0;
        }
        else
        {
            // When between thresholds, maintain state but do not count as quiet blocks
            quiet_blocks = 0;
        }

        // Gradually adjust midpoint when input is quiet
        if (state == InputState::Idle &&
            peak_to_peak < idle_threshold &&
            quiet_blocks > quiet_blocks_threshold)
        {
            midpoint_estimate += 0.125f * (mean - midpoint_estimate);

            midpoint = static_cast<int16_t>(std::lround(midpoint_estimate));
        }

        // Update final stats
        current_adc_block.p2p = peak_to_peak;
        current_adc_block.mean = mean;
        current_adc_block.midpoint = midpoint;
        current_adc_block.adc_errors = adc_error_count;
        current_adc_block.waveform_queue_errors = waveform_queue_errors;
        current_adc_block.state_change_queue_errors = state_change_queue_errors;
        current_adc_block.stats_queue_errors = stats_queue_errors;

        // Push statistics to queue; discard if queue is full
        if (!queue_try_add(&adc_stats_queue, &current_adc_block))
        {
            ++stats_queue_errors;
        }
        // Reset current ADC block stats to begin new block
        current_adc_block = {4095, 0, 0, 0};
    }    
}


/*
 * Interrupt service routine for handling ADC samples:
 * Watches ADC input for midpoint crossings and pushes their timestamps and slope to the 
 * event queue for further processing.
 */
static void guitar_input_isr()
{
    // The number of ADC samples taken (used for midpoint crossing timestamping)
    static uint32_t sample_count = 0;
    // Variable to store previous ADC sample value (used to detect midpoint crossings)
    static int16_t prev_sample = 0;
    // Boolean indicating that there is an up-to-date previous sample
    static bool have_prev = false;
    // Boolean indicating that the input reached the hysteresis arming threshold, so the next midpoint crossing should be recorded
    static bool crossing_armed = false;

    // The pulse whose rising edge has been identified
    static WaveformEvent pending_pulse;
    // Whether a pulse is actively being measured
    static bool collecting_pulse = false;
    // Timestamp for a downward slope crossing for pulse analysis (counted in ADC samples)
    static uint32_t falling_sample_count = 0;
    // Flag indicating that a falling crossing has been acquired for the current pulse
    static bool have_falling = false;

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
            // Disarm (reset hysteresis state)
            crossing_armed = false;
            // Reset ADC block statistics
            current_adc_block = {4095, 0, 0, 0};
            // Reset pulse tracking
            collecting_pulse = false;
            have_falling = false;

            // TODO: notify frequency estimator of discontinuity
            ++adc_error_count;
            continue;
        }

        // Isolate the 12-bit sample data as an unsigned integer
        uint16_t usample = (raw_sample & 0x0FFFu);

        // Store previous state before updating it
        const InputState prev_state = state;
        // Update ADC block statistics
        process_adc_sample(usample);

        // Handle state changes
        if (state != prev_state)
        {
            // Avoid comparing samples across state transitions
            have_prev = false;
            // Disarm (reset hysteresis state)
            crossing_armed = false;
            // Reset pulse tracking
            collecting_pulse = false;
            have_falling = false;

            // Format a state change event
            const InputEvent event
            {
                InputEventType::StateChanged,
                state
            };

            // Send state change event to main
            if (!queue_try_add(&input_event_queue, &event))
            {
                // TODO: robust queue full handling
                ++state_change_queue_errors;
            }
        }

        // Convert sample data to a signed integer
        int16_t sample = static_cast<int16_t>(usample);
        // Remove the VBIAS offset from the sample to center it around 0
        sample -= midpoint;

        // When input is active
        if (state == InputState::Active)
        {
            /* Update or finish pulse currently being collected */
            if (collecting_pulse)
            {
                // Track largest positive sample (pulse height)
                if (sample > 0)
                {
                    pending_pulse.pulse_height = std::max(
                        pending_pulse.pulse_height, 
                        static_cast<uint16_t>(sample));
                }

                // Record falling midpoint crossing
                if (have_prev &&
                    prev_sample >= 0 &&
                    sample < 0)
                {
                    falling_sample_count = sample_count;
                    have_falling = true;
                }

                // If input signal returns to positive side, invalidate falling midpoint crossing
                if (sample > 0)
                {
                    have_falling = false;
                }

                // Once signal falls below hysteresis threshold, stored falling crossing is confirmed
                if (have_falling &&
                    sample <= -crossing_hysteresis)
                {
                    // Calculate pulse width (falling crossing timestamp - rising crossing timestamp)
                    pending_pulse.pulse_width = falling_sample_count - pending_pulse.sample_count;

                    // Require a peak that rises above a positive hysteresis threshold
                    if (pending_pulse.pulse_height >= crossing_hysteresis &&
                        pending_pulse.pulse_width > 0)
                    {
                        // Create midpoint crossing event with confirmed pulse data
                        InputEvent event{};
                        event.type = InputEventType::Crossing;
                        event.state = state;
                        event.crossing = pending_pulse;

                        if (!queue_try_add(&input_event_queue, &event))
                        {
                            ++waveform_queue_errors;
                        }
                    }

                    // Pulse collection complete, reset variables
                    collecting_pulse = false;
                    have_falling = false;
                }
            }

            /* Arm detection for next rising crossing (hysteresis) */
            if (sample <= -crossing_hysteresis)
            {
                crossing_armed = true;
            }

            /* Begin measuring newly detected positive pulse */
            if(crossing_armed &&
                have_prev &&
                prev_sample < 0 &&
                sample >= 0)
            {
                // Create new crossing event and store crossing data
                pending_pulse = {};
                pending_pulse.sample_count = sample_count;
                pending_pulse.slope = sample - prev_sample;
                pending_pulse.sample_after = sample;
                // Initialize pulse height for later comparison/updating
                pending_pulse.pulse_height = static_cast<uint16_t>(sample);
                // Update pulse tracking flags
                collecting_pulse = true;
                have_falling = false;
                // Reset hysteresis arming
                crossing_armed = false;
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
 * Initializes the input event and stats queues then configures the ADC into free-running sampling 
 * mode to continuously read the guitar input signal, save conversions into an 8 sample FIFO buffer, 
 * and trigger an interrupt that is handled by guitar_in_isr() each time a new sample is recorded.
 */
void initialize_guitar_input()
{
    // Initialize the midpoint crossing event queue
    queue_init(
        &input_event_queue,     // Pointer to the queue to be initialized
        sizeof(InputEvent),     // Size of each entry in the queue
        input_event_queue_cap   // Maximum number of entries
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
    // If ADC sampling rate set to maximum (500 KHz), divisor must be set differently
    if (sample_rate_hz >= 500'000.0f)
    {
        adc_set_clkdiv(0.0f);
    }
    else    // Otherwise, do standard Hz to divisor conversion
    {
        adc_set_clkdiv((48'000'000.0f / sample_rate_hz) - 1.0f);
    }


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
 * Non-blocking removal of the oldest input handler event queue entry if non empty.
 * If non empty, returns true and copies the removed entry into the provided location.
 * Otherwise, returns false.
*/
bool try_get_input_event(InputEvent& event)
{
    return queue_try_remove(&input_event_queue, &event);
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
