#include "sample_buffer_input.hpp"

#include <cmath>

#include "pico/stdlib.h"
#include "pico/util/queue.h"

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/regs/adc.h"

// Initialize variables private to this file
namespace
{
    // Connected pico ADC GPIO pin
    constexpr unsigned adc_gpio = 26;
    // Automatic selection of ADC channel based on GPIO pin
    constexpr unsigned adc_channel = adc_gpio - 26;

    // Holds raw ADC samples from DMA, including each sample's error bit; also carries acquisition metadata
    struct RawCaptureFrame
    {
        // Array containing raw ADC samples transferred using DMA
        std::array<uint16_t, SampleInputConfig::frame_sample_count> samples {};

        // Numerical marker indicating in what order this block was captured
        uint32_t sequence_number = 0;
        // Time taken to acquire this interval
        uint32_t interval_us = 0;

        // Whether the ADC FIFO overflowed during capture
        bool fifo_overflowed = false;
        // Whether detected acquisition timing implies continuity
        bool continuity_valid = false;
    };

    // Direct Memory Access channel (initially -1 for unclaimed)
    int dma_channel = -1;
    // Initializes DMA config to 0, not defaults
    dma_channel_config dma_config{};

    // One block is written to by DMA while the other has its data copied/queued; then they switch responsibilities
    RawCaptureFrame dma_frames[2]{};    // Array of two ADC sample blocks that alternate between capturing and completed
    // Dedicated memory for processing a queued block
    RawCaptureFrame processing_frame{};

    // A queue containing copies of the completed ADC sample blocks
    queue_t completed_frame_queue{};

    // Index into dma_frames marking the block currently being written into by DMA
    unsigned active_buffer = 0;
    // Whether input signal capturing via DMA has begun
    bool running = false;

    // Whether the previous block experienced a FIFO overflow
    bool previous_fifo_overflow = false;

    // Numerical marker for the block currently being captured (helps identify dropped frames)
    uint32_t capture_sequence = 0;
    // Time since last completed block capture
    uint32_t last_completion_us = 0;
    // The sequence number of the last frame delivered to main
    uint32_t last_delivered_sequence = 0;

    // Written to by ISR and read by main
    volatile uint32_t dropped_frames = 0;   // The number of ADC sample frames discarded because the queue was full

    /*
     * Gathers and records frame statistics and errors for the provided SampleFrame
     */
    void calculate_frame_stats(SampleFrame& frame)
    {
        // Reset errors and statistics
        frame.conversion_error_count = 0;
        frame.near_rail_count = 0;
        frame.minimum_adc = 4095;
        frame.maximum_adc = 0;
        uint32_t sum = 0;   // Sum of ADC samples for calculating mean
        uint64_t sum_squares = 0;   // Sum of squared samples for RMS

        for (auto& raw : frame.samples)
        {
            // Record any ADC conversion errors
            if ((raw & ADC_FIFO_ERR_BITS) != 0)
            {
                ++frame.conversion_error_count;
            }

            // Store only the 12-bit ADC conversion (mask out flags)
            const uint16_t sample = static_cast<uint16_t>(raw & ADC_FIFO_VAL_BITS);
            // Replace original raw sample with 12-bit ADC data
            raw = sample;

            // Track minimum sample value
            if (sample < frame.minimum_adc)
            {
                frame.minimum_adc = sample;
            }

            // Track maximum sample value
            if (sample > frame.maximum_adc)
            {
                frame.maximum_adc = sample;
            }

            // Track number of samples near the 0 or 3.3 V rails
            if (sample <= 8 || sample >= 4087)
            {
                ++frame.near_rail_count;
            }

            // Update total sample sum
            sum += sample;
            // Update squares sample sum
            sum_squares += static_cast<uint64_t>(sample * sample);
        }

        // Calculate final frame statistics
        // Pk2Pk = max - min
        frame.peak_to_peak_adc = 
            static_cast<uint16_t>(
                frame.maximum_adc -
                frame.minimum_adc);

        // Store total sample count
        const double count = static_cast<double>(frame.samples.size());
        
        // Calculate average sample value
        const double mean = static_cast<double>(sum) / count;
        // Store mean
        frame.mean_adc = static_cast<float>(mean);

        // Calculate average of squared samples
        const double mean_square = static_cast<double>(sum_squares) / count;
        // Calculate sample variance
        const double variance = mean_square - mean * mean;
        // Calculate and store RMS
        frame.rms_adc = static_cast<float>(std::sqrt(variance > 0.0 ? variance : 0.0));

        // Capture is considered valid if no overflow or conversion errors occured
        frame.capture_valid = 
            !frame.fifo_overflowed &&
            frame.conversion_error_count == 0;
    }

