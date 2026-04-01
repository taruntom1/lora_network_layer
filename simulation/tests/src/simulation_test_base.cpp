#include "simulation_test_base.h"

#include <chrono>
#include <stdexcept>
#include <thread>

/**
 * @file simulation_test_base.cpp
 * @ingroup sim_internal
 * @brief Internal mechanics for capture callback wiring and stepped waiting helpers.
 */

SimulationTestBase::SimulationTestBase(std::unique_ptr<SimulationScenario> scenario)
    : scenario_(std::move(scenario))
{
    if (!scenario_) {
        throw std::runtime_error("SimulationTestBase requires a valid scenario");
    }
}

SimulationTestBase::~SimulationTestBase()
{
    stop();
}

void SimulationTestBase::start()
{
    if (started_) {
        return;
    }

    installCaptureCallbacks();
    scenario_->start();
    started_ = true;
}

void SimulationTestBase::stop()
{
    if (!started_) {
        return;
    }

    scenario_->stop();
    started_ = false;
}

int SimulationTestBase::sendFromDevice(uint16_t src_node_id,
                                       const uint8_t* payload,
                                       size_t payload_len,
                                       Priority priority,
                                       PropagationMode mode,
                                       uint16_t target_heading,
                                       uint8_t max_hops,
                                       uint16_t max_distance_m,
                                       uint16_t lifetime_s)
{
    NetworkManager* mgr = scenario_->manager(src_node_id);
    if (!mgr) {
        throw std::runtime_error("Unknown source node_id for sendFromDevice");
    }

    return mgr->sendMessage(priority,
                            mode,
                            target_heading,
                            max_hops,
                            max_distance_m,
                            lifetime_s,
                            payload,
                            payload_len);
}

int SimulationTestBase::sendFromDevice(uint16_t src_node_id,
                                       const std::vector<uint8_t>& payload,
                                       Priority priority,
                                       PropagationMode mode,
                                       uint16_t target_heading,
                                       uint8_t max_hops,
                                       uint16_t max_distance_m,
                                       uint16_t lifetime_s)
{
    return sendFromDevice(src_node_id,
                          payload.data(),
                          payload.size(),
                          priority,
                          mode,
                          target_heading,
                          max_hops,
                          max_distance_m,
                          lifetime_s);
}

void SimulationTestBase::clearCaptured()
{
    std::lock_guard<std::mutex> lock(capture_mutex_);
    received_.clear();
}

size_t SimulationTestBase::receivedCount(uint16_t node_id) const
{
    std::lock_guard<std::mutex> lock(capture_mutex_);
    const auto it = received_.find(node_id);
    if (it == received_.end()) {
        return 0;
    }
    return it->second.size();
}

std::vector<CapturedMessage> SimulationTestBase::receivedMessages(uint16_t node_id) const
{
    std::lock_guard<std::mutex> lock(capture_mutex_);
    const auto it = received_.find(node_id);
    if (it == received_.end()) {
        return {};
    }
    return it->second;
}

bool SimulationTestBase::waitForMessageCount(uint16_t node_id,
                                             size_t min_count,
                                             uint64_t timeout_ms,
                                             uint64_t step_ms,
                                             uint64_t idle_sleep_ms)
{
    return stepUntil(
        [this, node_id, min_count]() { return receivedCount(node_id) >= min_count; },
        timeout_ms,
        step_ms,
        idle_sleep_ms);
}

bool SimulationTestBase::stepUntil(const std::function<bool()>& predicate,
                                   uint64_t timeout_ms,
                                   uint64_t step_ms,
                                   uint64_t idle_sleep_ms)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }

        scenario_->step(step_ms);

        if (idle_sleep_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(idle_sleep_ms));
        }
    }

    return predicate();
}

bool SimulationTestBase::hasPayload(uint16_t node_id, const std::vector<uint8_t>& payload) const
{
    const std::vector<CapturedMessage> messages = receivedMessages(node_id);
    for (const CapturedMessage& msg : messages) {
        if (msg.payload == payload) {
            return true;
        }
    }
    return false;
}

SimulationScenario& SimulationTestBase::scenario()
{
    return *scenario_;
}

const SimulationScenario& SimulationTestBase::scenario() const
{
    return *scenario_;
}

void SimulationTestBase::installCaptureCallbacks()
{
    if (callbacks_installed_) {
        return;
    }

    const std::vector<uint16_t> node_ids = scenario_->nodeIds();
    for (uint16_t node_id : node_ids) {
        NetworkManager* mgr = scenario_->manager(node_id);
        if (!mgr) {
            continue;
        }

        mgr->setAppRxCallback([this, node_id](const NetworkHeader& hdr,
                                              const uint8_t* payload,
                                              size_t payload_len) {
            CapturedMessage msg;
            msg.header = hdr;
            msg.payload.assign(payload, payload + payload_len);

            std::lock_guard<std::mutex> lock(capture_mutex_);
            received_[node_id].push_back(std::move(msg));
        });
    }

    callbacks_installed_ = true;
}
