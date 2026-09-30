// InterpreterRegex.cpp — regexes, substitution, grammars and hyperoperators
//
// One of the parts InterpreterParts.h lists; what they share is declared there.
#include "InterpreterParts.h"

namespace rakupp {

// Escape regex metacharacters so an interpolated string matches literally.
static std::string spliceRegexValueFwd(const std::string& src);
static std::string quoteMetaRx(const std::string& s) {
    std::string out;
    for (char c : s) {
        // WHITESPACE is insignificant in a regex, so an interpolated string
        // containing spaces has to escape them or it silently matches the
        // squashed text: `/$msg/` with $msg = 'info test message' was matching
        // "infotestmessage" (Log's test writes exactly that).
        if (c == '\n') { out += "\\n"; continue; }
        if (c == '\t') { out += "\\t"; continue; }
        if (c == '\r') { out += "\\r"; continue; }
        if (c == ' ')  { out += "\\ "; continue; }
        // EVERY ASCII non-alphanumeric gets a backslash — the engine reads
        // backslash+punct as the literal char. The old curated list missed `&`
        // (regex CONJUNCTION), so an interpolated query string
        // (`/^POST\s$file/` with `?r=1&r=2`) became two branches that could
        // never match at one spot.
        if ((unsigned char)c < 0x80 && !ascii::isalnum((unsigned char)c) && c != '_')
            out += '\\';
        out += c;
    }
    return out;
}

// End of a Raku-CODE region inside a regex pattern: given the `{` at `i`, the
// index just past its matching `}`, counting nesting and stepping over quoted
// spans. 0 when the brace never closes — the caller then treats it as the
// literal character it must be (`<[{]>`), rather than swallowing the rest.
static size_t rxCodeBraceEnd(const std::string& p, size_t i) {
    int depth = 0;
    for (; i < p.size(); i++) {
        char c = p[i];
        if (c == '\\') { i++; continue; }
        if (c == '\'' || c == '"') {
            char q = c;
            for (i++; i < p.size(); i++) { if (p[i] == '\\') { i++; continue; } if (p[i] == q) break; }
            continue;
        }
        if (c == '{') depth++;
        else if (c == '}' && --depth == 0) return i + 1;
    }
    return 0;
}

// …and of a `:my …;` / `:temp …;` / `:let …;` declaration, which is code with a
// `;` for a terminator instead of braces. 0 when it never terminates.
static size_t rxDeclEnd(const std::string& p, size_t i) {
    for (; i < p.size(); i++) {
        char c = p[i];
        if (c == '\\') { i++; continue; }
        if (c == '\'' || c == '"') {
            char q = c;
            for (i++; i < p.size(); i++) { if (p[i] == '\\') { i++; continue; } if (p[i] == q) break; }
            continue;
        }
        if (c == '{') { size_t e = rxCodeBraceEnd(p, i); if (!e) return 0; i = e - 1; continue; }
        if (c == ';') return i + 1;
    }
    return 0;
}

// Interpolate @array variables into a regex as an LTM `|` alternation of the
// elements' literal (quotemeta'd) text, LONGEST-FIRST — `/@alpha/` matches any
// element, as in Rakudo.
// Base64 decodes via `$str.comb(/@alpha/)`. Left untouched: `@<name>` list
// captures, escaped `\@`, '…' literal spans, and unknown/empty arrays.
std::string Interpreter::rxInterpArrays(const std::string& pat) {
    if (pat.find('@') == std::string::npos || !tctx_.cur) return pat;
    std::string out;
    bool inSq = false; // inside '…': a literal span — no interpolation
    for (size_t i = 0; i < pat.size(); i++) {
        if (size_t sp = Regex::spliceSpan(pat, i)) { out += pat.substr(i, sp); i += sp - 1; continue; }
        if (pat[i] == '\\' && i + 1 < pat.size()) { out += pat[i]; out += pat[i + 1]; i++; continue; }
        if (pat[i] == '\'') { inSq = !inSq; out += pat[i]; continue; }
        if (inSq) { out += pat[i]; continue; }
        // A character class is copied through whole, so that a literal `{`
        // inside it cannot open a false code span and swallow the pattern after
        // it. (The scalar pass a few functions down has always done this; the
        // two are the same scan and should read the same.)
        if (pat[i] == '<' && i + 1 < pat.size() && (pat[i + 1] == '[' || pat[i + 1] == '-')) {
            size_t j = i;
            while (j + 1 < pat.size() && !(pat[j] == ']' && pat[j + 1] == '>')) out += pat[j++];
            while (j < pat.size() && pat[j] != '>') out += pat[j++];
            if (j < pat.size()) out += pat[j];
            i = j;
            continue;
        }
        // A brace region is Raku CODE, not pattern — a `{…}` block, a `<?{…}>`
        // or `<!{…}>` assertion, an interpolated `<{…}>`, a `**{…}` bound — and
        // `@a` in code is the ARRAY the code reads, not an alternation to
        // splice. Rewriting it there handed the block `[ 'x' | 'y' ]`, which as
        // Raku code is an array of ONE junction: Cro's route matcher asked its
        // bind check for `@handlers[1]`, got Nil off that one-element array,
        // and every route with a captured segment answered 404.
        if (pat[i] == '{') {
            if (size_t e = rxCodeBraceEnd(pat, i)) { out += pat.substr(i, e - i); i = e - 1; continue; }
        }
        // Same for a declaration: `:my @segs = @outer;` is code to the `;`.
        if (pat[i] == ':' && (pat.compare(i, 4, ":my ") == 0 || pat.compare(i, 6, ":temp ") == 0 ||
                              pat.compare(i, 5, ":let ") == 0)) {
            if (size_t e = rxDeclEnd(pat, i)) { out += pat.substr(i, e - i); i = e - 1; continue; }
        }
        // `@( … )` — the list contextualizer: the expression's elements are an
        // alternation, longest first, exactly as a bare `@arr`'s are
        // (`$<cat>=@(%h.keys)`, `@( <a b c o> )+`)
        if (pat[i] == '@' && i + 1 < pat.size() && pat[i + 1] == '(') {
            int depth = 0;
            size_t j = i + 1;
            for (; j < pat.size(); j++) {
                if (pat[j] == '(') depth++;
                else if (pat[j] == ')' && --depth == 0) { j++; break; }
            }
            if (depth == 0) {
                Value v = evalString(pat.substr(i, j - i));
                std::vector<std::pair<std::string, bool>> els;
                auto add = [&](const Value& e) { els.push_back({e.t == VT::Regex ? e.s.str() : e.toStr(), e.t == VT::Regex}); };
                if (v.arr()) for (auto& e : *v.arr()) add(e);
                else add(v);
                std::stable_sort(els.begin(), els.end(),
                    [](const std::pair<std::string, bool>& a, const std::pair<std::string, bool>& b) {
                        return a.first.size() > b.first.size(); });
                if (els.empty()) out += "<!>";                // an empty list matches nothing
                else {
                    out += "[";
                    for (size_t k = 0; k < els.size(); k++) {
                        if (k) out += "|";
                        out += els[k].second ? spliceRegexValueFwd(els[k].first) : quoteMetaRx(els[k].first);
                    }
                    out += "]";
                }
                i = j - 1;
                continue;
            }
        }
        // `@$aref` — the array a scalar holds, interpolated like `@arr`
        if (pat[i] == '@' && i + 2 < pat.size() && pat[i + 1] == '$' &&
            (ascii::isalpha((unsigned char)pat[i + 2]) || pat[i + 2] == '_')) {
            size_t j = i + 2;
            while (j < pat.size() && (ascii::isalnum((unsigned char)pat[j]) || pat[j] == '_' ||
                   (pat[j] == '-' && j + 1 < pat.size() && ascii::isalpha((unsigned char)pat[j + 1])))) j++;
            Value* v = tctx_.cur->find("$" + pat.substr(i + 2, j - i - 2));
            if (v && v->t == VT::Array && v->arr() && !v->arr()->empty()) {
                std::vector<std::string> els;
                for (auto& e : *v->arr()) els.push_back(e.toStr());
                std::stable_sort(els.begin(), els.end(),
                    [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
                // no blanks inside: under :sigspace a blank after an atom is a
                // <.ws>, and it would land INSIDE the caller's capture group
                out += "[";
                for (size_t k = 0; k < els.size(); k++) { if (k) out += "|"; out += quoteMetaRx(els[k]); }
                out += "]";
                i = j - 1;
                continue;
            }
        }
        if (pat[i] == '@' && i + 1 < pat.size() &&
            (ascii::isalpha((unsigned char)pat[i + 1]) || pat[i + 1] == '_')) {
            size_t j = i + 1;
            while (j < pat.size() && (ascii::isalnum((unsigned char)pat[j]) || pat[j] == '_' ||
                   (pat[j] == '-' && j + 1 < pat.size() && ascii::isalpha((unsigned char)pat[j + 1])))) j++;
            Value* v = tctx_.cur->find("@" + pat.substr(i + 1, j - i - 1));
            if (v && v->t == VT::Array && v->arr() && !v->arr()->empty()) {
                // `<@arr>` compiles each element AS A REGEX; a bare `@arr` is an
                // alternation of LITERALS (issue #15). Same positional rule as
                // the scalar form.
                bool inAngle = !out.empty() && out.back() == '<' && j < pat.size() && pat[j] == '>';
                // `<alias=@arr>` — the aliased assertion, found by the same
                // leftward scan the scalar `<alias=$var>` does. Without it the
                // `<alias=` stayed put and the substituted alternation read as an
                // inline CHARACTER CLASS (`<w=[…]>`), so `<w=@arr>` over
                // ('A (\S+) A', 'zz') matched the single letter "A".
                std::string alias;
                bool aliased = false;
                if (!inAngle && j < pat.size() && pat[j] == '>' && !out.empty() && out.back() == '=') {
                    size_t b = out.size() - 1, e = b;
                    while (e > 0 && (ascii::isalnum((unsigned char)out[e - 1]) ||
                                     out[e - 1] == '_' || out[e - 1] == '-')) e--;
                    if (e > 0 && e < b && out[e - 1] == '<') { alias = out.substr(e, b - e); aliased = true; }
                }
                std::vector<std::string> els;
                std::set<size_t> rxEls;   // elements that are REGEXES match as patterns
                for (auto& e : *v->arr()) els.push_back(e.toStr());
                {
                    std::vector<std::pair<std::string, bool>> tagged;
                    for (auto& e : *v->arr()) tagged.push_back({e.t == VT::Regex ? e.s.str() : e.toStr(), e.t == VT::Regex});
                    std::stable_sort(tagged.begin(), tagged.end(),
                        [](const std::pair<std::string, bool>& a, const std::pair<std::string, bool>& b) {
                            return a.first.size() > b.first.size(); });
                    els.clear();
                    for (size_t k = 0; k < tagged.size(); k++) {
                        els.push_back(tagged[k].first);
                        if (tagged[k].second) rxEls.insert(k);
                    }
                }
                // The ASSERTION forms are CALLS, like their scalar counterparts:
                // the alternation matches in its own capture frame, so an element's
                // `(…)` fills the sub-match's `$0` rather than renumbering onto the
                // host's — and `$<alias>` is that whole frame.
                if (inAngle || aliased) {
                    std::string src;
                    for (size_t k = 0; k < els.size(); k++) {
                        if (k) src += " | ";        // `|` — see the LTM note below
                        src += "[ "; src += els[k]; src += " ]";
                    }
                    if (aliased) out.erase(out.size() - alias.size() - 2); // drop `<alias=`
                    else out.pop_back();                                   // drop the '<'
                    out += Regex::subSpliceOf(alias, src, false);
                    i = j;                          // skip the '>'
                    continue;
                }
                // `|| @arr` asks for SEQUENTIAL alternation: the elements in
                // their own order, first match wins
                bool seqAlt = false;
                {
                    size_t b = out.size();
                    while (b > 0 && (out[b - 1] == ' ' || out[b - 1] == '\t')) b--;
                    seqAlt = b >= 2 && out[b - 1] == '|' && out[b - 2] == '|';
                }
                if (seqAlt) {
                    std::vector<std::pair<std::string, bool>> orig;
                    for (auto& e : *v->arr()) orig.push_back({e.t == VT::Regex ? e.s.str() : e.toStr(), e.t == VT::Regex});
                    out += "[";
                    for (size_t k = 0; k < orig.size(); k++) {
                        if (k) out += "||";
                        out += orig[k].second ? spliceRegexValueFwd(orig[k].first) : quoteMetaRx(orig[k].first);
                    }
                    out += "]";
                    i = j - 1;
                    continue;
                }
                out += "[";   // no blanks inside — see the `@$aref` arm
                for (size_t k = 0; k < els.size(); k++) {
                    // `|`, not `||`: Rakudo interpolates @arr as an LTM
                    // alternation. Under the probe the two are equivalent for
                    // literals (greedy end == literal length, longest-first
                    // sort keeps ties right), but under RAKUPP_LTM=1 a `||`
                    // rewrite gave the branch ONLY the first element as its
                    // declarative prefix — `[ arrow || time ] flies` on
                    // "timeflies" pruned the whole branch (exhaustive.t 71).
                    if (k) out += "|";
                    if (rxEls.count(k)) out += spliceRegexValueFwd(els[k]);
                    else out += quoteMetaRx(els[k]);
                }
                out += "]";
                i = j - 1;
                continue;
            }
        }
        out += pat[i];
    }
    return out;
}
// `rx/$base$tail/` where $base and $tail hold REGEXES composes them. In Rakudo
// the regex closes over those variables; here a Regex value is its source text,
// so the composition is baked in when the `rx//` term is evaluated — with the
// variables that are in scope at that moment. Deferring it to match time
// re-reads the names against whatever they mean *then*, which is wrong as soon
// as the result is fed back into the same variable, as IO::Glob does when it
// folds a glob's terms into one matcher. Str-valued variables are left for the
// match-time path, where they still interpolate literally.
// The `$var` atoms of a regex SOURCE, resolved against the current scope: a
// Regex value splices as a sub-pattern, anything else interpolates as literal
// text. Repeated until stable, because a spliced regex may name more of them.
// Shared by `~~` and by every builtin that compiles a raw pattern (`.split`).
// A `:P5`/`:Perl5` adverb among the leading `:adv ` tokens the lexer bakes into
// a regex literal — the whole pattern is Perl 5 syntax.
bool isP5Pattern(const std::string& pat) {
    size_t p = 0;
    while (p < pat.size() && pat[p] == ':') {
        size_t j = p + 1;
        if (j < pat.size() && pat[j] == '!') j++;
        size_t ns = j;
        while (j < pat.size() && ascii::isalnum((unsigned char)pat[j])) j++;
        if (j == ns) return false;
        std::string name = pat.substr(ns, j - ns);
        if (j < pat.size() && pat[j] == '(') {
            int d = 0;
            while (j < pat.size()) { char c = pat[j++]; if (c == '(') d++; else if (c == ')' && --d == 0) break; }
        }
        if (j < pat.size() && pat[j] == ' ') j++;
        else if (j < pat.size()) return false;
        if (name == "P5" || name == "Perl5") return true;
        p = j;
    }
    return false;
}

// How a regex VALUE goes into another regex's pattern. Same-flavour, its source
// pastes in, grouped so an alternation inside stays contained. ACROSS flavours it
// cannot: `[a-z]+` is a P5 character class and a Raku group over `a`, `-`, `z`,
// so the foreigner goes in as a marked splice (Regex::spliceOf) that the parser
// compiles with its own front-end. Both directions, one pair of functions —
// three copies of this rule had already drifted apart.
static std::string spliceRegexValue(const std::string& src) {   // …into a RAKU pattern
    if (isP5Pattern(src)) return Regex::spliceOf(src.substr(src.find(' ') + 1), true);
    // `<~~>` means "the pattern this is written in". Pasting the TEXT would make
    // it mean the HOST's pattern, so a self-recursive regex goes in as a marked
    // splice too — the parser compiles that with its own root and the recursion
    // stays inside it (`my $re = rx/ '(' <~~>* ')' /; "(())" ~~ /^$re$/`).
    if (src.find("<~~>") != std::string::npos) return Regex::spliceOf(src, false);
    return "[ " + src + " ]";
}
static std::string spliceRegexValueFwd(const std::string& src) { return spliceRegexValue(src); }
static std::string spliceRegexValueP5(const std::string& src) { // …into a PERL 5 one
    return isP5Pattern(src) ? "(?:" + src.substr(src.find(' ') + 1) + ")"
                            : Regex::spliceOf(src, false);
}

// The regex SOURCE behind an interpolated value, for the ASSERTION forms `<$var>`
// and `<alias=$var>` — which COMPILE the value instead of matching it literally.
// A Regex hands over its own pattern text (with a `:P5` prefix peeled off; the
// flavour travels beside the source, not inside it); anything else, its Str.
static std::string rxSourceOf(const Value& v, bool& p5) {
    std::string src = v.t == VT::Regex ? v.s.str() : v.toStr();
    p5 = isP5Pattern(src);
    if (p5) src = src.substr(src.find(' ') + 1);
    return src;
}

// A STRING compiled as a pattern at run time (`<$x>`, `<{ '…' }>`) is data,
// not code: Rakudo compiles it in restricted mode, where anything that would
// RUN something — a `{…}` block or assertion, `:my`, `$( … )`, an
// interpolating string, a call with arguments, a `::( … )` dynamic name — is
// refused with X::SecurityPolicy::Eval rather than executed. A Regex value is
// code its author wrote and is never checked.
bool Interpreter::rxRestricted() {
    Value* p = tctx_.cur ? tctx_.cur->find("$?NO-MONKEY-SEE-NO-EVAL") : nullptr;
    return p && p->truthy();
}
static void rxRestrictedCheck(const std::string& src) {
    auto refuse = []() {
        throw RakuError{Value::typeObj("X::SecurityPolicy::Eval"), "Prohibited regex interpolation"};
    };
    char q = 0;
    for (size_t i = 0; i < src.size(); i++) {
        char c = src[i];
        if (c == '\\') { i++; continue; }
        if (q == '\'') { if (c == q) q = 0; continue; }
        if (q == '"') {
            if (c == '"') q = 0;
            else if (c == '$' || c == '@' || c == '%' || c == '&' || c == '{') refuse();
            continue;
        }
        if (c == '\'' || c == '"') { q = c; continue; }
        if (c == '{') refuse();
        if (c == '$' && i + 1 < src.size() && src[i + 1] == '(') refuse();
        // `<$x>` / `<@x>` would reinterpret a variable AGAIN — only one level is allowed
        if (c == '<' && i + 1 < src.size() && (src[i + 1] == '$' || src[i + 1] == '@')) refuse();
        if (c == ':' && src.compare(i, 4, ":my ") == 0) refuse();
        if (c == ':' && i + 2 < src.size() && src[i + 1] == ':' && src[i + 2] == '(') refuse();
        if (c == '}') refuse();
        if (c == '<') {
            size_t j = i + 1;
            if (j < src.size() && (src[j] == '?' || src[j] == '!' || src[j] == '.')) j++;
            size_t b = j;
            bool call = false;
            while (j < src.size() && (ascii::isalnum((unsigned char)src[j]) || src[j] == '_' ||
                                      src[j] == '-' || src[j] == ':' || src[j] == '(')) {
                if (src[j] == '(') {   // `::( … )` inside a long name, or a call
                    call = true;
                    int d = 0;
                    for (; j < src.size(); j++) { if (src[j] == '(') d++; else if (src[j] == ')' && --d == 0) { j++; break; } }
                    continue;
                }
                j++;
            }
            // `<a::b=…>` — an alias cannot be a long name
            if (j < src.size() && src[j] == '=' && src.substr(b, j - b).find("::") != std::string::npos)
                throw RakuError{Value::typeObj("X::Syntax::Regex::Alias::LongName"),
                                "Can only alias to a short name (without '::')"};
            if (call && j > b) refuse();
        }
    }
}

// What a `<{ … }>` block answered, as the PATTERN it stands for. The value is
// read exactly as `<$var>` reads a variable: a Regex is its own source (flavour
// peeled off and carried as a flag), a Str is regex source, a List is an
// alternation of its elements read the same way, longest first as `<@arr>`
// orders its members. An undefined value and the null regex are ERRORS, as in
// Rakudo: `<{ %h{$k} }>` with no such key must not quietly turn into a
// zero-width match — that accident is what issue #81 reports.
// Compiled once per distinct (flags, source) and kept for the interpreter's
// life: the matcher holds the pointer for the rest of the match, possibly on
// another thread, so nothing is ever dropped. The map grows with the distinct
// sources a program produces (`(\w+) <{ $0 }>` compiles one per capture), the
// way an EVAL cache does.
const Regex* Interpreter::dynRegexFor(const Value& v, const std::string& flags) {
    auto undefined = [](const Value& x) { return x.t == VT::Nil || x.t == VT::Any || x.t == VT::Type; };
    if (undefined(v))
        throw RakuError{Value::typeObj("X::AdHoc"),
                        "<{ … }> in a regex produced an undefined value, not a pattern"};
    std::string src;
    bool p5 = false;
    if (v.t == VT::Array && v.arr()) {
        std::vector<std::string> els;
        for (auto& e : *v.arr()) {
            if (undefined(e)) continue;
            bool ep5 = false;
            std::string es = rxSourceOf(e, ep5);
            if (es.empty()) continue;
            els.push_back(ep5 ? Regex::spliceOf(es, true) : "[ " + es + " ]");
        }
        std::stable_sort(els.begin(), els.end(),
            [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
        for (size_t k = 0; k < els.size(); k++) { if (k) src += " | "; src += els[k]; }
    }
    else src = rxSourceOf(v, p5);
    if (v.t == VT::Str && !p5 && rxRestricted()) rxRestrictedCheck(src);
    // a `$var` in the answered SOURCE names a variable of the match — a `:my`
    // declared earlier in it (`:my $a = 2; <{ '$a' }>`) — so it is read now,
    // as the pattern it stands in (a Regex value splices as one)
    if (v.t == VT::Str && !p5 && src.find('$') != std::string::npos && tctx_.cur) src = interpRegexPattern(src);
    if (src.find_first_not_of(" \t\r\n") == std::string::npos)
        throw RakuError{Value::typeObj("X::Syntax::Regex::NullRegex"), "Null regex not allowed"};
    std::string f = flags;
    if (p5) f += '5';
    std::string key = f + '\x01' + src;
    std::lock_guard<std::mutex> lock(dynRxMutex_);
    auto it = dynRxCache_.find(key);
    if (it == dynRxCache_.end()) it = dynRxCache_.emplace(key, std::make_shared<Regex>(src, f)).first;
    const Regex* re = it->second.get();
    return re->ok() && re->root() ? re : nullptr;
}

// :P5 interpolation — Perl semantics: `$var` splices its value as raw regex
// SOURCE (`my $r = '\d+'; m:P5/$r/` compiles the \d+). No quotemeta, no code
// braces, no single-quote spans; `\$`, `$` anchors and `$1` digits pass through.
std::string Interpreter::interpP5Pattern(const std::string& in) {
    if (in.find('$') == std::string::npos || !tctx_.cur) return in;
    std::string out;
    for (size_t i = 0; i < in.size(); i++) {
        if (size_t sp = Regex::spliceSpan(in, i)) { out += in.substr(i, sp); i += sp - 1; continue; }
        if (in[i] == '\\' && i + 1 < in.size()) { out += in[i]; out += in[i + 1]; i++; continue; }
        if (in[i] == '$' && i + 1 < in.size() && (ascii::isalpha((unsigned char)in[i + 1]) || in[i + 1] == '_')) {
            size_t j = i + 1;
            while (j < in.size() && (ascii::isalnum((unsigned char)in[j]) || in[j] == '_')) j++;
            if (Value* v = tctx_.cur->find("$" + in.substr(i + 1, j - i - 1))) {
                out += v->t == VT::Regex ? spliceRegexValueP5(v->s) : v->toStr();
                i = j - 1;
                continue;
            }
        }
        out += in[i];
    }
    return out;
}
thread_local std::vector<RxTempSave> g_rxTemps;

std::string Interpreter::interpRegexPattern(const std::string& in) {
    std::string pat = in;
    // `:my $y = ' yack';` / `:constant $x = 'foo';` / `:our $o = …;` /
    // `:state $z++;` — a declaration inside the pattern. Its variable is what
    // the later `$y` atoms read, so each one runs NOW in a scope of its own
    // (state ones in a scope kept per pattern) and leaves the pattern text.
    std::shared_ptr<Env> declSaved;
    if (tctx_.cur && pat.find(':') != std::string::npos) {
        std::string out; bool inSq = false; int braces = 0;
        for (size_t i = 0; i < pat.size(); i++) {
            char c = pat[i];
            if (c == '\\' && i + 1 < pat.size()) { out += c; out += pat[++i]; continue; }
            if (c == '{') braces++;
            else if (c == '}' && braces) braces--;
            else if (c == '\'' && !braces) inSq = !inSq;
            if (c == ':' && !inSq && !braces) {
                static const char* kw[] = {"my", "our", "constant", "state", "temp", "let"};
                std::string word;
                for (const char* k : kw) {
                    size_t L = std::strlen(k);
                    if (pat.compare(i + 1, L, k) == 0 && i + 1 + L < pat.size() &&
                        ascii::isspace((unsigned char)pat[i + 1 + L])) { word = k; break; }
                }
                if (!word.empty()) {
                    size_t b = i + 1 + word.size();
                    while (b < pat.size() && ascii::isspace((unsigned char)pat[b])) b++;
                    // `:my token SIGN { <[+-]> }` — a lexical rule for the rest of
                    // the pattern: registered as `my token` is, and taken out
                    if (word == "my") {
                        size_t kb = b, ke = b;
                        while (ke < pat.size() && ascii::isalpha((unsigned char)pat[ke])) ke++;
                        std::string kind = pat.substr(kb, ke - kb);
                        if ((kind == "token" || kind == "rule" || kind == "regex") &&
                            ke < pat.size() && ascii::isspace((unsigned char)pat[ke])) {
                            size_t nb = ke;
                            while (nb < pat.size() && ascii::isspace((unsigned char)pat[nb])) nb++;
                            size_t ne = nb;
                            while (ne < pat.size() && (ascii::isalnum((unsigned char)pat[ne]) || pat[ne] == '_' || pat[ne] == '-')) ne++;
                            size_t ob = ne;
                            while (ob < pat.size() && ascii::isspace((unsigned char)pat[ob])) ob++;
                            size_t e = ne > nb && ob < pat.size() && pat[ob] == '{' ? rxCodeBraceEnd(pat, ob) : 0;
                            if (e) {
                                std::string name = pat.substr(nb, ne - nb);
                                namedRegex_[name] = pat.substr(ob + 1, e - ob - 2);
                                namedRegexKind_[name] = kind;
                                if (!declSaved) {
                                    declSaved = tctx_.cur;
                                    auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
                                    tctx_.cur = env;
                                }
                                i = e - 1;
                                continue;
                            }
                        }
                    }
                    // `:temp $a = 5;` / `:let $a = 5;` — assign the OUTER variable now,
                    // so a subrule reached later reads the new value; `:temp` puts it
                    // back when the whole match is over (RxTempGuard in regexMatch)
                    if (word == "temp" || word == "let") {
                        size_t e = (b < pat.size() && pat[b] == '$') ? rxDeclEnd(pat, b) : 0;
                        if (e) {
                            std::string body = pat.substr(b, e - 1 - b);
                            size_t ne = 1;
                            while (ne < body.size() && (ascii::isalnum((unsigned char)body[ne]) || body[ne] == '_' || body[ne] == '-')) ne++;
                            std::string var = body.substr(0, ne);
                            if (Value* slot = tctx_.cur->find(var)) {
                                g_rxTemps.push_back({tctx_.cur, var, *slot, word == "let"});
                                try { evalString(body); } catch (FeatureNotBuilt&) { throw; } catch (...) {}
                            }
                            if (!declSaved) {   // (the rewritten text is kept only with a decl scope)
                                declSaved = tctx_.cur;
                                auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
                                tctx_.cur = env;
                            }
                            i = e - 1;
                            continue;
                        }
                    }
                    size_t e = (b < pat.size() && (pat[b] == '$' || pat[b] == '@' || pat[b] == '%'))
                                   ? rxDeclEnd(pat, b) : 0;
                    if (e) {
                        std::string body = pat.substr(b, e - 1 - b); // `$y = ' yack'` (no `;`)
                        size_t ne = 1;
                        while (ne < body.size() && (ascii::isalnum((unsigned char)body[ne]) || body[ne] == '_' || body[ne] == '-')) ne++;
                        std::string var = body.substr(0, ne);
                        if (!declSaved) {
                            declSaved = tctx_.cur;
                            auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
                            tctx_.cur = env;
                        }
                        try {
                            if (word == "state") {
                                static thread_local std::map<std::string, std::shared_ptr<Env>> stateEnvs;
                                auto& se = stateEnvs[in + '\x01' + var];
                                if (!se) { se = std::make_shared<Env>(); se->define(var, Value::any()); }
                                se->parent = tctx_.cur;
                                auto sv = tctx_.cur; tctx_.cur = se;
                                try { evalString(body); } catch (...) { tctx_.cur = sv; throw; }
                                tctx_.cur = sv;
                                tctx_.cur->define(var, *se->find(var));
                            }
                            else {
                                Value v = evalString(body.size() > ne ? "my " + body : "my " + body + " = Any");
                                (void)v;
                            }
                        } catch (FeatureNotBuilt&) { throw; } catch (...) {}
                        // a `my`/`our` declaration stays in the pattern too: it runs
                        // again at match time, where a `{ … }` block reads it
                        if (word == "my" || word == "our") out += pat.substr(i, e - i);
                        i = e - 1;
                        continue;
                    }
                }
            }
            out += c;
        }
        if (declSaved) pat = out;
    }
    struct DeclRestore { std::shared_ptr<Env>& cur; std::shared_ptr<Env> saved;
        ~DeclRestore() { if (saved) cur = saved; } } declRestore{tctx_.cur, declSaved};
    for (int pass = 0; pass < 8 && pat.find('$') != std::string::npos && tctx_.cur; pass++) {
        std::string out;
        bool inSq = false; // inside '…': a literal span — $vars do NOT interpolate there
        bool inDq = false; // inside "…": a qq span — $vars AND {…} interpolate
        int braces = 0;    // inside `{…}` / `<?{…}>` / `**{…}`: CODE, not pattern — a
                           // `$var` there is the block's own variable and must reach the
                           // block verbatim, or `{ $c = $¢ }` arrives as `1 = $¢`.
        for (size_t i = 0; i < pat.size(); i++) {
            // an already-spliced sub-pattern is opaque: its source is another
            // regex's text and must not be reread as this one's
            if (size_t sp = Regex::spliceSpan(pat, i)) { out += pat.substr(i, sp); i += sp - 1; continue; }
            // `:my $y = …;` is CODE to its `;` — its `$y` is the declaration itself
            if (pat[i] == ':' && !braces && !inSq && !inDq &&
                (pat.compare(i, 4, ":my ") == 0 || pat.compare(i, 5, ":our ") == 0)) {
                if (size_t e = rxDeclEnd(pat, i)) { out += pat.substr(i, e - i); i = e - 1; continue; }
            }
            if (pat[i] == '\\' && i + 1 < pat.size()) { out += pat[i]; out += pat[i + 1]; i++; continue; }
            if (pat[i] == '<' && i + 1 < pat.size() && (pat[i + 1] == '[' || pat[i + 1] == '-')) {
                // a character class: copy it through so a literal `{` inside cannot
                // open a false code span
                size_t j = i;
                while (j + 1 < pat.size() && !(pat[j] == ']' && pat[j + 1] == '>')) out += pat[j++];
                while (j < pat.size() && pat[j] != '>') out += pat[j++];
                if (j < pat.size()) out += pat[j];
                i = j; continue;
            }
            // inside a DOUBLE-quoted span, `{…}` is qq-interpolation, not a
            // code block: evaluate now and splice the Str (escaped for the
            // quoted-literal parser) — /"warning!{$nl}"/ is Test::Output's
            // spelling, and Rakudo matches it
            if (pat[i] == '"' && !braces && !inSq) { inDq = !inDq; out += pat[i]; continue; }
            if (inDq) {
                if (pat[i] == '{') {
                    int depth = 0;
                    size_t j = i;
                    for (; j < pat.size(); j++) {
                        if (pat[j] == '{') depth++;
                        else if (pat[j] == '}' && --depth == 0) { j++; break; }
                    }
                    if (depth == 0) {
                        Value v = evalString(pat.substr(i + 1, j - i - 2));
                        for (char c : v.toStr()) {
                            if (c == '\\' || c == '"') out += '\\';
                            out += c;
                        }
                        i = j - 1;
                        continue;
                    }
                }
                // $vars fall through to the interpolation below, everything
                // else is literal text of the quoted span
                if (pat[i] != '$') { out += pat[i]; continue; }
            }
            if (pat[i] == '{') { braces++; out += pat[i]; continue; }
            if (pat[i] == '}') { if (braces) braces--; out += pat[i]; continue; }
            if (pat[i] == '\'' && !braces && !inDq) { inSq = !inSq; out += pat[i]; continue; }
            if (inSq || braces) { out += pat[i]; continue; }
            // `<::( expr )>` — a SYMBOLIC subrule: the expression names the rule
            if (pat.compare(i, 4, "<::(") == 0) {
                int depth = 0;
                size_t j = i + 3;
                for (; j < pat.size(); j++) {
                    if (pat[j] == '(') depth++;
                    else if (pat[j] == ')' && --depth == 0) break;
                }
                if (depth == 0 && j + 1 < pat.size() && pat[j + 1] == '>') {
                    out += "<" + evalString(pat.substr(i + 4, j - i - 4)).toStr() + ">";
                    i = j + 1;
                    continue;
                }
            }
            // `$( … )` — an expression, matched as its Str. Only an IDENTIFIER
            // was interpolated here, so `$($x.bytes)` was left in the pattern
            // verbatim: the `$` then read as the end anchor and the parenthesis
            // as a group, and the match failed for reasons having nothing to do
            // with the value. It never errored — it just quietly matched
            // something else, which is the worst way for a pattern to be wrong.
            if (pat[i] == '$' && i + 1 < pat.size() && pat[i + 1] == '(') {
                int depth = 0;
                size_t j = i + 1;
                for (; j < pat.size(); j++) {
                    if (pat[j] == '(') depth++;
                    else if (pat[j] == ')' && --depth == 0) { j++; break; }
                }
                if (depth == 0) {
                    Value v = evalString(pat.substr(i, j - i));   // the whole `$( … )`
                    out += v.t == VT::Regex ? spliceRegexValue(v.s) : quoteMetaRx(v.toStr());
                    i = j - 1;
                    continue;
                }
            }
            // `$^name` (and `$:name`) is a PLACEHOLDER — the enclosing block binds it
            // under the bare name, so the twigil is skipped for the lookup.
            // IO::Glob builds its alternation regexes as `@alts.map({ rx/$base$^alt/ })`.
            size_t tw = (pat[i] == '$' && i + 2 < pat.size() && (pat[i + 1] == '^' || pat[i + 1] == ':') &&
                         (ascii::isalpha((unsigned char)pat[i + 2]) || pat[i + 2] == '_')) ? 1 : 0;
            if (pat[i] == '$' && i + 1 + tw < pat.size() && !(i > 0 && pat[i - 1] == '@') &&   // `@$aref` is the array pass's
                (ascii::isalpha((unsigned char)pat[i + 1 + tw]) || pat[i + 1 + tw] == '_')) {
                size_t j = i + 1 + tw;
                // kebab-case names too: `-`/`'` + letter continues the identifier
                // (`/^ \\h* $comment-char /` — Text::Utils), as rxInterpArrays does
                while (j < pat.size() && (ascii::isalnum((unsigned char)pat[j]) || pat[j] == '_' ||
                       ((pat[j] == '-' || pat[j] == '\'') && j + 1 < pat.size() &&
                        ascii::isalpha((unsigned char)pat[j + 1])))) j++;
                Value* v = tctx_.cur->find("$" + pat.substr(i + 1 + tw, j - i - 1 - tw));
                // POSITION decides the reading (issue #15): `<$p>` compiles the
                // string AS A REGEX, a bare `$p` matches it LITERALLY. The
                // assertion form is `<` immediately before and `>` right after
                // the variable — anything else keeps the literal meaning.
                bool inAngle = !out.empty() && out.back() == '<' && j < pat.size() && pat[j] == '>';
                if (v && inAngle) {
                    out.pop_back();                 // drop the '<'
                    // A CALL, not a paste: the value matches in its own capture
                    // frame, so an inner `(…)` stays the callee's `$0` and never
                    // renumbers onto the host's — Rakudo discards it entirely for
                    // the unaliased form, since nothing names the sub-match.
                    bool vp5 = false;
                    std::string vsrc = rxSourceOf(*v, vp5);
                    if (v->t == VT::Str && !vp5 && rxRestricted()) rxRestrictedCheck(vsrc);
                    // A `$name` in that string is compiled HERE, so it names a
                    // variable of this scope, which must exist — Rakudo reports
                    // X::Undeclared for `$var = '$i'; /<$var>/` with no `$i`.
                    // (Code in the string may declare its own; that is left alone.)
                    if (v->t == VT::Str && !vp5 && vsrc.find('{') == std::string::npos &&
                        vsrc.find(":my") == std::string::npos && vsrc.find(":our") == std::string::npos) {
                        char q = 0;
                        for (size_t a = 0; a < vsrc.size(); a++) {
                            char c = vsrc[a];
                            if (c == '\\') { a++; continue; }
                            if (q) { if (c == q) q = 0; continue; }
                            if (c == '\'') { q = c; continue; }
                            if (c != '$' || a + 1 >= vsrc.size() ||
                                !(ascii::isalpha((unsigned char)vsrc[a + 1]) || vsrc[a + 1] == '_')) continue;
                            size_t e = a + 1;
                            while (e < vsrc.size() && (ascii::isalnum((unsigned char)vsrc[e]) || vsrc[e] == '_' ||
                                   (vsrc[e] == '-' && e + 1 < vsrc.size() && ascii::isalpha((unsigned char)vsrc[e + 1])))) e++;
                            std::string vn = "$" + vsrc.substr(a + 1, e - a - 1);
                            if (!tctx_.cur->find(vn)) throwUndeclaredVar(vn);
                            a = e - 1;
                        }
                    }
                    out += Regex::subSpliceOf("", vsrc, vp5);
                    i = j;                          // skip the '>'
                    continue;
                }
                // `<alias=$var>` — the ALIASED assertion form: the value compiles
                // as a regex and its match captures under `alias`. Rewritten to the
                // equivalent named-capture group `$<alias>=[ … ]`. Without this the
                // `$var` fell through to the literal reading and left
                // `<alias=value>` — a call to an unknown subrule, which matched as
                // a zero-width no-op, i.e. ALWAYS. Sparrow6's whole check engine
                // sits on this spelling (`$data.comb(/<mymatch=$pattern>/,:match)`),
                // so every `regexp:` check-file line passed against any output.
                if (v && j < pat.size() && pat[j] == '>' && !out.empty() && out.back() == '=') {
                    size_t b = out.size() - 1;      // the '='
                    size_t e = b;                   // scan the alias identifier leftwards
                    while (e > 0 && (ascii::isalnum((unsigned char)out[e - 1]) || out[e - 1] == '_' || out[e - 1] == '-')) e--;
                    if (e > 0 && e < b && out[e - 1] == '<') {
                        std::string alias = out.substr(e, b - e);
                        out.erase(e - 1);           // drop `<alias=`
                        // The aliased call captures the CALLEE's frame under
                        // `alias`. Rewriting it to `$<alias>=[ … ]` instead only
                        // grouped: the callee's `(\S+)` became the host's `$0` and
                        // `$<alias>` reported the whole span, so Sparrow6 read
                        // "ABCDCBA" where Rakudo reads "BCDCB".
                        bool vp5 = false;
                        std::string vsrc = rxSourceOf(*v, vp5);
                        out += Regex::subSpliceOf(alias, vsrc, vp5);
                        i = j;                      // skip the '>'
                        continue;
                    }
                }
                // A variable holding a REGEX splices as a sub-pattern, not as
                // literal text — `rx/$base$match/` composes two regexes, which is
                // how IO::Glob assembles a glob out of per-term matchers. Only a
                // Str value keeps the quote-it-literally reading.
                if (v && v->t == VT::Regex) {
                    out += spliceRegexValue(v->s);
                    i = j - 1;
                    continue;
                }
                if (v && v->t == VT::Hash)
                    throw RakuError{Value::typeObj("X::Syntax::Reserved"),
                                    "The use of hash variables in regexes is reserved"};
                // an undefined value matches nothing at all (Rakudo warns and
                // interpolates the empty string, but never matches with it)
                if (v && (v->t == VT::Any || v->t == VT::Nil || v->t == VT::Type)) {
                    out += "<!>"; i = j - 1; continue;
                }
                // a junction is an alternation of its eigenstates
                if (v && v->t == VT::Array && v->arr() &&
                    (v->enumName == "any" || v->enumName == "one")) {
                    out += "[ ";
                    for (size_t k = 0; k < v->arr()->size(); k++) {
                        if (k) out += " | ";
                        out += quoteMetaRx((*v->arr())[k].toStr());
                    }
                    out += " ]";
                    i = j - 1; continue;
                }
                if (v) {
                    // An ordinary variable's text is not part of the DECLARATIVE
                    // prefix an alternation ranks by — only a constant's is (its
                    // value exists when the regex is compiled): an empty `{}`
                    // ends the prefix where the variable begins
                    const std::string vname = "$" + pat.substr(i + 1 + tw, j - i - 1 - tw);
                    if (!inDq && pat.find('|') != std::string::npos && !constantNames_.count(vname)) out += "{}";
                    out += quoteMetaRx(v->toStr()); i = j - 1; continue;
                }
            }
            out += pat[i];
        }
        if (out == pat) break;
        pat = out;
    }
    return pat;
}

std::string Interpreter::spliceRegexVars(const std::string& pat) {
    if (pat.find('$') == std::string::npos || !tctx_.cur) return pat;
    std::string out;
    bool inSq = false, sawRegex = false;
    int braces = 0;
    for (size_t i = 0; i < pat.size(); i++) {
        char c = pat[i];
        if (c == '\\' && i + 1 < pat.size()) { out += c; out += pat[i + 1]; i++; continue; }
        if (c == '{') { braces++; out += c; continue; }
        if (c == '}') { if (braces) braces--; out += c; continue; }
        if (c == '\'' && !braces) { inSq = !inSq; out += c; continue; }
        if (inSq || braces) { out += c; continue; }
        size_t tw = (c == '$' && i + 2 < pat.size() && (pat[i + 1] == '^' || pat[i + 1] == ':') &&
                     (ascii::isalpha((unsigned char)pat[i + 2]) || pat[i + 2] == '_')) ? 1 : 0;
        if (c == '$' && i + 1 + tw < pat.size() && !(i > 0 && pat[i - 1] == '@') &&
            (ascii::isalpha((unsigned char)pat[i + 1 + tw]) || pat[i + 1 + tw] == '_')) {
            size_t j = i + 1 + tw;
            // kebab-case names continue through `-`/`'` + letter (same rule as above)
            while (j < pat.size() && (ascii::isalnum((unsigned char)pat[j]) || pat[j] == '_' ||
                   ((pat[j] == '-' || pat[j] == '\'') && j + 1 < pat.size() &&
                    ascii::isalpha((unsigned char)pat[j + 1])))) j++;
            // `<$p>` is the assertion form: it already compiles the value as a
            // regex at match time, so leave it alone.
            bool inAngle = !out.empty() && out.back() == '<' && j < pat.size() && pat[j] == '>';
            Value* v = tctx_.cur->find("$" + pat.substr(i + 1 + tw, j - i - 1 - tw)); // `$^x` binds as `$x`
            if (v && !inAngle) {
                if (v->t == VT::Regex) {
                    // An `@array` atom inside the spliced value belongs to the
                    // scope that value was WRITTEN in. Pasting the source text
                    // alone would leave the name to be resolved HERE instead,
                    // where it usually does not exist — which is how
                    // Path::Finder's `{a,b}` and `[a-z]` matchers, built as
                    // `/@list/` inside a grammar action and then folded
                    // together by a `reduce`, came out matching nothing.
                    std::string src = v->s.str();
                    if (v->ext() && src.find('@') != std::string::npos && !isP5Pattern(src)) {
                        auto savedOuter = tctx_.cur;
                        tctx_.cur = std::static_pointer_cast<Env>(v->ext());
                        src = rxInterpArrays(src);
                        tctx_.cur = savedOuter;
                    }
                    out += spliceRegexValue(src); sawRegex = true;
                }
                else out += quoteMetaRx(v->toStr());
                i = j - 1;
                continue;
            }
        }
        out += c;
    }
    // Only a pattern that actually composes another REGEX is baked. Everything
    // else keeps the existing behaviour, where a `$var` atom is read at match
    // time — that is what a plain `rx/$word/` over a changing $word expects.
    return sawRegex ? out : pat;
}
// The `<NAME>` subrule resolver over the lexical `my regex/token/rule` table
// (and the qualified `<Grammar::rule>` form), built once and shared by every
// entry point that compiles a raw pattern — `~~` and the occurrence scanner
// behind subst/comb/split/match alike. It used to exist only on the `~~` path,
// so `$str.subst(&rule, …)` compiled `<name>` as an unknown assertion and
// matched it as zero-width: a pattern that matched perfectly under `~~`
// quietly matched nothing under .subst. `lexNames` and `useHooks` are the
// caller's and must outlive the resolver, which holds them by reference.
void Interpreter::lexSubResolver(SubResolver& resolver, std::set<std::string>& lexNames,
                                 const GrammarHooks*& useHooks) {
    for (auto& kv : namedRegex_) lexNames.insert(kv.first);
    resolver = [&](const std::string& name, const std::string& subj, long pos, RxMatch& out) -> bool {
        if (name == "ws") { long p = pos; while (p < (long)subj.size() && ascii::isspace((unsigned char)subj[p])) p++; out.from = pos; out.to = p; out.matched = true; return true; }
        // `<Grammar::rule>` — a QUALIFIED rule reference inside a plain regex.
        // URI declares `subset Scheme of Str where /^ [ '' || <IETF::RFC_Grammar::URI::scheme> ] $/`,
        // and without this the name resolved to nothing, took the lenient
        // zero-width branch below, and the subset accepted only the empty string.
        {
            auto qs = name.rfind("::");
            if (qs != std::string::npos && qs > 0) {
                std::string gname = name.substr(0, qs), rname = name.substr(qs + 2);
                auto cit = classes_.find(gname);
                if (cit == classes_.end()) cit = classes_.find(resolveClassAlias(gname));
                // …and a package nothing declares is an error, not an empty match
                // (Rakudo refuses it while compiling)
                if (cit == classes_.end() && !(tctx_.cur && tctx_.cur->find(gname)))
                    throw RakuError{Value::typeObj("X::AdHoc"),
                                    "Could not locate compile-time value for symbol " + gname};
                if (cit != classes_.end() && cit->second && cit->second->findRule(rname)) {
                    Value m = grammarParse(cit->second.get(), subj.substr(pos),
                                           /*subparse=*/true, rname, Value());
                    if (!isDefined(m)) return false;
                    out.from = pos;
                    out.to = pos + (long)m.s.size();   // Match.s is the matched text
                    out.matched = true;
                    // Carry the rule's OWN captures back as spans. Returning only
                    // the extent threw away everything the rule matched inside —
                    // URI::Path reads `$path<segment>` / `$path<segment-nz>` off
                    // exactly this match to build its segment list, and without
                    // them every mutated path had one empty segment.
                    ParseNode root;
                    matchValueToNode(m, pos, root);
                    out.named = root.named;
                    if (root.kids) out.children = *root.kids;
                    if (root.listNames) out.listNames = root.listNames;
                    return true;
                }
            }
        }
        // `<&foo('a')>` / `<&foo: 'a'>` — a lexical regex WITH A SIGNATURE,
        // `my regex foo($s) { $s }`, is registered under its whole `foo($s)`
        // spelling: bind the call's arguments to its parameters in a scope of
        // their own, where the body's `$s` atoms read them, and match it there
        {
            size_t us = name.find('\x1f');
            const std::string base = us == std::string::npos ? name : name.substr(0, us);
            auto sit = namedRegex_.lower_bound(base + "(");
            if (!base.empty() && sit != namedRegex_.end() && sit->first.compare(0, base.size() + 1, base + "(") == 0) {
                std::vector<std::string> params;
                const std::string& sig = sit->first;
                for (size_t q = base.size(); q < sig.size(); q++) {
                    if (sig[q] != '$' && sig[q] != '@' && sig[q] != '%') continue;
                    size_t e = q + 1;
                    while (e < sig.size() && (ascii::isalnum((unsigned char)sig[e]) || sig[e] == '_' || sig[e] == '-')) e++;
                    if (e > q + 1) params.push_back(sig.substr(q, e - q));
                    q = e - 1;
                }
                ValueList av;
                if (us != std::string::npos) {
                    Value l = evalString("(" + name.substr(us + 1) + ",)");
                    if (l.arr()) av = *l.arr(); else av.push_back(l);
                }
                auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
                for (size_t k = 0; k < params.size(); k++) env->define(params[k], k < av.size() ? av[k] : Value::any());
                struct CurRestore { std::shared_ptr<Env>& cur; std::shared_ptr<Env> saved;
                    ~CurRestore() { cur = saved; } } curRestore{tctx_.cur, tctx_.cur};
                tctx_.cur = env;
                const std::string& kind = namedRegexKind_[sig];
                std::string flags = kind == "rule" ? "sr" : kind == "token" ? "r" : "";
                auto sub = compileRegexCached(rxInterpArrays(interpRegexPattern(sit->second)), flags);
                return sub->matchAt(subj, pos, out, resolver, &lexNames, useHooks);
            }
        }
        auto it = namedRegex_.find(name);
        // An IMPORTED `token`/`rule`/`regex` is not in namedRegex_ — that map
        // holds what THIS unit declared, and an exported one arrives as a Regex
        // VALUE bound to `&name` in the importing scope. Without this it fell to
        // the lenient branch below and the subrule matched the EMPTY STRING:
        // `$line ~~ / ^ "1.2.3.4 - foo " <timefmt> … /` in Apache::LogFormat's
        // own test said True while matching sixteen characters and none of the
        // rest, which is a silent wrong answer rather than a refusal.
        if (it == namedRegex_.end() && tctx_.cur) {
            if (Value* cv = tctx_.cur->find("&" + name)) {
                std::string pat, kind;
                if (cv->t == VT::Regex) { pat = cv->s.str(); kind = cv->hashKind.str(); }
                else if (cv->t == VT::Code && cv->code()) {
                    auto rit = regexRoutineSrc_.find(cv->code());
                    if (rit != regexRoutineSrc_.end()) { pat = rit->second.first; kind = rit->second.second; }
                }
                if (!pat.empty()) {
                    const std::string f = kind == "rule" ? "sr"       // sigspace + ratchet
                                        : kind == "token" ? "r"        // ratchet
                                        : "";                          // backtracking
                    auto sub = compileRegexCached(rxInterpArrays(pat), f);
                    return sub->matchAt(subj, pos, out, resolver, &lexNames, useHooks);
                }
            }
        }
        // an unknown name stays a lenient zero-width match: Rakudo built-ins we lack
        // (`<commit>`, `<same>`) arrive here too, and a throw killed S05-mass/rx.t at
        // test 18 of 756 (Grand Review E, withdrawn; ledger L8 F27)
        // …but a name that is no built-in rule at all is a missing METHOD, as in
        // Rakudo: `/<nosuchrule>/` dies naming it
        if (it == namedRegex_.end()) {
            static const std::set<std::string> kLenient = {
                "commit", "same", "prior", "at", "cut", "fail", "null", "before", "after", "ww", "wb",
                "ident", "print", "graph", "blank", "cntrl", "punct", "xdigit", "upper", "lower", "alpha",
                "digit", "alnum", "space", "ws", "sym", "alpha-num", "longest", "LTM", "MATCH", "ACCEPTS",
                "WHAT", "WHO", "HOW", "WHICH", "Str", "Bool", "gist", "orig", "from", "to", "pos", "chars", "new"};
            bool plain = !name.empty() && ascii::isalpha((unsigned char)name[0]);
            for (char ch : name) if (!(ascii::isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.')) plain = false;
            if (!kLenient.count(name) && plain && !tctx_.cur->find("&" + name))
                throw RakuError{Value::typeObj("X::Method::NotFound"),
                                "No such method '" + name + "' for invocant of type 'Match'"};
            out.from = pos; out.to = pos; out.matched = true; return true;
        }
        const std::string& kind = namedRegexKind_[name];
        std::string flags = kind == "rule" ? "sr"       // rule:  sigspace + ratchet
                          : kind == "token" ? "r"        // token: ratchet (no backtracking)
                          : "";                          // regex: backtracking, no sigspace
        // …and an `@array` atom in the body is an alternation of its elements,
        // resolved when the subrule is REACHED (the array may have grown since
        // the declaration), exactly as for a regex matched directly.
        // …and a `$var` atom reads the variable as it is NOW (`my $a = 1;
        // my regex ma { $a $a }` matched through `<ma>` read nothing)
        auto sub = compileRegexCached(rxInterpArrays(it->second.find('$') != std::string::npos
                                                         ? interpRegexPattern(it->second) : it->second), flags);
        // useHooks: a `my regex` body still runs its {…} blocks / <?{…}>
        return sub->matchAt(subj, pos, out, resolver, &lexNames, useHooks);
    };
}
// tr/from/to/ is Str.trans(from => to) with its adverbs (:d/:c/:s) passed
// along — Rakudo builds it the same way, so the replacement cycles and
// :delete/:complement/:squash all come from the one implementation. The
// pattern arrives as "\x01" + ":adv " … + raw.
std::string trApply(Interpreter& I, const std::string& subj, const std::string& pat,
                           const std::string& repl) {
    std::string from = pat.substr(1);
    ValueList args;
    std::vector<Value> advs;
    while (from.size() > 2 && from[0] == ':' && ascii::isalpha((unsigned char)from[1])) {
        size_t e = 1;
        while (e < from.size() && (ascii::isalnum((unsigned char)from[e]) || from[e] == '-')) e++;
        if (e >= from.size() || from[e] != ' ') break;
        std::string nm = from.substr(1, e - 1);
        Value p = Value::pair(nm, Value::boolean(true)); p.namedArg = true;
        advs.push_back(p);
        from = from.substr(e + 1);
    }
    auto unesc = [](const std::string& x) {
        std::string o;
        for (size_t i = 0; i < x.size(); i++) {
            if (x[i] == '\\' && i + 1 < x.size()) {
                char c = x[++i];
                if ((c == 'x' || c == 'o') && i + 1 < x.size()) { // \x20 / \o40 / \x[…]
                    bool br = x[i + 1] == '[';
                    size_t j = i + 1 + (br ? 1 : 0), st = j;
                    while (j < x.size() && (c == 'x' ? ascii::isxdigit((unsigned char)x[j]) : (x[j] >= '0' && x[j] <= '7'))) j++;
                    if (j > st) {
                        o += cpToUtf8((uint32_t)std::stoul(x.substr(st, j - st), nullptr, c == 'x' ? 16 : 8));
                        i = (br && j < x.size() && x[j] == ']') ? j : j - 1;
                        continue;
                    }
                }
                o += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
            } else o += x[i];
        }
        return o;
    };
    args.push_back(Value::pair(unesc(from), Value::str(unesc(repl))));
    for (auto& a : advs) args.push_back(a);
    return I.methodCall(Value::str(subj), "trans", args).toStr();
}

// ---- UTF-8 / grapheme helpers for :samemark / :ignoremark ----
static std::vector<uint32_t> smDecode(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i]; uint32_t cp; int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; n = 4; }
        else { cp = c; n = 1; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
        out.push_back(cp); i += n;
    }
    return out;
}
static std::string smEncode(uint32_t cp) {
    std::string r;
    if (cp < 0x80) r += (char)cp;
    else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3F)); r += (char)(0x80 | (cp & 0x3F)); }
    else { r += (char)(0xF0 | (cp >> 18)); r += (char)(0x80 | ((cp >> 12) & 0x3F)); r += (char)(0x80 | ((cp >> 6) & 0x3F)); r += (char)(0x80 | (cp & 0x3F)); }
    return r;
}
static uint32_t smLower(uint32_t c) {
    if (c < 128) return ascii::tolower((int)c);
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7) ) return c + 0x20; // Latin-1 uppercase block
    if ((c & 1) == 0 && ((c >= 0x100 && c <= 0x17F) || (c >= 0x1E00 && c <= 0x1EFF))) return c + 1; // Latin Ext-A/Additional even=upper
    return c;
}
static uint32_t smUpper(uint32_t c) {
    if (c < 128) return ascii::toupper((int)c);
    if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 0x20;
    if ((c & 1) == 1 && ((c >= 0x100 && c <= 0x17F) || (c >= 0x1E00 && c <= 0x1EFF))) return c - 1;
    return c;
}
// Grapheme-aligned case (:samecase) and/or combining-mark (:samemark) transfer:
// each replacement grapheme takes the case and/or the combining marks of the
// positionally aligned grapheme of the match; the last seen case carries on.
static std::string applyCaseMark(const std::string& orig, const std::string& repl, bool doCase, bool doMark) {
    using namespace rakupp;
    auto split = [](const std::vector<uint32_t>& cps) {
        std::vector<std::vector<uint32_t>> gs;
        for (size_t i = 0; i < cps.size();) {
            std::vector<uint32_t> g; g.push_back(cps[i++]);
            while (i < cps.size() && uniCombiningClass(cps[i]) != 0) g.push_back(cps[i++]);
            gs.push_back(g);
        }
        return gs;
    };
    auto og = split(smDecode(orig));
    struct GI { uint32_t base; bool hasCase, upper; std::vector<uint32_t> marks; };
    std::vector<GI> oi;
    for (auto& g : og) {
        auto nfd = uniNormalize(g, 0); GI gi{}; gi.base = nfd.empty() ? 0 : nfd[0];
        for (size_t k = 1; k < nfd.size(); k++) if (uniCombiningClass(nfd[k]) != 0) gi.marks.push_back(nfd[k]);
        gi.hasCase = smLower(gi.base) != smUpper(gi.base);
        gi.upper = gi.hasCase && gi.base == smUpper(gi.base);
        oi.push_back(gi);
    }
    auto rg = split(smDecode(repl));
    std::string out; bool lastUpper = false, haveLast = false;
    for (size_t i = 0; i < rg.size(); i++) {
        std::vector<uint32_t> g = rg[i];
        const GI* o = i < oi.size() ? &oi[i] : nullptr;
        if (doCase) {
            if (o && o->hasCase) { lastUpper = o->upper; haveLast = true; }
            if (haveLast) g[0] = lastUpper ? smUpper(g[0]) : smLower(g[0]);
        }
        // past the end of the match the LAST grapheme's marks carry on, as its case does
        const GI* om = o ? o : (oi.empty() ? nullptr : &oi.back());
        if (doMark && om) for (uint32_t mk : om->marks) g.push_back(mk);
        for (uint32_t cp : uniNormalize(g, 1)) out += smEncode(cp);
    }
    return out;
}
// …and under :sigspace WORD BY WORD: each word of the replacement aligns with
// the word of the match in its position (the last one past the end), so
// `s:mm:s/a+ o+/OOOOO UUU/` over "aääa öoöö" is "OÖÖOO ÜUÜ"
static std::string applyCaseMarkWords(const std::string& orig, const std::string& repl, bool doCase, bool doMark) {
    std::vector<std::string> ow;
    for (size_t i = 0; i < orig.size();) {
        while (i < orig.size() && ascii::isspace((unsigned char)orig[i])) i++;
        size_t b = i;
        while (i < orig.size() && !ascii::isspace((unsigned char)orig[i])) i++;
        if (i > b) ow.push_back(orig.substr(b, i - b));
    }
    if (ow.empty()) return applyCaseMark(orig, repl, doCase, doMark);
    std::string out; size_t wi = 0;
    for (size_t i = 0; i < repl.size();) {
        if (ascii::isspace((unsigned char)repl[i])) { out += repl[i++]; continue; }
        size_t b = i;
        while (i < repl.size() && !ascii::isspace((unsigned char)repl[i])) i++;
        out += applyCaseMark(ow[wi < ow.size() ? wi : ow.size() - 1], repl.substr(b, i - b), doCase, doMark);
        wi++;
    }
    return out;
}

