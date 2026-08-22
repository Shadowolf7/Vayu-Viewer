#!/usr/bin/env python3
"""Compute XUI child-widget rects from top/top_pad/top_delta/left_pad chains.

Usage: xui_layout_calc.py <panel.xml> [usable_height]

Mirrors LLView::initFromParams's topleft-layout defaulting (indra/llui/llview.cpp,
~line 2538 on): when a widget gives no top/top_pad/top_delta AND continues a row
via left_pad or left_delta, it inherits the previous sibling's exact top AND
bottom (its own declared height is not used for vertical positioning at all) -
it does NOT stack below. Only a widget with no positioning attributes
whatsoever drops to the next row, offset by VPAD (4px, lluiconstants.h).
Verified against the engine source, not just observed behavior - see
"XUI Layout Engine and Floater Geometry" (Obsidian, Vayu Viewer/) for the
worked trace if this ever needs re-deriving.
"""
import sys
import xml.etree.ElementTree as ET

VPAD = 4


def layout(root):
    rows = []
    prev_top, prev_bottom = 0, 0
    prev_left, prev_right = 0, 0
    for child in root:
        attrs = child.attrib
        height = int(attrs.get("height", 0))
        width = int(attrs.get("width", 0))
        continues_row = "left_pad" in attrs or "left_delta" in attrs

        if "top" in attrs:
            top = int(attrs["top"])
            bottom = top + height
        elif "top_pad" in attrs:
            top = prev_bottom + int(attrs["top_pad"])
            bottom = top + height
        elif "top_delta" in attrs:
            top = prev_top + int(attrs["top_delta"])
            bottom = top + height
        elif continues_row:
            # No top-positioning attribute, but left_pad/left_delta says "stay
            # on this row": inherits the previous sibling's rect vertically,
            # own height is ignored for positioning purposes.
            top = prev_top
            bottom = prev_bottom
        else:
            # No positioning attributes at all: drop to a new row.
            top = prev_bottom + VPAD
            bottom = top + height

        if "left" in attrs:
            left = int(attrs["left"])
        elif "left_pad" in attrs:
            left = prev_right + int(attrs["left_pad"])
        elif "left_delta" in attrs:
            left = prev_left + int(attrs["left_delta"])
        else:
            left = prev_left
        right = left + width

        rows.append((attrs.get("name", child.tag), child.tag, top, bottom, left, right))
        prev_top, prev_bottom, prev_left, prev_right = top, bottom, left, right
    return rows


DEFAULT_FLOATER_HEADER_HEIGHT = 25  # widgets/floater.xml default header_height


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    tree = ET.parse(sys.argv[1])
    root = tree.getroot()
    rows = layout(root)

    print(f"{'name':30} {'tag':20} {'top':>6} {'bottom':>6} {'left':>6} {'right':>6}")
    max_bottom = 0
    for name, tag, top, bottom, left, right in rows:
        print(f"{name[:30]:30} {tag[:20]:20} {top:6} {bottom:6} {left:6} {right:6}")
        max_bottom = max(max_bottom, bottom)

    print(f"\ncontent bottom: {max_bottom}px")

    # A <floater> root reserves header_height (default 25px, widgets/floater.xml)
    # off the top for its title bar - that's NOT available to children, even
    # though it's included in the floater's own declared height. A bare
    # <panel> (e.g. one tab of a tab container) has no title bar of its own,
    # so its full declared height is usable.
    declared_height = root.attrib.get("height")
    if root.tag == "floater" and declared_height is not None:
        header_height = int(root.attrib.get("header_height", DEFAULT_FLOATER_HEADER_HEIGHT))
        usable = int(declared_height) - header_height
        print(f"floater height={declared_height}px - header_height={header_height}px -> usable {usable}px")
    elif declared_height is not None:
        usable = int(declared_height)
        print(f"panel height={declared_height}px -> usable {usable}px")
    else:
        usable = None

    if len(sys.argv) > 2:
        usable = int(sys.argv[2])
        print(f"(overriding with explicit usable height argument: {usable}px)")

    if usable is not None:
        overflow = max_bottom - usable
        verdict = f"OVERFLOWS by {overflow}px" if overflow > 0 else f"fits, {-overflow}px to spare"
        print(f"usable height:  {usable}px -> {verdict}")


if __name__ == "__main__":
    main()
