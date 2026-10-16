/**
 * @file llimageworker.cpp
 * @brief Base class for images.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "linden_common.h"

#include "llimageworker.h"
#include "llimagedxt.h"
#include "vayuimageblockcompressor.h"
#include "vayubctexturecache.h"
#include "threadpool.h"

/*--------------------------------------------------------------------------*/
class ImageRequest
{
public:
    ImageRequest(const LLPointer<LLImageFormatted>&               image,
                 S32                                              discard,
                 bool                                             needs_aux,
                 bool                                             allow_compression,
                 const LLPointer<LLImageDecodeThread::Responder>& responder,
                 U32                                              request_id,
                 const LLUUID&                                    id,
                 EVayuTextureJob                                  job);
    virtual ~ImageRequest();

    /*virtual*/ bool processRequest();
    /*virtual*/ void finishRequest(bool completed);

private:
    // LLPointers stored in ImageRequest MUST be LLPointer instances rather
    // than references: we need to increment the refcount when storing these.
    // input
    LLPointer<LLImageFormatted> mFormattedImage;
    S32 mDiscardLevel;
    U32 mRequestId;
    bool mNeedsAux;
    bool mAllowCompression;
    LLUUID mID; // for the BC disk cache; may be null if the caller didn't pass one
    EVayuTextureJob mTextureJob;
    // output
    LLPointer<LLImageRaw> mDecodedImageRaw;
    LLPointer<LLImageRaw> mDecodedImageAux;
    bool mDecodedRaw;
    bool mDecodedAux;
    LLPointer<LLImageDecodeThread::Responder> mResponder;
    std::string mErrorString;};


//----------------------------------------------------------------------------

// MAIN THREAD
LLImageDecodeThread::LLImageDecodeThread(bool /*threaded*/)
    : mDecodeCount(0)
{
    mThreadPool = std::make_unique<LL::ThreadPool>("ImageDecode", 8);
    mThreadPool->start();
}

//virtual
LLImageDecodeThread::~LLImageDecodeThread()
{}

// MAIN THREAD
// virtual
size_t LLImageDecodeThread::update(F32 max_time_ms)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    return getPending();
}

size_t LLImageDecodeThread::getPending()
{
    return mThreadPool->getQueue().size();
}

LLImageDecodeThread::handle_t LLImageDecodeThread::decodeImage(
    const LLPointer<LLImageFormatted>& image,
    S32 discard,
    bool needs_aux,
    bool allow_compression,
    const LLPointer<LLImageDecodeThread::Responder>& responder,
    const LLUUID& id,
    EVayuTextureJob job)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    U32 decode_id = ++mDecodeCount;
    if (decode_id == 0)
        decode_id = ++mDecodeCount;

    // Instantiate the ImageRequest right in the lambda, why not?
    bool posted = mThreadPool->getQueue().post(
        [req = ImageRequest(image, discard, needs_aux, allow_compression, responder, decode_id, id, job)]
        () mutable
        {
            auto done = req.processRequest();
            req.finishRequest(done);
        });
    if (! posted)
    {
        LL_DEBUGS() << "Tried to start decoding on shutdown" << LL_ENDL;
        return 0;
    }

    return decode_id;
}

void LLImageDecodeThread::shutdown()
{
    mThreadPool->close();
}

LLImageDecodeThread::Responder::~Responder()
{
}

//----------------------------------------------------------------------------

ImageRequest::ImageRequest(const LLPointer<LLImageFormatted>& image,
                           S32 discard,
                           bool needs_aux,
                           bool allow_compression,
                           const LLPointer<LLImageDecodeThread::Responder>& responder,
                           U32 request_id,
                           const LLUUID& id,
                           EVayuTextureJob job)
    : mFormattedImage(image),
      mDiscardLevel(discard),
      mNeedsAux(needs_aux),
      mAllowCompression(allow_compression),
      mDecodedRaw(false),
      mDecodedAux(false),
      mResponder(responder),
      mRequestId(request_id),
      mID(id),
      mTextureJob(job)
{
}

