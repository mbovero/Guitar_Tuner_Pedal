#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "pitch_result.hpp"

// The estimated pitch from the YIN algorithm, alongside some related data
struct YinEstimate
{
    // The estimated frequency and its validity
    PitchResult pitch{};

    // The estimated number of samples per waveform cycle
    float period_samples = 0.0f;
    // The normalized mismatch at the selected period (lower means closer match)
    float normalized_difference = 1.0f;
};

class YinEstimator
{
public:
    // Initializes a YinEstimator instance with the provided ADC sampling rate
    explicit YinEstimator(float sampling_rate);

    YinEstimate estimate(const uint16_t* samples, size_t sample_count);

private:
    // Number of samples compared at each lag
    static constexpr size_t comparison_samples = 512;
    // Indexing outside this lag will index outside of the samples array
    static constexpr size_t max_supported_lag = comparison_samples - 1;

    // Allowed estimator frequency range
    static constexpr float min_frequency_hz = 40.0f;
    static constexpr float max_frequency_hz = 1500.0f;

    static constexpr float threshold = 0.15f;

    // ADC sampling rate set by constructor
    const float sample_rate_hz;

    /* These array sizes allow for index 0, supported lags 1 to max_supported_lags, 
    and an extra neighbor at the end for interpolation */
    // TODO description
    std::array<float, max_supported_lag + 2> difference_function{};
    // TODO description
    std::array<float, max_supported_lag + 2> normalized_difference{};
};