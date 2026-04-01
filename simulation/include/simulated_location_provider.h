#pragma once

#include <cstdint>
#include <mutex>

#include "location_provider.h"

/**
 * @file simulated_location_provider.h
 * @ingroup sim_runtime
 * @brief Host implementation of @ref ILocationProvider with deterministic kinematics.
 */

/**
 * @ingroup sim_api
 * @brief Deterministic, thread-safe location provider for simulated nodes.
 *
 * State is expressed as position + speed + heading + timestamp and can be
 * advanced in virtual time via @ref advance. The implementation assumes a
 * spherical Earth approximation for short-distance stepping.
 */
class SimulatedLocationProvider : public ILocationProvider {
public:
    /**
     * @brief Instantaneous motion state at a simulation timestamp.
     */
    struct Kinematics {
        /** Geodetic position in fixed-point degrees (deg * 1e7). */
        GeoPoint position;
        /** Speed in centimeters per second. */
        uint16_t speed_cm_s;
        /** Heading in centi-degrees, where 0 is north and 9000 is east. */
        uint16_t heading_cdeg;
        /** Timestamp in whole seconds. */
        uint32_t timestamp_s;
    };

    /**
     * @brief Construct provider with initial kinematic state.
     */
    explicit SimulatedLocationProvider(const Kinematics& initial);

    /** @copydoc ILocationProvider::getLocation */
    GeoPoint getLocation() const override;
    /** @copydoc ILocationProvider::getSpeed */
    uint16_t getSpeed() const override;
    /** @copydoc ILocationProvider::getHeading */
    uint16_t getHeading() const override;
    /** @copydoc ILocationProvider::getTimestamp */
    uint32_t getTimestamp() const override;

    /**
     * @brief Replace current state atomically.
     * @param state New kinematic state to apply immediately.
     */
    void setKinematics(const Kinematics& state);

    /**
     * @brief Advance node position and timestamp by a virtual-time delta.
     * @param delta_ms Virtual milliseconds to advance.
     */
    void advance(uint64_t delta_ms);

private:
    mutable std::mutex mutex_;
    Kinematics state_;
    uint16_t subsecond_ms_{0};
};
