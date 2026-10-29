#include "hum_filter.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    /*
    * Filters samples from input array, stores filtered results in output array, and collects
    * and returns statistics from the filtered frame.
    * Input samples are passed through a notch comb filter to attenuate hum (60 Hz by default)
    * and harmonics.
    * Returns default statistics if inputs were invalid or an error occured.
    */
    template <size_t DelaySamples>
    FilteredFrameStats filter_frame_impl(
        CombFilter<DelaySamples>& filter,
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
}

/*
 * Configures both statically allocated comb filters.
 * Returns false if configuration of a filter failed, otherwise true.
 */
bool HumFilter::configure()
{
    // Return false if either filter configuration fails
    if (!filter_50_hz.configure(
            SampleInputConfig::sample_rate_hz,
            HumFilterConfig::notch_bandwidth_hz) ||
        !filter_60_hz.configure(
            SampleInputConfig::sample_rate_hz,
            HumFilterConfig::notch_bandwidth_hz))
    {
        return false;
    }

    reset();
    return true;
}

/*
 * Selects a mode and discards its previous history
 */
void HumFilter::set_mode(FilterMode mode)
{
    mode_ = mode;
    reset();
}

bool HumFilter::enabled() const
{
    return mode_ != FilterMode::Off;
}

/*
 * Resets the active comb filter
 */
void HumFilter::reset(double initial_input)
{
    switch (mode_)
    {
        case FilterMode::Hz50:
            filter_50_hz.reset(initial_input);
            break;
        
        case FilterMode::Hz60:
            filter_60_hz.reset(initial_input);
            break;

        case FilterMode::Off:
            break;
    }
}

/*
* Filters samples from input array, stores filtered results in output array, and collects
* and returns statistics from the filtered frame.
* Input samples are passed through a notch comb filter to attenuate configured hum 
* frequency and harmonics.
* Returns default statistics if inputs were invalid or an error occured.
*/
FilteredFrameStats HumFilter::filter_frame(
    const int16_t* input,
    int16_t* output,
    size_t sample_count)
{
    FilteredFrameStats stats{};

    switch (mode_)
    {
        case FilterMode::Hz50:
            stats = filter_frame_impl(filter_50_hz, input, output, sample_count);
            break;

        case FilterMode::Hz60:
            stats = filter_frame_impl(filter_60_hz, input, output, sample_count);
            break;

        case FilterMode::Off:
            return stats;
    }

    return stats;
}