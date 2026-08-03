###############################################################################
# Standalone notecard/script editor
#
# Only the LSL lexer library is defined here so far -- the host application
# (window creation, embedding Scintilla, per-platform integration, loading
# keywords_lsl_default.xml at startup and populating the lexer's keyword-list
# slots via SCI_SETKEYWORDS) has not been written yet. See
# indra/notecard_editor/lexers/LexLSL.cxx for the keyword-list-slot design
# and its current, NOT YET COMPILED/TESTED status.
###############################################################################

add_library(
    lsl_lexer
    STATIC
    lexers/LexLSL.cxx
)

target_link_libraries(lsl_lexer PUBLIC lexilla_lexlib)
