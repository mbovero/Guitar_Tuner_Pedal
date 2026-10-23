#pragma once

#include <cstddef>
#include <cstdint>

// Below this threshold, the input is considered idle
inline constexpr uint16_t minimum_signal_peak_to_peak = 200;
// Idle input DC voltage in ADC values
inline constexpr int32_t midpoint_adc = 2093;

namespace SampleInputConfig
{
    // ADC sampling rate (Hz)
    inline constexpr uint32_t sample_rate_hz = 7680;
    // Number of ADC samples in one frame
    inline constexpr size_t frame_sample_count = 1024;

    static_assert(
        sample_rate_hz > 0,
        "Sample rate must be positive");

    static_assert(
        frame_sample_count > 0,
        "Frame size must be positive");

    // Approximately how long it takes to acquire a frame of ADC samples
    inline constexpr double expected_frame_time_us = 1'000'000.0 * frame_sample_count / sample_rate_hz;
}

namespace HumFilterConfig
{
    // The filter attenuates this frequency and every integer multiple of it
    inline constexpr uint32_t fundamental_hz = 60;
    // The full width between each notch's -3 dB points, measured in Hz
    inline constexpr double notch_bandwidth_hz = 2.0;

    static_assert(
        fundamental_hz > 0,
        "Hum frequency must be positive");
    
    static_assert(
        SampleInputConfig::sample_rate_hz % fundamental_hz == 0,
        "Integer comb requires sample rate to be a multiple of hum frequency");

    // Period of the fundamental frequency in samples; used for delay in filter difference equation
    inline constexpr size_t delay_samples = SampleInputConfig::sample_rate_hz / fundamental_hz;

    static_assert(
        delay_samples >= 2,
        "Comb requires at least two samples per hum cycle");
}

namespace ProcessedSampleLimits
{
    // These constants represent the range of valid outputs from filtering ADC samples: +/- 13 bits
    // The range is wider than the ADC resolution because filter transients can push the resulting
    // data past that range.

    // Minimum allowed value for a filtered ADC sample
    inline constexpr int32_t minimum = -8192;
    // Maximum allowed value for a filtered ADC sample
    inline constexpr int32_t maximum = 8191;
}