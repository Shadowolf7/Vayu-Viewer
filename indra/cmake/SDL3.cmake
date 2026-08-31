# -*- cmake -*-
include_guard()

# SDL's video and GL bring-up on its own, with none of the window backend's
# other appetites. The GL-backed llrender tests want a context and nothing else,
# and they want it on platforms where USE_SDL_WINDOW is off -- so this half is
# always available, and the window backend below builds on it.
add_library(ll::SDL3core INTERFACE IMPORTED)
find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(ll::SDL3core INTERFACE SDL3::SDL3)

add_library(ll::SDL3 INTERFACE IMPORTED)

if(NOT USE_SDL_WINDOW)
    return()
endif()

if(LINUX)
    # SDL3's own vcpkg build silently links libdecor only if it's present on the
    # host at *that* configure time (no vcpkg feature flag forces it) — without it,
    # windows come up with no OS decorations on compositors with no server-side
    # decoration (GNOME/Mutter, Weston) and nothing here fails to tell you why.
    # Failing loudly here can't undo an already-stale vcpkg-built SDL3 (see
    # doc/BUILD.md's Linux prerequisites for the package name per distro), but it
    # catches the common case: a fresh clone/CI image missing the dev package.
    find_package(PkgConfig)
    pkg_check_modules(LIBDECOR REQUIRED IMPORTED_TARGET GLOBAL libdecor-0)
endif()

target_link_libraries(ll::SDL3 INTERFACE ll::SDL3core)

# SDL3_ttf renders the LLWindowSDL splash-screen text (see LLSplashScreenSDL).
find_package(SDL3_ttf CONFIG REQUIRED)
target_link_libraries(ll::SDL3 INTERFACE $<IF:$<TARGET_EXISTS:SDL3_ttf::SDL3_ttf-shared>,SDL3_ttf::SDL3_ttf-shared,SDL3_ttf::SDL3_ttf-static>)

# SDL3_image loads the branded splash icon PNG (and, on macOS, the cursor TIFFs).
find_package(SDL3_image CONFIG REQUIRED)
target_link_libraries(ll::SDL3 INTERFACE $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
