#include "simulation_metrics.h"

/**
 * @file simulation_metrics.cpp
 * @ingroup sim_internal
 * @brief Metrics accumulator implementation for simulation runtime.
 */

void SimulationMetricsCollector::reset(uint64_t now_us)
{
    std::lock_guard<std::mutex> lock(mutex_);
    start_time_us_ = now_us;
    tx_attempts_ = 0;
    tx_success_ = 0;
    tx_fail_collision_ = 0;
    tx_fail_per_ = 0;
    retransmissions_ = 0;
    rx_delivered_ = 0;
    rx_dropped_ = 0;
    total_latency_us_ = 0;
    channel_busy_us_ = 0;
}

void SimulationMetricsCollector::onTxAttempt()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++tx_attempts_;
}

void SimulationMetricsCollector::onTxOutcome(bool success)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (success) {
        ++tx_success_;
    }
}

void SimulationMetricsCollector::onRetransmission()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++retransmissions_;
}

void SimulationMetricsCollector::onTxCollisionFailure()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++tx_fail_collision_;
}

void SimulationMetricsCollector::onTxPerFailure()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++tx_fail_per_;
}

void SimulationMetricsCollector::onRxDelivered(uint64_t latency_us)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++rx_delivered_;
    total_latency_us_ += latency_us;
}

void SimulationMetricsCollector::onRxDropped()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++rx_dropped_;
}

void SimulationMetricsCollector::addChannelBusyTime(uint64_t busy_us)
{
    std::lock_guard<std::mutex> lock(mutex_);
    channel_busy_us_ += busy_us;
}

SimulationMetricsSnapshot SimulationMetricsCollector::snapshot(uint64_t sim_now_us) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    SimulationMetricsSnapshot snap;
    snap.tx_attempts = tx_attempts_;
    snap.tx_success = tx_success_;
    snap.tx_fail_collision = tx_fail_collision_;
    snap.tx_fail_per = tx_fail_per_;
    snap.retransmissions = retransmissions_;
    snap.rx_delivered = rx_delivered_;
    snap.rx_dropped = rx_dropped_;

    if (tx_attempts_ > 0) {
        snap.packet_delivery_ratio =
            static_cast<double>(tx_success_) / static_cast<double>(tx_attempts_);
    }

    if (rx_delivered_ > 0) {
        snap.average_latency_ms =
            static_cast<double>(total_latency_us_) / static_cast<double>(rx_delivered_) / 1000.0;
    }

    const uint64_t elapsed_us = (sim_now_us > start_time_us_) ? (sim_now_us - start_time_us_) : 0;
    if (elapsed_us > 0) {
        snap.channel_utilization_pct =
            (100.0 * static_cast<double>(channel_busy_us_)) / static_cast<double>(elapsed_us);
    }

    return snap;
}
