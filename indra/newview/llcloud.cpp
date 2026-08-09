/**
 * @file llcloud.cpp
 * @brief Implementation of viewer LLCloudLayer class
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
 * Restored 2026-08-03 from git history (see llcloud.h for the full story).
 */

#include "llviewerprecompiledheaders.h"

#include "llcloud.h"

#include "llgl.h"
#include "patch_code.h"
#include "patch_dct.h"

#include "llagent.h"
#include "llappviewer.h"        // for gFrameTimeSeconds
#include "lldefs.h"             // for gDirOpposite
#include "llstartup.h"          // for LLStartUp::getStartupState()
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvoclouds.h"
#include "llworld.h"
#include "pipeline.h"

constexpr F32 CLOUD_GROW_RATE = 0.05f;
constexpr F32 CLOUD_DECAY_RATE = -0.05f;
constexpr F32 CLOUD_VELOCITY_SCALE = 0.6f;
constexpr F32 CLOUD_DENSITY = 25.f;
constexpr S32 CLOUD_COUNT_MAX = 20;
constexpr F32 CLOUD_HEIGHT_RANGE = 48.f;

//static
F32 LLCloudLayer::sCloudsAltitude = 192.f;   // default at login

enum
{
    LL_PUFF_GROWING = 0,
    LL_PUFF_DYING = 1
};

// Used for the patch decoder
static S32 sPatchBuffer[CLOUD_GRIDS_PER_EDGE * CLOUD_GRIDS_PER_EDGE];

LLCloudPuff::LLCloudPuff()
:   mAlpha(0.01f),
    mRate(CLOUD_GROW_RATE),
    mLifeState(LL_PUFF_GROWING)
{
}

LLCloudGroup::LLCloudGroup()
:   mCloudLayerp(NULL),
    mDensity(0.f),
    mTargetPuffCount(0),
    mLastAltitudeUpdate(0.f),
    mVOCloudsp(NULL)
{
}

void LLCloudGroup::cleanup()
{
    if (mVOCloudsp)
    {
        if (!mVOCloudsp->isDead())
        {
            gObjectList.killObject(mVOCloudsp);
        }
        mVOCloudsp = NULL;
    }
}

void LLCloudGroup::setCenterRegion(F32 x, F32 y)
{
    mLastAltitudeUpdate = gFrameTimeSeconds;
    mCenterRegion.set(x, y, LLCloudLayer::getCloudsAltitude());
    if (mVOCloudsp)
    {
        mVOCloudsp->setPositionRegion(mCenterRegion);

        // TEMPORARY: diagnosing a report of clouds rendering at ground level.
        static F32 last_debug_log_time = 0.f;
        if (gFrameTimeSeconds - last_debug_log_time >= 5.f)
        {
            last_debug_log_time = gFrameTimeSeconds;
            LL_INFOS("Clouds") << "setCenterRegion DEBUG: mCenterRegion=" << mCenterRegion
                                << " vocloud_posRegion=" << mVOCloudsp->getPositionRegion()
                                << " vocloud_posAgent=" << mVOCloudsp->getPositionAgent()
                                << " vocloud_posGlobal=" << mVOCloudsp->getPositionGlobal()
                                << LL_ENDL;
        }
    }
}

