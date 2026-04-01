#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "simulation_test_base.h"

namespace {

constexpr uint16_t kNodeBase = 0x5000;
constexpr GeoPoint kBasePoint{125000000, 773000000};

void expectTrue(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
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

SimulationDeviceConfig makeDevice(uint16_t node_id,
                                  GeoPoint position,
                                  float tx_power_dbm,
                                  float sensitivity_dbm,
                                  uint16_t speed_cm_s = 0,
                                  uint16_t heading_cdeg = 0)
{
    SimulationDeviceConfig cfg;
    cfg.node_id = node_id;
    cfg.initial_position = position;
    cfg.speed_cm_s = speed_cm_s;
    cfg.heading_cdeg = heading_cdeg;
    cfg.radio.tx_power_dbm = tx_power_dbm;
    cfg.radio.noise_floor_dbm = -110.0f;
    cfg.radio.sensitivity_dbm = sensitivity_dbm;
    return cfg;
}

SimulationConfig makeStressConfig(size_t node_count,
                                  uint16_t spacing_e7 = 8000,
                                  uint16_t speed_cm_s = 0)
{
    SimulationConfig cfg;
    cfg.runtime.carrier_freq_mhz = 868.0f;
    cfg.runtime.start_time_s = 2000;
    cfg.runtime.network_config = NetworkConfig{256, 8, 256};

    // Source node at the center of the grid.
    cfg.devices.push_back(makeDevice(kNodeBase, kBasePoint, 14.0f, -118.0f, speed_cm_s, 0));

    size_t added = 1;
    size_t ring = 1;
    while (added < node_count) {
        for (int dy = -static_cast<int>(ring); dy <= static_cast<int>(ring) && added < node_count; ++dy) {
            for (int dx = -static_cast<int>(ring); dx <= static_cast<int>(ring) && added < node_count; ++dx) {
                if (std::abs(dx) != static_cast<int>(ring) &&
                    std::abs(dy) != static_cast<int>(ring)) {
                    continue;
                }

                if (dx == 0 && dy == 0) {
                    continue;
                }

                GeoPoint pos{
                    kBasePoint.lat + dy * static_cast<int32_t>(spacing_e7),
                    kBasePoint.lon + dx * static_cast<int32_t>(spacing_e7),
                };

                const uint16_t node_id = static_cast<uint16_t>(kNodeBase + added);
                const uint16_t heading_cdeg = static_cast<uint16_t>(((added * 137U) % 360U) * 100U);
                cfg.devices.push_back(makeDevice(node_id,
                                                 pos,
                                                 14.0f,
                                                 -118.0f,
                                                 speed_cm_s,
                                                 heading_cdeg));
                ++added;
            }
        }
        ++ring;
    }

    return cfg;
}

using Snapshot = std::unordered_map<uint16_t, std::array<uint64_t, 2>>;

struct StochasticRunSummary {
    uint64_t received_count{0};
    uint64_t received_hash{0};
    uint64_t tx_fail_per{0};
};

Snapshot captureSnapshot(const SimulationTestBase& test)
{
    Snapshot out;
    const std::vector<uint16_t> node_ids = test.scenario().nodeIds();

    for (uint16_t node_id : node_ids) {
        const std::vector<CapturedMessage> msgs = test.receivedMessages(node_id);

        uint64_t hash = 1469598103934665603ULL; // FNV-1a offset basis
        for (const CapturedMessage& msg : msgs) {
            hash ^= static_cast<uint64_t>(msg.header.message_id);
            hash *= 1099511628211ULL;

            hash ^= static_cast<uint64_t>(msg.header.timestamp);
            hash *= 1099511628211ULL;

            for (uint8_t b : msg.payload) {
                hash ^= static_cast<uint64_t>(b);
                hash *= 1099511628211ULL;
            }
        }

        out[node_id] = {
            static_cast<uint64_t>(msgs.size()),
            hash,
        };
    }

    return out;
}

Snapshot runRepeatableWorkload()
{
    SimulationBuilder builder;
    builder.setConfig(makeStressConfig(20, 10000, 120));

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> msg_a = {1, 2, 3, 4, 5};
    const std::vector<uint8_t> msg_b = {9, 8, 7};

    for (size_t i = 0; i < 6; ++i) {
        const int rc = test.sendFromDevice(kNodeBase,
                                           msg_a,
                                           Priority::NORMAL,
                                           PropagationMode::OMNI,
                                           0,
                                           0,
                                           3000,
                                           30);
        expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                    "repeatability source send should succeed");
        test.scenario().step(120);
    }

    const uint16_t second_sender = static_cast<uint16_t>(kNodeBase + 1);
    for (size_t i = 0; i < 4; ++i) {
        const int rc = test.sendFromDevice(second_sender,
                                           msg_b,
                                           Priority::HIGH,
                                           PropagationMode::OMNI,
                                           0,
                                           0,
                                           3000,
                                           30);
        expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                    "repeatability secondary send should succeed");
        test.scenario().step(120);
    }

