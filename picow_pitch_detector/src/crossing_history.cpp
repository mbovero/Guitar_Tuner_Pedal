#include "crossing_history.hpp"


/*
 * Adds the provided event to the midpoint crossing event ring buffer.
 * If the buffer is at maximum capacity, a new push replaces the oldest event.
 */
void CrossingHistory::push(const WaveformEvent& event)
{
    // Store the pushed event in the next slot in the ring buffer
    events_[next_] = event;

    // Increment the write position, wrapping around at the end of the array
    next_ = (next_ + 1) % capacity;

    // Increment the count (unless at max capacity)
    if (count_ < capacity)
    {
        ++count_;
    }
}


/*
 * Returns the number of stored events in the midpoint crossing history ring buffer
 */
size_t CrossingHistory::size() const
{
    return count_;
}


/*
 * Attempts to retrieve the event at the specified index. Index order is as follows:
 * Index 0 is the oldest element
 * Index size() - 1 is the newest element
 * 
 * If the provided index is in range, the event at that index is copied into the given 
 * location and the function returns true. Otherwise, the function returns false.
 */
bool CrossingHistory::try_get(size_t i, WaveformEvent& event) const
{
    // Ensure requested index is inside existing range
    if (i >= count_)
    {
        return false;
    }

    // At max capacity, the oldest element is at index of next_
    // Under max capacity, oldest element is count_ indices behind next_
    const size_t oldest_index = (next_ + capacity - count_) % capacity;

    // Position of requested element can be found relative to the oldest element
    const size_t result_index = (oldest_index + i) % capacity;

    event = events_[result_index];
    return true;
}


/*
 * Resets the midpoint crossing ring buffer to size 0.
 */
void CrossingHistory::clear()
{
    // Setting the following members to 0 results in the ring buffer being perceived 
    // as empty without actually having to clear all the values. Old values are simply
    // overwritten when new pushes to the buffer occur.
    next_ = 0;
    count_ = 0;
}