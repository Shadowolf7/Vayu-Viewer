/**
 * @file lldiriterator.cpp
 * @brief Implementation of directory iterator class
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

#include "linden_common.h"

#include <cstddef>
#include <regex>

#include "boost/filesystem.hpp"

#include "lldiriterator.h"

#include "llfile.h"
#include "llstring.h"

using namespace boost::filesystem;

///////////////////////////////////////////////////////////////////////////////
// LLDirIterator::Impl class
///////////////////////////////////////////////////////////////////////////////

class LLDirIterator::Impl
{
public:
    inline Impl(const directory_iterator& dir_iter, U32 requested_info)
    :   mIter(dir_iter),
        mRequestedInfo(requested_info),
        mGotFilter(false),
        mIsFile(false),
        mIsDirectory(false),
        mIsLink(false),
        mIsHidden(false),
#if LL_WINDOWS
        mIsDriveIterator(false),
        mNextDrive(0),
#endif
        mSize(0),
        mTimeStamp(0)
    {
    }

    inline void setFilter(const std::regex& regexp)
    {
        mGotFilter = true;
        mFilterExp.assign(regexp);
    }

#if LL_WINDOWS
    inline void setDriveIterator()
    {
        mIsDriveIterator = true;
    }
#endif

    bool next(std::string& name, bool not_matching);

    inline bool isFile() const
    {
        checkRequestedInfo(DI_ISFILE);
        return mIsFile;
    }

    inline bool isDirectory() const
    {
        checkRequestedInfo(DI_ISDIR);
        return mIsDirectory;
    }

    inline bool isLink() const
    {
        checkRequestedInfo(DI_ISLINK);
        return mIsLink;
    }

    inline bool isHidden() const
    {
        checkRequestedInfo(DI_ISHIDDEN);
        return mIsHidden;
    }

    inline size_t getSize() const
    {
        checkRequestedInfo(DI_SIZE);
        return mSize;
    }

    inline time_t getTimeStamp() const
    {
        checkRequestedInfo(DI_TIMESTAMP);
        return mTimeStamp;
    }

    static std::string globPatternToRegex(const char* glob);

private:
    inline bool hasRequestedInfo(U32 info) const
    {
        return (mRequestedInfo & info) != 0;
    }

    void checkRequestedInfo(U32 info) const;
    void populateEntryInfo();

private:
    directory_iterator  mIter;
    std::regex          mFilterExp;
    size_t              mSize;
    time_t              mTimeStamp;
    U32                 mRequestedInfo;
#if LL_WINDOWS
    U8                  mNextDrive;
    bool                mIsDriveIterator;
#endif
    bool                mGotFilter;
    bool                mIsFile;
    bool                mIsDirectory;
    bool                mIsLink;
    bool                mIsHidden;
};

//static
std::string LLDirIterator::Impl::globPatternToRegex(const char* glob)
{
    size_t glob_len = strlen(glob);
    std::string expr;
    expr.reserve(glob_len * 2);
    S32 braces = 0;
    bool escaped = false;
    bool square_brace_open = false;

    for (size_t i = 0; i < glob_len; ++i)
    {
        const char& c = glob[i];
        switch (c)
        {
            case '*':
                if (i == 0)
                {
                    expr = "[^.].*";
                }
                else
                {
                    expr += escaped ? "*" : ".*";
                }
                break;

            case '?':
                expr += escaped ? '?' : '.';
                break;

            case '{':
                ++braces;
                expr += '(';
                break;

            case '}':
                if (--braces < 0)
                {
                    LL_ERRS("DirIterator") << "Closing brace without an equivalent opening brace in: "
                                           << glob << LL_ENDL;
                }
                expr += ')';
                break;

            case ',':
                expr += braces ? '|' : c;
                break;

            case '!':
                expr += square_brace_open ? '^' : c;
                break;

            case '.':
            case '^':
            case '(':
            case ')':
            case '+':
            case '|':
            case '$':
                expr += '\\';
                [[fallthrough]];

            default:
                expr += c;
        }

        escaped = c == '\\';
        square_brace_open = c == '[';
    }

    if (braces)
    {
        LL_ERRS("DirIterator") << "Unterminated brace expression in: " << glob << LL_ENDL;
    }

    return expr;
}

void LLDirIterator::Impl::checkRequestedInfo(U32 info) const
{
    if (!(mRequestedInfo & info))
    {
        LL_ERRS("DirIterator") << "Bad info request: " << info << LL_ENDL;
    }
}

void LLDirIterator::Impl::populateEntryInfo()
{
    if (hasRequestedInfo(DI_ISHIDDEN))
    {
#if LL_WINDOWS
        DWORD attr = GetFileAttributesA(mIter->path().string().c_str());
        mIsHidden = (attr & FILE_ATTRIBUTE_HIDDEN) != 0;
#else
        std::string name = mIter->path().filename().string();
        mIsHidden = !name.empty() && name[0] == '.';
#endif
        if (mRequestedInfo == DI_ISHIDDEN)
        {
            return;
        }
    }
    try
    {
        bool want_size = hasRequestedInfo(DI_SIZE);
        if (want_size || hasRequestedInfo(DI_ISFILE))
        {
            mIsFile = is_regular_file(mIter->path());
        }
        if (hasRequestedInfo(DI_ISDIR))
        {
            mIsDirectory = is_directory(mIter->path());
        }
        if (hasRequestedInfo(DI_ISLINK))
        {
            mIsLink = is_symlink(mIter->path());
        }
        if (want_size)
        {
            mSize = mIsFile ? (size_t)file_size(mIter->path()) : 0;
        }
        if (hasRequestedInfo(DI_TIMESTAMP))
        {
            mTimeStamp = last_write_time(mIter->path());
        }
    }
    catch (const filesystem_error& e)
    {
        LL_WARNS("DirIterator") << e.what() << LL_ENDL;
    }
}

bool LLDirIterator::Impl::next(std::string& name, bool not_matching)
{
#if LL_WINDOWS
    if (mIsDriveIterator)
    {
        name.clear();
        mIsDirectory = false;

        if (mNextDrive >= 26)
        {
            return false;
        }

        DWORD drives_map = GetLogicalDrives();
        for (U8 i = mNextDrive; i < 26; ++i)
        {
            if (drives_map & (1L << i))
            {
                char volume = 'A' + (char)i;
                name = volume;
                name += ':';
                mIsDirectory = true;
                mNextDrive = i + 1;
                break;
            }
        }

        return mIsDirectory;
    }
#endif

    bool found = false;
    directory_iterator end;

    if (mGotFilter)
    {
        try
        {
            boost::system::error_code ec;
            std::smatch match;
            while (mIter != end)
            {
                name = mIter->path().filename().string();
                if (name == "." || name == "..")
                {
                    continue;
                }
                found = std::regex_match(name, match, mFilterExp);
                if (not_matching)
                {
                    found = !found || is_symlink(mIter->path(), ec);
                }
                if (found)
                {
                    break;
                }
                ++mIter;
            }
        }
        catch (const std::regex_error& e)
        {
            LL_WARNS("DirIterator") << e.what() << LL_ENDL;
        }
    }
    else
    {
        while (mIter != end)
        {
            name = mIter->path().filename().string();
            if (name != "." && name != "..")
            {
                found = true;
                break;
            }
            ++mIter;
        }
    }

    if (found)
    {
        if (mRequestedInfo)
        {
            populateEntryInfo();
        }
    }
    else
    {
        name.clear();
        if (mRequestedInfo)
        {
            mIsFile = mIsDirectory = mIsLink = mIsHidden = false;
            mTimeStamp = 0;
        }
    }

    if (mIter != end)
    {
        ++mIter;
    }

    return found;
}

///////////////////////////////////////////////////////////////////////////////
// LLDirIterator class proper
///////////////////////////////////////////////////////////////////////////////

static void append_separator_if_needed(std::string& dirname)
{
#if LL_WINDOWS
    if (dirname.back() != '\\')
    {
        dirname += '\\';
    }
#else
    if (dirname.back() != '/')
    {
        dirname += '/';
    }
#endif
}

LLDirIterator::LLDirIterator(const std::string& dirname, const char* mask,
                             U32 requested_info)
:   mImpl(NULL)
{
    if (dirname.empty())
    {
#if LL_WINDOWS
        directory_iterator iter;
        mImpl = new Impl(iter, requested_info);
        mImpl->setDriveIterator();
        return;
#else
        LL_WARNS("DirIterator") << "Invalid (empty) path." << LL_ENDL;
        return;
#endif
    }

#if LL_WINDOWS
    path dir_path(ll_convert_string_to_wide(dirname));
#else
    path dir_path(dirname);
#endif

    boost::system::error_code ec;
    if (!is_directory(dir_path, ec) || ec.failed())
    {
        if (ec.failed())
        {
            LL_WARNS("DirIterator") << "Invalid path: " << dirname << " - Error: "
                                    << ec.message() << LL_ENDL;
        }
        else
        {
            LL_WARNS("DirIterator") << "Invalid path: " << dirname << LL_ENDL;
        }
        return;
    }

    mDirPath = dir_path.string();
    append_separator_if_needed(mDirPath);

    directory_iterator iter(dir_path, ec);
    if (ec.failed())
    {
        LL_WARNS("DirIterator") << "Directory: " << mDirPath
                                << " - Error while creating iterator: " << ec.message()
                                << LL_ENDL;
        return;
    }

    std::regex regexp;
    bool has_glob = mask && *mask;
    if (has_glob)
    {
        std::string expr = Impl::globPatternToRegex(mask);
        try
        {
            regexp.assign(expr);
        }
        catch (const std::regex_error& e)
        {
            LL_WARNS("DirIterator") << "\"" << expr << "\" is not a valid regular expression: "
                                    << e.what() << " - Search match global pattern was: "
                                    << mask << LL_ENDL;
            return;
        }
    }

    mImpl = new Impl(iter, requested_info);
    if (has_glob)
    {
        mImpl->setFilter(regexp);
    }
}

LLDirIterator::~LLDirIterator()
{
    delete mImpl;
}

bool LLDirIterator::next(std::string& name, bool not_matching)
{
    return mImpl && mImpl->next(name, not_matching);
}

bool LLDirIterator::isFile() const
{
    return mImpl && mImpl->isFile();
}

bool LLDirIterator::isDirectory() const
{
    return mImpl && mImpl->isDirectory();
}

bool LLDirIterator::isLink() const
{
    return mImpl && mImpl->isLink();
}

bool LLDirIterator::isHidden() const
{
    return mImpl && mImpl->isHidden();
}

size_t LLDirIterator::getSize() const
{
    return mImpl ? mImpl->getSize() : 0;
}

time_t LLDirIterator::getTimeStamp() const
{
    return mImpl ? mImpl->getTimeStamp() : 0;
}

//static
U32 LLDirIterator::deleteFilesInDir(const std::string& dirname,
                                    const char* mask, bool not_matching)
{
    if (not_matching && !(mask && *mask))
    {
        return 0;
    }

    LLDirIterator iter(dirname, mask, DI_ISDIR);
    if (!iter.isValid())
    {
        return 0;
    }

    U32 count = 0;

    boost::system::error_code ec;
    std::string filename;
    while (iter.next(filename, not_matching))
    {
        if (!iter.isDirectory())
        {
            ++count;
            remove(iter.getPath() + filename, ec);
            if (ec.failed())
            {
                --count;
                LL_WARNS("DirIterator") << "Failure to remove \"" << filename << "\". Reason: "
                                        << ec.message() << LL_ENDL;
            }
        }
    }

    return count;
}

//static
U32 LLDirIterator::deleteRecursivelyInDir(const std::string& dirname,
                                          const char* mask, bool not_matching)
{
    if (not_matching && !(mask && *mask))
    {
        return 0;
    }

    LLDirIterator iter(dirname, mask, DI_ISDIR);
    if (!iter.isValid())
    {
        return 0;
    }

    U32 count = 0;

    boost::system::error_code ec;
    std::string name, subdir;
    while (iter.next(name, not_matching))
    {
        if (iter.isDirectory())
        {
            subdir = dirname;
            append_separator_if_needed(subdir);
            subdir += name;
            count += deleteRecursivelyInDir(subdir, mask, not_matching);
            remove(subdir, ec);
        }
        else
        {
            ++count;
            remove(iter.getPath() + name, ec);
            if (ec.failed())
            {
                --count;
                LL_WARNS("DirIterator") << "Failure to remove \"" << name << "\". Reason: "
                                        << ec.message() << LL_ENDL;
            }
        }
    }

    return count;
}
