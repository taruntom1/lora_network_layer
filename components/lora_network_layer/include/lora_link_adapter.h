#pragma once

#include "link_layer_interface.h"
#include "lora_radio.h"

/**
 * Concrete ILinkLayer adapter that delegates to a LoraRadio instance.
 *
 * The radio RX callback is translated into an RxHandler invocation.
 */
class LoraLinkAdapter : public ILinkLayer {
public:
    explicit LoraLinkAdapter(LoraRadio& radio);
    ~LoraLinkAdapter() override = default;

    int  send(uint16_t dstId, const uint8_t* data, size_t len) override;
    void setRxHandler(RxHandler handler) override;
    uint16_t getNodeId() const override;

private:
    LoraRadio& radio_;
    RxHandler  handler_;
};
