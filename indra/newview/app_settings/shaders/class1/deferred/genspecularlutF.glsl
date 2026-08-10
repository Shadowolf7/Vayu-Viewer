/**
 * @file class1/deferred/genspecularlutF.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

/*[EXTRA_CODE_HERE]*/

in vec2 vary_uv;

out vec4 outColor;

// The real definition lives in deferredUtil.glsl, linked in via mFeatures.isDeferred -- every
// light shader that samples this table calls the same function to build it, rather than this
// LUT carrying its own copy of the curve to fall out of step with.
float evalBlinnPhongSpec(float nh, float glossiness);

void main()
{
    // No flip: unlike genbrdflutF (which matches an external reference implementation's own
    // axis convention), both the write here and the read in pointLightF/spotLightF/etc.
    // (texture(lightFunc, vec2(nh, spec.a))) were authored together against the same axes, so
    // vary_uv maps onto (nh, glossiness) directly.
    outColor = vec4(evalBlinnPhongSpec(vary_uv.s, vary_uv.t), 0.0, 0.0, 1.0);
}
