// DataCsv.cpp — the `csv` tag's engine primitives (DATA-PLAN P2).
//
// A port of CSV::Native's src/csv.c onto Value. See DataCsv.h for why it is a
// port and not a fresh implementation. Where the C used the extension ABI's
// rk_* calls this uses Value directly; the control flow, the error conditions
// and the byte-level decisions are the same, deliberately.
//
// The one difference on purpose: error messages read `from-csv: …` rather than
// `CSV::Native: …`, because naming a module that was not involved would be a
// lie. Behaviour is identical, and the regression file asserts that against the
// module itself.

#include "DataCsv.h"
#include "Interpreter.h"

#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace rakupp {

namespace {

[[noreturn]] void csvDie(const std::string& msg) {
    throw RakuError{Value::typeObj("X::AdHoc"), msg};
}

// ---- the dialect, validated exactly as the module's check-dialect does -----

struct Dialect {
    std::string sep = ",";
    std::string quo = "\"";
    std::string eol = "\n";
};

void checkDialect(const std::string& who, const Dialect& d) {
    if (d.sep.empty()) csvDie(who + ": sep must not be empty");
    // "exactly one character" is the module's wording and its check is on
    // CHARACTERS; a quote is compared here by bytes, so a multi-byte quote is
    // one character and several bytes, which the scan handles. Count code
    // points, then, rather than bytes: a byte count would reject a legal
    // multi-byte quote and accept an illegal two-character one.
    size_t qchars = 0;
    for (unsigned char c : d.quo) if ((c & 0xC0) != 0x80) qchars++;
    if (qchars != 1)
        csvDie(who + ": quote must be exactly one character, not '" + d.quo + "'");
    if (d.quo == "\n" || d.quo == "\r") csvDie(who + ": quote must not be a line ending");
    if (d.sep.find(d.quo) != std::string::npos)
        csvDie(who + ": sep must not contain the quote character");
    if (d.sep.find('\n') != std::string::npos || d.sep.find('\r') != std::string::npos)
        csvDie(who + ": sep must not contain a line ending");
}

// ---- parsing --------------------------------------------------------------

enum Term { T_SEP, T_EOL, T_EOF };

struct P {
    const char* p = nullptr;
    const char* end = nullptr;
    const Dialect* d = nullptr;
    long line = 1;              // 1-based, the line `p` is on
    std::string dec;            // decode buffer for quoted fields with doubled quotes

