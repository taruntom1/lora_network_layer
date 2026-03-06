#include "geo_utils.h"
#include <cmath>
#include <algorithm>

namespace geo {

static constexpr double kDegToRad = M_PI / 180.0;
static constexpr double kRadToDeg = 180.0 / M_PI;
static constexpr double kEarthR   = 6371000.0; // metres

/** Convert fixed-point ×1e7 integer to radians. */
static inline double toRad(int32_t fp) {
    return (static_cast<double>(fp) / 1e7) * kDegToRad;
}

/** Convert fixed-point ×1e7 integer to degrees. */
static inline double toDeg(int32_t fp) {
    return static_cast<double>(fp) / 1e7;
}

float haversine_m(GeoPoint a, GeoPoint b)
{
    double lat1 = toRad(a.lat);
    double lat2 = toRad(b.lat);
    double dLat = toRad(b.lat - a.lat);
    double dLon = toRad(b.lon - a.lon);

    double sa = std::sin(dLat / 2.0);
    double so = std::sin(dLon / 2.0);
    double h  = sa * sa + std::cos(lat1) * std::cos(lat2) * so * so;

    return static_cast<float>(2.0 * kEarthR * std::asin(std::sqrt(h)));
}

float bearing_deg(GeoPoint from, GeoPoint to)
{
    double lat1 = toRad(from.lat);
    double lat2 = toRad(to.lat);
    double dLon = toRad(to.lon - from.lon);

    double x = std::sin(dLon) * std::cos(lat2);
    double y = std::cos(lat1) * std::sin(lat2) -
               std::sin(lat1) * std::cos(lat2) * std::cos(dLon);

    double brng = std::atan2(x, y) * kRadToDeg;
    if (brng < 0.0) brng += 360.0;
    return static_cast<float>(brng);
}

bool isInsideCone(GeoPoint origin, GeoPoint target,
                  uint16_t target_heading_cdeg, uint16_t half_angle_cdeg)
{
    float brng = bearing_deg(origin, target);
    float heading = static_cast<float>(target_heading_cdeg) / 100.0f;
    float half    = static_cast<float>(half_angle_cdeg) / 100.0f;

    float diff = brng - heading;
    // Normalise to [-180, 180)
    if (diff > 180.0f)  diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;

    return std::fabs(diff) <= half;
}

bool isBetween(GeoPoint origin, GeoPoint me, GeoPoint new_transmitter,
               float threshold_m)
{
    // Use local flat-earth approximation (metre offsets from origin).
    auto mx = [](GeoPoint ref, GeoPoint p) -> std::pair<double,double> {
        double dLat = (toDeg(p.lat) - toDeg(ref.lat)) * kDegToRad * kEarthR;
        double dLon = (toDeg(p.lon) - toDeg(ref.lon)) * kDegToRad * kEarthR
                       * std::cos(toRad(ref.lat));
        return {dLat, dLon};
    };

    auto [my, mx_] = mx(origin, me);            // me relative to origin
    auto [ty, tx_] = mx(origin, new_transmitter); // new_tx relative to origin

    double seg_len2 = tx_ * tx_ + ty * ty;
    if (seg_len2 < 1e-6) return false;           // origin ≈ new_tx

    // Projection parameter t of "me" onto segment origin→new_tx
    double t = (mx_ * tx_ + my * ty) / seg_len2;
    if (t < 0.0 || t > 1.0) return false;

    // Perpendicular distance
    double px = mx_ - t * tx_;
    double py = my  - t * ty;
    double perp = std::sqrt(px * px + py * py);
    if (perp > static_cast<double>(threshold_m)) return false;

    // Additional check: dist(origin, me) < dist(origin, new_tx)
    double dist_me  = std::sqrt(mx_ * mx_ + my * my);
    double dist_tx  = std::sqrt(tx_ * tx_ + ty * ty);
    return dist_me < dist_tx;
}

} // namespace geo
