#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
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
 * @brief Coordinates the network-layer pipeline and runtime tasks.
 *
 * The manager wires the link adapter, location provider, duplicate filter,
 * routing engine, and forwarding queue together. It runs two FreeRTOS tasks:
 * one for RX processing and one for forwarding-timeout handling.
 */
class NetworkManager {
public:
    /** Application callback for delivered messages. */
    using AppRxCallback = std::function<void(const NetworkHeader& hdr,
                                             const uint8_t* payload,
                                             size_t payload_len)>;

    /**
     * @brief Construct a network manager instance.
     *
     * @param link Link-layer interface used for TX/RX operations.
     * @param loc Location provider used for routing and metadata updates.
     * @param cfg Runtime capacities and queue sizes.
     */
    NetworkManager(ILinkLayer& link, ILocationProvider& loc, const NetworkConfig& cfg);

    /**
     * @brief Destroy the manager and release internal RTOS resources.
     */
    ~NetworkManager();

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    /**
     * @brief Spawn RX and forwarding tasks.
     *
     * Call once after construction.
     */
    void start();

    /**
     * @brief Register the callback for messages delivered to this node.
     *
     * @param cb Application callback invoked for delivered payloads.
     */
    void setAppRxCallback(AppRxCallback cb);

    /**
     * @brief Originate a new network-layer message.
     *
     * @param priority Priority class encoded in the outgoing header.
     * @param mode Propagation mode (omni-directional or directional).
     * @param target_heading Directional target heading in 0.01 degrees.
     * @param max_hops Maximum number of relays allowed for this message.
     * @param max_distance_m Maximum propagation distance from origin in meters.
     * @param lifetime_s Time-to-live for the message in seconds.
     * @param payload Application payload bytes to transmit.
     * @param payload_len Number of payload bytes in @p payload.
     * @return 0 on success, negative value on error.
     */
    int sendMessage(Priority priority, PropagationMode mode,
                    uint16_t target_heading, uint8_t max_hops,
                    uint16_t max_distance_m, uint16_t lifetime_s,
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
    std::atomic<uint8_t> seq_;   // Outgoing sequence number

    /* FreeRTOS task entry-points (static trampolines) */
    static void rxTaskEntry(void* arg);
    static void fwdTaskEntry(void* arg);

    void rxTaskLoop();
    void fwdTaskLoop();
};