// `:samecase` — each replacement character takes the case of the positionally
// aligned character of the match (`oO` → `au` gives `aU`); past the end of the
// match the LAST character decides. A source character with no case (a space,
// a digit) leaves the replacement character as it is — Rakudo's rule, so
// `"a b"` over `FOO` is `fOo`.
static std::string applySamecase(const std::string& orig, const std::string& repl) {
    if (orig.empty()) return repl;
    std::string r = repl;
    for (size_t i = 0; i < r.size(); i++) {
        unsigned char oc = orig[i < orig.size() ? i : orig.size() - 1];
        if (!ascii::isalpha((unsigned char)r[i])) continue;
        if (ascii::isupper(oc)) r[i] = ascii::toupper((unsigned char)r[i]);
        else if (ascii::islower(oc)) r[i] = ascii::tolower((unsigned char)r[i]);
    }
    return r;
}
// …and under `:sigspace` it works WORD by word: the n-th word of the
// replacement takes its case from the n-th word of the match (the last one
// once the match runs out of words), whitespace untouched.
static std::string applySamecaseWords(const std::string& orig, const std::string& repl) {
    std::vector<std::string> ow;
    for (size_t i = 0; i < orig.size(); ) {
        while (i < orig.size() && ascii::isspace((unsigned char)orig[i])) i++;
        size_t b = i;
        while (i < orig.size() && !ascii::isspace((unsigned char)orig[i])) i++;
        if (i > b) ow.push_back(orig.substr(b, i - b));
    }
    if (ow.empty()) return applySamecase(orig, repl);
    std::string out; size_t wn = 0;
    for (size_t i = 0; i < repl.size(); ) {
        if (ascii::isspace((unsigned char)repl[i])) { out += repl[i++]; continue; }
        size_t b = i;
        while (i < repl.size() && !ascii::isspace((unsigned char)repl[i])) i++;
        out += applySamecase(ow[wn < ow.size() ? wn : ow.size() - 1], repl.substr(b, i - b));
        wn++;
    }
    return out;
}

