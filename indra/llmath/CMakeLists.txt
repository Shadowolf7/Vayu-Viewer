# -*- cmake -*-

add_library(llmath STATIC)

# ---------------------------------------------------------------------------
# Vayu ISPC kernels — SSE2 / SSE4 / AVX / AVX2, runtime dispatch
# Follows the same pattern as llimage/CMakeLists.txt (bc7e.ispc).
# ---------------------------------------------------------------------------
find_program(ISPC_EXECUTABLE ispc DOC "Intel Implicit SPMD Program Compiler")
if (ISPC_EXECUTABLE)
  set(ISPC_TARGETS "sse2-i32x4,sse4-i32x4,avx1-i32x8,avx2-i32x8")

  # 1. Frustum & Sphere culling kernel
  set(FRUSTUM_CULL_ISPC_OUTPUTS
      ${CMAKE_CURRENT_BINARY_DIR}/vayu_frustum_cull.o
      ${CMAKE_CURRENT_BINARY_DIR}/vayu_frustum_cull_sse2.o
      ${CMAKE_CURRENT_BINARY_DIR}/vayu_frustum_cull_sse4.o
      ${CMAKE_CURRENT_BINARY_DIR}/vayu_frustum_cull_avx.o
      ${CMAKE_CURRENT_BINARY_DIR}/vayu_frustum_cull_avx2.o
  )
  add_custom_command(
      OUTPUT  ${FRUSTUM_CULL_ISPC_OUTPUTS}
              ${CMAKE_CURRENT_SOURCE_DIR}/vayu_frustum_cull_ispc.h
      COMMAND ${ISPC_EXECUTABLE}
              -O3 --pic --opt=fast-math
              --target=${ISPC_TARGETS}
              -h ${CMAKE_CURRENT_SOURCE_DIR}/vayu_frustum_cull_ispc.h
              -o ${CMAKE_CURRENT_BINARY_DIR}/vayu_frustum_cull.o
              ${CMAKE_CURRENT_SOURCE_DIR}/vayu_frustum_cull.ispc
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/vayu_frustum_cull.ispc
      COMMENT "Compiling vayu_frustum_cull.ispc (SSE2, SSE4, AVX, AVX2)"
  )
  target_compile_definitions(llmath PUBLIC HAVE_FRUSTUM_CULL_ISPC)

  # 2. Skinning ISPC kernel
  set(SKINNING_ISPC_OUTPUTS
      ${CMAKE_CURRENT_BINARY_DIR}/vayu_skinning.o
      ${CMAKE_CURRENT_BINARY_DIR}/vayu_skinning_sse2.o
      ${CMAKE_CURRENT_BINARY_DIR}/vayu_skinning_sse4.o
      ${CMAKE_CURRENT_BINARY_DIR}/vayu_skinning_avx.o
      ${CMAKE_CURRENT_BINARY_DIR}/vayu_skinning_avx2.o
  )
  add_custom_command(
      OUTPUT  ${SKINNING_ISPC_OUTPUTS}
              ${CMAKE_CURRENT_SOURCE_DIR}/vayu_skinning_ispc.h
      COMMAND ${ISPC_EXECUTABLE}
              -O3 --pic --opt=fast-math
              --target=${ISPC_TARGETS}
              -h ${CMAKE_CURRENT_SOURCE_DIR}/vayu_skinning_ispc.h
              -o ${CMAKE_CURRENT_BINARY_DIR}/vayu_skinning.o
              ${CMAKE_CURRENT_SOURCE_DIR}/vayu_skinning.ispc
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/vayu_skinning.ispc
      COMMENT "Compiling vayu_skinning.ispc (SSE2, SSE4, AVX, AVX2)"
  )
  target_compile_definitions(llmath PUBLIC HAVE_SKINNING_ISPC)

else ()
  set(FRUSTUM_CULL_ISPC_OUTPUTS "")
  set(SKINNING_ISPC_OUTPUTS "")
  message(STATUS "ISPC not found — ISPC kernels skipped; scalar fallback active")
