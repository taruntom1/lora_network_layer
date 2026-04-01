#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "network_manager.h"
#include "simulation_builder.h"

/**
 * @file simulation_test_base.h
 * @ingroup sim_test
 * @brief Reusable simulation test harness utilities for phase test binaries.
 */

/**
 * @ingroup sim_test
 * @brief Captured application-level delivery event for assertions.
 */
struct CapturedMessage {
    /** Delivered network header. */
    NetworkHeader header{};
    /** Delivered application payload bytes. */
    std::vector<uint8_t> payload;
};

/**
 * @ingroup sim_test
 * @brief Helper wrapper for scenario lifecycle, traffic injection, and capture assertions.
 */
class SimulationTestBase {
public:
    /**
     * @brief Construct helper around an existing scenario.
     * @throws std::runtime_error If @p scenario is null.
     */
    explicit SimulationTestBase(std::unique_ptr<SimulationScenario> scenario);
    ~SimulationTestBase();

    SimulationTestBase(const SimulationTestBase&) = delete;
    SimulationTestBase& operator=(const SimulationTestBase&) = delete;

    /** @brief Install capture callbacks and start scenario managers. */
    void start();
    /** @brief Stop scenario managers. */
    void stop();

    /**
     * @brief Send payload from a specific source node using raw pointer input.
     */
    int sendFromDevice(uint16_t src_node_id,
                       const uint8_t* payload,
                       size_t payload_len,
                       Priority priority = Priority::NORMAL,
                       PropagationMode mode = PropagationMode::OMNI,
                       uint16_t target_heading = 0,
                       uint8_t max_hops = 3,
                       uint16_t max_distance_m = 2000,
                       uint16_t lifetime_s = 30);

    /**
     * @brief Send payload from a specific source node using vector input.
     */
    int sendFromDevice(uint16_t src_node_id,
                       const std::vector<uint8_t>& payload,
                       Priority priority = Priority::NORMAL,
                       PropagationMode mode = PropagationMode::OMNI,
                       uint16_t target_heading = 0,
                       uint8_t max_hops = 3,
                       uint16_t max_distance_m = 2000,
                       uint16_t lifetime_s = 30);

    /** @brief Clear all captured deliveries across nodes. */
    void clearCaptured();

    /** @brief Number of captured deliveries for a node. */
    size_t receivedCount(uint16_t node_id) const;
    /** @brief Snapshot of captured deliveries for a node. */
    std::vector<CapturedMessage> receivedMessages(uint16_t node_id) const;

    /**
     * @brief Step simulation until node has received at least @p min_count messages.
     */
    bool waitForMessageCount(uint16_t node_id,
                             size_t min_count,
                             uint64_t timeout_ms,
                             uint64_t step_ms = 50,
                             uint64_t idle_sleep_ms = 2);

    /**
     * @brief Repeatedly step simulation until predicate is true or timeout expires.
     */
    bool stepUntil(const std::function<bool()>& predicate,
                   uint64_t timeout_ms,
                   uint64_t step_ms = 50,
                   uint64_t idle_sleep_ms = 2);

    /** @brief Check whether node received an exact payload bytes sequence. */
    bool hasPayload(uint16_t node_id, const std::vector<uint8_t>& payload) const;

    /** @brief Mutable access to wrapped scenario. */
    SimulationScenario& scenario();
    /** @brief Const access to wrapped scenario. */
    const SimulationScenario& scenario() const;

private:
    void installCaptureCallbacks();

    std::unique_ptr<SimulationScenario> scenario_;
    mutable std::mutex capture_mutex_;
    std::unordered_map<uint16_t, std::vector<CapturedMessage>> received_;
    bool callbacks_installed_{false};
    bool started_{false};
};
