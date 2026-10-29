#pragma once

#include <cstddef>
#include <cstdint>

// Below this threshold, the input is considered idle
inline constexpr uint16_t minimum_signal_peak_to_peak = 200;
// Idle input DC voltage in ADC values
inline constexpr int32_t midpoint_adc = 2093;

namespace SampleInputConfig
{
    // Fixed rate supports integer comb delays for both 60 Hz and 50 Hz hum frequencies
    inline constexpr uint32_t sample_rate_hz = 9600;    // ADC sampling rate (Hz)
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
    // Full width between each notch's -3 dB points
    inline constexpr double notch_bandwidth_hz = 2.0;

    static_assert(
        SampleInputConfig::sample_rate_hz % 50u == 0 &&
        SampleInputConfig::sample_rate_hz % 60u == 0,
        "Sample rate must support integer delays for both hum frequencies");

    inline constexpr size_t delay_50_samples = SampleInputConfig::sample_rate_hz / 50u;
    inline constexpr size_t delay_60_samples = SampleInputConfig::sample_rate_hz / 60u;

    static_assert(
        delay_50_samples >=2 && delay_60_samples >= 2,
        "Comb delays must be at least two samples");
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

namespace ButtonInputConfig
{
    // Active-low menu inputs with internal pull-ups
    inline constexpr unsigned left_gpio = 14;
    inline constexpr unsigned right_gpio = 15;

    // Input must remain unchanged for this long before being accepted (debouncing)
    inline constexpr uint32_t debounce_ms = 25;
}