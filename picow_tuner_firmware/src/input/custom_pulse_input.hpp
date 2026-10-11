#pragma once

#include "waveform_event.hpp"

// ADC sampling rate
constexpr float sample_rate_hz = 256'000.0f;    // TODO: remove?

// Possible types of events from input handler
enum class InputEventType
{
    Crossing,
    StateChanged
};

// Possible states of the tuner pedal's input
enum class InputState
{
    Idle,
    Active
};

// Data structure to hold input handler events
struct InputEvent
{
    InputEventType type;
    InputState state;
    WaveformEvent crossing{}; // Only used for Crossing message types
};

// Data structure to hold accumulated statistics gathered across a block of ADC samples
struct ADCBlockStats
{
    uint16_t min;   // Minimum measured ADC sample taken this block
    uint16_t max;   // Maximum measured ADC sample taken this block
    uint32_t sum;   // Sum of all ADC samples taken this block
    uint32_t samples;   // Number of ADC samples taken this block
    unsigned p2p;
    float mean;
    int16_t midpoint;
    int32_t adc_errors;
    int32_t waveform_queue_errors;
    int32_t state_change_queue_errors;
    int32_t stats_queue_errors;
};

void initialize_pulse_input();

bool try_get_input_event(InputEvent& event);

bool try_get_adc_block_stats(ADCBlockStats& stats);
