/**
 * @file hbfastset.h
 *
 * $LicenseInfo:firstyear=2020&license=viewerlgpl$
 *
 * Copyright (c) 2020, Henri Beauchamp.
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

// safe_hset, fast_hset and flat_hset are the macros to use (for example, in
// place of boost::unordered_set or boost::container::flat_set) for unordered
// sets you wish to (potentially) speed up.
// safe_hset is guaranteed not to invalidate all the map iterators on erase()
// of one of its elements and to preserve pointers.

// The hset_erase #define is provided for a minor optimization with phmap
// containers, that may call a special _erase() method, that does not return
// an iterator (unlike erase()) and is therefore slightly faster. It is only
// valid when passed an iterator (const or not).

#if LL_NO_PHMAP
# include "boost/unordered_set.hpp"
# include "boost/container/flat_set.hpp"
# define safe_hset boost::unordered_set
# define fast_hset boost::unordered_set
# define flat_hset boost::container::flat_set
# define hset_erase(it) erase(it)
#else
# include "parallel_hashmap/phmap.h"
# define safe_hset phmap::node_hash_set
# define fast_hset phmap::flat_hash_set
# define flat_hset phmap::flat_hash_set
# define hset_erase(it) _erase(it)
#endif
