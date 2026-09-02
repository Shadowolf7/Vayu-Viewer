// -*- ispc -*-
// @file frustum_cull.ispc
// @brief Batch AABB and point frustum culling kernel.
//
// Compiled to SSE2/SSE4/AVX/AVX2 object files with automatic runtime ISA dispatch
// via the ISPC multi-target compilation path.  Included in C++ via the auto-generated
// frustum_cull_ispc.h header.
//
// Plane convention: the signed distance from a point P to a plane is
//   nx*Px + ny*Py + nz*Pz + d
// Positive distance = point is on the outside (back-face) side of the plane.
//
// Only active planes are passed in (dead planes are filtered by the caller),
// so plane_count is always the true count of planes to test — no sentinels needed.

// ---------------------------------------------------------------------------
// AABB batch cull
// ---------------------------------------------------------------------------
// Test 'aabb_count' axis-aligned bounding boxes against 'plane_count' frustum
// planes.  Inputs are in Structure-of-Arrays layout for maximum SIMD efficiency.
//
// results[i]:
//   0 = fully outside (cull)
//   1 = intersecting  (partial — render conservatively)
//   2 = fully inside  (no child cull needed)
//
// All plane_count planes are tested unconditionally for every AABB; there is no
// per-lane early exit.  This is the correct SIMD tradeoff: the 8-wide AVX2 gang
// processes 8 AABBs per loop iteration, so the vectorisation gain (8x) dwarfs
// the extra plane tests relative to a scalar early-exit path (~3.5 avg planes).
export void frustum_cull_aabbs(
    uniform const float planes_nx[],
    uniform const float planes_ny[],
    uniform const float planes_nz[],
    uniform const float planes_d[],
    uniform int         plane_count,
    uniform const float cx[],  // AABB centre x
    uniform const float cy[],  // AABB centre y
    uniform const float cz[],  // AABB centre z
    uniform const float ex[],  // AABB half-extent x
    uniform const float ey[],  // AABB half-extent y
    uniform const float ez[],  // AABB half-extent z
    uniform int         aabb_count,
    uniform int32       results[])
{
    foreach (i = 0 ... aabb_count)
    {
        float vcx = cx[i];
        float vcy = cy[i];
        float vcz = cz[i];
        float vex = ex[i];
        float vey = ey[i];
        float vez = ez[i];

        bool outside  = false;
        bool intersect = false;

        for (uniform int p = 0; p < plane_count; ++p)
        {
            uniform float nx = planes_nx[p];
            uniform float ny = planes_ny[p];
            uniform float nz = planes_nz[p];
            uniform float pd = planes_d[p];

            // Signed distance from AABB centre to this plane
            float dist = nx * vcx + ny * vcy + nz * vcz + pd;

            // Effective projected half-extent onto the plane normal
            float r_eff = abs(nx) * vex + abs(ny) * vey + abs(nz) * vez;

            // dist > r_eff  → centre is farther outside than the box can reach → cull
            outside  = outside  | (dist >  r_eff);
            // dist > -r_eff → centre is within extent-reach of the plane → intersecting
            intersect = intersect | (dist > -r_eff);
        }

        results[i] = outside ? 0 : (intersect ? 1 : 2);
    }
}

// ---------------------------------------------------------------------------
// Point batch cull
// ---------------------------------------------------------------------------
// Test 'point_count' points against 'plane_count' frustum planes.
// A point is "inside" if its signed distance to every plane is <= threshold.
// (Threshold of 0.05 is used by getVisiblePointCloud to accept slightly
//  outside-but-close points as contributing to the shadow volume.)
//
// results[i]: 1 = inside all planes, 0 = outside at least one plane.
export void frustum_cull_points(
    uniform const float planes_nx[],
    uniform const float planes_ny[],
    uniform const float planes_nz[],
    uniform const float planes_d[],
    uniform int         plane_count,
    uniform const float px[],  // point x
    uniform const float py[],  // point y
    uniform const float pz[],  // point z
    uniform int         point_count,
    uniform float       threshold,
    uniform int32       results[])
{
    foreach (i = 0 ... point_count)
    {
        float vpx = px[i];
        float vpy = py[i];
        float vpz = pz[i];

        bool outside = false;

        for (uniform int p = 0; p < plane_count; ++p)
        {
            uniform float nx = planes_nx[p];
            uniform float ny = planes_ny[p];
            uniform float nz = planes_nz[p];
            uniform float pd = planes_d[p];

            float dist = nx * vpx + ny * vpy + nz * vpz + pd;
            outside = outside | (dist > threshold);
        }

        results[i] = outside ? 0 : 1;
    }
}
