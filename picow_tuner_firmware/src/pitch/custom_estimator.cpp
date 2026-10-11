#include "custom_estimator.hpp"

#include <cmath>

CustomEstimator::CustomEstimator(float sampling_rate)
    : sample_rate_hz(sampling_rate),    // ADC sampling rate
      // Supported frequency range in terms of ADC samples
      min_period_samples(sample_rate_hz / max_freq_hz),
      max_period_samples(sample_rate_hz / min_freq_hz)
{
}

// Resets estimator state variables for clean frequency estimation
void CustomEstimator::reset()
{
    estimator_state = CustomEstimatorState::Acquiring;

    have_reference = false;
    ref_slope = 0.0f;
    ref_height = 0.0f;
    ref_width = 0.0f;

    prev_match = {};

    consistent_intervals = 0;
    mean_period_samples = 0.0f;

    latest_result = {};

}

/*
 * An attempt at a custom approach to frequency estimation. This estimator intakes waveform
 * features (e.g. midpoint crossings and pulse width/height) and uses them to identify 
 * signal periodicity. Once the estimator has acquired a few consistent periods, it begins
 * tracking that frequency, gradually improving its output until the input signal changes.
 * significantly. The period acquisition process then starts again. Rinse and repeat.
 */
bool CustomEstimator::process(const WaveformEvent& crossing, uint32_t now_us)
{
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
        return false;
    }

    // Set a reference slope if a valid one does not exist
    if (!have_reference)
    {
        ref_slope = new_slope;
        ref_height = new_height;
        ref_width = new_width;
        prev_match = crossing;

        last_accepted_us = now_us;
        have_reference = true;

        // Process next event (skip comparison to itself)
        return false;
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
        return false;
    }
    // If current pulse does not resembled the reference, it is not a new period; process next event
    if (use_pulse_matching &&
        (!height_matches || !width_matches))
    {
        return false;
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
        return false;
    }

    // Reject periods outside the supported frequency range during Acquisition
    // During tracking, allow more periods through for potential recovery
    if (estimator_state == CustomEstimatorState::Acquiring &&
        (candidate_period_samples < min_period_samples ||
        candidate_period_samples > max_period_samples))
    {
        return false;
    }

    /* Acquire a new period to track, or validate against an existing period */
    // Check if period estimator is currently acquiring a period to track
    if (estimator_state == CustomEstimatorState::Acquiring)
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
            return false;
        }

        // Otherwise, current candidate period accurately resembles tracked period
        // Keep tracked period inside allowed frequency range
        if (candidate_period_samples < min_period_samples ||
            candidate_period_samples > max_period_samples)
        {
            return false;
        }
        // And update the estimated period gradually (helps combat noise and transients)
        mean_period_samples += period_alpha * (candidate_period_samples - mean_period_samples);
    }

    // This event was accepted as the start of a period so:
    prev_match = crossing;              // Track it for later period estimation
    last_accepted_us = now_us;    // Prevent reference slope from resetting

    // Once enough period candidates matched during acquisition, switch to tracking mode
    if (estimator_state == CustomEstimatorState::Acquiring)
    {
        if (consistent_intervals < acquisition_intervals)
        {
            return false;
        }

        estimator_state = CustomEstimatorState::Tracking;
    }

    // Only adjust references once tracking has begun (a reliable period was found)
    ref_slope += slope_alpha * (new_slope - ref_slope);
    ref_height += pulse_feature_alpha * (new_height - ref_height);
    ref_width += pulse_feature_alpha * (new_width - ref_width);

    // Calculate average period from frequency and store in result struct
    latest_result = {
        true,
        sample_rate_hz / mean_period_samples
    };
    
    return true;
}

bool CustomEstimator::update(uint32_t now_us)
{
    // Reset the estimator if initialization has occurred and 
    // no period candidates matched over a certain timeframe
    if (have_reference &&
        now_us - last_accepted_us > estimator_timeout_us)
    {
        reset();
        return true;
    }
    // Otherwise estimator not reset
    return false;
}