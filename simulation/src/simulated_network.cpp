#include "simulated_network.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "geo_utils.h"
#include "network_header.h"
#include "simulated_link_layer.h"
#include "simulated_location_provider.h"

/**
 * @file simulated_network.cpp
 * @ingroup sim_internal
 * @brief Internal implementation of channel fan-out, propagation filtering, and delivery.
 *
 * Implementation notes:
 * - Registry access is protected by a mutex.
 * - Transmission creates a read-only snapshot while holding the lock, then performs
 *   downstream delivery after lock release to avoid callback-induced lock contention.
 * - Propagation checks use free-space path loss with a minimum distance clamp.
 */

namespace {

constexpr float kMinDistanceM = 1.0f;
constexpr double kSpeedOfLightMps = 299792458.0;
constexpr double kInv2Pow53 = 1.0 / 9007199254740992.0; // 2^53

constexpr uint8_t kPerModelDisabled = 0;
constexpr uint8_t kPerModelThreshold = 1;
constexpr uint8_t kPerModelLogistic = 2;

struct SnapshotEntry {
    uint16_t node_id;
    SimulatedLinkLayer* link;
    GeoPoint location;
    SimulatedNetwork::RadioConfig cfg;
};

} // namespace

SimulatedNetwork::SimulatedNetwork(float carrier_freq_mhz,
                                   bool compatibility_immediate_delivery)
    : carrier_freq_mhz_(carrier_freq_mhz)
    , compatibility_immediate_delivery_(compatibility_immediate_delivery)
{
}

void SimulatedNetwork::configureMac(uint32_t data_rate_bps,
                                    uint32_t slot_time_us,
                                    uint32_t difs_us,
                                    uint8_t max_retries,
                                    uint32_t propagation_min_delay_us,
                                    uint16_t cw_min,
                                    uint16_t cw_max,
                                    uint64_t random_seed,
                                    bool enable_collision_model,
                                    uint8_t per_model,
                                    float snr_threshold_db,
                                    float per_logistic_k,
                                    float per_logistic_mid_db,
                                    float fading_stddev_db,
                                    float noise_jitter_db,
                                    bool enable_congestion_drops,
                                    float congestion_utilization_threshold_pct,
                                    float congestion_drop_probability,
                                    uint32_t congestion_min_elapsed_us)
{
    data_rate_bps_ = (data_rate_bps == 0) ? 1U : data_rate_bps;
    slot_time_us_ = slot_time_us;
    difs_us_ = difs_us;
    max_retries_ = max_retries;
    propagation_min_delay_us_ = propagation_min_delay_us;
    cw_min_ = cw_min;
    cw_max_ = std::max<uint16_t>(cw_max, cw_min_);
    random_seed_ = random_seed;
    enable_collision_model_ = enable_collision_model;
    per_model_ = per_model;
    snr_threshold_db_ = snr_threshold_db;
    per_logistic_k_ = per_logistic_k;
    per_logistic_mid_db_ = per_logistic_mid_db;
    fading_stddev_db_ = fading_stddev_db;
    noise_jitter_db_ = noise_jitter_db;
    enable_congestion_drops_ = enable_congestion_drops;
    congestion_utilization_threshold_pct_ =
        std::max(0.0f, std::min(100.0f, congestion_utilization_threshold_pct));
    congestion_drop_probability_ =
        std::max(0.0f, std::min(1.0f, congestion_drop_probability));
    congestion_min_elapsed_us_ = congestion_min_elapsed_us;
}

void SimulatedNetwork::registerNode(uint16_t node_id,
                                    SimulatedLinkLayer* link,
                                    const SimulatedLocationProvider* location)
{
    const RadioConfig default_cfg{};
    registerNode(node_id, link, location, default_cfg);
}

void SimulatedNetwork::registerNode(uint16_t node_id,
                                    SimulatedLinkLayer* link,
                                    const SimulatedLocationProvider* location,
                                    const RadioConfig& cfg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_[node_id] = NodeEntry{link, location, cfg};
    cw_state_by_node_[node_id] = cw_min_;
}