endif ()

target_sources(llmath
    PRIVATE
    llbbox.cpp
    llbboxlocal.cpp
    llcalc.cpp
    llcalcparser.cpp
    llcamera.cpp
    llcoordframe.cpp
    llline.cpp
    llmatrix3a.cpp
    llmatrix4a.cpp
    llmeshoptimizer.cpp
    llmodularmath.cpp
    lloctree.cpp
    llperlin.cpp
    llquaternion.cpp
    llrigginginfo.cpp
    llrect.cpp
    llvector4a.cpp
    llvolume.cpp
    llvolumemgr.cpp
    llvolumeoctree.cpp
    llsdutil_math.cpp
    m3math.cpp
    m4math.cpp
    raytrace.cpp
    v2math.cpp
    v3color.cpp
    v3dmath.cpp
    v3math.cpp
    v4color.cpp
    v4coloru.cpp
    v4math.cpp
    xform.cpp
    ${FRUSTUM_CULL_ISPC_OUTPUTS}
    ${SKINNING_ISPC_OUTPUTS}
    )

target_sources(llmath
    PUBLIC
    FILE_SET HEADERS
    FILES
    llbbox.h
    llbboxlocal.h
    llcalc.h
    llcalcparser.h
    llcamera.h
    llcoord.h
    llcoordframe.h
    llinterp.h
    llline.h
    llmath.h
    llmatrix3a.h
    llmatrix3a.inl
    llmatrix4a.h
    llmeshoptimizer.h
    llmodularmath.h
    lloctree.h
    llperlin.h
    llplane.h
    llquantize.h
    llquaternion.h
    llquaternion2.h
    llquaternion2.inl
    llrect.h
    llrigginginfo.h
    llsimdmath.h
    llsimdtypes.h
    llsimdtypes.inl
    lltreenode.h
    llvector4a.h
    llvector4a.inl
    llvector4logical.h
    llvolume.h
    llvolumemgr.h
    llvolumeoctree.h
    llsdutil_math.h
    m3math.h
    m4math.h
    raytrace.h
    v2math.h
    v3color.h
    v3colorutil.h
    v3dmath.h
    v3math.h
    v4color.h
    v4coloru.h
    v4math.h
    xform.h
    )

if (ISPC_EXECUTABLE)
  target_sources(llmath PUBLIC FILE_SET HEADERS FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/vayu_frustum_cull_ispc.h
      ${CMAKE_CURRENT_SOURCE_DIR}/vayu_skinning_ispc.h)
endif ()

target_link_libraries(llmath PUBLIC llcommon ll::mikktspace ll::sse2neon ll::meshoptimizer)
target_include_directories(llmath  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})
target_precompile_headers(llmath REUSE_FROM llprecompiled)

# Add tests
if (BUILD_TESTING)
  # UNIT TESTS
  SET(llmath_TEST_SOURCE_FILES
    llbboxlocal.cpp
    llmodularmath.cpp
    llrect.cpp
    v2math.cpp
    v3color.cpp
    v4color.cpp
    v4coloru.cpp
    )

  LL_ADD_PROJECT_UNIT_TESTS(llmath "${llmath_TEST_SOURCE_FILES}")

  # INTEGRATION TESTS
  set(test_libs llmath llcommon)
  set(test_project llmath)
  # TODO: Some of these need refactoring to be proper Unit tests rather than Integration tests.
  LL_ADD_INTEGRATION_TEST(alignment "" "${test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llbbox llbbox.cpp "${test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llquaternion llquaternion.cpp "${test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llsdutil_math "" "${test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(mathmisc "" "${test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(m3math "" "${test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(v3dmath v3dmath.cpp "${test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(v3math v3math.cpp "${test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(v4math v4math.cpp "${test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(xform xform.cpp "${test_libs}" "${test_project}")
endif ()