ImageRequest::~ImageRequest()
{
    mDecodedImageRaw = NULL;
    mDecodedImageAux = NULL;
    mFormattedImage = NULL;
}

//----------------------------------------------------------------------------


// Returns true when done, whether or not decode was successful.
bool ImageRequest::processRequest()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    if (mFormattedImage.isNull())
        return true;

    const F32 decode_time_slice = 0.f; //disable time slicing
    bool done = true;

    LLImageDataLock lockFormatted(mFormattedImage);
    LLImageDataLock lockDecodedRaw(mDecodedImageRaw);
    LLImageDataLock lockDecodedAux(mDecodedImageAux);

    if (!mDecodedRaw)
    {
        // Decode primary channels
        if (mDecodedImageRaw.isNull())
        {
            // parse formatted header
            if (!mFormattedImage->updateData())
            {
                return true; // done (failed)
            }
            if ((mFormattedImage->getWidth() * mFormattedImage->getHeight() * mFormattedImage->getComponents()) == 0)
            {
                return true; // done (failed)
            }
            if (mDiscardLevel >= 0)
            {
                mFormattedImage->setDiscardLevel(mDiscardLevel);
            }
        }

        const S32 discard = (mDiscardLevel >= 0) ? mDiscardLevel : (S32)mFormattedImage->getDiscardLevel();
        // mAllowCompression is only ever set for a resolved role (see the decision point in
        // LLViewerFetchedTexture), so there is no unclaimed state to exclude here: a stale
        // colour/normal entry can never be resurrected for an uncompressed raw bind.
        const bool cacheable = !mNeedsAux && mID.notNull() && discard >= 0 && mAllowCompression;
        bool cache_hit = false;

        // Check VayuBCTextureCache *before* running expensive CPU OpenJPEG decode.
        // On a hit, we attach the cached compressed blocks and bypass software decode entirely.
        if (cacheable)
        {
            VayuBCCacheEntryHeader cache_header;
            std::vector<U8> cache_buffer;
            bool cache_hit_vbc = false;
            if (mTextureJob == EVayuTextureJob::PBRNormal)
            {
                if (VayuBCTextureCache::instance().readEntry(mID, discard, cache_header, cache_buffer, ".bc5", VayuBCTextureCache::kFormatBC5, VayuBCTextureCache::kRoleNormal))
                {
                    cache_hit_vbc = true;
                }
            }
            else if (mTextureJob == EVayuTextureJob::LegacyMaterialNormal)
            {
                // Legacy Blinn-Phong normal map: BC7 (fallback BC3). The cache
                // name carries format + role, so a colour .bc7 entry can never
                // satisfy this role.
                if (VayuBCTextureCache::instance().readEntry(mID, discard, cache_header, cache_buffer, ".bc7", VayuBCTextureCache::kFormatBC7, VayuBCTextureCache::kRoleNormal) ||
                    VayuBCTextureCache::instance().readEntry(mID, discard, cache_header, cache_buffer, ".bc3", VayuBCTextureCache::kFormatBC3, VayuBCTextureCache::kRoleNormal))
                {
                    cache_hit_vbc = true;
                }
            }
            else if (mTextureJob == EVayuTextureJob::LegacySpecular)
            {
                // Legacy Blinn-Phong specular map: BC7 (fallback BC3), role colour.
                // RGB tint + alpha gloss must survive, so only the 4-channel
                // formats qualify.
                if (VayuBCTextureCache::instance().readEntry(mID, discard, cache_header, cache_buffer, ".bc7", VayuBCTextureCache::kFormatBC7, VayuBCTextureCache::kRoleColor) ||
                    VayuBCTextureCache::instance().readEntry(mID, discard, cache_header, cache_buffer, ".bc3", VayuBCTextureCache::kFormatBC3, VayuBCTextureCache::kRoleColor))
                {
                    cache_hit_vbc = true;
                }
            }
            else
            {
                if (VayuBCTextureCache::instance().readEntry(mID, discard, cache_header, cache_buffer, ".bc"))
                {
                    cache_hit_vbc = true;
                }
            }

            if (cache_hit_vbc)
            {
                auto comp_res = std::make_shared<VayuBlockCompressionResult>();
                comp_res->mFormat = (EVayuBlockCompressionFormat)cache_header.mFormat;
                comp_res->mPreset = (EVayuBlockCompressionPreset)cache_header.mPreset;
                comp_res->mIsMask = (cache_header.mIsMask != 0);
                comp_res->mGLInternalFormat = cache_header.mGLInternalFormat;
                comp_res->mGLPrimaryFormat = cache_header.mGLPrimaryFormat;
                comp_res->mWidth = cache_header.mWidth;
                comp_res->mHeight = cache_header.mHeight;
                comp_res->mMipLevels = cache_header.mMipLevels;
                comp_res->mComponents = cache_header.mComponents;
                comp_res->mBuffer = std::move(cache_buffer);

                if (mDecodedImageRaw.isNull())
                {
                    mDecodedImageRaw = new LLImageRaw(cache_header.mWidth,
                                                      cache_header.mHeight,
                                                      cache_header.mComponents);
                }
                mDecodedImageRaw->setTextureJob(mTextureJob);
                mDecodedImageRaw->setBlockCompressionResult(comp_res);
                mFormattedImage->setDiscardLevel(discard);
                mDecodedRaw = true;
                done = true;
                cache_hit = true;
            }
        }

        if (!cache_hit)
        {
            if (mDecodedImageRaw.isNull())
            {
                mDecodedImageRaw = new LLImageRaw(mFormattedImage->getWidth(),
                                                  mFormattedImage->getHeight(),
                                                  mFormattedImage->getComponents());
            }
            mDecodedImageRaw->setTextureJob(mTextureJob);
            done = mFormattedImage->decode(mDecodedImageRaw, decode_time_slice);
            // some decoders are removing data when task is complete and there were errors
            mDecodedRaw = done && mDecodedImageRaw->getData();

            // Pick up errors from decoding
            mErrorString = LLImage::getLastThreadError();
        }
    }
    if (done && mNeedsAux && !mDecodedAux && mFormattedImage.notNull())
    {
        // Decode aux channel
        if (!mDecodedImageAux)
        {
            mDecodedImageAux = new LLImageRaw(mFormattedImage->getWidth(),
                                              mFormattedImage->getHeight(),
                                              1);
        }
        done = mFormattedImage->decodeChannels(mDecodedImageAux, decode_time_slice, 4, 4);
        mDecodedAux = done && mDecodedImageAux->getData();

        // Pick up errors from decoding
        mErrorString = LLImage::getLastThreadError();
    }

    if (done && mDecodedRaw && mDecodedImageRaw.notNull() && !mDecodedImageRaw->getBlockCompressionResult())
    {
        // mAllowCompression is the whole gate. It is decided once per request, in
        // LLViewerFetchedTexture, and already folds in the category exclusions and the
        // requirement that a role exists -- so an unclaimed texture, a placeholder or an
        // excluded category all arrive here with it false and stay raw.
        if (mAllowCompression &&
            VayuImageBlockCompressor::isEligible(mDecodedImageRaw->getWidth(),
                                               mDecodedImageRaw->getHeight(),
                                               mDecodedImageRaw->getComponents()))
        {
            const S32 discard = (mDiscardLevel >= 0) ? mDiscardLevel : (S32)mFormattedImage->getDiscardLevel();
            const bool cacheable = !mNeedsAux && mID.notNull() && discard >= 0;

            // Resolve the cache key (format, role) before encoding so we can check
            // for an existing entry and avoid re-encoding mips already on disk.
            const U8 cache_role   = (mTextureJob == EVayuTextureJob::PBRNormal || mTextureJob == EVayuTextureJob::LegacyMaterialNormal)
                                        ? VayuBCTextureCache::kRoleNormal
                                        : VayuBCTextureCache::kRoleColor;
            const U8 cache_format = (U8)VayuImageBlockCompressor::resolveFormat(mDecodedImageRaw->getData(),
                                                                                mDecodedImageRaw->getWidth(),
                                                                                mDecodedImageRaw->getHeight(),
                                                                                mDecodedImageRaw->getComponents(),
                                                                                mTextureJob,
                                                                                EVayuBlockCompressionFormat::Auto);

            // Check for an existing cache entry to determine incremental encode strategy.
            // resolveFormat() above is the same code path encode() uses, so a hit here
            // means the on-disk extension genuinely matches what this decode will produce.
            VayuBCCacheEntryHeader existing_header;
            const bool             has_existing =
                cacheable && VayuBCTextureCache::instance().readEntryHeader(mID, cache_format, cache_role, existing_header);

            // The entry already holds this level and every finer one. The read path above
            // normally serves that from cache before we get here; reaching it means the
            // probe disagreed with the real reader, so the GL upload still needs a result.
            // This encodes the FULL chain -- deliberately not max_mips=1, see the note on
            // max_mips below.
            const bool already_covered = has_existing && existing_header.mDiscardLevel <= discard;

            // The whole chain, always, and no splice. A coarser entry's tail was
            // built by downsampling a decode of w>>D_e -- for a texture first seen
            // on a small distant object that is 64x64 for a 2048x2048 texture --
            // so handing its levels to a refined pyramid spreads 64x64's worth of
            // detail across the levels that should hold 256x256 and 128x128.
            // Sizes and order stay correct, so nothing complains: the fine levels
            // are real and the coarse ones are flat, which renders as correct up
            // close and grey at distance. The decode in hand is finer than the
            // donor at every level below this one, so regenerating them is both
            // correct and cheaper than re-fetching ever was.
            //
            // LLImageGL::createGLTexture hands GL mBuffer.data() +
            // getLargestMipOffset() with data_hasmips=true, and setImage() derives
            // its level count from the DIMENSIONS via calcMipLevelCount() (always
            // down to 1x1) and walks the buffer backwards by that many levels. It
            // never consults mMipLevels, so a truncated buffer makes GL read past
            // the front of the allocation -- which is why this encodes whole.
            S32 max_mips = 0;

            auto gap_res = std::make_shared<VayuBlockCompressionResult>();
            if (VayuImageBlockCompressor::encode(mDecodedImageRaw, *gap_res, EVayuBlockCompressionFormat::Auto, mTextureJob, max_mips))
            {
                auto comp_res = gap_res;

                // If encode() failed there is nothing to upload; the image stays raw and
                // createGLTexture's uncompressed path handles it.

                mDecodedImageRaw->setBlockCompressionResult(comp_res);
                if (cacheable && !already_covered)
                {
                    VayuBCCacheEntryHeader cache_header;
                    cache_header.mFormat           = (U8)gap_res->mFormat;
                    cache_header.mPreset           = (U8)gap_res->mPreset;
                    cache_header.mIsMask           = gap_res->mIsMask ? 1 : 0;
                    cache_header.mRole             = cache_role;
                    cache_header.mDiscardLevel = (U8)discard;
                    cache_header.mMipLevels        = gap_res->mMipLevels;
                    cache_header.mWidth            = gap_res->mWidth;
                    cache_header.mHeight           = gap_res->mHeight;
                    cache_header.mComponents       = gap_res->mComponents;
                    cache_header.mGLInternalFormat = gap_res->mGLInternalFormat;
                    cache_header.mGLPrimaryFormat  = gap_res->mGLPrimaryFormat;

                    // Full chain every time. writeEntrySync's finer-wins guard lets this
                    // replace a coarser entry, which is exactly the refinement case, so a
                    // refined texture's own pyramid is what survives rather than a mix of
                    // its fine levels and the donor's.
                    VayuBCTextureCache::instance().writeEntry(mID, discard, cache_header,
                                                              std::shared_ptr<const std::vector<U8>>(gap_res, &gap_res->mBuffer));
                }
            }
        }
    }

    return done;
}

void ImageRequest::finishRequest(bool completed)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (mResponder.notNull())
    {
        bool success = completed && mDecodedRaw && (!mNeedsAux || mDecodedAux);
        mResponder->completed(success, mErrorString, mDecodedImageRaw, mDecodedImageAux, mRequestId);
    }
    // Will automatically be deleted
}
