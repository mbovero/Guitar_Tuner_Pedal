#include "hum_filter.hpp"

/*
* Filters samples from input array, stores filtered results in output array, and collects
* and returns statistics from the filtered frame.
* Input samples are passed through a notch comb filter to attenuate 60 Hz (by default)
* hum and harmonics.
* Returns default statistics if inputs were invalid or an error occured.
*/
FilteredFrameStats filter_frame(
    HumCombFilter& filter,
    const int16_t* input,
    int16_t* output,
    size_t sample_count)
{
    FilteredFrameStats stats{};

    // Return early if source or destination is null, or if sample count is zero
    if (input == nullptr ||
        output == nullptr ||
        sample_count == 0)
    {
        return stats;
    }

    // Initialize variables to track min vs max
    int32_t minimum = ProcessedSampleLimits::maximum;
    int32_t maximum = ProcessedSampleLimits::minimum;
    // Initialize sums for mean and RMS
    int64_t sum = 0;
    uint64_t sum_squares = 0;

    // Iterate through samples
    for (size_t i = 0; i < sample_count; ++i)
    {
        // Filter each sample
        const double filtered = filter.process(static_cast<double>(input[i]));

        // If filtered sample is invalid, return early
        if (!std::isfinite(filtered) ||
            filtered < ProcessedSampleLimits::minimum ||           // We allow values +/- 2^13 so filter transients do not invalidate the data
            filtered > ProcessedSampleLimits::maximum)
        {
            return stats;
        }

        // Convert filtered result to integer for YIN processing
        const int32_t value = static_cast<int32_t>(std::lround(filtered));

        // Store filtered value in output array
        output[i] = static_cast<int16_t>(value);

        // Update minimum
        if (value < minimum)
        {
            minimum = value;
        }

        // Update maximum
        if (value > maximum)
        {
            maximum = value;
        }

        // Update sums
        sum += value;
        sum_squares += static_cast<uint64_t>(value * value);
    }

    // Calculate and store filtered statistics
    const double count = static_cast<double>(sample_count);
    const double mean = static_cast<double>(sum) / count;
    const double variance = 
        static_cast<double>(sum_squares) / count -
        mean * mean;
    stats.peak_to_peak_adc = static_cast<uint32_t>(maximum - minimum);
    stats.rms_adc = static_cast<float>(
        std::sqrt(variance > 0.0 ? variance : 0.0));
    stats.valid = true;

    return stats;
}