void LLCloudGroup::updatePuffs(F32 dt)
{
    mDensity = mCloudLayerp->getDensityRegion(mCenterRegion);

    LLViewerRegion *regionp = mCloudLayerp->getRegion();
    if (!regionp)
    {
        return;
    }

    if (!mVOCloudsp || gFrameTimeSeconds - mLastAltitudeUpdate >= 10.f)
    {
        // Account for possible altitude change
        setCenterRegion(mCenterRegion.mV[VX], mCenterRegion.mV[VY]);
    }

    if (!mVOCloudsp)
    {
        mVOCloudsp = (LLVOClouds *)gObjectList.createObjectViewer(LLViewerObject::LL_VO_CLOUDS, regionp);
        mVOCloudsp->setCloudGroup(this);
        mVOCloudsp->setPositionRegion(mCenterRegion);
        F32 hsize = mCloudLayerp->getMetersPerEdge() / CLOUD_GROUPS_PER_EDGE + CLOUD_PUFF_WIDTH;
        mVOCloudsp->setScale(LLVector3(hsize, hsize, CLOUD_HEIGHT_RANGE + CLOUD_PUFF_HEIGHT) * 0.5f);
        gPipeline.createObject(mVOCloudsp);
    }

    LLVector3 velocity;
    LLVector3d vel_d;
    // Update the positions of all of the puffs
    for (U32 i = 0, count = (U32) mCloudPuffs.size(); i < count; i++)
    {
        LLCloudPuff &puff = mCloudPuffs[i];
        velocity = regionp->mWind.getCloudVelocity(regionp->getPosRegionFromGlobal(puff.mPositionGlobal));
        velocity *= CLOUD_VELOCITY_SCALE * dt;
        vel_d.set(velocity);
        puff.mPositionGlobal += vel_d;
        puff.mAlpha += puff.mRate * dt;
        puff.mAlpha = llclamp(puff.mAlpha, 0.f, 1.f);
    }
}

void LLCloudGroup::updatePuffOwnership()
{
    U32 i = 0;
    while (i < mCloudPuffs.size())
    {
        if (mCloudPuffs[i].getLifeState() == LL_PUFF_DYING)
        {
            i++;
            continue;
        }
        if (inGroup(mCloudPuffs[i]))
        {
            i++;
            continue;
        }

        LLCloudGroup *new_cgp = LLWorld::getInstance()->findCloudGroup(mCloudPuffs[i]);
        if (!new_cgp)
        {
            mCloudPuffs[i].setLifeState(LL_PUFF_DYING);
            mCloudPuffs[i].mRate = CLOUD_DECAY_RATE;
            i++;
            continue;
        }

        LLCloudPuff puff;
        puff.mPositionGlobal = mCloudPuffs[i].mPositionGlobal;
        puff.mAlpha = mCloudPuffs[i].mAlpha;
        mCloudPuffs.erase(mCloudPuffs.begin() + i);
        new_cgp->mCloudPuffs.push_back(puff);
    }
}

void LLCloudGroup::updatePuffCount()
{
    if (!mVOCloudsp)
    {
        return;
    }

    S32 target_puff_count = static_cast<S32>(std::llround(CLOUD_DENSITY * mDensity));
    target_puff_count = llclamp(target_puff_count, 0, CLOUD_COUNT_MAX);
    S32 current_puff_count = (S32) mCloudPuffs.size();

    // Create new puffs if we need them
    if (current_puff_count < target_puff_count)
    {
        F32 hsize = mCloudLayerp->getMetersPerEdge() / CLOUD_GROUPS_PER_EDGE;
        LLVector3d puff_pos_global;
        mCloudPuffs.resize(target_puff_count);
        for (S32 i = current_puff_count; i < target_puff_count; i++)
        {
            puff_pos_global = mVOCloudsp->getPositionGlobal();
            F32 x = ll_frand(hsize) - 0.5f * hsize;
            F32 y = ll_frand(hsize) - 0.5f * hsize;
            F32 z = ll_frand(CLOUD_HEIGHT_RANGE) - 0.5f * CLOUD_HEIGHT_RANGE;
            puff_pos_global += LLVector3d(x, y, z);
            mCloudPuffs[i].mPositionGlobal = puff_pos_global;
            mCloudPuffs[i].mAlpha = 0.01f;
        }
    }

    // Count the number of live puffs
    S32 live_puff_count = 0;
    for (S32 i = 0, count = (S32) mCloudPuffs.size(); i < count; i++)
    {
        if (mCloudPuffs[i].getLifeState() != LL_PUFF_DYING)
        {
            live_puff_count++;
        }
    }

    // Start killing enough puffs so the live puff count == target puff count
    S32 new_dying_count = llmax(0, live_puff_count - target_puff_count);
    S32 i = 0;
    while (new_dying_count > 0)
    {
        if (mCloudPuffs[i].getLifeState() != LL_PUFF_DYING)
        {
            mCloudPuffs[i].setLifeState(LL_PUFF_DYING);
            mCloudPuffs[i].mRate = CLOUD_DECAY_RATE;
            new_dying_count--;
        }
        i++;
    }

    // Remove fully dead puffs
    i = 0;
    while (i < (S32) mCloudPuffs.size())
    {
        if (mCloudPuffs[i].isDead())
        {
            mCloudPuffs.erase(mCloudPuffs.begin() + i);
        }
        else
        {
            i++;
        }
    }
}