// `target ~~ s/pat/repl/` as one runtime call — mirrors the evalBinary SubstLit
// branch so native codegen can compile substitutions instead of bailing to bundle.
Value Interpreter::substApply(Value* target, const std::string& pattern, const std::string& repl, bool nonMut) {
    Value subj = target ? *target : Value::str("");
    if (isTrSubst(pattern)) { // tr/from/to/ — transliteration, returns a StrDistance
        std::string before = subj.toStr(), out = trApply(*this, before, pattern, repl);
        if (target && !nonMut) *target = Value::str(out);
        if (nonMut) return Value::str(out);
        Value sd = Value::makeHash(); sd.hashKind = "StrDistance";
        (*sd.hash())["before"] = Value::str(before);
        (*sd.hash())["after"] = Value::str(out);
        return sd;
    }
    long nsub = 0; ValueList noArgs; Value mres;
    std::string out = substSelect(subj.toStr(), pattern, nullptr, noArgs, nsub, false, &repl, &mres);
    if (nonMut) return Value::str(out); // S/// : return the new string, leave the target intact
    if (target) *target = Value::str(out);
    return mres;                        // s/// returns the Match / List of matches
}

GrammarHooks Interpreter::codeAssertHooks() {
    GrammarHooks h;
    h.assertPass = [this](const std::string& code, long, long,
                          const GrammarHooks::NamedMap&, const GrammarHooks::ParamMap&) -> bool {
        try { return evalString(code).truthy(); }
        catch (FeatureNotBuilt&) { throw; }   // a SLIM stub fired: loud, never a silent pass
        catch (...) { return false; }
    };
    // …and `<{ … }>` evaluates the same way: no cursor, since these callers
    // never had one for the assertion either. A `die` inside leaves the match.
    h.dynRule = [this](const std::string& code, long, long, const GrammarHooks::NamedMap&,
                       const std::vector<std::pair<long, long>>&, const GrammarHooks::ParamMap&,
                       const std::string& flags) -> const Regex* {
        return dynRegexFor(evalString(code), flags);
    };
    return h;
}
// …and a `<{ … }>` pattern block arms the same hook set (codeAssertHooks
// carries dynRule beside assertPass), so every site that gates on this sees it.
bool Interpreter::patHasCodeAssert(const std::string& pat) {
    return pat.find("?{") != std::string::npos || pat.find("!{") != std::string::npos ||
           pat.find("<{") != std::string::npos;
}

// One capture's Match, WITH its own capture tree under it. A capture that
// matched through a subrule — or through a capture-scoping group, where
// `$<header>=( $<lang>=… )` puts `lang` inside `header` — carries nested
// captures, and a builder that keeps only its span answers Nil for
// `$<header><lang>`. Shared by every Match builder so they cannot disagree.
void Interpreter::matchValueToNode(const Value& mv, long offset, ParseNode& node) {
    node.from = offset + mv.rFrom();
    node.to   = offset + mv.rTo();
    // positional captures: one span each (a list-valued one keeps its last)
    if (mv.arr())
        for (auto& p : *mv.arr()) {
            const Value* one = &p;
            if (p.t == VT::Array && p.arr() && !p.arr()->empty()) one = &p.arr()->back();
            if (one->t == VT::Match) node.caps.push_back({offset + one->rFrom(), offset + one->rTo()});
            else node.caps.push_back({-1, -1});
        }
    if (!mv.hash()) return;
    auto kids = std::make_shared<ChildMap>();
    auto lists = std::make_shared<std::set<std::string>>();
    for (auto& kv : *mv.hash()) {
        auto addOne = [&](const Value& one) {
            if (one.t != VT::Match) return;
            ParseNode child; child.name = kv.first;
            matchValueToNode(one, offset, child);
            node.named[kv.first] = {child.from, child.to};
            (*kids)[kv.first].push_back(std::move(child));
        };
        if (kv.second.t == VT::Array && kv.second.arr()) {
            lists->insert(kv.first);          // a quantified capture stays a list
            for (auto& e : *kv.second.arr()) addOne(e);
        }
        else addOne(kv.second);
    }
    if (!kids->empty()) node.kids = kids;
    if (!lists->empty()) node.listNames = lists;
}

Value Interpreter::matchFromNode(const ParseNode& c, const std::string& subject,
                                 const std::shared_ptr<std::string>& orig) {
    Value cv = Value::matchVal(subject.substr(c.from, c.to - c.from), c.from, c.to);
    // `.orig`/.prematch/.postmatch read the WHOLE subject when the builder has
    // it to share; the callers that never did keep not doing it.
    if (orig) cv.extM() = orig;
    auto span = [&](long f, long t) {
        Value m = Value::matchVal(subject.substr(f, t - f), f, t);
        if (orig) m.extM() = orig;
        return m;
    };
    // The positional captures this one owns: a capture that captured something
    // itself was recorded as a child and is built from that record, so nesting
    // goes all the way down.
    std::map<int, const std::vector<ParseNode>*> capKids;
    if (c.kids) for (auto& ck : *c.kids)
        if (isPositionalKey(ck.first) && !ck.second.empty() && ck.second[0].capLocal >= 0)
            capKids[ck.second[0].capLocal] = &ck.second;
    for (size_t i = 0; i < c.caps.size(); i++) {
        auto kit = capKids.find((int)i);
        if (kit != capKids.end()) {
            const std::vector<ParseNode>& occ = *kit->second;
            if (occ.size() > 1 || (c.listCaps && c.listCaps->count((int)i))) {
                Value lst = Value::array();
                for (auto& o : occ) lst.arrRef().push_back(matchFromNode(o, subject, orig));
                cv.arrRef().push_back(lst);
            } else cv.arrRef().push_back(matchFromNode(occ[0], subject, orig));
            continue;
        }
        if (c.listCaps && c.listCaps->count((int)i)) { // `( (a)+ )` — every occurrence
            Value lst = Value::array();
            if (c.capReps) {
                auto rit = c.capReps->find((int)i);
                if (rit != c.capReps->end())
                    for (auto& o : rit->second) lst.arrRef().push_back(span(o.first, o.second));
            }
            cv.arrRef().push_back(lst);
            continue;
        }
        auto& p = c.caps[i];
        cv.arrRef().push_back(p.first < 0 ? Value::nil() : span(p.first, p.second));
    }
    while (cv.arr() && !cv.arr()->empty() && cv.arr()->back().t == VT::Nil) cv.arrRef().pop_back();
    for (auto& nm : c.named)
        if (!c.kids || !c.kids->count(nm.first))
            cv.hashRef()[nm.first] = span(nm.second.first, nm.second.second);
    // an alias pair (ParseNode::aliasId) is one Match under two keys
    std::unordered_map<uint64_t, Value> aliasBuilt;
    auto child = [&](const ParseNode& g) -> Value {
        if (!g.aliasId) return matchFromNode(g, subject, orig);
        auto hit = aliasBuilt.find(g.aliasId);
        if (hit != aliasBuilt.end()) return hit->second;
        return aliasBuilt.emplace(g.aliasId, matchFromNode(g, subject, orig)).first->second;
    };
    if (c.kids) for (auto& ck : *c.kids) {
        if (isPositionalKey(ck.first)) continue;   // a NUMBER — presented above
        bool many = ck.second.size() > 1 || (c.listNames && c.listNames->count(ck.first));
        if (!many) { cv.hashRef()[ck.first] = child(ck.second[0]); continue; }
        Value a2 = Value::array(); a2.isList = true;
        for (auto& g : ck.second) a2.arr()->push_back(child(g));
        cv.hashRef()[ck.first] = a2;
    }
    return cv;
}

