#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "network_header.h"
#include "link_layer_interface.h"
#include "location_provider.h"
#include "duplicate_filter.h"
#include "routing_engine.h"
#include "forwarding_queue.h"

/** Runtime configuration for the network manager. */
struct NetworkConfig {
    size_t duplicate_cache_size;
    size_t forwarding_queue_size;
    size_t rx_queue_depth;
};

/**
 * Orchestrator: ties together all network-layer components and spawns
 * two FreeRTOS tasks (RX processing and forwarding timer).
 */
class NetworkManager {
public:
    /** Application callback for delivered messages. */
    using AppRxCallback = std::function<void(const NetworkHeader& hdr,
                                             const uint8_t* payload,
                                             size_t payload_len)>;

    NetworkManager(ILinkLayer& link, ILocationProvider& loc,
                   const NetworkConfig& cfg);
    ~NetworkManager();

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    /** Spawn RX and forwarding tasks. Call once after construction. */
    void start();

    /** Register the callback for messages delivered to this node. */
    void setAppRxCallback(AppRxCallback cb);

    /**
     * Originate a new network-layer message.
     *
     * @return 0 on success, negative on error.
     */
    int sendMessage(Priority priority, PropagationMode mode,
                    uint16_t target_heading, uint8_t max_hops,
                    uint16_t max_dist_m, uint16_t lifetime_s,
                    const uint8_t* payload, size_t payload_len);

private:
    ILinkLayer&         link_;
    ILocationProvider&   loc_;
    DuplicateFilter     dup_filter_;
    RoutingEngine       routing_;
    ForwardingQueue     fwd_queue_;

    QueueHandle_t       rx_queue_;
    TaskHandle_t        rx_task_handle_;
    TaskHandle_t        fwd_task_handle_;

    AppRxCallback       app_cb_;
    uint8_t             seq_;   // Outgoing sequence number

    /* FreeRTOS task entry-points (static trampolines) */
    static void rxTaskEntry(void* arg);
    static void fwdTaskEntry(void* arg);

    void rxTaskLoop();
    void fwdTaskLoop();
};