bool LLCloudGroup::inGroup(const LLCloudPuff &puff) const
{
    LLViewerRegion *regionp = mCloudLayerp->getRegion();
    if (!regionp)
    {
        return false;
    }

    // Do min/max check on center of the cloud puff
    F32 delta = mCloudLayerp->getMetersPerEdge() / CLOUD_GROUPS_PER_EDGE * 0.5f;
    F32 min_x = mCenterRegion.mV[VX] - delta;
    F32 min_y = mCenterRegion.mV[VY] - delta;
    F32 max_x = mCenterRegion.mV[VX] + delta;
    F32 max_y = mCenterRegion.mV[VY] + delta;

    LLVector3 pos_region = regionp->getPosRegionFromGlobal(puff.getPositionGlobal());

    if (pos_region.mV[VX] < min_x || pos_region.mV[VY] < min_y ||
        pos_region.mV[VX] > max_x || pos_region.mV[VY] > max_y)
    {
        return false;
    }
    return true;
}

LLCloudLayer::LLCloudLayer()
:   mOriginGlobal(0.f, 0.f, 0.f),
    mMetersPerEdge(1.f),
    mMetersPerGrid(1.f),
    mWindp(NULL),
    mRegionp(NULL),
    mDensityp(NULL),
    mLastDensityUpdate(0.f)
{
    for (S32 i = 0; i < 4; i++)
    {
        mNeighbors[i] = NULL;
    }

    for (S32 i = 0; i < CLOUD_GROUPS_PER_EDGE; i++)
    {
        for (S32 j = 0; j < CLOUD_GROUPS_PER_EDGE; j++)
        {
            mCloudGroups[i][j].setCloudLayerp(this);
        }
    }
}

LLCloudLayer::~LLCloudLayer()
{
    destroy();
}

//static
F32 LLCloudLayer::getCloudsAltitude()
{
    static LLCachedControl<S32> clouds_altitude(gSavedSettings, "ClassicCloudsAvgAlt");
    static LLCachedControl<U32> max_clouds_alt(gSavedSettings, "ClassicCloudsMaxAlt");

    if (clouds_altitude > 0)
    {
        sCloudsAltitude = (F32) clouds_altitude;
    }
    // Wait until fully logged in before using the agent altitude (which is 0
    // until we get the region data and the correct agent position).
    else if (LLStartUp::getStartupState() == STATE_STARTED)
    {
        sCloudsAltitude = gAgent.getPositionAgent().mV[VZ] - (F32) clouds_altitude;
    }
    constexpr F32 MIN_ALT = CLOUD_HEIGHT_RANGE + CLOUD_PUFF_HEIGHT * 0.5f;
    sCloudsAltitude = llclamp(sCloudsAltitude, MIN_ALT, (F32) max_clouds_alt);

    // TEMPORARY: diagnosing a report of clouds rendering at ground level
    // despite a positive (absolute) ClassicCloudsAvgAlt. Throttled to avoid
    // flooding the log, since this is called once per cloud group.
    static F32 last_debug_log_time = 0.f;
    if (gFrameTimeSeconds - last_debug_log_time >= 5.f)
    {
        last_debug_log_time = gFrameTimeSeconds;
        LL_INFOS("Clouds") << "getCloudsAltitude DEBUG: raw_setting=" << (S32) clouds_altitude
                            << " agent_Z=" << gAgent.getPositionAgent().mV[VZ]
                            << " MIN_ALT=" << MIN_ALT
                            << " max_clouds_alt=" << (U32) max_clouds_alt
                            << " -> sCloudsAltitude=" << sCloudsAltitude << LL_ENDL;
    }

    return sCloudsAltitude;
}

