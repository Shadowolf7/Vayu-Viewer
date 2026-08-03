###############################################################################
# Scintilla editing component
#
# Vendored directly rather than pulled from vcpkg: vcpkg's scintilla port is
# Windows-only ("supports": "windows & !uwp & !mingw", MSBuild-based) despite
# Scintilla itself having GTK+ and Cocoa backends -- nobody has packaged those
# for vcpkg. Building the portable core plus the right platform layer
# ourselves gives one consistent build across all three platforms instead of
# depending on a port that only covers one of them.
#
# Pinned to 5.5.8, matching the version vcpkg's own (Windows-only) port uses,
# fetched from the same upstream distribution vcpkg trusts
# (https://www.scintilla.org/scintilla558.zip, SHA512
# b1cb0249426331c9fa14e3d3908be629814b10cba552f40ee7e7fe93957994a49550dd0ecb5a3d21d44f91ae9ba91f5fc3c1248700ddebcc7cd41334dc41adaf
# per vcpkg's ports/scintilla/portfile.cmake).
#
# NOTE: how the source actually lands in SCINTILLA_SOURCE_DIR (git submodule,
# CMake FetchContent, or a direct vendored source commit) is not decided by
# this file and is still an open question -- there is no suitable official
# git mirror for Scintilla to submodule against (unlike Lexilla, which has
# one: https://github.com/ScintillaOrg/lexilla). This file only assumes the
# directory is populated with an extracted 5.5.8 source tree by the time
# CMake configures.
###############################################################################

set(SCINTILLA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scintilla" CACHE PATH
    "Path to an extracted Scintilla 5.5.8 source tree")

set(SCINTILLA_CORE_SOURCES
    ${SCINTILLA_SOURCE_DIR}/src/AutoComplete.cxx
    ${SCINTILLA_SOURCE_DIR}/src/CallTip.cxx
    ${SCINTILLA_SOURCE_DIR}/src/CaseConvert.cxx
    ${SCINTILLA_SOURCE_DIR}/src/CaseFolder.cxx
    ${SCINTILLA_SOURCE_DIR}/src/CellBuffer.cxx
    ${SCINTILLA_SOURCE_DIR}/src/ChangeHistory.cxx
    ${SCINTILLA_SOURCE_DIR}/src/CharacterCategoryMap.cxx
    ${SCINTILLA_SOURCE_DIR}/src/CharacterType.cxx
    ${SCINTILLA_SOURCE_DIR}/src/CharClassify.cxx
    ${SCINTILLA_SOURCE_DIR}/src/ContractionState.cxx
    ${SCINTILLA_SOURCE_DIR}/src/DBCS.cxx
    ${SCINTILLA_SOURCE_DIR}/src/Decoration.cxx
    ${SCINTILLA_SOURCE_DIR}/src/Document.cxx
    ${SCINTILLA_SOURCE_DIR}/src/EditModel.cxx
    ${SCINTILLA_SOURCE_DIR}/src/Editor.cxx
    ${SCINTILLA_SOURCE_DIR}/src/EditView.cxx
    ${SCINTILLA_SOURCE_DIR}/src/Geometry.cxx
    ${SCINTILLA_SOURCE_DIR}/src/Indicator.cxx
    ${SCINTILLA_SOURCE_DIR}/src/KeyMap.cxx
    ${SCINTILLA_SOURCE_DIR}/src/LineMarker.cxx
    ${SCINTILLA_SOURCE_DIR}/src/MarginView.cxx
    ${SCINTILLA_SOURCE_DIR}/src/PerLine.cxx
    ${SCINTILLA_SOURCE_DIR}/src/PositionCache.cxx
    ${SCINTILLA_SOURCE_DIR}/src/RESearch.cxx
    ${SCINTILLA_SOURCE_DIR}/src/RunStyles.cxx
    ${SCINTILLA_SOURCE_DIR}/src/ScintillaBase.cxx
    ${SCINTILLA_SOURCE_DIR}/src/Selection.cxx
    ${SCINTILLA_SOURCE_DIR}/src/Style.cxx
    ${SCINTILLA_SOURCE_DIR}/src/UndoHistory.cxx
    ${SCINTILLA_SOURCE_DIR}/src/UniConversion.cxx
    ${SCINTILLA_SOURCE_DIR}/src/UniqueString.cxx
    ${SCINTILLA_SOURCE_DIR}/src/ViewStyle.cxx
    ${SCINTILLA_SOURCE_DIR}/src/XPM.cxx
)

if(WINDOWS)
    set(SCINTILLA_PLATFORM_SOURCES
        ${SCINTILLA_SOURCE_DIR}/win32/HanjaDic.cxx
        ${SCINTILLA_SOURCE_DIR}/win32/ListBox.cxx
        ${SCINTILLA_SOURCE_DIR}/win32/PlatWin.cxx
        ${SCINTILLA_SOURCE_DIR}/win32/ScintillaWin.cxx
        ${SCINTILLA_SOURCE_DIR}/win32/SurfaceD2D.cxx
        ${SCINTILLA_SOURCE_DIR}/win32/SurfaceGDI.cxx
    )
    set(SCINTILLA_PLATFORM_INCLUDE_DIR ${SCINTILLA_SOURCE_DIR}/win32)
elseif(DARWIN)
    set(SCINTILLA_PLATFORM_SOURCES
        ${SCINTILLA_SOURCE_DIR}/cocoa/InfoBar.mm
        ${SCINTILLA_SOURCE_DIR}/cocoa/PlatCocoa.mm
        ${SCINTILLA_SOURCE_DIR}/cocoa/ScintillaCocoa.mm
        ${SCINTILLA_SOURCE_DIR}/cocoa/ScintillaView.mm
    )
    set_source_files_properties(${SCINTILLA_PLATFORM_SOURCES} PROPERTIES LANGUAGE OBJCXX)
    set(SCINTILLA_PLATFORM_INCLUDE_DIR ${SCINTILLA_SOURCE_DIR}/cocoa)
elseif(LINUX)
    set(SCINTILLA_PLATFORM_SOURCES
        ${SCINTILLA_SOURCE_DIR}/gtk/PlatGTK.cxx
        ${SCINTILLA_SOURCE_DIR}/gtk/ScintillaGTK.cxx
        ${SCINTILLA_SOURCE_DIR}/gtk/ScintillaGTKAccessible.cxx
    )
    set(SCINTILLA_PLATFORM_INCLUDE_DIR ${SCINTILLA_SOURCE_DIR}/gtk)
    # GTK Scintilla needs GTK+3 (widget/drawing) -- not currently a project
    # dependency anywhere else. Not yet added to indra/vcpkg.json; needed
    # before this actually configures on Linux.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
endif()

add_library(
    scintilla
    STATIC
    ${SCINTILLA_CORE_SOURCES}
    ${SCINTILLA_PLATFORM_SOURCES}
)

target_include_directories(
    scintilla
    PUBLIC
    ${SCINTILLA_SOURCE_DIR}/include
    PRIVATE
    ${SCINTILLA_SOURCE_DIR}/src
    ${SCINTILLA_PLATFORM_INCLUDE_DIR}
)

if(LINUX)
    target_link_libraries(scintilla PUBLIC PkgConfig::GTK3)
endif()
