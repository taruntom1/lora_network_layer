#pragma once

#include <atomic>
#include <cstdint>

/**
 * @file simulation_clock.h
 * @ingroup sim_runtime
 * @brief Deterministic virtual clock used by simulation stepping.
 */

/**
 * @ingroup sim_api
 * @brief Monotonic simulation time source in milliseconds.
 *
 * The clock is explicitly advanced by test code or demo loops using @ref step,
 * enabling deterministic replay and eliminating host wall-time jitter from
 * scenario progression.
 */
class SimulationClock {
public:
    SimulationClock() = default;

    /**
     * @brief Set the current simulation time.
     * @param now_ms Absolute virtual time in milliseconds.
     */
    void reset(uint64_t now_ms = 0);

    /**
     * @brief Advance virtual time by a delta.
     * @param delta_ms Milliseconds to add.
     */
    void step(uint64_t delta_ms);

    /** @brief Current virtual time in milliseconds. */
    uint64_t nowMs() const;

    /** @brief Current virtual time in whole seconds. */
    uint32_t nowSeconds() const;

private:
    std::atomic<uint64_t> now_ms_{0};
};