    /*
     * Function that is run once DMA finishes transferring a block of raw ADC samples.
     * Clears interrupt flag, manages DMA destination buffers, tracks time between block completions, 
     * sets up and triggers next DMA capture, and queues raw ADC sample block alongside tracked metadata.
     */
    void dma_irq_handler()
    {
        // Cast DMA channel to unsigned for repeated use
        const unsigned channel = static_cast<unsigned>(dma_channel);

        // Ensure that our channel caused the interrupt
        if (!dma_channel_get_irq0_status(channel))
        {
            return;
        }

        // Clear interrupt flag
        dma_channel_acknowledge_irq0(channel);

        // Store the index of the completed DMA block
        const unsigned completed_index = active_buffer;
        // Update active buffer to other buffer
        active_buffer ^= 1u;    // XOR operation switches between indexes 0 and 1

        // Grab current timestamp to track time between completions
        const uint32_t now_us = time_us_32();

        // Retrieve overflow boolean
        const bool overflow = (adc_hw->fcs & ADC_FCS_OVER_BITS) != 0;
        // Reset the overflow flag atomically
        if (overflow)
        {
            hw_set_bits(&adc_hw->fcs, ADC_FCS_OVER_BITS);
        }

        // Set initial DMA write address as beginning of newly active buffer, but don't start the transfer
        dma_channel_set_write_addr(
            channel,
            dma_frames[active_buffer].samples.data(),
            false);
        // Set number of DMA channel bus transfers and begin transferring immediately
        dma_channel_set_transfer_count(
            channel,
            static_cast<uint32_t>(
                SampleInputConfig::frame_sample_count),
            true);
        
        // Prevent compiler from moving reads of completed sample memory above completion check
        __compiler_membar();

        // Store reference to captured block of raw ADC samples
        RawCaptureFrame& completed_frame = dma_frames[completed_index];

        // Record and update capture sequence number
        completed_frame.sequence_number = ++capture_sequence;

        // Record time since last complete block was acquired
        completed_frame.interval_us = now_us - last_completion_us;
        // Update last completion timestamp to current timestamp
        last_completion_us = now_us;

        // Store overflow boolean
        completed_frame.fifo_overflowed = overflow;
        // Timing is valid if neither this block nor the last experienced an overflow
        completed_frame.continuity_valid = !overflow && !previous_fifo_overflow;

        // Attempt to queue a copy of the completed block
        if (!queue_try_add(&completed_frame_queue, &completed_frame))
        {
            // If queueing fails, track dropper frame
            ++dropped_frames;
        }
    }
}

/* 
 * Initializes ADC, DMA, queue, and interrupt for continuous conversion and transfer 
 * of input signal data.
 */
