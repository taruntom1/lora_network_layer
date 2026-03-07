#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>

#include "network_header.h"

/**
 * Raw frame received from the link layer.
 * Stored in a FreeRTOS queue so the radio-task context is never blocked.
 */
struct RxEvent {
    uint8_t  data[LORA_MAX_PAYLOAD];
    size_t   len;
    float    rssi;
    float    snr;
};

/**
 * Abstract link-layer interface.
 *
 * Allows the network layer to be tested with a mock instead of real hardware.
 */
class ILinkLayer {
public:
    using RxHandler = std::function<void(const uint8_t* data, size_t len,
                                         float rssi, float snr)>;

    virtual ~ILinkLayer() = default;

    /** Transmit a raw frame to @p dstId. Returns 0 on success. */
    virtual int send(uint16_t dstId, const uint8_t* data, size_t len) = 0;

    /** Register the callback invoked on every received frame. */
    virtual void setRxHandler(RxHandler handler) = 0;

    /** Return this node's 16-bit address. */
    virtual uint16_t getNodeId() const = 0;
};