//static
bool LLCloudLayer::needClassicClouds()
{
    // Do not use clouds if they are not wanted, or when the camera is
    // underwater (clouds flicker when looking up at the water surface).
    static LLCachedControl<bool> use_classic_clouds(gSavedSettings, "SkyUseClassicClouds");
    if (!use_classic_clouds || LLViewerCamera::getInstance()->cameraUnderWater())
    {
        return false;
    }

    // Do not bother if they are beyond the draw distance.
    static LLCachedControl<F32> draw_distance(gSavedSettings, "RenderFarClip");
    F32 delta = fabsf(sCloudsAltitude - LLViewerCamera::getInstance()->getOrigin().mV[VZ]);
    return delta < draw_distance + CLOUD_HEIGHT_RANGE;
}

void LLCloudLayer::create(LLViewerRegion *regionp)
{
    llassert(regionp);
    setRegion(regionp);
    mDensityp = new F32[CLOUD_GRIDS_PER_EDGE * CLOUD_GRIDS_PER_EDGE];
    for (U32 i = 0; i < CLOUD_GRIDS_PER_EDGE * CLOUD_GRIDS_PER_EDGE; i++)
    {
        mDensityp[i] = 0.f;
    }
}

void LLCloudLayer::setRegion(LLViewerRegion *regionp)
{
    mRegionp = regionp;
    if (regionp)
    {
        // Variable region size support
        mMetersPerEdge = regionp->getWidth();
        mMetersPerGrid = mMetersPerEdge / CLOUD_GRIDS_PER_EDGE;

        for (S32 i = 0; i < CLOUD_GROUPS_PER_EDGE; i++)
        {
            F32 y = (0.5f + i) * mMetersPerEdge / CLOUD_GROUPS_PER_EDGE;
            for (S32 j = 0; j < CLOUD_GROUPS_PER_EDGE; j++)
            {
                F32 x = (0.5f + j) * mMetersPerEdge / CLOUD_GROUPS_PER_EDGE;
                mCloudGroups[i][j].setCenterRegion(x, y);
            }
        }
    }
}

void LLCloudLayer::destroy()
{
    reset();
    delete [] mDensityp;
    mDensityp = NULL;
    mWindp = NULL;
}

void LLCloudLayer::reset()
{
    // Kill all of the existing puffs
    for (S32 i = 0; i < CLOUD_GROUPS_PER_EDGE; i++)
    {
        for (S32 j = 0; j < CLOUD_GROUPS_PER_EDGE; j++)
        {
            mCloudGroups[i][j].cleanup();
        }
    }
}

void LLCloudLayer::setWindPointer(LLWind *windp)
{
    if (mWindp)
    {
        mWindp->setCloudDensityPointer(NULL);
    }
    mWindp = windp;
    if (mWindp)
    {
        mWindp->setCloudDensityPointer(mDensityp);
    }
}

