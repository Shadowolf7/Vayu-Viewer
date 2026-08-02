//  LSL (Linden Scripting Language) lexer for Lexilla/Scintilla
/** @file LexLSL.cxx
 ** Lexer for LSL, following the Lexilla plugin protocol so it can be loaded
 ** by any Lexilla-hosting Scintilla application.
 **
 ** Keyword list slots (populated by the host via SCI_SETKEYWORDS, matching
 ** LexerBase::keyWordLists):
 **   0 - control words  (do, else, for, if, jump, return, state, while)
 **   1 - types          (float, integer, key, list, quaternion, rotation, string, vector)
 **   2 - constants      (built-in constants, e.g. TRUE, PI, AGENT_FLYING...)
 **   3 - events         (event handler names, e.g. state_entry, touch_start...)
 **   4 - functions      (built-in function names, e.g. llSay, llSetPos...)
 ** This lexer only classifies tokens and checks identifiers against whatever
 ** word lists it's given -- it does not embed the LSL keyword tables itself.
 ** The host application is responsible for loading those (e.g. from this
 ** repo's keywords_lsl_default.xml) and supplying them at runtime, so both
 ** this tool and the viewer's own in-editor highlighting stay in sync with
 ** one source of truth instead of drifting apart.
 **
 ** Status: written against the real Lexilla/Scintilla headers (fetched and
 ** checked API-by-API, not from memory), but NOT YET COMPILED OR TESTED
 ** against an actual Scintilla build. The state-machine logic in Lex() in
 ** particular needs real verification before this can be trusted.
 **/
// Adapted from Lexilla's SimpleLexer.cxx example (public domain / same terms
// as Scintilla & Lexilla, see https://www.scintilla.org/License.txt).

#include <cstdlib>
#include <cstring>
#include <cassert>

#include <string>
#include <string_view>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"
// Lexilla.h is deliberately not included here: it declares statically linked
// functions without __declspec(dllexport), which would conflict with the
// exported functions below.

#include "WordList.h"
#include "PropSetSimple.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"
#include "LexerBase.h"

using namespace Scintilla;
using namespace Lexilla;

namespace {

// Style/state constants. These double as both the StyleContext state and the
// style index applied to the text -- the standard Scintilla lexer idiom.
enum
{
    SCE_LSL_DEFAULT      = 0,
    SCE_LSL_COMMENTLINE  = 1,
    SCE_LSL_COMMENTBLOCK = 2,
    SCE_LSL_STRING       = 3,
    SCE_LSL_NUMBER       = 4,
    SCE_LSL_OPERATOR     = 5,
    SCE_LSL_IDENTIFIER   = 6,
    SCE_LSL_CONTROL      = 7,
    SCE_LSL_TYPE         = 8,
    SCE_LSL_CONSTANT     = 9,
    SCE_LSL_EVENT        = 10,
    SCE_LSL_FUNCTION     = 11,
};

// Keyword list slot indices, matching what the host passes via SCI_SETKEYWORDS.
enum
{
    LSL_KEYWORDS_CONTROL   = 0,
    LSL_KEYWORDS_TYPES     = 1,
    LSL_KEYWORDS_CONSTANTS = 2,
    LSL_KEYWORDS_EVENTS    = 3,
    LSL_KEYWORDS_FUNCTIONS = 4,
};

bool IsIdentifierStart(int ch) noexcept
{
    static const CharacterSet setWordStart(CharacterSet::setAlpha, "_");
    return setWordStart.Contains(ch);
}

bool IsIdentifierContinue(int ch) noexcept
{
    static const CharacterSet setWord(CharacterSet::setAlphaNum, "_");
    return setWord.Contains(ch);
}

bool IsOperatorChar(int ch) noexcept
{
    return strchr("+-*/%=<>!&|^~?:;,.(){}[]", ch) != nullptr;
}

// Classify a completed identifier against the keyword lists the host has
// supplied. Falls back to SCE_LSL_IDENTIFIER (plain identifier) if it
// doesn't match any of them.
int ClassifyIdentifier(const std::string &ident, WordList *keywordLists[])
{
    if (keywordLists[LSL_KEYWORDS_CONTROL] && keywordLists[LSL_KEYWORDS_CONTROL]->InList(ident.c_str()))
        return SCE_LSL_CONTROL;
    if (keywordLists[LSL_KEYWORDS_TYPES] && keywordLists[LSL_KEYWORDS_TYPES]->InList(ident.c_str()))
        return SCE_LSL_TYPE;
    if (keywordLists[LSL_KEYWORDS_CONSTANTS] && keywordLists[LSL_KEYWORDS_CONSTANTS]->InList(ident.c_str()))
        return SCE_LSL_CONSTANT;
    if (keywordLists[LSL_KEYWORDS_EVENTS] && keywordLists[LSL_KEYWORDS_EVENTS]->InList(ident.c_str()))
        return SCE_LSL_EVENT;
    if (keywordLists[LSL_KEYWORDS_FUNCTIONS] && keywordLists[LSL_KEYWORDS_FUNCTIONS]->InList(ident.c_str()))
        return SCE_LSL_FUNCTION;
    return SCE_LSL_IDENTIFIER;
}

bool IsIdentifierStyle(int style) noexcept
{
    return style == SCE_LSL_IDENTIFIER || style == SCE_LSL_CONTROL || style == SCE_LSL_TYPE
        || style == SCE_LSL_CONSTANT || style == SCE_LSL_EVENT || style == SCE_LSL_FUNCTION;
}

class LexerLSL : public LexerBase
{
public:
    LexerLSL() : LexerBase(nullptr, 0) {}

