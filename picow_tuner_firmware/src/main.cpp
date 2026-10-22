#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "pico/stdlib.h"

#include "debug.hpp"
#include "tuner_config.hpp"
#include "frequency/yin_estimator.hpp"
#include "input/adc_input.hpp"
#include "tuning/tuning_feedback.hpp"
#include "filters/comb_filter.hpp"

namespace
{
    // Notch comb filter type alias
    using HumCombFilter = CombFilter<HumFilterConfig::delay_samples>;   

    // Contains statistics on a filtered frame of ADC samples
    struct FilteredFrameStats
    {
        // Whether filtering produced valid samples
        bool valid = false;

        // pk2pk of the filtered sample frame
        uint32_t peak_to_peak_adc = 0;
        // Root Mean Squre of the filtered sample frame
        float rms_adc = 0.0f;
    };

    /*
    * Filters samples from input array, stores filtered results in output array, and collects
    * and returns statistics from the filtered frame.
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
        int32_t minimum = 8191;
        int32_t maximum = -8192;
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
                filtered < -8192.0 ||           // We allow values +/- 2^13 so filter transients do not invalidate the data
                filtered > 8191.0)
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

int main()
{
    stdio_init_all();

    sleep_ms(2000); // Time to connect to serial monitor

    // Configure ADC, DMA, and queue for continuous input signal frame acquisition
    initialize_sample_input();

    // Initialize our YIN frequency estimator
    static YinEstimator estimator{SampleInputConfig::sample_rate_hz};
    if (!estimator.supports_frame(SampleInputConfig::frame_sample_count))
    {
        panic("YIN sample rate, frequency range, and frame size are incompatible");
    }

    // Made static to keep array out of normal stack
    static SampleFrame frame{}; // The current frame of ADC samples being processed

    // Configure filter
    static HumCombFilter comb;
    if (!comb.configure(
            SampleInputConfig::sample_rate_hz,
            HumFilterConfig::notch_bandwidth_hz))
    {
        panic("Failed to configure hum comb filter");
    }


    // Array for centering ADC samples near 0 V
    static std::array<int16_t, SampleInputConfig::frame_sample_count> centered_samples{};
    // Array to hold filtered ADC samples
    static std::array<int16_t, SampleInputConfig::frame_sample_count> filtered_samples{};

    // Filter control variables
    // Notch filters toggle
    constexpr bool use_filter = true;
    // Whether the notch filters have been initialized and have usable history
    bool filter_initialized = false;


    DEBUG_PRINT(
    "Sample rate: %lu Hz, expected frame: %.1f us, comb delay: %u samples \n",
    static_cast<unsigned long>(SampleInputConfig::sample_rate_hz),
    SampleInputConfig::expected_frame_time_us,
    static_cast<unsigned>(HumFilterConfig::delay_samples));

    // Begin input signal frame acquisition
    start_sample_input();

    // Below this threshold, the input is considered idle
    constexpr uint16_t minimum_signal_peak_to_peak = 200;
    // Idle input DC voltage in ADC values
    constexpr double midpoint = 2093.0;

    // Create tuning feedback config
    const TuningConfig tuning_config {
        440.0f, // Modern standard pitch (A4 = 440 Hz)
        1.0f    // +/- range from target pitch that is considered "In Tune"
    };
    // Most recent tuning feedback
    TuningResult latest_feedback{};


    // Main while loop
    while (true)
    {
        // Attempt to retrieve a sample frame
        if(!try_get_sample_frame(frame))
        {
            // Skip processing if no frame was available
            continue;
        }

        // Initialize default estimate
        YinEstimate estimate{};
        // Initialize default filtered frame stats
        FilteredFrameStats filtered_stats{};

        // Initialize total filtering time
        uint32_t filter_time_us = 0;
        // Initialize total processing time
        uint32_t processing_time_us = 0;

        // If ADC sample frame is invalid, require filter to be reinitialized (continuous stream is required)
        if (!frame.capture_valid)
        {
            filter_initialized = false;
        }
        else    // Otherwise frame is valid, so process it
        {
            // Center all samples
            for (size_t i = 0; i < frame.samples.size(); ++i)
            {
                // Remove approximate DC offset from each sample for intuitive calculations
                const int32_t centered = static_cast<int32_t>(frame.samples[i]) - static_cast<int32_t>(midpoint);
                // Update centered samples array
                centered_samples[i] = static_cast<int16_t>(centered);
            }

            // If filter has not been initialized, or if a discontinuity occurred, reinitialize it
            if (!filter_initialized || frame.discontinuous)
            {
                // Reset the notch filter using an estimated baseline as the initial value
                const double initial_value = static_cast<double>(frame.mean_adc) - midpoint;
                comb.reset(initial_value);

                // Mark filter as initialized
                filter_initialized = true;
            }

            // Record current timestamp to measure time spent filtering
            const uint32_t filter_start_us = time_us_32();
            // Filter this frame and record filtered frame statistics
            filtered_stats = filter_frame(
                comb,
                centered_samples.data(),
                filtered_samples.data(),
                centered_samples.size());
            // Calculate total filtering time
            filter_time_us = time_us_32() - filter_start_us;

            // If filtered results are invalid, require filter to be reset
            if (!filtered_stats.valid)
            {
                filter_initialized = false;
            }
        }

        // Feed estimator different samples based on whether notch filter was enabled
        const int16_t* estimator_samples =
            use_filter
                ? filtered_samples.data()
                : centered_samples.data();
        // Calculate pk2pk based on whether notch filter was enabled
        const uint32_t estimator_peak_to_peak = 
            use_filter
                ? filtered_stats.peak_to_peak_adc
                : frame.peak_to_peak_adc;

        // Determine if input to the estimator is valid
        const bool estimator_input_valid =
            frame.capture_valid &&      // Original sample frame must be valid
            (!use_filter || filtered_stats.valid);   // And must either not be using filter, or filtered results are valid

        // Determine if an input signal is present
        const bool signal_present = estimator_peak_to_peak >= minimum_signal_peak_to_peak;

        // Check that the input to the estimator is valid and a useful input was captured
        if (estimator_input_valid && signal_present)
        {
            // Track YIN computation time
            const uint32_t processing_start_us = time_us_32();
            // Perform estimation of signal frequency
            estimate = estimator.estimate(
                estimator_samples,
                SampleInputConfig::frame_sample_count);
            // Calculate elapsed calculation time
            processing_time_us = time_us_32() - processing_start_us;
        }


        // Print results/statistics
        DEBUG_PRINT(
            "\nframe=%lu, gap=%d, dropped=%lu\n"
            "ADC: min=%u max=%u mean=%.1f \n"
            "capture=%lu errors=%lu overflow=%d rails=%lu \n",
            static_cast<unsigned long>(frame.sequence_number),
            static_cast<int>(frame.discontinuous),
            static_cast<unsigned long>(dropped_sample_frames()),
            static_cast<unsigned>(frame.minimum_adc),
            static_cast<unsigned>(frame.maximum_adc),
            frame.mean_adc,
            static_cast<unsigned long>(frame.capture_time_us),
            static_cast<unsigned long>(frame.conversion_error_count),
            static_cast<int>(frame.fifo_overflowed),
            static_cast<unsigned long>(frame.near_rail_count));
        DEBUG_PRINT(
            "raw RMS=%.2f, filtered RMS=%.2f \n",
            frame.rms_adc,
            filtered_stats.rms_adc);
        DEBUG_PRINT(
            "raw pk2pk=%u, filtered pk2pk=%lu \n"
            "filter: %lu us, YIN: %lu us \n",
            static_cast<unsigned>(frame.peak_to_peak_adc),
            static_cast<unsigned long>(filtered_stats.peak_to_peak_adc),
            static_cast<unsigned long>(filter_time_us),
            static_cast<unsigned long>(processing_time_us));

        if (!frame.capture_valid)
        {
            DEBUG_PRINT("Capture invalid\n");
        }
        else if (use_filter && !filtered_stats.valid)
        {
            DEBUG_PRINT("Filter output invalid\n");
        }
        else if (!signal_present)
        {
            DEBUG_PRINT("Input quiet\n");
        }
        else if (!estimate.valid)
        {
            DEBUG_PRINT("No accepted pitch\n");
        }
        else
        {
            DEBUG_PRINT(
                "Frequency: %.3f Hz, Period: %.4f samples, "
                "Difference: %.6f \n",
                estimate.freq_hz,
                estimate.period_samples,
                estimate.normalized_difference);

            // Get tuning feedback based on latest frequency
            latest_feedback = analyze_frequency(estimate.freq_hz, tuning_config);

            // Print valid tuning feedback
            if (latest_feedback.valid)
            {
                DEBUG_PRINT("Note: %s%d \n", latest_feedback.note_name, latest_feedback.octave);
                DEBUG_PRINT("Measured: %.2f Hz, Target: %.2f Hz \n", 
                    latest_feedback.measured_freq_hz, latest_feedback.target_freq_hz);
                DEBUG_PRINT("Error: %+.1f cents \n", latest_feedback.cents_error);
                DEBUG_PRINT("Status: %s \n", tuning_status_text(latest_feedback.status));
            }
            else
            {
                DEBUG_PRINT("No valid note estimation \n");
            }

        }
    }
}