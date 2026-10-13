#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "pico/stdlib.h"

#include "debug.hpp"
#include "pitch/yin_estimator.hpp"

// Number of test samples
constexpr size_t test_sample_count = 1024;
// ADC sampling rate
constexpr float sample_rate_hz = 8000.0f;

// Estimation of pi
constexpr float pi = 3.14159265358979323846f;

// Populates the provided samples array with a simualted sine wave with the provided frequency
void generate_test_signal(std::array<uint16_t, test_sample_count>& samples, float frequency_hz)
{
    // Midpoint of 1.65 V
    constexpr float midpoint = 2048.0f;
    // Signal amplitude of ~0.146 V
    constexpr float amplitude = 600.0f;

    for (size_t i = 0; i < samples.size(); ++i)
    {
        // Convert ADC sample period into seconds
        const float time_seconds = static_cast<float>(i) / sample_rate_hz;

        // Calculate angle based on current time 
        const float angle = 2.0f * pi * frequency_hz * time_seconds;

        // Calculate waveform value using estimated angle
        const float value = midpoint + amplitude*std::sin(angle);

        // Round waveform value to simulate ADC sample
        samples[i] = static_cast<uint16_t>(std::lround(value));
    }
}

int main()
{
    stdio_init_all();

    sleep_ms(2000); // Time to connect to serial monitor

    static std::array<uint16_t, test_sample_count> test_samples{};

    static YinEstimator estimator{sample_rate_hz};

    constexpr float test_frequency_hz = 100.0f;
    generate_test_signal(test_samples, test_frequency_hz);

    // Main while loop
    while (true)
    {
        const uint32_t start_us = time_us_32();

        const YinEstimate estimate = estimator.estimate(test_samples.data(), test_samples.size());

        const uint32_t elapsed_us = time_us_32() - start_us;

        DEBUG_PRINT("Input %.3f Hz, Valid: %d, Estimate: %.3f Hz \n",
            test_frequency_hz,
            static_cast<int>(estimate.pitch.valid),
            estimate.pitch.freq_hz);

        DEBUG_PRINT("Period: %.4f samples, Normalized Difference: %.6f, Processing: %lu us \n",
            estimate.period_samples,
            estimate.normalized_difference,
            static_cast<unsigned long>(elapsed_us));

        sleep_ms(1000);
    }
}