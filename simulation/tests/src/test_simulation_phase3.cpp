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

void expectGteSize(size_t actual, size_t minimum, const char* message)
{
    if (actual < minimum) {
        throw std::runtime_error(std::string(message) +
                                 " (minimum=" +
                                 std::to_string(static_cast<unsigned long long>(minimum)) +
                                 ", actual=" +
                                 std::to_string(static_cast<unsigned long long>(actual)) +
                                 ")");
    }
}

void expectGteU64(uint64_t actual, uint64_t minimum, const char* message)
{
    if (actual < minimum) {
        throw std::runtime_error(std::string(message) +
                                 " (minimum=" +
                                 std::to_string(static_cast<unsigned long long>(minimum)) +
                                 ", actual=" +
                                 std::to_string(static_cast<unsigned long long>(actual)) +
                                 ")");
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

SimulationConfig makeTimedMacConfig()
{
    SimulationConfig cfg = makeBaseConfig();
    cfg.runtime.compatibility_immediate_delivery = false;
    cfg.runtime.data_rate_bps = 20000;
    cfg.runtime.slot_time_us = 5000;
    cfg.runtime.difs_us = 5000;
    cfg.runtime.max_retries = 6;
    return cfg;
}

SimulationConfig makeCollisionConfig()
{
    SimulationConfig cfg = makeTimedMacConfig();
    cfg.runtime.enable_collision_model = true;
    cfg.runtime.random_seed = 42;
    cfg.runtime.cw_min = 3;
    cfg.runtime.cw_max = 31;
    cfg.runtime.slot_time_us = 1000;
    cfg.runtime.difs_us = 0;
    return cfg;
}

SimulationConfig makePhase4EffectsConfig()
{
    SimulationConfig cfg = makeTimedMacConfig();
    cfg.runtime.enable_collision_model = false;
    cfg.runtime.random_seed = 123;
    cfg.runtime.cw_min = 1;
    cfg.runtime.cw_max = 15;
    cfg.runtime.data_rate_bps = 500000;
    cfg.runtime.slot_time_us = 1000;
    cfg.runtime.difs_us = 0;
    cfg.runtime.propagation_min_delay_us = 0;
    return cfg;
}

SimulationConfig makeCongestionConfig()
{
    SimulationConfig cfg = makePhase4EffectsConfig();
    cfg.runtime.enable_congestion_drops = true;
    cfg.runtime.congestion_utilization_threshold_pct = 0.0f;
    cfg.runtime.congestion_drop_probability = 1.0f;
    cfg.runtime.congestion_min_elapsed_us = 0;
    cfg.runtime.per_model = SimulationRuntimeConfig::PerModel::Disabled;
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

void testTimedDeliveryRequiresProgressedVirtualTime()
{
    SimulationConfig cfg = makeTimedMacConfig();
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -118.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125005000, 773005000}, 14.0f, -118.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    std::vector<uint8_t> payload(40, 0x5A); // ~34 ms including network header at 20 kbps
    const int rc = test.sendFromDevice(kNodeA, payload);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "timed send should succeed");

    expectEqSize(test.receivedCount(kNodeB), 0,
                 "receiver should not deliver before simulation time progresses");

    test.scenario().step(20);
    expectEqSize(test.receivedCount(kNodeB), 0,
                 "receiver should not deliver before tx duration elapses");

    expectTrue(test.waitForMessageCount(kNodeB, 1, 1200, 10, 1),
               "receiver should deliver once tx duration is elapsed");
    expectTrue(test.hasPayload(kNodeB, payload),
               "timed-delivery payload mismatch");

    test.stop();
}

void testBusyChannelDefersSecondTransmission()
{
    SimulationConfig cfg = makeTimedMacConfig();
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -118.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125005000, 773005000}, 14.0f, -118.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    std::vector<uint8_t> payload_one(40, 0x11); // ~34 ms including network header at 20 kbps
    std::vector<uint8_t> payload_two(40, 0x22); // queued while channel is busy

    int rc = test.sendFromDevice(kNodeA, payload_one);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "first timed send should succeed");

    rc = test.sendFromDevice(kNodeA, payload_two);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "second timed send should succeed");

    test.scenario().step(45);

    expectTrue(test.waitForMessageCount(kNodeB, 1, 300, 0, 1),
               "first transmission should be delivered before deferred second transmission");
    expectEqSize(test.receivedCount(kNodeB), 1,
                 "second transmission should be deferred while channel is busy");

    expectTrue(test.waitForMessageCount(kNodeB, 2, 1200, 10, 1),
               "deferred transmission should eventually deliver");

    expectTrue(test.hasPayload(kNodeB, payload_one),
               "receiver missing first deferred test payload");
    expectTrue(test.hasPayload(kNodeB, payload_two),
               "receiver missing second deferred test payload");

    const SimulationMetricsSnapshot metrics = test.scenario().metrics();
    expectGteU64(metrics.retransmissions, 1,
                 "busy-channel test should record at least one retransmission");

    test.stop();
}

