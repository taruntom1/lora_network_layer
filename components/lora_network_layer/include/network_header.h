#pragma once

#include <cstdint>
#include <cstring>

/** Fixed-point geographic coordinate (latitude or longitude × 1e7). */
struct GeoPoint {
    int32_t lat;
    int32_t lon;
};

/** Propagation mode for a network message. */
enum class PropagationMode : uint8_t {
    OMNI        = 0,
    DIRECTIONAL = 1,
};

/** Message priority (lower numeric value = higher urgency). */
enum class Priority : uint8_t {
    EMERGENCY = 0,
    HIGH      = 1,
    NORMAL    = 2,
    LOW       = 3,
};

/** Broadcast destination used at the link layer. */
static constexpr uint16_t BROADCAST_ADDR   = 0xFFFF;

/** Link-layer message type reserved for network-layer frames. */
static constexpr uint8_t  NET_MSG_TYPE     = 0x10;

/** Maximum application payload after the 45-byte network header (247 − 45). */
static constexpr size_t   NET_MAX_APP_PAYLOAD = 202;

/**
 * 45-byte packed network header transmitted over the air.
 *
 * All multi-byte fields are little-endian (native on ESP32).
 */
struct __attribute__((packed)) NetworkHeader {
    uint32_t message_id;       // (origin_nodeId << 16) | sequence
    uint8_t  priority;         // Priority enum
    uint32_t timestamp;        // Epoch seconds (32-bit)

    int32_t  origin_lat;       // Fixed-point ×1e7
    int32_t  origin_lon;       // Fixed-point ×1e7
    uint16_t origin_speed;     // cm/s
    uint16_t origin_heading;   // 0.01° (0–35999)

    int32_t  tx_lat;           // Updated each hop
    int32_t  tx_lon;           // Updated each hop
    uint16_t tx_speed;         // Updated each hop
    uint16_t tx_heading;       // Updated each hop

    uint8_t  prop_mode;        // PropagationMode enum
    uint16_t target_heading;   // Directional target (0.01°)
    uint8_t  hops_remaining;   // Countdown
    uint16_t max_distance_m;   // Max radius from origin (m)
    uint16_t lifetime_s;       // TTL in seconds
    uint8_t  reserved[4];      // Padding to 45 bytes (future use)

    /* ---- helpers ---- */

    GeoPoint originPoint() const { return {origin_lat, origin_lon}; }
    GeoPoint txPoint()     const { return {tx_lat, tx_lon}; }

    void setOriginPoint(GeoPoint p) { origin_lat = p.lat; origin_lon = p.lon; }
    void setTxPoint(GeoPoint p)     { tx_lat = p.lat; tx_lon = p.lon; }
};

static_assert(sizeof(NetworkHeader) == 45, "NetworkHeader must be exactly 45 bytes");
