#pragma once

#include "tuner_settings.hpp"

namespace settings_storage
{
    enum class SaveResult
    {
        Unchanged,
        Saved,
        Failed
    };

    // Load the newest valid record, or restore defaults and return false
    bool load(TunerSettings& settings);

    // Call from core 0 with acquisition stopped
    SaveResult save(const TunerSettings& settings);
}