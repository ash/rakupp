// DataCsv.h — the `csv` tag's engine primitives (DATA-PLAN P2).
//
// TWIN: CSV::Native's src/csv.c, which this is a PORT of rather than a
// reimplementation. That distinction is the plan's and it is load-bearing:
// the extension is what the distribution's suite already pins, and re-deriving
// the edge cases — doubled quotes, a quoted field spanning lines, CRLF against
// a lone CR, :strict, duplicate header names, what may follow a closing quote
// — is exactly how two implementations of one format come to disagree.
//
// The two are held together by CSV::Native's own suite, which runs its Raku
// implementation against its C on every case it has; a divergence here shows
// as a failure in t/regression/data-native-csv.raku, which asks the module the
// same questions.
//
// The surface is the MODULE's, not the C's: from-csv takes a Str, an IO::Path
// or an IO::Handle and normalises :headers itself, because that is what a
// caller of `use Data::Native <csv>` writes.
#pragma once

#include "Value.h"

namespace rakupp {

class Interpreter;

// `rakupp-from-csv($src, :$sep, :$quote, :$headers, :$strict)`
Value dataCsvFromCsv(Interpreter& I, ValueList& args);
// `rakupp-to-csv(@rows, :$sep, :$quote, :$eol, :$headers, :$always-quote)`
Value dataCsvToCsv(Interpreter& I, ValueList& args);

} // namespace rakupp
