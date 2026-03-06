#pragma once

#include "network_header.h"

namespace geo {

/** Haversine distance in metres between two fixed-point GeoPoints. */
float haversine_m(GeoPoint a, GeoPoint b);

/** Initial bearing in degrees [0, 360) from @p from to @p to. */
float bearing_deg(GeoPoint from, GeoPoint to);

/**
 * Return true if @p target lies inside the cone centred at @p origin
 * pointing toward @p target_heading_cdeg with the given @p half_angle_cdeg.
 * Angles are in hundredths of a degree (0.01°).
 */
bool isInsideCone(GeoPoint origin, GeoPoint target,
                  uint16_t target_heading_cdeg, uint16_t half_angle_cdeg);

/**
 * Return true if @p me is "between" @p origin and @p new_transmitter.
 *
 * Specifically: the projection of @p me onto the line segment
 * origin → new_transmitter has parameter t ∈ [0, 1], the perpendicular
 * distance is less than @p threshold_m, **and**
 * dist(origin, me) < dist(origin, new_transmitter).
 */
bool isBetween(GeoPoint origin, GeoPoint me, GeoPoint new_transmitter,
               float threshold_m);

} // namespace geo
