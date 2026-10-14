#include "sample_buffer_input.hpp"

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/regs/adc.h"

#include <cmath>

// Initialize variables private to this file
namespace
{
    // Connected pico ADC GPIO pin
    constexpr unsigned adc_gpio = 26;
    // Automatic selection of ADC channel based on GPIO pin
    constexpr unsigned adc_channel = adc_gpio - 26;

    // Direct Memory Access channel (initially -1 for unclaimed)
    int dma_channel = -1;
    // Initializes DMA config to 0, not defaults
    dma_channel_config dma_config{};

    SampleFrame captured_frame{};
}

/* 
 * Initializes ADC and DMA for conversion and transfer of input signal data.
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

    // Claim an available DMA channel, or fail initialization if none are available
    dma_channel = dma_claim_unused_channel(true);
    // Start with default DMA config
    dma_config = dma_channel_get_default_config(static_cast<unsigned>(dma_channel));

    // Configure the DMA transfers
    // Transfer 16 bits (fits 12-bit ADC conversions)
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_16);
    // Data will always be read from FIFO output, so incrementing is unnecessary
    channel_config_set_read_increment(&dma_config, false);
    // Data is written to the sample array, so incrementing is necessary
    channel_config_set_write_increment(&dma_config, true);
    // Let ADC coordinate DMA transfers when samples are available
    channel_config_set_dreq(&dma_config, DREQ_ADC);
}

/*
 * Collects a frame of ADC samples using DMA, then gathers and records errors and statistics.
 * Returns collected frame and associated data as a SampleFrame struct reference.
 * Note: DMA transfer process creates a blocking delay.
 */
const SampleFrame& capture_sample_frame()
{
    // Require proper initialization before attempting to capture a frame
    if (dma_channel < 0)
    {
        panic("Sample input must be initialized before capture");
    }

    // Frame capture is invalid by default
    captured_frame.capture_valid = false;

    // Drain the FIFO to discard leftover samples
    adc_run(false);
    adc_fifo_drain();

    // Clear any previous capture's FIFO flags
    adc_hw->fcs = adc_hw->fcs |
        ADC_FCS_OVER_BITS |
        ADC_FCS_UNDER_BITS;

    // Arm DMA for this frame
    dma_channel_configure(
        static_cast<unsigned>(dma_channel), // Channel to configure
        &dma_config,                        // Address of saved configuration
        captured_frame.samples.data(),      // Transfer destination (pointer to array of samples)
        &adc_hw->fifo,                      // Transfer source (pointer to ADC FIFO)
        static_cast<uint32_t>(              
            captured_frame.samples.size()), // Number of sample transfers
        true);                              // Start DMA transfer immediately

    // Record current timestamp to measure frame capture duration
    const uint32_t start_us = time_us_32();

    // Begin ADC conversions
    adc_run(true);
    // Wait for DMA to finish transferring all ADC samples
    dma_channel_wait_for_finish_blocking(static_cast<unsigned>(dma_channel));   // Main thread waits, but interrupts remain enabled
    // Stop ADC conversions once complete
    adc_run(false);

    // Record elapsed time for frame capture
    captured_frame.capture_time_us = time_us_32() - start_us;

    // Drain FIFO so it's clean for next operation
    adc_fifo_drain();

    // Record FIFO overflow flag
    captured_frame.fifo_overflowed = (adc_hw->fcs & ADC_FCS_OVER_BITS) != 0;
    // Reset errors and statistics
    captured_frame.conversion_error_count = 0;
    captured_frame.near_rail_count = 0;
    captured_frame.minimum_adc = 4095;
    captured_frame.maximum_adc = 0;
    uint32_t sum = 0;   // Sum of ADC samples for calculating mean
    uint64_t sum_squares = 0;   // Sum of squared samples for RMS

    // Inspect and clean captured samples
    for (size_t i = 0; i < captured_frame.samples.size(); ++i)
    {
        // Retrieve raw ADC sample
        const uint16_t raw = captured_frame.samples[i];

        // Record any ADC conversion errors
        if ((raw & ADC_FIFO_ERR_BITS) != 0)
        {
            ++captured_frame.conversion_error_count;
        }

        // Store only 12-bit ADC conversion (mask out flags)
        const uint16_t sample = static_cast<uint16_t>(raw & ADC_FIFO_VAL_BITS);
        captured_frame.samples[i] = sample;

        // Track minimum sample value
        if (sample < captured_frame.minimum_adc)
        {
            captured_frame.minimum_adc = sample;
        }

        // Track maximum sample value
        if (sample > captured_frame.maximum_adc)
        {
            captured_frame.maximum_adc = sample;
        }

        // Track number of samples near the 0 or 3.3 V rails
        if (sample <= 8 || sample >= 4087)
        {
            ++captured_frame.near_rail_count;
        }

        // Update total sample sum
        sum += sample;
        // Update squares sample sum
        sum_squares += static_cast<uint64_t>(sample * sample);
    }

    // Calculate final frame statistics
    // Store total sample count
    const double count = static_cast<double>(captured_frame.samples.size());
    // Calculate average sample value
    const double mean = static_cast<double>(sum) / count;
    // Store mean
    captured_frame.mean_adc = static_cast<float>(mean);
    // Calculate average of squared samples
    const double mean_square = static_cast<double>(sum_squares) / count;
    // Calculate sample variance
    const double variance = mean_square - mean * mean;
    // Calculate and store RMS
    captured_frame.rms_adc = static_cast<float>(std::sqrt(variance > 0.0 ? variance : 0.0));

    // P2P = max - min
    captured_frame.peak_to_peak_adc = 
        static_cast<uint16_t>(
            captured_frame.maximum_adc -
            captured_frame.minimum_adc);
            
    // Capture is considered valid if no overflow or conversion errors occured
    captured_frame.capture_valid = 
        !captured_frame.fifo_overflowed &&
        captured_frame.conversion_error_count == 0;

    return captured_frame;
}