F32 LLCloudLayer::getDensityRegion(const LLVector3 &pos_region)
{
    // "position" is region-local
    S32 i, j, ii, jj;

    i = lltrunc(pos_region.mV[VX] / mMetersPerGrid);
    j = lltrunc(pos_region.mV[VY] / mMetersPerGrid);
    ii = i + 1;
    jj = j + 1;

    // clamp
    if (i >= (S32) CLOUD_GRIDS_PER_EDGE)
    {
        i = CLOUD_GRIDS_PER_EDGE - 1;
        ii = i;
    }
    else if (i < 0)
    {
        i = 0;
        ii = i;
    }
    else if (ii >= (S32) CLOUD_GRIDS_PER_EDGE || ii < 0)
    {
        ii = i;
    }

    if (j >= (S32) CLOUD_GRIDS_PER_EDGE)
    {
        j = CLOUD_GRIDS_PER_EDGE - 1;
        jj = j;
    }
    else if (j < 0)
    {
        j = 0;
        jj = j;
    }
    else if (jj >= (S32) CLOUD_GRIDS_PER_EDGE || jj < 0)
    {
        jj = j;
    }

    F32 dx = (pos_region.mV[VX] - (F32) i * mMetersPerGrid) / mMetersPerGrid;
    F32 dy = (pos_region.mV[VY] - (F32) j * mMetersPerGrid) / mMetersPerGrid;
    F32 omdx = 1.f - dx;
    F32 omdy = 1.f - dy;

    F32 density = dx * dy * *(mDensityp + ii + jj * CLOUD_GRIDS_PER_EDGE) +
                  dx * omdy * *(mDensityp + i + jj * CLOUD_GRIDS_PER_EDGE) +
                  omdx * dy * *(mDensityp + ii + j * CLOUD_GRIDS_PER_EDGE) +
                  omdx * omdy * *(mDensityp + i + j * CLOUD_GRIDS_PER_EDGE);

    return density;
}

void LLCloudLayer::decompress(LLBitPack &bitpack, LLGroupHeader *group_headerp)
{
    LLPatchHeader patch_header;

    init_patch_decompressor(group_headerp->patch_size);

    // Don't use the packed group_header stride because the strides used on
    // simulator and viewer are not equal.
    group_headerp->stride = group_headerp->patch_size;     // offset required to step up one row
    set_group_of_patch_header(group_headerp);

    decode_patch_header(bitpack, &patch_header);
    decode_patch(bitpack, sPatchBuffer);
    decompress_patch(mDensityp, sPatchBuffer, &patch_header);

    // Real data arrived — mark density as freshly updated so
    // generateDensity()'s synthetic fallback doesn't clobber it.
    mLastDensityUpdate = gFrameTimeSeconds;
}

bool LLCloudLayer::shouldUpdateDensity()
{
    // Not more often than once a second
    return gFrameTimeSeconds - mLastDensityUpdate >= 1.f;
}

// Called each time the viewer processes a wind layer-data packet for a region
// that isn't sending real classic-cloud layer data (the common case on
// modern simulators — this is a client-side substitute data source). Grows
// and diffuses a plausible density field locally, in place of what a
// classic-clouds-enabled sim would otherwise stream in via decompress().
void LLCloudLayer::generateDensity()
{
    if (mLastDensityUpdate == 0.f)
    {
        for (U32 i = 0; i < CLOUD_GRIDS_PER_EDGE * CLOUD_GRIDS_PER_EDGE; i++)
        {
            // Limits deduced from values sampled in old, classic-clouds
            // enabled SL sim servers.
            mDensityp[i] = ll_frand(4.f) - 1.f;
        }
        mLastDensityUpdate = gFrameTimeSeconds;
    }
    else if (shouldUpdateDensity())
    {
        // Update the density probability matrix by averaging the value of
        // surrounding cells for each cell, with a weight factor of 2 for the
        // cell value itself and by adding a small random factor, i.e.:
        //   average = (2*this_cell + neighbors_total)/(neighbors+2) + rand
        // For edges, we "wrap" around north/south west/east rows/columns.
        static F32 buffer[CLOUD_GRIDS_PER_EDGE * CLOUD_GRIDS_PER_EDGE];
        for (S32 x = 0; x < (S32) CLOUD_GRIDS_PER_EDGE; x++)
        {
            S32 west = x - 1;
            if (west < 0)
            {
                west = CLOUD_GRIDS_PER_EDGE - 2;    // wrap edges
            }
            S32 east = x + 1;
            if (east >= (S32) CLOUD_GRIDS_PER_EDGE)
            {
                east = 0;                           // wrap edges
            }
            for (S32 y = 0; y < (S32) CLOUD_GRIDS_PER_EDGE; y++)
            {
                S32 south = y - 1;
                if (south < 0)
                {
                    south = CLOUD_GRIDS_PER_EDGE - 2;  // wrap edges
                }
                S32 north = y + 1;
                if (north >= (S32) CLOUD_GRIDS_PER_EDGE)
                {
                    north = 0;                         // wrap edges
                }
                north *= CLOUD_GRIDS_PER_EDGE;
                south *= CLOUD_GRIDS_PER_EDGE;
                S32 here = y * CLOUD_GRIDS_PER_EDGE;
                F32 average = 2.f * mDensityp[here + x];
                average += mDensityp[north + west] + mDensityp[north + x];
                average += mDensityp[north + east] + mDensityp[here + east];
                average += mDensityp[south + east] + mDensityp[south + x];
                average += mDensityp[south + west] + mDensityp[here + west];
                average = llclamp(average * 0.1f + ll_frand(0.5f) - 0.25f, -1.f, 3.f);
                buffer[here + x] = average;
            }
        }
        memcpy((void *) mDensityp, (void *) buffer, sizeof(F32) * CLOUD_GRIDS_PER_EDGE * CLOUD_GRIDS_PER_EDGE);
        mLastDensityUpdate = gFrameTimeSeconds;
    }
}

