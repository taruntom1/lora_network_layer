#include "simulation_clock.h"

/**
 * @file simulation_clock.cpp
 * @ingroup sim_internal
 * @brief Atomic virtual clock primitive used by scenario stepping.
 */

void SimulationClock::reset(uint64_t now_ms)
{
    now_ms_.store(now_ms);
}

void SimulationClock::step(uint64_t delta_ms)
{
    now_ms_.fetch_add(delta_ms);
}

uint64_t SimulationClock::nowMs() const
{
    return now_ms_.load();
}

uint32_t SimulationClock::nowSeconds() const
{
    return static_cast<uint32_t>(now_ms_.load() / 1000ULL);
}
