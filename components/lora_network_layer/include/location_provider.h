#pragma once

#include "network_header.h"

/**
 * Abstract GPS / location interface.
 *
 * The application must provide a concrete implementation (e.g. wrapping a
 * GNSS module) and inject it into the NetworkManager.
 */
class ILocationProvider {
public:
    virtual ~ILocationProvider() = default;

    /** Current position as fixed-point ×1e7. */
    virtual GeoPoint getLocation() const = 0;

    /** Speed in cm/s. */
    virtual uint16_t getSpeed() const = 0;

    /** Heading in 0.01° (0–35999). */
    virtual uint16_t getHeading() const = 0;

    /** Current epoch timestamp (seconds). 0 = unknown. */
    virtual uint32_t getTimestamp() const = 0;
};
