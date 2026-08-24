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
    ImageRequest(const LLPointer<LLImageFormatted>& image,
                 S32 discard,
                 bool needs_aux,
                 bool allow_compression,
                 const LLPointer<LLImageDecodeThread::Responder>& responder,
                 U32 request_id,
                 const LLUUID& id);
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
    const LLUUID& id)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    U32 decode_id = ++mDecodeCount;
    if (decode_id == 0)
        decode_id = ++mDecodeCount;

    // Report backlog here (called on the main thread for every decode request) since
    // LLImageDecodeThread::update() is dead code - nothing in the viewer calls it.
    VayuImageBlockCompressor::setQueueBacklog(mThreadPool->getQueue().size());

    // Instantiate the ImageRequest right in the lambda, why not?
    bool posted = mThreadPool->getQueue().post(
        [req = ImageRequest(image, discard, needs_aux, allow_compression, responder, decode_id, id)]
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
                           const LLUUID& id)
    : mFormattedImage(image),
      mDiscardLevel(discard),
      mNeedsAux(needs_aux),
      mAllowCompression(allow_compression),
      mDecodedRaw(false),
      mDecodedAux(false),
      mResponder(responder),
      mRequestId(request_id),
      mID(id)
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
            mDecodedImageRaw = new LLImageRaw(mFormattedImage->getWidth(),
                                              mFormattedImage->getHeight(),
                                              mFormattedImage->getComponents());
        }
        done = mFormattedImage->decode(mDecodedImageRaw, decode_time_slice);
        // some decoders are removing data when task is complete and there were errors
        mDecodedRaw = done && mDecodedImageRaw->getData();

        // Pick up errors from decoding
        mErrorString = LLImage::getLastThreadError();
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

    if (done && mDecodedRaw && mDecodedImageRaw.notNull())
    {
        if (mAllowCompression &&
            VayuImageBlockCompressor::isEligible(mDecodedImageRaw->getWidth(),
                                               mDecodedImageRaw->getHeight(),
                                               mDecodedImageRaw->getComponents()))
        {
            // Only mFormattedImage->getDiscardLevel() (read *after* decode, above)
            // reflects what was actually decoded - the pre-decode desired/loaded
            // discard can legitimately differ. Cache lookups keyed on the wrong
            // value would silently never hit.
            const S32 discard = mFormattedImage->getDiscardLevel();
            // A texture streaming in progressively writes one cache entry per
            // discard level it passes through (they're distinct keys, not
            // overwrites) - accepted churn, not a bug: each entry is still
            // independently useful for a future load that only wants that
            // discard, e.g. a texture that stays small/distant on screen.
            //
            // mNeedsAux requests decode a channel the BC cache never stores;
            // leave them on the always-live-encode path entirely.
            const bool cacheable = !mNeedsAux && mID.notNull() && discard >= 0;
            bool cache_hit = false;

            if (cacheable)
            {
                VayuBCCacheEntryHeader cache_header;
                std::vector<U8> cache_buffer;
                const U8 min_preset = (U8)VayuImageBlockCompressor::getEffectivePreset();
                if (VayuBCTextureCache::instance().readEntry(mID, discard, min_preset, cache_header, cache_buffer))
                {
                    auto comp_res = std::make_shared<VayuBlockCompressionResult>();
                    comp_res->mFormat = (EVayuBlockCompressionFormat)cache_header.mFormat;
                    comp_res->mPreset = (EVayuBlockCompressionPreset)cache_header.mPreset;
                    comp_res->mGLInternalFormat = cache_header.mGLInternalFormat;
                    comp_res->mGLPrimaryFormat = cache_header.mGLPrimaryFormat;
                    comp_res->mWidth = cache_header.mWidth;
                    comp_res->mHeight = cache_header.mHeight;
                    comp_res->mMipLevels = cache_header.mMipLevels;
                    comp_res->mComponents = cache_header.mComponents;
                    comp_res->mBuffer = std::move(cache_buffer);
                    mDecodedImageRaw->setBlockCompressionResult(comp_res);
                    cache_hit = true;
                }
            }

            if (!cache_hit)
            {
                auto comp_res = std::make_shared<VayuBlockCompressionResult>();
                if (VayuImageBlockCompressor::encode(mDecodedImageRaw, *comp_res))
                {
                    mDecodedImageRaw->setBlockCompressionResult(comp_res);

                    if (cacheable)
                    {
                        VayuBCCacheEntryHeader cache_header;
                        cache_header.mFormat = (U8)comp_res->mFormat;
                        cache_header.mPreset = (U8)comp_res->mPreset;
                        cache_header.mMipLevels = comp_res->mMipLevels;
                        cache_header.mWidth = comp_res->mWidth;
                        cache_header.mHeight = comp_res->mHeight;
                        cache_header.mComponents = comp_res->mComponents;
                        cache_header.mGLInternalFormat = comp_res->mGLInternalFormat;
                        cache_header.mGLPrimaryFormat = comp_res->mGLPrimaryFormat;
                        // Aliasing shared_ptr: shares comp_res's refcount but
                        // points at its buffer member, so the cache can hold
                        // the encoded bytes alive for its background flush
                        // without copying them. comp_res is already owned by
                        // mDecodedImageRaw above; writeEntry() requires the
                        // buffer stay unmodified from here on, which holds -
                        // encode() has already finished filling it, and the
                        // only later reader is LLImageGL::createGLTexture(),
                        // which just reads empty()/data() off it. The
                        // mBuffer = std::move(...) above is the cache-hit
                        // path, which never reaches this branch.
                        VayuBCTextureCache::instance().writeEntry(
                            mID, discard, cache_header,
                            std::shared_ptr<const std::vector<U8>>(comp_res, &comp_res->mBuffer));
                    }
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
