/**
 * @file llwind.h
 * @brief LLWind class header file
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLWIND_H
#define LL_LLWIND_H

#include "llmath.h"
#include "v3math.h"
#include "v3dmath.h"

class LLVector3;
class LLBitPack;
class LLGroupHeader;

const F32 WIND_SCALE_HACK       = 2.0f; // hack to make wind speeds more realistic


class LLWind
{
public:
    LLWind();
    ~LLWind();
    void renderVectors();
    LLVector3 getVelocity(const LLVector3 &location); // "location" is region-local
    // "location" is region-local. Restored 2026-08-03 (see llcloud.h) —
    // reverts a hunk of storm-1622 ("fix viewer-side wind turbulence
    // simulation") that stripped the cloud-velocity field five months
    // after classic clouds were removed. Cloud puffs drift by this field,
    // not by getVelocity(), so it can diverge from plain wind for a clumping
    // effect (see CLOUD_DIVERGENCE_COEF in llwind.cpp).
    LLVector3 getCloudVelocity(const LLVector3 &location);
    LLVector3 getVelocityNoisy(const LLVector3 &location, const F32 dim);   // "location" is region-local

    void decompress(LLBitPack &bitpack, LLGroupHeader *group_headerp);
    LLVector3 getAverage();
    // Points this wind at the region's cloud density grid (LLCloudLayer owns
    // the storage); decompress() reads it to compute the cloud-velocity
    // divergence. Null when no cloud layer is wired up.
    void setCloudDensityPointer(F32 *densityp);

    void setOriginGlobal(const LLVector3d &origin_global);

    // Variable region size support: the owning LLViewerRegion's actual width
    // in meters, used to scale the 16x16 wind/cloud-velocity grid to world
    // space. Defaults to 256m (the standard region size) until the owner
    // calls this with its real width.
    void setRegionWidth(F32 width) { mRegionWidth = width; }

private:
    S32 mSize;
    F32 mRegionWidth = 256.f;
    F32 * mVelX;
    F32 * mVelY;
    F32 * mCloudVelX;
    F32 * mCloudVelY;
    F32 * mCloudDensityp;

    LLVector3d mOriginGlobal;
    void init();

    LOG_CLASS(LLWind);
};

#endif
