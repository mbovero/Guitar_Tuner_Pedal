#pragma once

#include "crossing_history.hpp"
#include <cstdint>

// ADC target sampling rate
constexpr float sample_rate_hz = 128'000.0f;

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
    CrossingEvent crossing{}; // Only used for Crossing message types
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
    int32_t queue_errors;
};

void initialize_guitar_input();

bool try_get_input_event(InputEvent& event);

bool try_get_adc_block_stats(ADCBlockStats& stats);
