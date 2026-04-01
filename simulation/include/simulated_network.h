#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

class SimulatedLinkLayer;
class SimulatedLocationProvider;

/**
 * @file simulated_network.h
 * @ingroup sim_channel
 * @brief In-memory radio channel model used by host simulation nodes.
 */

/**
 * @ingroup sim_api
 * @brief Simulated LoRa channel that fans out frames across registered nodes.
 *
 * For each transmission, the channel snapshots sender/receiver states,
 * computes free-space path loss and SNR, and delivers only when the receiver's
 * sensitivity threshold is met.
 *
 * Receive power model (text form):
 * RSSI_dBm = P_tx_dBm - (32.44 + 20*log10(f_MHz) + 20*log10(d_km)).
 * SNR_dB = RSSI_dBm - NoiseFloor_dBm.
 */
class SimulatedNetwork {
public:
    /**
     * @brief Per-node radio profile applied during propagation checks.
     */
    struct RadioConfig {
        /** Transmit power in dBm used by the source node. */
        float tx_power_dbm = 14.0f;
        /** Receiver noise floor in dBm for SNR estimation. */
        float noise_floor_dbm = -110.0f;
        /** Minimum RSSI in dBm required to deliver a frame. */
        float sensitivity_dbm = -120.0f;
    };

    /**
     * @brief Construct channel model.
     * @param carrier_freq_mhz Carrier frequency in MHz for path-loss calculations.
     */
    explicit SimulatedNetwork(float carrier_freq_mhz = 868.0f);

    /**
     * @brief Register a node with default radio profile.
     */
    void registerNode(uint16_t node_id,
                      SimulatedLinkLayer* link,
                      const SimulatedLocationProvider* location);

    /**
     * @brief Register or replace a node with explicit radio profile.
     */
    void registerNode(uint16_t node_id,
                      SimulatedLinkLayer* link,
                      const SimulatedLocationProvider* location,
                      const RadioConfig& cfg);

    /** @brief Remove a node from the channel registry. */
    void unregisterNode(uint16_t node_id);
    /** @brief Current number of registered nodes. */
    size_t nodeCount() const;

    /**
     * @brief Transmit a frame to one node or broadcast to all reachable nodes.
     * @param src_node_id Source node id.
     * @param dst_id Destination node id or broadcast address.
     * @param data Frame payload pointer.
     * @param len Payload length in bytes.
     */
    void transmit(uint16_t src_node_id,
                  uint16_t dst_id,
                  const uint8_t* data,
                  size_t len) const;

private:
    struct NodeEntry {
        SimulatedLinkLayer* link;
        const SimulatedLocationProvider* location;
        RadioConfig cfg;
    };

    static float computeRssiDbm(float tx_power_dbm, float distance_m, float carrier_freq_mhz);
    static float computeSnrDb(float rssi_dbm, float noise_floor_dbm);

    float carrier_freq_mhz_;
    mutable std::mutex mutex_;
    std::unordered_map<uint16_t, NodeEntry> nodes_;
};
