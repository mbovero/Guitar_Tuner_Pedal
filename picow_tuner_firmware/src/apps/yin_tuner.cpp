#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "pico/stdlib.h"

#include "debug.hpp"
#include "pitch/yin_estimator.hpp"
#include "input/sample_buffer_input.hpp"
#include "tuning/tuning_feedback.hpp"

int main()
{
    stdio_init_all();

    sleep_ms(2000); // Time to connect to serial monitor

    initialize_sample_input();

    static YinEstimator estimator{SampleInputConfig::sample_rate_hz};

    constexpr uint16_t minimum_signal_peak_to_peak = 200;

    // Create tuning feedback config
    const TuningConfig note_config {
        440.0f, // Modern standard pitch (A4 = 440 Hz)
        3.0f    // +/- range from target pitch that is considered "In Tune"
    };
    // Most recent tuning feedback
    TuningResult latest_note{};


    // Main while loop
    while (true)
    {
        // Collect a frame of ADC samples
        const SampleFrame& frame = capture_sample_frame();

        // Initialize default estimate
        YinEstimate estimate{};
        // Initialize total processing time
        uint32_t processing_time_us = 0;

        // Determine if an input signal is present
        const bool signal_present = frame.peak_to_peak_adc >= minimum_signal_peak_to_peak;

        // Check that the frame is valid and captured a useful input
        if (frame.capture_valid && signal_present)
        {
            // Track YIN computation time
            const uint32_t start_us = time_us_32();
            // Perform estimation of signal frequency
            estimate = estimator.estimate(
                frame.samples.data(),
                frame.samples.size());
            // Calculate elapsed calculation time
            processing_time_us = time_us_32() - start_us;
        }


        // Print results/statistics
        DEBUG_PRINT(
            "\n ADC: min=%u max=%u p2p=%u mean=%.1f \n"
            "capture=%lu us errors=%lu overflow=%d rails=%lu \n",
            static_cast<unsigned>(frame.minimum_adc),
            static_cast<unsigned>(frame.maximum_adc),
            static_cast<unsigned>(frame.peak_to_peak_adc),
            frame.mean_adc,
            static_cast<unsigned long>(frame.capture_time_us),
            static_cast<unsigned long>(frame.conversion_error_count),
            static_cast<int>(frame.fifo_overflowed),
            static_cast<unsigned long>(frame.near_rail_count));

        if (!frame.capture_valid)
        {
            DEBUG_PRINT("Capture invalid \n");
        }
        else if (!signal_present)
        {
            DEBUG_PRINT("Input quiet \n");
        }
        else if (!estimate.pitch.valid)
        {
            DEBUG_PRINT("No accepted pitch \n");
        }
        else
        {
            DEBUG_PRINT(
                "Frequency: %.3f Hz, Period: %.4f samples, "
                "Difference: %.6f, Processing: %lu us \n",
                estimate.pitch.freq_hz,
                estimate.period_samples,
                estimate.normalized_difference,
                static_cast<unsigned long>(processing_time_us));

            // Get tuning feedback based on latest frequency
            latest_note = analyze_frequency(estimate.pitch.freq_hz, note_config);

            // Print valid tuning feedback
            if (latest_note.valid)
            {
                DEBUG_PRINT("Note: %s%d \n", latest_note.note_name, latest_note.octave);
                DEBUG_PRINT("Measured: %.2f Hz, Target: %.2f Hz \n", 
                    latest_note.measured_freq_hz, latest_note.target_freq_hz);
                DEBUG_PRINT("Error: %+.1f cents \n", latest_note.cents_error);
                DEBUG_PRINT("Status: %s \n", tuning_status_text(latest_note.status));
            }
            else
            {
                DEBUG_PRINT("No valid note estimation \n");
            }

        }

        sleep_ms(10);
    }
}