std::string Interpreter::substSelect(const std::string& subj, const std::string& pat,
                                     Value* replArg, ValueList& args, long& nsub, bool literal,
                                     const std::string* tmplRepl, Value* matchResult) {
    nsub = 0;
    // Where the next search starts after a match. Normally past the match
    // (and at least one character on, so a zero-width match cannot spin);
    // under `:ov` one character past where it BEGAN, which is what makes the
    // matches overlap. One CHARACTER, not one byte — stepping into the middle
    // of a UTF-8 sequence would search from half a character.
    bool overlapFlag = false;
    auto advanceAfter = [&](long from, long to) -> long {
        if (!overlapFlag) return to > from ? to : to + 1;
        long base = from + 1;
        while (base < (long)subj.size() && ((unsigned char)subj[(size_t)base] & 0xC0) == 0x80) base++;
        return base;
    };
    bool global = false, samecase = false, samespace = false, samemark = false;
    bool icase = false, sigspace = false, ignoremark = false, p5 = false;
    bool haveX = false, haveNth = false, haveStart = false, posAnchored = false;
    bool haveAs = false, overlap = false;
    // The REPLACEMENT's case/mark transfer goes word by word under :s and
    // :ss — a different question from whether the MATCH is sigspace. The
    // method form's `:ss` (`.subst-mutate(/c \s+ x/, 'Z P', :ss)`) matches the
    // Regex as it was compiled and only shapes the replacement; ss/// and
    // s:ss/// carry it in the pattern, where it makes the match sigspace too.
    bool wordByWord = false;
    Value xVal, nthVal, asVal; long startPos = 0;
    auto setAdverb = [&](const std::string& k, const Value& pv, bool inPattern) {
        // ordinal / count adverbs written as one token: :1st :2nd :3rd :5th, :2x
        if (k.size() >= 2 && ascii::isdigit((unsigned char)k[0])) {
            size_t d = 0; while (d < k.size() && ascii::isdigit((unsigned char)k[d])) d++;
            std::string suf = k.substr(d);
            long num = std::stol(k.substr(0, d));
            if (suf == "st" || suf == "nd" || suf == "rd" || suf == "th") { haveNth = true; nthVal = Value::integer(num); return; }
            if (suf == "x") { haveX = true; xVal = Value::integer(num); return; }
        }
        if ((k == "g" || k == "global") && pv.truthy()) global = true;
        else if (k == "x") { haveX = true; xVal = pv; }
        else if (k == "nth" || k == "st" || k == "nd" || k == "rd" || k == "th") { haveNth = true; nthVal = pv; }
        else if (k == "p" || k == "pos" || k == "c" || k == "continue") {
            haveStart = true;
            if (k == "p" || k == "pos") posAnchored = true;
            // a bare `:p` / `:c` starts where the `$/` in scope ended
            if (pv.t == VT::Bool) {
                startPos = 0;
                if (Value* mv = tctx_.cur ? tctx_.cur->find("$/") : nullptr)
                    if (mv->t == VT::Match) startPos = mv->rTo();
            }
            else startPos = pv.toInt();
        }
        else if (k == "i" || k == "ignorecase") icase = true;
        else if (k == "samecase") samecase = true; // implies :i for regex only (applied below)
        else if (k == "ii") { samecase = true; icase = true; } // :ii always implies :i
        else if (k == "s" || k == "sigspace") { sigspace = true; wordByWord = true; }
        else if (k == "samespace" || k == "ss") { samespace = true; wordByWord = true; if (inPattern) sigspace = true; }
        else if (k == "samemark" || k == "mm") { samemark = true; ignoremark = true; } // :mm implies :m
        else if (k == "m" || k == "ignoremark") ignoremark = true;
        else if (k == "P5" || k == "Perl5") p5 = true; // s:P5/// — Perl 5 pattern syntax
        // `:as(Str)` coerces every match to that type — the match objects are
        // what was found, `:as` says what to hand back. Ignoring it silently
        // returned Match objects AND swallowed the `:g`/`:x` that came with
        // it, so `.match(/a/, :g, :as(Str))` answered one Match.
        else if (k == "as") { haveAs = true; asVal = pv; }
        // `:ov` — OVERLAPPING matches: the search resumes one character after
        // each match STARTS, not after it ends, so `/aa/` on "aaa" finds two.
        // It implies :g (there is nothing to overlap in a single match).
        else if ((k == "ov" || k == "overlap") && pv.truthy()) {
            // …on a MATCH only. Overlapping matches cannot be substituted —
            // the second one's text has already been replaced — so `s:ov///`
            // is refused rather than quietly corrupting the string
            // (S05-substitution/subst.t asserts the refusal).
            if (!matchResult)
                throw RakuError{Value::typeObj("X::Syntax::Regex::Adverb"),
                                "Adverb overlap not allowed on substitution"};
            overlap = true; overlapFlag = true; global = true;
        }
        else throw RakuError{Value::typeObj("X::Syntax::Regex::Adverb"), "Unrecognized regex adverb: :" + k};
    };
    for (auto& a : args)
        if (a.t == VT::Pair) setAdverb(a.s, a.pairVal() ? *a.pairVal() : Value::boolean(true), false);
    // leading `:name` / `:name(arg)` adverbs baked into the pattern (s///, ss///)
    //
    // A LITERAL needle has none: `.subst(':ver<1.2>')` is asking for that text,
    // and reading its leading colon as an adverb either threw
    // ("Unrecognized regex adverb: :ver" — Identity::Utils' `without-ver`) or,
    // worse, silently consumed it — `.subst(':x')` stripped `:x` as the count
    // adverb, left an empty needle and substituted nothing at all.
    std::string realPat = pat;
    if (!literal) { size_t i = 0;
      while (i < realPat.size() && realPat[i] == ':') {
          size_t j = i + 1; std::string name;
          while (j < realPat.size() && ascii::isalnum((unsigned char)realPat[j])) name += realPat[j++];
          if (name.empty()) break;
          Value argv = Value::boolean(true);
          if (j < realPat.size() && realPat[j] == '(') {
              int d = 0; std::string arg;
              do { char c = realPat[j]; if (c == '(') d++; else if (c == ')') d--; arg += c; j++; } while (j < realPat.size() && d > 0);
              // the value of a match-mode adverb (:i, :m) must be a compile-time constant
              if ((name == "i" || name == "ignorecase" || name == "m" || name == "ignoremark")
                  && arg.find_first_of("$@%") != std::string::npos)
                  throw RakuError{Value::typeObj("X::Value::Dynamic"), "Value of :" + name + " must be known at compile time"};
              try { argv = evalString(arg); } catch (FeatureNotBuilt&) { throw; } catch (...) {}
          }
          while (j < realPat.size() && realPat[j] == ' ') j++;
          setAdverb(name, argv, true);
          i = j;
      }
      realPat = realPat.substr(i);
    }
    if (samecase && !literal) icase = true; // :samecase implies :i for a regex, not a Str pattern
    if (!literal && realPat.empty())
        throw RakuError{Value::typeObj("X::Syntax::Regex::NullRegex"), "Null regex not allowed"};
    if (haveX) { // the :x count must be an Int, a Range, or Whatever
        VT t = xVal.t;
        if (!(t == VT::Int || t == VT::Num || t == VT::Rat || t == VT::Range || t == VT::Whatever || t == VT::Bool))
            throwTypedV("X::Str::Match::x", {{"got", xVal}},
                "in Str.match, got invalid value of type " + xVal.typeName() +
                " for :x, must be Int or Range");
        // `Inf` means "all", but NaN and -Inf are not counts at all: neither
        // can be turned into a number of matches to take, so they throw
        // rather than quietly answering the empty list.
        if (t == VT::Num || t == VT::Rat) {
            const double xn = xVal.toNum();
            if (std::isnan(xn) || (std::isinf(xn) && xn < 0))
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Cannot coerce " + xVal.gist() + " to an Int for :x"};
        }
    }
    if (!literal) {
        // interpolate scalar variables ($foo, $^a) into the regex as literal (quotemeta'd) text
        std::string ip;
        for (size_t i = 0; i < realPat.size(); i++) {
            if (size_t sp = Regex::spliceSpan(realPat, i)) { ip += realPat.substr(i, sp); i += sp - 1; continue; }
            if (realPat[i] == '\\' && i + 1 < realPat.size()) { ip += realPat[i]; ip += realPat[i + 1]; i++; continue; }
            if (realPat[i] == '$' && i + 1 < realPat.size()) {
                size_t j = i + 1;
                if (realPat[j] == '^' && j + 1 < realPat.size()) j++; // $^a is visible as $a
                if (j < realPat.size() && (ascii::isalpha((unsigned char)realPat[j]) || realPat[j] == '_')) {
                    std::string nm;
                    while (j < realPat.size() && (ascii::isalnum((unsigned char)realPat[j]) || realPat[j] == '_' ||
                           ((realPat[j] == '-' || realPat[j] == '\'') && j + 1 < realPat.size() &&
                            ascii::isalpha((unsigned char)realPat[j + 1])))) nm += realPat[j++];
                    if (Value* v = tctx_.cur->find("$" + nm)) {
                        // A variable holding a REGEX is a sub-pattern, not text to
                        // quote: `s/^($token) \: //` (HTTP::Tinyish) matched the
                        // LITERAL "regex Regex" and so found no header name at all.
                        // The match path already spliced it; only this copy did not.
                        if (v->t == VT::Regex) {
                            ip += p5 ? spliceRegexValueP5(v->s) : spliceRegexValue(v->s);
                            i = j - 1; continue;
                        }
                        // P5 pattern: Perl interpolates a variable as regex SOURCE,
                        // not as quoted literal text — splice it raw
                        if (p5) { ip += v->toStr(); i = j - 1; continue; }
                        // whitespace must be escaped too (it is insignificant in a
                        // regex): a variable holding ' ' spliced bare, so
                        // `s/ $W ** 2 /-/` matched EMPTY — same rule as quoteMetaRx
                        for (char c : v->toStr()) {
                            if (c == '\n') { ip += "\\n"; continue; }
                            if (c == '\t') { ip += "\\t"; continue; }
                            if (c == '\r') { ip += "\\r"; continue; }
                            if (c == ' ')  { ip += "\\ "; continue; }
                            if (std::strchr(".\\+*?[^]$(){}=!<>|:-#", c)) ip += '\\';
                            ip += c;
                        }
                        i = j - 1; continue;
                    }
                }
            }
            ip += realPat[i];
        }
        realPat = ip;
        if (!p5) realPat = rxInterpArrays(realPat); // `/@alpha/` — element alternation (comb path)
    }
    // `\w**{$n}` / `**{1..3}` runtime-bounded quantifier: wire the range hook so
    // the bounds are evaluated at match time (without it they default to 0..*).
    GrammarHooks ssHooks;
    bool needRangeHook = !literal && realPat.find("**") != std::string::npos && realPat.find('{') != std::string::npos;
    if (needRangeHook) {
        ssHooks.range = [this](const std::string& code, const GrammarHooks::NamedMap&,
                               const GrammarHooks::ParamMap&) -> std::pair<long, long> {
            bool unbounded = code.find("..*") != std::string::npos || code.find("..Inf") != std::string::npos ||
                             code.find("..\xE2\x88\x9E") != std::string::npos;
            return dynQuantLimits(evalString(code), unbounded);
        };
    }
    // …and a CODE ASSERTION must evaluate here too. The `~~` path wires this
    // hook; the occurrence scan never did, so the engine's lenient default made
    // every `<?{ … }>` pass: `"abc".subst(/<?{ False }>./, "X", :g)` replaced
    // all three characters, and `.comb(/<?{ False }>./)` found three matches
    // where both should find none. The same regex answered one way to `~~` and
    // another to every repeated-match consumer built on this function.
    bool needAssertHook = !literal && patHasCodeAssert(realPat);
    if (needAssertHook) {
        GrammarHooks ch = codeAssertHooks();
        ssHooks.assertPass = ch.assertPass;
        ssHooks.dynRule = ch.dynRule;      // `<{ … }>` decides its pattern here too
    }
    const bool wantSsHooks = needRangeHook || needAssertHook;
    std::vector<RxMatch> matches;
    if (literal) {
        // plain string pattern: exact byte search (control chars, no regex metachars)
        std::string needle = realPat;
        if (icase) for (auto& c : needle) c = ascii::tolower((unsigned char)c);
        std::string hay = subj;
        if (icase) for (auto& c : hay) c = ascii::tolower((unsigned char)c);
        long pos = haveStart ? startPos : 0;
        // an EMPTY needle matches, zero-width, at every grapheme boundary —
        // the start and the end included (`"abc".match('', :g)` has four)
        if (needle.empty()) {
            const long n = (long)subj.size();
            for (long p = pos; p <= n; ) {
                RxMatch mm; mm.matched = true; mm.from = mm.to = p;
                matches.push_back(mm);
                if (p >= n) break;
                const long e = (long)uniClusterEndUtf8(subj, (size_t)p, (size_t)n);
                p = e > p ? e : p + 1;
            }
        }
        while (needle.size() && pos <= (long)hay.size()) {
            size_t f = graphemeFind(hay, needle, (size_t)pos);
            if (f == std::string::npos) break;
            RxMatch mm; mm.matched = true; mm.from = (long)f; mm.to = (long)f + (long)needle.size();
            matches.push_back(mm);
            pos = advanceAfter(mm.from, mm.to);
        }
    } else if (ignoremark) {
        // :m/:mm — fold subject & pattern to base characters (drop combining marks),
        // match on the folded text, then map matched ranges back to original coordinates.
        using namespace rakupp;
        auto nextGrapheme = [](const std::vector<uint32_t>& cps, size_t& i) {
            std::vector<uint32_t> g; g.push_back(cps[i++]);
            while (i < cps.size() && uniCombiningClass(cps[i]) != 0) g.push_back(cps[i++]);
            return g;
        };
        auto baseOf = [](const std::vector<uint32_t>& g) {
            std::string b; for (uint32_t cp : uniNormalize(g, 0)) if (uniCombiningClass(cp) == 0) b += smEncode(cp); return b;
        };
        std::vector<uint32_t> scps = smDecode(subj);
        std::string folded; std::vector<long> foldStart, origStart; long ob = 0;
        for (size_t i = 0; i < scps.size();) {
            foldStart.push_back((long)folded.size()); origStart.push_back(ob);
            std::string gtext; { auto g = nextGrapheme(scps, i);
                for (uint32_t cp : g) gtext += smEncode(cp); folded += baseOf(g); }
            ob += (long)gtext.size();
        }
        foldStart.push_back((long)folded.size()); origStart.push_back(ob);
        std::string fpat; { std::vector<uint32_t> pcps = smDecode(realPat);
            for (size_t i = 0; i < pcps.size();) fpat += baseOf(nextGrapheme(pcps, i)); }
        std::string flags = std::string(icase ? "i" : "") + (sigspace ? "s" : "") + (p5 ? "5" : "");
        std::shared_ptr<const Regex> reP = compileRegexCached(fpat, flags);
        const Regex& re = *reP;
        const GrammarHooks* useHooks = wantSsHooks ? &ssHooks : nullptr;
        if (!re.ok()) return subj;
        auto toOrig = [&](long fb) -> long {
            size_t idx = std::lower_bound(foldStart.begin(), foldStart.end(), fb) - foldStart.begin();
            return origStart[std::min(idx, origStart.size() - 1)];
        };
        long pos = 0; RxMatch mm;
        while (pos >= 0 && pos <= (long)folded.size() && re.search(folded, pos, mm, nullptr, nullptr, useHooks)) {
            RxMatch om; om.matched = true; om.from = toOrig(mm.from); om.to = toOrig(mm.to);
            matches.push_back(om);
            pos = advanceAfter(mm.from, mm.to);
        }
    } else {
        std::string flags = std::string(icase ? "i" : "") + (sigspace ? "s" : "") + (p5 ? "5" : "");
        std::shared_ptr<const Regex> reP = compileRegexCached(realPat, flags);
        const Regex& re = *reP;
        const GrammarHooks* useHooks = wantSsHooks ? &ssHooks : nullptr;
        if (!re.ok()) return subj;
        // `<NAME>` in the pattern means a lexical `my regex NAME {…}` here just
        // as it does under `~~`; without the resolver those names compiled to
        // an unknown assertion and matched zero-width, so a pattern built from
        // named pieces matched under `~~` and not under .subst/.comb/.split.
        // Built only when the pattern can actually name one.
        std::set<std::string> lexNames;
        SubResolver resolver;
        if (!namedRegex_.empty() && realPat.find('<') != std::string::npos)
            lexSubResolver(resolver, lexNames, useHooks);
        long pos = haveStart ? startPos : 0; RxMatch mm;
        while (pos >= 0 && pos <= (long)subj.size() &&
               re.search(subj, pos, mm, resolver, lexNames.empty() ? nullptr : &lexNames, useHooks)) {
            matches.push_back(mm);
            pos = advanceAfter(mm.from, mm.to);
        }
    }
    // :p(n) anchors the FIRST match exactly at position n (`:c` merely searches from n).
    if (posAnchored && (matches.empty() || matches[0].from != startPos)) matches.clear();
    long total = (long)matches.size();
    // occurrence selection (1-based). :x count, :nth indices, :g all, else first.
    auto bounds = [&](const Value& v, long& lo, long& hi) {
        // `:x` reads a Range by its ENDPOINTS and ignores exclusivity, so
        // `2^..^4` asks for 2..4 — a quirk, but the one Rakudo has, and a
        // count is the kind of thing nobody writes an exclusive range for
        // on purpose (Str sheet ST-47).
        if (v.t == VT::Range) { lo = v.rFrom(); hi = v.rTo(); }
        else if (v.t == VT::Whatever || std::isinf(v.toNum())) { lo = 1; hi = total; }
        else { lo = hi = v.toInt(); }
    };
    std::set<long> sel;
    if (haveNth) {
        ValueList l;
        // The scalar forms name ONE match: `*` / Inf the last; a WhateverCode
        // counts from the end (`*-1` is the one before the last — the tail
        // index is |code(-1)|); any other Callable is CALLED, with no
        // arguments, for the index (`:st{1;}`), a value below 1 taking the
        // first. Each becomes a one-index list, so :x applies as it does to
        // an Int.
        const bool infNth = nthVal.isNumeric() && std::isinf(nthVal.toNum()) && nthVal.toNum() > 0;
        if (nthVal.t == VT::Whatever || infNth) {
            if (total >= 1) l.push_back(Value::integer(total));
        }
        else if (nthVal.t == VT::Code && nthVal.code()) {
            long idx;
            if (nthVal.code()->isWhateverCode) {
                const long tail = std::labs((long)callCallable(nthVal, ValueList{Value::integer(-1)}).toInt());
                idx = tail >= 1 ? total - tail + 1 : 0;
            } else {
                idx = (long)callCallable(nthVal, ValueList{}).toInt();
                if (idx < 1) idx = 1;
            }
            if (idx >= 1 && idx <= total) l.push_back(Value::integer(idx));
        }
        else {
            // an ENDLESS index list (`:nth(2, 4 ... *)`) is read only as far as it can
            // matter: the indices rise, so no more than `total` of them can select
            Value nthList = endlessLazy(nthVal) ? methodCall(nthVal, "head", ValueList{Value::integer(total + 1)})
                                                : nthVal;
            forceLazy(nthList);
            l = (nthList.t == VT::Array && nthList.arr()) ? *nthList.arr() : nthList.flatten();
        }
        long xcap = total;
        if (haveX) { long lo, hi; bounds(xVal, lo, hi); xcap = hi; if ((long)l.size() < lo) l.clear(); }
        long cnt = 0, prev = 0;
        for (auto& v : l) { long i = v.toInt();
            if (i <= prev) throw RakuError{Value::typeObj("X::AdHoc"), "Attempt to fetch matches out of order with :nth"};
            prev = i;
            if (cnt >= xcap) break; if (i >= 1 && i <= total) { sel.insert(i); cnt++; } }
    } else if (haveX) {
        long lo, hi; bounds(xVal, lo, hi);
        long count = std::min(total, hi);
        if (count >= lo && count >= 1) for (long i = 1; i <= count; i++) sel.insert(i);
    } else if (global) {
        for (long i = 1; i <= total; i++) sel.insert(i);
    } else if (total >= 1) sel.insert(1);
    // Every Match reports the WHOLE subject as its .orig, exactly as the m//
    // path does (`mk`, above): only from/to say which part matched. This path —
    // which is what `.match(:g)`, `.comb(:match)` and every s/// adverb use —
    // built its Matches without it, so `.orig`, `.prematch` and `.postmatch`
    // answered the MATCHED TEXT instead of the subject, and `.from`/`.to` fell
    // through graphemeOff's no-subject branch and reported BYTE offsets. That
    // last one is correct by accident on ASCII and wrong on any string with a
    // multi-byte character, which is why it survived: `"— a — b — c".match(
    // /<[abc]>/, :g)` gives from = 4, 10, 16 where Rakudo gives 2, 6, 10.
    auto origStr = std::make_shared<std::string>(subj);
    auto mkm = [&](long from, long to) {
        Value mv = Value::matchVal(subj.substr(from, to - from), from, to);
        mv.extM() = origStr;
        return mv;
    };
    // Match value (positional + named captures) for one raw match.
    auto build = [&](const RxMatch& mm) {
        Value v = mkm((long)mm.from, (long)mm.to);
        for (size_t ci = 0; ci < mm.caps.size(); ci++) {
            if (mm.listCaps.count((int)ci)) { // repeated capture → $ci is an Array
                Value lst = Value::array();
                auto it = mm.capReps.find((int)ci);
                if (it != mm.capReps.end())
                    for (auto& o : it->second) lst.arrRef().push_back(mkm((long)o.first, (long)o.second));
                v.arrRef().push_back(lst);
                continue;
            }
            auto& c = mm.caps[ci];
            if (c.first < 0) v.arrRef().push_back(Value::nil());
            else v.arrRef().push_back(mkm((long)c.first, (long)c.second));
        }
        // …trailing unset captures are not part of the list (see the other builders)
        while (v.arr() && !v.arr()->empty() && v.arr()->back().t == VT::Nil) v.arrRef().pop_back();
        for (auto& kv : mm.named) {
            // a named capture under a quantifier is LIST-valued: every collated
            // occurrence becomes one Match ($m<bit>.list in URI::Encode's decoder)
            auto ch = mm.children.find(kv.first);
            if (mm.listNames && mm.listNames->count(kv.first) && ch != mm.children.end()) {
                Value lst = Value::array(); lst.isList = true;
                for (auto& pn : ch->second) lst.arrRef().push_back(matchFromNode(pn, subj, origStr));
                v.hashRef()[kv.first] = std::move(lst);
                continue;
            }
            // …and a capture with a tree of its own keeps it, so the block a
            // .subst is given can read `$<header><lang>` exactly as the `~~`
            // path's match can.
            if (ch != mm.children.end() && !ch->second.empty()) {
                v.hashRef()[kv.first] = matchFromNode(ch->second.back(), subj, origStr);
                continue;
            }
            v.hashRef()[kv.first] = mkm((long)kv.second.first, (long)kv.second.second);
        }
        return v;
    };
    auto interp = [&](const std::string& s, const Value& mv) -> std::string {
        std::string r;
        // `$0.uc()` in a replacement is a METHOD CALL, exactly as in any other
        // interpolating string — and, as there, only WITH parentheses (`$0.uc`
        // stays the capture followed by literal text). Without this the capture
        // interpolated and `.uc()` came out verbatim, so HTTP::Tiny's header
        // capitalisation emitted `h.uc()ost:` on the wire. The captures are
        // already bound in scope by bindCaps(), so the expression is simply
        // evaluated rather than re-implemented here.
        auto methodChain = [&](size_t start, size_t after) -> size_t {
            size_t j = after, last = after;
            while (j + 1 < s.size() && s[j] == '.' &&
                   (ascii::isalpha((unsigned char)s[j + 1]) || s[j + 1] == '_')) {
                size_t k = j + 1;
                while (k < s.size() && (ascii::isalnum((unsigned char)s[k]) || s[k] == '_' || s[k] == '-')) k++;
                if (k >= s.size() || s[k] != '(') break;   // no parens: not a call
                int depth = 0; size_t e = k;
                for (; e < s.size(); e++) {
                    if (s[e] == '(') depth++;
                    else if (s[e] == ')') { if (--depth == 0) { e++; break; } }
                }
                if (depth != 0) break;                     // unbalanced: leave it literal
                j = e; last = e;
            }
            return last > after ? last : start;             // start = "no chain found"
        };
        // where a run of postfix subscripts starting at `j` ends (`[…]`, `<…>`,
        // `{…}`, balanced), or `j` itself when there is none
        auto subscriptsEnd = [&](size_t j) -> size_t {
            while (j < s.size() && (s[j] == '[' || s[j] == '{' ||
                                    (s[j] == '<' && j + 1 < s.size() && s[j + 1] != ' ' && s[j + 1] != '='))) {
                char open = s[j], close = open == '[' ? ']' : open == '{' ? '}' : '>';
                int d = 0; size_t k = j;
                for (; k < s.size(); k++) { if (s[k] == open) d++; else if (s[k] == close && --d == 0) break; }
                if (k >= s.size()) break;
                j = k + 1;
            }
            return j;
        };
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                // the replacement side is qq-ish: decode the standard escapes
                // (s/$/\n/ appends a NEWLINE, not the letter n)
                char c = s[i + 1]; i++;
                switch (c) {
                    case 'n': r += '\n'; break;
                    case 't': r += '\t'; break;
                    case 'r': r += '\r'; break;
                    case '0': r += '\0'; break;
                    case 'e': r += '\x1b'; break;
                    case 'a': r += '\a'; break;
                    case 'f': r += '\f'; break;
                    case 'b': r += '\b'; break;
                    default:  r += c;    break;
                }
                continue;
            }
            if (s[i] == '$' && i + 1 < s.size() && ascii::isdigit((unsigned char)s[i + 1])) {
                size_t j = i + 1; std::string num; while (j < s.size() && ascii::isdigit((unsigned char)s[j])) num += s[j++];
                size_t chainEnd = methodChain(i, j);
                if (chainEnd != i) {
                    try { r += evalString(s.substr(i, chainEnd - i)).toStr(); i = chainEnd - 1; continue; }
                    catch (FeatureNotBuilt&) { throw; }
                    catch (...) { }                        // fall back to the plain capture
                }
                long idx = std::stol(num); if (mv.arr() && idx < (long)mv.arr()->size()) r += (*mv.arr())[idx].toStr();
                i = j - 1; continue;
            }
            if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '<') {
                size_t j = s.find('>', i + 2);
                if (j != std::string::npos) { std::string nm = s.substr(i + 2, j - i - 2);
                    if (mv.hash() && mv.hash()->count(nm)) r += (*mv.hash())[nm].toStr(); i = j; continue; }
            }
            // `$/` — the whole match, and it takes a subscript like any variable.
            // HTTP::Tinyish's `s/…/$/[0]/` rebuilds the status line from capture 0
            // that way; without this the replacement was the literal text `$/[0]`.
            if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '/') {
                size_t j = i + 2;
                if (j < s.size() && (s[j] == '[' || s[j] == '<')) {
                    char open = s[j], close = open == '[' ? ']' : '>';
                    int d = 0; size_t k = j;
                    for (; k < s.size(); k++) { if (s[k] == open) d++; else if (s[k] == close && --d == 0) break; }
                    if (k < s.size()) {
                        std::string sub = s.substr(j + 1, k - j - 1);
                        if (open == '[') {
                            long idx = 0;
                            try { idx = evalString(sub).toInt(); } catch (FeatureNotBuilt&) { throw; } catch (...) {}
                            if (mv.arr() && idx >= 0 && idx < (long)mv.arr()->size()) r += (*mv.arr())[idx].toStr();
                        }
                        else if (mv.hash() && mv.hash()->count(sub)) r += (*mv.hash())[sub].toStr();
                        i = k; continue;
                    }
                }
                // `$/.chars()` — a method call on it, parenthesized as in any
                // interpolating string
                if (j + 1 < s.size() && s[j] == '.' && (ascii::isalpha((unsigned char)s[j + 1]) || s[j + 1] == '_')) {
                    size_t k = j + 1;
                    while (k < s.size() && (ascii::isalnum((unsigned char)s[k]) || s[k] == '_' || s[k] == '-')) k++;
                    if (k < s.size() && s[k] == '(') {
                        int d = 0; size_t e = k;
                        for (; e < s.size(); e++) { if (s[e] == '(') d++; else if (s[e] == ')' && --d == 0) break; }
                        if (e < s.size()) {
                            Value* slot = tctx_.cur ? tctx_.cur->find("$/") : nullptr;
                            Value saved; if (slot) { saved = *slot; *slot = mv; }
                            try { r += evalString(s.substr(i, e - i + 1)).toStr(); }
                            catch (...) { if (slot) *slot = saved; throw; }
                            if (slot) *slot = saved;
                            i = e; continue;
                        }
                    }
                }
                r += mv.toStr(); i = i + 1; continue;   // bare `$/`
            }
            if (s[i] == '$' && i + 2 < s.size() && s[i + 1] == '^' && (ascii::isalpha((unsigned char)s[i + 2]) || s[i + 2] == '_')) {
                size_t j = i + 2; std::string nm; while (j < s.size() && (ascii::isalnum((unsigned char)s[j]) || s[j] == '_')) nm += s[j++];
                if (Value* v = tctx_.cur->find("$" + nm)) r += v->toStr(); // $^a placeholder (also visible as $a)
                i = j - 1; continue;
            }
            if (s[i] == '$' && i + 1 < s.size() && (ascii::isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_')) {
                size_t j = i + 1; std::string nm;
                // A hyphen or apostrophe CONTINUES an identifier when a letter or
                // digit follows it, exactly as it does everywhere else in Raku.
                // Stopping at the hyphen made `s/$sep\n/$commit-sep/` interpolate
                // an undeclared `$commit` — the empty string — and leave `-sep` as
                // literal text, so Git::Log's record separator became the four
                // characters `-sep` and two commits came back glued into one.
                while (j < s.size() && (ascii::isalnum((unsigned char)s[j]) || s[j] == '_' ||
                       ((s[j] == '-' || s[j] == '\'') && j + 1 < s.size() &&
                        (ascii::isalnum((unsigned char)s[j + 1]) || s[j + 1] == '_')))) nm += s[j++];
                // …with its postfix subscripts, as in any interpolating string:
                // `s/.*/$t[1]/`, `$h<key>`, `$h{$k}`, chained, then `.method()`
                size_t k = subscriptsEnd(j);
                size_t chainEnd = methodChain(i, k);
                if (k > j || chainEnd != i) {
                    size_t e = chainEnd != i ? chainEnd : k;
                    try { r += evalString(s.substr(i, e - i)).toStr(); i = e - 1; continue; }
                    catch (FeatureNotBuilt&) { throw; }
                    catch (...) { }   // fall back to the bare variable
                }
                if (Value* v = tctx_.cur->find("$" + nm)) r += v->toStr();
                i = j - 1; continue; // interpolate a scalar variable in the replacement
            }
            // `%h<key>` / `%h{$k}` — a hash interpolates only when subscripted
            if (s[i] == '%' && i + 1 < s.size() && (ascii::isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_')) {
                size_t j = i + 1;
                while (j < s.size() && (ascii::isalnum((unsigned char)s[j]) || s[j] == '_' ||
                       ((s[j] == '-' || s[j] == '\'') && j + 1 < s.size() &&
                        (ascii::isalnum((unsigned char)s[j + 1]) || s[j + 1] == '_')))) j++;
                size_t k = subscriptsEnd(j);
                if (k > j) {
                    try { r += evalString(s.substr(i, k - i)).toStr(); i = k - 1; continue; }
                    catch (FeatureNotBuilt&) { throw; }
                    catch (...) { }
                }
            }
            if (s[i] == '@' && i + 1 < s.size() && (ascii::isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_')) {
                size_t j = i + 1; std::string nm;
                // A hyphen or apostrophe CONTINUES an identifier when a letter or
                // digit follows it, exactly as it does everywhere else in Raku.
                // Stopping at the hyphen made `s/$sep\n/$commit-sep/` interpolate
                // an undeclared `$commit` — the empty string — and leave `-sep` as
                // literal text, so Git::Log's record separator became the four
                // characters `-sep` and two commits came back glued into one.
                while (j < s.size() && (ascii::isalnum((unsigned char)s[j]) || s[j] == '_' ||
                       ((s[j] == '-' || s[j] == '\'') && j + 1 < s.size() &&
                        (ascii::isalnum((unsigned char)s[j + 1]) || s[j + 1] == '_')))) nm += s[j++];
                // `@arr[expr]` — a non-empty subscript indexes one element (captures
                // are already bound, so the subscript may use $0/$<name>).
                if (j < s.size() && s[j] == '[') {
                    int depth = 0; size_t k = j;
                    for (; k < s.size(); k++) { if (s[k] == '[') depth++; else if (s[k] == ']' && --depth == 0) break; }
                    std::string subx = s.substr(j + 1, k - j - 1);
                    size_t ws = subx.find_first_not_of(" \t");
                    if (k < s.size() && ws != std::string::npos) { // non-empty subscript
                        long idx = 0; try { idx = evalString(subx).toInt(); } catch (FeatureNotBuilt&) { throw; } catch (...) {}
                        if (Value* v = tctx_.cur->find("@" + nm)) {
                            ValueList fl = v->flatten();
                            if (idx < 0) negIndexThrow(idx); // an interpolated Failure would die anyway
                            if (idx < (long)fl.size()) r += fl[idx].toStr();
                        }
                        i = k; continue;
                    }
                    if (k + 1 <= s.size()) j = k + 1; // empty `[]` zen slice → whole array
                }
                if (Value* v = tctx_.cur->find("@" + nm)) { // interpolate an array (space-joined)
                    ValueList fl = v->flatten();
                    for (size_t k = 0; k < fl.size(); k++) { if (k) r += ' '; r += fl[k].toStr(); }
                }
                i = j - 1; continue;
            }
            r += s[i];
        }
        return r;
    };
    auto replFor = [&](const RxMatch& mm) -> std::string {
        std::string orig = subj.substr(mm.from, mm.to - mm.from);
        std::string r;
        Value matchV = build(mm);
        auto bindCaps = [&]() {
            setMatchVar(matchV);
            if (matchV.arr()) for (size_t k = 0; k < matchV.arr()->size(); k++) tctx_.cur->define("$" + std::to_string(k), (*matchV.arr())[k]);
            for (auto& kv : *matchV.hash()) tctx_.cur->define("$<" + kv.first + ">", kv.second);
        };
        if (replArg && replArg->t == VT::Code) {
            bindCaps();
            // $_ may live in an OUTER scope: restore must ERASE our local shadow
            // rather than leave a stray `$_ = Any` (same rule as the `~~` path)
            bool hadLocalTopic = tctx_.cur->vars.count("$_") > 0;
            Value saved = hadLocalTopic ? tctx_.cur->vars["$_"] : Value::any();
            tctx_.cur->define("$_", matchV);
            auto restoreTopic = [&] {
                if (hadLocalTopic) tctx_.cur->vars["$_"] = saved;
                else tctx_.cur->vars.erase("$_");
            };
            try { r = callCallable(*replArg, ValueList{matchV}).toStr(); }
            catch (...) { restoreTopic(); throw; }
            restoreTopic();
        } else if (tmplRepl) {
            // s/// replacement TEMPLATE: `{ code }` evaluated per match, else $N/$<name> interpolation
            std::string rt = *tmplRepl;
            size_t a = rt.find_first_not_of(" \t\n"), b = rt.find_last_not_of(" \t\n");
            std::string t = a == std::string::npos ? "" : rt.substr(a, b - a + 1);
            bindCaps();
            if (t.size() >= 2 && t.front() == '{' && t.back() == '}') {
                // a code replacement re-parsed here can't see caller-local custom infixes
                // (`s[…] fromplus= …`); on failure keep the original so it can't abort.
                // a PARSE failure keeps the text (caller-local custom infixes are
                // not visible to the re-parse — the original motive); anything
                // else — a `die`, a typed exception, control flow — propagates
                try { r = evalString(t.substr(1, t.size() - 2)).toStr(); }
                catch (RakuError& e) {
                    if (e.message.rfind("EVAL parse error", 0) == 0) r = orig; else throw;
                }
            } else r = interp(*tmplRepl, matchV);
        } else if (replArg) {
            r = replArg->toStr();
        }
        if (samespace) {
            // replace each whitespace run in r with the corresponding run in the match
            // (done first so grapheme positions line up for :samecase / :samemark)
            std::vector<std::string> ws;
            for (size_t i = 0; i < orig.size(); ) {
                if (ascii::isspace((unsigned char)orig[i])) { std::string w; while (i < orig.size() && ascii::isspace((unsigned char)orig[i])) w += orig[i++]; ws.push_back(w); }
                else i++;
            }
            std::string a; size_t wi = 0;
            for (size_t i = 0; i < r.size(); ) {
                if (ascii::isspace((unsigned char)r[i])) { while (i < r.size() && ascii::isspace((unsigned char)r[i])) i++; a += wi < ws.size() ? ws[wi++] : std::string(" "); }
                else a += r[i++];
            }
            r = a;
        }
        if (ignoremark) r = wordByWord ? applyCaseMarkWords(orig, r, samecase, true)
                                     : applyCaseMark(orig, r, samecase, true); // :m/:mm transfer marks (and case if :ii)
        else if (samecase) r = wordByWord ? applySamecaseWords(orig, r) : applySamecase(orig, r);
        return r;
    };
    std::string out; long last = 0, occ = 0;
    ValueList selMatches;
    for (auto& mm : matches) {
        occ++;
        out += subj.substr(last, mm.from - last);
        if (sel.count(occ)) { out += replFor(mm); nsub++; selMatches.push_back(build(mm)); }
        else out += subj.substr(mm.from, mm.to - mm.from);
        last = mm.to;
    }
    out += subj.substr(last);
    // $/ / the s/// return value: no match → falsey Nil; :g/:nth-list → a List of
    // Matches; a SINGLE ordinal (:2nd, :nth(2)) yields the one Match even with :g.
    // (`.Str` = matched text, `+` of the :g List = count.)
    bool nthScalar = haveNth && !(nthVal.t == VT::Array || nthVal.t == VT::Range);
    Value result;
    // No match at all: a plain m// is Nil, but the MULTI-match adverbs answer with
    // an EMPTY LIST — `("abc" ~~ m:g/z/).elems` is 0 in Rakudo, and it was 1 here
    // because Nil was answering for every shape. Both are falsy, so only code that
    // counts or iterates the result could tell — which `.elems` does.
    if (haveAs && !selMatches.empty()) {
        const std::string want = asVal.t == VT::Type ? asVal.s.str() : asVal.typeName();
        for (auto& mv : selMatches) mv = coerceToType(mv, want);
    }
    if (selMatches.empty())
        result = (global || haveNth || haveX) ? Value::list(ValueList{}) : Value::nil();
    else if (nthScalar && selMatches.size() == 1) result = selMatches.back();
    else if (global || haveNth || haveX) { result = Value::list(selMatches); } // multi-match adverbs → List
    else result = selMatches.back();
    if (!literal) setMatchVar(result); // a literal (string) .subst leaves $/ untouched
    if (matchResult) *matchResult = result;
    return out;
}