void testCollisionModelDropsSimultaneousTransmissions()
{
    SimulationConfig cfg = makeCollisionConfig();
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -118.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125005000, 773005000}, 14.0f, -118.0f));
    cfg.devices.push_back(makeDevice(kNodeC, GeoPoint{125003000, 773003000}, 14.0f, -118.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload_a = {0xA1, 0xA2, 0xA3, 0xA4};
    const std::vector<uint8_t> payload_b = {0xB1, 0xB2, 0xB3, 0xB4};

    int rc = test.sendFromDevice(kNodeA, payload_a);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                "collision source A send should succeed");

    rc = test.sendFromDevice(kNodeB, payload_b);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                "collision source B send should succeed");

    test.stepUntil([]() { return false; }, 600, 10, 1);

    expectEqSize(test.receivedCount(kNodeC), 0,
                 "receiver should drop overlapping simultaneous transmissions");

    const SimulationMetricsSnapshot metrics = test.scenario().metrics();
    expectGteU64(metrics.tx_fail_collision, 2,
                 "collision model should count collision failures for both transmitters");
    expectGteSize(static_cast<size_t>(metrics.rx_dropped), 2,
                  "collision model should account dropped receptions");

    test.stop();
}

void testPropagationDelayDeliversNearBeforeFar()
{
    SimulationConfig cfg = makePhase4EffectsConfig();
    cfg.devices.push_back(makeDevice(kNodeA, GeoPoint{0, 0}, 30.0f, -200.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{0, 10000}, 14.0f, -200.0f));
    cfg.devices.push_back(makeDevice(kNodeC, GeoPoint{0, 1790000000}, 14.0f, -200.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload = {0x31, 0x32, 0x33};
    const int rc = test.sendFromDevice(kNodeA,
                                       payload,
                                       Priority::NORMAL,
                                       PropagationMode::OMNI,
                                       0,
                                       0,
                                       65535,
                                       30);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                "propagation-delay send should succeed");

    expectTrue(test.waitForMessageCount(kNodeB, 1, 600, 5, 1),
               "near node should receive first under propagation-delay model");
    expectEqSize(test.receivedCount(kNodeC), 0,
                 "far node should still be pending while near node already delivered");

    expectTrue(test.waitForMessageCount(kNodeC, 1, 1200, 10, 1),
               "far node should receive after longer propagation delay");

    test.stop();
}

void testPerThresholdModelDropAndAllow()
{
    SimulationConfig cfg = makePhase4EffectsConfig();
    cfg.runtime.per_model = SimulationRuntimeConfig::PerModel::Threshold;
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -200.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125005000, 773005000}, 14.0f, -200.0f));

    SimulationBuilder builder;

    cfg.runtime.snr_threshold_db = 80.0f;
    builder.setConfig(cfg);

    {
        SimulationTestBase test(builder.build());
        test.start();

        const std::vector<uint8_t> payload = {0x41, 0x42, 0x43};
        const int rc = test.sendFromDevice(kNodeA,
                                           payload,
                                           Priority::NORMAL,
                                           PropagationMode::OMNI,
                                           0,
                                           0,
                                           10000,
                                           30);
        expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                    "PER-threshold drop send should succeed");

        test.stepUntil([]() { return false; }, 500, 10, 1);
        expectEqSize(test.receivedCount(kNodeB), 0,
                     "high SNR threshold should drop frame in PER threshold mode");

        const SimulationMetricsSnapshot metrics = test.scenario().metrics();
        expectGteU64(metrics.tx_fail_per, 1,
                     "PER threshold drop should increment tx_fail_per metric");

        test.stop();
    }

    cfg.runtime.snr_threshold_db = -20.0f;
    builder.setConfig(cfg);

    {
        SimulationTestBase test(builder.build());
        test.start();

        const std::vector<uint8_t> payload = {0x51, 0x52, 0x53};
        const int rc = test.sendFromDevice(kNodeA,
                                           payload,
                                           Priority::NORMAL,
                                           PropagationMode::OMNI,
                                           0,
                                           0,
                                           10000,
                                           30);
        expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                    "PER-threshold allow send should succeed");

        expectTrue(test.waitForMessageCount(kNodeB, 1, 700, 10, 1),
                   "lower SNR threshold should allow PER-threshold delivery");

        test.stop();
    }
}

