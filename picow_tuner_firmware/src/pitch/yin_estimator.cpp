#include "yin_estimator.hpp"

#include <cmath>

YinEstimator::YinEstimator(float sampling_rate)
    : sample_rate_hz(sampling_rate)
{
}

YinEstimate YinEstimator::estimate(const uint16_t* samples, size_t sample_count)
{
    YinEstimate result{};

    // Input samples and sampling rate must be valid
    if (samples == nullptr ||
        !std::isfinite(sample_rate_hz) ||
        sample_rate_hz <= 0.0f)
    {
        return result;
    }

    // Calculate estimator frequency limits in terms of ADC samples
    const float min_period = sample_rate_hz / max_frequency_hz;
    const float max_period = sample_rate_hz / min_frequency_hz;

    // Configured period range must be valid
    if (min_period < 2.0f ||    // At least two samples per period are required for comparison
        max_period > static_cast<float>(max_supported_lag)) // Max period cannot exceed working array capacity
    {
        return result;
    }

    // Convert fractional bounds into indexes
    const size_t min_lag = static_cast<size_t>(std::floor(min_period));
    const size_t max_lag = static_cast<size_t>(std::ceil(max_period));
    const size_t last_lag = max_lag + 1;    // Includes extra neighbor needed for interpolation

    // There must be enough samples to allow for the configured comparison and frequency range
    if (sample_count < comparison_samples + last_lag)
    {
        return result;
    }

    // Ensure all samples are within 12-bit ADC range
    for (size_t i = 0; i < sample_count; ++i)
    {
        if (samples[i] > 4095)
        {
            return result;
        }
    }


    /* Calculate the difference function across valid lag range:
        d(tao) = summation with range i=[0,comparison_samples) of (x[i] - x[i + tao])^2
        where tao is the lag, x is the array of samples, and i is the index. 
        d(tao) is evaluated for tao=[0, last_lag]. */
    // Maintain value at index 0 as 0 for biasing later
    difference_function[0] = 0.0f;

    // Calculate difference function at all valid lag values
    for (size_t lag = 1; lag <= last_lag; ++lag)
    {
        // Initialize sum to zero for each function calculation
        uint64_t sum = 0;

        // Difference function
        for (size_t i = 0; i < comparison_samples; ++i)
        {
            // Calculate difference using int32 for larger/negative results
            const int32_t difference = 
                static_cast<int32_t>(samples[i]) -
                static_cast<int32_t>(samples[i + lag]);

            // Store squared difference as uint64 for large results and because result is always positive
            sum += static_cast<uint64_t>(difference * difference);
        }

        // Store difference function result for this lag
        difference_function[lag] = static_cast<float>(sum);
    }

    /* Normalize the differences:
        d'(tao) = (tao * d(tao)) / (summation with range j=[1, last_lag] of d(j))
        and d'(0) = 1. */
    // Prevent picking strong local minimum near beginning of function as incorrect fundamental period
    normalized_difference[0] = 1.0f;

    // Perform normalization using running sum
    float running_sum = 0.0f;
    for (size_t lag = 1; lag <= last_lag; ++lag)
    {
        // Update running sum
        running_sum += difference_function[lag];

        // Only perform normalization with valid running sum (avoid division by 0)
        if (running_sum > 0.0f)
        {
            normalized_difference[lag] = 
                difference_function[lag] *
                static_cast<float>(lag) /
                running_sum;
        }
        else    // Otherwise, bias result to 1 to prevent it from disrupting period identification
        {
            normalized_difference[lag] = 1.0f;
        }
    }

    /* Locate the first qualifying valley:
        We search the normalized data for the first value below the estimator's configured
        threshold. We then follow that curve while it continues downward to locate the bottom,
        likely indicating the sampled signal's period.
        The first valley is targeted because later valleys likely align with periods of octaves
        or harmonics of the fundamental. */
    // Initialize candidate index as 0, indicating "no candidate"
    size_t candidate = 0;

    // Traverse normalized difference data using valid period range in terms of lag
    for (size_t lag = min_lag; lag < max_lag; ++lag)
    {
        // Find the first value below the threshold
        if (normalized_difference[lag] < threshold)
        {
            // Follow the curve downward to the bottom of the valley
            while (lag < max_lag &&     // Stop at max period
                   normalized_difference[lag + 1] <     // Stop when curve is no longer increasing
                    normalized_difference[lag])
            {
                ++lag;
            }

            // Store identified lag as a candidate for input signal period
            candidate = lag;
            break;
        }
    }

    // If no valid candidate was found, return default result
    if (candidate == 0)
    {
        return result;
    }

    /* Refine the period estimation:
        The normalized curve is used for candidate selection, but the original difference curve
        is re-used for a final three-point fit:
        We have the following points: (x-1, y1), (x, y2), and (x+1, y3).
        These can be used to calculate the x value (period) at which the minimum of the parabola 
        occurs:
            x_min = x + (y1 - y3) / 2(y1 - 2*y2 + y3)
        We have x (currently candidate), we just need the other term in the equation. */
    // Store estimated period (x) as float
    float period = static_cast<float>(candidate);

    // Retrieve three points of the parabola
    const float left = difference_function[candidate - 1];  // y1
    const float center = difference_function[candidate];    // y2
    const float right = difference_function[candidate + 1]; // y3

    // Calculate the denominator of the x_min equation
    const float denominator = left - 2.0f * center + right;

    // Only apply offset if denominator is not near zero
    if (denominator > 1.0e-6f)
    {
        // Calculate offset (right-most term of x_min equation)
        const float offset = 0.5f * (left - right) / denominator;

        // Only apply offest if it is finite and within half a sample of the candidate index
        if (std::isfinite(offset) &&
            std::fabs(offset) <= 0.5f)
        {
            period += offset;
        }
    }   // Otherwise, period remains as integer candidate

    /* Calculate frequency from the identified period */
    const float frequency = sample_rate_hz / period;

    // Reject frequencies outside the supported range
    if (!std::isfinite(frequency) ||
        frequency < min_frequency_hz ||
        frequency > max_frequency_hz)
    {
        return result;
    }

    // Populate valid result
    result.pitch.valid = true;
    result.pitch.freq_hz = frequency;

    result.period_samples = period;
    result.normalized_difference = normalized_difference[candidate];

    return result;
}