    void SCI_METHOD Lex(Sci_PositionU startPos, Sci_Position length, int initStyle, IDocument *pAccess) override;
    void SCI_METHOD Fold(Sci_PositionU startPos, Sci_Position length, int initStyle, IDocument *pAccess) override;

    static ILexer5 *LexerFactoryLSL()
    {
        try
        {
            return new LexerLSL();
        }
        catch (...)
        {
            return nullptr;
        }
    }
};

void SCI_METHOD LexerLSL::Lex(Sci_PositionU startPos, Sci_Position length, int initStyle, IDocument *pAccess)
{
    try
    {
        LexAccessor styler(pAccess);
        StyleContext sc(startPos, length, initStyle, styler);
        std::string identBuffer;

        for (; sc.More(); sc.Forward())
        {
            // Accumulate an in-progress identifier/keyword-candidate until a
            // non-identifier character ends it, then classify and re-color
            // the whole run in one go.
            if (IsIdentifierStyle(sc.state))
            {
                if (IsIdentifierContinue(sc.ch))
                {
                    identBuffer += static_cast<char>(sc.ch);
                    continue;
                }
                sc.ChangeState(ClassifyIdentifier(identBuffer, keyWordLists));
                sc.SetState(SCE_LSL_DEFAULT);
                identBuffer.clear();
            }
            else if (sc.state == SCE_LSL_COMMENTLINE)
            {
                if (sc.atLineEnd)
                {
                    sc.SetState(SCE_LSL_DEFAULT);
                }
                continue;
            }
            else if (sc.state == SCE_LSL_COMMENTBLOCK)
            {
                if (sc.ch == '*' && sc.chNext == '/')
                {
                    sc.Forward();
                    sc.ForwardSetState(SCE_LSL_DEFAULT);
                }
                continue;
            }
            else if (sc.state == SCE_LSL_STRING)
            {
                if (sc.ch == '\\' && (sc.chNext == '"' || sc.chNext == '\\'))
                {
                    sc.Forward();
                }
                else if (sc.ch == '"')
                {
                    sc.ForwardSetState(SCE_LSL_DEFAULT);
                }
                else if (sc.atLineEnd)
                {
                    // Unterminated string: bail back to default at line end
                    // rather than eating the rest of the file.
                    sc.SetState(SCE_LSL_DEFAULT);
                }
                continue;
            }
            else if (sc.state == SCE_LSL_NUMBER)
            {
                const bool exponentSign = (sc.ch == '+' || sc.ch == '-') && (sc.chPrev == 'e' || sc.chPrev == 'E');
                if (IsIdentifierContinue(sc.ch) || sc.ch == '.' || exponentSign)
                {
                    continue;
                }
                sc.SetState(SCE_LSL_DEFAULT);
            }
            else if (sc.state == SCE_LSL_OPERATOR)
            {
                sc.SetState(SCE_LSL_DEFAULT);
            }

            // sc.state == SCE_LSL_DEFAULT at this point: decide what starts here.
            if (sc.state == SCE_LSL_DEFAULT)
            {
                if (sc.ch == '/' && sc.chNext == '/')
                {
                    sc.SetState(SCE_LSL_COMMENTLINE);
                }
                else if (sc.ch == '/' && sc.chNext == '*')
                {
                    sc.SetState(SCE_LSL_COMMENTBLOCK);
                    sc.Forward();
                }
                else if (sc.ch == '"')
                {
                    sc.SetState(SCE_LSL_STRING);
                }
                else if (IsADigit(sc.ch) || (sc.ch == '.' && IsADigit(sc.chNext)))
                {
                    sc.SetState(SCE_LSL_NUMBER);
                }
                else if (IsIdentifierStart(sc.ch))
                {
                    sc.SetState(SCE_LSL_IDENTIFIER);
                    identBuffer = static_cast<char>(sc.ch);
                }
                else if (IsOperatorChar(sc.ch))
                {
                    sc.SetState(SCE_LSL_OPERATOR);
                }
            }
        }

        // Flush a trailing identifier that ran to EOF without a terminating char.
        if (!identBuffer.empty())
        {
            sc.ChangeState(ClassifyIdentifier(identBuffer, keyWordLists));
        }

        sc.Complete();
    }
    catch (...)
    {
        // Should not throw into the caller, which may be compiled with a
        // different compiler or options.
        pAccess->SetErrorStatus(SC_STATUS_FAILURE);
    }
}

void SCI_METHOD LexerLSL::Fold(Sci_PositionU startPos, Sci_Position length, int initStyle, IDocument *pAccess)
{
    // Fold on brace nesting depth -- the usual convention for C-family
    // languages, which LSL's block structure follows.
    LexAccessor styler(pAccess);
    const Sci_PositionU endPos = startPos + length;
    Sci_Position lineCurrent = styler.GetLine(startPos);
    int levelCurrent = SC_FOLDLEVELBASE;
    if (lineCurrent > 0)
    {
        levelCurrent = styler.LevelAt(lineCurrent - 1) & SC_FOLDLEVELNUMBERMASK;
    }
    int levelNext = levelCurrent;

    for (Sci_PositionU i = startPos; i < endPos; i++)
    {
        const char ch = styler[i];
        const int style = styler.StyleAt(i);
        if (style == SCE_LSL_OPERATOR)
        {
            if (ch == '{')
            {
                levelNext++;
            }
            else if (ch == '}')
            {
                levelNext--;
            }
        }
        if (ch == '\n' || i == endPos - 1)
        {
            const int levelUse = levelCurrent;
            int lev = levelUse | (levelNext << 16);
            if (levelUse < levelNext)
            {
                lev |= SC_FOLDLEVELHEADERFLAG;
            }
            if (lev != styler.LevelAt(lineCurrent))
            {
                styler.SetLevel(lineCurrent, lev);
            }
            lineCurrent++;
            levelCurrent = levelNext;
        }
    }
}

} // namespace

