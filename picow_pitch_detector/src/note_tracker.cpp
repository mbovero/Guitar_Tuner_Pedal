#include "note_tracker.hpp"
#include <cmath>

/*
 * Takes the provided frequency, determines the closest pitch, and calculates the error between the two in cents.
 * Stores tuning feedback alongside other detailed information in NoteResult struct and returns it.
 * Optional config parameter allows for configuration of standard pitch and "In Tune" range.
 */
NoteResult analyze_frequency(float freq_hz, const NoteConfig& config)
{
    // Default result
    NoteResult result{};

    // Return default result if provided frequency or config is invalid
    if (!std::isfinite(freq_hz) || freq_hz <= 0.0f ||
        !std::isfinite(config.a4_frequency_hz) || config.a4_frequency_hz <= 0.0f ||
        !std::isfinite(config.in_tune_cents) || config.in_tune_cents <= 0.0f)
    {
        return result;
    }

    // Convert input frequency to MIDI number representation
    const float note_position = 69.0f + 12.0f * std::log2(freq_hz / config.a4_frequency_hz);
    // Return default result if MIDI representation is invalid 
    if (!std::isfinite(note_position) ||    
        note_position <= -0.5f ||           // Allowed MIDI range here is 0 to 127
        note_position >= 127.5f)            // But note position is still valid if it rounds into this range
    {
        return result;
    }

    // Round MIDI float representation to nearest integer to get target pitch
    const int nearest_note = static_cast<int>(std::round(note_position));
    // Calculate target frequency using target pitch (inverse of conversion to MIDI)
    const float target_hz = config.a4_frequency_hz * std::exp2(static_cast<float>(nearest_note - 69) / 12.0f);
    // Return default result if calculated target is invalid
    if (!std::isfinite(target_hz) || target_hz <= 0.0f)
    {
        return result;
    }

    // Calculate error between measured and target frequency in cents
    const float cents = 1200.0f * std::log2(freq_hz / target_hz);
    // Return default result if cents calculation is invalid
    if (!std::isfinite(cents))
    {
        return result;
    }

    // List of all possible note names sorted in 0 indexed MIDI representation order
    static constexpr const char* note_names[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    /* Format valid result to return */
    // All checks were passed, and values were successfully calculated so result is valid
    result.valid = true;
    // MIDI representation mod 12 give index 0 --> C, index 5 --> F, etc. for all 12 notes at any octave
    result.note_name = note_names[nearest_note % 12];
    // There are 12 notes in an octave. A4: MIDI = 69, 69/12 = 5 with integer division, subtract 1 gets correct octave
    result.octave = (nearest_note / 12) - 1;
    // Store MIDI representation of the target/nearest note
    result.midi_note = nearest_note;
    // Store processed frequency
    result.measured_freq_hz = freq_hz;
    // Store calculated target frequency
    result.target_freq_hz = target_hz;
    // Store error calculated in cents
    result.cents_error = cents;

    // Determine tuning status
    // If error is within tolerance, measured frequency is considered in tune
    if (std::fabs(cents) <= config.in_tune_cents)
    {
        result.status = TuningStatus::InTune;
    }
    else if (cents < 0.0f) // If error is negative, note is flat
    {
        result.status = TuningStatus::Flat;
    }
    else    // Otherwise, error is positive, so note is sharp
    {
        result.status = TuningStatus::Sharp;
    }

    return result;
}

/*
 * Takes a TuningStatus enum and returns a string with tuning instructions.
 */
const char* tuning_status_text(TuningStatus status)
{
    // Convert status to associated string
    switch (status)
    {
        case TuningStatus::InTune:
            return "In tune";
        case TuningStatus::Flat:
            return "Flat - raise pitch";
        case TuningStatus::Sharp:
            return "Sharp - lower pitch";
        case TuningStatus::NoEstimate:
            return "No valid estimate";
    }

    // Return invalid estimate string by default
    return "No Valid estimate";
}