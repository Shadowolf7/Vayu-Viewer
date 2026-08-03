/**
 * @file llcloud.h
 * @brief Description of viewer LLCloudLayer class
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
 *
 * Restored 2026-08-03: this file was part of the official Linden Lab viewer
 * until it was removed in 2011 (storm-1189, "remove classic clouds"). Restored
 * here from git history (commit fb9ef6233125d55d47104333d6b0062509850ff4^),
 * modernized against the current codebase, and extended with the client-side
 * density synthesis, altitude settings, and gating logic that Cool VL Viewer
 * (a different, independently-maintained SL viewer fork that kept this
 * feature) added over the years — most importantly, generateDensity(): modern
 * simulators mostly no longer send real cloud-layer data (that's *why* LL
 * removed this in 2011), so without it mDensityp would just stay empty.
 */

#ifndef LL_LLCLOUD_H
#define LL_LLCLOUD_H

#include "llframetimer.h"
#include "llmath.h"
#include "llpointer.h"
#include "v3dmath.h"
#include "v3math.h"
#include "v4color.h"

constexpr U32 CLOUD_GRIDS_PER_EDGE = 16;

constexpr F32 CLOUD_PUFF_WIDTH  = 64.f;
constexpr F32 CLOUD_PUFF_HEIGHT = 48.f;

class LLWind;
class LLVOClouds;
class LLViewerRegion;
class LLCloudLayer;
class LLBitPack;
class LLGroupHeader;

constexpr S32 CLOUD_GROUPS_PER_EDGE = 4;

class LLCloudPuff
{
    friend class LLCloudGroup;

public:
    LLCloudPuff();

    void updatePuffs(F32 dt);
    void updatePuffOwnership();

    const LLVector3d& getPositionGlobal() const    { return mPositionGlobal; }
    F32 getAlpha() const                           { return mAlpha; }
    U32 getLifeState() const                       { return mLifeState; }
    void setLifeState(U32 state)                   { mLifeState = state; }
    bool isDead() const                            { return mAlpha <= 0.f; }

protected:
    F32         mAlpha;
    F32         mRate;
    LLVector3d  mPositionGlobal;
    U32         mLifeState;
};

class LLCloudGroup
{
public:
    LLCloudGroup();

    void cleanup();

    void setCloudLayerp(LLCloudLayer *clp)         { mCloudLayerp = clp; }
    void setCenterRegion(F32 x, F32 y);

    void updatePuffs(F32 dt);
    void updatePuffOwnership();
    void updatePuffCount();

    bool inGroup(const LLCloudPuff &puff) const;

    F32 getDensity() const                         { return mDensity; }
    S32 getNumPuffs() const                         { return (S32) mCloudPuffs.size(); }
    const LLCloudPuff &getPuff(S32 i)               { return mCloudPuffs[i]; }

protected:
    LLCloudLayer            *mCloudLayerp;
    std::vector<LLCloudPuff> mCloudPuffs;
    LLPointer<LLVOClouds>    mVOCloudsp;
    LLVector3                mCenterRegion;
    F32                      mDensity;
    S32                      mTargetPuffCount;
    F32                      mLastAltitudeUpdate;    // last time altitude was checked
};

class LLCloudLayer
{
public:
    LLCloudLayer();
    ~LLCloudLayer();

    void create(LLViewerRegion *regionp);
    void destroy();

    void reset();       // Clears all active cloud puffs

    // Client-side density synthesis, for regions whose simulator doesn't
    // send real classic-cloud layer data (the common case on modern grids).
    void generateDensity();
    void resetDensity();
    bool shouldUpdateDensity();

    void updatePuffs(F32 dt);
    void updatePuffOwnership();
    void updatePuffCount();

    LLCloudGroup *findCloudGroup(const LLCloudPuff &puff);

    void setRegion(LLViewerRegion *regionp);
    LLViewerRegion *getRegion() const                      { return mRegionp; }
    void setWindPointer(LLWind *windp);
    void setOriginGlobal(const LLVector3d &origin_global)  { mOriginGlobal = origin_global; }
    F32 getMetersPerEdge() const                           { return mMetersPerEdge; }

    F32 getDensityRegion(const LLVector3 &pos_region);     // "position" is in local coordinates

    void decompress(LLBitPack &bitpack, LLGroupHeader *group_header);

    LLCloudLayer *getNeighbor(S32 n) const                 { return mNeighbors[n]; }

    void connectNeighbor(LLCloudLayer *cloudp, U32 direction);
    void disconnectNeighbor(U32 direction);
    void disconnectAllNeighbors();

    // Altitude (absolute if ClassicCloudsAvgAlt > 0, agent-relative
    // otherwise), clamped to [CLOUD_HEIGHT_RANGE + puff-height/2, max].
    static F32 getCloudsAltitude();
    // Gates on the SkyUseClassicClouds toggle, underwater camera, and draw
    // distance.
    static bool needClassicClouds();

public:
    LLVector3d  mOriginGlobal;
    F32         mMetersPerEdge;
    F32         mMetersPerGrid;

protected:
    LLCloudLayer    *mNeighbors[4];
    LLWind          *mWindp;
    LLViewerRegion  *mRegionp;
    F32             *mDensityp;            // the probability density grid
    F32              mLastDensityUpdate;   // last time density was updated (0 = never)

    LLCloudGroup     mCloudGroups[CLOUD_GROUPS_PER_EDGE][CLOUD_GROUPS_PER_EDGE];

    static F32       sCloudsAltitude;
};

#endif