    const std::vector<uint16_t> node_ids = test.scenario().nodeIds();
    for (uint16_t node_id : node_ids) {
        if (node_id == kNodeBase) {
            expectTrue(test.waitForMessageCount(node_id, 4, 3000),
                       "source node should receive four messages from secondary sender");
            continue;
        }

        if (node_id == second_sender) {
            expectTrue(test.waitForMessageCount(node_id, 6, 3000),
                       "secondary sender should receive six messages from source sender");
            continue;
        }

        expectTrue(test.waitForMessageCount(node_id, 10, 3000),
                   "receiver node should receive all workload messages");
    }

    Snapshot snapshot = captureSnapshot(test);
    test.stop();
    return snapshot;
}

SimulationConfig makeStochasticPerConfig(uint64_t seed)
{
    SimulationConfig cfg;
    cfg.runtime.carrier_freq_mhz = 868.0f;
    cfg.runtime.start_time_s = 2200;
    cfg.runtime.network_config = NetworkConfig{128, 8, 64};
    cfg.runtime.compatibility_immediate_delivery = false;
    cfg.runtime.random_seed = seed;
    cfg.runtime.data_rate_bps = 400000;
    cfg.runtime.slot_time_us = 1000;
    cfg.runtime.difs_us = 0;
    cfg.runtime.cw_min = 1;
    cfg.runtime.cw_max = 15;
    cfg.runtime.max_retries = 4;
    cfg.runtime.propagation_min_delay_us = 0;
    cfg.runtime.enable_collision_model = false;
    cfg.runtime.enable_congestion_drops = false;
    cfg.runtime.per_model = SimulationRuntimeConfig::PerModel::Logistic;
    cfg.runtime.per_logistic_k = 0.9f;
    cfg.runtime.per_logistic_mid_db = 10.0f;
    cfg.runtime.fading_stddev_db = 1.0f;
    cfg.runtime.noise_jitter_db = 0.5f;

    cfg.devices.push_back(makeDevice(kNodeBase,
                                     kBasePoint,
                                     14.0f,
                                     -120.0f,
                                     0,
                                     0));

    // ~13.3 km north gives SNR near the logistic midpoint for meaningful randomness.
    cfg.devices.push_back(makeDevice(static_cast<uint16_t>(kNodeBase + 1),
                                     GeoPoint{126200000, 773000000},
                                     14.0f,
                                     -120.0f,
                                     0,
                                     0));
    return cfg;
}

StochasticRunSummary runStochasticPerWorkload(uint64_t seed)
{
    SimulationBuilder builder;
    builder.setConfig(makeStochasticPerConfig(seed));

    SimulationTestBase test(builder.build());
    test.start();

    const uint16_t src = kNodeBase;
    const uint16_t dst = static_cast<uint16_t>(kNodeBase + 1);
    constexpr size_t kMessageCount = 80;

    for (size_t i = 0; i < kMessageCount; ++i) {
        const std::vector<uint8_t> payload = {
            static_cast<uint8_t>(i & 0xFF),
            static_cast<uint8_t>((i * 3) & 0xFF),
            0xAA,
            0x55,
        };

        const int rc = test.sendFromDevice(src,
                                           payload,
                                           Priority::NORMAL,
                                           PropagationMode::OMNI,
                                           0,
                                           0,
                                           60000,
                                           60);
        expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                    "stochastic PER workload send should succeed");
        test.scenario().step(6);
    }

    test.stepUntil([]() { return false; }, 2000, 10, 1);

    const std::vector<CapturedMessage> msgs = test.receivedMessages(dst);
    uint64_t hash = 1469598103934665603ULL;
    for (const CapturedMessage& msg : msgs) {
        hash ^= static_cast<uint64_t>(msg.header.message_id);
        hash *= 1099511628211ULL;
        for (uint8_t b : msg.payload) {
            hash ^= static_cast<uint64_t>(b);
            hash *= 1099511628211ULL;
        }
    }

    const SimulationMetricsSnapshot metrics = test.scenario().metrics();
    StochasticRunSummary summary;
    summary.received_count = static_cast<uint64_t>(msgs.size());
    summary.received_hash = hash;
    summary.tx_fail_per = metrics.tx_fail_per;

    test.stop();
    return summary;
}

