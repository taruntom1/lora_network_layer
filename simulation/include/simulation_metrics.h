#pragma once

#include <cstdint>
#include <mutex>

/**
 * @file simulation_metrics.h
 * @ingroup sim_runtime
 * @brief Simulation metrics snapshot and collection helpers.
 */

/**
 * @ingroup sim_api
 * @brief Aggregated simulation metrics snapshot.
 */
struct SimulationMetricsSnapshot {
    uint64_t tx_attempts{0};
    uint64_t tx_success{0};
    uint64_t tx_fail_collision{0};
    uint64_t tx_fail_per{0};
    uint64_t retransmissions{0};
    uint64_t rx_delivered{0};
    uint64_t rx_dropped{0};

    double packet_delivery_ratio{0.0};
    double average_latency_ms{0.0};
    double channel_utilization_pct{0.0};
};

/**
 * @ingroup sim_internal
 * @brief Thread-safe accumulator for simulation metrics.
 */
class SimulationMetricsCollector {
public:
    /** @brief Reset all counters and set baseline time. */
    void reset(uint64_t now_us = 0);

    /** @brief Count one transmit attempt. */
    void onTxAttempt();
    /** @brief Count transmit outcome as success/failure. */
    void onTxOutcome(bool success);
    /** @brief Count one retransmission/defer attempt. */
    void onRetransmission();
    /** @brief Count one collision-driven transmit failure. */
    void onTxCollisionFailure();
    /** @brief Count one PER-driven transmit failure. */
    void onTxPerFailure();
    /** @brief Count one delivered receive event with measured latency. */
    void onRxDelivered(uint64_t latency_us);
    /** @brief Count one dropped receive event. */
    void onRxDropped();
    /** @brief Add busy-time contribution in microseconds. */
    void addChannelBusyTime(uint64_t busy_us);

    /** @brief Materialize a read-only metrics snapshot. */
    SimulationMetricsSnapshot snapshot(uint64_t sim_now_us) const;

private:
    mutable std::mutex mutex_;
    uint64_t start_time_us_{0};
    uint64_t tx_attempts_{0};
    uint64_t tx_success_{0};
    uint64_t tx_fail_collision_{0};
    uint64_t tx_fail_per_{0};
    uint64_t retransmissions_{0};
    uint64_t rx_delivered_{0};
    uint64_t rx_dropped_{0};
    uint64_t total_latency_us_{0};
    uint64_t channel_busy_us_{0};
};
