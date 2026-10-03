#pragma once

/*
 * Data structure to hold a "timestamp" of a midpoint crossing and the slope at this point
 */
struct CrossingEvent 
{
    uint32_t sample_count;  // The number of ADC conversions performed before this sample
    int32_t slope;
};

// ADC target sampling rate
constexpr float sample_rate_hz = 16'000.0f;


void initialize_guitar_input();

bool try_get_crossing_event(CrossingEvent& event);