void testStochasticPerSameSeedIsRepeatable()
{
    const StochasticRunSummary first = runStochasticPerWorkload(1337);
    const StochasticRunSummary second = runStochasticPerWorkload(1337);

    expectTrue(first.received_count == second.received_count,
               "same-seed stochastic workload should keep identical receive counts");
    expectTrue(first.received_hash == second.received_hash,
               "same-seed stochastic workload should keep identical receive hashes");
    expectTrue(first.tx_fail_per == second.tx_fail_per,
               "same-seed stochastic workload should keep identical PER-failure counters");
}

void testStochasticPerDifferentSeedsDiverge()
{
    const StochasticRunSummary seed_a = runStochasticPerWorkload(1001);
    const StochasticRunSummary seed_b = runStochasticPerWorkload(2002);

    const bool count_differs = seed_a.received_count != seed_b.received_count;
    const bool hash_differs = seed_a.received_hash != seed_b.received_hash;
    const bool per_differs = seed_a.tx_fail_per != seed_b.tx_fail_per;

    expectTrue(count_differs || hash_differs || per_differs,
               "different seeds should diverge in stochastic PER outcomes");
}

void testCongestionThresholdPreventsDropsUnderLightLoad()
{
    SimulationConfig cfg;
    cfg.runtime.carrier_freq_mhz = 868.0f;
    cfg.runtime.start_time_s = 2400;
    cfg.runtime.network_config = NetworkConfig{128, 8, 64};
    cfg.runtime.compatibility_immediate_delivery = false;
    cfg.runtime.random_seed = 7;
    cfg.runtime.data_rate_bps = 1000000;
    cfg.runtime.slot_time_us = 1000;
    cfg.runtime.difs_us = 0;
    cfg.runtime.cw_min = 1;
    cfg.runtime.cw_max = 15;
    cfg.runtime.max_retries = 4;
    cfg.runtime.enable_collision_model = false;
    cfg.runtime.enable_congestion_drops = true;
    cfg.runtime.congestion_utilization_threshold_pct = 99.0f;
    cfg.runtime.congestion_drop_probability = 1.0f;
    cfg.runtime.congestion_min_elapsed_us = 500000;
    cfg.runtime.per_model = SimulationRuntimeConfig::PerModel::Disabled;

    cfg.devices.push_back(makeDevice(kNodeBase, kBasePoint, 14.0f, -118.0f, 0, 0));
    cfg.devices.push_back(makeDevice(static_cast<uint16_t>(kNodeBase + 1),
                                     GeoPoint{125002000, 773001000},
                                     14.0f,
                                     -118.0f,
                                     0,
                                     0));

    SimulationBuilder builder;
    builder.setConfig(cfg);

    SimulationTestBase test(builder.build());
    test.start();

    const uint16_t src = kNodeBase;
    const uint16_t dst = static_cast<uint16_t>(kNodeBase + 1);
    constexpr size_t kMessageCount = 20;

    for (size_t i = 0; i < kMessageCount; ++i) {
        const std::vector<uint8_t> payload = {
            0xE0,
            static_cast<uint8_t>(i & 0xFF),
        };

        const int rc = test.sendFromDevice(src,
                                           payload,
                                           Priority::NORMAL,
                                           PropagationMode::OMNI,
                                           0,
                                           0,
                                           5000,
                                           30);
        expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                    "light-load congestion test send should succeed");
        test.scenario().step(50);
    }

    expectTrue(test.waitForMessageCount(dst, kMessageCount, 3000, 20, 1),
               "light-load utilization should stay below threshold and avoid congestion drops");

    const SimulationMetricsSnapshot metrics = test.scenario().metrics();
    expectEqSize(test.receivedCount(dst), kMessageCount,
                 "light-load congestion configuration should keep all deliveries");
    expectTrue(metrics.channel_utilization_pct < 99.0,
               "light-load utilization should remain below congestion threshold");

    test.stop();
}

