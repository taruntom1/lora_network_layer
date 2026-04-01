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
    /** Carrier frequency in MHz for free-space path-loss calculations. */
    float carrier_freq_mhz{868.0f};
    /** Initial simulation wall-clock value in seconds. */
    uint32_t start_time_s{0};
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