void initialize_sample_input()
{
    // Do not initialize more than once
    if (dma_channel >= 0)
    {
        return;
    }

    // Initialize ADC hardware
    adc_init();
    // Stop continuous conversions
    adc_run(false);
    // Disable ADC FIFO interrupt
    adc_irq_set_enabled(false);

    // Initialize configured GPIO pin for ADC sampling
    adc_gpio_init(adc_gpio);
    // Select corresponding ADC channel
    adc_select_input(adc_channel);
    // Disable round robin sampling
    adc_set_round_robin(0);

    // Set up ADC FIFO
    adc_fifo_setup(
        true,   // Place conversion results in the FIFO
        true,   // Allow the FIFO to request DMA transfers
        1,      // Request a transfer when any 1 sample is available
        true,   // Include each sample's conversion error flag
        false   // Preserve the full 12-bit ADC value
    );

    // Set the ADC clock divisor to achieve the desired sampling rate
    // If ADC sampling rate is set to maximum (500 KHz), divisor must be set differently
    if (SampleInputConfig::sample_rate_hz >= 500'000.0f)
    {
        adc_set_clkdiv(0.0f);
    }
    else    // Otherwise, do standard Hz to divisor conversion
    {
        adc_set_clkdiv((48'000'000.0f / SampleInputConfig::sample_rate_hz) - 1.0f);
    }

    // Discard any leftover samples
    adc_fifo_drain();

    // Atomically clear overflow and underflow bits
    hw_set_bits(&adc_hw->fcs, ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS);

    // Initialize a queue of RawCaptureBlocks with a maximum of two entries
    queue_init(&completed_frame_queue, sizeof(RawCaptureFrame), 2);

    // Claim an available DMA channel, or fail initialization if none are available
    dma_channel = dma_claim_unused_channel(true);
    const unsigned channel = static_cast<unsigned>(dma_channel);

    // Start with default DMA config
    dma_config = dma_channel_get_default_config(channel);

    // Configure the DMA transfers
    // Transfer 16 bits (fits 12-bit ADC conversions)
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_16);
    // Data will always be read from FIFO output, so incrementing is unnecessary
    channel_config_set_read_increment(&dma_config, false);
    // Data is written across the sample array, so incrementing is necessary
    channel_config_set_write_increment(&dma_config, true);
    // Let ADC coordinate DMA transfers when samples are available
    channel_config_set_dreq(&dma_config, DREQ_ADC);

    // Configure DMA channel read/write locations, etc., but don't start the transfer
    dma_channel_configure(
        channel,
        &dma_config,
        dma_frames[0].samples.data(),
        &adc_hw->fifo,
        static_cast<uint32_t>(SampleInputConfig::frame_sample_count),
        false);

    // Configure this channel to trigger DMA_IRQ_0
    dma_channel_acknowledge_irq0(channel);
    // Set custom handler function as DMA_IRQ_0 ISR
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    // Set DMA_IRQ_0 priority at highest to reduce/prevent overflows
    irq_set_priority(DMA_IRQ_0, PICO_HIGHEST_IRQ_PRIORITY);
    // Enable DMA_IRQ_0 triggering on this channel
    dma_channel_set_irq0_enabled(channel, true);
    // Enable the DMA_IRQ_0 IRQ
    irq_set_enabled(DMA_IRQ_0, true);
}

/*
 * Begins continuous acquisition of ADC samples using DMA. Should only be ran once.
 */
void start_sample_input()
{
    // Require proper initialization before attempting to capture a frame
    if (dma_channel < 0)
    {
        panic("Sample input must be initialized before capture");
    }

    // If input capture has already started, do nothing
    if (running)
    {
        return;
    }
    // Otherwise, start a new capture
    running = true;

    // Record timestamp for tracking capture duration
    last_completion_us = time_us_32();

    // Begin DMA transfer on configured channel
    dma_start_channel_mask(1u << static_cast<unsigned>(dma_channel));

    // Begin ADC sampling
    adc_run(true);
}

/*
 * If a complete frame is available, writes completed sample frame data into the provided 
 * SampleFrame reference and returns true. Returns false when no frame is available.
 */
bool try_get_sample_frame(SampleFrame& frame)
{
    // Attempe to retrieve sample frame from queue
    if (!queue_try_remove(&completed_frame_queue, &processing_frame))
    {
        // If retrieval fails, return early
        return false;
    }

    // Copy over raw ADC samples
    frame.samples = processing_frame.samples;
    // Copy over this frame's sequence number
    frame.sequence_number = processing_frame.sequence_number;
    // Copy over time taken to record this frame
    frame.capture_time_us = processing_frame.interval_us;
    // Copy over overflow status
    frame.fifo_overflowed = processing_frame.fifo_overflowed;
    // Copy over validity
    frame.capture_valid = processing_frame.continuity_valid;

    // Calculate and record frame statistics
    calculate_frame_stats(frame);

    // Continuity was broken if sequence number is out of line or capture was invalid
    frame.discontinuous = 
        frame.sequence_number != (last_delivered_sequence + 1u) ||
        !frame.capture_valid;

    // Update the last delivered sequence number
    last_delivered_sequence = frame.sequence_number;

    return true;
}

/*
 * Returns the number of ADC sample frames discarded because the queue was full.
 */
uint32_t dropped_sample_frames()
{
    return dropped_frames;
}