// The `*`-twigil dynamics live at one rule completion, hung on that rule's
// ParseNode as an opaque `shared_ptr<const void>` (Regex.h cannot see Value).
// Produced by hooks.captureDyn, consumed by `build`; both live in grammarParse
// below, so the cast back is local to this file.
using GrammarDynSnap = std::vector<std::pair<std::string, Value>>;

Value Interpreter::grammarParse(ClassInfo* g, const std::string& input, bool subparse,
                                const std::string& startRule, Value actions,
                                const ValueList* ruleArgs, long startPos, long* consumedEnd) {
    bool haveActions = (actions.t == VT::Object || actions.t == VT::Type);
    ClassInfo* actCls = nullptr;
    if (actions.t == VT::Object && actions.obj()) actCls = actions.obj()->cls.get();
    else if (actions.t == VT::Type) { auto it = classes_.find(actions.s); if (it != classes_.end()) actCls = it->second.get(); }

    // Resolve the action method for a rule name ONCE per parse: build() asks
    // for every node of the tree (twice, for actualRule + name), and the class
    // chain walk plus the guillemet normalisation showed up in the grammar-json
    // profile. The grammar and actions object are fixed for the parse, so a
    // per-parse cache (negative entries included) is exact.
    auto actMethodCache = std::make_shared<std::unordered_map<std::string, Value*>>();
    auto resolveAction = [actCls, actMethodCache](const std::string& name) -> Value* {
        auto hit = actMethodCache->find(name);
        if (hit != actMethodCache->end()) return hit->second;
        Value* method = actCls ? actCls->findMethod(name) : nullptr;
        if (!method && actCls) {
            // A `:sym«baz»` candidate is acted on by `method X:sym<baz>` — normalise the
            // guillemet sym to the canonical angle form and retry.
            auto g = name.find(":sym\xC2\xAB");
            if (g != std::string::npos) { auto e = name.find("\xC2\xBB", g + 6);
                if (e != std::string::npos) method = actCls->findMethod(name.substr(0, g) + ":sym<" + name.substr(g + 6, e - (g + 6)) + ">" + name.substr(e + 2)); }
        }
        (*actMethodCache)[name] = method;
        return method;
    };

    // run the actions method for `name` (if any) on a freshly built Match, setting .made
    auto runAction = [&](const std::string& name, Value& mv) {
        if (!haveActions || !actCls) return;
        Value* method = resolveAction(name);
        if (!method) return;
        tctx_.makeTargets.push_back(&mv);
        try { invokeMethod(*method, actions, {mv}); }
        catch (RakuError& e) { if (std::getenv("RAKUPP_ACTTRACE")) std::cerr << "[ACT] " << name << " threw: " << e.message << "\n"; tctx_.makeTargets.pop_back(); throw; }
        catch (...) { tctx_.makeTargets.pop_back(); throw; }
        tctx_.makeTargets.pop_back();
    };

    // Gather every rule (walking the inheritance chain; child overrides parent)
    // into a backtrackable GrammarMatcher.
    GrammarMatcher gm;
    // Rule names in DECLARATION order (base grammar first, then derived), so proto
    // candidates keep source order — the final LTM tie-break (S05: earliest-declared
    // wins on an equal-length, equal-specificity match). A std::map would give
    // alphabetical order and silently mis-rank ties.
    std::vector<std::string> declOrder;
    std::set<std::string> declSeen;
    // MOST DERIVED FIRST. Within one grammar the earliest declaration wins an LTM
    // tie; ACROSS inheritance (and role composition, which puts the composed role
    // in the parent slot) the derived declaration wins, exactly as method
    // resolution does. Walking the base first made a base candidate outrank the
    // derived one on every tie — `token h100:sym<General>` beat
    // `token h100:sym<Spanish>` on the same text, so the General ACTION ran and
    // Lingua::NumericWordForms read "ciento sesenta y tres" as 63.
    std::function<void(ClassInfo*)> collect = [&](ClassInfo* c) {
        if (!c) return;
        for (auto& r : c->rules) {
            if (gm.rules.count(r.first)) continue; // a more-derived declaration already won
            GrammarMatcher::Rule rule;
            rule.pattern = r.second;
            rule.kind = c->ruleKind.count(r.first) ? c->ruleKind.at(r.first) : "token";
            auto pit = c->ruleParams.find(r.first);
            if (pit != c->ruleParams.end()) rule.params = pit->second;
            // `multi rule expr(0)`: attach the literal-value candidates of this
            // name, in declaration order, so a call can dispatch on its argument
            // values before binding the generic candidate's parameters.
            if (const std::vector<std::string>* lc = c->findRuleLitCands(r.first)) {
                for (auto& key : *lc) {
                    auto pt = c->findRule(key);
                    auto ar = c->ruleLitArgs.find(key);
                    if (!pt || ar == c->ruleLitArgs.end()) continue;
                    GrammarMatcher::Rule::Lit lit;
                    lit.args = ar->second;
                    lit.pattern = *pt;
                    auto ki = c->ruleKind.find(key);
                    lit.kind = ki != c->ruleKind.end() ? ki->second : rule.kind;
                    rule.lits.push_back(std::move(lit));
                }
                rule.litOnly = c->ruleLitOnly.count(r.first) > 0;
            }
            gm.rules[r.first] = std::move(rule);
        }
        for (auto& nm : c->ruleOrder) if (declSeen.insert(nm).second) declOrder.push_back(nm);
        collect(c->parent.get());
        // `class G does A does B` puts A in the parent slot and B (and every
        // further role) in extraParents — which nothing here ever walked, so a
        // SECOND sibling role's rules simply did not exist in the grammar.
        // DSL::Shared composes three roles into every grammar and lost two.
        for (auto& p : c->extraParents) collect(p.get());
    };
    collect(g);
    // `<name>` where the grammar chain declares no such rule but a LEXICAL
    // `my regex/token/rule name {…}` is in scope: Rakudo resolves the subrule
    // to the lexical routine. Getopt::Long keeps its shared `rule name`
    // OUTSIDE the grammar and parses every option spec through it — without
    // this the subrule matched nothing and no spec ever parsed. Grammar rules
    // win on a name clash (checked first), same shadowing as the plain-regex
    // resolver's.
    for (auto& kv : namedRegex_) {
        if (gm.rules.count(kv.first)) continue;
        GrammarMatcher::Rule rule;
        rule.pattern = kv.second;
        auto kit = namedRegexKind_.find(kv.first);
        rule.kind = (kit != namedRegexKind_.end() && !kit->second.empty()) ? kit->second : "regex";
        gm.rules[kv.first] = std::move(rule);
    }
    // Safety net: any rule not seen via ruleOrder still gets ranked (append in map order).
    for (auto& r : gm.rules) if (declSeen.insert(r.first).second) declOrder.push_back(r.first);

    // Register protoregex candidates: `element:<null>` / `element:sym<x>` is a
    // candidate of proto `element`; matching `<element>` tries them all (LTM).
    for (auto& nm : declOrder) {
        size_t c = nm.find(":sym<");
        if (c == std::string::npos) c = nm.find(":sym\xC2\xAB"); // :sym«…»
        if (c == std::string::npos) c = nm.find(":<");
        // a PLAIN adverb candidate — `token match:character-class { … }` — is a
        // proto member too (IO::Glob's whole matcher is built from these); any
        // `name:key` where key is a bare identifier counts, parens excluded
        // (`:foo('x')` carries an argument, not a candidate tag)
        if (c == std::string::npos) {
            size_t p2 = nm.find(':');
            if (p2 != std::string::npos && p2 > 0 && p2 + 1 < nm.size() &&
                (ascii::isalpha((unsigned char)nm[p2 + 1]) || nm[p2 + 1] == '_') &&
                nm.find('(', p2) == std::string::npos &&
                nm.find('<', p2) == std::string::npos)
                c = p2;
        }
        if (c != std::string::npos && c > 0) gm.protos[nm.substr(0, c)].push_back(nm);
    }

    // Wire the match-time interpreter hooks. Embedded Raku (`<?{…}>` assertions,
    // `:my`/`{…}` side-effects, `$var` atoms, `** {…}` bounds) is parsed once (cached)
    // and evaluated against the live interpreter scope, with $/ bound to input[from..to].
    auto codeCache = std::make_shared<std::map<std::string, std::shared_ptr<Program>>>();
    auto parseCode = [this, codeCache](const std::string& code) -> std::shared_ptr<Program> {
        auto it = codeCache->find(code);
        if (it != codeCache->end()) return it->second;
        auto prog = std::make_shared<Program>();
        // FeatureNotBuilt must escape: under --slim=-eval the Lexer IS the
        // stub, and caching nullptr here would turn every code block into a
        // silent permanent no-op (measured). Ordinary parse trouble stays
        // lenient, as before.
        try { Lexer lx(code); Parser ps(lx.tokenize()); *prog = ps.parseProgram(); }
        catch (FeatureNotBuilt&) { throw; }
        catch (...) { (*codeCache)[code] = nullptr; return nullptr; }
        { std::unique_lock<std::mutex> kl(sharedMut_, std::defer_lock); if (parallelMode_) kl.lock(); keptPrograms_.push_back(prog); }
        (*codeCache)[code] = prog;
        return prog;
    };
    // Every Match this parse hands out — the finished tree, the `$/` a code
    // block sees mid-match, a subrule's Cursor — shares ONE copy of the whole
    // subject, so a submatch's `.orig`/`.target`/`.prematch`/`.postmatch`
    // read the string being parsed, not just its own span (issue #99: an
    // action computing a line number from `$/.orig.substr(0, $/.from)` got
    // it relative to the enclosing match).
    auto targetStr = std::make_shared<std::string>(input);
    auto subMatch = [&input, targetStr](long f, long t) {
        Value m = Value::matchVal(input.substr(f, t - f), f, t);
        m.extM() = targetStr;
        return m;
    };
    // Inline `{ make … }` blocks in a token run during matching; their made value is
    // recorded here by span and applied to the node in build().
    auto pendingMakes = std::make_shared<std::map<std::pair<long, long>, Value>>();
    // …and a NAME-qualified twin, filled by the success-path replay of rules
    // reached through a non-capturing `<.rule>` call. A non-captured rule's
    // children are not in the tree either, so when the parent's replay rebuilds
    // them their actions are suppressed and any `.make` they set is lost — so
    // `$/<child>.made` came back empty (HTTP::Parser reads its header name that
    // way; the whole key vanished). The span-only map cannot carry it: a
    // captured rule over the SAME span (CSS::Grammar's token-over-comment) would
    // then inherit a sibling's made. Keying by NAME as well as span keeps each
    // rule's made to itself.
    auto pendingMakesNamed = std::make_shared<std::map<std::tuple<std::string, long, long>, Value>>();
    // A block containing `make` is DEFERRED: it runs in build() with $/ = the fully-built
    // match (so `{ make $/.values[0].ast }` sees children's .made). A queue per span
    // disambiguates a parent and child that happen to share the same span (innermost-first).
    auto pendingMakeCode = std::make_shared<std::map<std::pair<long, long>, std::vector<std::string>>>();
    // Execute `code` with an overlay of the current rule params (as Str) and $/ (carrying
    // the named captures so far) temporarily bound; `:my`/assignments persist in tctx_.cur.
    using NamedMap = GrammarHooks::NamedMap; using ParamMap = GrammarHooks::ParamMap;
    auto runCode = [this, &input, parseCode, pendingMakes, subMatch](const std::string& code, long from, long to,
                                             const NamedMap& named, const ParamMap& params,
                                             const std::vector<std::pair<long, long>>* caps = nullptr,
                                             const RxCursorCaps* cc = nullptr) -> Value {
        auto prog = parseCode(code);
        if (!prog) return Value::any();
        // build $/ over [from..to] with the named sub-captures attached
        Value m = subMatch(from, to);
        for (auto& nm : named)
            m.hashRef()[nm.first] = subMatch(nm.second.first, nm.second.second);
        // …and where a name has OCCURRENCES rather than one span, the list it will
        // be in the finished match. The flat `named` map keeps only the last span
        // per name, so `<n> '+' <n> { $<n>.elems }` read 0 mid-match where the
        // finished match reads 2.
        if (cc && cc->children)
            for (auto& kv : *cc->children) {
                const auto& occ = kv.second;
                if (occ.empty()) continue;
                bool asList = occ.size() > 1 || (cc->listNames && cc->listNames->count(kv.first));
                auto one = [&](const ParseNode& pn) {
                    return subMatch(pn.from, pn.to);
                };
                if (!asList) { m.hashRef()[kv.first] = one(occ.back()); continue; }
                Value lst = Value::array(); lst.isList = true;
                for (auto& pn : occ) lst.arrRef().push_back(one(pn));
                m.hashRef()[kv.first] = std::move(lst);
            }
        // …and the POSITIONAL ones, when the caller has them. A code assertion in
        // a GRAMMAR rule saw only the named captures, so `([\w]+) <?{ f($0.Str) }>`
        // — the shape a grammar uses to gate a token on a lookup — asked about an
        // empty string and never matched (Lingua::NumericWordForms::Koremutake).
        std::vector<std::pair<std::string, Value>> capSlots;
        if (caps) {
            for (size_t ci = 0; ci < caps->size(); ci++) {
                long cb = (*caps)[ci].first, ce = (*caps)[ci].second;
                Value cv = cb >= 0 && ce >= cb ? subMatch(cb, ce)
                                               : Value::any();
                m.arrRef().push_back(cv);
                capSlots.push_back({"$" + std::to_string(ci), cv});
            }
        }
        // save & overlay $/ + params; restore them after (but let :my vars persist)
        std::vector<std::pair<std::string, Value>> restore;
        auto overlay = [&](const std::string& name, const Value& v) {
            Value* slot = tctx_.cur->find(name);
            restore.push_back({name, slot ? *slot : Value::nil()});
            tctx_.cur->define(name, v);
        };
        bool hadSlash = tctx_.cur->find("$/") != nullptr; Value savedSlash = hadSlash ? *tctx_.cur->find("$/") : Value::nil();
        setMatchVar(m);
        for (auto& cs : capSlots) overlay(cs.first, cs.second);
        for (auto& p : params) {
            // A rule parameter with NO textual value (the entry rule's, when the
            // caller passed `args => (…)` instead of a `<rule($x)>` argument
            // string) must not shadow the real value grammarParse bound into the
            // match scope — overlaying "" there made `<?{ $*extended }>` read an
            // empty string for a Bool the caller had supplied.
            if (p.second.empty() && tctx_.cur->find(p.first)) continue;
            overlay(p.first, Value::str(p.second));
        }
        Value makeTarget; tctx_.makeTargets.push_back(&makeTarget); // capture an inline `make`
        Value last = Value::any();
        try { for (auto& s : prog->stmts) last = exec(s.get()); }
        catch (FeatureNotBuilt&) { tctx_.makeTargets.pop_back(); throw; } // a stub fired INSIDE the block: loud
        catch (RakuError& e) { if (!regexBlockErrorStaysQuiet(e)) { tctx_.makeTargets.pop_back(); throw; } } // a `die` in a grammar block LEAVES the parse (Rakudo); a quiet one falls to the pop below
        catch (...) {}
        tctx_.makeTargets.pop_back();
        if (makeTarget.pairVal()) (*pendingMakes)[{from, to}] = *makeTarget.pairVal(); // record the inline make
        for (auto it = restore.rbegin(); it != restore.rend(); ++it) tctx_.cur->define(it->first, it->second);
        if (Value* s2 = tctx_.cur->find("$/")) *s2 = savedSlash;
        return last;
    };
    gm.hooks.assertPass = [runCode, parseCode](const std::string& code, long from, long to, const NamedMap& nm, const ParamMap& pm) -> bool {
        if (!parseCode(code)) return true; // unparseable assertion → lenient pass
        return runCode(code, from, to, nm, pm).truthy();
    };
    gm.hooks.assertPassCaps = [runCode, parseCode](const std::string& code, long from, long to, const NamedMap& nm,
                                                   const std::vector<std::pair<long, long>>& caps,
                                                   const ParamMap& pm) -> bool {
        if (!parseCode(code)) return true; // unparseable assertion → lenient pass
        return runCode(code, from, to, nm, pm, &caps).truthy();
    };
    gm.hooks.run = [runCode, pendingMakeCode](const std::string& code, long from, long to, const NamedMap& nm, const ParamMap& pm) {
        // a `make` block is deferred to build() (it may reference children's .made);
        // other blocks (`:my`, indentation assignments) run now for their side effects.
        if (code.find("make") != std::string::npos) (*pendingMakeCode)[{from, to}].push_back(code);
        else runCode(code, from, to, nm, pm);
    };
    // The same, with the cursor's capture lists — preferred by the engine when set,
    // and the only way a mid-match `$<n>` sees a repeated name as the list it is.
    gm.hooks.runCursor = [runCode, pendingMakeCode](const std::string& code, long from, long to,
                                                    const NamedMap& nm,
                                                    const std::vector<std::pair<long, long>>& caps,
                                                    const RxCursorCaps& cc, const ParamMap& pm) {
        if (code.find("make") != std::string::npos) (*pendingMakeCode)[{from, to}].push_back(code);
        else runCode(code, from, to, nm, pm, &caps, &cc);
    };
    gm.hooks.str = [runCode](const std::string& expr, const NamedMap& nm, const ParamMap& pm) -> std::string {
        // Fast path: a bare `$param` atom (e.g. `$indent`) is by far the most common
        // VarMatch in real grammars and is matched constantly. Resolve it straight from
        // the rule's param bindings, skipping parse/Match-build/overlay/exec entirely.
        auto it = pm.find(expr);
        if (it != pm.end()) return it->second;
        return runCode(expr, 0, 0, nm, pm).toStr();
    };
    // `<{ … }>` — the block's value is the pattern, read with the cursor's
    // captures beside it (a token may well write `(\w) <{ $0 }>`).
    gm.hooks.dynRule = [this, runCode](const std::string& code, long from, long to, const NamedMap& nm,
                                       const std::vector<std::pair<long, long>>& caps, const ParamMap& pm,
                                       const std::string& flags) -> const Regex* {
        return dynRegexFor(runCode(code, from, to, nm, pm, &caps), flags);
    };
    gm.hooks.range = [this, runCode](const std::string& code, const NamedMap& nm, const ParamMap& pm) -> std::pair<long, long> {
        // `** { N..* }` / `..Inf` / `..∞` is an unbounded quantifier — detect it from the
        // source (a Whatever endpoint numifies to 0, which would wrongly mean "exactly N").
        bool unbounded = code.find("..*") != std::string::npos || code.find("..Inf") != std::string::npos ||
                         code.find("..\xE2\x88\x9E") != std::string::npos;
        return dynQuantLimits(runCode(code, 0, 0, nm, pm), unbounded);
    };
    // Snapshot/restore the interpreter side-effects an LTM probe may touch. We roll back
    // `:my` vars (a probe branch may set e.g. $new-indent that the committed branch must
    // re-derive), but NOT the deferred-make queues: those are keyed by span and consumed
    // in build() strictly by the FINAL tree's spans, so entries left by a probed-but-
    // rejected branch are inert. Copying the whole (O(input)-sized) make queues on every
    // `|` — and `|` is hit O(input) times — was quadratic; skipping it keeps probes O(1).
    struct GState { VarMap vars; };
    // Shared sentinel meaning "scope was empty at snapshot time" — restoring it just
    // clears whatever the probe added, with no per-probe allocation or map copy. Most
    // LTM `|` probes happen before any `:my` var exists, so this is the common path.
    auto emptySentinel = std::make_shared<int>(0);
    gm.hooks.saveState = [this, emptySentinel]() -> std::shared_ptr<void> {
        if (tctx_.cur->vars.empty()) return emptySentinel;
        auto s = std::make_shared<GState>();
        s->vars = tctx_.cur->vars;
        return s;
    };
    gm.hooks.restoreState = [this, emptySentinel](std::shared_ptr<void> v) {
        if (v == emptySentinel) { tctx_.cur->vars.clear(); return; }
        auto s = std::static_pointer_cast<GState>(v);
        tctx_.cur->vars = s->vars;
    };
    // …and the reason a rule's `:my %*FOO` has to be carried on the NODE rather
    // than simply left in the scope: the restore above wipes it when the rule
    // exits, while that rule's ACTION does not run until the whole parse is over
    // (actions replay bottom-up from the recorded tree — see `build`). Only a
    // `:my` in TOP used to survive, because TOP is not entered through the
    // subrule path and so nothing rolls it back; an action could read a dynamic
    // declared in TOP and in no other rule. Rakudo runs the action inside the
    // rule's own frame, where every enclosing `:my` is visible, so a node
    // snapshots the `*`-twigil dynamics live at its completion and `build` puts
    // them back for that node's whole subtree.
    //
    // Only nodes beneath a `:my` SUBRULE get here at all: the matcher keeps a
    // depth count (dynScopeDepth_) and does not call this when it is zero, so a
    // grammar that declares no dynamics — and most nodes of one that does —
    // pays one integer test per completion, not a call. When it is called the
    // scan is over matchScope, the small isolated map the comment below
    // describes, which holds a handful of entries.
    gm.hooks.captureDyn = [this]() -> std::shared_ptr<const void> {
        if (!tctx_.cur) return nullptr;
        std::shared_ptr<GrammarDynSnap> snap;
        for (auto& kv : tctx_.cur->vars) {
            if (kv.first.size() < 2 || kv.first[1] != '*') continue;
            if (!snap) snap = std::make_shared<GrammarDynSnap>();
            snap->push_back({kv.first, kv.second});
        }
        return snap;
    };

    // `<.name>` where `name` is an ordinary METHOD of the grammar (issue #64):
    // Rakudo dispatches every subrule call as a method call on the cursor, so
    // `[ 'c' || <.panic("expected c")> ]` reaches `method panic` — which dies
    // with a line number, the standard way a grammar reports a parse error.
    // The engine asks once per name whether a method exists; the call hands
    // the method a cursor as `self` (a Match at the call position carrying the
    // whole subject, so `self.pos` / `self.target` read as in Rakudo) and takes
    // a returned Match as the subrule's match. Its `.orig` is the parse's
    // shared subject.
    // `<Other::Grammar::rule>` — a QUALIFIED call into another grammar's rule,
    // run there as a subparse at the cursor (the plain-regex resolver does
    // the same). Answers the grammar, and the rule's short name in `rname`.
    auto qualGrammar = [this](const std::string& nm, std::string& rname) -> ClassInfo* {
        auto qs = nm.rfind("::");
        if (qs == std::string::npos || qs == 0) return nullptr;
        std::string gname = nm.substr(0, qs);
        rname = nm.substr(qs + 2);
        auto cit = classes_.find(gname);
        if (cit == classes_.end()) cit = classes_.find(resolveClassAlias(gname));
        return cit != classes_.end() && cit->second && cit->second->findRule(rname) ? cit->second.get() : nullptr;
    };
    gm.hooks.hasMethod = [g, qualGrammar](const std::string& nm) -> bool {
        std::string rn;
        return g->findMethodForCall(nm) != nullptr || qualGrammar(nm, rn) != nullptr;
    };
    // A metaclass that overrides find_method (`grammar G {…}` under an
    // `EXPORTHOW.WHO.<grammar>` supersede — a profiler): every call the
    // grammar's own rules and methods answer is resolved through it
    // (GrammarHooks::viaHow).
    bool howFind = false;
    if (g->howObj.t == VT::Object && g->howObj.obj())
        for (ClassInfo* c = g->howObj.obj()->cls.get(); c && !howFind; c = c->parent.get())
            howFind = c->methods.count("find_method") > 0;
    gm.hooks.viaHow = howFind;
    gm.hooks.callMethod = [this, g, runCode, targetStr, qualGrammar, howFind](
            const std::string& name, const std::string& args, long pos,
            const NamedMap& named, const std::vector<std::pair<long, long>>& caps,
            const ParamMap& params, RxCursorCall& call, long& endOut, ParseNode& nodeOut) -> int {
        ClassInfo* owner = nullptr;
        Value* method = g->findMethodForCall(name, langRev_ < 2, &owner);
        // Ask the metaclass for the code to run; `callsame` inside its
        // find_method answers what the engine would have found (the rule or
        // method itself). Whatever it hands back runs with the cursor as its
        // first argument — a wrapper then calls the original on that cursor.
        Value howCode;
        if (howFind && (method || g->findRule(name))) {
            const Value tobj = Value::typeObj(g->name);
            Value own = methodCall(tobj, "^find_method", ValueList{Value::str(name)});
            RedispatchCtx rc;
            rc.sameArgs = ValueList{tobj, Value::str(name)};
            rc.next = [own](ValueList) { return own; };
            redispatchStack_.push_back(std::move(rc));
            try { howCode = methodCall(g->howObj, "find_method", ValueList{tobj, Value::str(name)}); }
            catch (...) { redispatchStack_.pop_back(); throw; }
            redispatchStack_.pop_back();
            if (howCode.t != VT::Code || !howCode.code()) return 0;
        }
        if (!method && howCode.t != VT::Code) {
            std::string rname;
            ClassInfo* og = qualGrammar(name, rname);
            if (!og) return 0;
            Value m = grammarParse(og, targetStr->substr((size_t)pos), /*subparse=*/true, rname, Value());
            if (!isDefined(m)) return 0;
            endOut = pos + (long)m.s.size();
            nodeOut = ParseNode{};
            matchValueToNode(m, pos, nodeOut);
            return 1;
        }
        // the call's arguments are Raku source, evaluated where a rule's own
        // code blocks are: rule params and the match so far in scope
        ValueList av;
        if (!args.empty()) {
            Value lst = runCode("(" + args + ",)", pos, pos, named, params, &caps);
            if (lst.t == VT::Array && lst.arr()) for (auto& e : *lst.arr()) av.push_back(e);
            else if (lst.t != VT::Any) av.push_back(lst);
        }
        auto cur = std::make_shared<GrammarCursor>();
        cur->grammar = g; cur->input = targetStr;
        cur->live = std::make_shared<RxCursorCall*>(&call);
        Value self = Value::matchVal("", pos, pos);
        self.extM() = targetStr;
        self.mdW().cursor = cur;
        Value r;
        try {
            if (howCode.t == VT::Code) {
                // a METHOD handed back as it was runs as one; anything else —
                // a wrapper block, the rule itself — is called with the cursor
                if (howCode.code()->isMethod && !howCode.code()->isRegexRoutine && !howCode.code()->builtin)
                    r = invokeMethod(howCode, self, std::move(av));
                else {
                    av.insert(av.begin(), self);
                    r = callCallable(howCode, std::move(av));
                }
            }
            else r = invokeMethodChain(name, g, self, std::move(av), nullptr, method, owner);
        }
        catch (...) { *cur->live = nullptr; throw; }
        *cur->live = nullptr; // the engine's re-entry point dies with the call
        if (r.t == VT::Match) {
            if (r.rTo() < pos) return 0;   // a failed rule call, handed back as-is
            endOut = r.rTo();
            nodeOut = ParseNode{};
            matchValueToNode(r, 0, nodeOut);
            // a Match that IS a rule's answers to that rule's name, so the tree
            // fires the rule's action, not one named after the method
            if (r.md() && r.md()->cursor) {
                auto rc = std::static_pointer_cast<GrammarCursor>(r.md()->cursor);
                if (!rc->rule.empty()) nodeOut.name = rc->rule;
            }
            return 1;
        }
        if (!rtIsDefined(r)) return 0;
        throw RakuError{Value::typeObj("X::AdHoc"),
            "Method '" + name + "' was called as a subrule and must return a Match, not " + r.typeName()};
    };

    // Completed-subrule log: on an overall parse FAILURE the actions of the
    // subrules that DID complete replay in completion order — Rakudo fires
    // actions during the match and a failing TOP does not unfire them
    // (HTTP::Header sets its fields from a parse that fails on a missing
    // trailing newline). A SUCCESSFUL parse discards the log and fires
    // everything once in the normal bottom-up build — the proven path.
    auto completionLog = std::make_shared<std::vector<ParseNode>>();
    bool replayBuild = false;  // building $/ for a replayed firing: build fires nothing…
    // …except beneath the replayed node, for a kid the log does not replay on
    // its own. The replayed node builds its children, and one it shares with
    // the final tree (the memo answered the second call, so it was logged once,
    // as a tree node) or one the matcher inlined without logging (a
    // single-character rule such as `space`) would otherwise have no `.made`.
    // YAMLish's `<key>` tries `<single-key>` on `'a b'` before a list entry
    // wins with `<single-quoted>`: single-key's action joined Nils (issue #100).
    // Rakudo fires such a kid on every attempt, having no memo.
    const ParseNode* replayRoot = nullptr;
    std::set<std::tuple<std::string, long, long>> replayedKeys;

    // Turn the recorded parse tree into Match values, running actions bottom-up
    // so `$<child>.made` is available to a parent's action.
    std::function<Value(const ParseNode&)> build = [&](const ParseNode& pn) -> Value {
        // Put this node's dynamic scope back for the whole subtree. It has to
        // go on BEFORE the children are built, not just around this node's own
        // make and action: the rule that DECLARES `:my %*FOO` is an ancestor of
        // the rules whose actions read it, and those actions fire on the way
        // back up from here. A nested declaration simply overlays again and
        // restores to this one, so the nesting comes out as it was at match
        // time. Restored when this frame leaves, so a sibling inherits nothing.
        struct DynOverlay {
            std::shared_ptr<Env> env;
            std::vector<std::pair<std::string, Value>> prev;  // names we shadowed
            std::vector<std::string> added;                   // …and names we introduced
            ~DynOverlay() {
                if (!env) return;
                for (auto& kv : prev)  env->vars[kv.first] = kv.second;
                for (auto& n  : added) env->vars.erase(n);
            }
        } dynOverlay;
        if (pn.dynScope && tctx_.cur) {
            auto snap = std::static_pointer_cast<const GrammarDynSnap>(pn.dynScope);
            dynOverlay.env = tctx_.cur;
            for (auto& kv : *snap) {
                auto it = tctx_.cur->vars.find(kv.first);
                if (it != tctx_.cur->vars.end()) dynOverlay.prev.push_back({kv.first, it->second});
                else                             dynOverlay.added.push_back(kv.first);
                tctx_.cur->vars[kv.first] = kv.second;
            }
        }
        Value mv = subMatch(pn.from, pn.to);
        // `<v=rule>` is ONE capture filed under two keys (ParseNode::aliasId):
        // build it once, fire its action once, and hand both keys the same Match.
        std::unordered_map<uint64_t, Value> aliasBuilt;
        auto buildRule = [&](const ParseNode& child) -> Value {
            if (!child.aliasId) return build(child);
            auto hit = aliasBuilt.find(child.aliasId);
            if (hit != aliasBuilt.end()) return hit->second;
            return aliasBuilt.emplace(child.aliasId, build(child)).first->second;
        };
        // Names captured INSIDE a positional group belong to that group's Match,
        // not to this one: `rule array { '{' ( <element> ','?)* '}' }` gives
        // `$0[i]<element>`, and `$/<element>` does not exist (Rakudo scopes a
        // capture to the group that encloses it). Publishing them here too put
        // an extra key in `.hash`, so `.values` answered one entry per element
        // PLUS a hoisted one — which is what DBDish::Pg walks to rebuild a
        // Postgres array literal, and it read every array as a single element.
        std::map<std::string, size_t> consumedByGroup;
        // A capture that captured something ITSELF was recorded as a child node,
        // and that record is what `$n` answers — nesting, rather than the span
        // alone, so `$0[0]` and `$0<name>` reach inside the group.
        std::map<int, const std::vector<ParseNode>*> capKids;
        if (pn.kids) for (auto& ck : *pn.kids)
            if (isPositionalKey(ck.first) && !ck.second.empty() && ck.second[0].capLocal >= 0)
                capKids[ck.second[0].capLocal] = &ck.second;
        for (size_t ci = 0; ci < pn.caps.size(); ci++) {
            auto kit = capKids.find((int)ci);
            if (kit != capKids.end()) {
                const std::vector<ParseNode>& occ = *kit->second;
                if (occ.size() > 1 || (pn.listCaps && pn.listCaps->count((int)ci))) {
                    Value lst = Value::array(); lst.isList = true;
                    for (auto& o : occ) lst.arr()->push_back(build(o));
                    mv.arrRef().push_back(std::move(lst));
                } else mv.arrRef().push_back(build(occ[0]));
                continue;
            }
            // a positional capture under a repetition quantifier is an ARRAY of
            // every occurrence (`(...)+` → @$0), as in Rakudo — Cro::Uri's pchars
            // action concatenates `@$0` chunks to rebuild a path segment
            if (pn.listCaps && pn.listCaps->count((int)ci)) {
                Value lst = Value::array(); lst.isList = true;
                if (pn.capReps) {
                    auto it = pn.capReps->find((int)ci);
                    if (it != pn.capReps->end())
                        for (auto& o : it->second) {
                            Value om = subMatch(o.first, o.second);
                            // each occurrence carries the SUBRULE children whose
                            // spans nest inside it — Cro::Uri's query action reads
                            // `$_<pchars>` per occurrence of `( <pchars> | … )*`
                            if (pn.kids) for (auto& kv : *pn.kids) {
                                if (!kv.first.empty() && kv.first[0] == '\x01') continue; // internal keys
                                Value hits = Value::array(); hits.isList = true;
                                for (auto& child : kv.second)
                                    // (an EMPTY capture where the occurrence ends came
                                    // after it: `("XX")+ %% $<d>=<[a..z]>*`)
                                    if (child.from >= o.first && child.to <= o.second &&
                                        !(child.from == child.to && child.from == o.second && o.second > o.first)) {
                                        consumedByGroup[kv.first]++;
                                        hits.arr()->push_back(child.name.empty()
                                            ? subMatch(child.from, child.to)
                                            : buildRule(child));
                                    }
                                if (hits.arr()->size() == 1) om.hashRef()[kv.first] = (*hits.arr())[0];
                                else if (!hits.arr()->empty()) om.hashRef()[kv.first] = hits;
                            }
                            lst.arr()->push_back(std::move(om));
                        }
                }
                mv.arrRef().push_back(std::move(lst));
                continue;
            }
            auto& c = pn.caps[ci];
            if (c.first < 0) mv.arrRef().push_back(Value::nil());
            else mv.arrRef().push_back(subMatch(c.first, c.second));
        }
        for (auto& kv : pn.named)
            if (!pn.kids || !pn.kids->count(kv.first))
                mv.hashRef()[kv.first] = subMatch(kv.second.first, kv.second.second);
        // a leaf with no rule name is a plain $<x>=[…] capture: just its span, no actions
        auto buildChild = [&](const ParseNode& child) -> Value {
            if (child.name.empty())
                return subMatch(child.from, child.to);
            return buildRule(child);
        };
        if (pn.kids) for (auto& kv : *pn.kids) {
            if (isPositionalKey(kv.first)) continue;   // a NUMBER — presented above
            {   // wholly inside a positional group: it is the GROUP's capture, not ours
                auto cg = consumedByGroup.find(kv.first);
                if (cg != consumedByGroup.end() && cg->second >= kv.second.size()) continue;
            }
            // a name captured more than once ($<num> ... $<num>) collates into a list —
            // and a name under a quantifier (<item>+) is a list even with one occurrence
            bool asList = kv.second.size() > 1
                       || (pn.listNames && pn.listNames->count(kv.first));
            if (!asList) mv.hashRef()[kv.first] = buildChild(kv.second[0]);
            else {
                Value arr = Value::array(); arr.isList = true;
                for (auto& child : kv.second) arr.arr()->push_back(buildChild(child));
                mv.hashRef()[kv.first] = arr;
            }
        }
        // a quantified capture that matched zero times is an empty list, not absent
        if (pn.listNames) for (auto& nm : *pn.listNames)
            if ((!pn.kids || !pn.kids->count(nm)) && !mv.hashRef().count(nm) &&
                !consumedByGroup.count(nm)) {
                Value arr = Value::array(); arr.isList = true;
                mv.hashRef()[nm] = arr;
            }
        // a deferred inline `{ make … }` runs now, with $/ = this fully-built match
        // (so it can read `$/.values[0].ast` etc.) and this node as the make target.
        // Pop the innermost queued make for this span — but only when THIS rule's
        // body actually contains the block: a parent and its only child share the
        // span (`token TOP { <number> {make …} }`), and the make is the parent's.
        auto mc = pendingMakeCode->find({pn.from, pn.to});
        static const bool debugMake = std::getenv("RAKUPP_DEBUG_MAKE") != nullptr; // per NODE otherwise (profiled)
        if (debugMake)
            fprintf(stderr, "[make] node=%s span=%ld..%ld queued=%s\n", pn.name.c_str(), pn.from, pn.to,
                    mc != pendingMakeCode->end() && !mc->second.empty() ? mc->second.front().c_str() : "(none)");
        size_t makePick = 0;
        if (mc != pendingMakeCode->end() && !mc->second.empty()) {
            const std::string* rulePat = g ? g->findRule(
                pn.actualRule.empty() ? pn.name : pn.actualRule) : nullptr;
            // Pick the queued block that belongs to THIS rule. Several rules can
            // share one span — a parent and its only child, and (under LTM) every
            // protoregex candidate that matched, not just the winner. A blind FIFO
            // pop then hands a node a loser's block and drops the node's own:
            // YAMLish's `element:<int>`/`<float>` both match "42", so TOP's
            // `make $/.values[0].ast` was discarded and every number came out Nil.
            auto ownedBy = [&](const std::string& code) {
                if (rulePat && rulePat->find(code) != std::string::npos) return true;
                // a proto's tree node keeps the PROTO name while the make block
                // lives in the winning `name:sym<…>` candidate — accept it if
                // any candidate's pattern contains the code
                std::string prefix = pn.name + ":";
                for (const ClassInfo* ci = g; ci; ci = ci->parent.get())
                    for (auto& rk : ci->rules)
                        if (rk.first.rfind(prefix, 0) == 0 &&
                            rk.second.find(code) != std::string::npos) return true;
                return false;
            };
            // Is this block the body of some rule the grammar actually declares?
            auto ownedBySomeRule = [&](const std::string& code) {
                for (const ClassInfo* ci = g; ci; ci = ci->parent.get())
                    for (auto& rk : ci->rules)
                        if (rk.second.find(code) != std::string::npos) return true;
                return false;
            };
            if (!rulePat) {
                // No pattern to compare against: this node is a BUILT-IN assertion
                // (`<sym>`, a char class), not a declared rule. Keep FIFO order
                // only for a block no declared rule claims — otherwise `<sym>`
                // inside `token match:sym<*> { <sym> { make … } }` swallowed the
                // candidate's own block, and `$<match>.made` came out empty.
                if (ownedBySomeRule(mc->second.front())) mc = pendingMakeCode->end();
                else makePick = 0;
            }
            else {
                makePick = (size_t)-1;
                for (size_t i = 0; i < mc->second.size(); i++)
                    if (ownedBy(mc->second[i])) { makePick = i; break; }
                if (makePick == (size_t)-1) mc = pendingMakeCode->end();
            }
        }
        if (mc != pendingMakeCode->end() && !mc->second.empty()) {
            std::string code = mc->second[makePick];
            mc->second.erase(mc->second.begin() + makePick);
            if (auto prog = parseCode(code)) {
                Value* slot = tctx_.cur->find("$/"); Value saved = slot ? *slot : Value::nil();
                setMatchVar(mv);
                tctx_.makeTargets.push_back(&mv);
                try { for (auto& s : prog->stmts) exec(s.get()); }
                catch (const RakuError& e) {
                    if (std::getenv("RAKUPP_DEBUG_MAKE"))
                        fprintf(stderr, "[make] threw: %s\n", e.message.c_str());
                }
                catch (...) {
                    if (std::getenv("RAKUPP_DEBUG_MAKE")) fprintf(stderr, "[make] threw (non-Raku)\n");
                }
                tctx_.makeTargets.pop_back();
                if (Value* s2 = tctx_.cur->find("$/")) *s2 = saved;
            }
        }
        auto pm = pendingMakes->find({pn.from, pn.to});
        if (pm != pendingMakes->end() && !mv.pairVal()) mv.setPairVal(std::make_shared<Value>(pm->second));
        // a made value the success replay recorded for THIS rule at THIS span
        // (its own name — never a same-span sibling's): applied when the parent's
        // replay rebuilds this child with its action suppressed
        if (!mv.pairVal()) {
            auto pmn = pendingMakesNamed->find({pn.name, pn.from, pn.to});
            if (pmn != pendingMakesNamed->end()) mv.setPairVal(std::make_shared<Value>(pmn->second));
        }
        // an action-class method (if any) can still override; a proto node
        // dispatches to the winning candidate's method (`x:sym<y>`), falling
        // back to a method named after the proto itself. (A failure-replay
        // build dispatches its own method — build must not double-fire.)
        if (!replayBuild || (&pn != replayRoot && !replayedKeys.count({pn.name, pn.from, pn.to}))) {
            // A proto used as the parse ENTRY POINT (`.parse(:rule<string>)`)
            // records the winning candidate in actualRule and the PROTO in name.
            // Fire the candidate — and NOT the proto as well: an explicit `proto
            // method string {*}` in the actions class, invoked directly with just
            // `$/`, cannot pick a `:sym<…>` candidate and dies "No matching multi
            // candidate" (CSS::Grammar parses `string` as its entry rule). Rakudo
            // fires the candidate once; the proto merely redispatches. actualRule
            // is set ONLY on this proto-entry path, so this never drops a wanted
            // firing elsewhere.
            if (!pn.actualRule.empty() && pn.actualRule != pn.name) runAction(pn.actualRule, mv);
            else runAction(pn.name, mv);
        }
        return mv;
    };

    // Log completed subrules that HAVE an action method: on an overall failure
    // their actions replay in completion order (see completionLog above).
    if (haveActions && actCls) {
        gm.hooks.hasAction = [resolveAction](const std::string& nm) -> bool {
            if (resolveAction(nm)) return true; // cached; guillemet form included
            size_t c = nm.find(':'); // proto candidate falls back to the base name
            if (c != std::string::npos && c > 0 && resolveAction(nm.substr(0, c))) return true;
            return false;
        };
        gm.hooks.onRule = [completionLog](ParseNode pn) {
            completionLog->push_back(std::move(pn)); // moved end-to-end from the fire site
        };
    }

    // Match against a dedicated child scope so match-time `:my` vars land in a small,
    // isolated map — the LTM `|` snapshot (saveState) then copies O(:my vars) per probe,
    // not the whole enclosing scope (which for a module-level parse holds every sub/var).
    ParseNode tree; long endPos = -1;
    bool matched;
    {
        auto matchScope = std::make_shared<Env>();
        matchScope->parent = tctx_.cur;
        // `.parse($s, :rule<TOP>, args => (…))` binds the START RULE's parameters.
        // They land in the match scope as real VALUES rather than as the textual
        // params a `<rule($x)>` call passes, so a `$*`-sigil one is a genuine
        // dynamic every nested rule can read and a Bool stays a Bool — the
        // stringified form made `<?{ $*extended }>` true for `False`.
        // (DateTime::Grammar gates its whole extended-format branch on it.)
        if (ruleArgs && !ruleArgs->empty())
            if (const std::vector<std::string>* pn = g->findRuleParams(startRule)) {
                // a Pair is a NAMED argument: it binds the parameter of its name
                // (`rule r(:$arg)` with `:args(\(:arg(42)))`); the rest bind in order
                size_t pi = 0;
                for (auto& ra : *ruleArgs) {
                    if (ra.t == VT::Pair && ra.namedArg) {
                        for (auto& nm : *pn) {
                            std::string bare = nm;
                            while (!bare.empty() && (bare[0] == ':' || bare[0] == '*')) bare.erase(0, 1);
                            if (bare.size() > 1 && bare.substr(1) == ra.s.str()) {
                                matchScope->define(bare, ra.pairVal() ? *ra.pairVal() : Value::boolean(true));
                                break;
                            }
                        }
                        continue;
                    }
                    // (a `:$name` parameter — marked by its leading ':' — is named only)
                    while (pi < pn->size() && ((*pn)[pi].empty() || (*pn)[pi][0] == ':')) pi++;
                    if (pi < pn->size()) matchScope->define((*pn)[pi++], ra);
                }
            }
        auto savedScope = tctx_.cur;
        tctx_.cur = matchScope;
        // a grammar method may `die` mid-parse (issue #64): the exception is the
        // caller's, the match scope is not
        try { matched = gm.parse(input, startRule, subparse, tree, endPos, startPos); }
        catch (...) { tctx_.cur = savedScope; throw; }
        if (consumedEnd) *consumedEnd = matched ? endPos : -1;
        // G1: publish the highwater for rakupp-parse-diagnosis — byte offset
        // to CHARACTER position here, where the input is at hand. A success
        // clears it; a stale diagnosis must not outlive the parse it names.
        if (!matched && gm.hwPos >= 0) {
            long cp = 0;
            for (long b = 0; b < gm.hwPos && b < (long)input.size(); b++)
                if (((unsigned char)input[b] & 0xC0) != 0x80) cp++;
            grammarParseDiag() = {true, cp, gm.hwRule};
        }
        else grammarParseDiag() = {};
        if (!matched) {
            // the parse FAILED, but the subrules that completed still fire their
            // actions, in completion order — Rakudo's during-match firing does
            // not unfire on a later failure (HTTP::Header relies on the side
            // effects of exactly such a parse)
            if (haveActions && actCls && !completionLog->empty()) {
                replayBuild = true;
                replayedKeys.clear();
                for (auto& pn : *completionLog) replayedKeys.insert({pn.name, pn.from, pn.to});
                for (auto& pn : *completionLog) {
                    Value mv;
                    replayRoot = &pn;
                    try { mv = build(pn); } catch (...) { continue; }
                    if (!pn.actualRule.empty() && pn.actualRule != pn.name)
                        try { runAction(pn.actualRule, mv); } catch (RakuError&) {}
                    try { runAction(pn.name, mv); }
                    catch (RakuError&) {} // a throwing action cannot abort a parse that already failed
                    // proto-candidate fallback: `x:sym<y>` fires `method x` when
                    // no candidate-named method exists
                    size_t c = pn.name.find(':');
                    if (c != std::string::npos && c > 0 && !actCls->findMethod(pn.name))
                        try { runAction(pn.name.substr(0, c), mv); } catch (RakuError&) {}
                    // the replay is bottom-up (completion order): store this
                    // made so a later PARENT's build sees `$<child>.made`
                    if (std::getenv("RAKUPP_DEBUG_REPLAY"))
                        fprintf(stderr, "[replay] %s %ld..%ld made=%s\n", pn.name.c_str(),
                                pn.from, pn.to, mv.pairVal() ? "yes" : "no");
                    if (mv.pairVal()) (*pendingMakes)[{pn.from, pn.to}] = *mv.pairVal();
                }
                replayBuild = false; replayRoot = nullptr;
            }
            tctx_.cur = savedScope;
            setMatchVar(Value::nil()); return Value::nil();
        }
        // Success: the bottom-up build below fires an action for every node
        // in the TREE — but a rule reached only through a non-capturing call
        // (`<.ws>` → `<.comment>` → `<unclosed-comment>`, CSS::Grammar's
        // unterminated-comment warning) leaves no node, and Rakudo fires its
        // action all the same. Those completions are in the log; replay the
        // ones the tree does not carry, in completion order, exactly as the
        // failure path does.
        if (haveActions && actCls && !completionLog->empty()) {
            std::set<std::tuple<std::string, long, long>> inTree;
            std::function<void(const ParseNode&)> walk = [&](const ParseNode& pn) {
                inTree.insert({pn.name, pn.from, pn.to});
                if (!pn.actualRule.empty()) inTree.insert({pn.actualRule, pn.from, pn.to});
                if (!pn.kids) return;
                std::unordered_set<uint64_t> seen;   // an alias pair is one subtree: walk it once
                for (auto& kv : *pn.kids)
                    for (auto& k : kv.second)
                        if (!k.aliasId || seen.insert(k.aliasId).second) walk(k);
            };
            walk(tree);
            replayBuild = true;
            replayedKeys.clear();
            for (auto& pn : *completionLog)
                if (!inTree.count({pn.name, pn.from, pn.to})) replayedKeys.insert({pn.name, pn.from, pn.to});
            for (auto& pn : *completionLog) {
                if (inTree.count({pn.name, pn.from, pn.to})) continue;
                Value mv;
                replayRoot = &pn;
                try { mv = build(pn); } catch (...) { continue; }
                if (!pn.actualRule.empty() && pn.actualRule != pn.name)
                    try { runAction(pn.actualRule, mv); } catch (RakuError&) {}
                try { runAction(pn.name, mv); } catch (RakuError&) {}
                // Keep the made, but ONLY under this node's own name+span, so a
                // PARENT replayed later (completion order is bottom-up) can read
                // `$/<child>.made` when it rebuilds this child. A same-span
                // captured rule has a different NAME and so cannot inherit it —
                // the span-only map's hazard that this twin exists to avoid.
                if (mv.pairVal()) {
                    (*pendingMakesNamed)[{pn.name, pn.from, pn.to}] = *mv.pairVal();
                    if (!pn.actualRule.empty() && pn.actualRule != pn.name)
                        (*pendingMakesNamed)[{pn.actualRule, pn.from, pn.to}] = *mv.pairVal();
                }
            }
            replayBuild = false; replayRoot = nullptr;
        }
        completionLog->clear(); // the normal bottom-up build fires the rest once
        // build with the match scope still current: a deferred `{ make … }`
        // may read the rule's `:my` vars (Cro's route matcher makes `$cap`)
        Value mv;
        try { mv = build(tree); }
        catch (...) { tctx_.cur = savedScope; throw; }
        tctx_.cur = savedScope;
        setMatchVar(mv);
        return mv;
    }
}

