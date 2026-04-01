#pragma once

#include <cstdint>
#include <vector>

#include "network_manager.h"
#include "simulated_network.h"

/**
 * @file simulation_config.h
 * @ingroup sim_config
 * @brief Scenario configuration structures used by builder and file parser paths.
 */

/**
 * @ingroup sim_config
 * @brief Waypoint state to apply to a device when simulation clock reaches @ref at_s.
 */
struct SimulationWaypointConfig {
    /** Waypoint activation time in whole seconds. */
    uint32_t at_s{0};
    /** Target geodetic position in fixed-point degrees (deg * 1e7). */
    GeoPoint position{0, 0};
    /** Target speed in centimeters per second after activation. */
    uint16_t speed_cm_s{0};
    /** Target heading in centi-degrees, where 0 is north and 9000 is east. */
    uint16_t heading_cdeg{0};
};

/**
 * @ingroup sim_config
 * @brief Device-level scenario definition for one simulated node.
 */
struct SimulationDeviceConfig {
    /** Unique node identifier used by NetworkManager and simulated radio fan-out. */
    uint16_t node_id{0};
    /** Initial geodetic position in fixed-point degrees (deg * 1e7). */
    GeoPoint initial_position{0, 0};
    /** Initial speed in centimeters per second. */
    uint16_t speed_cm_s{0};
    /** Initial heading in centi-degrees. */
    uint16_t heading_cdeg{0};
    /** Per-node radio profile used by SimulatedNetwork propagation checks. */
    SimulatedNetwork::RadioConfig radio{};
    /** Optional ordered motion updates applied during stepping. */
    std::vector<SimulationWaypointConfig> waypoints;
};

/**
 * @ingroup sim_config
 * @brief Runtime-level parameters shared by all nodes in a scenario.
 */
struct SimulationRuntimeConfig {
    /** Packet error model selection for SNR-based reception success. */
    enum class PerModel : uint8_t {
        Disabled = 0,
        Threshold = 1,
        Logistic = 2,
    };

    /** Carrier frequency in MHz for free-space path-loss calculations. */
    float carrier_freq_mhz{868.0f};
    /** Initial simulation wall-clock value in seconds. */
    uint32_t start_time_s{0};
    /** Deterministic seed for simulation randomness features. */
    uint64_t random_seed{1};
    /** Keep legacy immediate delivery semantics when true. */
    bool compatibility_immediate_delivery{true};
    /** Target data rate used by timed channel models (future phases). */
    uint32_t data_rate_bps{50000};
    /** CSMA slot duration in microseconds (future phases). */
    uint32_t slot_time_us{1000};
    /** DIFS guard interval in microseconds (future phases). */
    uint32_t difs_us{0};
    /** Minimum contention window (future phases). */
    uint16_t cw_min{3};
    /** Maximum contention window (future phases). */
    uint16_t cw_max{1023};
    /** Maximum retry count for contention/backoff (future phases). */
    uint8_t max_retries{4};
    /** Standard deviation of fading term in dB (future phases). */
    float fading_stddev_db{0.0f};
    /** Extra jitter term in dB applied before PER decisions (future phases). */
    float noise_jitter_db{0.0f};
    /** PER model mode used after RSSI/sensitivity checks. */
    PerModel per_model{PerModel::Disabled};
    /** SNR threshold used by threshold PER mode. */
    float snr_threshold_db{-200.0f};
    /** Logistic slope parameter for logistic PER mode. */
    float per_logistic_k{1.0f};
    /** Logistic midpoint SNR (dB) for logistic PER mode. */
    float per_logistic_mid_db{0.0f};
    /** Minimum propagation delay clamp in microseconds (future phases). */
    uint32_t propagation_min_delay_us{0};
    /** Toggle collision model once implemented. */
    bool enable_collision_model{false};
    /** Toggle utilization-based congestion drops once implemented. */
    bool enable_congestion_drops{false};
    /** Utilization threshold (percentage) above which congestion drops may apply. */
    float congestion_utilization_threshold_pct{95.0f};
    /** Probability [0,1] of congestion drop when threshold is exceeded. */
    float congestion_drop_probability{0.0f};
    /** Minimum elapsed virtual-time window before congestion checks are active. */
    uint32_t congestion_min_elapsed_us{1000};
    /** Network-layer queue and duplicate-filter capacities for all managers. */
    NetworkConfig network_config{64, 8, 16};
};

/**
 * @ingroup sim_api
 * @brief Full simulation scenario definition consumed by @ref SimulationBuilder.
 */
struct SimulationConfig {
    /** Shared runtime parameters. */
    SimulationRuntimeConfig runtime{};
    /** All nodes that participate in the scenario. */
    std::vector<SimulationDeviceConfig> devices;
};
