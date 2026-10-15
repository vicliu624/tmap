#pragma once
#include "tmap/administration.h"
#include "tmap/contour_renderer.h"
#include "tmap/coordinate_route.h"
#include "tmap/integer_coordinate_route.h"
#include "tmap/integer_geometry.h"
#include "tmap/integer_leg_elevation.h"
#include "tmap/integer_maneuvers.h"
#include "tmap/road_rules.h"
#include "tmap/route_elevation.h"
#include "tmap/turn_permissions.h"
#include "tmap/turn_rules.h"
#include "tmap/virtual_map.h"

namespace tmap
{
// Constant-work cancellation. Borrowed buffers remain caller-owned; published
// counts are revoked without clearing potentially large arrays. Begin restarts.
inline void cancel(RouteCursor& cursor)
{
    cursor.complete = true;
    cursor.terminal = Result::Cancelled;
    cursor.goal_label = UINT32_MAX;
    cursor.heap_size = 0;
}
inline void cancel(CoordinateRouteCursor& cursor)
{
    cancel(cursor.searching);
    cursor.complete = true;
    cursor.terminal = Result::Cancelled;
    cursor.count = 0;
    cursor.found = false;
    cursor.cost = 0;
}
inline void cancel(ContourRenderCursor& cursor)
{
    cursor.complete = true;
    cursor.terminal = Result::Cancelled;
    cursor.count = 0;
    cursor.has_previous = false;
}
inline void cancel(ManeuverCursor& cursor)
{
    cursor.complete = true;
    cursor.terminal = Result::Cancelled;
}
inline void cancel(ElevationCursor& cursor) { cancelElevation(cursor); }
inline void cancel(ProfileCursor& cursor) { cursor.cancelled = cursor.complete = true; }
inline void cancel(ContourCursor& cursor) { cursor.cancelled = cursor.complete = true; }
inline void cancel(SnapCursor& cursor)
{
    cursor.cancelled = cursor.complete = true;
    cursor.best.edge = UINT32_MAX;
}
inline void cancel(RouteGeometryCursor& cursor) { cursor.cancelled = cursor.complete = true; }
inline void cancel(LegGeometryCursor& cursor)
{
    cursor.cancelled = cursor.complete = true;
}
inline void cancel(NavigationCursor& cursor) { cursor.cancelled = cursor.complete = true; }
inline void cancel(AdministrativeCursor& cursor) { cancelAdministrativeQuery(cursor); }
inline void cancel(ReadCursor& cursor) { cancelRead(cursor); }
inline void cancel(ValidationCursor& cursor) { cancelValidation(cursor); }
} // namespace tmap