// Shared hyper-operator core for every spelling (`>>op<<`, `»op«`, and the
// bracketed `>>[&op]<<` form). A `>>` on the left / `<<` on the right marks
// that side STRICT — it dictates the shape; a dwimmy side is CYCLED to the
// strict length (truncating when longer), but a strict SCALAR facing a list
// dies, as does a dwimmy side that is known-infinite in a position where no
// finite side dictates the length. Hash keysets follow Rakudo: both strict →
// union, one strict → that side's keys, both dwimmy → intersection.
// the infix a hyper is applying, for X::HyperOp::NonDWIM's `operator`
thread_local const std::string* g_hyperOpName = nullptr;

Value Interpreter::hyperCore(Value& l, Value& r, bool strictL, bool strictR,
        const std::function<Value(const Value&, const Value&, Value*, Value*)>& apply,
        Value* lroot, Value* rroot, bool wantSlots) {
    // A shaped operand distributes over its LEAVES, and the result is the flat
    // list of them: `@a[3;2] >>+>> 1` is (2, 3, 4, 5, 6, 7), not three rows.
    // Done once here rather than in deepApply, which recurses on plain rows.
    Value lflat, rflat;
    if (isMultiDimShaped(l)) { lflat = Value::array(shapedLeaves(l)); lflat.isList = true; l = lflat; }
    if (isMultiDimShaped(r)) { rflat = Value::array(shapedLeaves(r)); rflat.isList = true; r = rflat; }
    // element-level distribution: a Pair keeps its key and ops its value; a
    // nested array/hash element recurses with the same marker rules — so
    // ([1,2],[3,[4,5]]) »+« ([6,7],[8,[9,10]]) distributes deeply, and a hash
    // sitting in an array ops its VALUES against the other side's element.
    std::function<Value(const Value&, const Value&, Value*, Value*)> deepApply =
        [&](const Value& x, const Value& y, Value* xs, Value* ys) -> Value {
        bool xP = x.t == VT::Pair, yP = y.t == VT::Pair;
        if (xP || yP) {
            const std::string& key = xP ? x.s : y.s;
            const Value& xv = xP && x.pairVal() ? *x.pairVal() : x;
            const Value& yv = yP && y.pairVal() ? *y.pairVal() : y;
            return Value::pair(key, deepApply(xv, yv, nullptr, nullptr));
        }
        bool xC = (x.t == VT::Array && x.arr()) || x.t == VT::Range || (x.t == VT::Hash && x.hash());
        bool yC = (y.t == VT::Array && y.arr()) || y.t == VT::Range || (y.t == VT::Hash && y.hash());
        if (xC || yC) {
            Value x2 = x, y2 = y;
            Value res = hyperCore(x2, y2, strictL, strictR, apply);
            // an itemized [..] operand yields an itemized (mutable) Array result
            if (res.t == VT::Array && ((x.t == VT::Array && !x.isList) ||
                                       (y.t == VT::Array && !y.isList)))
                res.isList = false;
            return res;
        }
        return apply(x, y, xs, ys);
    };
    auto dwimDie = [&](size_t le = 0, size_t re = 0) -> RakuError {
        const std::string opn = g_hyperOpName ? *g_hyperOpName : std::string();
        Value opv = Value::any();
        if (!opn.empty()) {
            if (Value* f = tctx_.cur->find("&infix:<" + opn + ">")) opv = *f;
            else {   // a built-in infix: a routine value that answers its name
                opv = Value::closure([](ValueList&) { return Value::nil(); });
                opv.code()->name = "infix:<" + opn + ">";
            }
        }
        const std::string msg = "Lists on either side of non-dwimmy hyperop of infix:<" + opn +
            "> are not of the same length\nleft: " + std::to_string(le) + " elements, right: " +
            std::to_string(re) + " elements";
        return RakuError{makeTypedEx("X::HyperOp::NonDWIM",
            {{"left-elems", Value::integer((long long)le)}, {"right-elems", Value::integer((long long)re)},
             {"operator", opv}, {"recursing", Value::boolean(false)}}, msg), msg};
    };
    static const std::set<std::string> settyKinds = {
        "Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash"};
    // rebuild a per-key result with the source hash's kind (SetHash/BagHash/…
    // re-coerce their counts exactly like `%h is Kind = pairs` does)
    auto hashOut = [&](const std::vector<std::pair<std::string, Value>>& pairs,
                       const Value& src) -> Value {
        if (src.t == VT::Hash && settyKinds.count(src.hashKind)) {
            ValueList pl;
            for (auto& kv : pairs) pl.push_back(Value::pair(kv.first, kv.second));
            return makeBaggy(pl, src.hashKind);
        }
        Value out = Value::makeHash();
        if (src.t == VT::Hash) out.hashKind = src.hashKind; else out.hashKind.clear();
        // A hyper over a TYPED or OBJECT-KEYED hash answers the same flavour:
        // `my Cool %a; %a >>~<< %b` is a Hash[Cool] and `my %a{Any}` stays
        // Hash[Any,Any]. Only hashKind travelled, so every result came back a
        // plain Hash.
        if (src.t == VT::Hash) { out.ofTypeM() = src.ofType(); out.objKeyed = src.objKeyed; }
        for (auto& kv : pairs) (*out.hash())[kv.first] = kv.second;
        return out;
    };
    bool lHash = l.t == VT::Hash && l.hash();
    bool rHash = r.t == VT::Hash && r.hash();
    if (lHash && rHash) {
        std::vector<std::pair<std::string, Value>> pairs;
        auto lval = [&](const std::string& k) { auto it = l.hash()->find(k); return it != l.hash()->end() ? it->second : Value::any(); };
        auto rval = [&](const std::string& k) { auto it = r.hash()->find(k); return it != r.hash()->end() ? it->second : Value::any(); };
        auto lslot = [&](const std::string& k) -> Value* { auto it = l.hash()->find(k); return it != l.hash()->end() ? &it->second : nullptr; };
        auto rslot = [&](const std::string& k) -> Value* { auto it = r.hash()->find(k); return it != r.hash()->end() ? &it->second : nullptr; };
        auto applyK = [&](const std::string& k) {
            pairs.push_back({k, deepApply(lval(k), rval(k), lslot(k), rslot(k))});
        };
        if (strictL && strictR) { // union — a strict side may not be shrunk
            for (auto& kv : *l.hash()) applyK(kv.first);
            for (auto& kv : *r.hash()) if (!l.hash()->count(kv.first)) applyK(kv.first);
        }
        else if (strictL) for (auto& kv : *l.hash()) applyK(kv.first);
        else if (strictR) for (auto& kv : *r.hash()) applyK(kv.first);
        else { // both dwimmy → intersection
            for (auto& kv : *l.hash()) if (r.hash()->count(kv.first)) applyK(kv.first);
        }
        return hashOut(pairs, l);
    }
    if (lHash || rHash) {
        // hash ⨯ scalar / scalar ⨯ hash: the scalar side must be dwimmy
        // (it's "extended" over every key); a strict scalar side dies.
        Value& h = lHash ? l : r;
        if (lHash ? strictR : strictL) throw dwimDie();
        std::vector<std::pair<std::string, Value>> pairs;
        for (auto& kv : *h.hash())
            pairs.push_back({kv.first, lHash ? deepApply(kv.second, r, &kv.second, rroot)
                                             : deepApply(l, kv.second, lroot, &kv.second)});
        return hashOut(pairs, h);
    }
    // a CArray operand hypers over its DECODED elements (`@vector <<*>> @vector`
    // is the pure norm in Math::DistanceFunctions) — as one item it reached
    // applyArith, which tried to numify the raw byte string
    auto carrayList = [](Value& v) {
        if (v.t == VT::Str && v.hashKind == "CArray") {
            Value out = Value::array(); out.isList = true;
            std::string et = v.enumName.empty() ? std::string("int64") : v.enumName.str();
            int w = Interpreter::ncElemSize(et);
            long long n = w > 0 ? (long long)(v.s.size() / (size_t)w) : 0;
            for (long long i = 0; i < n; i++)
                out.arr()->push_back(Interpreter::ncReadElem((long long)(intptr_t)v.s.data(), et, i));
            v = out;
        }
    };
    carrayList(l); carrayList(r);
    // an object that IS an array (`class Vector is Array`) hypers over the
    // elements it boxes
    auto unboxList = [](Value& v) {
        if (v.t == VT::Object && v.obj() && v.obj()->hasBoxed && v.obj()->boxed.t == VT::Array)
            { Value unboxed = v.obj()->boxed; v = std::move(unboxed); }
    };
    unboxList(l); unboxList(r);
    forceLazy(l); forceLazy(r);   // a gather operand: its elements
    bool lIter = l.t == VT::Array || l.t == VT::Range;
    bool rIter = r.t == VT::Array || r.t == VT::Range;
    auto isInf = [](const Value& v) {
        if (v.t == VT::Range && v.rTo() >= 9000000000000000000LL) return true;
        if (v.t == VT::Array && v.ext())
            return std::static_pointer_cast<LazySeqState>(v.ext())->infinite;
        return false;
    };
    bool lInf = isInf(l), rInf = isInf(r);
    if (lInf || rInf) {
        // an infinite side is fine only where a finite STRICT side dictates n
        bool ok = (lInf && !rInf && !strictL && strictR && rIter) ||
                  (rInf && !lInf && strictL && !strictR && lIter);
        if (!ok) {
            const std::string side = lInf && rInf ? "both" : lInf ? "left" : "right";
            throwTypedV("X::HyperOp::Infinite", {{"side", Value::str(side)}},
                std::string("Lists on ") + (lInf && rInf ? "both sides" : lInf ? "left side" : "right side") +
                " of hyperop are known to be infinite");
        }
    }
    // ONE level of elements — nested arrays stay whole so deepApply distributes
    // into them (flatten() would destroy the shape)
    ValueList la, ra;
    if (!lInf) la = l.t == VT::Array && l.arr() ? *l.arr() : lIter ? l.flatten() : ValueList{l};
    if (!rInf) ra = r.t == VT::Array && r.arr() ? *r.arr() : rIter ? r.flatten() : ValueList{r};
    // An EMPTY side annihilates: `True »+» ()` and `() «+« True` are both (),
    // never a length complaint. Only the doubly-strict `»+«` still objects to
    // the mismatch, which is the one shape Rakudo throws on.
    if (!lInf && !rInf && (la.empty() || ra.empty()) && !(strictL && strictR)) {
        Value empty = Value::array();
        const Value& sh = lIter ? l : r;
        empty.isList = !(sh.t == VT::Array && !sh.isList);
        return empty;
    }
    // `(1, 2, 3, *)` — a trailing Whatever extends the list by repeating its
    // last element to whatever length the other side needs
    bool lStar = !lInf && la.size() >= 2 && la.back().t == VT::Whatever;
    bool rStar = !rInf && ra.size() >= 2 && ra.back().t == VT::Whatever;
    if (lStar || rStar) {
        if (lStar) la.pop_back();
        if (rStar) ra.pop_back();
        size_t want = lStar && rStar ? std::max(la.size(), ra.size())
                    : lStar ? ra.size() : la.size();
        if (lStar) while (la.size() < want) la.push_back(la.back());
        if (rStar) while (ra.size() < want) ra.push_back(ra.back());
        if (!lStar && la.size() < want && !strictL) { size_t k = la.size(); for (size_t i = 0; la.size() < want; i++) la.push_back(la[i % k]); }
        if (!rStar && ra.size() < want && !strictR) { size_t k = ra.size(); for (size_t i = 0; ra.size() < want; i++) ra.push_back(ra[i % k]); }
    }
    size_t n;
    if (lInf) n = ra.size();
    else if (rInf) n = la.size();
    else if (lStar || rStar) n = std::min(la.size(), ra.size());
    else if (strictL && strictR) {
        if (la.size() != ra.size()) throw dwimDie(la.size(), ra.size());
        n = la.size();
    }
    else if (strictL) { if (!lIter && rIter) throw dwimDie(la.size(), ra.size()); n = la.size(); }
    else if (strictR) { if (!rIter && lIter) throw dwimDie(la.size(), ra.size()); n = ra.size(); }
    else n = std::max(la.size(), ra.size());
    // materialize the first n elements of an infinite side (cycle unit / count-up)
    auto fill = [&](const Value& v, ValueList& out) {
        if (v.t == VT::Range) {
            long long lo = v.rFrom() + (v.rExFrom() ? 1 : 0);
            for (size_t i = out.size(); i < n; i++) out.push_back(Value::integer(lo + (long long)i));
        }
        else if (v.arr()) {
            out = *v.arr();
            auto st = std::static_pointer_cast<LazySeqState>(v.ext());
            while (out.size() < n && st->appendNext(out)) {}
        }
    };
    if (lInf) fill(l, la);
    if (rInf) fill(r, ra);
    // element slots line up only when flatten() mirrored the raw array
    bool lSlot = wantSlots && l.t == VT::Array && l.arr() && l.arr()->size() == la.size();
    bool rSlot = wantSlots && r.t == VT::Array && r.arr() && r.arr()->size() == ra.size();
    // The result MIRRORS the left operand's shape, as Rakudo does: an Array in
    // gives an Array out (`@a >>+>> @b` gists `[8 10]`), a List in gives a List
    // (`(6,8) >>+>> @b` gists `(8 10)`). rakupp always answered a List.
    // …but only a side that IS a collection can dictate a shape: with a scalar
    // on the left (`3 <<~>> @a`) the RIGHT operand does, so that is an Array —
    // which is 1000 assertions of S03-metaops/infix.t on its own, and was
    // invisible while is-deeply compared an Array and a List as equal.
    const Value& shaper = lIter ? l : r;
    Value out = Value::array();
    out.isList = !(shaper.t == VT::Array && !shaper.isList);
    for (size_t i = 0; i < n && !la.empty() && !ra.empty(); i++) {
        const Value& x = la[i % la.size()];
        const Value& y = ra[i % ra.size()];
        Value* xs = lSlot ? &(*l.arr())[i % la.size()] : (!lIter ? lroot : nullptr);
        Value* ys = rSlot ? &(*r.arr())[i % ra.size()] : (!rIter ? rroot : nullptr);
        out.arr()->push_back(deepApply(x, y, xs, ys));
    }
    return (!lIter && !rIter && out.arr()->size() == 1) ? (*out.arr())[0] : out;
}

