#include "routing_engine.h"
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
            return {Verdict::DROP, 0};
        }
    }

    // 2. Duplicate check
    if (dup_filter_.isDuplicate(hdr.message_id)) {
        return {Verdict::DROP, 0};
    }

    // 3. Hops remaining
    if (hdr.hops_remaining == 0) {
        return {Verdict::DELIVER_ONLY, 0};
    }

    // 4. Max distance from origin
    GeoPoint my_loc = loc_.getLocation();
    float dist_from_origin = geo::haversine_m(hdr.originPoint(), my_loc);
    if (hdr.max_distance_m > 0 &&
        dist_from_origin > static_cast<float>(hdr.max_distance_m)) {
        return {Verdict::DELIVER_ONLY, 0};
    }

    // 5. Directional cone check
    if (static_cast<PropagationMode>(hdr.prop_mode) == PropagationMode::DIRECTIONAL) {
        if (!geo::isInsideCone(hdr.originPoint(), my_loc,
                               hdr.target_heading,
                               CONFIG_NET_DIRECTIONAL_HALF_ANGLE)) {
            return {Verdict::DELIVER_ONLY, 0};
        }
    }

    // All checks passed — deliver and forward
    uint32_t holdback = computeHoldback(hdr, snr);
    return {Verdict::DELIVER_AND_FORWARD, holdback};
}

uint32_t RoutingEngine::computeHoldback(const NetworkHeader& hdr,
                                        float snr) const
{
    GeoPoint my_loc = loc_.getLocation();
    float dist = geo::haversine_m(hdr.txPoint(), my_loc);
    float range = static_cast<float>(CONFIG_NET_ESTIMATED_RADIO_RANGE_M);

    float dist_ratio = std::clamp(dist / range, 0.0f, 1.0f);

    // Normalise SNR to [0, 1].  Assume SNR range roughly [-20, +15] dB.
    float snr_norm = std::clamp((snr + 20.0f) / 35.0f, 0.0f, 1.0f);

    // Far nodes / weak SNR → short timer (relay first)
    float combined = 0.7f * (1.0f - dist_ratio) + 0.3f * snr_norm;

    float t_min = static_cast<float>(CONFIG_NET_HOLDBACK_MIN_MS);
    float t_max = static_cast<float>(CONFIG_NET_HOLDBACK_MAX_MS);
    float holdback = t_min + (t_max - t_min) * combined;

    uint8_t pri = hdr.priority;
    if (pri > kMaxPriorityIndex) pri = kMaxPriorityIndex;
    holdback *= kPriorityMultiplier[pri];

    return static_cast<uint32_t>(holdback);
}
