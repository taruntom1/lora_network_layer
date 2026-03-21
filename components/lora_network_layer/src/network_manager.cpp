#include "network_manager.h"
#include "net_log.h"
#include <cstring>
#include <chrono>
#include <thread>
#include <mutex>

#ifndef CONFIG_NET_DUPLICATE_CACHE_SIZE
#define CONFIG_NET_DUPLICATE_CACHE_SIZE 64
#endif
#ifndef CONFIG_NET_FORWARDING_QUEUE_SIZE
#define CONFIG_NET_FORWARDING_QUEUE_SIZE 8
#endif
#ifndef CONFIG_NET_RX_QUEUE_DEPTH
#define CONFIG_NET_RX_QUEUE_DEPTH 8
#endif

static const char* TAG = "network_manager";

NetworkManager::NetworkManager(ILinkLayer& link, ILocationProvider& loc,
                               const NetworkConfig& cfg)
    : link_(link)
    , loc_(loc)
    , dup_filter_(cfg.duplicate_cache_size)
    , routing_(dup_filter_, loc_)
    , fwd_queue_(cfg.forwarding_queue_size, link_, loc_)
    , rx_queue_depth_(cfg.rx_queue_depth)
    , started_(false)
    , seq_(0)
{
}

NetworkManager::~NetworkManager()
{
    link_.setRxHandler(nullptr);
    stop();
}

void NetworkManager::stop()
{
    running_.store(false);
    rx_queue_cv_.notify_all();  // Wake the RX thread if it is blocked waiting
    if (rx_thread_.joinable())  rx_thread_.join();
    if (fwd_thread_.joinable()) fwd_thread_.join();
}

void NetworkManager::start()
{
    // Idempotent guard: ignore repeated start() calls.
    if (started_.exchange(true)) {
        return;
    }

    running_.store(true);

    // Register link-layer RX handler that pushes events onto the queue.
    link_.setRxHandler([this](const uint8_t* data, size_t len,
                              float rssi, float snr) {
        RxEvent evt{};
        size_t copy_len = (len <= sizeof(evt.data)) ? len : sizeof(evt.data);
        std::memcpy(evt.data, data, copy_len);
        evt.len  = copy_len;
        evt.rssi = rssi;
        evt.snr  = snr;
        {
            std::lock_guard<std::mutex> lk(rx_queue_mutex_);
            if (rx_queue_.size() >= rx_queue_depth_) {
                NET_LOGW(TAG, "RX queue full, dropping frame len=%zu", copy_len);
                return;
            }
            rx_queue_.push(evt);
        }
        rx_queue_cv_.notify_one();
    });

    rx_thread_  = std::thread(&NetworkManager::rxTaskLoop, this);
    fwd_thread_ = std::thread(&NetworkManager::fwdTaskLoop, this);
    NET_LOGI(TAG, "RX and forwarding threads started");
}

void NetworkManager::setAppRxCallback(AppRxCallback cb)
{
    std::lock_guard<std::mutex> lock(app_cb_mutex_);
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

void NetworkManager::rxTaskLoop()
{
    while (running_.load()) {
        RxEvent evt;
        {
            std::unique_lock<std::mutex> lk(rx_queue_mutex_);
            rx_queue_cv_.wait_for(lk, std::chrono::milliseconds(100),
                                  [this] { return !rx_queue_.empty() || !running_.load(); });
            if (rx_queue_.empty()) continue;
            evt = rx_queue_.front();
            rx_queue_.pop();
        }

        if (evt.len < sizeof(NetworkHeader)) continue;

        NetworkHeader hdr;
        std::memcpy(&hdr, evt.data, sizeof(NetworkHeader));
        const uint8_t* app_payload = evt.data + sizeof(NetworkHeader);
        size_t app_len = evt.len - sizeof(NetworkHeader);

        fwd_queue_.onDuplicateHeard(hdr);
        EvalResult result = routing_.evaluate(hdr, evt.rssi, evt.snr);

        if (result.verdict == Verdict::DROP) {
            NET_LOGD(TAG, "Dropping msg_id=0x%08lx by routing verdict",
                     static_cast<unsigned long>(hdr.message_id));
            continue;
        }

        AppRxCallback cb;
        {
            std::lock_guard<std::mutex> lock(app_cb_mutex_);
            cb = app_cb_;
        }

        if (cb) {
            cb(hdr, app_payload, app_len);
        }

        if (result.verdict == Verdict::DELIVER_AND_FORWARD) {
            NET_LOGD(TAG, "Scheduling relay msg_id=0x%08lx holdback_ms=%lu",
                     static_cast<unsigned long>(hdr.message_id),
                     static_cast<unsigned long>(result.holdback_ms));
            if (!fwd_queue_.enqueue(hdr, app_payload, app_len, result.holdback_ms)) {
                NET_LOGW(TAG, "Forwarding queue full, dropped relay msg_id=0x%08lx",
                         static_cast<unsigned long>(hdr.message_id));
            }
        }
    }
}

void NetworkManager::fwdTaskLoop()
{
    while (running_.load()) {
        fwd_queue_.processTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}