# -*- cmake -*-
include_guard()

# rpmalloc has no vcpkg port; it's vendored under indra/externals/rpmalloc/
# (see that directory's README entry) pinned to upstream release 2.0.1.
#
# Unlike Firestorm's jemalloc, which ships a prebuilt .so and LD_PRELOADs it
# at runtime (Linux/macOS only, since LD_PRELOAD has no Windows equivalent),
# rpmalloc is linked directly into the binary: ENABLE_OVERRIDE makes
# rpmalloc.c define the standard malloc/free/realloc/calloc family with
# external linkage, so the linker resolves the CRT's own callers to rpmalloc
# instead. This is rpmalloc's primary supported mode and works identically
# on every platform we build for.
if (USE_RPMALLOC)
  add_library(rpmalloc STATIC ${INDRA_SOURCE_DIR}/externals/rpmalloc/rpmalloc/rpmalloc.c)
  target_include_directories(rpmalloc SYSTEM PUBLIC ${INDRA_SOURCE_DIR}/externals/rpmalloc/rpmalloc)
  target_compile_definitions(rpmalloc PRIVATE ENABLE_OVERRIDE=1)

  # llcommon.cpp defines its own operator new/delete when Tracy profiling is
  # compiled in (LL_PROFILER_CONFIGURATION >= LL_PROFILER_CONFIG_TRACY), to
  # hook LL_PROFILE_ALLOC/FREE -- it still calls plain malloc/free
  # internally, so those allocations already end up in rpmalloc regardless.
  # Without this, rpmalloc.c's own operator new/delete (see malloc.c's local
  # patch) collide with llcommon's at link time in any Tracy-enabled build.
  # See RPMALLOC_NO_CXX_OPERATOR_OVERRIDE's comment in that vendored file.
  target_compile_definitions(rpmalloc PRIVATE RPMALLOC_NO_CXX_OPERATOR_OVERRIDE=1)

  add_library(ll::rpmalloc ALIAS rpmalloc)
endif ()
