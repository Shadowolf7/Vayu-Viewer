/**
 * @file lluuid.h
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

#ifndef LL_LLUUID_H
#define LL_LLUUID_H

#include <cstring>
#include <functional>
#include <iostream>
#include <set>
#include <vector>
#include "stdtypes.h"
#include "llpreprocessor.h"
#include "hbfastset.h"
#include <boost/functional/hash.hpp>

class LLMutex;

const S32 UUID_BYTES = 16;
const S32 UUID_WORDS = 4;
const S32 UUID_STR_LENGTH = 37; // number of bytes needed to store a UUID as a string
const S32 UUID_STR_SIZE = 36; // .size() of a UUID in a std::string
const S32 UUID_BASE85_LENGTH = 21; // including the trailing NULL.

struct uuid_time_t {
    U32 high;
    U32 low;
        };

class LL_COMMON_API LLUUID
{
public:
    //
    // CREATORS
    //
    constexpr LLUUID() noexcept = default;
    explicit constexpr LLUUID(const U8 (&bytes)[UUID_BYTES]) noexcept
    {
        for (S32 i = 0; i < UUID_BYTES; ++i) mData[i] = bytes[i];
    }
    explicit LLUUID(const char *in_string); // Convert from string.
    explicit LLUUID(const std::string& in_string); // Convert from string.
    ~LLUUID() = default;

    //
    // MANIPULATORS
    //
    void    generate();                 // Generate a new UUID
    void    generate(const std::string& stream); //Generate a new UUID based on hash of input stream

    //static versions of above for use in initializer expressions such as constructor params, etc.
    static LLUUID generateNewID();
    static LLUUID generateNewID(const std::string& stream);

    bool    set(const char *in_string, bool emit = true);   // Convert from string, if emit is false, do not emit warnings
    bool    set(const std::string& in_string, bool emit = true);    // Convert from string, if emit is false, do not emit warnings
    inline void setNull() { memset(mData, 0, UUID_BYTES); }

    S32     cmpTime(uuid_time_t *t1, uuid_time_t *t2);
    static void    getSystemTime(uuid_time_t *timestamp);
    void    getCurrentTime(uuid_time_t *timestamp);

    //
    // ACCESSORS
    //
    inline bool isNull() const
    {
        U64 a, b;
        memcpy(&a, mData,     8);
        memcpy(&b, mData + 8, 8);
        return (a | b) == 0;
    }
    inline bool notNull() const
    {
        U64 a, b;
        memcpy(&a, mData,     8);
        memcpy(&b, mData + 8, 8);
        return (a | b) != 0;
    }
    // JC: This is dangerous.  It allows UUIDs to be cast automatically
    // to integers, among other things.  Use isNull() or notNull().
    //      operator bool() const;

    inline bool operator==(const LLUUID &rhs) const
    {
        return memcmp(mData, rhs.mData, UUID_BYTES) == 0;
    }
    inline bool operator!=(const LLUUID &rhs) const
    {
        return memcmp(mData, rhs.mData, UUID_BYTES) != 0;
    }
    bool    operator<(const LLUUID &rhs) const;
    bool    operator>(const LLUUID &rhs) const;

    // xor functions. Useful since any two random uuids xored together
    // will yield a determinate third random unique id that can be
    // used as a key in a single uuid that represents 2.
    const LLUUID& operator^=(const LLUUID& rhs);
    LLUUID operator^(const LLUUID& rhs) const;

    // similar to functions above, but not invertible
    // yields a third random UUID that can be reproduced from the two inputs
    // but which, given the result and one of the inputs can't be used to
    // deduce the other input
    LLUUID combine(const LLUUID& other) const;
    void combine(const LLUUID& other, LLUUID& result) const;

    friend LL_COMMON_API std::ostream&   operator<<(std::ostream& s, const LLUUID &uuid);
    friend LL_COMMON_API std::istream&   operator>>(std::istream& s, LLUUID &uuid);

    void to_chars(char* out) const;
    void to_wchars(wchar_t* out) const;
    void toString(std::string& out) const;
    void toCompressedString(std::string& out) const;

    std::string asString() const;
    std::string getString() const;

    U16 getCRC16() const;
    U32 getCRC32() const;

    // Returns a 64 bit digest of the UUID by XORing its two 64-bit halves.
    // memcpy into locals avoids the strict-aliasing/alignment UB of reading
    // a U8[16] through a U64*. Numerical output is unchanged on each platform.
    inline U64 getDigest64() const
    {
        U64 a, b;
        memcpy(&a, mData,     sizeof(a));
        memcpy(&b, mData + 8, sizeof(b));
        return a ^ b;
    }

    static bool validate(const std::string& in_string); // Validate that the UUID string is legal.

    static const LLUUID null;
    static LLMutex * mMutex;

    static S32 getNodeID(unsigned char * node_id);

    static bool parseUUID(const std::string& buf, LLUUID* value);

    U8 mData[UUID_BYTES] {};
};
static_assert(std::is_trivially_copyable<LLUUID>::value, "LLUUID must be trivial copy");
static_assert(std::is_trivially_move_assignable<LLUUID>::value, "LLUUID must be trivial move");
static_assert(std::is_standard_layout<LLUUID>::value, "LLUUID must be a standard layout type");

// LLUUID::null is defined out-of-line in lluuid.cpp, NOT as an inline constexpr
// variable here. LLUUID is exported (class LL_COMMON_API), which makes this
// static member dllimport in consumer modules, and a dllimport member may not
// have an in-header (inline) definition. So it is an ordinary exported constant
// defined once in the DLL. It is still constant-initialized (LLUUID has a
// constexpr default ctor), so there is no static-init-order hazard.

typedef std::vector<LLUUID> uuid_vec_t;
typedef std::set<LLUUID> uuid_set_t;

// Helper structure for ordering lluuids in stl containers.  eg:
// std::map<LLUUID, LLWidget*, lluuid_less> widget_map;
//
// (isn't this the default behavior anyway? I think we could
// everywhere replace these with uuid_set_t, but someone should
// verify.)
struct lluuid_less
{
    bool operator()(const LLUUID& lhs, const LLUUID& rhs) const
    {
        return lhs < rhs;
    }
};

typedef safe_hset<LLUUID> uuid_list_t;
/*
 * Sub-classes for keeping transaction IDs and asset IDs
 * straight.
 */
typedef LLUUID LLAssetID;

class LL_COMMON_API LLTransactionID : public LLUUID
{
public:
    constexpr LLTransactionID() noexcept = default;

    static const LLTransactionID tnull;
    LLAssetID makeAssetID(const LLUUID& session) const;
};

// As with LLUUID::null above: defined out-of-line in lluuid.cpp because an
// exported (dllimport-in-consumers) static member can't have an inline
// definition.

// Fast canonical hash for LLUUID (also used by boost::container_hash via ADL)
// using the 64-bit digest XOR.
inline size_t hash_value(const LLUUID& id) noexcept
{
    return (size_t)id.getDigest64();
}

namespace std
{
    template<> struct hash<LLUUID>
    {
        inline size_t operator()(const LLUUID& id) const noexcept
        {
            return hash_value(id);
        }
    };
}

#endif // LL_LLUUID_H
