#include "simulated_location_provider.h"

#include <cmath>

/**
 * @file simulated_location_provider.cpp
 * @ingroup sim_internal
 * @brief Internal kinematics integration for simulated node movement.
 *
 * Position updates use heading-based north/east decomposition and convert
 * displacement back to latitude/longitude deltas using a spherical Earth model.
 * Timestamp progression tracks sub-second accumulation to keep second granularity
 * consistent with NetworkHeader fields.
 */

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6371000.0;

} // namespace

SimulatedLocationProvider::SimulatedLocationProvider(const Kinematics& initial)
    : state_(initial)
{
}

GeoPoint SimulatedLocationProvider::getLocation() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.position;
}

uint16_t SimulatedLocationProvider::getSpeed() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.speed_cm_s;
}

uint16_t SimulatedLocationProvider::getHeading() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.heading_cdeg;
}

uint32_t SimulatedLocationProvider::getTimestamp() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.timestamp_s;
}

void SimulatedLocationProvider::setKinematics(const Kinematics& state)
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    subsecond_ms_ = 0;
}

void SimulatedLocationProvider::advance(uint64_t delta_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t distance_cm = static_cast<uint64_t>(state_.speed_cm_s) * delta_ms / 1000ULL;
    double distance_m = static_cast<double>(distance_cm) / 100.0;

    if (distance_m > 0.0) {
        double heading_deg = static_cast<double>(state_.heading_cdeg) / 100.0;
        double heading_rad = heading_deg * (kPi / 180.0);

        // 0 degrees is north: split travel into north/east components.
        double north_m = std::cos(heading_rad) * distance_m;
        double east_m = std::sin(heading_rad) * distance_m;

        double lat_deg = static_cast<double>(state_.position.lat) / 1e7;
        double lat_rad = lat_deg * (kPi / 180.0);

        double delta_lat_deg = (north_m / kEarthRadiusM) * (180.0 / kPi);
        double denom = std::cos(lat_rad);
        if (std::fabs(denom) < 1e-6) {
            denom = (denom < 0.0) ? -1e-6 : 1e-6;
        }
        double delta_lon_deg = (east_m / (kEarthRadiusM * denom)) * (180.0 / kPi);

        double next_lat = lat_deg + delta_lat_deg;
        double lon_deg = static_cast<double>(state_.position.lon) / 1e7;
        double next_lon = lon_deg + delta_lon_deg;

        state_.position.lat = static_cast<int32_t>(std::llround(next_lat * 1e7));
        state_.position.lon = static_cast<int32_t>(std::llround(next_lon * 1e7));
    }

    uint64_t total_ms = static_cast<uint64_t>(subsecond_ms_) + delta_ms;
    state_.timestamp_s += static_cast<uint32_t>(total_ms / 1000ULL);
    subsecond_ms_ = static_cast<uint16_t>(total_ms % 1000ULL);
}
