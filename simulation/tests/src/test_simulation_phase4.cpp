#include <cstdio>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "simulation_test_base.h"

namespace {

constexpr uint16_t kNodeA = 0x3001;
constexpr uint16_t kNodeB = 0x3002;
constexpr uint16_t kNodeC = 0x3003;
constexpr uint16_t kNodeD = 0x3004;

constexpr GeoPoint kBasePoint{125000000, 773000000};

void expectTrue(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expectEqSize(size_t actual, size_t expected, const char* message)
{
    if (actual != expected) {
        throw std::runtime_error(std::string(message) +
                                 " (expected=" +
                                 std::to_string(static_cast<unsigned long long>(expected)) +
                                 ", actual=" +
                                 std::to_string(static_cast<unsigned long long>(actual)) +
                                 ")");
    }
}

void expectEqInt(int actual, int expected, const char* message)
{
    if (actual != expected) {
        throw std::runtime_error(std::string(message) +
                                 " (expected=" + std::to_string(expected) +
                                 ", actual=" + std::to_string(actual) + ")");
    }
}

SimulationDeviceConfig makeDevice(uint16_t node_id,
                                  GeoPoint position,
                                  float tx_power_dbm,
                                  float sensitivity_dbm)
{
    SimulationDeviceConfig cfg;
    cfg.node_id = node_id;
    cfg.initial_position = position;
    cfg.speed_cm_s = 0;
    cfg.heading_cdeg = 0;
    cfg.radio.tx_power_dbm = tx_power_dbm;
    cfg.radio.noise_floor_dbm = -110.0f;
    cfg.radio.sensitivity_dbm = sensitivity_dbm;
    return cfg;
}

SimulationConfig makeBaseConfig()
{
    SimulationConfig cfg;
    cfg.runtime.carrier_freq_mhz = 868.0f;
    cfg.runtime.start_time_s = 1000;
    cfg.runtime.network_config = NetworkConfig{64, 8, 16};
    return cfg;
}

void testTtlExpiryDropsDelayedForward()
{
    SimulationConfig cfg = makeBaseConfig();
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -82.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125000000, 773100000}, 14.0f, -82.0f));
    cfg.devices.push_back(makeDevice(kNodeC, GeoPoint{125000000, 773200000}, 14.0f, -82.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC};
    const int rc = test.sendFromDevice(kNodeA,
                                       payload,
                                       Priority::LOW,
                                       PropagationMode::OMNI,
                                       0,
                                       3,
                                       4000,
                                       1);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "TTL test send should succeed");

    expectTrue(test.waitForMessageCount(kNodeB, 1, 1200),
               "relay node should receive original frame");

    // Advance virtual time so forwarded copies are expired when they arrive.
    test.scenario().step(3000);

    test.stepUntil([]() { return false; }, 1300);

    expectEqSize(test.receivedCount(kNodeC), 0,
                 "destination should not receive expired forwarded frame");

    test.stop();
}

void testDirectionalConeForwardingPath()
{
    SimulationConfig cfg = makeBaseConfig();
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -82.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125000000, 773100000}, 14.0f, -82.0f));
    cfg.devices.push_back(makeDevice(kNodeC, GeoPoint{125100000, 773000000}, 14.0f, -82.0f));
    cfg.devices.push_back(makeDevice(kNodeD, GeoPoint{125000000, 773200000}, 14.0f, -82.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload = {0x11, 0x22, 0x33, 0x44};
    const int rc = test.sendFromDevice(kNodeA,
                                       payload,
                                       Priority::NORMAL,
                                       PropagationMode::DIRECTIONAL,
                                       9000,
                                       3,
                                       5000,
                                       30);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "directional send should succeed");

    expectTrue(test.waitForMessageCount(kNodeB, 1, 1500),
               "in-cone relay node should receive directional frame");
    expectTrue(test.waitForMessageCount(kNodeC, 1, 1500),
               "out-of-cone node should still deliver locally");
    expectTrue(test.waitForMessageCount(kNodeD, 1, 1800),
               "far east node should be reached via relay");

    const auto d_msgs = test.receivedMessages(kNodeD);
    expectEqSize(d_msgs.size(), 1, "destination should get exactly one directional message");

    const SimulatedLocationProvider* relay_loc = test.scenario().locationProvider(kNodeB);
    expectTrue(relay_loc != nullptr, "relay location provider missing");

    const GeoPoint relay_point = relay_loc->getLocation();
    const GeoPoint tx_point = d_msgs.front().header.txPoint();
    expectTrue(tx_point.lat == relay_point.lat && tx_point.lon == relay_point.lon,
               "directional destination should see relay tx location in header");

    test.stop();
}

