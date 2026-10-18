#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "frequency_estimate.hpp"

// The estimated pitch from the YIN algorithm, alongside some related data
struct YinEstimate
{
    // The estimated frequency and its validity
    FrequencyEstimate frequency_estimate{};

    // The estimated number of samples per waveform cycle
    float period_samples = 0.0f;
    // The normalized mismatch at the selected period (lower means closer match)
    float normalized_difference = 1.0f;
};

// Pitch estimator implemented using the YIN algorithm
class YinEstimator
{
public:
    // Initializes a YinEstimator instance with the provided ADC sampling rate
    explicit YinEstimator(float sampling_rate);

    YinEstimate estimate(const int16_t* samples, size_t sample_count);

private:
    // Number of samples compared at each lag
    static constexpr size_t comparison_samples = 512;
    // Indexing outside this lag will index outside of the samples array
    static constexpr size_t max_supported_lag = comparison_samples - 1;

    // Allowed estimator frequency range
    static constexpr float min_frequency_hz = 40.0f;
    static constexpr float max_frequency_hz = 1500.0f;

    static constexpr float threshold = 0.10f;

    // ADC sampling rate set by constructor
    const float sample_rate_hz;

    /* These array sizes allow for index 0, supported lags 1 to max_supported_lags, 
    and an extra neighbor at the end for interpolation */
    // YIN Difference Function d(tao)
    std::array<float, max_supported_lag + 2> difference_function{};
    // YIN Normalized Difference Function d'(tao)
    std::array<float, max_supported_lag + 2> normalized_difference{};
};