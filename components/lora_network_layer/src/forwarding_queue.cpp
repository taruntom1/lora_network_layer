#include "forwarding_queue.h"
#include "geo_utils.h"
#include <cstring>
#include <algorithm>

#ifndef CONFIG_NET_BETWEENNESS_THRESHOLD_M
#define CONFIG_NET_BETWEENNESS_THRESHOLD_M 100
#endif

ForwardingQueue::ForwardingQueue(size_t capacity, ILinkLayer& link,
                                 const ILocationProvider& loc)
    : capacity_(capacity)
    , link_(link)
    , loc_(loc)
{
    slots_ = new PendingRelay[capacity_];
    for (size_t i = 0; i < capacity_; ++i) {
        slots_[i].active = false;
    }
    mutex_ = xSemaphoreCreateMutex();
    configASSERT(mutex_);
}

ForwardingQueue::~ForwardingQueue()
{
    vSemaphoreDelete(mutex_);
    delete[] slots_;
}

bool ForwardingQueue::enqueue(const NetworkHeader& hdr, const uint8_t* payload,
                              size_t payload_len, uint32_t holdback_ms)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);

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
        xSemaphoreGive(mutex_);
        return false;
    }

    slot->hdr = hdr;
    size_t copy_len = (payload_len <= NET_MAX_APP_PAYLOAD)
                        ? payload_len : NET_MAX_APP_PAYLOAD;
    std::memcpy(slot->payload, payload, copy_len);
    slot->payload_len = copy_len;
    slot->fire_tick = xTaskGetTickCount() +
                      pdMS_TO_TICKS(holdback_ms);
    slot->active = true;

    xSemaphoreGive(mutex_);
    return true;
}

void ForwardingQueue::processTick()
{
    TickType_t now = xTaskGetTickCount();

    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (size_t i = 0; i < capacity_; ++i) {
        if (slots_[i].active && now >= slots_[i].fire_tick) {
            // Copy out while holding the lock, then release.
            PendingRelay entry = slots_[i];
            slots_[i].active = false;
            xSemaphoreGive(mutex_);

            fireEntry(entry);

            // Re-acquire for the rest of the iteration.
            xSemaphoreTake(mutex_, portMAX_DELAY);
        }
    }
    xSemaphoreGive(mutex_);
}

void ForwardingQueue::onDuplicateHeard(const NetworkHeader& heard_hdr)
{
    GeoPoint my_loc = loc_.getLocation();

    xSemaphoreTake(mutex_, portMAX_DELAY);
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
    xSemaphoreGive(mutex_);
}

size_t ForwardingQueue::activeCount() const
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    size_t count = 0;
    for (size_t i = 0; i < capacity_; ++i) {
        if (slots_[i].active) ++count;
    }
    xSemaphoreGive(mutex_);
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

    link_.send(BROADCAST_ADDR, buf, total);
}
