/**
 * @file lldiriterator.h
 * @brief Definition of directory iterator class
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
 *
 * Copyright (c) 2010, Linden Research, Inc. (c) 2021 Henri Beauchamp.
 *
 * Modifications by Henri Beauchamp:
 *  - Allow a simple iterator without matching pattern.
 *  - Allow iterating on entries that do *not* match the given pattern.
 *  - Allow to return sundry information for each found entry.
 *  - Added LLDirIterator::deleteFilesInDir().
 *  - Added LLDirIterator::deleteRecursivelyInDir().
 *  - Proper catching of throw()s and boost::filesystem errors.
 *  - Got rid of boost::regex in favour of std::regex since we now use C++11.
 *  - Added support for iterating on logical drives (when passed an empty
 *    path), under Windows.
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

#include "stdtypes.h"
#include "llerror.h"

#include <string>
#include <time.h>           // For time_t
#include <filesystem>

// Information requested for each entry, as a bitmap
enum
{
    DI_NONE         = 0,
    DI_ISFILE       = 1 << 0,   // Regular file (non-directory, non-link)
    DI_ISDIR        = 1 << 1,   // Directory (maybe a link to a directory)
    DI_ISLINK       = 1 << 2,   // Symbolic link (to a file or directory)
    DI_ISHIDDEN     = 1 << 3,   // Hidden file or directory
    DI_SIZE         = 1 << 4,   // File size
    DI_TIMESTAMP    = 1 << 5,   // Last modified time stamp
    DI_ALL          = ~0
};

// Iterates through directory entries.
class LL_COMMON_API LLDirIterator
{
protected:
    LOG_CLASS(LLDirIterator);

public:
    // Directory iterator with optional global pattern matching, and file info
    // retrieval.
    // Wildcards supported in 'mask':
    // --------------------------------------------------------------
    // | Wildcard   | Matches                                       |
    // --------------------------------------------------------------
    // |    *       | zero or more characters                       |
    // |    ?       | exactly one character                         |
    // | [abcde]    | exactly one character listed                  |
    // | [a-e]      | exactly one character in the given range      |
    // | [!abcde]   | any character that is not listed              |
    // | [!a-e]     | any character that is not in the given range  |
    // | {abc,xyz}  | exactly one entire word in the options given  |
    // --------------------------------------------------------------
    // When 'mask' is ommitted or empty, the iterator becomes a simple one,
    // without pattern matching (i.e. all the entries in the directory are
    // returned in sequence by next()).
    // 'requested_info' is an optionnal bitmap using the flags in the above
    // enum.
    LLDirIterator(const std::string& dirname, const char* mask = NULL,
                  U32 requested_info = DI_NONE);
    LLDirIterator(const std::string& dirname, const std::string& mask,
                  U32 requested_info = DI_NONE)
        : LLDirIterator(dirname, mask.c_str(), requested_info) {}
    LLDirIterator(const std::filesystem::path& dir_path, const std::string& mask,
                  U32 requested_info = DI_NONE)
        : LLDirIterator(dir_path.string(), mask.c_str(), requested_info) {}

    ~LLDirIterator();

    inline bool isValid() const                  { return mImpl != NULL; }
    inline const std::string& getPath() const    { return mDirPath; }

    // Search for the next matching entry, returning true when a match is
    // found, with the matching entry name returned in 'name'.
    // When 'not_matching' is set to true, the method returns the next entry
    // that does *not* match the glob pattern (which must have been given in
    // this case, 'not_matching' being ignored when no pattern was given).
    bool next(std::string& name, bool not_matching = false);

    // Info for the last matching entry, only usable when the corresponding
    // flag was set in the constructor, via 'requested_info'. Trying to use one
    // of these methods when the corresponding flag was not set results in an
    // llerrs.
    bool isFile() const;
    bool isDirectory() const;
    bool isLink() const;
    bool isHidden() const;
    size_t getSize() const;         // Always returns 0 for non-regular files
    time_t getTimeStamp() const;    // "Last modified" time stamp

    // Utility method to replace the one that was formerly available from LLDir
    // via the old (and slow) getNextFileInDir() iteration mechanism. It also
    // replaces LLDir::deleteAllNonDirFilesInDir(), when you omit the 'mask'
    // parameter (or pass an empty string for it). As a bonus (compared with
    // the old LLDir methods), when 'not_matching' is set to true, the method
    // deletes all the *files* that do *not* match the glob pattern (which must
    // have been given in this case, else it is a no-operation), but still
    // delete *symbolic links* matching the pattern.
    // Returns the number of deleted files.
    static U32 deleteFilesInDir(const std::string& dirname,
                                const char* mask = NULL,
                                bool not_matching = false);

    // Same as above, but deletes all files in all sub-directories recursively.
    // The sub-directories themselves are also removed (when empty, which may
    // not be always the case when 'not_matching' is true).
    static U32 deleteRecursivelyInDir(const std::string& dirname,
                                      const char* mask = NULL,
                                      bool not_matching = false);

private:
    class Impl;
    Impl*       mImpl;

    std::string mDirPath;
};
