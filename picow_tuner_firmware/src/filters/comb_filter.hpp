#pragma once
#include <array>
#include <cmath>
#include <cstddef>

// Size must be known at compile time to avoid dynamic memory allocation
template <size_t DelaySamples>  // A comb notch filter that attenuates a fundamental frequency and its integer multiples
class CombFilter    
{
    static_assert(
        DelaySamples >= 2,
        "Comb delay must be at least two samples");

public:
    /* 
     * Configures the CombFilter with maximum gain of 1 and the specified notch bandwidth using 
     * the provided sampling rate.
     * Bandwidth is the full width between each notch's -3 dB points, measured in Hz.
     * Returns true if configuration was successful, false if parameters were invalid
     */
    bool configure(double sampling_rate, double notch_bandwidth_hz)
    {
        // Require parameters to be positive and finite
        if (!std::isfinite(sampling_rate) ||
            sampling_rate <= 0.0 ||
            !std::isfinite(notch_bandwidth_hz) ||
            notch_bandwidth_hz <= 0.0)
        {
            return false;
        }

        // Spacing between consecutive notches in hz
        const double spacing_hz = sampling_rate / DelaySamples;

        // Restrict implementation to narrow notches 
        if (notch_bandwidth_hz >= spacing_hz * 0.5)
        {
            return false;
        }

        // Restrict implementation to feedback below 1x
        constexpr double pi = 3.14159265358979323846;
        // Intermediate variable to calculate feedback coefficient
        const double bw_param = std::tan(pi * notch_bandwidth_hz / (2.0 * spacing_hz));
        // Feedback coefficient must be positive and less than one to ensure proper filter behavior and stability
        const double new_feedback = (1.0 - bw_param) / (1.0 + bw_param);
        if (!std::isfinite(new_feedback) ||
            new_feedback < 0.0 ||
            new_feedback >= 1.0)
        {
            return false;
        }

        feedback = new_feedback;

        // Normalize maximum passband gain to 1
        gain = 0.5 * (1.0 + feedback);

        reset();

        return true;
    }

    /*
     * Resets the filters history. Assumes an intial constant input
     */
    void reset(double initial_input = 0.0)
    {
        // Upon resetting, assume a previous constant input
        input_history.fill(initial_input);
        // Previous output set to 0 to replicate filter's response to constant input
        output_history.fill(0.0);

        // Set first buffer index to use on the next call
        next_index = 0;
    }

    /*
     * Filters the provided double using the filter's history and configured difference equation.
     * Updates filter history and returns the filtered double.
     */
    double process(double input)
    {
        // Retrieve the input and output from DelaySamples samples ago
        const double delayed_input = input_history[next_index];
        const double delayed_output = output_history[next_index];

        // Apply comb filter difference equation
        const double output = gain * (input - delayed_input) + feedback * (delayed_output);

        // Replace circular buffer's oldest values with newest values
        input_history[next_index] = input;
        output_history[next_index] = output;

        // Advance to next oldest value
        ++next_index;
        // Wrap around after reaching end of the array
        if (next_index >= DelaySamples)
        {
            next_index = 0;
        }

        return output;
    }

private:
    // Gain parameter used to limit max filter gain to 1
    double gain = 1.0;
    // Delayed output coefficient; influences bandwidth
    double feedback = 0.0;

    // Previous inputs seen by the filter
    std::array<double, DelaySamples> input_history{};
    // Previous outputs produced by the filter (used for feedback)
    std::array<double, DelaySamples> output_history{};

    // Pointer to the oldest element in the filter history (the next element to be processed)
    size_t next_index = 0;
};