    bool at(const std::string& needle) const {
        return (size_t)(end - p) >= needle.size() && std::memcmp(p, needle.data(), needle.size()) == 0;
    }
    // One line ending at p — LF, CRLF or a lone CR — counted as one line.
    void eol() {
        if (*p == '\r') { p++; if (p < end && *p == '\n') p++; }
        else p++;
        line++;
    }
};

[[noreturn]] void failLine(const char* what, long line) {
    csvDie("from-csv: " + std::string(what) + " at line " + std::to_string(line));
}

// One field. Sets `term` to what ended it.
std::string field(P& s, Term& term) {
    long fline = s.line;
    const char* start;

    if (s.at(s.d->quo)) {
        // Quoted. Zero-copy until the first doubled quote; from there the field
        // is assembled in the shared decode buffer.
        bool decoded = false;
        s.p += s.d->quo.size();
        start = s.p;
        s.dec.clear();
        for (;;) {
            if (s.p >= s.end) failLine("unterminated quoted field starting", fline);
            if (*s.p == s.d->quo[0] && s.at(s.d->quo)) {
                const char* q = s.p;
                s.p += s.d->quo.size();
                if (s.at(s.d->quo)) {            // doubled: one quote of content
                    decoded = true;
                    s.dec.append(start, (size_t)(q - start));
                    s.dec += s.d->quo;
                    s.p += s.d->quo.size();
                    start = s.p;
                    continue;
                }
                std::string out;                  // closing
                if (decoded) { s.dec.append(start, (size_t)(q - start)); out = s.dec; }
                else out.assign(start, (size_t)(q - start));
                // After a closing quote only a separator, a line ending or the
                // end of the text may follow — a space there is an error, not
                // content.
                if (s.p >= s.end) term = T_EOF;
                else if (s.at(s.d->sep)) { s.p += s.d->sep.size(); term = T_SEP; }
                else if (*s.p == '\n' || *s.p == '\r') { s.eol(); term = T_EOL; }
                else failLine("text after a closing quote", fline);
                return out;
            }
            if (*s.p == '\n' || *s.p == '\r') s.eol();
            else s.p++;
        }
    }

    // Unquoted: everything up to the next separator or line ending, and a quote
    // anywhere in it is an error (RFC 4180: a field holding a quote must itself
    // be quoted).
    start = s.p;
    for (;;) {
        if (s.p >= s.end) { term = T_EOF; break; }
        char ch = *s.p;
        if (ch == s.d->sep[0] && s.at(s.d->sep)) {
            std::string v(start, (size_t)(s.p - start));
            s.p += s.d->sep.size();
            term = T_SEP;
            return v;
        }
        if (ch == '\n' || ch == '\r') {
            std::string v(start, (size_t)(s.p - start));
            s.eol();
            term = T_EOL;
            return v;
        }
        if (ch == s.d->quo[0] && s.at(s.d->quo)) failLine("a quote inside an unquoted field", fline);
        s.p++;
    }
    return std::string(start, (size_t)(s.p - start));
}

// A duplicate name would make one column silently overwrite another.
void checkDupKeys(const std::vector<std::string>& keys) {
    for (size_t i = 0; i < keys.size(); i++)
        for (size_t j = i + 1; j < keys.size(); j++)
            if (keys[i] == keys[j])
                csvDie("from-csv: duplicate header '" + keys[i].substr(0, 200) + "'");
}

// ---- writing --------------------------------------------------------------

struct W {
    std::string out;
    const Dialect* d = nullptr;
    bool always = false;
};

bool contains(const std::string& s, const std::string& needle) {
    return !needle.empty() && s.find(needle) != std::string::npos;
}

// One cell: its Str, quoted when the separator, the quote or a line ending is
// in it (or always, when asked); an undefined value is an empty field.
void wcell(W& w, const Value* v) {
    std::string s;
    if (v && rtIsDefined(*v)) s = v->toStr();
    bool quote = w.always || contains(s, w.d->sep) || contains(s, w.d->quo) ||
                 s.find('\n') != std::string::npos || s.find('\r') != std::string::npos;
    if (!quote) { w.out += s; return; }
    w.out += w.d->quo;
    size_t from = 0;
    for (size_t i = 0; i + w.d->quo.size() <= s.size(); ) {
        if (s[i] == w.d->quo[0] && std::memcmp(s.data() + i, w.d->quo.data(), w.d->quo.size()) == 0) {
            w.out.append(s, from, i + w.d->quo.size() - from);   // the quote itself,
            w.out += w.d->quo;                                    // then again
            i += w.d->quo.size();
            from = i;
        }
        else i++;
    }
    w.out.append(s, from, s.size() - from);
    w.out += w.d->quo;
}

void wrowCells(W& w, const std::vector<const Value*>& cells) {
    for (size_t i = 0; i < cells.size(); i++) {
        if (i) w.out += w.d->sep;
        wcell(w, cells[i]);
    }
    w.out += w.d->eol;
}

// ---- argument handling ----------------------------------------------------

const Value* namedArg(ValueList& a, const char* name) {
    for (auto& x : a)
        if (x.t == VT::Pair && x.namedArg && x.s == name) return x.pairVal();
    return nullptr;
}
const Value* positional(ValueList& a, size_t which) {
    size_t seen = 0;
    for (auto& x : a) {
        if (x.t == VT::Pair && x.namedArg) continue;
        if (seen++ == which) return &x;
    }
    return nullptr;
}
std::string namedStr(ValueList& a, const char* name, const std::string& dflt) {
    const Value* v = namedArg(a, name);
    return v && rtIsDefined(*v) ? v->toStr() : dflt;
}

// The module's header-names, as three outcomes rather than a Bool that has to
// mean two things at once — conflating "names were given" with "the Bool was
// true" made `:headers` read the first record as a header with zero columns.
enum HeaderMode { H_NONE, H_FIRST_ROW, H_NAMED };

HeaderMode headerMode(const Value* h, std::vector<std::string>& names, const char* who) {
    names.clear();
    if (!h || !rtIsDefined(*h)) return H_NONE;
    if (h->t == VT::Bool) return h->b ? H_FIRST_ROW : H_NONE;
    if (h->t == VT::Str && h->hashKind.empty()) { names.push_back(h->s); return H_NAMED; }
    if (h->t == VT::Array && h->arr()) {
        for (auto& e : *h->arr()) names.push_back(e.toStr());
        return H_NAMED;
    }
    csvDie(std::string(who) + ": headers must be a Bool or a list of names");
}

} // namespace

// ---------------------------------------------------------------------------

