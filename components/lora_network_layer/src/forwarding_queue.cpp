#include "forwarding_queue.h"
#include "net_log.h"
#include "geo_utils.h"
#include <cstring>
#include <algorithm>
#include <cassert>
#include <mutex>
#include <chrono>

#ifndef CONFIG_NET_BETWEENNESS_THRESHOLD_M
#define CONFIG_NET_BETWEENNESS_THRESHOLD_M 100
#endif

static const char* TAG = "forwarding_queue";

ForwardingQueue::ForwardingQueue(size_t capacity, ILinkLayer& link,
                                 const ILocationProvider& loc)
    : capacity_(capacity)
    , link_(link)
    , loc_(loc)
{
    // Runtime capacity must not exceed the compile-time storage limit.
    assert(capacity_ <= kMaxSlots);
    for (size_t i = 0; i < capacity_; ++i) {
        slots_[i].active = false;
    }
}

bool ForwardingQueue::enqueue(const NetworkHeader& hdr, const uint8_t* payload,
                              size_t payload_len, uint32_t holdback_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Find a free slot.
    PendingRelay* slot = nullptr;
    for (size_t i = 0; i < capacity_; ++i) {
        if (!slots_[i].active) {
            slot = &slots_[i];
            break;
        }
    }

    // If full, evict the entry with the lowest priority (highest numeric value).
    if (!slot) {
        PendingRelay* worst = nullptr;
        for (size_t i = 0; i < capacity_; ++i) {
            if (!worst || slots_[i].hdr.priority > worst->hdr.priority) {
                worst = &slots_[i];
            }
        }
        // Only evict if incoming message is higher priority (lower numeric value).
        if (worst && hdr.priority < worst->hdr.priority) {
            slot = worst;
        }
    }

    if (!slot) {
        NET_LOGW(TAG, "Forwarding queue full, drop msg_id=0x%08lx",
                 static_cast<unsigned long>(hdr.message_id));
        return false;
    }

    slot->hdr = hdr;
    size_t copy_len = (payload_len <= NET_MAX_APP_PAYLOAD)
                        ? payload_len : NET_MAX_APP_PAYLOAD;
    std::memcpy(slot->payload, payload, copy_len);
    slot->payload_len = copy_len;
    slot->fire_time = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(holdback_ms);
    slot->active = true;

    return true;
}

void ForwardingQueue::processTick()
{
    auto now = std::chrono::steady_clock::now();
    size_t due_count = 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < capacity_; ++i) {
            if (slots_[i].active && now >= slots_[i].fire_time) {
                // Snapshot due entries while locked, then send after unlock.
                fire_buf_[due_count++] = slots_[i];
                slots_[i].active = false;
            }
        }
    }

    for (size_t i = 0; i < due_count; ++i) {
        fireEntry(fire_buf_[i]);
    }
}

void ForwardingQueue::onDuplicateHeard(const NetworkHeader& heard_hdr)
{
    GeoPoint my_loc = loc_.getLocation();

    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < capacity_; ++i) {
        if (!slots_[i].active) continue;
        if (slots_[i].hdr.message_id != heard_hdr.message_id) continue;

        // Check betweenness: is this node between origin and the heard TX?
        if (geo::isBetween(slots_[i].hdr.originPoint(), my_loc,
                           heard_hdr.txPoint(),
                           static_cast<float>(CONFIG_NET_BETWEENNESS_THRESHOLD_M))) {
            slots_[i].active = false;  // Implicit cancellation
        }
    }
}

size_t ForwardingQueue::activeCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (size_t i = 0; i < capacity_; ++i) {
        if (slots_[i].active) ++count;
    }
    return count;
}

void ForwardingQueue::fireEntry(PendingRelay& entry)
{
    // Decrement hops
    if (entry.hdr.hops_remaining > 0) {
        entry.hdr.hops_remaining--;
    }

    // Stamp current location as transmitter
    GeoPoint loc = loc_.getLocation();
    entry.hdr.setTxPoint(loc);
    entry.hdr.tx_speed   = loc_.getSpeed();
    entry.hdr.tx_heading = loc_.getHeading();

    // Serialise header + payload into a single buffer
    uint8_t buf[sizeof(NetworkHeader) + NET_MAX_APP_PAYLOAD];
    std::memcpy(buf, &entry.hdr, sizeof(NetworkHeader));
    std::memcpy(buf + sizeof(NetworkHeader), entry.payload, entry.payload_len);
    size_t total = sizeof(NetworkHeader) + entry.payload_len;

    NET_LOGD(TAG, "Relay TX msg_id=0x%08lx bytes=%zu hops_remaining=%u",
             static_cast<unsigned long>(entry.hdr.message_id),
             total,
             entry.hdr.hops_remaining);
    link_.send(BROADCAST_ADDR, buf, total);
}
