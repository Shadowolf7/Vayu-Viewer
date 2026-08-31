/**
 * @file llfilesystem.cpp
 * @brief Implementation of the local file system.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 *
 * Copyright (c) 2020, Linden Research, Inc. (c) 2021 Henri Beauchamp.
 *
 * Modifications by Henri Beauchamp:
 *  - Pointless per-asset-type file naming removed.
 *  - Cached filename for faster operations.
 *  - Use of faster LLFile operations where possible.
 *  - Fixed various bugs in write operations. Removed the pointless READ_WRITE
 *    mode, added the OVERWRITE one, and changed seek() to auto-padding files
 *    with zeros in WRITE mode when seeking past the end of an existing file.
 *  - Real time tracking of bytes added to/removed from cache.
 *  - Proper cache validity verification.
 *  - Immediate date-stamping on creation of LLFileSystem instances, to prevent
 *    potential race conditions with the threaded cache purging mechanism.
 *  - Multiple threads and multiple viewer instances deconfliction.
 *  - Added LLFile::sFlushOnWrite to work around a bug in Wine (*) which
 *    reports a wrong file position after non flushed writes. (*) This is for
 *    people perverted enough to run a Windows build under Wine under Linux
 *    instead of a Linux native build: yes, I'm perverted since I do it to test
 *    Windows builds under Linux... :-P
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

#include "llfilesystem.h"

#include "lldiskcache.h"
#include "llfile.h"

static S32 get_file_size_helper(const std::string& filename)
{
    llstat st;
    if (LLFile::stat(filename, &st) == 0)
    {
        return (S32)st.st_size;
    }
    return 0;
}

LLFileSystem::LLFileSystem(const LLUUID& id, S32 mode, const char* extra_info)
:   mFileID(id),
    mMode(mode),
    mPosition(0),
    mBytesRead(0),
    mTotalBytesWritten(0),
    mFilename(LLDiskCache::getFilePath(id, extra_info)),
    mValid(LLDiskCache::isValid())
{
    if (extra_info && *extra_info)
    {
        mExtraInfo.assign(extra_info);
    }

    mExists = mValid && LLFile::isfile(mFilename);
    if (mExists)
    {
        // Update the last access time for the file since this is the way the
        // cache works; it relies on a valid "last accessed time" for each file
        // so that it knows how to remove the oldest, unused files.
        // Since LLFileSystem instances are short-lived, we update the file
        // access time on construction (which also allows to update that time
        // at once during cache purging, preventing an old file that would be
        // reused to get purged in between LLFileSystem instance construction
        // and its actual usage). HB
        LLDiskCache::updateFileAccessTime(mFilename);
    }

    // In append mode, we always write to the end of file, so make sure to
    // initialize the current position there... HB
    if (mExists && mMode == APPEND)
    {
        mPosition = get_file_size_helper(mFilename);
    }
}

LLFileSystem::~LLFileSystem()
{
    if (mTotalBytesWritten)
    {
        // Inform the disk cache about how much bytes we added or removed. HB
        LLDiskCache::addBytesWritten(mTotalBytesWritten);
    }
}

bool LLFileSystem::read(U8* buffer, S32 bytes)
{
    if (!mValid || bytes < 0 || !buffer)
    {
        return false;
    }
    if (!bytes)
    {
        mExists = LLFile::isfile(mFilename);
        return mExists;
    }

    LLFILE* file = LLFile::fopen(mFilename, "rb");
    mExists = file != NULL;
    if (!mExists)
    {
        return false;
    }

    if (mPosition > 0)
    {
        fseek(file, mPosition, SEEK_SET);
    }
    mBytesRead = (S32)fread(buffer, 1, bytes, file);
    LLFile::close(file);

    if (mBytesRead > 0)
    {
        mPosition += mBytesRead;
        // Short reads are also considered a success (needed due to how
        // buffered reads are implemented in the viewer code such as in,
        // for example, LLAssetStorage::legacyGetDataCallback())... HB
        return true;
    }

    return false;
}

bool LLFileSystem::write(const U8* buffer, S32 bytes)
{
    if (!mValid)
    {
        return false;
    }
    if (mMode == APPEND)
    {
        // Write to file, appending to it if it already exists.
        LLFILE* file = LLFile::fopen(mFilename, "a+b");
        if (file)
        {
            fwrite((void*)buffer, 1, bytes, file);
            mPosition = (S32)ftell(file);
            LLFile::close(file);
            mTotalBytesWritten += bytes;
            mExists = true;
            return true;
        }
    }
    else if (mMode == OVERWRITE)
    {
        // Discard any existing contents and write.
        mTotalBytesWritten -= get_file_size_helper(mFilename);
        LLFILE* file = LLFile::fopen(mFilename, "wb");
        if (file)
        {
            fwrite((void*)buffer, 1, bytes, file);
            mPosition = (S32)ftell(file);
            LLFile::close(file);
            mTotalBytesWritten += bytes;
            mExists = true;
            return true;
        }
    }
    else if (mMode == WRITE)
    {
        // Write at current position, without truncating
        S32 size = get_file_size_helper(mFilename);  // Remember current size
        mExists = size > 0;
        const char* mode = mExists ? "r+b" : "wb";
        LLFILE* file = LLFile::fopen(mFilename, mode);
        if (file)
        {
            if (mExists && mPosition > 0)
            {
                fseek(file, mPosition, SEEK_SET);
            }
            fwrite((void*)buffer, 1, bytes, file);
            mPosition = (S32)ftell(file);
            LLFile::close(file);
            if (mPosition > size)
            {
                mTotalBytesWritten += mPosition - size;
            }
            mExists = true;
            return true;
        }
    }
    else
    {
        LL_ERRS("FileSystem") << "Cannot write in READ mode." << LL_ENDL;
    }

    mExists = false;
    return false;
}

bool LLFileSystem::seek(S32 offset, S32 origin)
{
    if (!mValid)
    {
        return false;
    }
    if (mMode == OVERWRITE || mMode == APPEND)
    {
        LL_ERRS("FileSystem") << "Cannot seek in file before writing into it in mode "
                              << (mMode == APPEND ? "APPEND" : "OVERWRITE") << LL_ENDL;
    }
    if (origin < 0)
    {
        origin = mPosition;
    }
    S32 new_pos = origin + offset;
    S32 size = get_file_size_helper(mFilename);
    if (new_pos > size)
    {
        if (mMode == READ)
        {
            LL_WARNS("FileSystem") << "Attempt to seek past end of file: " << mFilename
                                   << LL_ENDL;
            mPosition = size;
            return false;
        }
        else    // Append zeros to the file up to the new position. HB
        {
            mPosition = size;
            LLFILE* file = LLFile::fopen(mFilename, "a+b");
            if (!file)
            {
                LL_WARNS("FileSystem") << "Attempt to seek past end of file \"" << mFilename
                                       << "\", and could not open it to pad it with zeros."
                                       << LL_ENDL;
                return false;
            }

            mExists = true;
            size_t bytes = new_pos - size;
            char* buffer = new (std::nothrow) char[bytes];
            if (buffer)
            {
                LL_DEBUGS("FileSystem") << "Appending " << bytes
                                        << " padding bytes to: " << mFilename
                                        << LL_ENDL;
                memset((void*)buffer, 0, bytes);
                fwrite((void*)buffer, 1, bytes, file);
                mPosition = (S32)ftell(file);
                mTotalBytesWritten += mPosition - size;
                delete[] buffer;
            }
            LLFile::close(file);
            if (mPosition == new_pos)
            {
                return true;
            }
            LL_WARNS("FileSystem") << "Could not append enough padding bytes to seek to position: "
                                   << size << " in \"" << mFilename << "\" (position "
                                   << mPosition << " reached)." << LL_ENDL;
            return false;
        }
    }
    if (new_pos < 0)
    {
        LL_WARNS("FileSystem") << "Attempt to seek past beginning of file: " << mFilename
                               << LL_ENDL;
        mPosition = 0;
        return false;
    }
    mPosition = new_pos;
    return true;
}

S32 LLFileSystem::getSize() const
{
    return mValid ? get_file_size_helper(mFilename) : 0;
}

bool LLFileSystem::remove()
{
    if (!mValid)
    {
        return false;
    }
    mExists = false;
    llstat st;
    if (LLFile::stat(mFilename, &st))
    {
        // No such file, we are done.
        return true;
    }
    mTotalBytesWritten -= st.st_size;
    return (LLFile::remove(mFilename) == 0);
}

bool LLFileSystem::rename(const LLUUID& new_id)
{
    mFileID = new_id;
    if (!mValid)
    {
        return false;
    }
    std::string newfname = LLDiskCache::getFilePath(new_id,
                                                    mExtraInfo.c_str());
    // First remove the new file when it exists
    llstat st;
    if (LLFile::stat(newfname, &st) == 0)
    {
        mTotalBytesWritten -= st.st_size;
        LLFile::remove(newfname);
    }
    // Note: this call may fail and will appropriately warn in the log...
    mExists = (LLFile::rename(mFilename, newfname) == 0);
    mFilename = newfname;
    return mExists;
}

//static
bool LLFileSystem::getExists(const LLUUID& id, const char* extra_info)
{
    if (!LLDiskCache::isValid())
    {
        return false;
    }
    return LLFile::isfile(LLDiskCache::getFilePath(id, extra_info));
}

//static
S32 LLFileSystem::getFileSize(const LLUUID& id, const char* extra_info)
{
    if (!LLDiskCache::isValid())
    {
        return 0;
    }
    return get_file_size_helper(LLDiskCache::getFilePath(id, extra_info));
}

//static
bool LLFileSystem::removeFile(const LLUUID& id, const char* extra_info)
{
    if (!LLDiskCache::isValid())
    {
        return false;
    }
    std::string filename = LLDiskCache::getFilePath(id, extra_info);
    llstat st;
    if (LLFile::stat(filename, &st))
    {
        // No such file, we are done.
        return true;
    }
    if (st.st_size)
    {
        LLDiskCache::addBytesWritten(-st.st_size);
    }
    return (LLFile::remove(filename) == 0);
}

//static
bool LLFileSystem::renameFile(const LLUUID& old_id, const LLUUID& new_id,
                              const char* extra_info)
{
    if (!LLDiskCache::isValid())
    {
        return false;
    }

    std::string old_filename = LLDiskCache::getFilePath(old_id, extra_info);
    std::string new_filename = LLDiskCache::getFilePath(new_id, extra_info);
    // First remove the new file when it exists
    llstat st;
    if (LLFile::stat(old_filename, &st) == 0)
    {
        if (st.st_size)
        {
            LLDiskCache::addBytesWritten(-st.st_size);
        }
        LLFile::remove(new_filename);
    }

    // Note: this call may fail and will appropriately warn in the log...
    return (LLFile::rename(old_filename, new_filename) == 0);
}
