#include "notch_filter.hpp"

#include <cmath>

/* 
 * Calculates and configures filter coefficients using provided parameters.
 * Returns true if configuration is successful. False indicates invalid inputs.
 */
bool NotchFilter::configure(double sampling_rate, double frequency_hz, double quality_factor)
{
    // Validate inputs
    if (!std::isfinite(sampling_rate) ||    // Require finite parameters
        !std::isfinite(frequency_hz) ||
        !std::isfinite(quality_factor) ||
        sampling_rate <= 0.0 ||             // Require positive parameters
        frequency_hz <= 0.0 ||
        frequency_hz >= sampling_rate * 0.5 ||  // Nyquist theorem
        quality_factor <= 0.0)
    {
        return false;
    }

    // Store estimate of pi
    constexpr double pi = 3.14159265358979323846;

    // Calculate center frequency in rad/sample
    const double omega = 2.0 * pi * frequency_hz / sampling_rate;
    const double cosine = std::cos(omega);

    // Calculate notch width
    const double alpha = std::sin(omega) / (2.0 * quality_factor);

    // Ensure alpha is finite and positive
    if (!std::isfinite(alpha) || alpha <= 0.0)
    {
        return false;
    }

    // Calculate normalized coefficients
    const double a0 = 1.0 + alpha;

    b0 = 1.0 / a0;
    b1 = -2.0 * cosine / a0;
    b2 = b0;

    a1 = b1;
    a2 = (1.0 - alpha) / a0;

    // Clear history of old samples
    reset();

    return true;
}

/*
 * Reinitialize the stored sample history.
 *
 * This filter passes a constant input with unity steady-state gain.
 * Initializing both input and output history to an estimated constant baseline
 * avoids the filter from behaving as if the circuit's DC baseline suddenly
 * appeared from zero.
 */
void NotchFilter::reset(double initial_value)
{
    input_1 = initial_value;
    input_2 = initial_value;

    output_1 = initial_value;
    output_2 = initial_value;
}

/* 
 * Process one sample and update filter history.
 */
double NotchFilter::process(double input)
{
    // Process sample
    const double output = 
        b0 * input +
        b1 * input_1 +
        b2 * input_2 -
        a1 * output_1 -
        a2 * output_2;

    // Update history
    input_2 = input_1;
    input_1 = input;
    output_2 = output_1;
    output_1 = output;

    return output;
}