void LLCloudLayer::resetDensity()
{
    if (mLastDensityUpdate > 0.f)
    {
        for (U32 i = 0; i < CLOUD_GRIDS_PER_EDGE * CLOUD_GRIDS_PER_EDGE; i++)
        {
            mDensityp[i] = 0.f;
        }
        reset();
        mLastDensityUpdate = 0.f;
    }
}

void LLCloudLayer::updatePuffs(F32 dt)
{
    for (S32 i = 0; i < CLOUD_GROUPS_PER_EDGE; i++)
    {
        for (S32 j = 0; j < CLOUD_GROUPS_PER_EDGE; j++)
        {
            mCloudGroups[i][j].updatePuffs(dt);
        }
    }
}

void LLCloudLayer::updatePuffOwnership()
{
    for (S32 i = 0; i < CLOUD_GROUPS_PER_EDGE; i++)
    {
        for (S32 j = 0; j < CLOUD_GROUPS_PER_EDGE; j++)
        {
            mCloudGroups[i][j].updatePuffOwnership();
        }
    }
}

void LLCloudLayer::updatePuffCount()
{
    for (S32 i = 0; i < CLOUD_GROUPS_PER_EDGE; i++)
    {
        for (S32 j = 0; j < CLOUD_GROUPS_PER_EDGE; j++)
        {
            mCloudGroups[i][j].updatePuffCount();
        }
    }
}

LLCloudGroup *LLCloudLayer::findCloudGroup(const LLCloudPuff &puff)
{
    for (S32 i = 0; i < CLOUD_GROUPS_PER_EDGE; i++)
    {
        for (S32 j = 0; j < CLOUD_GROUPS_PER_EDGE; j++)
        {
            if (mCloudGroups[i][j].inGroup(puff))
            {
                return &(mCloudGroups[i][j]);
            }
        }
    }
    return NULL;
}

void LLCloudLayer::connectNeighbor(LLCloudLayer *cloudp, U32 direction)
{
    if (direction >= 4)
    {
        // Only care about cardinal 4 directions.
        return;
    }

    if (!cloudp && mNeighbors[direction])
    {
        mNeighbors[direction]->mNeighbors[gDirOpposite[direction]] = NULL;
    }

    mNeighbors[direction] = cloudp;
    if (cloudp)
    {
        cloudp->mNeighbors[gDirOpposite[direction]] = this;
    }
}

void LLCloudLayer::disconnectNeighbor(U32 direction)
{
    if (direction >= 4)
    {
        // Only care about cardinal 4 directions.
        return;
    }

    LLCloudLayer *cloudp = mNeighbors[direction];
    if (cloudp)
    {
        cloudp->mNeighbors[gDirOpposite[direction]] = NULL;
        mNeighbors[direction] = NULL;
    }
}

void LLCloudLayer::disconnectAllNeighbors()
{
    for (S32 i = 0; i < 4; i++)
    {
        disconnectNeighbor(i);
    }
}
