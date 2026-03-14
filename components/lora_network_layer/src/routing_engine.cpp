#include "routing_engine.h"
#include "esp_log.h"
#include "geo_utils.h"
#include <algorithm>
#include <cmath>
#include <iterator>

#ifndef CONFIG_NET_HOLDBACK_MAX_MS
#define CONFIG_NET_HOLDBACK_MAX_MS 500
#endif
#ifndef CONFIG_NET_HOLDBACK_MIN_MS
#define CONFIG_NET_HOLDBACK_MIN_MS 50
#endif
#ifndef CONFIG_NET_ESTIMATED_RADIO_RANGE_M
#define CONFIG_NET_ESTIMATED_RADIO_RANGE_M 2000
#endif
#ifndef CONFIG_NET_DIRECTIONAL_HALF_ANGLE
#define CONFIG_NET_DIRECTIONAL_HALF_ANGLE 4500
#endif

static constexpr uint8_t kMaxPriorityIndex = 3;
static const char* TAG = "routing_engine";

// Weights for blending SNR and RSSI into a combined signal-quality score.
static constexpr float kSnrWeight  = 0.5f;
static constexpr float kRssiWeight = 0.5f;


static constexpr float kPriorityMultiplier[] = {
    0.25f,  // EMERGENCY
    0.50f,  // HIGH
    1.00f,  // NORMAL
    1.50f,  // LOW
};

static_assert(std::size(kPriorityMultiplier) == kMaxPriorityIndex + 1,
              "Priority multiplier array size mismatch");

RoutingEngine::RoutingEngine(DuplicateFilter& dup_filter,
                             const ILocationProvider& loc_provider)
    : dup_filter_(dup_filter)
    , loc_(loc_provider)
{
}

EvalResult RoutingEngine::evaluate(const NetworkHeader& hdr,
                                   float rssi, float snr)
{
    // 1. TTL check (skip if timestamp is 0 = time unknown)
    uint32_t now = loc_.getTimestamp();
    if (now != 0 && hdr.timestamp != 0) {
        if (hdr.timestamp + hdr.lifetime_s < now) {
            ESP_LOGD(TAG, "TTL expired msg_id=0x%08lx, dropping",
                     static_cast<unsigned long>(hdr.message_id));
            return {Verdict::DROP, 0};
        }
    }

    // 2. Duplicate check
    if (dup_filter_.isDuplicate(hdr.message_id)) {
        ESP_LOGD(TAG, "Duplicate detected msg_id=0x%08lx, dropping",
        static_cast<unsigned long>(hdr.message_id));
        return {Verdict::DROP, 0};
    }

    // 3. Hops remaining
    if (hdr.hops_remaining == 0) {
        ESP_LOGD(TAG, "Routing verdict DELIVER_ONLY msg_id=0x%08lx (hops exhausted)",
                 static_cast<unsigned long>(hdr.message_id));
        return {Verdict::DELIVER_ONLY, 0};
    }

    // 4. Max distance from origin
    GeoPoint my_loc = loc_.getLocation();
    float dist_from_origin = geo::haversine_m(hdr.originPoint(), my_loc);
    if (hdr.max_distance_m > 0 &&
        dist_from_origin > static_cast<float>(hdr.max_distance_m)) {
        ESP_LOGD(TAG, "Routing verdict DELIVER_ONLY msg_id=0x%08lx (max distance exceeded)",
                 static_cast<unsigned long>(hdr.message_id));
        return {Verdict::DELIVER_ONLY, 0};
    }

    // 5. Propagation mode validation + directional cone check
    PropagationMode mode = static_cast<PropagationMode>(hdr.prop_mode);

    switch (mode) {

    case PropagationMode::OMNI:
        break;

    case PropagationMode::DIRECTIONAL:
        if (!geo::isInsideCone(hdr.originPoint(), my_loc,
                            hdr.target_heading,
                            CONFIG_NET_DIRECTIONAL_HALF_ANGLE)) {
            ESP_LOGD(TAG, "Routing verdict DELIVER_ONLY msg_id=0x%08lx (outside cone)",
                    static_cast<unsigned long>(hdr.message_id));
            return {Verdict::DELIVER_ONLY, 0};
        }
        break;

    default:
        ESP_LOGW(TAG,
                "Invalid prop_mode=%u msg_id=0x%08lx, delivering only",
                hdr.prop_mode,
                static_cast<unsigned long>(hdr.message_id));
        return {Verdict::DELIVER_ONLY, 0};
    }

    // All checks passed — deliver and forward
    uint32_t holdback = computeHoldback(hdr, rssi, snr);
    ESP_LOGD(TAG, "Routing verdict DELIVER_AND_FORWARD msg_id=0x%08lx holdback_ms=%lu",
             static_cast<unsigned long>(hdr.message_id),
             static_cast<unsigned long>(holdback));
    return {Verdict::DELIVER_AND_FORWARD, holdback};
}

uint32_t RoutingEngine::computeHoldback(const NetworkHeader& hdr,
                                        float rssi, float snr) const
{
    GeoPoint my_loc = loc_.getLocation();
    float dist = geo::haversine_m(hdr.txPoint(), my_loc);
    float range = static_cast<float>(CONFIG_NET_ESTIMATED_RADIO_RANGE_M);

    float dist_ratio = std::clamp(dist / range, 0.0f, 1.0f);

    // Normalise SNR to [0, 1].  Assume SNR range roughly [-20, +15] dB.
    float snr_norm = std::clamp((snr + 20.0f) / 35.0f, 0.0f, 1.0f);

    // Normalise RSSI to [0, 1].  Assume RSSI range roughly [-120, -30] dBm.
    float rssi_norm = std::clamp((rssi + 120.0f) / 90.0f, 0.0f, 1.0f);

    // Combined signal quality: weighted average of normalised SNR and RSSI.
    // Nodes with high signal quality (strong reception) wait longer — weaker/farther
    // nodes that heard the message just barely should relay first.
    float signal_quality = kSnrWeight * snr_norm + kRssiWeight * rssi_norm;

    // Far nodes / weak signal → short timer (relay first)
    float combined = 0.7f * (1.0f - dist_ratio) + 0.3f * signal_quality;

    float t_min = static_cast<float>(CONFIG_NET_HOLDBACK_MIN_MS);
    float t_max = static_cast<float>(CONFIG_NET_HOLDBACK_MAX_MS);
    float holdback = t_min + (t_max - t_min) * combined;

    uint8_t pri = hdr.priority;
    if (pri > kMaxPriorityIndex) pri = kMaxPriorityIndex;
    holdback *= kPriorityMultiplier[pri];

    return static_cast<uint32_t>(holdback);
}
