#include "simulation_event_queue.h"

#include <utility>

/**
 * @file simulation_event_queue.cpp
 * @ingroup sim_internal
 * @brief Deterministic event queue implementation for simulation runtime.
 */

bool SimulationEventQueue::EventCompare::operator()(const SimEvent& lhs, const SimEvent& rhs) const
{
    if (lhs.time_us != rhs.time_us) {
        return lhs.time_us > rhs.time_us;
    }

    if (lhs.priority != rhs.priority) {
        return lhs.priority > rhs.priority;
    }

    return lhs.sequence_id > rhs.sequence_id;
}

void SimulationEventQueue::push(const SimEvent& event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(event);
}

void SimulationEventQueue::push(SimEvent&& event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(event));
}

bool SimulationEventQueue::popDue(uint64_t horizon_us, SimEvent& out_event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return false;
    }

    const SimEvent& next = queue_.top();
    if (next.time_us > horizon_us) {
        return false;
    }

    out_event = next;
    queue_.pop();
    return true;
}

size_t SimulationEventQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void SimulationEventQueue::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_ = std::priority_queue<SimEvent, std::vector<SimEvent>, EventCompare>{};
}
