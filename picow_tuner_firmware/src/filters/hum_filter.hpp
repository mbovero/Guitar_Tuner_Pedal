#pragma once

#include <cstddef>
#include <cstdint>

#include "comb_filter.hpp"
#include "tuner_config.hpp"

// Notch comb filter to handle 60 Hz noise and harmonics
using HumCombFilter = CombFilter<HumFilterConfig::delay_samples>;   

// Contains statistics on a frame of ADC samples filtered through a HumCombFilter
struct FilteredFrameStats
{
    // Whether filtering produced valid samples
    bool valid = false;

    // pk2pk of the filtered sample frame
    uint32_t peak_to_peak_adc = 0;
    // Root Mean Squre of the filtered sample frame
    float rms_adc = 0.0f;
};

FilteredFrameStats filter_frame(
    HumCombFilter& filter,
    const int16_t* input,
    int16_t* output,
    size_t sample_count);

