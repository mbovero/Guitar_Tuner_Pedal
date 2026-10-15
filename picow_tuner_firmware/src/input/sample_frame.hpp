#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Configuration for the sample buffer input
namespace SampleInputConfig
{
    constexpr float sample_rate_hz = 8000.0f;   // ADC sampling rate
    constexpr size_t frame_sample_count = 1024; // Number of ADC samples in a frame
}

// A frame of ADC samples, the frame's validity, and frame errors and statistics 
struct SampleFrame
{
    // Collection of ADC samples
    std::array<uint16_t, SampleInputConfig::frame_sample_count> samples{};

    // True if no FIFO overflows or sample conversions errors were observed
    bool capture_valid = false;
    // Whether an ADC FIFO overflow occured during this frame
    bool fifo_overflowed = false;

    // Number of captured samples carrying an ADC error flag this frame
    uint32_t conversion_error_count = 0;
    // Number of samples encountered near the ADC's limits this frame (clipping indicator)
    uint32_t near_rail_count = 0;

    // Waveform statistics
    uint16_t minimum_adc = 0;   // Minimum sample value this frame
    uint16_t maximum_adc = 0;   // Maximum sample value this frame
    uint16_t peak_to_peak_adc = 0;  // Peak to peak amplitude this frame

    float mean_adc = 0.0f;  // Average ADC value this frame
    float rms_adc = 0.0f;   // Root Mean Square of ADC values this frame

    uint32_t sequence_number = 0;   // Value to track sequential frames
    bool discontinuous = false;     // Indicates if something disrupted the continuous data stream

    uint32_t capture_time_us = 0;   // Interval between DMA completion interrupts
};