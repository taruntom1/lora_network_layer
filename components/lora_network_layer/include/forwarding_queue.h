#pragma once

#include <cstdint>
#include <cstddef>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "network_header.h"
#include "link_layer_interface.h"
#include "location_provider.h"

/**
 * Fixed-size pool of pending relay slots with hold-back timers.
 *
 * Entries fire when their hold-back timer expires.  Implicit cancellation
 * removes entries that a closer node has already relayed.
 */
class ForwardingQueue {
public:
    ForwardingQueue(size_t capacity, ILinkLayer& link,
                    const ILocationProvider& loc);
    ~ForwardingQueue();

    ForwardingQueue(const ForwardingQueue&) = delete;
    ForwardingQueue& operator=(const ForwardingQueue&) = delete;

    /**
     * Enqueue a message for deferred relay.
     *
     * @return true if the message was queued; false if the pool is full
     *         (lowest-priority eviction may occur).
     */
    bool enqueue(const NetworkHeader& hdr, const uint8_t* payload,
                 size_t payload_len, uint32_t holdback_ms);

    /**
     * Called periodically (~10 ms) by the forwarding task.
     * Fires any entries whose hold-back timer has expired.
     */
    void processTick();

    /**
     * Implicit cancellation: called when a duplicate frame is heard.
     * Cancels queued entries whose relay is no longer needed because the
     * heard transmitter is farther from the origin.
     */
    void onDuplicateHeard(const NetworkHeader& heard_hdr);

    /** Number of active (non-cancelled, non-fired) entries. */
    size_t activeCount() const;

private:
    struct PendingRelay {
        NetworkHeader hdr;
        uint8_t       payload[NET_MAX_APP_PAYLOAD];
        size_t        payload_len;
        TickType_t    fire_tick;
        bool          active;
    };

    PendingRelay*       slots_;
    size_t              capacity_;
    ILinkLayer&         link_;
    const ILocationProvider& loc_;
    SemaphoreHandle_t   mutex_;

    void fireEntry(PendingRelay& entry);
};
