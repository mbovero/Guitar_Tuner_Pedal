#include <stdio.h>
#include "pico/stdlib.h"
#include "guitar_input.hpp"
#include "crossing_history.hpp"
#include "debug.hpp"
#include <cstdlib>
#include <cmath>
#include "note_tracker.hpp"

// State of the period estimator
enum class EstimatorState
{
    Acquiring,  // Considering candidate periods before committing to one
    Tracking    // Actively tracking a calculated period and improving estimates
};

int main()
{
    stdio_init_all();
    // Set up the ADC to intake the guitar signal and queue midpoint crossing events
    initialize_guitar_input();

    // The most recent estimated frequency
    float latest_freq_hz = 0.0f;
    // Supported frequency range in Hz
    constexpr float min_freq_hz = 40.0f;
    constexpr float max_freq_hz = 1500.0f;
    // Supported frequency range in terms of ADC samples
    constexpr float min_period_samples = sample_rate_hz / max_freq_hz;
    constexpr float max_period_samples = sample_rate_hz / min_freq_hz;

    // Reference slope used to identify beginning/end of waveform period
    float ref_slope = 0.0f;
    // Whether a valid reference slope has been set; helps initialize/reset ref_slope
    bool have_reference = false;
    // How much a compared slope can vary from reference slope and still be considered matching
    constexpr float slope_noise_allowance = 10.0f;
    constexpr float slope_relative_allowance = 0.1f;
    // Smoothing factor applied to accepted measurements to update slope estimate
    constexpr float slope_alpha = 0.1f;

    // Reference pulse height to identify beginning/end of waveform period
    float ref_height = 0.0f;
    // How much a compared pulse height can vary from reference height and still be considered matching
    constexpr float height_noise_allowance = 20.0f; // Measured in ADC values
    constexpr float height_relative_allowance = 0.35f;
    // Reference pulse width to identify beginning/end of waveform period
    float ref_width = 0.0f;
    // How much a compared pulse width can vary from reference width and still be considered matching
    constexpr float width_noise_allowance = 2.0f;   // Measured in ADC samples
    constexpr float width_relative_allowance = 0.2f;
    // Smoothing factor applied to accepted measurements to update pulse height or width estimate
    constexpr float pulse_feature_alpha = 0.2f;

    // Debug toggles for slope matching and/or pulse matching
    constexpr bool use_pulse_matching = true;
    constexpr bool use_slope_matching = true;

    // The latest accepted crossing event (used to estimate period)
    CrossingEvent prev_match{};

    // Running average of acquired/tracked periods in terms of # of ADC samples
    float mean_period_samples = 0.0f;
    // Tolerance allowed for matching candidate periods
    constexpr float period_relative_allowance = 0.08f;
    // Smoothing factor applied to accepted measurements to update period estimate
    constexpr float period_alpha = 0.2f;

    // Current input handler state
    InputState input_state = InputState::Idle;
    // Current state of the estimator (either Acquiring or Tracking)
    EstimatorState estimator_state = EstimatorState::Acquiring;

    // Number of times consecutive period candidates matched (used to transition from Acquiring to Tracking)
    unsigned consistent_intervals = 0;
    // Number of consistent intervals required before starting period tracking
    constexpr unsigned acquisition_intervals = 3;

    // The last time a period candidate matched (used to occasionally reset reference slope)
    uint32_t last_accepted_us = time_us_32();
    // Default configuration allows four periods at lowest supported frequency
    // Ex: Four 40 Hz periods is 0.1 seconds or 100,000 us
    // Length of time with no period candidate matches before reference slope is reset
    constexpr uint32_t estimator_timeout_us = static_cast<uint32_t>((8.0f / min_freq_hz) * 1'000'000.0f);

    // Variables for regularly printing status updates
    uint32_t last_print_us = time_us_32();
    constexpr uint32_t print_interval_us = 500'000; // 2 Hz
    CrossingEvent debug_crossing{0,0};
    bool freq_updated = false;

    // Latest statistics from the ADC ISR
    ADCBlockStats latest_adc_stats{};
    // Flag to initialize latest ADC statistics struct
    bool have_adc_stats = false;

    // Create tuning feedback config
    const NoteConfig note_config {
        440.0f, // Modern standard pitch (A4 = 440 Hz)
        3.0f    // +/- range from target pitch that is considered "In Tune"
    };
    // Most recent tuning feedback
    NoteResult latest_note{};

    // Lambda function that resets local estimator variables by reference
    auto reset_estimator = [&]()
    {
        estimator_state = EstimatorState::Acquiring;

        have_reference = false;
        ref_slope = 0.0f;
        ref_height = 0.0f;
        ref_width = 0.0f;
        prev_match = {};

        consistent_intervals = 0;
        mean_period_samples = 0.0f;

        latest_freq_hz = 0.0f;
        freq_updated = false;

        latest_note = {};
    };

    /* Main while loop */
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
                // Process next event (no crossing event to handle)
                continue;
            }

            // If the input is not active, don't process crossing events
            if (input_state != InputState::Active)
            {
                continue;
            }


            // Otherwise, handle event as a crossing event
            const CrossingEvent& crossing = event.crossing;
            // Copy of the crossing event for debug prints
            debug_crossing = crossing;
            // Extract event information
            const float new_slope = static_cast<float>(crossing.slope);
            const float new_height = static_cast<float>(crossing.pulse_height);
            const float new_width = static_cast<float>(crossing.pulse_width);

            // Only process complete, valid positive-pulse records
            if (crossing.slope <= 0 ||
                crossing.pulse_height == 0 ||
                crossing.pulse_width == 0)
            {
                continue;
            }

            // Set a reference slope if a valid one does not exist
            if (!have_reference)
            {
                ref_slope = new_slope;
                ref_height = new_height;
                ref_width = new_width;
                prev_match = crossing;

                last_accepted_us = time_us_32();
                have_reference = true;

                // Process next event (skip comparison to itself)
                continue;
            }


            /* Does current slope and/or pulse resemble previous ones? */
            // Calculate the allowed differences
            const float allowed_slope_diff = slope_noise_allowance + slope_relative_allowance * std::fabs(ref_slope);
            const float allowed_height_diff = height_noise_allowance + height_relative_allowance * ref_height;
            const float allowed_width_diff = width_noise_allowance + width_relative_allowance * ref_width;
            // Determine which features match
            const bool slope_matches = std::fabs(new_slope - ref_slope) <= allowed_slope_diff;
            const bool height_matches = std::fabs(new_height - ref_height) <= allowed_height_diff;
            const bool width_matches = std::fabs(new_width - ref_width) <= allowed_width_diff;

            // Togglable slope or pulse comparisons implemented here
            // If current slope does not resemble the reference, it is not a new period; process next event
            if (use_slope_matching && !slope_matches)
            {
                continue;
            }
            // If current pulse does not resembled the reference, it is not a new period; process next event
            if (use_pulse_matching &&
                !height_matches || !width_matches)
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

            // Always reject non-positive periods
            if (candidate_period_samples <= 0.0f)
            {
                continue;
            }

            // Reject periods outside the supported frequency range during Acquisition
            // During tracking, allow more periods through for potential recovery
            if (estimator_state == EstimatorState::Acquiring &&
                (candidate_period_samples < min_period_samples ||
                candidate_period_samples > max_period_samples))
            {
                continue;
            }

            /* Acquire a new period to track, or validate against an existing period */
            // Check if period estimator is currently acquiring a period to track
            if (estimator_state == EstimatorState::Acquiring)
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
                    /* Attempt recovery of period tracking */
                    // Estimate how many cycles this candidate period spans
                    const float cycles = std::round(candidate_period_samples / mean_period_samples);

                    // Check if the candidate period seems to be a multiple of the tracked period (2x or 3x)
                    if (cycles >= 2.0f &&
                        cycles <= 3.0f &&
                        std::fabs(candidate_period_samples - cycles * mean_period_samples) <= allowed_period_diff)
                    {
                        // Reset previous match marker to allow for potential recovery of period tracking
                        prev_match = crossing;

                        // Do not update the following due to unreliable data:
                        // - last_accepted_us
                        // - mean_period_samples
                        // - ref_slope
                        // - latest_freq_hz
                        // - freq_updated
                    }

                    // Reject crossing as mark of new period; do not update prev_match
                    // A later crossing may form the correct full period, or timeout will eventually occur
                    continue;
                }

                // Otherwise, current candidate period accurately resembles tracked period
                // Keep tracked period inside allowed frequency range
                if (candidate_period_samples < min_period_samples ||
                    candidate_period_samples > max_period_samples)
                {
                    continue;
                }
                // And update the estimated period gradually (helps combat noise and transients)
                mean_period_samples += period_alpha * (candidate_period_samples - mean_period_samples);
            }

            // This event was accepted as the start of a period so:
            prev_match = crossing;              // Track it for later period estimation
            last_accepted_us = time_us_32();    // Prevent reference slope from resetting

            // Once enough period candidates matched during acquisition, switch to tracking mode
            if (estimator_state == EstimatorState::Acquiring)
            {
                if (consistent_intervals < acquisition_intervals)
                {
                    continue;
                }

                estimator_state = EstimatorState::Tracking;
            }

            // Only adjust references once tracking has begun (a reliable period was found)
            ref_slope += slope_alpha * (new_slope - ref_slope);
            ref_height += pulse_feature_alpha * (new_height - ref_height);
            ref_width += pulse_feature_alpha * (new_width - ref_width);

            // Convert the tracked, averaged period into frequency
            latest_freq_hz = sample_rate_hz / mean_period_samples;
            // Get tuning feedback based on latest frequency
            latest_note = analyze_frequency(latest_freq_hz, note_config);
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
                if (estimator_state == EstimatorState::Acquiring)
                {
                    DEBUG_PRINT("Acquiring: %u/%u consistent intervals\n", consistent_intervals, acquisition_intervals);
                }
                else if (freq_updated)
                {
                    // Tuning feedback
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

                    // Detailed period estimation statistics
                    // DEBUG_PRINT("Estimated Frequency: %.3f Hz \n", latest_freq_hz);
                    // DEBUG_PRINT("Mean Period: %.3f samples \n", mean_period_samples);
                    // DEBUG_PRINT("Reference Slope: %.2f, Latest Slope: %d \n", ref_slope, debug_crossing.slope);
                    // DEBUG_PRINT("Reference Height: %.2f, Latest Height: %u \n", ref_height, debug_crossing.pulse_height);
                    // DEBUG_PRINT("Reference Width: %.3f ms, Latest Width: %.3f ms \n",
                    //     1000.0f * ref_width / sample_rate_hz,
                    //     1000.0f * debug_crossing.pulse_width / sample_rate_hz);

                    freq_updated = false;
                }
            }

            if (have_adc_stats)
            {
                // DEBUG_PRINT("ADC: P2P=%u, mean=%.2f, min=%d, max=%d \n",
                // latest_adc_stats.p2p,
                // latest_adc_stats.mean,
                // latest_adc_stats.min,
                // latest_adc_stats.max);

                // DEBUG_PRINT("Midpoint: %d \n", latest_adc_stats.midpoint);
                // DEBUG_PRINT("ADC Errors: %d \n", latest_adc_stats.adc_errors);
                // DEBUG_PRINT("Queue Errors: %d \n", latest_adc_stats.queue_errors);
            }

            last_print_us = now_us;
        }

    }
}
