#pragma once

#include <cstdint>
#include <mutex>

#include "link_layer_interface.h"

class SimulatedNetwork;

/**
 * @file simulated_link_layer.h
 * @ingroup sim_channel
 * @brief Host link-layer adapter bridging NetworkManager and SimulatedNetwork.
 */

/**
 * @ingroup sim_api
 * @brief In-memory link-layer implementation for simulation nodes.
 *
 * Outbound traffic is forwarded to @ref SimulatedNetwork::transmit. Inbound
 * frames are delivered through the registered @ref RxHandler callback.
 */
class SimulatedLinkLayer : public ILinkLayer {
public:
    /**
     * @brief Construct a simulated link interface for one node.
     * @param node_id Node identifier exposed to network-layer logic.
     * @param network Shared simulated radio channel.
     */
    SimulatedLinkLayer(uint16_t node_id, SimulatedNetwork& network);

    /**
     * @brief Destructor unregisters the node from the simulated network.
     */
    ~SimulatedLinkLayer() override;

    /** @copydoc ILinkLayer::send */
    int send(uint16_t dstId, const uint8_t* data, size_t len) override;
    /** @copydoc ILinkLayer::setRxHandler */
    void setRxHandler(RxHandler handler) override;
    /** @copydoc ILinkLayer::getNodeId */
    uint16_t getNodeId() const override;

    /**
     * @brief Inject a received frame from the simulated channel.
     * @param data Pointer to payload bytes.
     * @param len Payload length in bytes.
     * @param rssi Received signal strength in dBm.
     * @param snr Signal-to-noise ratio in dB.
     */
    void deliverFromNetwork(const uint8_t* data, size_t len, float rssi, float snr);

private:
    uint16_t node_id_;
    SimulatedNetwork& network_;
    mutable std::mutex mutex_;
    RxHandler handler_;
};
