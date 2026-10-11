#pragma once

#include <cstdint>

// ADC target sampling rate
constexpr float sample_rate_hz = 256'000.0f;

// Possible types of events from input handler
enum class InputEventType
{
    Crossing,
    StateChanged
};

/*
 * Data structure to hold a "timestamp" of a rising midpoint crossing, the slope at this point, 
 * and associated pulse data
 */
struct WaveformEvent 
{
    uint32_t sample_count;  // The number of ADC conversions performed before this rising crossing
    int32_t slope;          // Note: Should be positive for proper program execution
    int16_t sample_after;   // The centered ADC sample right after this rising crossing; used for interpolation

    uint16_t pulse_height;  // Height of peak above midpoint measured in ADC values
    uint32_t pulse_width;   // Rising to falling width of the pulse measured in # of ADC samples
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

void initialize_guitar_input();

bool try_get_input_event(InputEvent& event);

bool try_get_adc_block_stats(ADCBlockStats& stats);
