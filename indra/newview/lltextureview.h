/**
 * @file lltextureview.h
 * @brief LLTextureView class header file
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

#ifndef LL_LLTEXTUREVIEW_H
#define LL_LLTEXTUREVIEW_H

#include "llcontainerview.h"

class LLViewerFetchedTexture;
class LLTextureBar;
class LLGLTexMemBar;
class LLAvatarTexBar;
class LLTextureStatusBar;

class LLTextureView final : public LLContainerView
{
public:
    AL_VIEW_TYPE(LLTextureView, LLContainerView);
private:
    friend class LLTextureBar;
    friend class LLGLTexMemBar;
    friend class LLAvatarTexBar;
    friend class LLTextureStatusBar;

protected:
    LLTextureView(const Params&);
    friend class LLUICtrlFactory;
public:
    ~LLTextureView();

    void draw() override;
    void setVisible(bool visible) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleScrollWheel(S32 x, S32 y, LLScrollDelta delta) override;
    bool handleKey(KEY key, MASK mask, bool called_from_parent) override;
    bool hasFocus() const override;

    static void addDebugImage(LLViewerFetchedTexture* image) { sDebugImages.insert(image); }
    static void removeDebugImage(LLViewerFetchedTexture* image) { sDebugImages.insert(image); }
    static void clearDebugImages() { sDebugImages.clear(); }

private:
    bool addBar(LLViewerFetchedTexture *image, S32 hilight = 0);

    // Whether image survives the current UUID substring filter. Empty filter
    // matches everything, so an unfiltered view is the same list as before.
    bool matchesFilter(const LLViewerFetchedTexture* image) const;

    // Rows that fit under the two child bars. The view's own rect comes from
    // LLDebugView::init's r.set(150, height-60, 835, 110), which -- LLRect's
    // four-argument set being (left, top, right, bottom) -- leaves it 685 wide
    // and (window height - 170) tall, so this is tens of rows, not six.
    S32 rowsPerPage() const;

    void scrollBy(S32 rows);
    void setFirstRow(S32 row);

    // Children laid out above the rows: LLGLTexMemBar reserves eight lines for
    // its status block and the column headers, LLTextureStatusBar one for the
    // filter and page indicator.
    static constexpr S32 MEM_BAR_LINES = 8;
    static constexpr S32 STATUS_LINES  = 1;
    // LLContainerView::arrange() reserves this between every child, so it is part
    // of what a row costs, not decoration.
    static constexpr S32 CHILD_GAP = 2;

private:
    bool mFreezeView;
    bool mOrderFetch;
    bool mPrintList;

    std::string mFilter;   // UUID substring; empty means no filter
    S32         mFirstRow; // first displayed row of the filtered list
    S32         mRowCount; // rows the filter matched, whole list not just a page

    std::vector<LLTextureBar*> mTextureBars;
    U32 mNumTextureBars;

    LLGLTexMemBar* mGLTexMemBar;
    LLAvatarTexBar* mAvatarTexBar;
    LLTextureStatusBar* mStatusBar;

public:
    static std::set<LLViewerFetchedTexture*> sDebugImages;
};

extern LLTextureView *gTextureView;
#endif // LL_TEXTURE_VIEW_H
