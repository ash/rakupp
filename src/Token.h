#pragma once
#include <string>

namespace rakupp {

enum class Tok {
    End,
    // literals
    IntLit,
    NumLit,
    StrLit,        // single-quoted, no interpolation
    VersionLit,    // v1.2.3 / v6.* / v1.2+ — text holds the version WITHOUT the 'v'
    StrInterp,     // double-quoted, interpolation
    RegexLit,      // / / , m// , rx//  -> text = pattern (adverbs prepended)
    SubstLit,      // s///  -> text = pattern, text2 = replacement
    QwList,        // qw<...> / Qw<...> / qqw<...>  -> text = raw content (split on whitespace)
    // identifiers / names
    Ident,         // bareword / sub name / method name
    Var,           // sigil-prefixed variable: $x @a %h &f
    // punctuation
    LParen, RParen,
    LBrace, RBrace,
    LBracket, RBracket,
    Semicolon,
    Comma,
    FatArrow,      // =>
    // operators (text captured in `text`)
    Op,
    // keywords are returned as Ident; the parser decides
};

struct Token {
    Tok kind = Tok::End;
    std::string text;   // raw text / identifier / operator spelling
    std::string text2;  // SubstLit replacement; RegexLit adverb flags
    // for StrInterp the text holds the raw inner content (escapes unprocessed)
    long long ival = 0;
    double nval = 0;
    int line = 0;
    int col = 0;
    bool spaceBefore = false; // whitespace/comment preceded this token
    bool flag = false;        // SubstLit: non-mutating S/// (returns new string, leaves $_ intact)
};

} // namespace rakupp
