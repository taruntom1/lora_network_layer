#include "simulation_builder.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "network_manager.h"

/**
 * @file simulation_builder.cpp
 * @ingroup sim_internal
 * @brief Internal runtime assembly and stepping logic for simulation scenarios.
 *
 * This unit wires per-node runtime objects in dependency order:
 * location provider -> link adapter -> simulated network registration ->
 * network manager. During each step, the scenario advances global virtual time,
 * advances node kinematics, then applies due waypoint overrides.
 */

struct SimulationScenario::NodeRuntime {
    SimulationDeviceConfig config;
    std::unique_ptr<SimulatedLocationProvider> location;
    std::unique_ptr<SimulatedLinkLayer> link;
    std::unique_ptr<NetworkManager> manager;
    size_t next_waypoint_idx{0};
};

SimulationScenario::SimulationScenario(const SimulationConfig& config)
    : network_(config.runtime.carrier_freq_mhz)
{
    clock_.reset(static_cast<uint64_t>(config.runtime.start_time_s) * 1000ULL);

    for (const SimulationDeviceConfig& device_cfg : config.devices) {
        if (nodes_.find(device_cfg.node_id) != nodes_.end()) {
            throw std::runtime_error("Duplicate device node_id in simulation config: " +
                                     std::to_string(static_cast<unsigned long long>(device_cfg.node_id)));
        }

        auto runtime = std::make_unique<NodeRuntime>();
        runtime->config = device_cfg;

        SimulatedLocationProvider::Kinematics initial_state{
            device_cfg.initial_position,
            device_cfg.speed_cm_s,
            device_cfg.heading_cdeg,
            clock_.nowSeconds(),
        };

        runtime->location = std::make_unique<SimulatedLocationProvider>(initial_state);
        runtime->link = std::make_unique<SimulatedLinkLayer>(device_cfg.node_id, network_);

        network_.registerNode(device_cfg.node_id,
                              runtime->link.get(),
                              runtime->location.get(),
                              device_cfg.radio);

        runtime->manager = std::make_unique<NetworkManager>(*runtime->link,
                                                            *runtime->location,
                                                            config.runtime.network_config);

        std::sort(runtime->config.waypoints.begin(), runtime->config.waypoints.end(),
                  [](const SimulationWaypointConfig& a, const SimulationWaypointConfig& b) {
                      return a.at_s < b.at_s;
                  });

        nodes_.emplace(device_cfg.node_id, std::move(runtime));
    }

    for (auto& [_, node] : nodes_) {
        applyDueWaypoints(*node);
    }
}

SimulationScenario::~SimulationScenario()
{
    stop();
}

void SimulationScenario::start()
{
    if (started_) {
        return;
    }

    for (auto& [_, node] : nodes_) {
        node->manager->start();
    }
    started_ = true;
}

void SimulationScenario::stop()
{
    if (!started_) {
        return;
    }

    for (auto& [_, node] : nodes_) {
        node->manager->stop();
    }
    started_ = false;
}

void SimulationScenario::step(uint64_t delta_ms)
{
    clock_.step(delta_ms);

    for (auto& [_, node] : nodes_) {
        node->location->advance(delta_ms);
    }

    for (auto& [_, node] : nodes_) {
        applyDueWaypoints(*node);
    }
}

size_t SimulationScenario::deviceCount() const
{
    return nodes_.size();
}

SimulationClock& SimulationScenario::clock()
{
    return clock_;
}

const SimulationClock& SimulationScenario::clock() const
{
    return clock_;
}

SimulatedNetwork& SimulationScenario::network()
{
    return network_;
}

const SimulatedNetwork& SimulationScenario::network() const
{
    return network_;
}

NetworkManager* SimulationScenario::manager(uint16_t node_id)
{
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return nullptr;
    }
    return it->second->manager.get();
}

const NetworkManager* SimulationScenario::manager(uint16_t node_id) const
{
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return nullptr;
    }
    return it->second->manager.get();
}

SimulatedLocationProvider* SimulationScenario::locationProvider(uint16_t node_id)
{
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return nullptr;
    }
    return it->second->location.get();
}

const SimulatedLocationProvider* SimulationScenario::locationProvider(uint16_t node_id) const
{
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return nullptr;
    }
    return it->second->location.get();
}

std::vector<uint16_t> SimulationScenario::nodeIds() const
{
    std::vector<uint16_t> ids;
    ids.reserve(nodes_.size());
    for (const auto& [node_id, _] : nodes_) {
        ids.push_back(node_id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void SimulationScenario::applyDueWaypoints(NodeRuntime& node)
{
    const uint32_t now_s = clock_.nowSeconds();

    while (node.next_waypoint_idx < node.config.waypoints.size()) {
        const SimulationWaypointConfig& waypoint = node.config.waypoints[node.next_waypoint_idx];
        if (waypoint.at_s > now_s) {
            break;
        }

        SimulatedLocationProvider::Kinematics state{
            waypoint.position,
            waypoint.speed_cm_s,
            waypoint.heading_cdeg,
            now_s,
        };
        node.location->setKinematics(state);
        ++node.next_waypoint_idx;
    }
}

SimulationBuilder::SimulationBuilder() = default;

SimulationBuilder& SimulationBuilder::setCarrierFrequencyMhz(float freq_mhz)
{
    config_.runtime.carrier_freq_mhz = freq_mhz;
    return *this;
}

SimulationBuilder& SimulationBuilder::setStartTimeSeconds(uint32_t start_time_s)
{
    config_.runtime.start_time_s = start_time_s;
    return *this;
}

SimulationBuilder& SimulationBuilder::setNetworkConfig(const NetworkConfig& cfg)
{
    config_.runtime.network_config = cfg;
    return *this;
}

SimulationBuilder& SimulationBuilder::addDevice(const SimulationDeviceConfig& cfg)
{
    for (SimulationDeviceConfig& existing : config_.devices) {
        if (existing.node_id == cfg.node_id) {
            existing = cfg;
            return *this;
        }
    }

    config_.devices.push_back(cfg);
    return *this;
}

SimulationBuilder& SimulationBuilder::addWaypoint(uint16_t node_id,
                                                   const SimulationWaypointConfig& waypoint)
{
    for (SimulationDeviceConfig& device : config_.devices) {
        if (device.node_id == node_id) {
            device.waypoints.push_back(waypoint);
            return *this;
        }
    }

    throw std::runtime_error("Cannot add waypoint for unknown node_id: " +
                             std::to_string(static_cast<unsigned long long>(node_id)));
}

SimulationBuilder& SimulationBuilder::loadConfigFile(const std::string& file_path)
{
    config_ = ConfigLoader::loadFromFile(file_path);
    return *this;
}

SimulationBuilder& SimulationBuilder::setConfig(const SimulationConfig& cfg)
{
    config_ = cfg;
    return *this;
}

std::unique_ptr<SimulationScenario> SimulationBuilder::build() const
{
    if (config_.devices.empty()) {
        throw std::runtime_error("Simulation config must contain at least one device");
    }

    return std::make_unique<SimulationScenario>(config_);
}

const SimulationConfig& SimulationBuilder::config() const
{
    return config_;
}