// The CONTAINER an expression denotes, as a proxy — for the two places that bind
// rather than read: `@path.BIND-POS($i, $node)` stores the container, not its
// value, and BinaryHeap's sift-down walks a heap by building exactly such a path
// of aliases. Falls back to the plain value for anything with no container
// behind it, which is what Rakudo's `is raw` binding does too.
Value Interpreter::containerOfExpr(Expr* e) {
    if (!e) return Value::any();
    // an expression that ALREADY holds an alias hands that same alias on, rather
    // than wrapping a second proxy around the slot that holds it
    if (e->kind == NK::NameTerm || e->kind == NK::VarExpr) {
        const std::string& n = e->kind == NK::NameTerm
            ? static_cast<NameTerm*>(e)->name : static_cast<VarExpr*>(e)->name;
        if (tctx_.cur)
            if (Value* p = tctx_.cur->find(n)) {
                if (p->t == VT::Hash && p->hashKind == "Proxy" && p->hash()) return *p;
                if (e->kind == NK::VarExpr) {
                    for (std::shared_ptr<Env> en = tctx_.cur; en; en = en->parent)
                        if (en->local(n)) return makeEnvSlotProxy(en, n);
                }
            }
    }
    if (e->kind == NK::Index) {
        auto* ix = static_cast<Index*>(e);
        if (ix->index && !ix->multiDim) {
            Value* base = nullptr;
            try { base = lvalue(ix->base.get(), /*asInvocant=*/true); } catch (RakuError&) { base = nullptr; }
            if (base && !ix->isHash && base->t == VT::Array && base->arr() && !base->shape()) {
                if (std::shared_ptr<ValueList> arr = base->arrS()) {
                    Value kv = eval(ix->index.get());
                    if (kv.t == VT::Code && kv.code() && kv.code()->isWhateverCode)
                        kv = callCallable(kv, ValueList{Value::integer((long long)arr->size())});
                    long long i = kv.toInt();
                    if (i < 0) negIndexThrow(i);
                    if (i >= 0) {
                        while ((long long)arr->size() <= i) arr->push_back(Value::any());
                        size_t at = (size_t)i;
                        // the slot may itself hold an alias — pass that one along
                        if ((*arr)[at].t == VT::Hash && (*arr)[at].hashKind == "Proxy" && (*arr)[at].hash())
                            return (*arr)[at];
                        return makeArraySlotProxy(arr, at);
                    }
                }
            }
        }
    }
    return eval(e);
}

