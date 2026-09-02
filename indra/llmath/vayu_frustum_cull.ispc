// -*- ispc -*-
// @file frustum_cull.ispc
// @brief Batch AABB and point frustum and sphere culling kernels.
//
// Compiled to SSE2/SSE4/AVX/AVX2 object files with automatic runtime ISA dispatch
// via the ISPC multi-target compilation path. Included in C++ via the auto-generated
// frustum_cull_ispc.h header.
//
// Plane convention: the signed distance from a point P to a plane is
//   nx*Px + ny*Py + nz*Pz + d
// Positive distance = point is on the outside (back-face) side of the plane.
//
// Only active planes are passed in (dead planes are filtered by the caller),
// so plane_count is always the true count of planes to test — no sentinels needed.

// ---------------------------------------------------------------------------
// AABB batch cull (Frustum only)
// ---------------------------------------------------------------------------
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

        bool outside   = false;
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
            outside   = outside   | (dist >  r_eff);
            // dist > -r_eff → centre is within extent-reach of the plane → intersecting
            intersect = intersect | (dist > -r_eff);
        }

        results[i] = outside ? 0 : (intersect ? 1 : 2);
    }
}

// ---------------------------------------------------------------------------
// AABB batch cull with Frustum + Draw-Distance Sphere culling
// ---------------------------------------------------------------------------
export void frustum_and_sphere_cull_aabbs(
    uniform const float planes_nx[],
    uniform const float planes_ny[],
    uniform const float planes_nz[],
    uniform const float planes_d[],
    uniform int         plane_count,
    uniform float       cam_x,
    uniform float       cam_y,
    uniform float       cam_z,
    uniform float       sphere_radius_sq, // <= 0.0f means disable sphere check
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

        bool outside   = false;
        bool intersect = false;

        for (uniform int p = 0; p < plane_count; ++p)
        {
            uniform float nx = planes_nx[p];
            uniform float ny = planes_ny[p];
            uniform float nz = planes_nz[p];
            uniform float pd = planes_d[p];

            float dist = nx * vcx + ny * vcy + nz * vcz + pd;
            float r_eff = abs(nx) * vex + abs(ny) * vey + abs(nz) * vez;

            outside   = outside   | (dist >  r_eff);
            intersect = intersect | (dist > -r_eff);
        }

        int32 f_res = outside ? 0 : (intersect ? 1 : 2);

        if (f_res != 0 && sphere_radius_sq > 0.0f)
        {
            float min_x = vcx - vex;
            float min_y = vcy - vey;
            float min_z = vcz - vez;

            float max_x = vcx + vex;
            float max_y = vcy + vey;
            float max_z = vcz + vez;

            // 1. Fully inside sphere check
            float dmin_x = min_x - cam_x;
            float dmin_y = min_y - cam_y;
            float dmin_z = min_z - cam_z;
            float dmin_sq = dmin_x * dmin_x + dmin_y * dmin_y + dmin_z * dmin_z;

            float dmax_x = max_x - cam_x;
            float dmax_y = max_y - cam_y;
            float dmax_z = max_z - cam_z;
            float dmax_sq = dmax_x * dmax_x + dmax_y * dmax_y + dmax_z * dmax_z;

            int32 s_res = 1;
            if (dmin_sq < sphere_radius_sq && dmax_sq < sphere_radius_sq)
            {
                s_res = 2;
            }
            else
            {
                // 2. Outside sphere check (closest point distance to sphere center)
                float tx = (cam_x < min_x) ? (min_x - cam_x) : ((cam_x > max_x) ? (cam_x - max_x) : 0.0f);
                float ty = (cam_y < min_y) ? (min_y - cam_y) : ((cam_y > max_y) ? (cam_y - max_y) : 0.0f);
                float tz = (cam_z < min_z) ? (min_z - cam_z) : ((cam_z > max_z) ? (cam_z - max_z) : 0.0f);
                float d = tx * tx + ty * ty + tz * tz;

                if (d > sphere_radius_sq)
                {
                    s_res = 0;
                }
            }

            f_res = min(f_res, s_res);
        }

        results[i] = f_res;
    }
}

// ---------------------------------------------------------------------------
// Point batch cull
// ---------------------------------------------------------------------------
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
