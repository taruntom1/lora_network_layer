#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "simulation_event_queue.h"
#include "simulation_metrics.h"

class SimulatedLinkLayer;
class SimulatedLocationProvider;

/**
 * @file simulated_network.h
 * @ingroup sim_channel
 * @brief In-memory radio channel model used by host simulation nodes.
 */

/**
 * @ingroup sim_api
 * @brief Simulated LoRa channel that fans out frames across registered nodes.
 *
 * For each transmission, the channel snapshots sender/receiver states,
 * computes free-space path loss and SNR, and delivers only when the receiver's
 * sensitivity threshold is met.
 *
 * Receive power model (text form):
 * RSSI_dBm = P_tx_dBm - (32.44 + 20*log10(f_MHz) + 20*log10(d_km)).
 * SNR_dB = RSSI_dBm - NoiseFloor_dBm.
 */
class SimulatedNetwork {
public:
    /**
     * @brief Per-node radio profile applied during propagation checks.
     */
    struct RadioConfig {
        /** Transmit power in dBm used by the source node. */
        float tx_power_dbm = 14.0f;
        /** Receiver noise floor in dBm for SNR estimation. */
        float noise_floor_dbm = -110.0f;
        /** Minimum RSSI in dBm required to deliver a frame. */
        float sensitivity_dbm = -120.0f;
    };

    /**
     * @brief Construct channel model.
     * @param carrier_freq_mhz Carrier frequency in MHz for path-loss calculations.
      * @param compatibility_immediate_delivery Keep immediate-delivery compatibility behavior.
     */
     explicit SimulatedNetwork(float carrier_freq_mhz = 868.0f,
                                        bool compatibility_immediate_delivery = true);

    /**
     * @brief Register a node with default radio profile.
     */
    void registerNode(uint16_t node_id,
                      SimulatedLinkLayer* link,
                      const SimulatedLocationProvider* location);

    /**
     * @brief Register or replace a node with explicit radio profile.
     */
    void registerNode(uint16_t node_id,
                      SimulatedLinkLayer* link,
                      const SimulatedLocationProvider* location,
                      const RadioConfig& cfg);

    /** @brief Remove a node from the channel registry. */
    void unregisterNode(uint16_t node_id);
    /** @brief Current number of registered nodes. */
    size_t nodeCount() const;

    /**
     * @brief Transmit a frame to one node or broadcast to all reachable nodes.
     * @param src_node_id Source node id.
     * @param dst_id Destination node id or broadcast address.
     * @param data Frame payload pointer.
     * @param len Payload length in bytes.
     */
    void transmit(uint16_t src_node_id,
                  uint16_t dst_id,
                  const uint8_t* data,
                  size_t len);

    /** @brief Configure timing and retry parameters for queued channel access. */
    void configureMac(uint32_t data_rate_bps,
                      uint32_t slot_time_us,
                      uint32_t difs_us,
                      uint8_t max_retries,
                      uint32_t propagation_min_delay_us,
                      uint16_t cw_min,
                      uint16_t cw_max,
                      uint64_t random_seed,
                      bool enable_collision_model,
                      uint8_t per_model,
                      float snr_threshold_db,
                      float per_logistic_k,
                      float per_logistic_mid_db,
                      float fading_stddev_db,
                      float noise_jitter_db,
                      bool enable_congestion_drops,
                      float congestion_utilization_threshold_pct,
                      float congestion_drop_probability,
                      uint32_t congestion_min_elapsed_us);

    /** @brief Update current virtual time marker used by enqueueing send operations. */
    void setNowUs(uint64_t now_us);
    /** @brief Current virtual time marker in microseconds. */
    uint64_t nowUs() const;

    /** @brief Process all queued events with timestamp <= @p horizon_us. */
    void processUntil(uint64_t horizon_us);

    /** @brief Snapshot current channel and delivery metrics. */
    SimulationMetricsSnapshot metricsSnapshot(uint64_t sim_now_us) const;
    /** @brief Reset metrics counters using @p now_us as baseline. */
    void resetMetrics(uint64_t now_us = 0);

private:
    struct NodeEntry {
        SimulatedLinkLayer* link;
        const SimulatedLocationProvider* location;
        RadioConfig cfg;
    };

    struct ActiveTransmission {
        uint64_t tx_id;
        uint16_t src_node_id;
        uint64_t start_us;
        uint64_t end_us;
        bool collided;
    };

    struct TxDeliveryState {
        size_t pending_deliveries;
        bool any_delivered;
        bool any_per_drop;
        bool collided;
        uint64_t tx_end_us;
    };

    static float computeRssiDbm(float tx_power_dbm, float distance_m, float carrier_freq_mhz);
    static float computeSnrDb(float rssi_dbm, float noise_floor_dbm);
    uint64_t computeTxDurationUs(size_t payload_len_bytes) const;
    uint64_t computeBackoffSlots(uint16_t src_node_id,
                                 uint8_t retry_count,
                                 uint64_t tx_id,
                                 uint16_t cw_current) const;
    double deterministicUnitInterval(uint64_t key_a,
                                     uint64_t key_b,
                                     uint64_t key_c) const;
    float sampleFadingDb(uint64_t tx_id, uint16_t dst_node_id) const;
    float sampleNoiseJitterDb(uint64_t tx_id, uint16_t dst_node_id) const;
    bool evaluatePerSuccess(float snr_db, uint64_t tx_id, uint16_t dst_node_id) const;
    bool shouldDropForCongestion(uint64_t event_time_us,
                                 uint64_t tx_id,
                                 uint16_t dst_node_id) const;
    static uint64_t mix64(uint64_t x);

    uint16_t getOrInitCwState(uint16_t node_id);
    void resetCwState(uint16_t node_id);

    void executeTxStartEvent(const SimEvent& event);
    void executeRetryBackoffEvent(const SimEvent& event);
    void executeTxEndEvent(const SimEvent& event);
    void executeDeliveryEvent(const SimEvent& event);
    uint64_t nextSequenceId();

    float carrier_freq_mhz_;
    bool compatibility_immediate_delivery_;
    std::atomic<uint64_t> now_us_{0};
    std::atomic<uint64_t> next_sequence_id_{1};
    uint64_t channel_busy_until_us_{0};

    uint32_t data_rate_bps_{50000};
    uint32_t slot_time_us_{1000};
    uint32_t difs_us_{0};
    uint8_t max_retries_{4};
    uint32_t propagation_min_delay_us_{0};
    uint16_t cw_min_{3};
    uint16_t cw_max_{1023};
    uint64_t random_seed_{1};
    bool enable_collision_model_{false};
    uint8_t per_model_{0};
    float snr_threshold_db_{-200.0f};
    float per_logistic_k_{1.0f};
    float per_logistic_mid_db_{0.0f};
    float fading_stddev_db_{0.0f};
    float noise_jitter_db_{0.0f};
    bool enable_congestion_drops_{false};
    float congestion_utilization_threshold_pct_{95.0f};
    float congestion_drop_probability_{0.0f};
    uint32_t congestion_min_elapsed_us_{1000};
    uint64_t channel_busy_accumulated_us_{0};
    uint64_t utilization_start_time_us_{0};

    mutable std::mutex mutex_;
    std::unordered_map<uint16_t, NodeEntry> nodes_;
    std::unordered_map<uint16_t, uint16_t> cw_state_by_node_;
    std::unordered_map<uint64_t, ActiveTransmission> active_transmissions_;
    std::unordered_map<uint64_t, TxDeliveryState> tx_delivery_state_;

    SimulationEventQueue event_queue_;
    SimulationMetricsCollector metrics_;
};