// The lexical `&infix:<op>` that shadows a built-in of the same spelling, or
// null. Only reached when the filter admits the spelling, so the key is
// built in a reused buffer rather than a fresh string per operator.
// The name half of the lookup, with no operands to judge — what the WhateverCode
// curry needs, because it must resolve the shadow where `* cmp *` was WRITTEN.
Value* Interpreter::lexInfixLookup(const std::string& op) {
    // the lexer spells `×` `÷` `−` `≥` `≤` `≠` as their ASCII twins, so a
    // `sub infix:<×>` shadows what arrives here as `*`
    static const std::map<std::string, std::string> kUniTwin = {
        {"*", "\xC3\x97"}, {"/", "\xC3\xB7"}, {"-", "\xE2\x88\x92"},
        {">=", "\xE2\x89\xA5"}, {"<=", "\xE2\x89\xA4"}, {"!=", "\xE2\x89\xA0"}};
    if (!tctx_.cur) return nullptr;
    if (!lexShadowPossible(op)) {
        auto tw = kUniTwin.find(op);
        if (tw == kUniTwin.end() || !lexShadowPossible(tw->second)) return nullptr;
        Value* f = tctx_.cur->find("&infix:<" + tw->second + ">");
        if (f && f->t == VT::Code && f->code() && !f->code()->name.empty() &&
            (f->code()->isMultiDispatcher || f->code()->isMultiCandidate || f->code()->isProto))
            return nullptr;
        // …but not from inside that operator's own body: `sub infix:<×>($a, $b)
        // { $a * $b }` means the built-in `*` there, not itself again (the lexer
        // spells both `*`, so this is the one place the two can be told apart)
        if (f && f->t == VT::Code && tctx_.curRoutineVal && tctx_.curRoutineVal->t == VT::Code &&
            tctx_.curRoutineVal->code() == f->code())
            return nullptr;
        return f;
    }
    static thread_local std::string key;
    key.assign("&infix:<").append(op).append(">");
    Value* f = tctx_.cur->find(key);
    // Only an anonymous binding is a lexical shadow; a declared `sub infix:<op>`
    // carries its own name and belongs to the multi-dispatch path (see the
    // g_lexShadowMask note in Interpreter.h).
    if (f && f->t == VT::Code && f->code() && !f->code()->name.empty() &&
        (f->code()->isMultiDispatcher || f->code()->isMultiCandidate || f->code()->isProto))
        return nullptr;
    return f;
}

Value* Interpreter::lexShadowedInfix(const std::string& op, const Value& l, const Value& r) {
    if (!tctx_.cur || !g_lexShadowMask.load(std::memory_order_relaxed)) return nullptr;
    // `* cmp *` still CURRIES into a WhateverCode (applyArith builds it) — the
    // shadowing lexical applies when that closure RUNS, and the run comes back
    // through here with real operands. Intercepting the curry itself would call
    // the user's block once, eagerly, with two Whatevers.
    auto wish = [](const Value& v) {
        return v.t == VT::Whatever || (v.t == VT::Code && v.code() && v.code()->isWhateverCode);
    };
    if (wish(l) || wish(r)) return nullptr;
    return lexInfixLookup(op);
}

// `l ~~ r` (or `!~~`) where l is a VALUE already in hand — a `when` topic, a
// bound argument a `where` clause checks, an element a matcher tests, what a
// variable holds — and never the literal `*` term that Whatever-curries. Rakudo
// decides currying syntactically, so such a value is an ordinary object there:
// `Pair.ACCEPTS(*)` is False and `given * { when Pair {…} }` passes the arm
// over. applyArith cannot see the syntax; the one-shot flag tells it. Raised
// only when it would change anything — a Whatever(Code) on the left — and
// dropped again in case applyBinOp answered without reaching applyArith.
// `$topic ~~ $pair` — Pair.ACCEPTS, in its three shapes (sheet HM-20):
//
//   * a PAIR topic matches when the key ACCEPTS the key and the value the value,
//     so `(a => 1) ~~ (a => Int)` holds and `(a => Int) ~~ (a => 1)` does not;
//   * an ASSOCIATIVE topic looks the key up and matches the value against what
//     it finds — `%(a => 1, b => 2) ~~ (a => 1)` is True, the other keys unread;
//   * anything else calls the key AS A METHOD on the topic and compares the
//     Bools of the answer and the value, so `"abc" ~~ (chars => 3)` is True
//     because both are truthy. A key that names no method is an ERROR, not a
//     False — Rakudo says so in a message that names the mistake, and a `.grep`
//     over a misspelt pair used to come back quietly empty.
bool Interpreter::pairAccepts(const Value& topic, const Value& pair) {
    Value want = pair.pairVal() ? *pair.pairVal() : Value::boolean(true);
    Value key = pair.pairKey() ? *pair.pairKey() : Value::str(pair.s);
    if (topic.t == VT::Pair) {
        Value tk = topic.pairKey() ? *topic.pairKey() : Value::str(topic.s);
        Value tv = topic.pairVal() ? *topic.pairVal() : Value::any();
        return matcherAccepts(*this, tk, key) && matcherAccepts(*this, tv, want);
    }
    if (topic.t == VT::Hash && (topic.hashKind.empty() || topic.hashKind == "Map" ||
                                topic.hashKind == "Stash")) {
        Value got = topic.hash() && topic.hash()->count(pair.s) ? topic.hash()->at(pair.s)
                                                               : Value::any();
        return matcherAccepts(*this, got, want);
    }
    // a QuantHash is Associative too: the key's weight (a Set's True) is matched
    if (topic.t == VT::Hash && topic.hash() &&
        (topic.hashKind == "Set" || topic.hashKind == "SetHash" || topic.hashKind == "Bag" ||
         topic.hashKind == "BagHash" || topic.hashKind == "Mix" || topic.hashKind == "MixHash")) {
        const bool setty = topic.hashKind == "Set" || topic.hashKind == "SetHash";
        auto it = topic.hash()->find(hashSubKey(key, &topic));
        Value got = it != topic.hash()->end() ? (setty ? Value::boolean(true) : it->second)
                                               : (setty ? Value::boolean(false) : Value::integer(0));
        if (got.t == VT::Pair && got.pairVal()) got = *got.pairVal();
        return matcherAccepts(*this, got, want);
    }
    try { return methodCall(topic, pair.s, {}).truthy() == want.truthy(); }
    catch (RakuError& e) {
        if (e.payload.t == VT::Type && e.payload.s == "X::Method::NotFound")
            throwTypedV("X::Method::NotFound",
                {{"method", Value::str(pair.s)}, {"typename", Value::str(topic.typeName())}},
                "No such method '" + pair.s + "' for invocant of type '" + topic.typeName() +
                "'. Or did you try to smartmatch against a Pair specifically? If so, "
                "then the key of the Pair should be a valid method name, not '" + pair.s + "'.");
        throw;
    }
}
// Smartmatch on two VALUES — what the native backend emits for `~~`/`!~~` and
// for every `when`, so that a compiled match runs the interpreter's rules.
//
// A CALLABLE matcher is invoked with the topic. That arm lives in evalBinary,
// which emitted code never reaches, so compiled `$x ~~ $matcher` fell through to
// applyArith and compared a Code object with a number: `200 ~~ (* > 100)`
// answered False. Everything else goes to smartmatchValue, the value-level entry
// the junction and `where` paths already use — it knows Regex values, Str/object
// ACCEPTS, Pair patterns, junctions, and that a Whatever which ARRIVED as a
// value is a value rather than a curry.
//
// That last point is what issue #96 was: `when * < 1` evaluates to a
// WhateverCode, and handing one to applyArith curried it a SECOND time. The
// result was a Code object — truthy whatever the topic — so the first `when` arm
// always won, and a compiled `given @a[$i] - @a[$i-1] { when * < 1 … }` printed
// the opposite of what the interpreter printed.
// Callable.ACCEPTS calls a ZERO-arity routine with no arguments: `$x ~~ &always`
// where `sub always { True }` asks only the sub (S03-smartmatch/any-callable.t)
ValueList smartmatchCallArgs(const Value& r, const Value& topic) {
    const Callable* c = r.code();
    if (c && !c->isBlock && !c->isWhateverCode && !c->builtin && c->placeholders.empty() && !c->isMethod) {
        if (!c->hadSig && !c->usesArgs && !c->name.empty()) return {};
        if (c->hadSig && c->params) {
            bool any = false;
            for (auto& p : *c->params) if (!p.named) { any = true; break; }
            if (!any) return {};
        }
    }
    return ValueList{topic};
}

Value Interpreter::smartmatchValue(const std::string& op, const Value& l, const Value& r) {
    if (l.t == VT::Array) l.seqTouch();   // `$s ~~ …` caches a Seq (SeqToken)
    if (!(l.t == VT::Whatever || (l.t == VT::Code && l.code() && l.code()->isWhateverCode)))
        return applyBinOp(op, l, r);
    struct Drop { ~Drop() { Interpreter::valueSmartmatch_ = false; } } drop;
    Interpreter::valueSmartmatch_ = true;
    return applyBinOp(op, l, r);
}
// The value-level smartmatch hooks, out of line on purpose: applyArith's Int/Int
// fast path and the junction collapse above it are hot enough that carrying
// these ~50 lines in the same body cost 3% per eigenstate on the width
// benchmark — the checks never even run there (Int ~~ Int returns earlier),
// so it was pure code layout. Gated by a cheap type test at the call site.
bool valueSmartmatchHook(const std::string& op, const Value& l, const Value& r, Value& out) {
    // `$x ~~ $obj` where $obj's class defines ACCEPTS — the general hook every
    // matcher type in Raku is built on. It lives HERE, at the bottom that every
    // smartmatch path converges on, and not in evalBinary or applyBinOp: the
    // junction eigenstate loops and the `where`-constraint sites hold VALUES and
    // reach applyArith directly, so a hook any higher was invisible to them.
    // `5 ~~ ($matcher | $other)` and `sub f($x where $matcher)` both answered
    // False without ever calling ACCEPTS, while the same match written out
    // (`5 ~~ $matcher`) and `.grep($matcher)` called it — the hook was in
    // evalBinary, which only the written-out form goes through.
    // The call site places this AFTER applyArith's junction arms, because a
    // junction TOPIC threads first: `any(1, 2) ~~ $matcher` asks the matcher
    // about each eigenstate rather than handing it the junction whole. Only a
    // DEFINED instance takes this road; `$x ~~ SomeClass` stays a type check.
    // Each arm re-tests what the gate already checked: cheap here (this is only
    // reached when the gate passed) and it keeps the helper correct on its own.
    if (r.t == VT::Object && r.obj() && r.obj()->cls && g_cbInterp &&
        (op == "~~" || op == "!~~")) {
        bool hasAccepts = false;
        for (ClassInfo* ci = r.obj()->cls.get(); ci && !hasAccepts; ci = ci->parent.get())
            if (ci->methods.count("ACCEPTS")) hasAccepts = true;
        if (hasAccepts) {
            ValueList one{l};
            // A user's ACCEPTS is usually a MULTI constrained to the types it
            // knows how to match. When the topic is none of them, Rakudo falls
            // back to Mu.ACCEPTS — the ordinary type/identity check — rather
            // than failing to dispatch. Tinky's State accepts only a State, and
            // `$object ~~ $state` (an Object against it) has to answer False,
            // not "No matching multi candidate for method ACCEPTS".
            try {
                Value m = g_cbInterp->methodCall(r, "ACCEPTS", one);
                if (op == "~~" && m.t == VT::Match) { out = m; return true; }
                bool ok = g_cbInterp->boolify(m);
                out = Value::boolean(op == "~~" ? ok : !ok); return true;
            }
            catch (RakuError& e) {
                if (e.message.find("No matching multi candidate for method ACCEPTS") == std::string::npos &&
                    e.message.rfind("Cannot resolve caller ACCEPTS", 0) != 0)
                    throw;
                // fall through to the generic smartmatch below
            }
        }
    }
    // `\(…) ~~ :(…)` — a Signature matches by BINDING the capture to it, which
    // is Signature.ACCEPTS's job. Here for the same reason as the arm above: a
    // signature reached as a VALUE (a junction eigenstate, a `where :(…)`) never
    // passes through evalBinary, and answered False for every capture.
    if (r.t == VT::Hash && r.hashKind == "Signature" && g_cbInterp &&
        (op == "~~" || op == "!~~")) {
        ValueList one{l};
        bool ok = g_cbInterp->boolify(g_cbInterp->methodCall(r, "ACCEPTS", one));
        out = Value::boolean(op == "~~" ? ok : !ok); return true;
    }
    // A NUMERIC matcher smartmatches with `==` — that is Rakudo's
    // `Numeric.ACCEPTS(Any:D \a) { self == a }` — and `==` NUMIFIES the topic, so
    // an object with its own (or a delegated) `method Numeric` matches by the
    // number it answers. Lumberjack tests a log message against a level exactly
    // this way: `$message ~~ $level`, where Message.Numeric hands back the
    // message's own level. Here rather than in evalBinary for the same reason as
    // the two arms above — `$obj ~~ (42 | 99)` and `sub f($x where 42)` were both
    // False because they reach applyArith without passing through evalBinary.
    if (l.t == VT::Object && l.obj() && l.obj()->cls && g_cbInterp &&
        (r.t == VT::Int || r.t == VT::Num || r.t == VT::Rat || r.t == VT::Complex) &&
        (op == "~~" || op == "!~~")) {
        Value nb = bridgeReal(*g_cbInterp, l);
        if (!(nb.t == VT::Object)) {
            bool ok = g_cbInterp->boolify(applyArith("==", nb, r));
            out = Value::boolean(op == "~~" ? ok : !ok); return true;
        }
    }
    return false;
}
// Real-role bridge: a user object that defines .Bridge (or .Numeric) numifies
// through it, so numeric operators work on `class F does Real` instances.
Value bridgeReal(Interpreter& I, const Value& v) {
    if (v.t == VT::Object && v.obj() && v.obj()->cls) {
        if (Value* br = v.obj()->cls->findMethod("Bridge"))  { ValueList none; return I.invokeMethod(*br, v, none); }
        if (Value* nu = v.obj()->cls->findMethod("Numeric")) { ValueList none; return I.invokeMethod(*nu, v, none); }
        // …or a DELEGATED one. `has FatRat $.fatrat handles <Numeric …>` gives
        // the object the method without putting it in the class's table, so the
        // lookups above missed it and every numeric comparison against such an
        // object fell back to comparing addresses — FatRatStr is written exactly
        // that way and `$x.FatRatStr == $x` was False for every value.
        for (ClassInfo* c = v.obj()->cls.get(); c; c = c->parent.get())
            for (auto& at : c->attrs)
                for (auto& h : at.handles)
                    if (h == "Bridge" || h == "Numeric" || h == "*") {
                        ValueList none;
                        return I.methodCall(v, h == "Bridge" ? "Bridge" : "Numeric", none);
                    }
    }
    return v;
}

// The operators that do NOT evaluate both sides: which one is skipped depends on
// the other's value, so they take AST nodes rather than values. `R&&` and friends
// reuse this with the operands swapped, which is the whole point — `$n++ R&& 0`
// must leave $n alone.
Value Interpreter::shortCircuitOp(const std::string& op, Expr* lhs, Expr* rhs) {
    if (op == "&&" || op == "and") { Value l = eval(lhs); return boolify(l) ? eval(rhs) : l; }
    if (op == "||" || op == "or")  { Value l = eval(lhs); return boolify(l) ? l : eval(rhs); }
    if (op == "//") { Value l = eval(lhs); return topicDefined(l) ? l : eval(rhs); }
    if (op == "^^" || op == "xor") { // the two-operand form; the CHAIN is walked in evalBinary
        Value a = eval(lhs), c = eval(rhs);
        bool ta = boolify(a), tc = boolify(c);
        if (ta && tc) return Value::nil();
        return ta ? a : c;           // …and with neither true, the LAST operand
    }
    Value l = eval(lhs);
    bool def = topicDefined(l);
    bool run = op == "andthen" ? def : !def; // orelse/notandthen fire on undefined
    if (!run) { // skip the RHS: orelse yields the LHS, andthen/notandthen yield Empty
        if (op == "andthen" || op == "notandthen") { Value e = Value::array(); e.isList = true; e.s = "Slip"; return e; }
        return l;
    }
    auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
    scope->define("$_", l);
    auto saved = tctx_.cur; tctx_.cur = scope;
    Value r; try { r = eval(rhs); } catch (...) { tctx_.cur = saved; throw; }
    tctx_.cur = saved;
    // `$x notandthen .=new` — the topic IS the left side's container, so a
    // `.=` on it writes the variable at the head of the chain
    if (rhs && rhs->kind == NK::MethodCall && static_cast<MethodCall*>(rhs)->mutate) {
        auto* mc = static_cast<MethodCall*>(rhs);
        if (mc->inv && mc->inv->kind == NK::VarExpr && static_cast<VarExpr*>(mc->inv.get())->name == "$_") {
            Expr* tgt = lhs;
            while (tgt && tgt->kind == NK::Binary &&
                   (static_cast<Binary*>(tgt)->op == "andthen" || static_cast<Binary*>(tgt)->op == "orelse" ||
                    static_cast<Binary*>(tgt)->op == "notandthen"))
                tgt = static_cast<Binary*>(tgt)->lhs.get();
            if (tgt && tgt->kind == NK::VarExpr && !static_cast<VarExpr*>(tgt)->declare)
                if (Value* nv = scope->local("$_"))
                    if (Value* lv = lvalue(tgt)) { *lv = *nv; r = *nv; }
        }
    }
    // `$x orelse .=new andthen .=new: 43` — `andthen` binds tighter, so the
    // `.=` steps sit in a chain of their own on the right; the topic they
    // leave behind is still the head variable's
    else if (rhs && rhs->kind == NK::Binary) {
        auto isChainOp = [](const std::string& o) { return o == "andthen" || o == "orelse" || o == "notandthen"; };
        std::function<bool(Expr*)> hasTopicMutate = [&](Expr* e) -> bool {
            if (!e) return false;
            if (e->kind == NK::MethodCall) {
                auto* mc = static_cast<MethodCall*>(e);
                return mc->mutate && mc->inv && mc->inv->kind == NK::VarExpr &&
                       static_cast<VarExpr*>(mc->inv.get())->name == "$_";
            }
            if (e->kind == NK::Binary && isChainOp(static_cast<Binary*>(e)->op))
                return hasTopicMutate(static_cast<Binary*>(e)->lhs.get()) ||
                       hasTopicMutate(static_cast<Binary*>(e)->rhs.get());
            return false;
        };
        if (isChainOp(static_cast<Binary*>(rhs)->op) && hasTopicMutate(rhs)) {
            Expr* tgt = lhs;
            while (tgt && tgt->kind == NK::Binary && isChainOp(static_cast<Binary*>(tgt)->op))
                tgt = static_cast<Binary*>(tgt)->lhs.get();
            if (tgt && tgt->kind == NK::VarExpr && !static_cast<VarExpr*>(tgt)->declare) {
                const bool skipped = r.t == VT::Array && r.s == "Slip" && r.arr() && r.arr()->empty();
                Value nv = r;
                if (skipped) { if (Value* sv = scope->local("$_")) nv = *sv; }
                if (Value* lv = lvalue(tgt)) *lv = nv;
            }
        }
    }
    // A BLOCK on the right is CALLED with the left value: `'x' andthen -> $a {…}`
    // passes 'x', and `Failure.new(…) orelse -> $f {…}` passes the Failure. Only
    // a block written there — a Code that merely came out of an expression is
    // the answer, not something to invoke.
    if (r.t == VT::Code && r.code() && rhs->kind == NK::BlockExpr) {
        Value arg = l;
        // The left side of an `orelse` may be a FAILURE — that is the usual
        // reason the right side runs — and asking about it is what handles it.
        // Passed on unhandled, it detonated inside the block instead.
        if (arg.t == VT::Hash && arg.hashKind == "Failure" && arg.hash())
            (*arg.hash())["handled"] = Value::boolean(true);
        return callCallable(r, ValueList{arg});
    }
    return r;
}

Value Interpreter::xxRepeat(Expr* item, Expr* count) {
// list repetition THUNKS its left side: `EXPR xx N` re-evaluates EXPR once
    // per copy (so `rand xx 3` / `(…roll…) xx $N` yield independent results).
    Value rv = eval(count);
    // Only `*` and `+Inf` name an endless repetition. `NaN` and `-Inf` name no
    // count at all, so they are a refusal — the same X::Numeric::CannotConvert
    // `x` already gives. Treating every infinity as endless made `'a' xx -Inf`
    // build a list nothing could stop reading (S03-operators/repeat.t hung in
    // throws-like's diagnostic, which prints the value it was handed).
    rtXxCountCheck(rv);
    if (rv.t == VT::Whatever || (rv.t == VT::Num && std::isinf(rv.n))) {
        // `EXPR xx *` — an endlessly repeating lazy list. The left side is a
        // THUNK on this path too, not one value repeated: `[] xx *` owes each
        // element a FRESH array. Repeating one unit made every key of
        // `my %rows = @!column-name Z=> [] xx *` share a single array, so
        // DBDish's allrows(:hash-of-array) pushed every column into all of them.
        Value a = Value::seq();   // `EXPR xx *` is a Seq too (sheet LA-32)
        auto st = std::make_shared<LazySeqState>(); st->infinite = true;
        st->hasCount = true; st->countVal = Value::number(INFINITY); // `.count-only` is Inf
        Expr* le = item;
        auto env = tctx_.cur;   // the thunk keeps the scope it was written in
        st->appendNext = [this, le, env](ValueList& cache) -> bool {
            auto saved = tctx_.cur;
            tctx_.cur = env;
            size_t before = cache.size();
            try { rtXxAppend(cache, eval(le)); } // a Slip replicates its ELEMENTS
            catch (...) { tctx_.cur = saved; throw; }
            tctx_.cur = saved;
            // An EMPTY slip contributes nothing, so a generator that only ever
            // splices would never advance and a demand for five elements would
            // spin for ever. Rakudo's iterator yields one value per repetition
            // and an empty one yields Nil: `(|() xx *)[^5]` is five Nils.
            if (cache.size() == before) cache.push_back(Value::nil());
            return true;
        };
        {   size_t before = a.arr()->size();
            rtXxAppend(*a.arr(), eval(item));
            if (a.arr()->size() == before) a.arr()->push_back(Value::nil());
        }
        a.extM() = st;
        return a;
    }
    long long n = strictInt(rv); // a non-numeric count is X::Str::Numeric, not 0
    // A count past the materialisation cap — `42 xx 9999999999`, `42 xx 2**62`,
    // `42 xx 2**99999` — is generated ON DEMAND with its length recorded, the
    // way Rakudo's iterator does. Building it took as long as the count said
    // and S03-operators/repeat.t's `sink cheaply` subtest never returned.
    if (rv.big() || n > kXxEagerMax) {
        Value a = Value::seq();
        auto st = std::make_shared<LazySeqState>();
        st->infinite = true;        // never materialise the whole of it
        st->hasCount = true; st->countVal = rv.big() ? rv : Value::integer(n);
        Expr* le = item;
        auto env = tctx_.cur;
        auto left = std::make_shared<long long>(rv.big() ? -1 : n); // -1: effectively unbounded
        st->appendNext = [this, le, env, left](ValueList& cache) -> bool {
            if (*left == 0) return false;
            if (*left > 0) --*left;
            auto saved = tctx_.cur;
            tctx_.cur = env;
            size_t before = cache.size();
            try { rtXxAppend(cache, eval(le)); }
            catch (...) { tctx_.cur = saved; throw; }
            tctx_.cur = saved;
            if (cache.size() == before) cache.push_back(Value::nil());
            return true;
        };
        if (n != 0) { --*left; rtXxAppend(*a.arr(), eval(item));
                      if (a.arr()->empty()) a.arr()->push_back(Value::nil()); }
        a.extM() = st;
        return a;
    }
    Value a = Value::array(); a.isList = true; a.s = "Seq"; // `EXPR xx N` is a Seq (Rakudo)
    for (long long k = 0; k < n; k++) rtXxAppend(*a.arr(), eval(item));
    return a;
}

} // namespace rakupp
