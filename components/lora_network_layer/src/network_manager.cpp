#include "network_manager.h"
#include <cstring>

#ifndef CONFIG_NET_DUPLICATE_CACHE_SIZE
#define CONFIG_NET_DUPLICATE_CACHE_SIZE 64
#endif
#ifndef CONFIG_NET_FORWARDING_QUEUE_SIZE
#define CONFIG_NET_FORWARDING_QUEUE_SIZE 8
#endif
#ifndef CONFIG_NET_RX_QUEUE_DEPTH
#define CONFIG_NET_RX_QUEUE_DEPTH 8
#endif

static constexpr UBaseType_t kRxTaskPrio  = 5;
static constexpr UBaseType_t kFwdTaskPrio = 4;
static constexpr uint32_t   kRxTaskStack  = 4096;
static constexpr uint32_t   kFwdTaskStack = 4096;

NetworkManager::NetworkManager(ILinkLayer& link, ILocationProvider& loc,
                               const NetworkConfig& cfg)
    : link_(link)
    , loc_(loc)
    , dup_filter_(cfg.duplicate_cache_size)
    , routing_(dup_filter_, loc_)
    , fwd_queue_(cfg.forwarding_queue_size, link_, loc_)
    , rx_queue_(nullptr)
    , rx_task_handle_(nullptr)
    , fwd_task_handle_(nullptr)
    , started_(false)
    , seq_(0)
{
    rx_queue_ = xQueueCreate(cfg.rx_queue_depth, sizeof(RxEvent));
    configASSERT(rx_queue_);
}

NetworkManager::~NetworkManager()
{
    link_.setRxHandler(nullptr);
    if (rx_task_handle_)  vTaskDelete(rx_task_handle_);
    if (fwd_task_handle_) vTaskDelete(fwd_task_handle_);
    if (rx_queue_)        vQueueDelete(rx_queue_);
}

void NetworkManager::start()
{
    // Idempotent guard: ignore repeated start() calls.
    if (started_.exchange(true)) {
        return;
    }

    // Register link-layer RX handler that pushes events onto the queue.
    link_.setRxHandler([this](const uint8_t* data, size_t len,
                              float rssi, float snr) {
        RxEvent evt{};
        size_t copy_len = (len <= sizeof(evt.data)) ? len : sizeof(evt.data);
        std::memcpy(evt.data, data, copy_len);
        evt.len  = copy_len;
        evt.rssi = rssi;
        evt.snr  = snr;
        xQueueSendToBack(rx_queue_, &evt, 0);  // Non-blocking
    });

    BaseType_t rc = xTaskCreate(rxTaskEntry, "net_rx", kRxTaskStack, this,
                                kRxTaskPrio, &rx_task_handle_);
    if (rc != pdPASS) {
        link_.setRxHandler(nullptr);
        started_.store(false);
        configASSERT(false);
        return;
    }

    rc = xTaskCreate(fwdTaskEntry, "net_fwd", kFwdTaskStack, this,
                     kFwdTaskPrio, &fwd_task_handle_);
    if (rc != pdPASS) {
        vTaskDelete(rx_task_handle_);
        rx_task_handle_ = nullptr;
        link_.setRxHandler(nullptr);
        started_.store(false);
        configASSERT(false);
        return;
    }
}

void NetworkManager::setAppRxCallback(AppRxCallback cb)
{
    app_cb_ = std::move(cb);
}

int NetworkManager::sendMessage(Priority priority, PropagationMode mode,
                                uint16_t target_heading, uint8_t max_hops,
                                uint16_t max_distance_m, uint16_t lifetime_s,
                                const uint8_t* payload, size_t payload_len)
{
    if (payload_len > NET_MAX_APP_PAYLOAD) {
        return static_cast<int>(NetworkError::PayloadTooLarge);
    }

    NetworkHeader hdr{};
    uint16_t node_id = link_.getNodeId();
    hdr.message_id    = (static_cast<uint32_t>(node_id) << 16) | seq_++;
    hdr.priority      = static_cast<uint8_t>(priority);
    hdr.timestamp     = loc_.getTimestamp();

    GeoPoint loc = loc_.getLocation();
    hdr.setOriginPoint(loc);
    hdr.origin_speed   = loc_.getSpeed();
    hdr.origin_heading = loc_.getHeading();
    hdr.setTxPoint(loc);
    hdr.tx_speed       = loc_.getSpeed();
    hdr.tx_heading     = loc_.getHeading();

    hdr.prop_mode       = static_cast<uint8_t>(mode);
    hdr.target_heading  = target_heading;
    hdr.hops_remaining  = max_hops;
    hdr.max_distance_m  = max_distance_m;
    hdr.lifetime_s      = lifetime_s;

    // Register in duplicate filter so we don't process our own message.
    dup_filter_.markSeen(hdr.message_id);

    // Serialise and send
    uint8_t buf[sizeof(NetworkHeader) + NET_MAX_APP_PAYLOAD];
    std::memcpy(buf, &hdr, sizeof(NetworkHeader));
    std::memcpy(buf + sizeof(NetworkHeader), payload, payload_len);
    size_t total = sizeof(NetworkHeader) + payload_len;

    int send_rc = link_.send(BROADCAST_ADDR, buf, total);
    if (send_rc != 0) {
        return static_cast<int>(NetworkError::LinkSendFailed);
    }
    return static_cast<int>(NetworkError::Ok);
}

/* ---- Task entry points ---- */

void NetworkManager::rxTaskEntry(void* arg)
{
    static_cast<NetworkManager*>(arg)->rxTaskLoop();
}

void NetworkManager::fwdTaskEntry(void* arg)
{
    static_cast<NetworkManager*>(arg)->fwdTaskLoop();
}

void NetworkManager::rxTaskLoop()
{
    RxEvent evt;
    for (;;) {
        if (xQueueReceive(rx_queue_, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        // Need at least a full network header.
        if (evt.len < sizeof(NetworkHeader)) continue;

        NetworkHeader hdr;
        std::memcpy(&hdr, evt.data, sizeof(NetworkHeader));
        const uint8_t* app_payload = evt.data + sizeof(NetworkHeader);
        size_t app_len = evt.len - sizeof(NetworkHeader);

        // Implicit cancellation: check pending relays before routing eval.
        fwd_queue_.onDuplicateHeard(hdr);

        // Run routing pipeline.
        EvalResult result = routing_.evaluate(hdr, evt.rssi, evt.snr);

        if (result.verdict == Verdict::DROP) continue;

        // Deliver to application.
        if (app_cb_) {
            app_cb_(hdr, app_payload, app_len);
        }

        // Schedule relay if needed.
        if (result.verdict == Verdict::DELIVER_AND_FORWARD) {
            fwd_queue_.enqueue(hdr, app_payload, app_len, result.holdback_ms);
        }
    }
}

void NetworkManager::fwdTaskLoop()
{
    const TickType_t kTickInterval = pdMS_TO_TICKS(10);
    for (;;) {
        fwd_queue_.processTick();
        vTaskDelay(kTickInterval);
    }
}
