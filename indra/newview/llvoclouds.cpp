/**
 * @file llvoclouds.cpp
 * @brief Implementation of LLVOClouds class which is a derivation of LLViewerObject
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
 * Puff coloring rewritten against the current PBR/EE LLSettingsSky lighting
 * accessors (the original used the legacy Windlight gSky color getters,
 * which no longer reflect the active sky). The black-ambient workaround
 * below is adapted from Cool VL Viewer's equivalent fix, substituting
 * getSunDiffuse()/getMoonDiffuse() for its mSunLightColor/mMoonLightColor
 * (gPipeline members that don't exist in this codebase).
 */

#include "llviewerprecompiledheaders.h"

#include "llvoclouds.h"

#include "llenvironment.h"
#include "llsettingssky.h"

#include "llagent.h"            // to get camera position
#include "lldrawable.h"
#include "llface.h"
#include "llspatialpartition.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewertexturelist.h"
#include "llworld.h"
#include "pipeline.h"

LLVOClouds::LLVOClouds(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp)
:   LLAlphaObject(id, LL_VO_CLOUDS, regionp),
    mCloudGroupp(NULL)
{
    mbCanSelect = false;
    setNumTEs(1);
    // Shipped locally (skins/default/textures/cloud-particle.j2c) rather than
    // fetched over the network — it's bundled either way, so there's no
    // point depending on a live asset fetch for it.
    LLViewerTexture *image = LLViewerTextureManager::getFetchedTextureFromFile("cloud-particle.j2c", FTT_LOCAL_FILE, true, LLGLTexture::BOOST_CLOUDS);
    setTEImage(0, image);
}

void LLVOClouds::idleUpdate(LLAgent &agent, const F64 &time)
{
    if (!mDrawable || !gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_CLOUDS))
    {
        return;
    }

    // Set dirty flag so the renderer will rebuild the primitive
    gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_VOLUME);

    // Compute and cache this frame's cloud color.
    LLSettingsSky::ptr_t skyp = LLEnvironment::instance().getCurrentSky();
    if (!skyp)
    {
        return; // paranoia
    }

    // With PBR, some published EE midday settings set the ambient light to
    // black (a hack to fix an unrelated shiny-material hue issue), which
    // would otherwise leave clouds rendering pitch-black. Fall back to the
    // sun/moon diffuse color as a stand-in ambient when that happens.
    static LLCachedControl<F32> min_ambient(gSavedSettings, "CloudsMinAmbientThreshold");
    static LLCachedControl<F32> adjustment(gSavedSettings, "CloudsDiffuseLightAdjustment");
    LLColor3 total_ambient(skyp->getTotalAmbient());
    if (total_ambient.brightness() < (F32) min_ambient)
    {
        bool sun_up = LLEnvironment::instance().getIsSunUp();
        const LLColor3 &diffuse = sun_up ? skyp->getSunDiffuse() : skyp->getMoonDiffuse();
        // No separate ambient-color substitute available here (unlike Cool
        // VL's mSunLightColor/mMoonLightColor), so approximate the original
        // diffuse+ambient sum by doubling the diffuse color instead.
        mCloudsColor = diffuse * 2.f;
        // Inlined equivalent of Cool VL's LLColor3::adjust(), which this
        // codebase's LLColor3 doesn't have: scale by adjustment, but if that
        // would push the brightest channel past 1.0, rescale so it caps at
        // exactly 1.0 instead (preserves hue instead of letting channels
        // clip independently). No-op if adjustment is negative.
        F32 clamped_adjustment = (F32) adjustment;
        if (clamped_adjustment >= 0.f)
        {
            F32 max_channel = llmax(mCloudsColor.mV[0], mCloudsColor.mV[1], mCloudsColor.mV[2]);
            if (max_channel > 0.f && max_channel * clamped_adjustment > 1.f)
            {
                clamped_adjustment = 1.f / max_channel;
            }
            mCloudsColor *= clamped_adjustment;
        }
    }
    else
    {
        mCloudsColor = skyp->getLightDiffuse() + LLColor3(skyp->getTotalAmbient());
    }
}

void LLVOClouds::setPixelAreaAndAngle(LLAgent &agent)
{
    mAppAngle = 50;
    mPixelArea = 1500 * 100;
}

void LLVOClouds::updateTextures()
{
    getTEImage(0)->addTextureStats(mPixelArea);
}

LLDrawable *LLVOClouds::createDrawable(LLPipeline *pipeline)
{
    pipeline->allocDrawable(this);
    mDrawable->setLit(false);
    mDrawable->setRenderType(LLPipeline::RENDER_TYPE_CLOUDS);
    return mDrawable;
}

