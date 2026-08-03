/**
 * @file llvoclouds.h
 * @brief Description of LLVOClouds class
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

#ifndef LL_LLVOCLOUDS_H
#define LL_LLVOCLOUDS_H

#include "llviewerobject.h"
#include "v3color.h"

class LLCloudGroup;

class LLVOClouds : public LLAlphaObject
{
public:
    LLVOClouds(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp);

    /*virtual*/ void updateDrawable(bool force_damped);

    /*virtual*/ LLDrawable *createDrawable(LLPipeline *pipeline);
    /*virtual*/ bool        updateGeometry(LLDrawable *drawable);
    /*virtual*/ void        getGeometry(S32 idx,
                                LLStrider<LLVector4a>& verticesp,
                                LLStrider<LLVector3>& normalsp,
                                LLStrider<LLVector2>& texcoordsp,
                                LLStrider<LLColor4U>& colorsp,
                                LLStrider<LLColor4U>& emissivep,
                                LLStrider<U16>& indicesp);

    /*virtual*/ bool isActive() const { return true; } // Whether this object needs to do an idleUpdate.
    F32 getPartSize(S32 idx);

    /*virtual*/ void updateTextures();
    /*virtual*/ void setPixelAreaAndAngle(LLAgent &agent); // generate accurate apparent angle and area

    void updateFaceSize(S32 idx) { }
    /*virtual*/ void idleUpdate(LLAgent &agent, const F64 &time);

    virtual U32 getPartitionType() const;

    void setCloudGroup(LLCloudGroup *cgp)      { mCloudGroupp = cgp; }

protected:
    virtual ~LLVOClouds() = default;

    LLCloudGroup *mCloudGroupp;
    LLColor3      mCloudsColor;
};

extern LLUUID gCloudTextureID;

#endif // LL_LLVOCLOUDS_H
