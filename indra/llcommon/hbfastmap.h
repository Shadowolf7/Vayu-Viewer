/**
 * @file hbfastmap.h
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

// safe_hmap, fast_hmap and flat_hmap are the macros to use (for example, in
// place of boost::unordered_map or boost::container::flat_map) for unordered
// or flat maps you wish to (potentially) speed up.
// safe_hmap is guaranteed not to invalidate all the map iterators on erase()
// of one of its elements and to preserve pointers to keys and values: this is
// the closest thing to boost::unordered_map for phmap's implementation.
// fast_hmap is guaranteed not to invalidate all the map iterators on erase()
// of one of its elements and to preserve pointers to values (but not to keys).

// The hmap_erase #define is provided for a minor optimization with phmap
// containers, which may call a special _erase() method, that does not return
// an iterator (unlike erase()) and is therefore slightly faster. It is only
// valid when passed an iterator (const or not).

#if LL_NO_PHMAP
# include "boost/unordered_map.hpp"
# include "boost/container/flat_map.hpp"
# define safe_hmap boost::unordered_map
# define fast_hmap boost::unordered_map
# define flat_hmap boost::container::flat_map
# define hmap_erase(it) erase(it)
#else
# include "parallel_hashmap/phmap.h"
# define safe_hmap phmap::node_hash_map
# define fast_hmap phmap::flat_hash_map
# define flat_hmap phmap::flat_hash_map
# define hmap_erase(it) _erase(it)
#endif
