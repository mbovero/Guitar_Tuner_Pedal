#pragma once

// Possible states of the tuner feedback
enum class TuningStatus
{
    NoEstimate,
    Flat,
    InTune,
    Sharp
};

// Config for standard pitch frequency and "In Tune" range
struct NoteConfig
{
    float a4_frequency_hz = 440.0f; // Standard pitch; uses modern standard pitch of 440 Hz by default
    float in_tune_cents = 3.0f;     // Input signal is considered in tune if it's within +-in_tune_cents from target pitch
};

// Data structure containing detailed tuning information and feedback on the evaluated input signal
struct NoteResult
{
    bool valid = false;             // Whether the given frequency can be interpreted
    const char* note_name = "";     // Name of the target note (C, C#, D, etc.)
    int octave = 0;                 // Indicated octave of the note (A0, A1, A2, etc.)
    int midi_note = -1;             // MIDI number representing the target note

    float measured_freq_hz = 0.0f;  // Frequency of the input signal that was evaluated
    float target_freq_hz = 0.0f;    // Frequency of the target note
    float cents_error = 0.0f;       // Difference between measured and target frequency in cents

    TuningStatus status = TuningStatus::NoEstimate; // State of the tuner feedback
};

NoteResult analyze_frequency(float freq_hz, const NoteConfig& config = {});

const char* tuning_status_text(TuningStatus status);