void testChannelUtilizationMetricsIncreaseAfterTraffic()
{
    SimulationConfig cfg = makePhase4EffectsConfig();
    cfg.runtime.enable_congestion_drops = false;
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -118.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125005000, 773005000}, 14.0f, -118.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload(80, 0x66);
    const int rc = test.sendFromDevice(kNodeA, payload);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                "utilization test send should succeed");

    expectTrue(test.waitForMessageCount(kNodeB, 1, 800, 5, 1),
               "utilization test receiver should get payload");

    const SimulationMetricsSnapshot metrics = test.scenario().metrics();
    expectTrue(metrics.channel_utilization_pct > 0.0,
               "channel utilization should increase after timed transmission");

    test.stop();
}

void testCongestionDropModeCanForceDrops()
{
    SimulationConfig cfg = makeCongestionConfig();
    cfg.devices.push_back(makeDevice(kNodeA, kBasePoint, 14.0f, -118.0f));
    cfg.devices.push_back(makeDevice(kNodeB, GeoPoint{125005000, 773005000}, 14.0f, -118.0f));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload(32, 0x77);
    const int rc = test.sendFromDevice(kNodeA, payload);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                "congestion-drop test send should succeed");

    test.stepUntil([]() { return false; }, 600, 10, 1);
    expectEqSize(test.receivedCount(kNodeB), 0,
                 "forced congestion-drop configuration should suppress delivery");

    const SimulationMetricsSnapshot metrics = test.scenario().metrics();
    expectGteU64(metrics.rx_dropped, 1,
                 "congestion-drop test should increment dropped reception count");

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
    failures += runTest("timed_delivery_requires_progressed_virtual_time",
                        testTimedDeliveryRequiresProgressedVirtualTime);
    failures += runTest("busy_channel_defers_second_transmission",
                        testBusyChannelDefersSecondTransmission);
    failures += runTest("collision_model_drops_simultaneous_transmissions",
                        testCollisionModelDropsSimultaneousTransmissions);
    failures += runTest("propagation_delay_delivers_near_before_far",
                        testPropagationDelayDeliversNearBeforeFar);
    failures += runTest("per_threshold_model_drop_and_allow",
                        testPerThresholdModelDropAndAllow);
    failures += runTest("channel_utilization_metrics_increase_after_traffic",
                        testChannelUtilizationMetricsIncreaseAfterTraffic);
    failures += runTest("congestion_drop_mode_can_force_drops",
                        testCongestionDropModeCanForceDrops);

    if (failures == 0) {
        std::printf("All phase 3 simulation tests passed\n");
        return 0;
    }

    std::printf("Phase 3 simulation tests failed (%d)\n", failures);
    return 1;
}
