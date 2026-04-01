#include "simulated_link_layer.h"

#include <utility>

#include "simulated_network.h"

/**
 * @file simulated_link_layer.cpp
 * @ingroup sim_internal
 * @brief Internal link adapter implementation for in-memory frame transport.
 *
 * The adapter mirrors the runtime contract of ILinkLayer while replacing radio I/O
 * with direct calls into SimulatedNetwork. Receive callback installation and
 * dispatch are mutex-protected to support asynchronous manager threads.
 */

SimulatedLinkLayer::SimulatedLinkLayer(uint16_t node_id, SimulatedNetwork& network)
    : node_id_(node_id)
    , network_(network)
{
}

SimulatedLinkLayer::~SimulatedLinkLayer()
{
    network_.unregisterNode(node_id_);
}

int SimulatedLinkLayer::send(uint16_t dstId, const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > LORA_MAX_PAYLOAD) {
        return -1;
    }

    network_.transmit(node_id_, dstId, data, len);
    return 0;
}

void SimulatedLinkLayer::setRxHandler(RxHandler handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    handler_ = std::move(handler);
}

uint16_t SimulatedLinkLayer::getNodeId() const
{
    return node_id_;
}

void SimulatedLinkLayer::deliverFromNetwork(const uint8_t* data,
                                            size_t len,
                                            float rssi,
                                            float snr)
{
    RxHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handler = handler_;
    }

    if (handler) {
        handler(data, len, rssi, snr);
    }
}
