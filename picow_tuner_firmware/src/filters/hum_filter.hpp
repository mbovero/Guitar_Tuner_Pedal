#pragma once

#include <cstddef>
#include <cstdint>

#include "comb_filter.hpp"
#include "tuner_config.hpp"
#include "tuner_settings.hpp"

// Contains statistics on a frame of ADC samples filtered through a Hum Comb Filter
struct FilteredFrameStats
{
    // Whether filtering produced valid samples
    bool valid = false;

    // pk2pk of the filtered sample frame
    uint32_t peak_to_peak_adc = 0;
    // Root Mean Squre of the filtered sample frame
    float rms_adc = 0.0f;
};

class HumFilter
{
public:
    // Configure both comb filters using the fixed sampling configuration
    bool configure();

    // Select a mode and discard its previous history
    void set_mode(FilterMode mode);

    // Whether the selected mode requires filtering
    bool enabled() const;

    // Reset active filter history
    void reset(double initial_input = 0.0);

    // Process on frame using the selected comb
    FilteredFrameStats filter_frame(
        const int16_t* input,
        int16_t* output,
        size_t sample_count);

private:
    CombFilter<HumFilterConfig::delay_50_samples> filter_50_hz{};
    CombFilter<HumFilterConfig::delay_60_samples> filter_60_hz{};

    // Current filter mode (60 Hz filter by default)
    FilterMode mode_ = FilterMode::Hz60;
};

