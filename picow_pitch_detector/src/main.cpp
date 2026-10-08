#include <stdio.h>
#include "pico/stdlib.h"
#include "guitar_input.hpp"
#include "crossing_history.hpp"
#include "debug.hpp"
#include <cstdlib>
#include <cmath>

// State of the period estimator
enum class PeriodState
{
    Acquiring,  // Considering candidate periods before committing to one
    Tracking    // Actively tracking a calculated period and improving estimates
};

int main()
{
    stdio_init_all();

    // Set up the ADC to intake the guitar signal and queue midpoint crossing events
    initialize_guitar_input();

    
    // Supported frequency range in Hz
    constexpr float min_frequency_hz = 40.0f;
    constexpr float max_frequency_hz = 1500.0f;
    // Supported frequenct range in terms of ADC samples
    constexpr float min_period_samples = sample_rate_hz / max_frequency_hz;
    constexpr float max_period_samples = sample_rate_hz / min_frequency_hz;

    // How much a compared slope can vary from reference slope and still be considered matching
    constexpr float slope_noise_allowance = 10.0f;
    constexpr float slope_relative_allowance = 0.1f;

    // Tolerance allowed for matching candidate periods
    constexpr float period_relative_allowance = 0.08f;

    // Number of consistent intervals required before reporting frequency
    constexpr unsigned acquisition_intervals = 3;

    // Smoothing factors applied to accepted measurements
    constexpr float period_alpha = 0.2f;
    constexpr float slope_alpha = 0.1f;

    // Default configuration allows four periods at lowest supported frequency
    // With a 40 Hz minimum, timeout is 100 ms
    constexpr uint32_t estimator_timeout_us = static_cast<uint32_t>(4'000'000.0f / min_frequency_hz);

    PeriodState period_state = PeriodState::Acquiring;
    
    // Whether a valid reference slope has been set; helps initialize/reset ref_slope
    bool have_reference = false;
    // Reference slope used to identify beginning/end of waveform period
    float ref_slope = 0.0f;
    // The latest accepted crossing event event
    CrossingEvent prev_match{};

    unsigned consistent_intervals = 0;
    float mean_period_samples = 0.0f;

    uint32_t last_accepted_us = time_us_32();

    float latest_freq_hz = 0.0f;
    bool freq_updated = false;


    // Variables for regularly printing status updates
    uint32_t last_print_us = time_us_32();
    constexpr uint32_t print_interval_us = 500'000; // 2 Hz
    CrossingEvent debug_crossing{0,0};

    // Latest statistics from the ADC ISR
    ADCBlockStats latest_adc_stats{};
    // Flag to initialize latest ADC statistics struct
    bool have_adc_stats = false;

    // Current input handler state
    InputState input_state = InputState::Idle;

    // Lambda function that resets local estimator variables by reference
    auto reset_estimator = [&]()
    {
        period_state = PeriodState::Acquiring;

        have_reference = false;
        ref_slope = 0.0f;
        prev_match = {};

        consistent_intervals = 0;
        mean_period_samples = 0.0f;

        latest_freq_hz = 0.0f;
        freq_updated = false;
    };

    while (true) 
    {
        // Continuously drain the midpoint crossing event queue and process events
        InputEvent event;
        while (try_get_input_event(event))
        {
            // Handle state change events
            if (event.type == InputEventType::StateChanged)
            {
                // Store current state
                input_state = event.state;
                // Reset the frequency estimator
                reset_estimator();
                // Process next event
                continue;
            }

            // If the input is not active, don't process other events
            if (input_state != InputState::Active)
            {
                continue;
            }


            // Otherwise, handle event as a crossing event
            const CrossingEvent& crossing = event.crossing;
            // Copy of the crossing event for debug prints
            debug_crossing = crossing;

            // Set a reference slope if a valid one does not exist
            if (!have_reference)
            {
                ref_slope = static_cast<float>(crossing.slope);
                prev_match = crossing;

                last_accepted_us = time_us_32();
                have_reference = true;

                // Process next event (skip comparison to itself)
                continue;
            }


            /* Does the slope at this crossing resemble the reference slope? */
            // Retrieve this crossing event's slope
            const float new_slope = static_cast<float>(crossing.slope);
            // Calculate the allowed difference from the reference slope
            const float allowed_slope_diff = slope_noise_allowance + slope_relative_allowance * std::fabs(ref_slope);
            // If current slope does not resemble the reference, it is not a new period; process next event
            if (std::fabs(new_slope - ref_slope) > allowed_slope_diff)
            {
                continue;
            }

            /* Estimate candidate period using interpolation */
            // Calculate how far back true midpoint crossing is from previous crossing's positive ADC value
            const float prev_backtrack = static_cast<float>(prev_match.sample_after) / prev_match.slope;
            // Calculate how far back true midpoint crossing is from current crossing's positive ADC value
            const float current_backtrack = static_cast<float>(crossing.sample_after) / crossing.slope;
            // Calculate total elasped ADC samples as integer
            const uint32_t whole_samples = crossing.sample_count - prev_match.sample_count;
            // Backtrack from each crossing's positive ADC value to calculate precise total elapsed ADC samples
            const float candidate_period_samples = static_cast<float>(whole_samples) + prev_backtrack - current_backtrack;

            // Reject periods outside the supported frequency range
            if (candidate_period_samples < min_period_samples ||
                candidate_period_samples > max_period_samples)
            {
                continue;
            }

            /* Acquire a new period to track, or validate against an existing period */
            // Check if period estimator is currently acquiring a period to track
            if (period_state == PeriodState::Acquiring)
            {
                // Evaluate if the current candidate's period resembles existing candidates
                const bool agrees_with_candidates =
                    consistent_intervals > 0 &&
                    std::fabs(candidate_period_samples - mean_period_samples) <= period_relative_allowance * mean_period_samples;

                // If not (there are no existing candidates or the current candidate does not resemble them):
                if (!agrees_with_candidates)
                {
                    // Treat current candidate as the seed to compare later candidates to
                    mean_period_samples = candidate_period_samples;
                    consistent_intervals = 1;
                }
                else    // Otherwise, the current candidate does match existing candidates
                {
                    // Track number of consecutive similar periods
                    ++consistent_intervals;

                    // Update running mean of candidate periods
                    mean_period_samples += 
                        (candidate_period_samples - mean_period_samples) /
                        static_cast<float>(consistent_intervals);
                }
            }
            else    // Otherwise, period estimator is Tracking
            {
                // Calculate allowed difference from the tracked period
                const float allowed_period_diff = period_relative_allowance * mean_period_samples;

                // Check if current candidate period does not resemble tracked period
                if (std::fabs(candidate_period_samples - mean_period_samples) > allowed_period_diff)
                {
                    // Reject crossing as mark of new period; do not update prev_match
                    // A later crossing may form the correct full period
                    continue;
                }

                // Otherwise, current candidate period does resemble tracked period,
                // so update the estimated period gradually (helps combat noise and transients)
                mean_period_samples += period_alpha * (candidate_period_samples - mean_period_samples);
            }

            // This event was accepted as the potential start of a period so:
            prev_match = crossing;              // Track it for later period estimation
            last_accepted_us = time_us_32();    // Prevent reference slope from resetting

            // Once enough period candidates matched during acquisition, switch to tracking mode
            if (period_state == PeriodState::Acquiring)
            {
                if (consistent_intervals < acquisition_intervals)
                {
                    continue;
                }

                period_state = PeriodState::Tracking;
            }

            // Only adjust reference slope once tracking has begun (reliable period found)
            ref_slope += slope_alpha * (new_slope - ref_slope);

            // Convert the tracked, averaged period into frequency
            latest_freq_hz = sample_rate_hz / mean_period_samples;
            // Allow updated frequency estimation to be displayed
            freq_updated = true;
        }


        // Continuously drain the ADC block stats queue and store stats
        ADCBlockStats stats;
        while (try_get_adc_block_stats(stats))
        {
            latest_adc_stats = stats;
            have_adc_stats = true;
        }


        // Get current time for upkeep tasks
        const uint32_t now_us = time_us_32();

        // Reset the estimator if initialization has occurred and 
        // no period candidates matched over a certain timeframe
        if (have_reference &&
            now_us - last_accepted_us > estimator_timeout_us)
        {
            reset_estimator();
        }


        // Regularly print status updates
        if ((now_us - last_print_us) >= print_interval_us)
        {
            DEBUG_PRINT("\n");

            if (input_state == InputState::Active)
            {
                DEBUG_PRINT("State: Active\n");
            }else
            {
                DEBUG_PRINT("State: Idle\n");
            }

            if (input_state == InputState::Active)
            {
                if (period_state == PeriodState::Acquiring)
                {
                    DEBUG_PRINT("Acquiring: %u/%u consistent intervals\n", consistent_intervals, acquisition_intervals);
                }
                else if (freq_updated)
                {
                    DEBUG_PRINT("Estimated Frequency: %.3f Hz \n", latest_freq_hz);
                    DEBUG_PRINT("Mean Period: %.3f samples \n", mean_period_samples);
                    DEBUG_PRINT("Reference Slope: %.2f, Latest Slope: %d \n", ref_slope, debug_crossing.slope);
                    freq_updated = false;
                }
            }

            if (have_adc_stats)
            {
                DEBUG_PRINT("ADC: P2P=%u, mean=%.2f, min=%d, max=%d \n",
                latest_adc_stats.p2p,
                latest_adc_stats.mean,
                latest_adc_stats.min,
                latest_adc_stats.max);

                DEBUG_PRINT("Midpoint: %d \n", latest_adc_stats.midpoint);
                // DEBUG_PRINT("ADC Errors: %d \n", latest_adc_stats.adc_errors);
                // DEBUG_PRINT("Queue Errors: %d \n", latest_adc_stats.queue_errors);
            }

            last_print_us = now_us;
        }

    }
}