Value dataCsvFromCsv(Interpreter& I, ValueList& a) {
    Dialect d;
    d.sep = namedStr(a, "sep", ",");
    d.quo = namedStr(a, "quote", "\"");
    checkDialect("from-csv", d);

    const Value* src = positional(a, 0);
    if (!src) csvDie("from-csv: expected something to read");

    // Str, IO::Path or IO::Handle, as the module takes it — and anything else
    // through `.Str`, which is the module's own last arm rather than an error.
    // The two IO shapes are spelled differently in this engine: an IO::Path is
    // a Str tagged "IO", an IO::Handle a Hash tagged "FileHandle". Asking for
    // the type name would be the readable check and is the wrong one — a
    // subclass answers its own name — so these are the tags.
    bool isPath   = (src->t == VT::Str  && src->hashKind == "IO");
    bool isHandle = (src->t == VT::Hash && src->hashKind == "FileHandle");
    std::string text;
    if (isPath || isHandle) {
        ValueList sa; sa.push_back(*src);
        text = I.callBuiltin("slurp", std::move(sa)).toStr();
    }
    else text = src->toStr();

    std::vector<std::string> keys;
    HeaderMode hm = headerMode(namedArg(a, "headers"), keys, "from-csv");
    bool haveKeys = (hm == H_NAMED);
    bool wantHeader = (hm == H_FIRST_ROW);
    if (haveKeys) checkDupKeys(keys);

    const Value* st = namedArg(a, "strict");
    bool strict = st && st->truthy();

    // A UTF-8 byte-order mark is Excel's signature, not a field.
    size_t off = 0;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) off = 3;

    P s;
    s.d = &d;
    s.p = text.data() + off;
    s.end = text.data() + text.size();

    bool haveExpected = haveKeys;
    size_t expected = keys.size();

    Value rows = Value::array();
    std::vector<std::string> cells;
    while (s.p < s.end) {
        long rowLine = s.line;
        cells.clear();
        Term term;
        for (;;) {
            cells.push_back(field(s, term));
            if (term != T_SEP) break;
        }

        if (wantHeader && !haveKeys) {
            keys = cells;
            checkDupKeys(keys);
            haveKeys = true;
            haveExpected = true; expected = keys.size();
            continue;
        }
        if (strict) {
            if (!haveExpected) { haveExpected = true; expected = cells.size(); }
            else if (cells.size() != expected)
                csvDie("from-csv: line " + std::to_string(rowLine) + " has " +
                       std::to_string(cells.size()) + " fields, expected " + std::to_string(expected));
        }
        if (haveKeys) {
            if (cells.size() > keys.size())
                csvDie("from-csv: line " + std::to_string(rowLine) + " has " +
                       std::to_string(cells.size()) + " fields but the header has " +
                       std::to_string(keys.size()));
            Value h = Value::makeHash();
            for (size_t i = 0; i < cells.size(); i++) (*h.hash())[keys[i]] = Value::str(cells[i]);
            rows.arr()->push_back(h);
        }
        else {
            Value r = Value::array();
            for (auto& c : cells) r.arr()->push_back(Value::str(c));
            rows.arr()->push_back(r);
        }
    }
    return rows;
}

Value dataCsvToCsv(Interpreter& I, ValueList& a) {
    (void)I;
    Dialect d;
    d.sep = namedStr(a, "sep", ",");
    d.quo = namedStr(a, "quote", "\"");
    d.eol = namedStr(a, "eol", "\n");
    checkDialect("to-csv", d);
    if (d.eol != "\n" && d.eol != "\r\n" && d.eol != "\r")
        csvDie("to-csv: eol must be \\n, \\r\\n or \\r");

    const Value* rowsv = positional(a, 0);
    if (!rowsv || rowsv->t != VT::Array || !rowsv->arr())
        csvDie("to-csv: expected a list of rows");
    auto& rows = *rowsv->arr();

    std::vector<std::string> names;
    const Value* hv = namedArg(a, "headers");
    bool namesGiven = (headerMode(hv, names, "to-csv") == H_NAMED);

    // The module resolves :headers into a list of names and a flag saying
    // whether to write them as the first line. A hash first row supplies its
    // sorted keys, and only an explicit :!headers suppresses the line.
    bool headerLine = false;
    if (namesGiven) headerLine = true;
    else if (!rows.empty() && rows[0].t == VT::Hash && rows[0].hash()) {
        for (auto& kv : *rows[0].hash()) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        headerLine = !(hv && hv->t == VT::Bool && !hv->b);
    }

    W w;
    w.d = &d;
    const Value* aq = namedArg(a, "always-quote");
    w.always = aq && aq->truthy();

    std::vector<Value> keyVals;                    // header cells, as Values
    for (auto& n : names) keyVals.push_back(Value::str(n));
    if (headerLine) {
        std::vector<const Value*> cells;
        for (auto& k : keyVals) cells.push_back(&k);
        wrowCells(w, cells);
    }

    for (size_t r = 0; r < rows.size(); r++) {
        const Value& row = rows[r];
        if (row.t == VT::Array && row.arr()) {
            std::vector<const Value*> cells;
            for (auto& e : *row.arr()) cells.push_back(&e);
            wrowCells(w, cells);
        }
        else if (row.t == VT::Hash && row.hash()) {
            // Project the hash onto the header order; a key outside the header
            // is simply not a column.
            if (names.empty())
                csvDie("to-csv: row " + std::to_string(r) + " is a hash but no headers are known");
            std::vector<const Value*> cells(names.size(), nullptr);
            for (auto& kv : *row.hash()) {
                for (size_t i = 0; i < names.size(); i++)
                    if (names[i] == kv.first) { cells[i] = &kv.second; break; }
            }
            wrowCells(w, cells);
        }
        else csvDie("to-csv: row " + std::to_string(r) + " is not a list or a hash");
    }
    return Value::str(w.out);
}

} // namespace rakupp
