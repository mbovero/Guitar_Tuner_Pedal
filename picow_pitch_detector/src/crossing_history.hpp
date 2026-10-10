#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include "adc_input_handler.hpp"

/*
 * A circular buffer to hold midpoint crossing events. Once the buffer reaches its maximum
 * capacity, pushing a new entry will replace the oldest entry.
 */
class CrossingHistory
{
public:
    // Maximum number of midpoint crossing events
    static constexpr size_t capacity = 64;

    void push(const WaveformEvent& event);

    size_t size() const;

    bool try_get(size_t i, WaveformEvent& event) const;

    void clear();

private:
    // Array of midpoint crossing events with a maximum capacity
    std::array<WaveformEvent, capacity> events_{};
    // Index where the next event will be pushed to
    size_t next_ = 0;
    // Number of events recorded in this circular buffer
    size_t count_ = 0;
};