#pragma once

#include <cstdint>

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
