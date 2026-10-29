#pragma once

#include "tuning/tuning_feedback.hpp"

namespace tuner_display
{
    void init();
    void show();
    void update(const TuningFeedback& feedback, bool clipping);
}