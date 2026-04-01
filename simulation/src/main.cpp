#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "network_manager.h"
#include "simulation_builder.h"

/**
 * @file main.cpp
 * @ingroup sim_internal
 * @brief Demo executable showcasing scenario construction and message flow.
 *
 * The demo supports two startup modes:
 * - Built-in three-node default scenario.
 * - External scenario loaded from config file argument.
 */

namespace {

constexpr uint16_t kNodeA = 0x1001;
constexpr uint16_t kNodeB = 0x1002;
constexpr uint16_t kNodeC = 0x1003;

std::unique_ptr<SimulationScenario> createDefaultScenario()
{
    SimulationBuilder builder;

    builder.setCarrierFrequencyMhz(868.0f)
        .setStartTimeSeconds(1000)
        .setNetworkConfig(NetworkConfig{64, 8, 16})
        .addDevice(SimulationDeviceConfig{
            kNodeA,
            GeoPoint{125000000, 773000000},
            80,
            9000,
            SimulatedNetwork::RadioConfig{14.0f, -110.0f, -118.0f},
            {}})
        .addDevice(SimulationDeviceConfig{
            kNodeB,
            GeoPoint{125005000, 773005000},
            40,
            18000,
            SimulatedNetwork::RadioConfig{14.0f, -110.0f, -118.0f},
            {}})
        .addDevice(SimulationDeviceConfig{
            kNodeC,
            GeoPoint{125030000, 773030000},
            0,
            0,
            SimulatedNetwork::RadioConfig{14.0f, -110.0f, -118.0f},
            {}})
        .addWaypoint(kNodeA,
                     SimulationWaypointConfig{1002,
                                              GeoPoint{125001200, 773001500},
                                              100,
                                              4500});

    return builder.build();
}

} // namespace

int main(int argc, char** argv)
{
    try {
        std::unique_ptr<SimulationScenario> scenario;

        if (argc > 1) {
            SimulationBuilder builder;
            const std::string config_path = argv[1];
            builder.loadConfigFile(config_path);
            scenario = builder.build();
            std::printf("Loaded simulation config: %s\n", config_path.c_str());
        } else {
            scenario = createDefaultScenario();
            std::printf("Using built-in default scenario\n");
        }

        const std::vector<uint16_t> node_ids = scenario->nodeIds();
        if (node_ids.size() < 2) {
            std::fprintf(stderr, "Need at least 2 devices in scenario\n");
            return 2;
        }

        const uint16_t sender_id = node_ids.front();
        NetworkManager* sender_mgr = scenario->manager(sender_id);
        if (!sender_mgr) {
            std::fprintf(stderr, "Sender node not found\n");
            return 2;
        }

        for (size_t i = 1; i < node_ids.size(); ++i) {
            const uint16_t node_id = node_ids[i];
            NetworkManager* mgr = scenario->manager(node_id);
            if (!mgr) {
                continue;
            }

            mgr->setAppRxCallback([node_id](const NetworkHeader& hdr,
                                            const uint8_t* payload,
                                            size_t len) {
                std::printf("Node 0x%04x received msg_id=0x%08lx bytes=%zu first=%u\n",
                            static_cast<unsigned>(node_id),
                            static_cast<unsigned long>(hdr.message_id),
                            len,
                            (len > 0) ? static_cast<unsigned>(payload[0]) : 0U);
            });
        }

        scenario->start();

        const uint8_t payload[] = {42, 99, 7};
        int rc = sender_mgr->sendMessage(Priority::NORMAL,
                                         PropagationMode::OMNI,
                                         0,
                                         3,
                                         2000,
                                         30,
                                         payload,
                                         sizeof(payload));

        std::printf("Initial send rc=%d\n", rc);

        for (int i = 0; i < 20; ++i) {
            scenario->step(100);
        }

        scenario->stop();
        return 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Simulation failed: %s\n", ex.what());
        return 1;
    }
}
