/**
 * @file lltexturecache.h
 * @brief Object for managing texture cachees.
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

#pragma once

#include <map>
#include <memory>
#include <set>

#include <boost/unordered/unordered_flat_map.hpp>

#include "llatomic.h"
#include "lldir.h"
#include "llerror.h"
#include "llfile.h"
#include "llmutex.h"
#include "llpointer.h"
#include "llstring.h"
#include "llrefcount.h"
#include "lltimer.h"
#include "lluuid.h"
#include "threadpool.h"

class LLImageFormatted;

class LLTextureCache
{
	friend class LLTextureCacheWorker;
	friend class LLTextureCacheRemoteWorker;
	friend class LLTextureCacheLocalFileWorker;

protected:
	LOG_CLASS(LLTextureCache);

private:
	// Entries
	struct EntriesInfo
	{
		EntriesInfo()
		:	mVersion(0.f),
			mAddressSize(0),
			mEntries(0)
		{
		}

		F32 mVersion;
		U32 mAddressSize;
		U32 mEntries;
	};

	struct Entry
	{
        Entry()
		:	mBodySize(0),
			mImageSize(0),
			mTime(0)
		{
		}

		Entry(const LLUUID& id, S32 imagesize, S32 bodysize, U32 time)
		:	mID(id),
			mImageSize(imagesize),
			mBodySize(bodysize),
			mTime(time)
		{
		}

		inline void init(const LLUUID& id, U32 time)
		{
			mID = id;
			mImageSize = mBodySize = 0;
			mTime = time;
		}

		inline Entry& operator=(const Entry& entry)
		{
			mID = entry.mID;
			mImageSize = entry.mImageSize;
			mBodySize = entry.mBodySize;
			mTime = entry.mTime;
			return *this;
		}

		LLUUID	mID;		// 16 bytes
		S32		mImageSize;	// total size of image if known
		S32		mBodySize;	// size of body file in body cache
		U32		mTime;		// seconds since 1/1/1970
	};

public:
	class Responder : public LLThreadSafeRefCount
	{
	public:
		virtual ~Responder() = default;

		virtual void started() = 0;
		virtual void completed(bool success) = 0;

		virtual void setData(U8* data, S32 datasize, S32 imagesize,
							 S32 imageformat, bool imagelocal) = 0;
	};

	class ReadResponder : public Responder
	{
	public:
		inline ReadResponder()
		:	mImageSize(0),
			mImageLocal(false)
		{
		}

		// Called from LLTextureCacheWorker::finishRead()
		void setData(U8* data, S32 datasize, S32 imagesize,
					 S32 imageformat, bool imagelocal) override;

		// Called from LLTextureFetchWorker::CacheReadResponder constructor
		inline void setImage(LLImageFormatted* image)
		{
			mFormattedImage = image;
		}

	protected:
		LLPointer<LLImageFormatted>	mFormattedImage;
		S32							mImageSize;
		bool						mImageLocal;
	};

	class WriteResponder : public Responder
	{
		// Not used for write operations.
		inline void setData(U8*, S32, S32, S32, bool) override
		{
		}
	};

	LLTextureCache();
	~LLTextureCache();

	void shutdown();

	S32 update();

	void purgeCache(ELLPath location);

	// Called from the main thread before initCache() is called.
	inline void setReadOnly(bool read_only)	{ mReadOnly = read_only; }

	S64 initCache(ELLPath location, S64 maxsize);

	// This is for reads from local files (typically, UI textures).
	bool readFromFile(const std::string& local_filename, const LLUUID& id,
					  S32 offset, S32 size, ReadResponder* responder);

	// This is for reads from the actual textures cache.
	bool readFromCache(const LLUUID& id, S32 offset, S32 size,
					   ReadResponder* responder);

	bool writeToCache(const LLUUID& id, U8* data, S32 datasize,
					  S32 imagesize, LLPointer<LLImageRaw> rawimage,
					  S32 discardlevel, WriteResponder* responder);

	bool removeFromCache(const LLUUID& id);

	// Debug
	inline U32 getNumReads()					{ return mNumReads; }
	inline U32 getNumWrites()				{ return mNumWrites; }
	inline S64 getUsage()					{ return mTexturesSizeTotal; }
	inline U32 getEntries()					{ return mHeaderEntriesInfo.mEntries; }

	bool isInCache(const LLUUID& id);
	bool isInLocal(const LLUUID& id);			// NOT thread-safe

	inline static S64 getMaxUsage()			{ return sCacheMaxTexturesSize; }

protected:
	std::string getLocalFileName(const LLUUID& id);
	std::string getTextureFileName(const LLUUID& id);
	void setDirNames(ELLPath location);
	void readHeaderCache();
	void clearCorruptedCache();
	void purgeAllTextures(bool purge_directories);
	void purgeTextures(bool validate);
	void purgeTextureFilesTimeSliced(bool force = false);
	LLFile* openHeaderEntriesFile(bool readonly, S32 offset);
	void closeHeaderEntriesFile();
	void readEntriesHeader();
	void writeEntriesHeader();
	S32 openAndReadEntry(const LLUUID& id, Entry& entry, bool create);
	bool updateEntry(S32& idx, Entry& entry, S32 new_image_size,
					 S32 new_body_size);
	void updateEntryTimeStamp(S32 idx, Entry& entry);
	U32 openAndReadEntries(std::vector<Entry>& entries);
	void writeEntriesAndClose(const std::vector<Entry>& entries);
	void readEntryFromHeaderImmediately(S32& idx, Entry& entry);
	S32 readEntryFromHeaderImmediatelyShared(S32& idx, Entry& entry);
	void writeEntryToHeaderImmediately(S32& idx, Entry& entry,
									   bool write_header = false);
	void removeEntry(S32 idx, Entry& entry, std::string& filename,
					 bool remove_file = true);
	void removeCachedTexture(const LLUUID& id);
	S32 getHeaderCacheEntry(const LLUUID& id, Entry& entry);
	S32 setHeaderCacheEntry(const LLUUID& id, Entry& entry, S32 imagesize,
							S32 datasize);
	void writeUpdatedEntries();
	void updatedHeaderEntriesFile();

protected:
	typedef std::unique_ptr<LL::ThreadPool> thread_pool_ptr_t;
	thread_pool_ptr_t	mThreadPoolp;

	LLMutex				mLRUMutex;
	LLMutex				mHeaderMutex;
	LLAtomicU32			mNumReads;
	LLAtomicU32			mNumWrites;

	LLFile*				mHeaderFile;

	typedef boost::unordered_flat_map<LLUUID, std::string> purge_map_t;
	purge_map_t			mFilesToDelete;
	LLTimer				mSlicedPurgeTimer;

	// Headers (each header entry includes the first mip)
	std::string			mHeaderEntriesFileName;
	std::string			mHeaderDataFileName;
	EntriesInfo			mHeaderEntriesInfo;
	// Indexes of the deleted entries; keep it as an ordered set so that the
	// list is auto-sorted !  HB
	std::set<S32>		mFreeList;
	uuid_list_t			mLRU;
	typedef boost::unordered_flat_map<LLUUID, S32> id_map_t;
	id_map_t			mHeaderIDMap;

	// Bodies (textures minus headers)
	std::string			mTexturesDirName;
	typedef boost::unordered_flat_map<LLUUID, S32> size_map_t;
	size_map_t			mTexturesSizeMap;
	S64					mTexturesSizeTotal;
	LLAtomicBool		mDoPurge;

	// Keep this as an ordered map !  HB
	typedef std::map<S32, Entry> idx_entry_map_t;
	idx_entry_map_t		mUpdatedEntryMap;

	bool				mReadOnly;

	static LLAtomicU32	sTotalHits;
	static LLAtomicU32	sTotalMisses;
	static LLAtomicU32	sTotalWrites;
	static LLAtomicU32	sTotalErrors;
	static U32			sCacheMaxEntries;
	static S64			sCacheMaxTexturesSize;
};

// Note: there is no good to define 1024 for TEXTURE_CACHE_ENTRY_SIZE while
// FIRST_PACKET_SIZE is 600 on sim side. The value is hard-coded here since
// this tree does not define FIRST_PACKET_SIZE (like the Cool VL Viewer does).
constexpr S32 TEXTURE_CACHE_ENTRY_SIZE = 600;
