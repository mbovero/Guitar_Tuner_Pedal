#pragma once
#include <cstdint>

/* 
This file contains RUNTIME settings that can be changed through the tuner's
interactive menu. tuner_config.hpp holds hardware assignments, sample rate,
etc.
*/

enum class FilterMode
{
    Off,
    Hz50,
    Hz60
};

namespace ReferencePitchLimits
{
    inline constexpr uint16_t min_hz = 400;
    inline constexpr uint16_t max_hz = 480;
}

// Default settings
struct TunerSettings
{
    FilterMode filter_mode = FilterMode::Hz60;
    uint16_t a4_frequency_hz = 440;
};
