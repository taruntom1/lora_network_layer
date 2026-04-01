#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
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

/** Error codes returned by NetworkManager::sendMessage(). */
enum class NetworkError : int {
    Ok              = 0,
    PayloadTooLarge = -1,
    LinkSendFailed  = -2,
};

/**
 * @brief Coordinates the network-layer pipeline and runtime threads.
 *
 * The manager wires the link adapter, location provider, duplicate filter,
 * routing engine, and forwarding queue together. It runs two threads:
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
     * @brief Gracefully stops RX and forwarding tasks.
     *
     * Call once before destruction.
     */
    void stop();
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
     * @return `static_cast<int>(NetworkError::Ok)` on success.
     * @return `static_cast<int>(NetworkError::PayloadTooLarge)` when
     *         @p payload_len exceeds NET_MAX_APP_PAYLOAD.
     * @return `static_cast<int>(NetworkError::LinkSendFailed)` when the
     *         underlying link-layer send operation fails.
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

    std::queue<RxEvent>     rx_queue_;
    size_t                  rx_queue_depth_;
    std::mutex              rx_queue_mutex_;
    std::condition_variable rx_queue_cv_;

    std::thread         rx_thread_;
    std::thread         fwd_thread_;

    std::mutex          app_cb_mutex_;

    AppRxCallback       app_cb_;
    std::atomic<bool>   started_;  // True once runtime threads/handlers are installed.
    std::atomic<bool>   running_{true}; // Set false to signal threads to stop and exit cleanly
    std::atomic<uint8_t> seq_;   // Outgoing sequence number

    void rxTaskLoop();
    void fwdTaskLoop();
};