void testLargeScaleBroadcastBurstNoLoss()
{
    SimulationBuilder builder;
    builder.setConfig(makeStressConfig(80, 8000, 0));

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload = {0x10, 0x20, 0x30, 0x40, 0x50};
    constexpr size_t kBurstCount = 25;

    for (size_t i = 0; i < kBurstCount; ++i) {
        const int rc = test.sendFromDevice(kNodeBase,
                                           payload,
                                           Priority::NORMAL,
                                           PropagationMode::OMNI,
                                           0,
                                           0,
                                           3000,
                                           60);
        expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                    "stress burst send should succeed");
    }

    const std::vector<uint16_t> node_ids = test.scenario().nodeIds();
    for (uint16_t node_id : node_ids) {
        if (node_id == kNodeBase) {
            expectEqSize(test.receivedCount(node_id), 0,
                         "source should not receive its own broadcasts");
            continue;
        }

        expectTrue(test.waitForMessageCount(node_id, kBurstCount, 5000),
                   "receiver missed broadcast messages during stress burst");
        expectEqSize(test.receivedCount(node_id), kBurstCount,
                     "receiver message count mismatch after stress burst");
    }

    test.stop();
}

void testRepeatabilitySnapshotStableAcrossRuns()
{
    const Snapshot first = runRepeatableWorkload();
    const Snapshot second = runRepeatableWorkload();

    expectEqSize(first.size(), second.size(),
                 "repeatability runs should produce same node set size");

    for (const auto& [node_id, summary] : first) {
        const auto it = second.find(node_id);
        expectTrue(it != second.end(), "missing node summary in second run");

        const auto& second_summary = it->second;
        expectTrue(summary[0] == second_summary[0],
                   "message count mismatch between repeatability runs");
        expectTrue(summary[1] == second_summary[1],
                   "message content/hash mismatch between repeatability runs");
    }
}

void testHundredNodeStepBudget()
{
    SimulationBuilder builder;
    builder.setConfig(makeStressConfig(100, 8500, 60));

    SimulationTestBase test(builder.build());
    test.start();

    const auto start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < 1000; ++i) {
        test.scenario().step(20);

        if (i % 100 == 0) {
            const std::vector<uint8_t> payload = {
                static_cast<uint8_t>(i & 0xFF),
                static_cast<uint8_t>((i / 2) & 0xFF),
            };

            const int rc = test.sendFromDevice(kNodeBase,
                                               payload,
                                               Priority::NORMAL,
                                               PropagationMode::OMNI,
                                               0,
                                               0,
                                               3000,
                                               30);
            expectEqInt(rc, static_cast<int>(NetworkError::Ok),
                        "budget test send should succeed");
        }
    }

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();

    const uint16_t probe_node = static_cast<uint16_t>(kNodeBase + 1);
    expectTrue(test.waitForMessageCount(probe_node, 10, 5000),
               "probe node did not receive expected budget-test messages");

    expectTrue(elapsed_ms <= 8000,
               "100-node, 1000-step workload exceeded phase-5 time budget");

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

    failures += runTest("large_scale_broadcast_burst_no_loss", testLargeScaleBroadcastBurstNoLoss);
    failures += runTest("repeatability_snapshot_stable_across_runs",
                        testRepeatabilitySnapshotStableAcrossRuns);
    failures += runTest("hundred_node_step_budget", testHundredNodeStepBudget);
    failures += runTest("stochastic_per_same_seed_is_repeatable",
                        testStochasticPerSameSeedIsRepeatable);
    failures += runTest("stochastic_per_different_seeds_diverge",
                        testStochasticPerDifferentSeedsDiverge);
    failures += runTest("congestion_threshold_prevents_drops_under_light_load",
                        testCongestionThresholdPreventsDropsUnderLightLoad);

    if (failures == 0) {
        std::printf("All phase 5 simulation tests passed\n");
        return 0;
    }

    std::printf("Phase 5 simulation tests failed (%d)\n", failures);
    return 1;
}