#if defined(_WIN32)
#define EXPORT_FUNCTION __declspec(dllexport)
#define CALLING_CONVENTION __stdcall
#else
#define EXPORT_FUNCTION __attribute__((visibility("default")))
#define CALLING_CONVENTION
#endif

namespace {
const char *lexerName = "lsl";
}

extern "C" {

EXPORT_FUNCTION int CALLING_CONVENTION GetLexerCount()
{
    return 1;
}

EXPORT_FUNCTION void CALLING_CONVENTION GetLexerName(unsigned int index, char *name, int buflength)
{
    *name = 0;
    if (index == 0 && buflength > static_cast<int>(strlen(lexerName)))
    {
        strcpy(name, lexerName);
    }
}

EXPORT_FUNCTION LexerFactoryFunction CALLING_CONVENTION GetLexerFactory(unsigned int index)
{
    if (index == 0)
    {
        return LexerLSL::LexerFactoryLSL;
    }
    return nullptr;
}

EXPORT_FUNCTION Scintilla::ILexer5 *CALLING_CONVENTION CreateLexer(const char *name)
{
    if (0 == strcmp(name, lexerName))
    {
        return LexerLSL::LexerFactoryLSL();
    }
    return nullptr;
}

EXPORT_FUNCTION const char *CALLING_CONVENTION GetNameSpace()
{
    return "vayu";
}

} // extern "C"