void SimulatedNetwork::unregisterNode(uint16_t node_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_.erase(node_id);
    cw_state_by_node_.erase(node_id);
}

size_t SimulatedNetwork::nodeCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.size();
}

void SimulatedNetwork::transmit(uint16_t src_node_id,
                                uint16_t dst_id,
                                const uint8_t* data,
                                size_t len)
{
    if (!data || len == 0 || len > LORA_MAX_PAYLOAD) {
        return;
    }

    metrics_.onTxAttempt();

    SimEvent event;
    event.time_us = now_us_.load();
    event.priority = 2;
    event.sequence_id = nextSequenceId();
    event.tx_id = nextSequenceId();
    event.type = compatibility_immediate_delivery_ ? SimEventType::RxDelivery : SimEventType::TxStart;
    event.src_node_id = src_node_id;
    event.dst_id = dst_id;
    event.retry_count = 0;
    event.payload.assign(data, data + len);

    event_queue_.push(std::move(event));

    if (compatibility_immediate_delivery_) {
        processUntil(now_us_.load());
    }
}

void SimulatedNetwork::setNowUs(uint64_t now_us)
{
    now_us_.store(now_us);
}

uint64_t SimulatedNetwork::nowUs() const
{
    return now_us_.load();
}

void SimulatedNetwork::processUntil(uint64_t horizon_us)
{
    SimEvent event;
    while (event_queue_.popDue(horizon_us, event)) {
        now_us_.store(event.time_us);
        switch (event.type) {
        case SimEventType::TxStart:
            executeTxStartEvent(event);
            break;
        case SimEventType::TxEnd:
            executeTxEndEvent(event);
            break;
        case SimEventType::RetryBackoff:
            executeRetryBackoffEvent(event);
            break;
        case SimEventType::RxDelivery:
            executeDeliveryEvent(event);
            break;
        }
    }

    now_us_.store(horizon_us);
}

SimulationMetricsSnapshot SimulatedNetwork::metricsSnapshot(uint64_t sim_now_us) const
{
    return metrics_.snapshot(sim_now_us);
}

void SimulatedNetwork::resetMetrics(uint64_t now_us)
{
    metrics_.reset(now_us);
    std::lock_guard<std::mutex> lock(mutex_);
    channel_busy_until_us_ = now_us;
    channel_busy_accumulated_us_ = 0;
    utilization_start_time_us_ = now_us;
    active_transmissions_.clear();
    tx_delivery_state_.clear();
    for (auto& [node_id, cw_current] : cw_state_by_node_) {
        (void)node_id;
        cw_current = cw_min_;
    }
}

