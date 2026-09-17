/**
 * @file llimageworker_test.cpp
 * @author Merov Linden
 * @date 2009-04-28
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

// Precompiled header: almost always required for newview cpp files
#include "linden_common.h"
// Class to test
#include "../llimageworker.h"
#include "../vayubctexturecache.h"
#include "../vayuimageblockcompressor.h"
// For timer class
#include "../llcommon/lltimer.h"
// for lltrace class
#include "../llcommon/lltrace.h"
// Tut header
#include "../test/lltut.h"
#include <filesystem>

// -------------------------------------------------------------------------------------------
// Stubbing: Declarations required to link and run the class being tested
// Notes:
// * Add here stubbed implementation of the few classes and methods used in the class to be tested
// * Add as little as possible (let the link errors guide you)
// * Do not make any assumption as to how those classes or methods work (i.e. don't copy/paste code)
// * A simulator for a class can be implemented here. Please comment and document thoroughly.

LLImageBase::LLImageBase()
: mData(NULL),
mDataSize(0),
mWidth(0),
mHeight(0),
mComponents(0),
mBadBufferAllocation(false),
mAllowOverSize(false)
{
}
LLImageBase::~LLImageBase()
{
    deleteData();
}
void LLImageBase::dump() { }
void LLImageBase::sanityCheck() { }
void LLImageBase::deleteData()
{
    delete[] mData;
    mData = NULL;
    mDataSize = 0;
}
U8* LLImageBase::allocateData(S32 size)
{
    delete[] mData;
    if (size > 0)
    {
        mData = new U8[size];
        mDataSize = size;
        memset(mData, 0, size);
    }
    else
    {
        mData = NULL;
        mDataSize = 0;
    }
    return mData;
}
U8* LLImageBase::reallocateData(S32 size) { return allocateData(size); }
void LLImageBase::setSize(S32 width, S32 height, S32 ncomponents)
{
    mWidth = width;
    mHeight = height;
    mComponents = ncomponents;
}

LLImageFormatted::LLImageFormatted(S8 codec)
    : LLImageBase(),
      mCodec(codec),
      mDecoding(0),
      mDecoded(0),
      mDiscardLevel(-1),
      mLevels(0)
{
}
LLImageFormatted::~LLImageFormatted(){}
void LLImageFormatted::dump() { }
void LLImageFormatted::sanityCheck() { }
void LLImageFormatted::deleteData() { }
U8* LLImageFormatted::allocateData(S32 size) { return NULL; }
U8* LLImageFormatted::reallocateData(S32 size) { return NULL; }
void LLImageFormatted::resetLastError() { }
void LLImageFormatted::setLastError(const std::string&, const std::string&) { }
S32 LLImageFormatted::calcDataSize(S32 discard_level) { return 0; }
S32 LLImageFormatted::calcDiscardLevelBytes(S32 bytes) { return 0; }
bool LLImageFormatted::decodeChannels(LLImageRaw* raw_image,F32  decode_time, S32 first_channel, S32 max_channel) { return false; }
S8 LLImageFormatted::getCodec() const { return mCodec; }

LLImageRaw::LLImageRaw()
    : LLImageBase()
{
}
LLImageRaw::LLImageRaw(U16 width, U16 height, S8 components)
    : LLImageBase()
{
    setSize(width, height, components);
    if (width > 0 && height > 0 && components > 0)
    {
        allocateData(width * height * components);
    }
}
LLImageRaw::~LLImageRaw() { }
void LLImageRaw::deleteData() { LLImageBase::deleteData(); }
U8* LLImageRaw::allocateData(S32 size) { return LLImageBase::allocateData(size); }
U8* LLImageRaw::reallocateData(S32 size) { return LLImageBase::reallocateData(size); }
const U8* LLImageBase::getData() const { return mData; }
U8* LLImageBase::getData() { return mData; }
bool LLImageBase::isBufferInvalid() const { return false; }
const std::string& LLImage::getLastThreadError() { static std::string msg; return msg; }

// End Stubbing
// -------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------
// TUT
// -------------------------------------------------------------------------------------------

namespace tut
{
    // Test wrapper declarations

    // Note: We derive the responder class for 2 reasons:
    // 1. It's a pure virtual class and we can't compile without completed() being implemented
    // 2. We actually need a responder to test that the thread work test completed
    // We implement this making no assumption on what's done in the thread or worker
    // though, just that the responder's completed() method is called in the end.
    // Note on responders: responders are ref counted and *will* be deleted by the request they are
    // attached to when the queued request is deleted. The recommended way of using them is to
    // create them when creating a request, put a callback method in completed() and not rely on
    // anything to survive in the responder object once completed() has been called. Let the request
    // do the deletion and clean up itself.
    class responder_test : public LLImageDecodeThread::Responder
    {
        public:
            responder_test(bool* res, bool* success_out = nullptr, LLPointer<LLImageRaw>* raw_out = nullptr)
            {
                done = res;
                *done = false;
                mSuccessOut = success_out;
                mRawOut = raw_out;
                if (mSuccessOut) *mSuccessOut = false;
            }
            virtual void completed(bool success, const std::string& error_message, LLImageRaw* raw, LLImageRaw* aux, U32 request_id)
            {
                if (mSuccessOut) *mSuccessOut = success;
                if (mRawOut && raw) *mRawOut = raw;
                *done = true;
            }
        private:
            bool* done;
            bool* mSuccessOut;
            LLPointer<LLImageRaw>* mRawOut;
    };

    class LLImageFormattedMock : public LLImageFormatted
    {
    public:
        LLImageFormattedMock(U16 width = 16, U16 height = 16, S8 components = 4)
            : LLImageFormatted(IMG_CODEC_J2C)
        {
            setSize(width, height, components);
            mLevels = 4;
        }

        std::string getExtension() override { return "j2c"; }

        bool updateData() override
        {
            return true;
        }

        bool decode(LLImageRaw* raw_image, F32 decode_time) override
        {
            if (!raw_image) return false;
            if (!raw_image->getData())
            {
                raw_image->allocateData(getWidth() * getHeight() * getComponents());
            }
            U8* data = raw_image->getData();
            if (data)
            {
                for (size_t i = 0; i < (size_t)(getWidth() * getHeight()); ++i)
                {
                    data[i * 4 + 0] = 200;
                    data[i * 4 + 1] = 100;
                    data[i * 4 + 2] = 50;
                    data[i * 4 + 3] = 255;
                }
            }
            return true;
        }

        bool encode(const LLImageRaw* raw_image, F32 encode_time) override
        {
            return true;
        }
    };

    // Test wrapper declaration : decode thread
    struct imagedecodethread_test
    {
        // Instance to be tested
        LLImageDecodeThread* mThread;
        // Constructor and destructor of the test wrapper
        imagedecodethread_test()
        {
            mThread = NULL;
        }
        ~imagedecodethread_test()
        {
            delete mThread;
        }
    };

    // Tut templating thingamagic: test group, object and test instance
    typedef test_group<imagedecodethread_test> imagedecodethread_t;
    typedef imagedecodethread_t::object imagedecodethread_object_t;
    tut::imagedecodethread_t tut_imagedecodethread("LLImageDecodeThread");

    // ---------------------------------------------------------------------------------------
    // Test functions
    // Notes:
    // * Test as many as you possibly can without requiring a full blown simulation of everything
    // * The tests are executed in sequence so the test instance state may change between calls
    // * Remember that you cannot test private methods with tut
    // ---------------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------------
    // Test the LLImageDecodeThread interface
    // ---------------------------------------------------------------------------------------

    template<> template<>
    void imagedecodethread_object_t::test<1>()
    {
        // Test a *threaded* instance of the class
        mThread = new LLImageDecodeThread(true);
        ensure("LLImageDecodeThread: threaded constructor failed", mThread != NULL);
        // Insert something in the queue
        bool done = false;
        LLImageDecodeThread::handle_t decodeHandle = mThread->decodeImage(NULL, 0, false, true, new responder_test(&done));
        // Verifies we get back a valid handle
        ensure("LLImageDecodeThread:  threaded decodeImage(), returned handle is null", decodeHandle != 0);
        // Wait till the thread has time to handle the work order (though it doesn't do much per work order...)
        const U32 INCREMENT_TIME = 500;             // 500 milliseconds
        const U32 MAX_TIME = 20 * INCREMENT_TIME;   // Do the loop 20 times max, i.e. wait 10 seconds but no more
        U32 total_time = 0;
        while ((done == false) && (total_time < MAX_TIME))
        {
            ms_sleep(INCREMENT_TIME);
            total_time += INCREMENT_TIME;
        }
        // Verifies that the responder has now been called
        ensure("LLImageDecodeThread: threaded work unit not processed", done == true);
    }

    template<> template<>
    void imagedecodethread_object_t::test<2>()
    {
        // Test coarse decode (discard > 0) attaches compressed blocks in RAM and writes entry to cache
        auto test_dir = std::filesystem::temp_directory_path() / "vayu_llimageworker_test_coarse";
        std::error_code ec;
        std::filesystem::remove_all(test_dir, ec);
        std::filesystem::create_directories(test_dir, ec);

        VayuImageBlockCompressor::init();
        VayuBCTextureCache::instance().initCache(test_dir, 1024 * 1024);

        mThread = new LLImageDecodeThread(true);
        ensure("LLImageDecodeThread: constructor succeeded", mThread != NULL);

        LLUUID test_id;
        test_id.generate();

        LLPointer<LLImageFormattedMock> mock_image = new LLImageFormattedMock(16, 16, 4);
        bool done = false;
        bool success = false;
        LLPointer<LLImageRaw> decoded_raw;

        // Discard level 2, allow compression = true, pass test_id
        LLImageDecodeThread::handle_t handle = mThread->decodeImage(
            mock_image, 2, false, true, new responder_test(&done, &success, &decoded_raw), test_id);
        ensure("Valid handle for coarse decode", handle != 0);

        const U32 INCREMENT_TIME = 50;
        const U32 MAX_TIME = 100 * INCREMENT_TIME;
        U32 total_time = 0;
        while (!done && total_time < MAX_TIME)
        {
            ms_sleep(INCREMENT_TIME);
            total_time += INCREMENT_TIME;
        }

        ensure("Coarse decode work unit completed", done == true);
        ensure("Decode succeeded", success == true);
        ensure("Raw image returned to responder", decoded_raw.notNull());
        ensure("Coarse decode attaches compressed blocks in RAM", decoded_raw->getBlockCompressionResult() != nullptr);
        ensure_equals("Discard level in RAM block width matches", decoded_raw->getBlockCompressionResult()->mWidth, 16u);

        // Allow cache writer thread to flush pending write
        VayuBCTextureCache::instance().shutdown();

        // Verify that valid discard level was passed to cache write
        VayuBCCacheEntryHeader cache_header;
        std::vector<U8> cache_buffer;
        bool cache_hit = VayuBCTextureCache::instance().readEntry(test_id, 2, cache_header, cache_buffer);
        ensure("Cache entry recorded with valid discard level", cache_hit);
        ensure_equals("Cache entry header discard matches", (int)cache_header.mDiscardLevel, 2);

        VayuBCTextureCache::instance().clear();
        std::filesystem::remove_all(test_dir, ec);
    }
}
