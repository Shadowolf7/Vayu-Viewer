/**
 * @file hbintrinsics.h
 * @brief Cross-platform intrinsics inclusion header
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 *
 * Copyright (c) 2024, Henri Beauchamp.
 *
 * Second Life Viewer Source Code
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; version 2.1 of the License only.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA"
 * $/LicenseInfo$
 */

#pragma once

// This header file is automatically included (via linden_common.h) by all
// *.cpp modules, and may be included as need be from header files using
// inlined intrinsics functions.

// This header also defines LL_SS* to 1 when the compiler is set to generate
// the corresponding intrinsic types: these defines shall be tested instead
// of the __SS*__ ones in the corresponding optimized parts of the viewer code,
// since the MSVC compiler and ARM64 achictectures lack the gcc/clang x86_64
// defines while perhaps able to use these optimizations.
// Note that SSE2 support is a prerequisite for the viewer, so you do not need
// to test for anything before using SSE or SSE2 intrinsics in your code.
// The __AVX*__ defines can be used "as is" (for now, and until sse2neon.h
// implements support for AVX intrinsics).

#if SSE2NEON
# define SSE2NEON_ALLOC_DEFINED
# include "sse2neon.h"
// sse2non offers most SSE* equivalent intrinsics, but for now lacks AVX ones.
# define LL_SSE3 1
# define LL_SSSE3 1
# define LL_SSE41 1
# define LL_SSE42 1
#elif !defined(__clang__) && (defined(__MSVC_VER__) || defined(_MSC_VER))
# include <immintrin.h>
// Note: MSVC does not define any of __SS*__
// We define __SSE__ and __SSE2__ here (since all viewer builds are now 64 bits
// ones and therefore always got both), but we do not really care as far as
// actual optimizations are concerned: the only place where they are tested is
// in llfloaterabout.cpp to display what maths intrinsics have been generated
// by the compiler itself (and the MSVC build options do include SSE2 maths
// generation).
# ifndef __SSE__
#  define __SSE__ 1
# endif
# ifndef __SSE2__
#  define __SSE2__ 1
# endif
// When AVX is here, so should be SS(S)E 3 and SSE 4...
# if defined(__AVX__)
#  define __SSE3__ 1
#  define __SSSE3__ 1
#  define __SSE4_1__ 1
#  define __SSE4_2__ 1
#  define __SSE4A__ 1
# endif
#else
# include <immintrin.h>
#endif

#if defined(__SSE3__)
# define LL_SSE3 1
#endif
#if defined(__SSSE3__)
# define LL_SSSE3 1
#endif
#if defined(__SSE4_1__)
# define LL_SSE41 1
#endif
#if defined(__SSE4_2__)
# define LL_SSE42 1
#endif
#if defined(__SSE4A__)
# define LL_SSE4A 1
#endif
