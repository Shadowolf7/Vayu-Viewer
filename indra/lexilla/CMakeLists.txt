###############################################################################
# Lexilla lexer-writing helper library (lexlib)
#
# Only lexlib is built here -- the shared helper classes (StyleContext,
# WordList, LexerBase, CharacterSet, etc.) that indra/notecard_editor's
# LexLSL.cxx is written against. Lexilla's own src/Lexilla.cxx (the dynamic
# multi-lexer catalogue/dispatch hub that lets a Scintilla host load
# arbitrary third-party lexer shared libraries by name at runtime) is
# deliberately NOT built: our tool only ever needs its own LSL lexer, so it
# can instantiate LexerLSL directly in C++ rather than going through
# Lexilla's dynamic-loading indirection, which exists for host apps that
# don't control the lexer's source the way we do.
#
# Pinned to 5.4.6, fetched from the same upstream distribution vcpkg trusts
# (https://www.scintilla.org/lexilla546.zip, SHA512
# 7290de2acbe9e52cac31aa3bf89dae66faa2040b45e715a2e18d2dd5804b2486dac8ae1cec68d8dc9215fc953628d492dbf57e61751011d17c3d70899a47dec0
# per vcpkg's ports/lexilla/portfile.cmake). Unlike Scintilla, Lexilla does
# have an official-ish git mirror (github.com/ScintillaOrg/lexilla) that
# could be used as a submodule -- but see indra/scintilla/CMakeLists.txt's
# note: the source-acquisition mechanism for both isn't decided yet, and
# using a submodule for one and something else for the other would be
# inconsistent, so this is left open for both pending that decision.
###############################################################################

set(LEXILLA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lexilla" CACHE PATH
    "Path to an extracted Lexilla 5.4.6 source tree")

add_library(
    lexilla_lexlib
    STATIC
    ${LEXILLA_SOURCE_DIR}/lexlib/Accessor.cxx
    ${LEXILLA_SOURCE_DIR}/lexlib/CharacterCategory.cxx
    ${LEXILLA_SOURCE_DIR}/lexlib/CharacterSet.cxx
    ${LEXILLA_SOURCE_DIR}/lexlib/DefaultLexer.cxx
    ${LEXILLA_SOURCE_DIR}/lexlib/InList.cxx
    ${LEXILLA_SOURCE_DIR}/lexlib/LexAccessor.cxx
    ${LEXILLA_SOURCE_DIR}/lexlib/LexerBase.cxx
    ${LEXILLA_SOURCE_DIR}/lexlib/LexerModule.cxx
    ${LEXILLA_SOURCE_DIR}/lexlib/LexerSimple.cxx
    ${LEXILLA_SOURCE_DIR}/lexlib/PropSetSimple.cxx
    ${LEXILLA_SOURCE_DIR}/lexlib/StyleContext.cxx
    ${LEXILLA_SOURCE_DIR}/lexlib/WordList.cxx
)

target_include_directories(
    lexilla_lexlib
    PUBLIC
    ${LEXILLA_SOURCE_DIR}/include
    ${LEXILLA_SOURCE_DIR}/lexlib
)

target_link_libraries(lexilla_lexlib PUBLIC scintilla)
