#pragma once

#include "link_layer_interface.h"
#include "lora_radio.hpp"

/**
 * Concrete ILinkLayer adapter that delegates to a LoraRadio instance.
 *
 * The radio RX callback is translated into an RxHandler invocation.
 */
class LoraLinkAdapter : public ILinkLayer {
public:
    /**
     * @param radio   Reference to an initialised LoraRadio instance.
     * @param nodeId  This node's 16-bit address (typically from
     *                LoraRadioConfig::nodeId).
     */
    LoraLinkAdapter(LoraRadio& radio, uint16_t nodeId);
    ~LoraLinkAdapter() override = default;

    int  send(uint16_t dstId, const uint8_t* data, size_t len) override;
    void setRxHandler(RxHandler handler) override;
    uint16_t getNodeId() const override;

private:
    LoraRadio& radio_;
    uint16_t   nodeId_;
    RxHandler  handler_;
};
