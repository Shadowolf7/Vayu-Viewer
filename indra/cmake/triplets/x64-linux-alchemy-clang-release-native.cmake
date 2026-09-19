set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)

set(ALCHEMY_ISA_TIER baseline)
set(ALCHEMY_TRIPLET_REVISION 3)
include("${CMAKE_CURRENT_LIST_DIR}/alchemy-base.cmake")

# Match the viewer's own -march (USE_MARCH_NATIVE) with Clang chainload
set(VCPKG_C_FLAGS "-march=native -fvisibility=hidden")
set(VCPKG_CXX_FLAGS "-march=native -fvisibility=hidden -fvisibility-inlines-hidden")
set(VCPKG_CMAKE_POSITION_INDEPENDENT_CODE ON)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/x64-linux-clang-toolchain.cmake")