void SimulatedNetwork::executeTxStartEvent(const SimEvent& event)
{
    const uint64_t tx_start_us = event.time_us;
    const uint64_t tx_duration_us = computeTxDurationUs(event.payload.size());
    const uint64_t tx_end_us = tx_start_us + tx_duration_us;

    uint64_t busy_until_snapshot = 0;
    bool allow_collision_start = false;
    std::vector<std::pair<uint16_t, float>> recipients;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto src_it = nodes_.find(event.src_node_id);
        if (src_it == nodes_.end()) {
            metrics_.onTxOutcome(false);
            return;
        }

        const GeoPoint src_location = src_it->second.location->getLocation();
        recipients.reserve(nodes_.size());
        for (const auto& [node_id, entry] : nodes_) {
            if (node_id == event.src_node_id) {
                continue;
            }
            if (event.dst_id != BROADCAST_ADDR && event.dst_id != node_id) {
                continue;
            }

            float distance_m = geo::haversine_m(src_location, entry.location->getLocation());
            distance_m = std::max(distance_m, kMinDistanceM);
            recipients.emplace_back(node_id, distance_m);
        }

        busy_until_snapshot = channel_busy_until_us_;

        if (tx_start_us < busy_until_snapshot && enable_collision_model_ && event.retry_count == 0) {
            for (const auto& [active_tx_id, active_tx] : active_transmissions_) {
                (void)active_tx_id;
                if (active_tx.start_us == tx_start_us && active_tx.end_us > tx_start_us) {
                    allow_collision_start = true;
                    break;
                }
            }
        }
    }

    if (tx_start_us < busy_until_snapshot && !allow_collision_start) {
        if (event.retry_count >= max_retries_) {
            metrics_.onTxOutcome(false);
            return;
        }

        uint16_t next_cw = cw_min_;
        uint64_t backoff_slots = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const uint16_t current_cw = getOrInitCwState(event.src_node_id);
            next_cw = static_cast<uint16_t>(std::min<uint32_t>(
                static_cast<uint32_t>(cw_max_),
                static_cast<uint32_t>(current_cw) * 2U + 1U));
            cw_state_by_node_[event.src_node_id] = next_cw;
            backoff_slots = computeBackoffSlots(event.src_node_id,
                                                event.retry_count,
                                                event.tx_id,
                                                next_cw);
        }

        SimEvent retry = event;
        retry.type = SimEventType::RetryBackoff;
        retry.time_us = busy_until_snapshot + difs_us_ + backoff_slots * slot_time_us_;
        retry.priority = 1;
        retry.retry_count = static_cast<uint8_t>(event.retry_count + 1);
        retry.sequence_id = nextSequenceId();

        metrics_.onRetransmission();
        event_queue_.push(std::move(retry));
        return;
    }

    bool tx_collided = false;
    std::vector<uint64_t> overlapped_tx_ids;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enable_collision_model_) {
            for (const auto& [active_tx_id, active_tx] : active_transmissions_) {
                if (tx_start_us < active_tx.end_us && tx_end_us > active_tx.start_us) {
                    overlapped_tx_ids.push_back(active_tx_id);
                }
            }

            if (!overlapped_tx_ids.empty()) {
                tx_collided = true;
                for (uint64_t active_tx_id : overlapped_tx_ids) {
                    auto it = active_transmissions_.find(active_tx_id);
                    if (it != active_transmissions_.end()) {
                        it->second.collided = true;
                    }

                    auto state_it = tx_delivery_state_.find(active_tx_id);
                    if (state_it != tx_delivery_state_.end()) {
                        state_it->second.collided = true;
                    }
                }
            }
        }

        if (tx_end_us > channel_busy_until_us_) {
            channel_busy_until_us_ = tx_end_us;
        }

        channel_busy_accumulated_us_ += tx_duration_us;

        active_transmissions_[event.tx_id] = ActiveTransmission{
            event.tx_id,
            event.src_node_id,
            tx_start_us,
            tx_end_us,
            tx_collided,
        };
        tx_delivery_state_[event.tx_id] = TxDeliveryState{
            recipients.size(),
            false,
            false,
            tx_collided,
            tx_end_us,
        };

        resetCwState(event.src_node_id);
    }

    metrics_.addChannelBusyTime(tx_duration_us);

    SimEvent tx_end;
    tx_end.time_us = tx_end_us;
    tx_end.priority = 0;
    tx_end.sequence_id = nextSequenceId();
    tx_end.type = SimEventType::TxEnd;
    tx_end.tx_id = event.tx_id;
    tx_end.src_node_id = event.src_node_id;
    tx_end.dst_id = event.dst_id;
    event_queue_.push(std::move(tx_end));

    if (recipients.empty()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tx_delivery_state_.erase(event.tx_id);
        }

        if (tx_collided) {
            metrics_.onTxCollisionFailure();
        }
        metrics_.onTxOutcome(false);
        return;
    }

    for (const auto& [dst_node_id, distance_m] : recipients) {
        const double delay_us_fp = (static_cast<double>(distance_m) * 1000000.0) / kSpeedOfLightMps;
        const uint64_t propagation_delay_us = std::max<uint64_t>(
            propagation_min_delay_us_,
            static_cast<uint64_t>(delay_us_fp + 0.5));

        SimEvent delivery = event;
        delivery.type = SimEventType::RxDelivery;
        delivery.time_us = tx_end_us + propagation_delay_us;
        delivery.priority = 3;
        delivery.sequence_id = nextSequenceId();
        delivery.tx_id = event.tx_id;
        delivery.dst_id = dst_node_id;
        event_queue_.push(std::move(delivery));
    }
}

