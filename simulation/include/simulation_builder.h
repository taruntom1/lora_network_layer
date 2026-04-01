#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "config_loader.h"
#include "simulation_clock.h"
#include "simulation_config.h"
#include "simulated_link_layer.h"
#include "simulated_location_provider.h"
#include "simulated_network.h"

class NetworkManager;

/**
 * @file simulation_builder.h
 * @ingroup sim_runtime
 * @brief Scenario orchestration and fluent builder APIs for host simulation.
 */

/**
 * @ingroup sim_api
 * @brief Runtime container for a fully built simulation scenario.
 *
 * A scenario owns all per-node runtime objects (location providers, simulated
 * links, network managers), a shared simulated channel, and a deterministic
 * virtual clock. Progression is controlled manually through @ref step.
 */
class SimulationScenario {
public:
    /**
     * @brief Construct and wire node runtimes from configuration.
     * @throws std::runtime_error If duplicate node ids are present.
     */
    explicit SimulationScenario(const SimulationConfig& config);
    ~SimulationScenario();

    SimulationScenario(const SimulationScenario&) = delete;
    SimulationScenario& operator=(const SimulationScenario&) = delete;

    /** @brief Start all node managers (idempotent). */
    void start();
    /** @brief Stop all node managers (idempotent). */
    void stop();

    /**
     * @brief Advance virtual time and update node kinematics.
     * @param delta_ms Milliseconds to advance.
     */
    void step(uint64_t delta_ms);

    /** @brief Number of nodes in the scenario. */
    size_t deviceCount() const;

    /** @brief Mutable access to scenario virtual clock. */
    SimulationClock& clock();
    /** @brief Const access to scenario virtual clock. */
    const SimulationClock& clock() const;

    /** @brief Mutable access to shared simulated channel. */
    SimulatedNetwork& network();
    /** @brief Const access to shared simulated channel. */
    const SimulatedNetwork& network() const;

    /**
     * @brief Lookup node manager by node id.
     * @return Pointer to manager, or @c nullptr if node is missing.
     */
    NetworkManager* manager(uint16_t node_id);
    /**
     * @brief Const lookup node manager by node id.
     * @return Pointer to manager, or @c nullptr if node is missing.
     */
    const NetworkManager* manager(uint16_t node_id) const;

    /**
     * @brief Lookup node location provider by node id.
     * @return Pointer to provider, or @c nullptr if node is missing.
     */
    SimulatedLocationProvider* locationProvider(uint16_t node_id);
    /**
     * @brief Const lookup node location provider by node id.
     * @return Pointer to provider, or @c nullptr if node is missing.
     */
    const SimulatedLocationProvider* locationProvider(uint16_t node_id) const;

    /** @brief Sorted list of node ids in this scenario. */
    std::vector<uint16_t> nodeIds() const;

private:
    struct NodeRuntime;

    void applyDueWaypoints(NodeRuntime& node);

    SimulationClock clock_;
    SimulatedNetwork network_;
    bool started_{false};
    std::unordered_map<uint16_t, std::unique_ptr<NodeRuntime>> nodes_;
};

/**
 * @ingroup sim_api
 * @brief Fluent builder for constructing @ref SimulationScenario instances.
 *
 * The builder supports programmatic scenario composition and file-driven
 * loading through @ref ConfigLoader. Build-time validation guarantees that at
 * least one node exists before runtime objects are instantiated.
 */
class SimulationBuilder {
public:
    /** @brief Construct builder with default runtime config values. */
    SimulationBuilder();

    /** @brief Set radio carrier frequency in MHz. */
    SimulationBuilder& setCarrierFrequencyMhz(float freq_mhz);
    /** @brief Set initial simulation clock value in whole seconds. */
    SimulationBuilder& setStartTimeSeconds(uint32_t start_time_s);
    /** @brief Set shared network-manager runtime capacities. */
    SimulationBuilder& setNetworkConfig(const NetworkConfig& cfg);

    /**
     * @brief Add or replace a device config by node id.
     * @param cfg Device definition.
     */
    SimulationBuilder& addDevice(const SimulationDeviceConfig& cfg);
    /**
     * @brief Add a waypoint to an existing device.
     * @throws std::runtime_error If @p node_id does not exist in current config.
     */
    SimulationBuilder& addWaypoint(uint16_t node_id, const SimulationWaypointConfig& waypoint);

    /** @brief Replace current config from a parsed config file. */
    SimulationBuilder& loadConfigFile(const std::string& file_path);
    /** @brief Replace current config object wholesale. */
    SimulationBuilder& setConfig(const SimulationConfig& cfg);

    /**
     * @brief Materialize a runnable simulation scenario.
     * @throws std::runtime_error If config has no devices.
     */
    std::unique_ptr<SimulationScenario> build() const;

    /** @brief Read current builder config snapshot. */
    const SimulationConfig& config() const;

private:
    SimulationConfig config_;
};
