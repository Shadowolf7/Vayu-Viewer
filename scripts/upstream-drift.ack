# Paths whose divergence from upstream Alchemy is deliberate.
#
# scripts/upstream-drift.py reports every path in both trees where upstream
# changed a file after we last touched ours. That is the shape of a merge that
# silently kept our side: git sees "ours differs from the base, theirs equals
# it" and takes ours, so an upstream rewrite lands as a non-event. Most such a
# path is drift to realign; a path listed here is meant to differ, and the
# reason has to be one someone can still read in six months.
#
# path  reason

indra/newview/lltexturecache.h  Vayu's CoolVL-style texture cache: no threaded ctor, no start(). Upstream's (threaded) LLTextureCache constructor was declined at c8244a653f; the implementation sources did not change in that merge range.
