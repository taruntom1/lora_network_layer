#pragma once

#include "network_header.h"
#include "duplicate_filter.h"
#include "location_provider.h"

/** Result of the routing evaluation pipeline. */
enum class Verdict {
    DROP,              // Already seen / expired / invalid
    DELIVER_ONLY,      // Deliver to app, do NOT relay
    DELIVER_AND_FORWARD, // Deliver to app AND schedule relay
};

/** Outcome returned by RoutingEngine::evaluate(). */
struct EvalResult {
    Verdict  verdict;
    uint32_t holdback_ms;  // Meaningful only when verdict == DELIVER_AND_FORWARD
};

/**
 * Stateless routing evaluation pipeline.
 *
 * Checks: duplicate → TTL → hops → max distance → directional cone.
 * Computes the hold-back timer for messages that pass all checks.
 */
class RoutingEngine {
public:
    RoutingEngine(DuplicateFilter& dup_filter,
                  const ILocationProvider& loc_provider);

    /**
     * Run the full evaluation pipeline on an incoming network header.
     *
     * @param hdr  The received network header.
     * @param rssi Received signal strength (dBm).
     * @param snr  Signal-to-noise ratio (dB).
     * @return     Verdict and (if forwarding) hold-back time in ms.
     */
    EvalResult evaluate(const NetworkHeader& hdr, float rssi, float snr);

private:
    DuplicateFilter&        dup_filter_;
    const ILocationProvider& loc_;

    uint32_t computeHoldback(const NetworkHeader& hdr, float rssi, float snr) const;
};