void testMultiHopRelayToUnreachableNode()
{
    SimulationConfig cfg = makeBaseConfig();
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -82.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125000000, 773100000}, 14.0f, -82.0f));
    cfg.devices.push_back(makeDevice(kNodeC, GeoPoint{125000000, 773200000}, 14.0f, -82.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload = {5, 4, 3, 2, 1};
    const int rc = test.sendFromDevice(kNodeA,
                                       payload,
                                       Priority::NORMAL,
                                       PropagationMode::OMNI,
                                       0,
                                       2,
                                       5000,
                                       30);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "multihop send should succeed");

    expectTrue(test.waitForMessageCount(kNodeB, 1, 1200), "relay should receive initial frame");
    expectTrue(test.waitForMessageCount(kNodeC, 1, 2000), "destination should receive relayed frame");

    const auto c_msgs = test.receivedMessages(kNodeC);
    expectEqSize(c_msgs.size(), 1, "destination should receive one relayed message");

    const NetworkHeader& hdr = c_msgs.front().header;
    expectTrue(hdr.hops_remaining == 1,
               "destination should observe decremented hop count from relay");

    const SimulatedLocationProvider* relay_loc = test.scenario().locationProvider(kNodeB);
    expectTrue(relay_loc != nullptr, "relay location provider missing");

    const GeoPoint relay_point = relay_loc->getLocation();
    const GeoPoint tx_point = hdr.txPoint();
    expectTrue(tx_point.lat == relay_point.lat && tx_point.lon == relay_point.lon,
               "destination should observe relay location as tx point");

    test.stop();
}

void testDuplicateSuppressionAtReceiver()
{
    SimulationConfig cfg = makeBaseConfig();
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -95.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125000000, 773050000}, 14.0f, -95.0f));
    cfg.devices.push_back(makeDevice(kNodeC, GeoPoint{125000000, 773100000}, 14.0f, -95.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    const int rc = test.sendFromDevice(kNodeA,
                                       payload,
                                       Priority::NORMAL,
                                       PropagationMode::OMNI,
                                       0,
                                       3,
                                       5000,
                                       30);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "duplicate suppression send should succeed");

    expectTrue(test.waitForMessageCount(kNodeC, 1, 1000),
               "receiver should get initial direct copy");

    // Let relay duplicates propagate; receiver should still deliver only once.
    test.stepUntil([]() { return false; }, 1600);

    expectEqSize(test.receivedCount(kNodeC), 1,
                 "receiver should deliver one message despite duplicate relays");

    test.stop();
}

int runTest(const char* name, const std::function<void()>& fn)
{
    try {
        fn();
        std::printf("[PASS] %s\n", name);
        return 0;
    } catch (const std::exception& ex) {
        std::printf("[FAIL] %s: %s\n", name, ex.what());
        return 1;
    } catch (...) {
        std::printf("[FAIL] %s: unknown exception\n", name);
        return 1;
    }
}

} // namespace

int main()
{
    int failures = 0;

    failures += runTest("ttl_expiry_drops_delayed_forward", testTtlExpiryDropsDelayedForward);
    failures += runTest("directional_cone_forwarding_path", testDirectionalConeForwardingPath);
    failures += runTest("multi_hop_relay_to_unreachable_node", testMultiHopRelayToUnreachableNode);
    failures += runTest("duplicate_suppression_at_receiver", testDuplicateSuppressionAtReceiver);

    if (failures == 0) {
        std::printf("All phase 4 simulation tests passed\n");
        return 0;
    }

    std::printf("Phase 4 simulation tests failed (%d)\n", failures);
    return 1;
}
