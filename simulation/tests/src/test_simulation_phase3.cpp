#include <cstdio>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "simulation_test_base.h"

namespace {

constexpr uint16_t kNodeA = 0x2001;
constexpr uint16_t kNodeB = 0x2002;
constexpr uint16_t kNodeC = 0x2003;

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

void testBroadcastDelivery()
{
    SimulationConfig cfg = makeBaseConfig();
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -118.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125005000, 773005000}, 14.0f, -118.0f));
    cfg.devices.push_back(makeDevice(kNodeC, GeoPoint{124996000, 773002000}, 14.0f, -118.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload = {10, 20, 30, 40};
    const int rc = test.sendFromDevice(kNodeA, payload);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "broadcast send should succeed");

    expectTrue(test.waitForMessageCount(kNodeB, 1, 1200), "node B did not receive broadcast");
    expectTrue(test.waitForMessageCount(kNodeC, 1, 1200), "node C did not receive broadcast");

    expectTrue(test.hasPayload(kNodeB, payload), "node B payload mismatch");
    expectTrue(test.hasPayload(kNodeC, payload), "node C payload mismatch");

    expectEqSize(test.receivedCount(kNodeA), 0, "origin node should not deliver its own message");

    test.stop();
}

void testDistanceBasedReachability()
{
    SimulationConfig cfg = makeBaseConfig();
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -95.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125005000, 773005000}, 14.0f, -95.0f));
    cfg.devices.push_back(makeDevice(kNodeC, GeoPoint{130000000, 773000000}, 14.0f, -95.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload = {1, 2, 3};
    const int rc = test.sendFromDevice(kNodeA, payload);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "distance test send should succeed");

    expectTrue(test.waitForMessageCount(kNodeB, 1, 1200), "nearby node B should receive message");

    test.stepUntil([]() { return false; }, 400);
    expectEqSize(test.receivedCount(kNodeC), 0, "far node C should remain unreachable");

    test.stop();
}

void testWaypointMovementChangesConnectivity()
{
    SimulationConfig cfg = makeBaseConfig();
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -95.0f));

    SimulationDeviceConfig moving = makeDevice(kNodeB, GeoPoint{130000000, 773000000}, 14.0f, -95.0f);
    moving.waypoints.push_back(SimulationWaypointConfig{
        1002,
        GeoPoint{125006000, 773004000},
        0,
        0,
    });

    cfg.devices.push_back(moving);

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> before_move_payload = {9, 9, 9};
    int rc = test.sendFromDevice(kNodeA, before_move_payload);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "pre-move send should succeed");

    test.stepUntil([]() { return false; }, 300);
    expectEqSize(test.receivedCount(kNodeB), 0,
                 "moving node should not receive before entering range");

    test.scenario().step(2500);
    test.clearCaptured();

    const std::vector<uint8_t> after_move_payload = {7, 7, 7};
    rc = test.sendFromDevice(kNodeA, after_move_payload);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "post-move send should succeed");

    expectTrue(test.waitForMessageCount(kNodeB, 1, 1200),
               "moving node should receive after entering range");
    expectTrue(test.hasPayload(kNodeB, after_move_payload),
               "moving node received wrong payload after moving");

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

    failures += runTest("broadcast_delivery", testBroadcastDelivery);
    failures += runTest("distance_based_reachability", testDistanceBasedReachability);
    failures += runTest("waypoint_movement_changes_connectivity", testWaypointMovementChangesConnectivity);

    if (failures == 0) {
        std::printf("All phase 3 simulation tests passed\n");
        return 0;
    }

    std::printf("Phase 3 simulation tests failed (%d)\n", failures);
    return 1;
}
