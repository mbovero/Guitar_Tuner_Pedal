#pragma once

#include <cstdint>

#include "input/waveform_event.hpp"
#include "pitch_result.hpp"

// State of the pitch estimator
enum class CustomEstimatorState
{
    Acquiring,  // Considering candidate periods before committing to one
    Tracking    // Actively tracking a calculated period and improving estimates
};

class CustomEstimator
{
public:
    // Stores estimator's ADC sampling rate and derives period limits
    explicit CustomEstimator(float sampling_rate);

    // Clear the estimator's tracking state and result
    void reset();

    // Processes a single waveform pulse; returns true if it yields a new frequency estimate (false otherwise)
    bool process(const WaveformEvent& crossing, uint32_t now_us);

    // Compares estimator timeout against provided timestamp; returns true if the estimator was reset (false otherwise)
    bool update(uint32_t now_us);

    // Returns currently held frequency estimate
    PitchResult result() const
    {
        return latest_result;
    }

    // Returns the current state of the pitch estimator
    CustomEstimatorState state() const
    {
        return estimator_state;
    }

    // Returns the number of times consecutive period candidates matched
    unsigned interval_count() const
    {
        return consistent_intervals;
    }

    // Returns the number of consistent intervals required before starting period tracking
    unsigned required_intervals() const
    {
        return acquisition_intervals;
    }

    // Returns the running average of acquired/tracked periods in terms of # of ADC samples
    float mean_period() const
    {
        return mean_period_samples;
    }

    // Returns the reference slope used to identify the beginning/end of a waveform period
    float reference_slope() const
    {
        return ref_slope;
    }

    // Returns the pulse height used to identify the beggining/end of a waveform period
    float reference_height() const
    {
        return ref_height;
    }

    // Returns the pulse width used to identify the beggining/end of a waveform period
    float reference_width() const
    {
        return ref_width;
    }

    // Returns the latest WaveformEvent (used to print status updates)
    WaveformEvent latest_pulse() const
    {
        return debug_crossing;
    }

private:
    /* Settings */
    // Supported frequency range in Hz
    static constexpr float min_freq_hz = 40.0f;
    static constexpr float max_freq_hz = 1500.0f;

    // How much a compared slope can vary from reference slope and still be considered matching
    static constexpr float slope_noise_allowance = 5.0f;
    static constexpr float slope_relative_allowance = 0.1f;
    // Smoothing factor applied to accepted measurements to update slope estimate
    static constexpr float slope_alpha = 0.1f;

    // How much a compared pulse height can vary from reference height and still be considered matching
    static constexpr float height_noise_allowance = 10.0f; // Measured in ADC values
    static constexpr float height_relative_allowance = 0.25f;

    // How much a compared pulse width can vary from reference width and still be considered matching
    static constexpr float width_noise_allowance = 4.0f;   // Measured in ADC samples
    static constexpr float width_relative_allowance = 0.1f;

    // Smoothing factor applied to accepted measurements to update pulse height or width estimate
    static constexpr float pulse_feature_alpha = 0.1f;

    // Debug toggles for enabling slope matching and/or pulse matching
    static constexpr bool use_pulse_matching = true;
    static constexpr bool use_slope_matching = true;

    // Tolerance allowed for matching candidate periods
    static constexpr float period_relative_allowance = 0.01f;
    // Smoothing factor applied to accepted measurements to update period estimate
    static constexpr float period_alpha = 0.1f;

    // Number of consistent intervals required before starting period tracking
    static constexpr unsigned acquisition_intervals = 3;

    // Default configuration allows four periods at lowest supported frequency
    // Ex: Eight 40 Hz periods is 0.2 seconds or 200,000 us
    // Length of time with no period candidate matches before reference slope is reset
    static constexpr uint32_t estimator_timeout_us = static_cast<uint32_t>((8.0f / min_freq_hz) * 1'000'000.0f);


    /* Instance State */
    // ADC sampling rate
    float sample_rate_hz;
    // Supported frequency range in terms of ADC samples
    float min_period_samples;
    float max_period_samples;

    // Reference slope used to identify beginning/end of waveform period
    float ref_slope = 0.0f;
    // Reference pulse height to identify beginning/end of waveform period
    float ref_height = 0.0f;
    // Reference pulse width to identify beginning/end of waveform period
    float ref_width = 0.0f;

    // Whether valid references have been set; helps initialize/reset references
    bool have_reference = false;

    // The latest accepted waveform event (used to estimate period)
    WaveformEvent prev_match{};
    // Copy of the latest waveform for debug prints
    WaveformEvent debug_crossing{};

    // Running average of acquired/tracked periods in terms of # of ADC samples
    float mean_period_samples = 0.0f;

    // Current state of the estimator (in acquiring state by default)
    CustomEstimatorState estimator_state = CustomEstimatorState::Acquiring;

    // Number of times consecutive period candidates matched (used to transition from Acquiring to Tracking)
    unsigned consistent_intervals = 0;

    // The last time a period candidate matched (used to reset reference slope with timeout)
    uint32_t last_accepted_us = 0;

    // The most recent frequency estimation and its validity
    PitchResult latest_result{};
};