void SimulatedNetwork::executeRetryBackoffEvent(const SimEvent& event)
{
    SimEvent retry_start = event;
    retry_start.type = SimEventType::TxStart;
    retry_start.sequence_id = nextSequenceId();
    event_queue_.push(std::move(retry_start));
}

void SimulatedNetwork::executeTxEndEvent(const SimEvent& event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    active_transmissions_.erase(event.tx_id);
    if (event.time_us >= channel_busy_until_us_) {
        channel_busy_until_us_ = event.time_us;
    }
}

void SimulatedNetwork::executeDeliveryEvent(const SimEvent& event)
{
    if (compatibility_immediate_delivery_) {
        const uint8_t* data = event.payload.data();
        const size_t len = event.payload.size();

        SnapshotEntry src{};
        std::vector<SnapshotEntry> recipients;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto src_it = nodes_.find(event.src_node_id);
            if (src_it == nodes_.end()) {
                metrics_.onTxOutcome(false);
                return;
            }

            src = SnapshotEntry{
                event.src_node_id,
                src_it->second.link,
                src_it->second.location->getLocation(),
                src_it->second.cfg,
            };

            recipients.reserve(nodes_.size());
            for (const auto& [node_id, entry] : nodes_) {
                if (node_id == event.src_node_id) {
                    continue;
                }
                if (event.dst_id != BROADCAST_ADDR && event.dst_id != node_id) {
                    continue;
                }

                recipients.push_back(SnapshotEntry{
                    node_id,
                    entry.link,
                    entry.location->getLocation(),
                    entry.cfg,
                });
            }
        }

        bool delivered_any = false;
        for (const SnapshotEntry& dst_entry : recipients) {
            float distance_m = geo::haversine_m(src.location, dst_entry.location);
            distance_m = std::max(distance_m, kMinDistanceM);

            const float rssi_dbm = computeRssiDbm(src.cfg.tx_power_dbm, distance_m, carrier_freq_mhz_);
            if (rssi_dbm < dst_entry.cfg.sensitivity_dbm) {
                metrics_.onRxDropped();
                continue;
            }

            const float snr_db = computeSnrDb(rssi_dbm, dst_entry.cfg.noise_floor_dbm);
            dst_entry.link->deliverFromNetwork(data, len, rssi_dbm, snr_db);
            metrics_.onRxDelivered(0);
            delivered_any = true;
        }

        metrics_.onTxOutcome(delivered_any);
        return;
    }

    const uint8_t* data = event.payload.data();
    const size_t len = event.payload.size();

    SnapshotEntry src{};
    SnapshotEntry dst{};
    bool has_endpoints = false;
    bool collided = false;
    uint64_t tx_end_us = event.time_us;
    bool delivered = false;
    bool per_drop = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto state_it = tx_delivery_state_.find(event.tx_id);
        if (state_it == tx_delivery_state_.end()) {
            return;
        }
        collided = state_it->second.collided;
        tx_end_us = state_it->second.tx_end_us;

        if (!collided) {
            auto src_it = nodes_.find(event.src_node_id);
            auto dst_it = nodes_.find(event.dst_id);
            if (src_it != nodes_.end() && dst_it != nodes_.end()) {
                src = SnapshotEntry{
                    event.src_node_id,
                    src_it->second.link,
                    src_it->second.location->getLocation(),
                    src_it->second.cfg,
                };
                dst = SnapshotEntry{
                    event.dst_id,
                    dst_it->second.link,
                    dst_it->second.location->getLocation(),
                    dst_it->second.cfg,
                };
                has_endpoints = true;
            }
        }
    }

    if (!collided && has_endpoints) {
        float distance_m = geo::haversine_m(src.location, dst.location);
        distance_m = std::max(distance_m, kMinDistanceM);

        float rssi_dbm = computeRssiDbm(src.cfg.tx_power_dbm, distance_m, carrier_freq_mhz_);
        rssi_dbm += sampleFadingDb(event.tx_id, dst.node_id);
        rssi_dbm += sampleNoiseJitterDb(event.tx_id, dst.node_id);

        if (rssi_dbm >= dst.cfg.sensitivity_dbm) {
            const float snr_db = computeSnrDb(rssi_dbm, dst.cfg.noise_floor_dbm);
            if (evaluatePerSuccess(snr_db, event.tx_id, dst.node_id)) {
                if (!shouldDropForCongestion(event.time_us, event.tx_id, dst.node_id)) {
                    dst.link->deliverFromNetwork(data, len, rssi_dbm, snr_db);
                    delivered = true;
                }
            } else {
                per_drop = true;
            }
        }
    }

    if (delivered) {
        const uint64_t latency_us = (event.time_us > tx_end_us) ? (event.time_us - tx_end_us) : 0;
        metrics_.onRxDelivered(latency_us);
    } else {
        metrics_.onRxDropped();
    }

    bool finalize_tx = false;
    bool final_any_delivered = false;
    bool final_any_per_drop = false;
    bool final_collided = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto state_it = tx_delivery_state_.find(event.tx_id);
        if (state_it == tx_delivery_state_.end()) {
            return;
        }

        if (delivered) {
            state_it->second.any_delivered = true;
        }
        if (per_drop) {
            state_it->second.any_per_drop = true;
        }

        if (state_it->second.pending_deliveries > 0) {
            --state_it->second.pending_deliveries;
        }

        if (state_it->second.pending_deliveries == 0) {
            finalize_tx = true;
            final_any_delivered = state_it->second.any_delivered;
            final_any_per_drop = state_it->second.any_per_drop;
            final_collided = state_it->second.collided;
            tx_delivery_state_.erase(state_it);
        }
    }

    if (finalize_tx) {
        if (final_collided && !final_any_delivered) {
            metrics_.onTxCollisionFailure();
        }

        if (final_any_per_drop && !final_any_delivered) {
            metrics_.onTxPerFailure();
        }

        metrics_.onTxOutcome(final_any_delivered);
    }
}

