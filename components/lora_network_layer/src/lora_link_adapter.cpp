#include "lora_link_adapter.h"
#include <cstring>

static LoraLinkAdapter* s_adapter_instance = nullptr;

LoraLinkAdapter::LoraLinkAdapter(LoraRadio& radio, uint16_t nodeId)
    : radio_(radio)
    , nodeId_(nodeId)
{
}

int LoraLinkAdapter::send(uint16_t dstId, const uint8_t* data, size_t len)
{
    return radio_.send(dstId, NET_MSG_TYPE, data, len, /*requestAck=*/false);
}

void LoraLinkAdapter::setRxHandler(RxHandler handler)
{
    handler_ = std::move(handler);
    s_adapter_instance = this;

    radio_.setRxCallback([](const PacketHeader& pkt_hdr,
                            const uint8_t* payload,
                            float rssi, float snr) {
        if (!s_adapter_instance || !s_adapter_instance->handler_) return;
        // Only handle network-layer frames
        if (pkt_hdr.msgType != NET_MSG_TYPE) return;

        s_adapter_instance->handler_(payload, pkt_hdr.payloadLen, rssi, snr);
    });
}

uint16_t LoraLinkAdapter::getNodeId() const
{
    return nodeId_;
}
