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

struct SnapshotEntry {
    uint16_t node_id;
    SimulatedLinkLayer* link;
    GeoPoint location;
    SimulatedNetwork::RadioConfig cfg;
};

} // namespace

SimulatedNetwork::SimulatedNetwork(float carrier_freq_mhz)
    : carrier_freq_mhz_(carrier_freq_mhz)
{
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
}

void SimulatedNetwork::unregisterNode(uint16_t node_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_.erase(node_id);
}

size_t SimulatedNetwork::nodeCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.size();
}

void SimulatedNetwork::transmit(uint16_t src_node_id,
                                uint16_t dst_id,
                                const uint8_t* data,
                                size_t len) const
{
    if (!data || len == 0 || len > LORA_MAX_PAYLOAD) {
        return;
    }

    SnapshotEntry src{};
    std::vector<SnapshotEntry> recipients;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto src_it = nodes_.find(src_node_id);
        if (src_it == nodes_.end()) {
            return;
        }

        src = SnapshotEntry{
            src_node_id,
            src_it->second.link,
            src_it->second.location->getLocation(),
            src_it->second.cfg,
        };

        recipients.reserve(nodes_.size());
        for (const auto& [node_id, entry] : nodes_) {
            if (node_id == src_node_id) {
                continue;
            }
            if (dst_id != BROADCAST_ADDR && dst_id != node_id) {
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

    for (const SnapshotEntry& dst : recipients) {
        float distance_m = geo::haversine_m(src.location, dst.location);
        distance_m = std::max(distance_m, kMinDistanceM);

        float rssi_dbm = computeRssiDbm(src.cfg.tx_power_dbm, distance_m, carrier_freq_mhz_);
        if (rssi_dbm < dst.cfg.sensitivity_dbm) {
            continue;
        }

        float snr_db = computeSnrDb(rssi_dbm, dst.cfg.noise_floor_dbm);
        dst.link->deliverFromNetwork(data, len, rssi_dbm, snr_db);
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