float SimulatedNetwork::computeRssiDbm(float tx_power_dbm,
                                       float distance_m,
                                       float carrier_freq_mhz)
{
    float distance_km = std::max(distance_m / 1000.0f, 0.001f);
    float fspl_db = 32.44f +
                    20.0f * std::log10(carrier_freq_mhz) +
                    20.0f * std::log10(distance_km);
    return tx_power_dbm - fspl_db;
}

float SimulatedNetwork::computeSnrDb(float rssi_dbm, float noise_floor_dbm)
{
    return rssi_dbm - noise_floor_dbm;
}

uint64_t SimulatedNetwork::nextSequenceId()
{
    return next_sequence_id_.fetch_add(1);
}

uint64_t SimulatedNetwork::computeTxDurationUs(size_t payload_len_bytes) const
{
    const uint64_t bits = static_cast<uint64_t>(payload_len_bytes) * 8ULL;
    if (bits == 0) {
        return 1;
    }

    const uint64_t rate = static_cast<uint64_t>((data_rate_bps_ == 0) ? 1U : data_rate_bps_);
    const uint64_t numer = bits * 1000000ULL;
    const uint64_t duration = (numer + rate - 1ULL) / rate;
    return std::max<uint64_t>(1ULL, duration);
}

uint64_t SimulatedNetwork::computeBackoffSlots(uint16_t src_node_id,
                                               uint8_t retry_count,
                                               uint64_t tx_id,
                                               uint16_t cw_current) const
{
    if (cw_current == 0) {
        return 0;
    }

    uint64_t x = mix64(random_seed_ ^ (static_cast<uint64_t>(src_node_id) << 16U));
    x ^= mix64(static_cast<uint64_t>(retry_count) << 8U);
    x ^= mix64(tx_id);
    x = mix64(x);

    return x % (static_cast<uint64_t>(cw_current) + 1ULL);
}

