/**
 * @file lltexturecache.cpp
 * @brief Object which handles local texture caching
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
 *
 * Copyright (c) 2000-2009, Linden Research, Inc.
 * Copyright (c) 2011-2023, Henri Beauchamp.
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

#include "llviewerprecompiledheaders.h"

#include "lltexturecache.h"

#include "lldir.h"
#include "lldiriterator.h"
#include "llimage.h"

#include "llappviewer.h"
#include "llviewercontrol.h"

// The Cool VL Viewer defines these in llimage.h; they are not part of this
// tree, so we define them locally instead.
static U8* allocate_texture_mem(U32 size)
{
	// Cool VL declares this in llimage.h; all call sites check for NULL and
	// bail with an "Out of memory!" path, so returning NULL is sufficient.
	return (U8*)malloc(size);
}

static void free_texture_mem(U8* mem)
{
	free(mem);
}

// Global variable, kept file-local. Set in the constructor.
static LLTextureCache* gTextureCachep = NULL;

// Cache organization:
// cache/texture.entries
//  Unordered array of Entry structs
// cache/texture.cache
//  First TEXTURE_CACHE_ENTRY_SIZE bytes of each texture in texture.entries in
//  the same order
// cache/textures/[0-F]/UUID.texture
//  Actual texture body files

// Version of our texture cache: increment each time its structure changes.
// Note: we use an unusually large number, which should ensure that no cache
// written by another viewer than the Cool VL Viewer would be considered valid
// (even though the cache directory is normally already different).
constexpr F32 TEXTURE_CACHE_VERSION = 10001.f;
constexpr U32 CACHE_ADDRESS_SIZE_BITS = sizeof(void*) * 8;	// in bits

constexpr char LL_DIR_DELIM_CHR = std::filesystem::path::preferred_separator;
constexpr const char* LL_DIR_DELIM_STR = (LL_DIR_DELIM_CHR == '\\') ? "\\" : "/";

// % amount of cache left after a purge.
constexpr S64 TEXTURE_PURGED_CACHE_SIZE = 80;
// % amount for LRU list (low overhead to regenerate)
constexpr F32 TEXTURE_CACHE_LRU_SIZE = .10f;
// w, h, c, level
constexpr S32 TEXTURE_FAST_CACHE_ENTRY_OVERHEAD = sizeof(S32) * 4;
constexpr S32 TEXTURE_FAST_CACHE_DATA_SIZE = 16 * 16 * 4;
constexpr S32 TEXTURE_FAST_CACHE_ENTRY_SIZE =
	TEXTURE_FAST_CACHE_DATA_SIZE + TEXTURE_FAST_CACHE_ENTRY_OVERHEAD;

//////////////////////////////////////////////////////////////////////////////
// Pool thread worker classes. This is where reads and writes do happpen.
//////////////////////////////////////////////////////////////////////////////

class LLTextureCacheWorker
{
	friend class LLTextureCache;

protected:
	LOG_CLASS(LLTextureCacheWorker);

public:
	inline LLTextureCacheWorker(const LLUUID& id, U8* data, S32 datasize,
								   S32 offset, S32 imagesize,
								   LLTextureCache::Responder* responder)
	:	mID(id),
		mReadData(NULL),
		mWriteData(data),
		mDataSize(datasize),
		mOffset(offset),
		mImageSize(imagesize),			 // For writes
		mImageFormat(IMG_CODEC_J2C),
		mImageLocal(false),
		mCorrupted(false),
		mResponder(responder)
	{
	}

	inline virtual ~LLTextureCacheWorker()
	{
		free_texture_mem(mReadData);
	}

	// Override these methods (called from the pool thread):
	virtual void doRead() = 0;
	virtual void doWrite() = 0;

private:
	// Called from the pool thread
	void finishRead();
	void finishWrite();

protected:
	LLPointer<LLTextureCache::Responder>	mResponder;
	LLUUID									mID;
	U8*										mReadData;
	U8*										mWriteData;
	S32										mDataSize;
	S32										mOffset;
	S32										mImageSize;
	EImageCodec								mImageFormat;
	bool									mImageLocal;
	bool									mCorrupted;
};

class LLTextureCacheLocalFileWorker final : public LLTextureCacheWorker
{
protected:
	LOG_CLASS(LLTextureCacheLocalFileWorker);

public:
	LLTextureCacheLocalFileWorker(const std::string& filename,
								  const LLUUID& id, U8* data, S32 datasize,
								  S32 offset, S32 imagesize, // For writes
								  LLTextureCache::Responder* responder)
	:	LLTextureCacheWorker(id, data, datasize, offset, imagesize, responder),
		mFileName(filename)
	{
	}

	void doRead() override;

	void doWrite() override
	{
		LL_ERRS() << "Cannot write to a local texture file !" << LL_ENDL;
	}

private:
	std::string	mFileName;
};

void LLTextureCacheLocalFileWorker::doRead()
{
	if (mResponder.notNull())		// Paranoia
	{
		mResponder->started();
	}

	S32 local_size = (S32)LLFile::size(mFileName);

	if (local_size > 0 && mFileName.size() > 4)
	{
		mDataSize = local_size; // Only a complete file is valid

		std::string extension = mFileName.substr(mFileName.size() - 3, 3);
		mImageFormat = LLImageBase::getCodecFromExtension(extension);
		if (mImageFormat == IMG_CODEC_INVALID)
		{
			LL_DEBUGS("TextureCache") << "Unrecognized file extension "
									  << extension << " for local texture "
									  << mFileName << LL_ENDL;
			mDataSize = 0; // No data
			return;
		}
	}
	else
	{
		// File does not exist: no data
		mDataSize = 0;
		return;
	}
	if (!mDataSize || mDataSize > local_size - mOffset)
	{
		mDataSize = local_size - mOffset;
	}

	mReadData = (U8*)allocate_texture_mem(mDataSize);
	if (!mReadData)
	{
		// Out of memory !
		mDataSize = 0;
		return;
	}

	S32 bytes = (S32)LLFile::read(mFileName, mReadData, mOffset, mDataSize);
	if (bytes != mDataSize)
	{
 		LL_DEBUGS("TextureCache") << "Error reading from local file: "
								  << mFileName << " - Bytes: " << mDataSize
								  << " Offset: " << mOffset << LL_ENDL;
		mDataSize = 0;
		free_texture_mem(mReadData);
		mReadData = NULL;
	}
	else
	{
		mImageSize = local_size;
		mImageLocal = true;
	}
}

class LLTextureCacheRemoteWorker final : public LLTextureCacheWorker
{
public:
	LLTextureCacheRemoteWorker(const LLUUID& id, U8* data, S32 datasize,
							   S32 offset, S32 imagesize,
							   LLPointer<LLImageRaw> raw, S32 discardlevel,
							   LLTextureCache::Responder* responder)
	:	LLTextureCacheWorker(id, data, datasize, offset, imagesize, responder),
		mRawImage(raw),
		mRawDiscardLevel(discardlevel)
	{
	}

	void doRead() override;
	void doWrite() override;

private:
	LLPointer<LLImageRaw>	mRawImage;
	S32						mRawDiscardLevel;
};

// This is where a texture is read from the cache system (header and body)
// Current assumption are:
// - the whole data are in a raw form, will be stored at mReadData
// - the size of this raw data is mDataSize and can be smaller than
//   TEXTURE_CACHE_ENTRY_SIZE (the size of a record in the header cache)
// - the code supports offset reading but this is actually never exercised in
//   the viewer
void LLTextureCacheRemoteWorker::doRead()
{
	if (mResponder.notNull())		// Paranoia
	{
		mResponder->started();
	}

	if (!gTextureCachep)
	{
		mDataSize = -1;	// Failed
		return;
	}

	LLTextureCache::Entry entry;
	S32 idx = gTextureCachep->getHeaderCacheEntry(mID, entry);
	if (idx < 0)
	{
		// The texture is *not* cached. We are done here...
		mDataSize = 0; // no data
		return;
	}

	mImageSize = entry.mImageSize;

	// If the read offset is bigger than the header cache, we read directly
	// from the body. Note that currently, we *never* read with offset from the
	// cache.
	if (mOffset < TEXTURE_CACHE_ENTRY_SIZE)
	{
		// Read data from the header cache (texture.entries) file.

		// We need an entry here or reading the header makes no sense:
		llassert_always(idx >= 0 && mOffset < TEXTURE_CACHE_ENTRY_SIZE);

		S32 offset = idx * TEXTURE_CACHE_ENTRY_SIZE + mOffset;
		// Compute the size we need to read (in bytes)
		S32 size = TEXTURE_CACHE_ENTRY_SIZE - mOffset;
		size = llmin(size, mDataSize);

		// Allocate the read buffer
		mReadData = (U8*)allocate_texture_mem(size);
		if (!mReadData)
		{
			// Out of memory !
			mDataSize = -1;	// Failed
			return;
		}

		S32 bytes_read =
			(S32)LLFile::read(gTextureCachep->mHeaderDataFileName, mReadData,
								offset, size);
		if (bytes_read != size)
		{
			LL_WARNS() << "LLTextureCacheWorker: " << mID
					<< " incorrect number of bytes read from header: "
					<< bytes_read << " / " << size << LL_ENDL;
			free_texture_mem(mReadData);
			mReadData = NULL;
			mDataSize = -1;	// Failed
			mCorrupted = true;
			return;
		}
		// If we already read all we expected, we are actually done
		if (mDataSize <= bytes_read)
		{
			return;
		}
	}

	// Maybe read the rest of the data from the UUID based cached file
	std::string filename = gTextureCachep->getTextureFileName(mID);
	S32 filesize = (S32)LLFile::size(filename);
	if (filesize && filesize + TEXTURE_CACHE_ENTRY_SIZE > mOffset)
	{
		S32 max_datasize = TEXTURE_CACHE_ENTRY_SIZE + filesize - mOffset;
		mDataSize = llmin(max_datasize, mDataSize);

		// Reserve the whole data buffer first
		U8* data = (U8*)allocate_texture_mem(mDataSize);
		if (!data)
		{
			// Out of memory !
			mDataSize = -1;	// Failed
			return;
		}

		S32 data_offset, file_size, file_offset;
		// Set the data file pointers taking the read offset into account.
		// 2 cases:
		if (mOffset < TEXTURE_CACHE_ENTRY_SIZE)
		{
			// Offset within the header record. That means we read something
			// from the header cache. Note: most common case is (mOffset = 0),
			// so this is the "normal" code path.

			// I.e. TEXTURE_CACHE_ENTRY_SIZE if mOffset nul (common case):
			data_offset = TEXTURE_CACHE_ENTRY_SIZE - mOffset;
			file_offset = 0;
			file_size = mDataSize - data_offset;
			// Copy the raw data we've been holding from the header cache into
			// the new sized buffer
			llassert_always(mReadData);
			memcpy(data, mReadData, data_offset);
			free_texture_mem(mReadData);
			mReadData = NULL;
		}
		else
		{
			// Offset bigger than the header record. That means we have not
			// read anything yet.
			data_offset = 0;
			file_offset = mOffset - TEXTURE_CACHE_ENTRY_SIZE;
			file_size = mDataSize;
			// No data from header cache to copy in that case, we skipped it
			// all
		}

		// Now use that buffer as the object read buffer
		llassert_always(mReadData == NULL);
		mReadData = data;

		// Read the data at last
		S32 bytes_read = (S32)LLFile::read(filename, mReadData + data_offset,
											 file_offset, file_size);
		if (bytes_read != file_size)
		{
			LL_DEBUGS("TextureCache") << "Texture: "  << mID
									  << ". Incorrect number of bytes read from body: "
									  << bytes_read	<< " / " << file_size
									  << LL_ENDL;
			free_texture_mem(mReadData);
			mReadData = NULL;
			mDataSize = -1;	// Failed
			mCorrupted = true;
			return;
		}
	}
	else
	{
		// No body, we are done.
		mDataSize = llmax(TEXTURE_CACHE_ENTRY_SIZE - mOffset, 0);
		LL_DEBUGS("TextureCache") << "No body file for texture: " << mID
								  << LL_ENDL;
	}
	// Nothing else to do at that point...
}

// This is where *everything* about a texture is written down into the cache
// system (entry map, header and body).
// Current assumption are:
// - the whole data are in a raw form, starting at mWriteData
// - the size of this raw data is mDataSize and can be smaller than
//   TEXTURE_CACHE_ENTRY_SIZE (the size of a record in the header cache)
// - the code *does not* support offset writing so there are no difference
//   between buffer addresses and start of data
void LLTextureCacheRemoteWorker::doWrite()
{
	if (mResponder.notNull())		// Paranoia
	{
		mResponder->started();
	}

	if (!gTextureCachep)
	{
		mDataSize = -1;	// Failed
		mRawImage = NULL;
		return;
	}

	// First stage: check that what we are trying to cache is in an OK shape
	if (mOffset != 0 ||  mDataSize <= 0 || mImageSize < mDataSize ||
		mRawDiscardLevel < 0 || !mRawImage || mRawImage->isBufferInvalid())
	{
		LL_WARNS() << "INIT state check failed for texture: " << mID
				<< " . Aborted." << LL_ENDL;
		mDataSize = -1;	// Failed
		mRawImage = NULL;
		return;
	}

	// Second stage: set an entry in the headers entry (texture.entries) file
	LLTextureCache::Entry entry;
	// Checks if this image is already in the entry list
	S32 idx = gTextureCachep->getHeaderCacheEntry(mID, entry);
	if (idx >= 0)
	{
		// Update the existing entry.
		if (gTextureCachep->updateEntry(idx, entry, mImageSize, mDataSize))
		{
			// Success, we are done !
			mRawImage = NULL;
			return;
		}
	}
	else
	{
		// Create the new entry.
		idx = gTextureCachep->setHeaderCacheEntry(mID, entry, mImageSize,
												  mDataSize);
		if (idx < 0)
		{
			LL_WARNS() << "Texture: "  << mID
					<< ". Unable to create header entry for writing !"
					<< LL_ENDL;
			mDataSize = -1;	// Failed
			mRawImage = NULL;
			return;
		}
	}

	// Skip to the correct spot in the header file:
	S32 offset = idx * TEXTURE_CACHE_ENTRY_SIZE;
	// Record size is fixed for the header:
	S32 size = TEXTURE_CACHE_ENTRY_SIZE;
	S32 bytes_written;

	// Third stage, write the header, possibly with the texture if small enough
	if (mDataSize < TEXTURE_CACHE_ENTRY_SIZE)
	{
		// We need to write a full record in the header cache so, if the amount
		// of data is smaller than a record, we need to transfer the data to a
		// buffer padded with 0 and write that.
		U8* pad_buffer = (U8*)allocate_texture_mem(TEXTURE_CACHE_ENTRY_SIZE);
		if (!pad_buffer)
		{
			// Out of memory !
			mDataSize = -1;	// Failed
			mRawImage = NULL;
			return;
		}

		// Init with zeros
		memset(pad_buffer, 0, TEXTURE_CACHE_ENTRY_SIZE);
		// Copy the write buffer
		memcpy(pad_buffer, mWriteData, mDataSize);
		bytes_written =
			(S32)LLFile::write(gTextureCachep->mHeaderDataFileName,
								 pad_buffer, offset, size);
		free_texture_mem(pad_buffer);
	}
	else
	{
		// Write the header record (== first TEXTURE_CACHE_ENTRY_SIZE bytes of
		// the raw file) in the header file
		bytes_written =
			(S32)LLFile::write(gTextureCachep->mHeaderDataFileName,
								 mWriteData, offset, size);
	}

	if (bytes_written <= 0)
	{
		LL_WARNS() << "Unable to write header entry for texture: " << mID
				<< LL_ENDL;
		mCorrupted = true;
		mDataSize = -1;	// Failed
		mRawImage = NULL;
		return;
	}

	// If we wrote everything (may be more with padding) in the header cache,
	// we do not have a body to store, so we are done...
	if (mDataSize <= bytes_written)
	{
		mRawImage = NULL;
		return;
	}

	// Fourth stage: write the body file, i.e. the rest of the texture in an
	// file name derived from the texture UUID.
	if (mDataSize <= TEXTURE_CACHE_ENTRY_SIZE)
	{
		// It would not make sense to be here otherwise...
		mDataSize = -1;	// Failed
		mCorrupted = true;
		mRawImage = NULL;
		return;
	}

	S32 file_size = mDataSize - TEXTURE_CACHE_ENTRY_SIZE;
	// Build the cache file name from the UUID
	std::string filename = gTextureCachep->getTextureFileName(mID);
	LL_DEBUGS("TextureCache") << "Writing Body: " << filename << " - Bytes: "
							  << file_size << LL_ENDL;
	bytes_written = (S32)LLFile::write(filename,
										 mWriteData + TEXTURE_CACHE_ENTRY_SIZE,
										 0, file_size);
	if (bytes_written <= 0)
	{
		LL_WARNS() << "Texture "  << mID
				<< ". Incorrect number of bytes written to body: "
				<< bytes_written << " / " << file_size << LL_ENDL;
		mDataSize = -1;	// Failed
		mCorrupted = true;
	}

	// Nothing else to do at that point... Clean up and exit
	mRawImage = NULL;
}

void LLTextureCacheWorker::finishRead()
{
	bool success = mDataSize > 0;
	if (mResponder.notNull())		// Paranoia
	{
		if (success)
		{
			mResponder->setData(mReadData, mDataSize, mImageSize,
								mImageFormat, mImageLocal);
			mReadData = NULL;		// Responder owns data
			mDataSize = 0;
			++LLTextureCache::sTotalHits;
		}
		else
		{
			free_texture_mem(mReadData);
			mReadData = NULL;
			++LLTextureCache::sTotalMisses;
		}
		mResponder->completed(success);
	}
	if (gTextureCachep && mCorrupted)
	{
		gTextureCachep->removeFromCache(mID);
		++LLTextureCache::sTotalErrors;
	}
}

void LLTextureCacheWorker::finishWrite()
{
	bool success = mDataSize > 0;
	if (mResponder.notNull())		// Paranoia
	{
		mWriteData = NULL;			// We never owned data
		mDataSize = 0;
		mResponder->completed(success);
	}
	if (gTextureCachep && mCorrupted)
	{
		gTextureCachep->removeFromCache(mID);
		++LLTextureCache::sTotalErrors;
	}
	else if (success)
	{
		++LLTextureCache::sTotalWrites;
	}
}

//////////////////////////////////////////////////////////////////////////////
// Read responder for texture cache

// Called from LLTextureCacheWorker::finishRead()
//virtual
void LLTextureCache::ReadResponder::setData(U8* data, S32 datasize,
											S32 imagesize, S32 imageformat,
											bool imagelocal)
{
	if (mFormattedImage.notNull())
	{
		llassert_always(mFormattedImage->getCodec() == imageformat);
		mFormattedImage->appendData(data, datasize);
	}
	else
	{
		mFormattedImage = LLImageFormatted::createFromType(imageformat);
		mFormattedImage->setData(data, datasize);
	}
	mImageSize = imagesize;
	mImageLocal = imagelocal;
}

//////////////////////////////////////////////////////////////////////////////
// LLTextureCache class proper
//////////////////////////////////////////////////////////////////////////////

// Static members
U32 LLTextureCache::sCacheMaxEntries = 1024 * 1024;
S64 LLTextureCache::sCacheMaxTexturesSize = 0; // No limit

LLAtomicU32 LLTextureCache::sTotalHits(0);
LLAtomicU32 LLTextureCache::sTotalMisses(0);
LLAtomicU32 LLTextureCache::sTotalWrites(0);
LLAtomicU32 LLTextureCache::sTotalErrors(0);

static const char* entries_filename = "texture.entries";
static const char* cache_filename = "texture.cache";
static const char* textures_dirname = "texturecache";

LLTextureCache::LLTextureCache()
:	mHeaderFile(NULL),
	mTexturesSizeTotal(0),
	mNumReads(0),
	mNumWrites(0),
	mDoPurge(false),
	// Do not allow to change the texture cache until setReadOnly() is called:
	mReadOnly(true)
{
	gTextureCachep = this;

	// We use two threads to service this pool, in case of a spurious slow disk
	// operation or file corruption (which would otherwise block the queue
	// until the texture fetcher timeout fires).
	// *TODO: maybe also use two pools, one for writes and the other for
	// reads ?  HB
	LL_INFOS() << "Initializing with 2 worker threads..." << LL_ENDL;
	mThreadPoolp.reset(new LL::ThreadPool("Texture cache", 2));
	mThreadPoolp->start();
}

LLTextureCache::~LLTextureCache()
{
	purgeTextureFilesTimeSliced(true);
	writeUpdatedEntries();
	gTextureCachep = NULL;
}

void LLTextureCache::shutdown()
{
	if (mThreadPoolp)
	{
		mThreadPoolp->close();
		mThreadPoolp.reset(nullptr);
		LL_INFOS() << "Thread pool destroyed." << LL_ENDL;
	}
	LL_INFOS() << "Total hits: " << sTotalHits << " - Total misses: "
			<< sTotalMisses << " - Total writes: " << sTotalWrites
			<< " - Total errors: " << sTotalErrors << LL_ENDL;
}

//virtual
S32 LLTextureCache::update()
{
	S32 res = mThreadPoolp ? (S32)mThreadPoolp->getQueue().size() : 0;

	static F32 last_update = 0.f;
	constexpr F32 MAX_UPDATE_INTERVAL = 300.f; // in seconds.
	F32 time = (F32)gFrameTimeSeconds.value();
	if (!res && time - last_update > MAX_UPDATE_INTERVAL)
	{
		last_update = time;
		writeUpdatedEntries();
	}

	return res;
}

// Searches for local copy of UUID-based image file
std::string LLTextureCache::getLocalFileName(const LLUUID& id)
{
	// Does not include extension
	std::string idstr = id.asString();
	std::string filename = gDirUtilp->getExpandedFilename(LL_PATH_SKINS, "default",
												"textures", idstr);
	return filename;
}

std::string LLTextureCache::getTextureFileName(const LLUUID& id)
{
	std::string idstr = id.asString();
	return mTexturesDirName + LL_DIR_DELIM_STR + idstr[0] + LL_DIR_DELIM_STR +
		   idstr + ".texture";
}

bool LLTextureCache::isInCache(const LLUUID& id)
{
	bool in_cache;
	mHeaderMutex.lock();
	in_cache = mHeaderIDMap.count(id) != 0;
	mHeaderMutex.unlock();
	return in_cache;
}

bool LLTextureCache::isInLocal(const LLUUID& id)
{
	S32 local_size = 0;
	std::string local_filename;

	std::string filename = getLocalFileName(id);

	// Is it a JPEG2000 file ?
	{
		local_filename = filename + ".j2c";
		local_size = (S32)LLFile::size(local_filename);
		if (local_size > 0)
		{
			return true;
		}
	}

	// If not, is it a jpeg file ?
	{
		local_filename = filename + ".jpg";
		local_size = (S32)LLFile::size(local_filename);
		if (local_size > 0)
		{
			return true;
		}
	}

	// Is it a PNG file (used for UI texture mostly) ?
	{
		local_filename = filename + ".png";
		local_size = (S32)LLFile::size(local_filename);
		if (local_size > 0)
		{
			return true;
		}
	}

	// Hmm... What about a targa file (used for UI texture mostly) ?
	{
		local_filename = filename + ".tga";
		local_size = (S32)LLFile::size(local_filename);
		if (local_size > 0)
		{
			return true;
		}
	}

	return false;
}

void LLTextureCache::setDirNames(ELLPath location)
{
	mHeaderEntriesFileName = gDirUtilp->getExpandedFilename(location, textures_dirname,
												  entries_filename);
	mHeaderDataFileName = gDirUtilp->getExpandedFilename(location, textures_dirname,
											   cache_filename);
	mTexturesDirName = gDirUtilp->getExpandedFilename(location, textures_dirname);
}

void LLTextureCache::purgeCache(ELLPath location)
{
	mHeaderMutex.lock();

	if (!mReadOnly)
	{
		setDirNames(location);
		llassert_always(mHeaderFile == NULL);
	}

	// Remove the current texture cache.
	purgeAllTextures(true);

	mHeaderMutex.unlock();
}

// Called from the main thread.
S64 LLTextureCache::initCache(ELLPath location, S64 max_size)
{
	S64 entry_size = (9 * max_size) / 25; // 0.36 * max_size
	S64 max_entries = entry_size / (TEXTURE_CACHE_ENTRY_SIZE +
									TEXTURE_FAST_CACHE_ENTRY_SIZE);
	sCacheMaxEntries = (S32)(llmin((S64)sCacheMaxEntries, max_entries));
	entry_size = sCacheMaxEntries * (TEXTURE_CACHE_ENTRY_SIZE +
									 TEXTURE_FAST_CACHE_ENTRY_SIZE);
	max_size -= entry_size;
	if (sCacheMaxTexturesSize > 0)
	{
		sCacheMaxTexturesSize = llmin(sCacheMaxTexturesSize, max_size);
	}
	else
	{
		sCacheMaxTexturesSize = max_size;
	}
	max_size -= sCacheMaxTexturesSize;

	LL_INFOS() << "Headers: " << sCacheMaxEntries << " Textures size: "
			<< sCacheMaxTexturesSize / (1024 * 1024) << " MB" << LL_ENDL;

	setDirNames(location);

	if (!mReadOnly)
	{
		LLFile::mkdir(mTexturesDirName);

		const char* subdirs = "0123456789abcdef";
		for (S32 i = 0; i < 16; ++i)
		{
			LLFile::mkdir(mTexturesDirName + LL_DIR_DELIM_STR + subdirs[i]);
		}
	}
	readHeaderCache();

	// Calculate mTexturesSize and make some room in the texture cache if we
	// need it
	purgeTextures(true);

	return max_size; // unused cache space
}

//----------------------------------------------------------------------------
// mHeaderMutex must be locked for the following methods !

LLFile* LLTextureCache::openHeaderEntriesFile(bool readonly, S32 offset)
{
	llassert_always(mHeaderFile == NULL);
	auto flags = readonly
		? LLFile::in | LLFile::binary
		: LLFile::isfile(mHeaderEntriesFileName)
			? LLFile::in | LLFile::out | LLFile::binary
			: LLFile::in | LLFile::out | LLFile::trunc | LLFile::binary;
	std::error_code ec;
	// All code calling openHeaderEntriesFile, immediately calls
	// closeHeaderEntriesFile(), so this file is very short-lived.
	mHeaderFile = new LLFile(mHeaderEntriesFileName, flags, ec);
	if (offset > 0)
	{
		mHeaderFile->seek(offset, LLFile::beg, ec);
	}
	return mHeaderFile;
}

void LLTextureCache::closeHeaderEntriesFile()
{
	if (mHeaderFile)
	{
		delete mHeaderFile;
		mHeaderFile = NULL;
	}
}

void LLTextureCache::readEntriesHeader()
{
	llassert_always(mHeaderFile == NULL);

	// mHeaderEntriesInfo initializes to default values so it is safe not to
	// read it
	if (LLFile::isfile(mHeaderEntriesFileName))
	{
		std::error_code ec;
		LLFile::read(mHeaderEntriesFileName, (void*)&mHeaderEntriesInfo, 0,
					 sizeof(EntriesInfo), ec);
	}
	else	// Create an empty entries header.
	{
		mHeaderEntriesInfo.mVersion = TEXTURE_CACHE_VERSION;
		mHeaderEntriesInfo.mAddressSize = CACHE_ADDRESS_SIZE_BITS;
		mHeaderEntriesInfo.mEntries = 0;
		writeEntriesHeader();
	}
}

void LLTextureCache::writeEntriesHeader()
{
	llassert_always(mHeaderFile == NULL);
	if (!mReadOnly)
	{
		std::error_code ec;
		LLFile::write(mHeaderEntriesFileName, (void*)&mHeaderEntriesInfo, 0,
					  sizeof(EntriesInfo), ec);
	}
}

S32 LLTextureCache::openAndReadEntry(const LLUUID& id, Entry& entry,
									 bool create)
{
	mLRUMutex.lock();
	mLRU.erase(id);
	mLRUMutex.unlock();

	LLMutexLock hlock(&mHeaderMutex);
	S32 idx = -1;

	id_map_t::iterator iter1 = mHeaderIDMap.find(id);
	if (iter1 != mHeaderIDMap.end())
	{
		idx = iter1->second;
	}

	if (idx < 0)
	{
		if (create && !mReadOnly)
		{
			if (mHeaderEntriesInfo.mEntries < sCacheMaxEntries)
			{
				// Add an entry to the end of the list
				idx = mHeaderEntriesInfo.mEntries++;

			}
			else if (!mFreeList.empty())
			{
				std::set<S32>::iterator it = mFreeList.begin();
				idx = *it;
				mFreeList.erase(it);
			}
			else
			{
				mLRUMutex.lock();
				// Look for a still valid entry in the LRU
				for (uuid_list_t::iterator iter2 = mLRU.begin();
					 iter2 != mLRU.end(); )
				{
					uuid_list_t::iterator curiter2 = iter2++;
					LLUUID oldid = *curiter2;
					// Erase entry from LRU regardless
					mLRU.erase(curiter2);
					// Look up entry and use it if it is valid
					id_map_t::iterator iter3 = mHeaderIDMap.find(oldid);
					if (iter3 != mHeaderIDMap.end() && iter3->second >= 0)
					{
						idx = iter3->second;
						// Remove the existing cached texture to release the
						// entry index.
						removeCachedTexture(oldid);
						break;
					}
				}
				// If (idx < 0) at this point, we will rebuild the LRU and
				// retry if called from setHeaderCacheEntry(), otherwise this
				// should not happen and will trigger an error
				mLRUMutex.unlock();
			}
			if (idx >= 0)
			{
				entry.mID = id;
				entry.mImageSize = -1; // Mark it is a brand-new entry.
				entry.mBodySize = 0;
			}
		}
	}
	else
	{
		// Read the entry
		S32 bytes_read = sizeof(Entry);
		idx_entry_map_t::iterator iter = mUpdatedEntryMap.find(idx);
		if (iter != mUpdatedEntryMap.end())
		{
			entry = iter->second;
		}
		else
		{
			bytes_read = readEntryFromHeaderImmediatelyShared(idx, entry);
		}
		if (bytes_read != sizeof(Entry))
		{
			clearCorruptedCache() ; // Clear the cache.
			idx = -1;
		}
		// It happens on 64 bits systems, do not know why
		else if (entry.mImageSize <= entry.mBodySize)
		{
			LL_WARNS() << "Corrupted entry: " << id << " - Entry image size: "
					<< entry.mImageSize << " - Entry body size: "
					<< entry.mBodySize << LL_ENDL;

			// Erase this entry and the cached texture from the cache.
			std::string tex_filename = getTextureFileName(id);
			removeEntry(idx, entry, tex_filename);
			mUpdatedEntryMap.erase(idx);
			idx = -1;
		}
	}
	return idx;
}

// mHeaderMutex must be locked before calling this.
void LLTextureCache::writeEntryToHeaderImmediately(S32& idx, Entry& entry,
												   bool write_header)
{
	LLFile* file;
	S32 bytes_written;
	std::error_code ec;
	S32 offset = sizeof(EntriesInfo) + idx * sizeof(Entry);
	if (write_header)
	{
		file = openHeaderEntriesFile(false, 0);
		bytes_written = (S32)file->write((U8*)&mHeaderEntriesInfo,
										sizeof(EntriesInfo), ec);
		if (bytes_written != sizeof(EntriesInfo))
		{
			clearCorruptedCache();	// Clear the cache.
			idx = -1;				// Mark the index as invalid.
			return;
		}

		mHeaderFile->seek(offset, LLFile::beg, ec);
	}
	else
	{
		file = openHeaderEntriesFile(false, offset);
	}
	bytes_written = (S32)file->write((U8*)&entry, (S32)sizeof(Entry), ec);
	if (bytes_written != sizeof(Entry))
	{
		clearCorruptedCache();		// Clear the cache.
		idx = -1;					// Mark the index as invalid.
		return;
	}

	closeHeaderEntriesFile();
	mUpdatedEntryMap.erase(idx);
}

// mHeaderMutex must be locked before calling this.
void LLTextureCache::readEntryFromHeaderImmediately(S32& idx, Entry& entry)
{
	S32 offset = sizeof(EntriesInfo) + idx * sizeof(Entry);
	LLFile* file = openHeaderEntriesFile(true, offset);
	std::error_code ec;
	S32 bytes_read = (S32)file->read((U8*)&entry, (S32)sizeof(Entry), ec);
	closeHeaderEntriesFile();

	if (bytes_read != sizeof(Entry))
	{
		clearCorruptedCache();	// Clear the cache.
		idx = -1;				// Mark the index as invalid.
	}
}

S32 LLTextureCache::readEntryFromHeaderImmediatelyShared(S32& idx,
														 Entry& entry)
{
	S64 offset = (S64)sizeof(EntriesInfo) + (S64)idx * sizeof(Entry);
	std::error_code ec;
	return (S32)LLFile::read(mHeaderEntriesFileName, (void*)&entry, offset,
							 sizeof(Entry), ec);
}

// mHeaderMutex must be locked before calling this.
// Updates an existing entry time stamp, delays writing.
void LLTextureCache::updateEntryTimeStamp(S32 idx, Entry& entry)
{
	static const U32 max_entries_without_time_stamp =
		(U32)(LLTextureCache::sCacheMaxEntries * 0.75f);

	if (mHeaderEntriesInfo.mEntries < max_entries_without_time_stamp)
	{
		// There are enough empty entry index space, no need to stamp time.
		return;
	}

	if (idx >= 0 && !mReadOnly)
	{
		entry.mTime = time(NULL);
		mUpdatedEntryMap[idx] = entry;
	}
}

// Updates an existing entry if needed, writing to header file immediately.
bool LLTextureCache::updateEntry(S32& idx, Entry& entry, S32 new_image_size,
								 S32 new_data_size)
{
	S32 new_body_size = llmax(0, new_data_size - TEXTURE_CACHE_ENTRY_SIZE);

	if (new_image_size <= entry.mImageSize && new_body_size <= entry.mBodySize)
	{
		// Nothing changed, or a higher resolution version is already in cache.
		return true;
	}

	bool purge = false;

	mHeaderMutex.lock();

	bool update_header = false;
	if (entry.mImageSize < 0) // Is a brand-new entry
	{
		mHeaderIDMap[entry.mID] = idx;
		mTexturesSizeMap[entry.mID] = new_body_size;
		mTexturesSizeTotal += new_body_size;

		// Update Header
		update_header = true;
	}
	else if (entry.mBodySize != new_body_size)
	{
		// Aready in mHeaderIDMap.
		mTexturesSizeMap[entry.mID] = new_body_size;
		mTexturesSizeTotal -= entry.mBodySize;
		mTexturesSizeTotal += new_body_size;
	}
	entry.mTime = time(NULL);
	entry.mImageSize = new_image_size;
	entry.mBodySize = new_body_size;

	writeEntryToHeaderImmediately(idx, entry, update_header);

	if (mTexturesSizeTotal > sCacheMaxTexturesSize)
	{
		purge = true;
	}

	mHeaderMutex.unlock();

	if (purge)
	{
		mDoPurge = true;
	}

	return false;
}

// mHeaderMutex must be locked before calling this.
U32 LLTextureCache::openAndReadEntries(std::vector<Entry>& entries)
{
	U32 num_entries = mHeaderEntriesInfo.mEntries;

	mHeaderIDMap.clear();
	mTexturesSizeMap.clear();
	mFreeList.clear();
	mTexturesSizeTotal = 0;

	LLFile* file = NULL;
	if (mUpdatedEntryMap.empty())
	{
		file = openHeaderEntriesFile(true, (S32)sizeof(EntriesInfo));
	}
	else	// Update the header file first.
	{
		file = openHeaderEntriesFile(false, 0);
		if (!file)
		{
			return 0;
		}
		updatedHeaderEntriesFile();
		std::error_code ec;
		file->seek((S32)sizeof(EntriesInfo), LLFile::beg, ec);
	}
	try
	{
		std::error_code ec;
		for (U32 idx = 0; idx < num_entries; ++idx)
		{
			Entry entry;
			S64 bytes_read = file->read((U8*)&entry, (S32)sizeof(Entry), ec);
			if (bytes_read < (S32)sizeof(Entry))
			{
				LL_WARNS() << "Corrupted header entries, failed at " << idx
						<< " / " << num_entries << LL_ENDL;
				closeHeaderEntriesFile();
				purgeAllTextures(false);
				return 0;
			}
			entries.emplace_back(entry);
			if (entry.mImageSize > entry.mBodySize)
			{
				mHeaderIDMap[entry.mID] = idx;
				mTexturesSizeMap[entry.mID] = entry.mBodySize;
				mTexturesSizeTotal += entry.mBodySize;
			}
			else
			{
				mFreeList.insert(idx);
			}
		}
	}
	catch (...)
	{
		LL_WARNS() << "Failed to read cache entries: out of memory ?" << LL_ENDL;
		closeHeaderEntriesFile();
		purgeAllTextures(false);
		return 0;		
	}

	closeHeaderEntriesFile();
	return num_entries;
}

void LLTextureCache::writeEntriesAndClose(const std::vector<Entry>& entries)
{
	S32 num_entries = entries.size();
	llassert_always(num_entries == (S32)mHeaderEntriesInfo.mEntries);

	if (!mReadOnly)
	{
		LLFile* file = openHeaderEntriesFile(false, (S32)sizeof(EntriesInfo));
		std::error_code ec;
		for (S32 idx = 0; idx < num_entries; ++idx)
		{
			S64 bytes_written = file->write((U8*)(&entries[idx]),
											(S32)sizeof(Entry), ec);
			if (bytes_written != sizeof(Entry))
			{
				clearCorruptedCache();	// clear the cache.
				return;
			}
		}
		closeHeaderEntriesFile();
	}
}

void LLTextureCache::writeUpdatedEntries()
{
	mHeaderMutex.lock();
	if (!mReadOnly && !mUpdatedEntryMap.empty())
	{
		openHeaderEntriesFile(false, 0);
		updatedHeaderEntriesFile();
		closeHeaderEntriesFile();
	}
	mHeaderMutex.unlock();
}

// mHeaderMutex must be locked and mHeaderFile must be created before calling
// this.
void LLTextureCache::updatedHeaderEntriesFile()
{
	if (!mReadOnly && !mUpdatedEntryMap.empty() && mHeaderFile)
	{
		// EntriesInfo
		std::error_code ec;
		mHeaderFile->seek(0, LLFile::beg, ec);
		S64 bytes_written = mHeaderFile->write((U8*)&mHeaderEntriesInfo,
											   sizeof(EntriesInfo), ec);
		if (bytes_written != sizeof(EntriesInfo))
		{
			clearCorruptedCache(); // Clear the cache.
			return;
		}

		// Write each updated entry
		S32 entry_size = (S32)sizeof(Entry);
		S32 prev_idx = -1;
		S32 delta_idx;
		for (idx_entry_map_t::iterator iter = mUpdatedEntryMap.begin(),
									   end = mUpdatedEntryMap.end();
			 iter != end; ++iter)
		{
			delta_idx = iter->first - prev_idx - 1;
			prev_idx = iter->first;
			if (delta_idx)
			{
				mHeaderFile->seek(delta_idx * entry_size, LLFile::cur, ec);
			}

			bytes_written = mHeaderFile->write((U8*)&iter->second, entry_size,
											   ec);
			if (bytes_written != entry_size)
			{
				clearCorruptedCache(); // Clear the cache.
				return;
			}
		}
		mUpdatedEntryMap.clear();
	}
}

// Called from either the main thread or the worker thread
void LLTextureCache::readHeaderCache()
{
	mLRUMutex.lock();
	mLRU.clear(); // Always clear the LRU
	mLRUMutex.unlock();

	bool repeat_reading = false;

	{
		LLMutexLock hlock(&mHeaderMutex);

		readEntriesHeader();
		if (mHeaderEntriesInfo.mVersion != TEXTURE_CACHE_VERSION ||
			mHeaderEntriesInfo.mAddressSize != CACHE_ADDRESS_SIZE_BITS)
		{
			if (!mReadOnly)
			{
				purgeAllTextures(false);
			}
			return;
		}

		std::vector<Entry> entries;
		U32 num_entries = openAndReadEntries(entries);
		if (!num_entries)
		{
			return;
		}

		U32 empty_entries = 0;
		typedef std::pair<U32, S32> lru_data_t;
		std::set<lru_data_t> lru;
		std::set<U32> purge_list;
		for (U32 i = 0; i < num_entries; ++i)
		{
			Entry& entry = entries[i];
			if (entry.mImageSize <= 0)
			{
				// This will be in the Free List, do not put it in the LRU
				++empty_entries;
			}
			else
			{
				lru.emplace(entry.mTime, i);
				if (entry.mBodySize > 0)
				{
					if (entry.mBodySize > entry.mImageSize)
					{
						// Should not happen, failsafe only
						LL_WARNS() << "Bad entry: " << i << ": " << entry.mID
								<< ": BodySize: " << entry.mBodySize
								<< LL_ENDL;
						purge_list.insert(i);
					}
				}
			}
		}
		if (num_entries - empty_entries > sCacheMaxEntries)
		{
			// Special case: cache size was reduced, need to remove entries.
			// Note: After we prune entries, we will call this again and
			// create the LRU
			U32 entries_to_purge = num_entries - empty_entries -
								   sCacheMaxEntries;
			LL_INFOS() << "Texture Cache Entries: " << num_entries << " Max: "
					<< sCacheMaxEntries << " Empty: " << empty_entries
					<< " Purging: " << entries_to_purge << LL_ENDL;
			for (std::set<lru_data_t>::iterator iter = lru.begin(),
												end = lru.end();
				 iter != end; ++iter)
			{
				purge_list.insert(iter->second);
				if (purge_list.size() >= entries_to_purge)
				{
					break;
				}
			}
		}
		else
		{
			mLRUMutex.lock();
			S32 lru_entries = (S32)((F32)sCacheMaxEntries *
									TEXTURE_CACHE_LRU_SIZE);
			for (std::set<lru_data_t>::iterator iter = lru.begin(),
												end = lru.end();
				 iter != end; ++iter)
			{
				mLRU.emplace(entries[iter->second].mID);
				if (--lru_entries <= 0)
				{
					break;
				}
			}
			mLRUMutex.unlock();
		}

		if (!purge_list.size())
		{
			// Entries are not changed, nothing to do.
			return;
		}

		for (std::set<U32>::iterator iter = purge_list.begin(),
									 end = purge_list.end();
			 iter != end; ++iter)
		{
			std::string tex_filename =
				getTextureFileName(entries[*iter].mID);
			removeEntry((S32)*iter, entries[*iter], tex_filename);
		}
		// If we removed any entries, we need to rebuild the entries list,
		// write the header, and call this again
		std::vector<Entry> new_entries;
		for (U32 i = 0; i < num_entries; ++i)
		{
			const Entry& entry = entries[i];
			if (entry.mImageSize > 0)
			{
				new_entries.emplace_back(entry);
			}
		}
		mFreeList.clear();     // Recreating list, no longer valid.
		llassert_always(new_entries.size() <= sCacheMaxEntries);
		mHeaderEntriesInfo.mEntries = new_entries.size();
		writeEntriesHeader();
		writeEntriesAndClose(new_entries);
		repeat_reading = true;
	}

	// Repeat with new entries file
	if (repeat_reading)
	{
		readHeaderCache();
	}
}

// mHeaderMutex must be locked before calling this.
void LLTextureCache::clearCorruptedCache()
{
	LL_WARNS() << "The texture cache is corrupted:clearing it." << LL_ENDL;

	closeHeaderEntriesFile();	// Close possible file handler
	purgeAllTextures(false);	// Clear the cache.

	if (!mReadOnly)
	{
		// Regenerate the directory tree if it does not exist
		LLFile::mkdir(mTexturesDirName);

		const char* subdirs = "0123456789abcdef";
		for (S32 i = 0; i < 16; ++i)
		{
			LLFile::mkdir(mTexturesDirName + LL_DIR_DELIM_STR + subdirs[i]);
		}
	}
}

void LLTextureCache::purgeAllTextures(bool purge_directories)
{
	if (!mReadOnly)
	{
		const char* subdirs = "0123456789abcdef";
		std::string dirname;
		for (S32 i = 0; i < 16; ++i)
		{
			dirname = mTexturesDirName + LL_DIR_DELIM_STR + subdirs[i];
			LL_INFOS() << "Deleting files in directory: " << dirname << LL_ENDL;
			LLDirIterator::deleteFilesInDir(dirname);
			if (purge_directories)
			{
				LLFile::remove(dirname);
			}
		}
		if (purge_directories)
		{
			LLDirIterator::deleteFilesInDir(mTexturesDirName);
			LLFile::remove(mTexturesDirName);
		}
	}
	mHeaderIDMap.clear();
	mTexturesSizeMap.clear();
	mTexturesSizeTotal = 0;
	mFreeList.clear();
	mTexturesSizeTotal = 0;
	mUpdatedEntryMap.clear();

	// Info with 0 entries
	mHeaderEntriesInfo.mVersion = TEXTURE_CACHE_VERSION;
	mHeaderEntriesInfo.mAddressSize = CACHE_ADDRESS_SIZE_BITS;
	mHeaderEntriesInfo.mEntries = 0;
	writeEntriesHeader();

	LL_INFOS() << "The entire texture cache is cleared." << LL_ENDL;
}

void LLTextureCache::purgeTextures(bool validate)
{
	mDoPurge = false;

	if (mReadOnly)
	{
		return;
	}

	if (!validate && mTexturesSizeTotal < sCacheMaxTexturesSize)
	{
		return;
	}

	LLMutexLock hlock(&mHeaderMutex);

	// Read the entries list
	std::vector<Entry> entries;
	U32 num_entries = openAndReadEntries(entries);
	if (!num_entries)
	{
		return; // Nothing to purge
	}

	LL_INFOS() << "Purging the cache from old textures..." << LL_ENDL;

	// Use mTexturesSizeMap to collect UUIDs of textures with bodies
	typedef std::set<std::pair<U32, S32> > time_idx_set_t;
	time_idx_set_t time_idx_set;
	id_map_t::iterator header_id_map_end = mHeaderIDMap.end();
	for (size_map_t::iterator iter1 = mTexturesSizeMap.begin(),
							  end = mTexturesSizeMap.end();
		 iter1 != end; ++iter1)
	{
		if (iter1->second > 0)
		{
			id_map_t::iterator iter2 = mHeaderIDMap.find(iter1->first);
			if (iter2 == header_id_map_end)
			{
				LL_WARNS() << "mTexturesSizeMap / mHeaderIDMap corrupted."
						<< LL_ENDL;
				clearCorruptedCache();
				return;
			}
			S32 idx = iter2->second;
			time_idx_set.emplace(entries[idx].mTime, idx);
		}
	}

	// Validate 1/32th of the files on startup
	constexpr U32 FRACTION = 8;	// 256 / 8 = 32
	U32 validate_idx = 0;
	if (validate)
	{
		validate_idx = (gSavedSettings.getU32("CacheValidateCounter") /
						FRACTION) * FRACTION;
		U32 next_idx = (validate_idx + FRACTION) % 256;
		gSavedSettings.setU32("CacheValidateCounter", next_idx);
		LL_DEBUGS("TextureCache") << "Validating indexes " << validate_idx
								  << " to " << validate_idx + FRACTION - 1
								  << LL_ENDL;
	}

	S64 cache_size = mTexturesSizeTotal;
	S64 purged_cache_size =
		(TEXTURE_PURGED_CACHE_SIZE * sCacheMaxTexturesSize) / (S64)100;
	S32 purge_count = 0;
	for (time_idx_set_t::iterator iter = time_idx_set.begin(),
								  end = time_idx_set.end();
		 iter != end; ++iter)
	{
		S32 idx = iter->second;
		bool purge_entry = false;
		std::string filename = getTextureFileName(entries[idx].mID);
		if (cache_size >= purged_cache_size)
		{
			purge_entry = true;
		}
		else if (validate)
		{
			// Make sure file exists and is the correct size
			U32 uuididx = entries[idx].mID.mData[0];
			if (uuididx >= validate_idx && uuididx < validate_idx + 4)
			{
 				LL_DEBUGS("TextureCache") << "Validating: " << filename
										  << "Size: " << entries[idx].mBodySize
										  << LL_ENDL;
				S32 bodysize = (S32)LLFile::size(filename);
				if (bodysize != entries[idx].mBodySize)
				{
					LL_WARNS() << "Purging corrupted cached texture (body size "
							<< bodysize << " != " << entries[idx].mBodySize
							<< "): " << filename << LL_ENDL;
					purge_entry = true;
				}
			}
		}
		else
		{
			break;
		}

		if (purge_entry)
		{
			++purge_count;
			mFilesToDelete.emplace(entries[idx].mID, filename);
			cache_size -= entries[idx].mBodySize;
			// Remove the entry but not the file:
			removeEntry(idx, entries[idx], filename, false);
		}
	}

	LL_DEBUGS("TextureCache") << "Writing Entries: " << num_entries << LL_ENDL;

	if (purge_count > 0)
	{
		writeEntriesAndClose(entries);

		LL_INFOS() << "Purged: " << purge_count << " - Entries: " << num_entries
				<< " - Cache size: " << mTexturesSizeTotal / 1048576 << " MB"
				<< " - Files scheduled for deletion: " << mFilesToDelete.size()
				<< LL_ENDL;
	}
	else
	{
		LL_INFOS() << "Nothing to purge." << LL_ENDL;
	}

	mSlicedPurgeTimer.reset();
}

void LLTextureCache::purgeTextureFilesTimeSliced(bool force)
{
	constexpr F32 delay_between_passes = 2.f;	// seconds
	constexpr F32 max_time_per_pass = 0.1f;		// seconds

	if (!force && mSlicedPurgeTimer.getElapsedTimeF32() <= delay_between_passes)
	{
		return;
	}

	if (!mFilesToDelete.empty())
	{
		LL_INFOS() << "Time-sliced purging with " << mFilesToDelete.size()
				<< " files scheduled for deletion" << LL_ENDL;

		mSlicedPurgeTimer.reset();

		mHeaderMutex.lock();

		U32 purged = 0;
		std::string filename;

		for (LLTextureCache::purge_map_t::iterator
				iter = mFilesToDelete.begin();
			 iter != mFilesToDelete.end(); )
		{
			LLTextureCache::purge_map_t::iterator curiter = iter++;
			// Only remove files for textures that have not been cached again
			// since we selected them for removal !
			if (!mHeaderIDMap.count(curiter->first))
			{
				filename = curiter->second;
				LLFile::remove(filename);
			}
			else
			{
				LL_DEBUGS("TextureCache") << curiter->second
										  << " selected for removal, but texture cached again since !"
										  << LL_ENDL;
			}
			mFilesToDelete.erase(curiter);
			++purged;

			if (!force &&
				mSlicedPurgeTimer.getElapsedTimeF32() > max_time_per_pass)
			{
				break;
			}
		}

		if (mFilesToDelete.empty())
		{
			LL_INFOS() << "Time-sliced purge finished with " << purged
					<< " files deleted in "
					<< mSlicedPurgeTimer.getElapsedTimeF32() << "s" << LL_ENDL;
		}
		else
		{
			LL_INFOS() << "time sliced purge: " << purged << " files deleted in "
					<< mSlicedPurgeTimer.getElapsedTimeF32() << "s ("
					<< mFilesToDelete.size() << " files left for next pass)"
					<< LL_ENDL;
		}

		mHeaderMutex.unlock();

		mSlicedPurgeTimer.reset();
	}
}

//////////////////////////////////////////////////////////////////////////////
// Called from pool work threads

// Reads imagesize from the header, updates timestamp
S32 LLTextureCache::getHeaderCacheEntry(const LLUUID& id, Entry& entry)
{
	S32 idx = openAndReadEntry(id, entry, false);
	if (idx >= 0)
	{
		mHeaderMutex.lock();
		updateEntryTimeStamp(idx, entry); // Updates time
		mHeaderMutex.unlock();
	}
	return idx;
}

// Writes imagesize to the header, updates timestamp
S32 LLTextureCache::setHeaderCacheEntry(const LLUUID& id, Entry& entry,
										S32 imagesize, S32 datasize)
{
	S32 idx = openAndReadEntry(id, entry, true); // read or create

	if (idx < 0) // Retry once
	{
		readHeaderCache(); // We could not write an entry, so refresh the LRU

		idx = openAndReadEntry(id, entry, true);
	}

	if (idx >= 0)
	{
		updateEntry(idx, entry, imagesize, datasize);
	}
	else
	{
		LL_WARNS() << "Failed to set cache entry for image: " << id << LL_ENDL;
		clearCorruptedCache();
	}

	return idx;
}

// Called after mHeaderMutex is locked.
void LLTextureCache::removeCachedTexture(const LLUUID& id)
{
	size_map_t::iterator it = mTexturesSizeMap.find(id);
	if (it != mTexturesSizeMap.end())
	{
		mTexturesSizeTotal -= it->second;
		mTexturesSizeMap.erase(id);
	}
	mHeaderIDMap.erase(id);
	LLFile::remove(getTextureFileName(id));
}

// Called after mHeaderMutex is locked.
void LLTextureCache::removeEntry(S32 idx, Entry& entry, std::string& filename,
								 bool remove_file)
{
 	bool file_maybe_exists = true;	// Always attempt to remove when idx is invalid.

	if (idx >= 0) //valid entry
	{
		if (entry.mBodySize == 0)	// Always attempt to remove when mBodySize > 0.
		{
			// Sanity check. Should not exist when body size is 0.
			if (LLFile::exists(filename))
			{
				LL_WARNS() << "Entry has body size of zero but file " << filename
						<< " exists. Deleting this file, too." << LL_ENDL;
			}
			else
			{
				file_maybe_exists = false;
			}
		}
		mTexturesSizeTotal -= entry.mBodySize;
		entry.mImageSize = -1;
		entry.mBodySize = 0;
		mHeaderIDMap.erase(entry.mID);
		mTexturesSizeMap.erase(entry.mID);
		mFreeList.insert(idx);
	}

	if (file_maybe_exists && remove_file)
	{
		LLFile::remove(filename);
	}
}

bool LLTextureCache::removeFromCache(const LLUUID& id)
{
	//LL_INFOS() << "Removing texture from cache: " << id << LL_ENDL;
	bool ret = false;
	if (!mReadOnly)
	{
		Entry entry;
		S32 idx = openAndReadEntry(id, entry, false);
		std::string tex_filename = getTextureFileName(id);

		mHeaderMutex.lock();
		removeEntry(idx, entry, tex_filename);
		if (idx >= 0)
		{
			writeEntryToHeaderImmediately(idx, entry);
			ret = true;
		}
		mHeaderMutex.unlock();
	}
	return ret;
}

///////////////////////////////////////////////////////////////////////////////
// Called from the texture fetcher thread (i.e. LLTextureFetch) and from the
// main thread in HBObjectBackup's and LKFloaterColladaExport's idle callbacks.

bool LLTextureCache::readFromFile(const std::string& filename,
								  const LLUUID& id, S32 offset, S32 size,
								  ReadResponder* responder)
{
	if (offset == 0)	// To avoid spam from possible successive chunks reads.
	{
		LL_DEBUGS("TextureCache") << "Request to read texture from file: "
								  << filename << LL_ENDL;
	}

	if (!mThreadPoolp)
	{
		return false;
	}

	++mNumReads;
	mThreadPoolp->getQueue().post(
		[req = LLTextureCacheLocalFileWorker(filename, id, NULL, size, offset,
											 0, responder)]() mutable
		{
			LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
			// Queued file read operations are aborted on shutdown to prevent
			// crashes (because LLThreadPool did already shut down on
			// LLApp::isExiting()); this it not a big deal, since we do not
			// care about rendering textures at this point !  HB
			if (!LLApp::isExiting())
			{
				req.doRead();
				req.finishRead();
			}
			if (gTextureCachep)
			{
				--gTextureCachep->mNumReads;
			}
		});

	return true;
}

bool LLTextureCache::readFromCache(const LLUUID& id, S32 offset, S32 size,
								   ReadResponder* responder)
{
	if (!mThreadPoolp)
	{
		return false;
	}

	++mNumReads;
	mThreadPoolp->getQueue().post(
		[req = LLTextureCacheRemoteWorker(id, NULL, size, offset, 0, NULL, 0,
										  responder)]() mutable
		{
			LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
			// Queued file read operations are aborted on shutdown to prevent
			// crashes (because LLThreadPool did already shut down on
			// LLApp::isExiting()); this it not a big deal, since we do not
			// care about rendering textures at this point !  HB
			if (!LLApp::isExiting())
			{
				req.doRead();
				req.finishRead();
			}
			if (gTextureCachep)
			{
				--gTextureCachep->mNumReads;
			}
		});

	return true;
}

bool LLTextureCache::writeToCache(const LLUUID& id, U8* data, S32 datasize,
								  S32 imagesize, LLPointer<LLImageRaw> rawimage,
								  S32 discardlevel, WriteResponder* responder)
{
	if (!mThreadPoolp || mReadOnly)
	{
		return false;
	}

	if (mDoPurge)
	{
		purgeTextures(false);
	}
	static LLCachedControl<bool> purge_time_sliced(gSavedSettings,
												   "CachePurgeTimeSliced");
	purgeTextureFilesTimeSliced(!purge_time_sliced);

	// This may happen when a texture fails to decode...
	if (rawimage.isNull() || !rawimage->getData())
	{
		return false;
	}

	++mNumWrites;
	mThreadPoolp->getQueue().post(
		[req = LLTextureCacheRemoteWorker(id, data, datasize, 0, imagesize,
										  rawimage, discardlevel,
										  responder)]() mutable
		{
			LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
			// Queued file write operations are aborted on shutdown to prevent
			// crashes (because LLThreadPool did already shut down on
			// LLApp::isExiting()); this it not a big deal, since it simply
			// means the texture will not get cached at all...  HB
			if (!LLApp::isExiting())
			{
				req.doWrite();
				req.finishWrite();
			}
			if (gTextureCachep)
			{
				--gTextureCachep->mNumWrites;
			}
		});

	return true;
}