bool LLVOClouds::updateGeometry(LLDrawable *drawable)
{
    S32 num_parts = mCloudGroupp->getNumPuffs();
    LLSpatialGroup *group = drawable->getSpatialGroup();
    if (!group && num_parts)
    {
        drawable->movePartition();
        group = drawable->getSpatialGroup();
    }

    if (group && group->isVisible())
    {
        dirtySpatialGroup();
    }

    if (!num_parts)
    {
        if (group && drawable->getNumFaces())
        {
            group->setState(LLSpatialGroup::GEOM_DIRTY);
        }
        drawable->setNumFaces(0, NULL, getTEImage(0));
        return true;
    }

    if (!gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_CLOUDS))
    {
        return true;
    }

    if (num_parts > drawable->getNumFaces())
    {
        drawable->setNumFacesFast(num_parts + num_parts / 4, NULL, getTEImage(0));
    }

    mDepth = (getPositionAgent() - LLViewerCamera::getInstance()->getOrigin()) * LLViewerCamera::getInstance()->getAtAxis();

    LLFace *facep;
    S32 face_indx = 0;
    for ( ; face_indx < num_parts; face_indx++)
    {
        facep = drawable->getFace(face_indx);
        if (!facep)
        {
            continue;
        }

        facep->setTEOffset(face_indx);
        facep->setSize(4, 6);
        facep->setViewerObject(this);

        const LLCloudPuff &puff = mCloudGroupp->getPuff(face_indx);
        facep->mCenterLocal = gAgent.getPosAgentFromGlobal(puff.getPositionGlobal());
        facep->setFaceColor(LLColor4(mCloudsColor, puff.getAlpha()));
        facep->setDiffuseMap(getTEImage(0));
    }
    for (S32 count = drawable->getNumFaces(); face_indx < count; face_indx++)
    {
        facep = drawable->getFace(face_indx);
        if (facep)
        {
            facep->setTEOffset(face_indx);
            facep->setSize(0, 0);
        }
    }

    drawable->movePartition();

    return true;
}

F32 LLVOClouds::getPartSize(S32 idx)
{
    return (CLOUD_PUFF_HEIGHT + CLOUD_PUFF_WIDTH) * 0.5f;
}

void LLVOClouds::getGeometry(S32 idx,
                              LLStrider<LLVector4a>& verticesp,
                              LLStrider<LLVector3>& normalsp,
                              LLStrider<LLVector2>& texcoordsp,
                              LLStrider<LLColor4U>& colorsp,
                              LLStrider<LLColor4U>& emissivep,
                              LLStrider<U16>& indicesp)
{
    if (idx >= mCloudGroupp->getNumPuffs())
    {
        return;
    }

    LLFace *facep = mDrawable->getFace(idx);
    if (!facep || !facep->hasGeometry())
    {
        return;
    }

    const LLCloudPuff &puff = mCloudGroupp->getPuff(idx);

    LLColor4 float_color(mCloudsColor, puff.getAlpha());
    facep->setFaceColor(float_color);

    LLVector4a part_pos_agent;
    part_pos_agent.load3(facep->mCenterLocal.mV);
    LLVector4a at;
    at.load3(LLViewerCamera::getInstance()->getAtAxis().mV);
    LLVector4a up(0.f, 0.f, 1.f);
    LLVector4a right;

    right.setCross3(at, up);
    right.normalize3fast();
    up.setCross3(right, at);
    up.normalize3fast();
    right.mul(0.5f * CLOUD_PUFF_WIDTH);
    up.mul(0.5f * CLOUD_PUFF_HEIGHT);

    // *HACK: the [3] = 0.f below sets the texture index to 0 (clouds don't
    // use texture batching) — there's a 4th float stored after the vertex
    // position that's used as a texture index, same trick LLVOPartGroup uses.
    LLVector4a ppapu;
    LLVector4a ppamu;

    ppapu.setAdd(part_pos_agent, up);
    ppamu.setSub(part_pos_agent, up);

    verticesp->setSub(ppapu, right);
    (*verticesp++).getF32ptr()[3] = 0.f;
    verticesp->setSub(ppamu, right);
    (*verticesp++).getF32ptr()[3] = 0.f;
    verticesp->setAdd(ppapu, right);
    (*verticesp++).getF32ptr()[3] = 0.f;
    verticesp->setAdd(ppamu, right);
    (*verticesp++).getF32ptr()[3] = 0.f;

    LLColor4U color;
    color.setVec(float_color);
    *colorsp++ = color;
    *colorsp++ = color;
    *colorsp++ = color;
    *colorsp++ = color;

    // Note: LLCloudPartition's vertex data mask (LLDrawPoolAlpha::VERTEX_DATA_MASK)
    // doesn't include MAP_EMISSIVE, so emissivep isn't backed by real buffer
    // space here — matching Cool VL's implementation, which also doesn't write it.

    LLVector3 normal(0.f, 0.f, -1.f);
    *normalsp++ = normal;
    *normalsp++ = normal;
    *normalsp++ = normal;
    *normalsp++ = normal;
}

U32 LLVOClouds::getPartitionType() const
{
    return LLViewerRegion::PARTITION_CLOUD;
}

// virtual
void LLVOClouds::updateDrawable(bool force_damped)
{
    // Force an immediate rebuild on any update
    if (mDrawable.notNull())
    {
        mDrawable->updateXform(true);
        gPipeline.markRebuild(mDrawable);
    }
    clearChanged(SHIFTED);
}

LLCloudPartition::LLCloudPartition(LLViewerRegion *regionp)
:   LLParticlePartition(regionp)
{
    mDrawableType = LLPipeline::RENDER_TYPE_CLOUDS;
    mPartitionType = LLViewerRegion::PARTITION_CLOUD;
}