double SimulatedNetwork::deterministicUnitInterval(uint64_t key_a,
                                                   uint64_t key_b,
                                                   uint64_t key_c) const
{
    uint64_t x = mix64(random_seed_ ^ key_a);
    x ^= mix64(key_b + 0x9E3779B97F4A7C15ULL);
    x ^= mix64(key_c + 0xBF58476D1CE4E5B9ULL);
    x = mix64(x);

    return static_cast<double>(x >> 11U) * kInv2Pow53;
}

float SimulatedNetwork::sampleFadingDb(uint64_t tx_id, uint16_t dst_node_id) const
{
    if (fading_stddev_db_ <= 0.0f) {
        return 0.0f;
    }

    // Sum of six uniforms approximates a normal variable with stddev ~= 0.707.
    double sum = 0.0;
    for (uint64_t i = 0; i < 6; ++i) {
        sum += deterministicUnitInterval(tx_id, dst_node_id, 0x1000ULL + i);
    }

    const double z = (sum - 3.0) * 1.4142135623730951; // normalize to stddev ~= 1
    return static_cast<float>(z * static_cast<double>(fading_stddev_db_));
}

float SimulatedNetwork::sampleNoiseJitterDb(uint64_t tx_id, uint16_t dst_node_id) const
{
    if (noise_jitter_db_ <= 0.0f) {
        return 0.0f;
    }

    const double u = deterministicUnitInterval(tx_id, dst_node_id, 0x2000ULL);
    const double signed_u = 2.0 * u - 1.0;
    return static_cast<float>(signed_u * static_cast<double>(noise_jitter_db_));
}

bool SimulatedNetwork::evaluatePerSuccess(float snr_db, uint64_t tx_id, uint16_t dst_node_id) const
{
    if (per_model_ == kPerModelDisabled) {
        return true;
    }

    if (per_model_ == kPerModelThreshold) {
        return snr_db >= snr_threshold_db_;
    }

    if (per_model_ == kPerModelLogistic) {
        const double x = static_cast<double>(per_logistic_k_) *
                         static_cast<double>(snr_db - per_logistic_mid_db_);
        const double clamped = std::max(-60.0, std::min(60.0, x));
        const double p_success = 1.0 / (1.0 + std::exp(-clamped));
        const double u = deterministicUnitInterval(tx_id, dst_node_id, 0x3000ULL);
        return u < p_success;
    }

    return true;
}

bool SimulatedNetwork::shouldDropForCongestion(uint64_t event_time_us,
                                               uint64_t tx_id,
                                               uint16_t dst_node_id) const
{
    if (!enable_congestion_drops_ || congestion_drop_probability_ <= 0.0f) {
        return false;
    }

    uint64_t busy_us = 0;
    uint64_t start_us = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        busy_us = channel_busy_accumulated_us_;
        start_us = utilization_start_time_us_;
    }

    if (event_time_us <= start_us) {
        return false;
    }

    const uint64_t elapsed_us = event_time_us - start_us;
    if (elapsed_us < congestion_min_elapsed_us_) {
        return false;
    }

    const double utilization_pct =
        (100.0 * static_cast<double>(busy_us)) / static_cast<double>(elapsed_us);
    if (utilization_pct < static_cast<double>(congestion_utilization_threshold_pct_)) {
        return false;
    }

    const double u = deterministicUnitInterval(tx_id, dst_node_id, 0x4000ULL);
    return u < static_cast<double>(congestion_drop_probability_);
}

uint64_t SimulatedNetwork::mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31U);
}

uint16_t SimulatedNetwork::getOrInitCwState(uint16_t node_id)
{
    auto it = cw_state_by_node_.find(node_id);
    if (it != cw_state_by_node_.end()) {
        return it->second;
    }

    cw_state_by_node_[node_id] = cw_min_;
    return cw_min_;
}

void SimulatedNetwork::resetCwState(uint16_t node_id)
{
    cw_state_by_node_[node_id] = cw_min_;
}
