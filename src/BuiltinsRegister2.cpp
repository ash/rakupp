// BuiltinsRegister2.cpp — registerBuiltins, pieces three and four: string and list subs, radix, coercions
//
// One of the parts BuiltinsParts.h lists; what they share is declared there.
#include "BuiltinsParts.h"

namespace rakupp {

// registerBuiltins, continued. Split for compile time: each piece ends by
// calling the next, so the registrations run in the original order (a later
// one of the same name still replaces an earlier one).
void Interpreter::registerBuiltinsPart3() {
    auto& B = builtins_;
    // the sub forms delegate to the methods so they share the char-based
    // positioning, junction autothreading, and negative/out-of-range validation
    B["index"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::nil();
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "index", rest); };
    B["rindex"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::nil();
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "rindex", rest); };
    // …and `indices` has the same sub form (String::Utils' stem splits a
    // basename on every dot with it)
    B["indices"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::nil();
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "indices", rest); };
    // `min`/`max` as SUBS delegate to the method, so `:by`/`:k`/`:v`/`:kv`/`:p`
    // behave identically. A lone Positional/Associative argument IS the list;
    // several arguments are the list themselves — and they are NOT flattened, so
    // `min((1,2),(3,4))` compares the two sublists.
    for (const char* mm : {"min", "max"}) {
        std::string mname = mm;
        B[mname] = [mname](Interpreter& I, ValueList& a) -> Value {
            ValueList pos, named;
            for (auto& v : a) { if (v.t == VT::Pair && v.namedArg) named.push_back(v); else pos.push_back(v); }
            // COMPARING candidates uses them, so a Failure among two or more
            // detonates: `min +'a', +'a'` throws the X::Str::Numeric the coercion
            // produced. A single candidate is never compared with anything, so it
            // comes back as the Failure it is (`min +'a'` merely fails).
            if (pos.size() >= 2)
                for (auto& v : pos)
                    if (v.t == VT::Hash && v.hashKind == "Failure") I.methodCall(v, "throw", {});
            if (pos.size() == 1 && pos[0].t == VT::Hash && pos[0].hashKind == "Failure") return pos[0];
            Value list;
            if (pos.size() == 1 && (pos[0].t == VT::Array || pos[0].t == VT::Hash || pos[0].t == VT::Range))
                list = pos[0];
            else { list = Value::array(pos); list.isList = true; }
            return I.methodCall(list, mname, named);
        };
    }
    // `minmax` as a SUB delegates to the method, so `:by(&code)` and a leading
    // &mapper mean there what they mean here
    B["minmax"] = [](Interpreter& I, ValueList& a) -> Value {
        ValueList pos, named;
        for (auto& v : a) { if (v.t == VT::Pair && v.namedArg) named.push_back(v); else pos.push_back(v); }
        Value mapper;
        if (!pos.empty() && pos[0].t == VT::Code) { mapper = pos[0]; pos.erase(pos.begin()); }
        Value list;
        if (pos.size() == 1 && (pos[0].t == VT::Array || pos[0].t == VT::Range)) list = pos[0];
        else { list = Value::array(flattenArgs(pos)); list.isList = true; }
        ValueList ma;
        if (mapper.t == VT::Code) ma.push_back(mapper);
        for (auto& nv : named) ma.push_back(nv);
        return I.methodCall(list, "minmax", ma);
    };
    // What `chdir`/`indir` check before they move $*CWD: the path exists and,
    // by default (`:d`), is a directory; `:r`/`:w`/`:x` add permission tests.
    // Empty when it passes, else the os-error X::IO::Chdir carries.
    static auto chdirRefusal = [](const std::string& to, const ValueList& a) -> std::string {
        bool d = true, r = false, w = false, x = false;
        for (auto& v : a)
            if (v.t == VT::Pair && v.namedArg) {
                bool on = !v.pairVal() || v.pairVal()->truthy();
                if (v.s == "d") d = on; else if (v.s == "r") r = on;
                else if (v.s == "w") w = on; else if (v.s == "x") x = on;
            }
        if (!d && !r && !w && !x) return "";   // `:!d` and nothing else: no checks at all (Rakudo)
        struct stat cst{};
        if (::stat(to.c_str(), &cst) != 0) return "does not exist";
        if (d && !S_ISDIR(cst.st_mode)) return "is not a directory";
        if (r && ::access(to.c_str(), R_OK) != 0) return "did not pass :r test";
        if (w && ::access(to.c_str(), W_OK) != 0) return "did not pass :w test";
        if (x && ::access(to.c_str(), X_OK) != 0) return "did not pass :x test";
        return "";
    };
    B["chdir"] = [](Interpreter& I, ValueList& a) -> Value {
        // `chdir()` matches no candidate of the multi — which is the error the
        // caller sees, and the type a `CATCH` for a bad call is written against
        if (a.empty()) throw RakuError{Value::typeObj("X::Multi::NoMatch"),
            "Cannot resolve caller chdir(); none of these signatures matches"};
        if (a[0].toStr().find('\0') != std::string::npos)
            throw RakuError{Value::typeObj("X::IO::Null"),
                "Cannot use null character (U+0000) as part of the path"};
        std::string old = I.cwdName(); // the base BEFORE the switch
        std::string to = a[0].toStr();
        // a relative IO::Path argument is relative to ITS OWN captured :CWD
        if (a[0].hashKind == "IO" && !a[0].ofType().empty() && !to.empty() && to[0] != '/')
            to = logicalJoin(a[0].ofType(), to);
        // (the checks are Rakudo's; the process follows along when it can, so
        // relative file operations keep resolving — `chdir $file, :!d` moves
        // only $*CWD, as Rakudo's always does)
        std::string abs = to.empty() || to[0] == '/' ? to : logicalJoin(old, to);
        if (std::string why = chdirRefusal(abs, a); !why.empty())
            return I.ioFailure("X::IO::Chdir",
                               {{"path", Value::str(a[0].toStr())}, {"os-error", Value::str(why)}},
                               "Failed to change the working directory to '" + a[0].toStr() + "': " + why);
        (void)::chdir(abs.c_str());
        I.logicalCwd_ = logicalJoin(old, to);
        // Rakudo's answer is the new cwd as an absolute IO::Path, based where you were
        Value p = Value::str(I.logicalCwd_); p.hashKind = "IO"; p.ofTypeM() = old;
        // …and it IS `$*CWD = …`: a `$*CWD` the caller can see (`temp $*CWD`,
        // `my $*CWD`) now holds it
        if (Value* slot = I.findDynamicLenient("$*CWD")) *slot = p;
        return p;
    };
    // indir($path, &code) — run the block with the process directory changed,
    // then put it back however the block exits
    // uniparse("LATIN SMALL LETTER A") — the inverse of .uniname. A comma-
    // separated list of names yields one character each.
    // `comb($matcher, $input [, $limit])` — the sub form takes the matcher FIRST
    B["comb"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) { Value o = Value::array(); o.isList = true; o.s = "Seq"; return o; }
        if (a.size() == 1) return I.methodCall(a[0], "comb", ValueList{});
        ValueList rest{a[0]};
        for (size_t i = 2; i < a.size(); i++) rest.push_back(a[i]);
        return I.methodCall(a[1], "comb", rest);
    };
    // Pod::To::Text's `pod2text` — a core module in Rakudo, native here.
    B["pod2text"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        return Value::str(pod2text(a[0]));
    };
    B["uniparse"] = [](Interpreter& I, ValueList& a) -> Value {
        // an unknown name is a Failure (`fails-like … X::Str::InvalidCharName`),
        // not an immediate throw
        try { return I.callBuiltin("uniparse:impl", a); }
        catch (RakuError& e) {
            if (e.payload.t == VT::Type && e.payload.s == "X::Str::InvalidCharName")
                return I.ioFailure("X::Str::InvalidCharName", {}, e.message);
            throw;
        }
    };
    B["parse-names"] = B["uniparse"];
    B["uniparse:impl"] = [](Interpreter&, ValueList& a) -> Value {
        std::string out;
        for (auto& v : a) {
            std::string spec = v.toStr(), cur;
            auto emit = [&](std::string nm) {
                // trim
                size_t b = nm.find_first_not_of(" \t"), e = nm.find_last_not_of(" \t");
                if (b == std::string::npos) return;
                nm = nm.substr(b, e - b + 1);
                int32_t cp = uniCharByName(nm);
                if (cp < 0) {
                    // …or a NAMED SEQUENCE, several codepoints under one name
                    std::string seq = uniSeqByName(nm);
                    if (seq.empty())
                        throw RakuError{Value::typeObj("X::Str::InvalidCharName"),
                                        "Unrecognized character name [" + nm + "]"};
                    out += seq;
                    return;
                }
                out += cpToU8((uint32_t)cp);
            };
            for (char c : spec) { if (c == ',') { emit(cur); cur.clear(); } else cur += c; }
            emit(cur);
        }
        return Value::str(out);
    };
    B["indir"] = [](Interpreter& I, ValueList& a) -> Value {
        // indir($path, :d, :r, :w, :x, &code) — the block is the positional
        // after the path, wherever the adverbs sit
        // (`indir :!d, $path, {…}` — an adverb may come first, too)
        ValueList pos;
        for (auto& v : a) if (!(v.t == VT::Pair && v.namedArg)) pos.push_back(v);
        Value code;
        for (size_t k = 1; k < pos.size(); k++)
            if (pos[k].t == VT::Code) { code = pos[k]; break; }
        if (pos.empty() || code.t != VT::Code) return Value::any();
        std::string to = pos[0].toStr();
        // a relative IO::Path argument is relative to ITS OWN captured :CWD
        if (pos[0].hashKind == "IO" && !pos[0].ofType().empty() && !to.empty() && to[0] != '/')
            to = logicalJoin(pos[0].ofType(), to);
        // The process has ONE working directory, and a `start indir …` runs on
        // a worker while others do the same: there the block gets its `$*CWD`
        // and nothing else, which is all Rakudo's indir does anywhere.
        const bool worker = std::this_thread::get_id() != I.mainThreadId();
        char buf[4096];
        std::string from = getcwd(buf, sizeof buf) ? buf : ".";
        std::string base = I.cwdName(), oldLogical = I.logicalCwd_;
        std::string abs = to.empty() || to[0] == '/' ? to : logicalJoin(base, to);
        // a Failure, not a throw: `indir($maybe, {…}) // handle-it` is how a
        // caller copes with a directory that is not there, and the os-error
        // is what tells the refusals apart
        if (std::string why = chdirRefusal(abs, a); !why.empty())
            return I.ioFailure("X::IO::Chdir",
                               {{"path", Value::str(to)}, {"os-error", Value::str(why)}},
                               "Failed to change the working directory to '" + to + "': " + why);
        const std::string logical = logicalJoin(base, to);  // $*CWD keeps the caller's spelling
        if (!worker) {
            (void)::chdir(abs.c_str());   // the process follows when it can (see chdir)
            I.logicalCwd_ = logical;
        }
        // …and the block runs under its OWN `$*CWD` — Rakudo's indir is
        // `my $*CWD = $path; code()` — so a caller's `temp $*CWD` neither
        // shadows it nor gets overwritten by the block
        // (A `$*CWD` the caller already has is set for the duration and put
        // back after, as `temp` would — the block's own lookups can reach that
        // one lexically; with none, a fresh frame carries it.)
        Value cwdv = Value::str(logical); cwdv.hashKind = "IO"; cwdv.ofTypeM() = base;
        if (worker && code.code()) {
            // a scope of the block's own around its closure holds the new
            // `$*CWD`: the block reads and assigns THAT, and a caller's
            // `$*CWD`, which other threads are reading, is never touched
            auto own = std::make_shared<Env>();
            own->parent = code.code()->closure;
            own->define("$*CWD", cwdv);
            auto cc = makePayload<Callable>(*code.code());
            cc->closure = own;
            Value blk; blk.t = VT::Code; blk.setCode(cc);
            ValueList none;
            return I.callCallable(blk, none);
        }
        auto denv = std::make_shared<Env>();
        denv->parent = Interpreter::tctx_.cur;
        Value* slot = I.findDynamicLenient("$*CWD");
        Value slotWas = slot ? *slot : Value();
        if (slot) *slot = cwdv; else denv->define("$*CWD", cwdv);
        auto savedCur = Interpreter::tctx_.cur;
        Interpreter::tctx_.cur = denv;
        auto restore = [&] {
            Interpreter::tctx_.cur = savedCur;
            if (slot) *slot = slotWas;
            if (!worker) { ::chdir(from.c_str()); I.logicalCwd_ = oldLogical; }
        };
        Value r;
        try { ValueList none; r = I.callCallable(code, none); }
        catch (...) { restore(); throw; }   // restore on ANY exit
        restore();
        return r;
    };
    // (loop-control escaping a dies-ok/lives-ok block is a death — see those below)
    B["cross"] = [](Interpreter& I, ValueList& a) -> Value {
        Value withF;
        std::vector<ValueList> rows;
        // `+lists`: a SINGLE non-itemized list argument IS the list of lists —
        // `cross(%h<>:v.map: *.flat)` crosses what the Seq holds
        ValueList pos;
        for (auto& v : a) if (!(v.t == VT::Pair && v.namedArg)) pos.push_back(v);
        if (pos.size() == 1 && pos[0].t == VT::Array && pos[0].arr() && !pos[0].itemized) {
            ValueList rest;
            for (auto& v : a) if (v.t == VT::Pair && v.namedArg) rest.push_back(v);
            ValueList spread = *pos[0].arr();
            for (auto& e : spread) rest.push_back(e);
            a = rest;
        }
        for (auto& v : a) {
            if (v.t == VT::Pair && v.s == "with" && v.pairVal()) { withF = *v.pairVal(); continue; }
            if (v.t == VT::Array && v.arr()) rows.push_back(*v.arr());
            else if (v.t == VT::Range) rows.push_back(v.flatten());
            else rows.push_back(ValueList{v});
        }
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        bool any = !rows.empty();
        for (auto& r : rows) if (r.empty()) any = false;
        if (any) {
            std::vector<size_t> idx(rows.size(), 0);
            for (;;) {
                if (withF.t == VT::Code) {
                    ValueList tup;
                    for (size_t k = 0; k < rows.size(); k++) tup.push_back(rows[k][idx[k]]);
                    Value acc = tup[0];
                    for (size_t k = 1; k < tup.size(); k++) acc = I.callCallable(withF, ValueList{acc, tup[k]});
                    out.arr()->push_back(acc);
                } else {
                    Value t = Value::array(); t.isList = true;
                    for (size_t k = 0; k < rows.size(); k++) t.arr()->push_back(rows[k][idx[k]]);
                    out.arr()->push_back(t);
                }
                size_t k = rows.size();
                while (k > 0 && ++idx[k - 1] == rows[k - 1].size()) idx[--k] = 0;
                if (k == 0) break;
            }
        }
        return out;
    };
    B["leave"] = [](Interpreter&, ValueList& a) -> Value {
        LeaveEx ex; if (!a.empty()) { ex.v = a[0]; ex.hasVal = true; }
        throw ex;
    };
    B["times"] = [](Interpreter&, ValueList&) -> Value {
#if defined(_WIN32)
        // clock() approximates CPU time on Windows; child times unavailable
        double u = (double)std::clock() / CLOCKS_PER_SEC;
        Value out = Value::array({Value::number(u), Value::number(0.0), Value::number(0.0), Value::number(0.0)});
#else
        struct rusage ru, rc;
        getrusage(RUSAGE_SELF, &ru); getrusage(RUSAGE_CHILDREN, &rc);
        auto sec = [](const timeval& tv) { return Value::number(tv.tv_sec + tv.tv_usec / 1e6); };
        Value out = Value::array({sec(ru.ru_utime), sec(ru.ru_stime), sec(rc.ru_utime), sec(rc.ru_stime)});
#endif
        out.isList = true; return out;
    };
    B["elems"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() > 1) throw RakuError{Value::typeObj("X::TypeCheck::Argument"),
            "Calling elems() with more than one positional argument will never work"};
        if (a.empty()) return Value::integer(0);
        // count without materializing (toList deep-copies every element)
        const Value& v = a[0];
        if (v.t == VT::Array && v.arr() && !v.ext()) return Value::integer((long long)v.arr()->size());
        if (v.t == VT::Hash && v.hash() && v.hashKind.empty()) return Value::integer((long long)v.hash()->size());
        return Value::integer((long long)toList(v).size());
    };
    B["defined"] = [](Interpreter& I, ValueList& a) -> Value {
        // a Junction answers through its own `.defined`, which autothreads
        // and collapses (`defined(any(Str, Int))` is False)
        if (!a.empty() && a[0].t == VT::Array && !a[0].enumName.empty() &&
            (a[0].enumName == "any" || a[0].enumName == "all" || a[0].enumName == "one" ||
             a[0].enumName == "none"))
            return I.methodCall(a[0], "defined", ValueList{});
        return Value::boolean(!a.empty() && defined(a[0]));
    };
    // Prefix forms of the metamethods: WHAT($x) === $x.WHAT, etc.
    for (const char* mm : {"WHAT", "WHO", "HOW", "VAR", "WHICH", "WHY"})
        B[mm] = [mm](Interpreter& I, ValueList& a) -> Value { ValueList none; return I.methodCall(a.empty() ? Value::any() : a[0], mm, none); };
    B["chars"] = [](Interpreter& I, ValueList& a) -> Value { return a.empty() ? Value::integer(0) : rtBChars(I, a[0]); };
    auto cpOfArg = [](const Value& v, bool& ok) -> uint32_t {
        ok = true;
        if (v.t == VT::Int || v.t == VT::Bool) return (uint32_t)v.toInt();
        auto cps = utf8cp(v.toStr()); if (cps.empty()) { ok = false; return 0; } return cps[0];
    };
    B["expmod"] = [](Interpreter& I, ValueList& a) -> Value { // expmod($b, $e, $m)
        if (a.size() < 3) return Value::integer(0);
        ValueList rest{a[1], a[2]};
        return I.methodCall(a[0], "expmod", rest);
    };
    // (unival/univals sub forms are the method-delegating loop below)
    B["uninames"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.empty() ? Value::str("") : a[0];
        ValueList none; return I.methodCall(v, "uninames", none);
    };
    B["uniname"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty() || a[0].t == VT::Type)
            throw RakuError{Value::typeObj("X::Multi::NoMatch"), "Cannot call uniname with a type object"};
        ValueList none; return I.methodCall(a[0], "uniname", none);
    };
    B["uniprop"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        Value inv = a[0]; ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(inv, "uniprop", rest); // full property dispatch
    };
    for (const char* un : {"uniprops", "unival", "univals"}) {
        std::string mn = un;
        B[mn] = [mn](Interpreter& I, ValueList& a) -> Value {
            if (a.empty()) return Value::str("");
            Value inv = a[0]; ValueList rest(a.begin() + 1, a.end());
            return I.methodCall(inv, mn, rest);
        };
    }
    // (the Str/Int method form delegates here through the sub-as-method fallback)
    // unimatch($char, $propval [, $propname]) — property match; a bare value
    // tests the general category (major class prefix allowed: L matches Lu)
    B["unimatch"] = [cpOfArg](Interpreter&, ValueList& a) -> Value {
        if (a.size() < 2) return Value::boolean(false);
        if (a[0].t == VT::Type)
            throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                            "Cannot call unimatch with a type object"};
        bool ok; uint32_t cp = cpOfArg(a[0], ok); if (!ok) return Value::nil(); // "" → Nil
        std::string want = a[1].toStr();
        auto loose = [](const std::string& s) {
            std::string o;
            for (char ch : s) if (ascii::isalnum((unsigned char)ch)) o += (char)ascii::tolower((unsigned char)ch);
            return o;
        };
        if (a.size() > 2) { // explicit property: unimatch($c, 'Hebrew', 'Block') etc.
            std::string prop = a[2].toStr();
            std::string got = (prop == "Script" || prop == "sc") ? uniScript(cp)
                            : (prop == "Block" || prop == "blk") ? uniBlockOf(cp)
                                                                 : uniGeneralCategory(cp);
            if (got == want) return Value::boolean(true);
            if (prop == "Block" || prop == "blk") return Value::boolean(loose(got) == loose(want));
            if (want.size() == 1 && !got.empty() && got[0] == want[0]) return Value::boolean(true);
            return Value::boolean(false);
        }
        // 2-arg: the name may be a general category, script, binary property, or block
        std::string got = uniGeneralCategory(cp);
        if (got == want) return Value::boolean(true);
        // major-class prefix: unimatch("A", "L") is true for Lu
        if (want.size() == 1 && !got.empty() && got[0] == want[0]) return Value::boolean(true);
        if ((want == "L&" || want == "LC") && got.size() == 2 && got[0] == 'L' &&
            (got[1] == 'u' || got[1] == 'l' || got[1] == 't')) return Value::boolean(true);
        if (uniMatchesProp(cp, want)) return Value::boolean(true); // script / binary / <:Prop> forms
        // a block name without the In prefix, loosely normalized
        std::string qn = loose(want);
        return Value::boolean(!qn.empty() && qn == loose(uniBlockOf(cp)));
    };
    B["uc"] = [](Interpreter& I, ValueList& a) -> Value { return a.empty() ? Value::str("") : rtBUc(I, a[0]); };
    B["lc"] = [](Interpreter& I, ValueList& a) -> Value { return a.empty() ? Value::str("") : rtBLc(I, a[0]); };
    B["tc"] = [](Interpreter&, ValueList& a) -> Value { return Value::str(a.empty() ? "" : mapCase(a[0].toStr(), 0, 1)); };
    // `so *` / `not *` curry like operators do (Rakudo: (so *).^name is WhateverCode)
    // (`userOp`: the program's own `prefix:<so>`/`prefix:<not>` candidates, as
    // seen where the `not *…` was WRITTEN — the curried closure runs somewhere
    // else, and Red's `grep(not *.name in <b c>)` must build a NotIn AST there)
    auto boolCurry = [](bool negate, const Value& w, Value userOp = Value::any()) -> Value {
        Value code; code.t = VT::Code; code.setCode(makePayload<Callable>());
        code.code()->isWhateverCode = true;
        code.code()->whateverArity = (w.t == VT::Code && w.code() && w.code()->whateverArity > 0) ? w.code()->whateverArity : 1;
        Value inner = w;
        code.code()->builtin = [negate, inner, userOp](Interpreter& I, ValueList& xs) -> Value {
            Value v = inner.t == VT::Whatever ? (xs.empty() ? Value::any() : xs[0])
                                              : I.callCallable(inner, xs);
            if (userOp.t == VT::Code && v.t == VT::Object && v.obj() && v.obj()->cls) {
                try { return I.callCallable(userOp, ValueList{v}); }
                catch (RakuError& e) {
                    if (!(e.payload.t == VT::Type && e.payload.s == "X::Multi::NoMatch")) throw;
                }
            }
            bool b = I.boolify(v);
            return Value::boolean(negate ? !b : b);
        };
        return code;
    };
    // `so`/`not` are word PREFIXES, and a program may add candidates to them
    // (Red: `multi prefix:<so>(Red::AST $a)` builds SQL instead of a Bool).
    // Same rule as evalUnary's for a symbolic prefix: an OBJECT operand tries
    // the user's candidates first, and one none of them takes falls through
    // to the built-in, as Rakudo's wider core candidate would take it.
    auto userWordPrefix = [](Interpreter& I, const char* op, ValueList& a, Value& out) -> bool {
        if (a.size() != 1 || a[0].t != VT::Object || !a[0].obj() || !a[0].obj()->cls) return false;
        Value* f = I.tctx_.cur ? I.tctx_.cur->find(std::string("&prefix:<") + op + ">") : nullptr;
        if (!f) return false;
        try { out = I.callCallable(*f, ValueList{a[0]}); return true; }
        catch (RakuError& e) {
            if (!(e.payload.t == VT::Type && e.payload.s == "X::Multi::NoMatch")) throw;
        }
        return false;
    };
    B["so"] = [boolCurry, userWordPrefix](Interpreter& I, ValueList& a) -> Value {
        if (a.size() == 1 && (a[0].t == VT::Whatever || (a[0].t == VT::Code && a[0].code() && a[0].code()->isWhateverCode)))
            return boolCurry(false, a[0], [&]() {
                Value* f = I.tctx_.cur ? I.tctx_.cur->find("&prefix:<so>") : nullptr;
                return f ? *f : Value::any(); }());
        if (Value r; userWordPrefix(I, "so", a, r)) return r;
        return Value::boolean(!a.empty() && I.boolify(a[0]));
    };
    B["not"] = [boolCurry, userWordPrefix](Interpreter& I, ValueList& a) -> Value {
        if (a.size() == 1 && (a[0].t == VT::Whatever || (a[0].t == VT::Code && a[0].code() && a[0].code()->isWhateverCode)))
            return boolCurry(true, a[0], [&]() {
                Value* f = I.tctx_.cur ? I.tctx_.cur->find("&prefix:<not>") : nullptr;
                return f ? *f : Value::any(); }());
        if (Value r; userWordPrefix(I, "not", a, r)) return r;
        return Value::boolean(a.empty() || !I.boolify(a[0]));
    };
    // Junction constructors: all()/any()/one()/none() (also written via & | ^).
    // (all/any/one/none are registered ONCE, earlier, with the one-arg rule —
    // a flattening duplicate here used to shadow it)
    // `rand` is a TERM, not a sub: the Perl 5 call forms `rand()` and `rand($n)`
    // are a compile-time X::Obsolete in Rakudo, which tells the reader to write
    // `rand` or `$n.rand` instead. Registering the name here is what makes the
    // CALL form reach an error that says so — it used to be
    // X::Undeclared::Symbols, which names the wrong problem. The bare term never
    // comes through a builtin at all (the lexer knows it), so it is unaffected.
    B["rand"] = [](Interpreter&, ValueList& a) -> Value {
        throw RakuError{Value::typeObj("X::Obsolete"),
            a.empty()
              ? std::string("Unsupported use of rand(); in Raku please use: rand")
              : std::string("Unsupported use of rand(N); in Raku please use: N.rand "
                            "for a random number in the range 0..^N")};
    };
    B["ord"] = [](Interpreter& I, ValueList& a) -> Value {
        // bare `ord` (no argument) is the Perl-5-ism Rakudo rejects with X::Obsolete
        if (a.empty()) throw RakuError{Value::typeObj("X::Obsolete"),
            "Unsupported use of bare \"ord\". In Raku please use: .ord if you meant to call it as a method on $_, or use an explicit invocant or argument"};
        return rtBOrd(I, a[0]);
    };
    B["chr"] = [](Interpreter& I, ValueList& a) -> Value {
        return rtBChr(I, a.empty() ? Value::integer(0) : a[0]);
    };
    B["ords"] = [](Interpreter& I, ValueList& a) -> Value { Value v = a.empty() ? Value::any() : a[0]; ValueList none; return I.methodCall(v, "ords", none); };
    // parse-base($str, $radix) — the SUB form of the method (Digest's md5.t
    // builds its expected digests with `parse-base($hex, 16).polymod(…)`)
    B["parse-base"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::any();
        Value v = a[0];
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(v, "parse-base", rest);
    };
    // `pack TEMPLATE, @items` (use experimental :pack) — the inverse of Buf.unpack,
    // sharing its directive set: A/a/Z text, C/c bytes, S/v/n 16-bit, L/V/N 32-bit,
    // Q 64-bit, H hex digits, x a null byte. S/L/Q are native (little-endian here),
    // v/V little and n/N big. MIME::Base64's test suite builds UTF-16 input with it.
    B["pack"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) { Value b = Value::str(""); b.hashKind = "Buf"; return identify(b); }
        std::string tmpl = a[0].toStr();
        ValueList items;
        for (size_t i = 1; i < a.size(); i++)
            for (auto& x : toList(a[i])) items.push_back(x);
        std::string out;
        size_t ai = 0;
        auto next = [&]() -> Value { return ai < items.size() ? items[ai++] : Value::integer(0); };
        auto putLE = [&](unsigned long long v, int w) { for (int k = 0; k < w; k++) out += (char)((v >> (8 * k)) & 0xFF); };
        auto putBE = [&](unsigned long long v, int w) { for (int k = w - 1; k >= 0; k--) out += (char)((v >> (8 * k)) & 0xFF); };
        for (size_t k = 0; k < tmpl.size(); k++) {
            char dir = tmpl[k];
            if (ascii::isspace((unsigned char)dir)) continue;
            bool all = false; long long cnt = 1;
            if (k + 1 < tmpl.size() && tmpl[k + 1] == '*') { all = true; k++; }
            else if (k + 1 < tmpl.size() && ascii::isdigit((unsigned char)tmpl[k + 1])) {
                size_t j = k + 1; std::string num;
                while (j < tmpl.size() && ascii::isdigit((unsigned char)tmpl[j])) num += tmpl[j++];
                cnt = std::stoll(num); k = j - 1;
            }
            if (dir == 'A' || dir == 'a' || dir == 'Z') {
                std::string t = next().toStr();
                long long w = all ? (long long)t.size() + (dir == 'Z' ? 1 : 0) : cnt;
                for (long long i2 = 0; i2 < w; i2++)
                    out += i2 < (long long)t.size() ? t[i2] : (dir == 'A' ? ' ' : '\0');
            }
            else if (dir == 'H') {
                std::string t = next().toStr();
                long long w = all ? (long long)t.size() : cnt;
                auto hv = [](char c) { return c >= '0' && c <= '9' ? c - '0'
                                            : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                            : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; };
                for (long long i2 = 0; i2 < w; i2 += 2) {
                    int hi = i2 < (long long)t.size() ? hv(t[i2]) : 0;
                    int lo = i2 + 1 < w && i2 + 1 < (long long)t.size() ? hv(t[i2 + 1]) : 0;
                    out += (char)((hi << 4) | lo);
                }
            }
            else if (dir == 'x') { for (long long i2 = 0; i2 < (all ? 1 : cnt); i2++) out += '\0'; }
            else {
                int w = (dir == 'C' || dir == 'c') ? 1
                      : (dir == 'S' || dir == 'v' || dir == 'n') ? 2
                      : (dir == 'Q' || dir == 'q') ? 8 : 4;
                bool bigEnd = (dir == 'n' || dir == 'N');
                long long r = all ? (long long)(items.size() - ai) : cnt;
                for (long long i2 = 0; i2 < r; i2++) {
                    unsigned long long v = (unsigned long long)next().toInt();
                    if (bigEnd) putBE(v, w); else putLE(v, w);
                }
            }
        }
        Value b = Value::str(out); b.hashKind = "Buf"; return identify(b);
    };
    // callframe($level) — the caller's frame $level steps up: its .file, the LINE
    // the call was written on, and .code (the routine it sits in, `<unit>` at
    // mainline). Log::Async stamps every log message with `callframe(1)`.
    B["callframe"] = [](Interpreter& I, ValueList& a) -> Value {
        long long lvl = a.empty() ? 0 : a[0].toInt();
        if (lvl < 0) lvl = 0;
        auto& fr = I.tctx_.callFrames;
        // past the outermost frame there is no frame at all: Nil, so a walk
        // `callframe($_) or last` ends (MoarVM#562)
        if (lvl > (long long)fr.size() + 1) return Value::nil();
        Value f = Value::makeHash(); f.hashKind = "CallFrame";
        (*f.hash())["file"] = Value::str(I.srcFileAbs_.empty() ? I.srcFile_ : I.srcFileAbs_);
        (*f.hash())["prog"] = Value::str(I.srcFile_);   // as the program was named (`.gist`)
        // level 0 is where we are now (the current statement's line); each further
        // level steps out one activation, taking that call's own line with it
        size_t idx = fr.size();                 // frames_[idx-1] is the innermost
        long long line = I.curLine_;
        const Value* code = nullptr;
        for (long long k = 0; k < lvl; k++) {
            if (idx == 0) break;
            line = fr[idx - 1].line;            // the line THIS activation was called from
            idx--;
        }
        if (idx > 0) code = fr[idx - 1].code;
        (*f.hash())["line"] = Value::integer(line);
        if (code) (*f.hash())["code"] = *code;
        // `.my` — that frame's lexicals, as a live stash (only an `is dynamic`
        // one may be written through it)
        {
            std::string chain = "LEXICAL";
            if (lvl > 0) { chain.clear(); for (long long k = 0; k < lvl; k++) chain += (k ? "::" : "") + std::string("CALLER"); }
            Value st = I.makePseudoStash(chain);
            if (st.t == VT::Object && st.obj()) st.obj()->attrs["dynonly"] = Value::boolean(true);
            (*f.hash())["my"] = st;
        }
        return f;
    };
    // `MY::<&foo>:exists` — is that symbol declared in the current scope chain?
    // A second argument counts OUTER hops: `OUTER::MY::<$x>:exists` asks the
    // ENCLOSING scope, so the answer at unit scope is False however visible the
    // name is here (Rakudo's, checked both ways).
    // A third argument names the pseudo-package. `UNIT::` asks the COMPILATION
    // UNIT's own scope rather than the current one, so it is the outermost frame
    // of the chain we are standing in, not an OUTER hop count — which is what a
    // module's `EXPORT` sub needs: it runs inside its own routine frame and asks
    // about the file's symbols. Test::Output builds its whole export list that
    // way (`UNIT::{"&$_"}:exists`), and rakupp answered "Undefined routine
    // 'exists'" because only MY:: and LEXICAL:: ever reached here.
    auto symEnv = [](Interpreter& I, ValueList& a, size_t hopIdx) -> Env* {
        Env* e = I.tctx_.cur.get();
        if (a.size() > hopIdx + 1 && a[hopIdx + 1].toStr() == "UNIT") {
            // The compilation unit's scope is the OUTERMOST frame below the
            // process-wide global one: a module's file scope hangs off the
            // global frame, and walking all the way up answered the global
            // frame instead — where a module sub that shadows an operator
            // (String::Utils' `after`/`before`) is never published, so
            // `UNIT::.grep: { .key.starts-with('&') }` could not export it.
            // The mainline's own unit is that global frame, and stays so.
            Env* g = I.global_.get();
            while (e && e->parent && e->parent.get() != g) e = e->parent.get();
            return e;
        }
        for (long long hops = a.size() > hopIdx ? a[hopIdx].toInt() : 0; hops > 0 && e; hops--)
            e = e->parent.get();
        return e;
    };
    B["__sym-exists"] = [symEnv](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        const std::string n = a[0].toStr();
        Env* e = symEnv(I, a, 1);
        if (e && e->find(n)) return Value::boolean(true);
        return Value::boolean(false);
    };
    // `UNIT::<&foo>:p` is the KEY => VALUE pair when the symbol is there, and
    // NOTHING (an empty list, which flattens away) when it is not — that is what
    // makes `@names.map: { UNIT::{"&$_"}:p }` build a Map of just the symbols
    // that exist.
    B["__sym-pair"] = [symEnv](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::list({});
        const std::string n = a[0].toStr();
        Env* e = symEnv(I, a, 1);
        if (e) if (Value* v = e->find(n)) return Value::pair(n, *v);
        return Value::list({});
    };
    // A bare pseudo-package as a VALUE — `UNIT::.grep: {…}`, `MY::.keys` — is
    // the scope's symbol table, as a Hash of name => value. `UNIT::` is the
    // compilation unit's own frame (the outermost one, as above), `MY::` the
    // current scope, `LEXICAL::` the whole chain with inner names shadowing
    // outer ones. String::Utils exports everything `&`-named this way.
    B["__sym-stash"] = [symEnv](Interpreter& I, ValueList& a) -> Value {
        Value h = Value::makeHash();
        Env* e = symEnv(I, a, 0);
        const std::string pk = a.size() > 1 ? a[1].toStr() : std::string("MY");
        // a frame's names: its map, then its live pad slots (a routine's, the
        // mainline's or an inline block's own `my`s live there)
        auto addFrame = [&h](Env* x) {
            for (auto& kv : x->vars)
                if (!h.hash()->count(kv.first)) (*h.hash())[kv.first] = kv.second;
            if (x->layout) {
                uint64_t live = x->padLive.load(std::memory_order_acquire);
                for (size_t i = 0; i < x->pad.size(); i++)
                    if (((live >> i) & 1) && !h.hash()->count(x->layout->names[i]))
                        (*h.hash())[x->layout->names[i]] = x->pad[i];
            }
        };
        if (pk == "LEXICAL") for (Env* x = e; x; x = x->parent.get()) addFrame(x);
        else if (e) addFrame(e);
        return h;
    };
    // `MY::`, `CALLER::OUTER::`, … as a term: the live stash (makePseudoStash)
    B["__pseudo-stash"] = [](Interpreter& I, ValueList& a) -> Value {
        return I.makePseudoStash(a.empty() ? std::string("MY") : a[0].toStr());
    };
    // `OUTER::MY::<$x>` — the same lookup as `MY::<$x>`, started that many scopes
    // out. A miss is Nil, as Rakudo's is; the no-hop forms resolve at parse time.
    // CLIENT:: — the frames of the running routine's own compilation unit are
    // skipped; the first caller from elsewhere (or the mainline) is the client
    // `1 + %h<k>:exists` — the adverb went to the built-in operator (see
    // Parser::adverbToOperator), and no candidate of it takes a named argument
    B["__adverb-nomatch"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string sig;
        for (size_t i = 1; i < a.size(); i++) {
            if (!sig.empty()) sig += ", ";
            if (a[i].t == VT::Pair && a[i].namedArg) sig += ":" + a[i].s.str();
            else sig += a[i].typeName() + (a[i].t == VT::Type ? ":U" : ":D");
        }
        throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                        "Cannot resolve caller " + (a.empty() ? std::string("?") : a[0].toStr()) + "(" + sig +
                        "); no candidate takes a named argument"};
    };
    B["__assign-immutable"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.empty() ? Value::any() : a[0];
        throw RakuError{Value::typeObj("X::Assignment::RO"),
                        "Cannot modify an immutable " + v.typeName() + " (" + v.gist() + ")"};
    };
    B["__client-lookup"] = [](Interpreter& I, ValueList& a) -> Value {
        const std::string sym = a.empty() ? std::string() : a[0].toStr();
        auto& fr = Interpreter::tctx_.callFrames;
        int rev = I.mainLangRev_;
        std::string pkg = "GLOBAL";
        if (!fr.empty() && fr.back().code && fr.back().code->t == VT::Code && fr.back().code->code()) {
            const std::string curFile = fr.back().code->code()->declFile;
            for (size_t i = fr.size() - 1; i-- > 0;) {
                const Value* cv = fr[i].code;
                if (!cv || cv->t != VT::Code || !cv->code()) continue;
                if (cv->code()->declFile == curFile) continue;
                rev = cv->code()->langRev;
                const std::string cf = cv->code()->declFile;
                pkg.clear();
                for (size_t j = i + 1; j-- > 0;) {
                    const Value* dv = fr[j].code;
                    if (!dv || dv->t != VT::Code || !dv->code() || dv->code()->declFile != cf) continue;
                    if (!dv->code()->pkg.empty()) { pkg = dv->code()->pkg; break; }
                }
                if (pkg.empty() || pkg == "GLOBAL") {
                    auto uit = I.unitPkgByFile_.find(cf);
                    pkg = uit != I.unitPkgByFile_.end() ? uit->second : std::string("GLOBAL");
                }
                break;
            }
        }
        if (sym == "CORE-SETTING-REV") return Value::str(rev <= 0 ? "c" : rev == 1 ? "d" : "e");
        if (sym == "$?PACKAGE") return Value::typeObj(pkg);
        VarExpr v(sym);
        return I.eval(&v);
    };
    // `OUTER::<$x>` is ONE scope, the one N steps out — not that scope and
    // outwards: an outer-outer `$x` is no OUTER's (Rakudo answers Nil). The
    // `state` frames of a loop or a routine are no scopes of the program's, so a
    // step passes them. A `my` further down the scope already counts
    // (Interpreter::frameSymbol).
    B["__sym-lookup"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::nil();
        const std::string n = a[0].toStr();
        Env* e = I.tctx_.cur.get();
        for (long long hops = a.size() > 1 ? a[1].toInt() : 0; hops > 0 && e; hops--) {
            e = e->parent.get();
            while (e && (e->loopFrame || e->stateFrame) && e->parent) e = e->parent.get();
        }
        Value v;
        if (e && I.frameSymbol(e, n, v)) return v;
        // …but every block HAS a `$_`, the outer one's unless it topicalizes:
        // `start { cas $a, -> @c { … OUTER::<$_> } }` reads the start block's,
        // which is the `map` element around it (S17-promise/allof.t)
        if (e && n == "$_") if (Value* t = e->find(n)) return *t;
        return Value::nil();
    };
    B["chrs"] = [](Interpreter&, ValueList& a) -> Value { std::string r; for (auto& x : flattenArgs(a)) r += cpToUtf8((uint32_t)x.toInt()); return Value::str(r); };
    // msb/lsb — the position of an Int's highest and lowest set bit, counting
    // from 0. Rakudo's answer for a NEGATIVE argument is the two's-complement
    // one: msb(-1) is 0, msb(-255) is 8 (one more than msb(255)), because the
    // sign bit needs a place. Zero has no set bit at all, so both answer Nil.
    // Rat::Precise sizes its decimal expansion with `msb(self.denominator)`.
    // Both read their answer off the limbs (BigInt::bitLength / lowestSetBit):
    // at once for most numbers, and 23 ms for the hardest million-bit ones.
    {
        auto asBig = [](const Value& v) {
            return v.big() ? *v.big() : BigInt(v.toInt());
        };
        B["msb"] = [asBig](Interpreter&, ValueList& a) -> Value {
            if (a.empty()) return Value::nil();
            BigInt n = asBig(a[0]);
            if (n.isZero()) return Value::nil();
            // a negative needs the bit that holds its sign: msb(-n) is the
            // length of (|n| - 1), which is 0 for -1 and 8 for both -255 and -256
            if (n.sign < 0) return Value::integer((n.abs() - BigInt(1)).bitLength());
            return Value::integer(n.bitLength() - 1);
        };
        B["lsb"] = [asBig](Interpreter&, ValueList& a) -> Value {
            if (a.empty()) return Value::nil();
            BigInt n = asBig(a[0]);
            if (n.isZero()) return Value::nil();
            return Value::integer(n.lowestSetBit());
        };
    }
    B["sign"] = [](Interpreter& I, ValueList& a) -> Value { return rtBSign(I, a.empty() ? Value::any() : a[0]); };
    B["is-prime"] = [](Interpreter& I, ValueList& a) -> Value { return rtBIsPrime(I, a.empty() ? Value::any() : a[0]); };
    // `end` and `kv` are protos of ONE positional (`($, *%)`), so neither no
    // argument nor several can ever bind: `end(1,2,3,4)` is an error, where
    // `end (1,2,3,4)` passes the one list and answers 3.
    auto oneArgProto = [](const char* name, ValueList& a) {
        if (a.size() == 1) return;
        throw RakuError{Value::typeObj("X::TypeCheck::Argument"),
            "Calling " + std::string(name) + "(" + (a.empty() ? "" : "...") +
            ") will never work with signature of the proto ($, *%)"};
    };
    B["end"] = [oneArgProto](Interpreter& I, ValueList& a) -> Value { oneArgProto("end", a); ValueList none; return I.methodCall(a[0], "end", none); };
    B["kv"] = [oneArgProto](Interpreter& I, ValueList& a) -> Value { oneArgProto("kv", a); ValueList none; return I.methodCall(a[0], "kv", none); };
    B["prepend"] = [](Interpreter& I, ValueList& a) -> Value { arrayOpArgs("prepend", a, false); if (a.empty()) return Value::any(); Value inv = a[0]; ValueList rest(a.begin() + 1, a.end()); return I.methodCall(inv, "prepend", rest); };
    B["append"] = [](Interpreter& I, ValueList& a) -> Value { arrayOpArgs("append", a, false); if (a.empty()) return Value::any(); Value inv = a[0]; ValueList rest(a.begin() + 1, a.end()); return I.methodCall(inv, "append", rest); };
    // …through the METHOD, so the sub and the method stringify identically.
    // joinValues asks each element's raw rendering, which does not know about a
    // user `method Str` — so `join(';', $obj)` printed Class<address> where
    // `@list.join(';')` printed the object's own text. Dice::Roller's whole
    // display is `join('; ', @!rolls)` over objects that define Str.
    B["join"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        Value items = Value::array(); items.isList = true;
        for (size_t i = 1; i < a.size(); i++) {
            // an ITEMIZED list (`$[…]`) is one thing to join, not several
            if ((a[i].t == VT::Array || a[i].t == VT::Hash) && a[i].itemized && a[i].hashKind.empty() &&
                a[i].enumName.empty()) { items.arr()->push_back(a[i]); continue; }
            for (auto& x : toList(a[i])) items.arr()->push_back(x);
        }
        return I.methodCall(items, "join", ValueList{a[0]});
    };
    // `«a $x»` whose words interpolate (Parser::qqwwList): a[0] is "c"/"n"
    // (collapse a one-word result to that word, or not) then one letter per item
    // of the list in a[1] — `l` an item as it stands, `s` an interpolated value
    // to split on whitespace and val() word by word, `w` to split only, `v` a
    // quoted interpolation to val() whole.
    B["__qqww"] = [](Interpreter& I, ValueList& a) -> Value {
        const std::string flags = a.size() > 0 ? a[0].toStr() : std::string("n");
        ValueList items = a.size() > 1 ? toList(a[1]) : ValueList{};
        ValueList out;
        for (size_t k = 0; k < items.size(); k++) {
            const char f = k + 1 < flags.size() ? flags[k + 1] : 'l';
            if (f == 's' || f == 'w') {
                Value s = I.methodCall(items[k], "Str", ValueList{});
                for (auto& w : toList(I.methodCall(s, "words", ValueList{})))
                    out.push_back(f == 's' ? rakupp::valAllomorph(w) : w);
            }
            else if (f == 'v') out.push_back(rakupp::valAllomorph(I.methodCall(items[k], "Str", ValueList{})));
            else out.push_back(items[k]);
        }
        if (flags[0] == 'c' && out.size() == 1) return out[0];
        return Value::list(out);
    };
    registerBuiltinsPart4();
}

// registerBuiltins, continued. Split for compile time: each piece ends by
// calling the next, so the registrations run in the original order (a later
// one of the same name still replaces an earlier one).
void Interpreter::registerBuiltinsPart4() {
    auto& B = builtins_;
    // :16("2e") radix conversion — the value's digits parsed in the given base.
    // :256[a, b, c] — place-value digits in the given base; slips/arrays
    // in the list flatten (`:256[|@^a]`)
    B["__radix-list"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::integer(0);
        // `:100[3, '.', 14, 16]` — a '.' element is the radix point: the digits
        // after it are the fraction, and the answer is a Rat (3.1416)
        {
            ValueList ds; bool point = false;
            for (size_t k = 1; k < a.size(); k++) {
                if (a[k].t == VT::Array && a[k].arr()) for (auto& e : *a[k].arr()) ds.push_back(e);
                else ds.push_back(a[k]);
            }
            for (auto& d : ds) if (d.t == VT::Str && d.s == ".") { point = true; break; }
            if (point) {
                Value base = a[0].t == VT::Str ? Value::bigint(BigInt::fromString(a[0].toStr())) : a[0];
                Value acc = Value::integer(0), den = Value::integer(1);
                bool frac = false;
                for (auto& d : ds) {
                    if (d.t == VT::Str && d.s == ".") { frac = true; continue; }
                    acc = applyArith("+", applyArith("*", acc, base), Value::integer(d.toInt()));
                    if (frac) den = applyArith("*", den, base);
                }
                return applyArith("/", acc, den);
            }
        }
        if (a[0].t == VT::Str) {   // a base past 64 bits, as its digits
            BigInt b = BigInt::fromString(a[0].toStr()), acc(0LL);
            auto addB = [&](const Value& d) {
                acc = acc * b + (d.t == VT::Int && d.big() ? *d.big() : BigInt(d.toInt()));
            };
            for (size_t k = 1; k < a.size(); k++) {
                if (a[k].t == VT::Array && a[k].arr())
                    for (auto& e : *a[k].arr()) addB(e);
                else addB(a[k]);
            }
            return Value::bigint(acc);
        }
        long long base = a[0].toInt();
        // accumulate in int64 until the next place-shift would overflow, then
        // spill to BigInt — :256[16 bytes] is a 128-bit value (UUID.Str)
        long long val = 0;
        bool big = false; BigInt bval;
        long long lim = (std::numeric_limits<long long>::max)() / (base > 1 ? base : 2) - base;
        auto add = [&](long long d) {
            if (!big) {
                if (val > lim) { big = true; bval = BigInt(val); }
                else { val = val * base + d; return; }
            }
            bval = bval * BigInt(base) + BigInt(d);
        };
        for (size_t k = 1; k < a.size(); k++) {
            if (a[k].t == VT::Array && a[k].arr())
                for (auto& e : *a[k].arr()) add(e.toInt());
            else add(a[k].toInt());
        }
        return big ? Value::bigint(bval) : Value::integer(val);
    };
    B["__radix"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::integer(0);
        int base = (int)a[0].toInt();
        // `:10(42)` — the call form converts STRINGS; a number handed to it is
        // a confusion Rakudo names (X::Numeric::Confused), not a no-op
        if (!a[1].isAllomorph() && a[1].hashKind.empty() &&
            (a[1].t == VT::Int || a[1].t == VT::Rat || a[1].t == VT::Num || a[1].t == VT::Array)) {
            std::string tn = a[1].t == VT::Array ? std::string("Array") : a[1].typeName();
            std::string sv = a[1].toStr();
            I.throwTypedV("X::Numeric::Confused", {{"num", a[1]}, {"base", Value::integer(base)}},
                "This call only converts base-" + std::to_string(base) + " strings to numbers; value " +
                sv + " is of type " + tn + ", so cannot be converted!\n(If you really wanted to convert " +
                sv + " to a base-" + std::to_string(base) + " string, use " + sv + ".base(" +
                std::to_string(base) + ") instead.)");
        }
        std::string s = a[1].toStr();
        // The STRING may name its own base, which then wins over the literal's:
        // `:10('0b1110')` is 14, and so is `:10(':2<1110>')`.
        // …but only when that letter is no DIGIT of the base: `:16("0d4a1185")`
        // is hex, `0d` its first two digits
        if (s.size() > 2 && s[0] == '0' && std::strchr("bodx", s[1]) && s[1] - 'a' + 10 >= base) {
            base = s[1] == 'b' ? 2 : s[1] == 'o' ? 8 : s[1] == 'd' ? 10 : 16;
            s = s.substr(2);
        }
        else if (s.size() > 3 && s[0] == ':' && ascii::isdigit((unsigned char)s[1])) {
            size_t lt = s.find('<');
            if (lt != std::string::npos && s.back() == '>') {
                base = std::atoi(s.substr(1, lt - 1).c_str());
                s = s.substr(lt + 1, s.size() - lt - 2);
            }
        }
        // Every character has to be a digit OF THAT BASE (or a separating `_`, or the
        // one radix point): `:16<fo>` is X::Str::Numeric, not 15. Stopping at the first
        // bad character silently turned a malformed colour like 'foobar' into a number
        // — which is how Color.new('foobar') "worked".
        auto bad = [&](const std::string& why) -> Value {
            throw RakuError{Value::typeObj("X::Str::Numeric"),
                "Cannot convert string to number: " + why + " in ':" + std::to_string(base) +
                "<" + s + ">'"};
        };
        // BigInt throughout: a long long silently overflowed, so
        // `:16("FFFFFFFFFFFFFFFF")` answered -1 rather than 18446744073709551615
        BigInt val(0), den(0), bb((long long)base);   // den = 0 until a radix point is met
        bool any = false;
        for (size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == '_') {
                if (i == 0 || i + 1 == s.size()) return bad("'_' must be between digits");
                continue;
            }
            if (c == '.') {
                if (!den.isZero()) return bad("more than one radix point");
                den = BigInt(1); continue;
            }
            int d = (c >= '0' && c <= '9') ? c - '0'
                  : (c >= 'a' && c <= 'z') ? c - 'a' + 10
                  : (c >= 'A' && c <= 'Z') ? c - 'A' + 10 : -1;
            if (d < 0 || d >= base)
                return bad("base-" + std::to_string(base) + " number must begin with valid digits or '.'");
            val = val * bb + BigInt(d);
            if (!den.isZero()) den = den * bb;
            any = true;
        }
        if (!any) return bad("base-" + std::to_string(base) + " number must begin with valid digits or '.'");
        if (!den.isZero() && BigInt::cmp(den, BigInt(1)) > 0) return Value::rat(val, den);
        return Value::bigint(val);
    };
    // split(SEP, STR, …) is the sub form of STR.split(SEP, …)
    B["split"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::array();
        ValueList margs; margs.push_back(a[0]);
        for (size_t i = 2; i < a.size(); i++) margs.push_back(a[i]);
        return I.methodCall(a[1], "split", margs, nullptr);
    };
    // The single-argument rule: ONE Iterable argument is the list to reverse,
    // SEVERAL are each one element — `reverse((1,2), 3)` is `(3, $(1, 2))`, the
    // inner list kept whole. With nothing at all there is no meaning to give
    // (List-Array sheet LA-35).
    B["reverse"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty())
            return armedFailure("X::NoZeroArgMeaning", "No zero-arg meaning for infix:<reverse>");
        ValueList items;
        // a lazy source (`reverse lines`) has to be read to its end first
        if (a.size() == 1 && a[0].t == VT::Array && a[0].ext() && a[0].arr() &&
            !std::static_pointer_cast<LazySeqState>(a[0].ext())->infinite)
            forceLazy(a[0]);
        if (a.size() == 1) items = toList(a[0]);
        else for (auto& v : a) {
            Value e = v;
            if (e.t == VT::Array || e.t == VT::Hash) e.itemized = true; // an argument slot is a container
            items.push_back(e);
        }
        std::reverse(items.begin(), items.end());
        return Value::seq(items);
    };
    B["sort"] = [](Interpreter& I, ValueList& a) -> Value {
        // `sort {comparator}, @list` / `sort &by, @list`: a leading Code is the
        // comparator/key extractor, not an element. `:by(&f)` names it; other
        // adverbs (:k) are forwarded. All such forms delegate to List.sort.
        // `sort()` with nothing to sort is a mistake, not the empty list
        // (Nil-Any sheet NA-24).
        if (a.empty())
            throw RakuError{Value::typeObj("X::AdHoc"), "Must specify something to sort"};
        Value cmp; bool haveCmp = false;
        ValueList named, pos;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.namedArg) {
                if (v.s == "by" && v.pairVal()) { cmp = *v.pairVal(); haveCmp = true; }
                else named.push_back(v);
            }
            else pos.push_back(v);
        }
        if (!haveCmp && !pos.empty() && pos[0].t == VT::Code) {
            cmp = pos[0]; haveCmp = true;
            pos.erase(pos.begin());
        }
        ValueList items; for (auto& v : pos) { ValueList l = toList(v); items.insert(items.end(), l.begin(), l.end()); }
        if (haveCmp || !named.empty()) {
            Value lst = Value::list(items);
            ValueList ma;
            if (haveCmp) ma.push_back(cmp);
            for (auto& nv : named) ma.push_back(nv);
            return I.methodCall(lst, "sort", ma);
        }
        std::stable_sort(items.begin(), items.end(), [](const Value& x, const Value& y) { return valueCmp(x, y) < 0; });
        Value o = Value::array(items); o.isList = true; o.s = "Seq"; return o;
    };
    B["sum"] = [](Interpreter& I, ValueList& a) -> Value {
        // delegate to the method (like min/max): the exact tower keeps big Ints
        // and Rats exact where the old double accumulator silently rounded
        ValueList items;
        // A Range answers for itself — toList would hand back the truncated
        // prefix of an endless (`sum(1..Inf)` is Inf) or a huge one
        // (`sum(1..10**100)` is its Gauss sum). Summing per operand and adding
        // the parts is the same total.
        Value ranges; bool sawRange = false;
        for (auto& v : a) {
            if (v.t == VT::Range || isEndlessLazy(v)) {
                Value s = I.methodCall(v, "sum", ValueList{});
                ranges = sawRange ? applyArith("+", ranges, s) : s;
                sawRange = true;
                continue;
            }
            for (auto& x : toList(v)) items.push_back(x);
        }
        Value list = Value::array(items); list.isList = true;
        ValueList none;
        Value rest = I.methodCall(list, "sum", none);
        if (!sawRange) return rest;
        return items.empty() ? ranges : applyArith("+", rest, ranges);
    };
    // `keys(…)` / `values(…)` ARE the methods: they answered a fresh ARRAY, so
    // `keys(42)` was `[0]` where the method says `(0,).Seq` and `values(42)`
    // `[42]` where it says `(42,)` — the same list, three shapes apart
    // (Nil-Any sheet NA-49).
    for (auto nm : {"keys", "values"})
        B[nm] = [nm](Interpreter& I, ValueList& a) -> Value {
            if (a.empty()) return Value::nil();
            Value inv = a[0]; ValueList rest(a.begin() + 1, a.end());
            return I.methodCall(inv, nm, rest);
        };
    // Synchronous react/whenever/supply: eager, deterministic model.
    B["react"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty() || a.back().t != VT::Code) return Value::nil();
        auto ctx = std::make_shared<ReactCtx>();
        I.reactStack_.push_back(ctx);
        try { I.callCallable(a.back(), {}); }
        catch (DoneEx&) {} // `done` in the react body: normal completion (ctx already closed)
        catch (...) { I.reactStack_.pop_back(); throw; }
        I.reactStack_.pop_back();
        // Deferred whenever activations (issue #18): the body has finished, so
        // statements after a `whenever` have run — now drain the synchronous
        // sources, in registration order. A die inside a drained body kills
        // the react with that exception, exactly like the body itself dying.
        try {
            for (;;) {
                std::vector<std::function<void()>> ds;
                { std::lock_guard<std::mutex> lk(ctx->m); ds.swap(ctx->deferred); }
                if (ds.empty()) break;
                for (auto& d : ds) d();
            }
        }
        catch (DoneEx&) {}
        I.runReactLoop(ctx); // block until every live whenever source is done
        {   // react is over: tear down externally-wired taps (OS-signal taps) so
            // their dispatcher stops firing the handler once the block is gone.
            std::vector<std::shared_ptr<TapHandle>> extTaps;
            { std::lock_guard<std::mutex> lk(ctx->m); extTaps.swap(ctx->extTaps); }
            for (auto& h : extTaps) if (h) I.closeTapHandle(h);
        }
        {   // react is over: its whenever taps close — run on-close callbacks
            ValueList closers;
            { std::lock_guard<std::mutex> lk(ctx->m); closers.swap(ctx->closers); }
            for (auto& cb : closers) if (cb.t == VT::Code) { try { I.callCallable(cb, {}); } catch (...) {} }
        }
        if (ctx->quitFlag) { // a whenever'd supply quit unhandled: the react dies with it
            std::string qm = "Supply quit";
            try { ValueList na; Value mv = I.methodCall(ctx->quitErr, "message", na); if (mv.t == VT::Str) qm = mv.s; } catch (...) {}
            // …and the exception says so: the original is handed on with
            // X::React::Died mixed in, so a CATCH can tell a death that came out
            // of a react from one raised where it stands (Roast
            // syntax-nonblocking-await.t asks `.does(X::React::Died)`).
            Value err = ctx->quitErr;
            if (err.t == VT::Object)
                try { err = I.mixinValue(err, Value::typeObj("X::React::Died"), /*copy=*/true); } catch (...) {}
            throw RakuError{err, qm};
        }
        return Value::nil();
    };
    B["whenever"] = [](Interpreter& I, ValueList& a) -> Value {
        // S-60: `whenever` coerces its argument with `Supply()`. An Iterable is
        // one event per element; a Str, an Int or any other single value is one
        // event. Coercing to a from-list Supply (rather than running the body
        // inline) is what puts those events behind the body, as S-53 requires.
        auto coerceToSupply = [](Value v) -> Value {
            ValueList vs;
            if (v.t == VT::Range) vs = v.flatten();
            else if (v.t == VT::Array && v.arr()) vs = *v.arr();
            else if (v.t == VT::Hash && v.hashKind.empty() && v.hash()) {
                for (auto& kv : *v.hash()) { Value pr = Value::pair(kv.first, kv.second); vs.push_back(pr); }
            }
            else vs.push_back(v);
            Value s2 = Value::makeHash(); s2.hashKind = "Supply";
            Value a2 = Value::array(); *a2.arr() = std::move(vs);
            (*s2.hash())["values"] = a2;
            return s2;
        };

        // `whenever $supplier` coerces via .Supply (Rakudo does the same): a raw
        // Supplier used to fall through to the run-once-with-the-value arm, which
        // ran the handler EAGERLY with the Supplier as topic. That deadlock was
        // masked by awaitPromise's no-workers escape until a real async source
        // (Supply.interval) engaged the GIL earlier in the program.
        // …and so does anything else that PUBLISHES a Supply. An
        // IO::Socket::Async is the one in the wild: `whenever $conn -> $msg`
        // (no `.Supply`) is how IO::Socket::Async::SSL's own upgrade test
        // reads a connection, and it fell through to the run-once-with-the-
        // value arm below, which ran the body EAGERLY with the socket as its
        // topic. That is what made a `my $tap = do whenever $conn {…$tap…}`
        // report `$tap` undeclared: the body ran while the declaration it
        // belongs to was still being evaluated.
        // (`whenever $proc` taps a Proc::Async's merged output, `$proc.Supply`)
        static const char* supplyish[] = {"Supplier", "AsyncSocket", "Proc::Async"};
        if (!a.empty() && a[0].t == VT::Hash)
            for (auto* k : supplyish)
                if (a[0].hashKind == k) {
                    ValueList none; a[0] = I.methodCall(a[0], "Supply", none);
                    break;
                }
        // NOT extended to a user CLASS that publishes a Supply, though Rakudo
        // does coerce those too: IO::Socket::Async::SSL's `whenever $conn.head`
        // then taps a real stream and its upgrade test HANGS where it used to
        // finish with one failure. The tap is right and something downstream of
        // it is not; shipping the coercion without knowing what would trade a
        // wrong answer for a hang.
        // Inside an on-demand supply activation (real tap or eager drain): wire a
        // real inner tap. The body runs (now or later, from an I/O worker) with
        // this activation re-established, so its emits reach the downstream tap.
        if (!I.tctx_.tapStack.empty() && a.size() >= 2 && a.back().t == VT::Code) {
            auto ctx = I.tctx_.tapStack.back();
            Value src = a[0], blk = a.back();
            // whenever Promise.in(N)/at(T) in a supply block: a real timer (was
            // firing immediately — Cro's connection/headers timeouts rely on it).
            if (src.t == VT::Hash && src.hashKind == "Promise" && src.hash()->count("kind") &&
                (*src.hash())["kind"].toStr() == "timer") {
                return I.spawnSupplyTimer(timerRemainingSecs(src), blk, ctx);
            }
            // whenever Supply.interval(N) in a supply block: a repeating ticker
            // that keeps this activation open until done/close stops it.
            if (src.t == VT::Hash && src.hashKind == "Supply" && src.hash()->count("kind") &&
                (*src.hash())["kind"].toStr() == "watch")
                return I.spawnSupplyInterval(watchInterval(src), 0, watchFilter(src, I.wrapSupplyChain(src, blk)), ctx);
            if (src.t == VT::Hash && src.hashKind == "Supply" && src.hash()->count("kind") &&
                (*src.hash())["kind"].toStr() == "interval") {
                double iv = src.hash()->count("interval") ? (*src.hash())["interval"].toNum() : 1;
                double dl = src.hash()->count("delay") ? (*src.hash())["delay"].toNum() : 0;
                return I.spawnSupplyInterval(iv, dl, I.wrapSupplyChain(src, blk), ctx);
            }
            // whenever $channel in a supply block: a live source, one run per
            // value sent. Without this the Channel fell through to tapSupply()
            // below, which cannot tap one — so the block ran ONCE with the
            // Channel itself as its argument, and Log::Timeline printed a
            // stringified Channel onto its socket.
            if (src.t == VT::Hash && src.hashKind == "Channel")
                return I.spawnSupplyChannel(src, blk, ctx);
            // whenever over a Promise: register an ASYNC one-shot — the block runs
            // once, with the promise's RESULT, when the promise settles. Must NOT
            // block the supply-block setup: an unkept Promise stays dormant (Cro's
            // ResponseParser has `whenever $cancellation` that is normally never
            // kept — awaiting it synchronously hung the whole response pipeline).
            if (src.t == VT::Hash && src.hashKind == "Promise" && src.ext()) {
                auto ps = std::static_pointer_cast<PromiseState>(src.ext());
                ValueList lastP, quitP;
                scanSupplyPhasers(blk, &lastP, &quitP, nullptr);
                // a promise is a live source until it settles: hold the supply open
                // (so the block that ends after registering it doesn't finish early),
                // and release the hold once it fires.
                ctx->pending++;
                // fire under the supply activation so the body's emits reach downstream
                Value fireW = ctxCallable(ctx, [blk, lastP, quitP, ps, ctx](Interpreter& I2, ValueList&) -> Value {
                    if (ps->broken) {
                        // S-57, the same rule the Supply path's quitW follows: a QUIT
                        // phaser works like CATCH. A matching when/default consumes
                        // the break and this whenever merely counts as done; a phaser
                        // with no matching branch runs and then lets the quit travel
                        // on to the tapper. Running the phasers and then dropping the
                        // quit whenever one existed left the supply unfinished —
                        // Cro's redirect arm is a bare `QUIT { $request-log.end }`,
                        // so a redirect onto a 4xx hung even once the die below
                        // quit correctly.
                        Value ex = causeException(I2, ps->cause, ps->causeMsg);
                        Value repl;
                        int r = quitP.empty() ? 1 : I2.runQuitPhasers(quitP, ex, repl);
                        if (r != 0) {
                            Value out = r == 2 ? repl : ex;
                            if (ctx->quitCb.t == VT::Code) { ValueList one{out}; try { I2.callCallable(ctx->quitCb, one); } catch (...) {} }
                            // nothing may follow a quit (S-06); `done` also stops the
                            // maybeFinishSupply below emitting one after it.
                            ctx->done = true;
                            if (ctx->tap) I2.closeTapHandle(ctx->tap);
                        }
                    } else {
                        ValueList one{ ps->result };
                        bool died = false;
                        try { I2.callCallable(blk, one); }
                        catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                        catch (RakuError& e) {
                            // S-57: an exception raised by the whenever BODY is not a
                            // source quit — no QUIT phaser sees it; it ends the supply
                            // and reaches the TAPPER's quit handler, exactly as the
                            // Supply path below already does. Without this arm the
                            // throw escaped PAST the pending-- underneath and was
                            // swallowed by `run`'s catch(...), so the activation was
                            // never released: `Promise(supply {…})` stayed Planned for
                            // ever. Cro raises every 4xx/5xx as a `die` inside exactly
                            // this shape, so a 404 hung the client instead of throwing.
                            died = true;
                            Value ex = I2.exceptionFor(e);
                            if (ctx->quitCb.t == VT::Code) {
                                ValueList qa{ex};
                                try { I2.callCallable(ctx->quitCb, qa); } catch (...) {}
                            }
                            // nothing may follow a quit (S-06); `done` also makes the
                            // maybeFinishSupply below a no-op, so no done is emitted.
                            ctx->done = true;
                            if (ctx->tap) I2.closeTapHandle(ctx->tap);
                        }
                        if (!died) I2.runLastPhasers(lastP, nullptr);
                    }
                    ctx->pending--;
                    I2.maybeFinishSupply(ctx);
                    return Value::any();
                });
                Interpreter* self = &I;
                std::function<void()> run = [self, fireW]() { ValueList na; try { self->callCallable(fireW, na); } catch (...) {} };
                bool now = false;
                { std::lock_guard<std::mutex> lk(ps->m); if (ps->done) now = true; else ps->thens.push_back(run); }
                if (now) run();
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            // whenever over a plain (non-Supply, non-Promise) value: Supply() it
            if (!(src.t == VT::Hash && src.hashKind == "Supply")) {
                if (src.t == VT::Hash && src.hashKind == "Promise" && src.hash()->count("result"))
                    src = (*src.hash())["result"];
                src = coerceToSupply(src);
            }
            ValueList lastP, quitP;
            scanSupplyPhasers(blk, &lastP, &quitP, nullptr);
            // S-53: the subscription is made here and now, but a source that
            // delivers SYNCHRONOUSLY does so while this body is still running —
            // and one whenever's handler must never nest inside another body.
            // Each of the three callbacks therefore either runs straight away
            // (nothing is running) or joins the activation's queue, tagged with
            // this subscription so `last` can drop its backlog alone.
            const long long subId = ++ctx->subSeq;
            auto wrap = [&I, ctx, subId](std::function<void(Interpreter&, ValueList&)> fn) -> Value {
                return I.supplyDelivery(ctx, subId, std::move(fn));
            };
            Value emitW = wrap([blk, ctx, subId, lastP](Interpreter& I2, ValueList& args) {
                try { ValueList one = args; I2.callCallable(blk, one); }
                catch (NextEx&) {}
                catch (LastEx&) {
                    // S-55: `last` closes THIS whenever — its LAST phasers run,
                    // its backlog is dropped, and when no whenever is left the
                    // supply is done.
                    ctx->closeSub(subId);
                    I2.runLastPhasers(lastP, nullptr);
                    if (ctx->pending > 0) ctx->pending--;
                    I2.maybeFinishSupply(ctx);
                }
                catch (DoneEx&) {}
                catch (RakuError& e) {
                    // S-57: an exception raised by the whenever BODY is not a
                    // source quit. The QUIT phasers — this whenever's or the
                    // supply block's — do not see it; it ends the supply and
                    // reaches the TAPPER's quit handler (Cro's frame parser
                    // dies per malformed frame and its test reads it there).
                    ctx->closeSub(subId);
                    Value ex = I2.exceptionFor(e);
                    if (ctx->quitCb.t == VT::Code) { ValueList one{ex}; try { I2.callCallable(ctx->quitCb, one); } catch (...) {} }
                    ctx->done = true;
                    if (ctx->tap) I2.closeTapHandle(ctx->tap);
                }
            });
            // every inner tap holds the supply open until its done fires; the
            // done hook runs LAST phasers, then releases this activation's hold
            ctx->pending++;
            Value doneW = wrap([lastP, ctx, subId](Interpreter& I2, ValueList&) {
                ctx->closeSub(subId);
                I2.runLastPhasers(lastP, nullptr);
                if (ctx->pending > 0) ctx->pending--;
                I2.maybeFinishSupply(ctx);
            });
            // S-57: the SOURCE quitting is what a QUIT phaser is for. It works
            // like CATCH — a matching `when`/`default` consumes the quit and
            // this whenever simply counts as done; otherwise the quit travels
            // on to the tapper once the phaser body has run, and ends the
            // supply, because nothing may follow a quit (S-06).
            Value quitW = wrap([quitP, ctx, subId](Interpreter& I2, ValueList& args) {
                ctx->closeSub(subId);
                Value ex = args.empty() ? Value::nil() : args[0];
                Value repl;
                int r = quitP.empty() ? 1 : I2.runQuitPhasers(quitP, ex, repl);
                if (r == 0) {
                    if (ctx->pending > 0) ctx->pending--;
                    I2.maybeFinishSupply(ctx);
                    return;
                }
                Value out = r == 2 ? repl : ex;
                if (ctx->quitCb.t == VT::Code) { ValueList one{out}; try { I2.callCallable(ctx->quitCb, one); } catch (...) {} }
                ctx->done = true;
                if (ctx->tap) I2.closeTapHandle(ctx->tap);
            });
            Value tapV = I.tapSupply(src, emitW, doneW, quitW);
            // closing the outer tap closes this inner one
            if (ctx->tap && tapV.t == VT::Hash && tapV.ext() &&
                tapV.hash()->count("wired") && (*tapV.hash())["wired"].truthy()) {
                auto ih = std::static_pointer_cast<TapHandle>(tapV.ext());
                Interpreter* ip = &I;
                std::lock_guard<std::mutex> lk(ctx->tap->m);
                if (!ctx->tap->closed) ctx->tap->closers.push_back([ip, ih] { ip->closeTapHandle(ih); });
            }
            // …and a plain (Supplier-fed) inner tap is closed the same way:
            // S-54's `done` and S-61's outer close must stop the source feeding
            // this activation, not just stop the body from running.
            else if (ctx->tap && tapV.t == VT::Hash && tapV.hashKind == "Tap" && tapV.hash()) {
                auto rec = tapV.hashS();
                std::lock_guard<std::mutex> lk(ctx->tap->m);
                if (!ctx->tap->closed) ctx->tap->closers.push_back([rec] {
                    (*rec)["closed"] = Value::boolean(true);
                    (*rec)["ended"] = Value::boolean(true);
                });
            }
            return tapV;
        }
        // a `done` in an earlier whenever closes the react — later whenevers don't run
        if (!I.reactStack_.empty()) {
            auto ctx = I.reactStack_.back();
            std::lock_guard<std::mutex> lk(ctx->m);
            if (ctx->closed) return Value::nil();
        }
        // whenever SUPPLY { BLOCK }: tap the supply, running BLOCK for each emitted value
        if (a.size() >= 2 && a.back().t == VT::Code) {
            Value s = a[0], blk = a.back();
            // whenever signal(SIGINT) { … } in a react: wire the OS-signal tap,
            // counting it as a live react source so the loop waits until `done`.
            if (s.t == VT::Hash && s.hashKind == "Supply" &&
                s.hash()->count("kind") && (*s.hash())["kind"].toStr() == "signal") {
                std::vector<int> sigs;
                if (s.hash()->count("signals") && (*s.hash())["signals"].arr())
                    for (auto& n : *(*s.hash())["signals"].arr()) sigs.push_back((int)n.toInt());
                std::shared_ptr<ReactCtx> ctx;
                if (!I.reactStack_.empty()) {
                    ctx = I.reactStack_.back();
                    std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++;
                }
                Value emitW; emitW.t = VT::Code; emitW.setCode(makePayload<Callable>());
                Value blkCopy = blk;
                emitW.code()->builtin = [blkCopy](Interpreter& I2, ValueList& args) -> Value {
                    ValueList one = args;
                    try { return I2.callCallable(blkCopy, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                    return Value::any();
                };
                return I.tapSignal(sigs, I.wrapSupplyChain(s, emitW), Value::nil(), ctx);
            }
            // whenever Supply.interval(N) { … } in a react: a live ticker source —
            // the react waits on it (forever, unless `done`/`last` ends it).
            if (s.t == VT::Hash && s.hashKind == "Supply" &&
                s.hash()->count("kind") && (*s.hash())["kind"].toStr() == "watch") {
                std::shared_ptr<ReactCtx> ctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                return I.spawnIntervalWhenever(watchInterval(s), 0, watchFilter(s, I.wrapSupplyChain(s, blk)), ctx, nullptr);
            }
            if (s.t == VT::Hash && s.hashKind == "Supply" &&
                s.hash()->count("kind") && (*s.hash())["kind"].toStr() == "interval") {
                double iv = s.hash()->count("interval") ? (*s.hash())["interval"].toNum() : 1;
                double dl = s.hash()->count("delay") ? (*s.hash())["delay"].toNum() : 0;
                std::shared_ptr<ReactCtx> ctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                return I.spawnIntervalWhenever(iv, dl, I.wrapSupplyChain(s, blk), ctx, nullptr);
            }
            // whenever $socket.Supply { … } — an async-read/async-listen stream in a
            // react: count it as a live source so the block waits for data, and
            // decrement when the stream ends (connection close) so the react exits.
            if (s.t == VT::Hash && s.hashKind == "Supply" && s.hash()->count("kind")) {
                std::string k = (*s.hash())["kind"].toStr();
                if (k == "async-read" || k == "async-listen" || k == "udp-read") {
                    std::shared_ptr<ReactCtx> ctx;
                    if (!I.reactStack_.empty()) {
                        ctx = I.reactStack_.back();
                        std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++;
                    }
                    Value doneW;
                    if (ctx) {
                        std::weak_ptr<ReactCtx> wctx = ctx;
                        doneW.t = VT::Code; doneW.setCode(makePayload<Callable>());
                        doneW.code()->builtin = [wctx](Interpreter&, ValueList&) -> Value {
                            if (auto c = wctx.lock()) { std::lock_guard<std::mutex> lk(c->m); if (c->liveSources > 0) c->liveSources--; c->cv.notify_all(); }
                            return Value::any();
                        };
                    }
                    return I.tapSupply(s, blk, doneW, Value::nil());
                }
                // A MERGE (zip, zip-latest) with a LIVE source among its inputs
                // subscribes NOW, not after the react body: a `signal()` in it
                // must have its handler in place before a later whenever in the
                // same body runs — Roast's bug-coverage-stress.t child prints
                // 'started' from `whenever Promise.kept` and is sent SIGINT the
                // moment it does. Only list-backed inputs keep the deferred
                // activation below.
                std::function<bool(const Value&)> liveSource = [&](const Value& v) -> bool {
                    if (v.t != VT::Hash || v.hashKind != "Supply" || !v.hash()) return false;
                    if (v.hash()->count("supplier")) return true;
                    auto kt = v.hash()->find("kind");
                    if (kt == v.hash()->end()) return false;
                    const std::string kk = kt->second.toStr();
                    if (kk == "signal" || kk == "interval" || kk == "watch" || kk == "async-read" ||
                        kk == "async-listen" || kk == "udp-read") return true;
                    if (kk == "combine" && v.hash()->count("sources") && (*v.hash())["sources"].arr())
                        for (auto& src : *(*v.hash())["sources"].arr()) if (liveSource(src)) return true;
                    return false;
                };
                if (k == "combine" && liveSource(s)) {
                    std::shared_ptr<ReactCtx> ctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                    // the phasers close over a shim env mirroring each call's
                    // parameters, so `LAST { say $v }` sees the last value
                    auto phEnv = std::make_shared<Env>();
                    phEnv->parent = blk.code() ? blk.code()->closure : nullptr;
                    ValueList lastP, quitP;
                    scanSupplyPhasers(blk, &lastP, &quitP, nullptr, phEnv);
                    std::vector<std::string> pnames;
                    if (blk.code() && blk.code()->params)
                        for (auto& p : *blk.code()->params) if (!p.name.empty()) pnames.push_back(p.name);
                    if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++; }
                    std::weak_ptr<ReactCtx> wctx = ctx;
                    // The subscription is made NOW — a signal() in it has its
                    // handler from this moment — but what it delivers is HELD
                    // until the react body (or the whenever block this runs in)
                    // has finished, as Rakudo delivers it: a list-backed input
                    // emits during the subscribe itself.
                    struct Eager {
                        std::mutex m; bool holding = true, ended = false;
                        std::vector<std::function<void()>> held;
                        std::shared_ptr<TapHandle> handle;
                        std::atomic<bool> released{false};
                    };
                    auto es = std::make_shared<Eager>();
                    auto release = [wctx, es]() {
                        if (es->released.exchange(true)) return;
                        if (auto c = wctx.lock()) {
                            std::lock_guard<std::mutex> lk(c->m);
                            if (c->liveSources > 0) c->liveSources--;
                            c->cv.notify_all();
                        }
                    };
                    auto endSub = [es]() -> bool {   // true the first time only
                        std::lock_guard<std::mutex> lk(es->m);
                        if (es->ended) return false;
                        es->ended = true;
                        return true;
                    };
                    auto gate = [es](std::function<void()> f) {
                        {   std::lock_guard<std::mutex> lk(es->m);
                            if (es->ended) return;
                            if (es->holding) { es->held.push_back(std::move(f)); return; }
                        }
                        f();
                    };
                    auto closeTap = [es](Interpreter& I2) { if (es->handle) I2.closeTapHandle(es->handle); };
                    Value blkCopy = blk;
                    auto onEmit = [blkCopy, wctx, es, phEnv, pnames, lastP, release, endSub, closeTap](Interpreter& I2, ValueList args) {
                        { std::lock_guard<std::mutex> lk(es->m); if (es->ended) return; }
                        auto c = wctx.lock();
                        if (c) { std::lock_guard<std::mutex> lk(c->m); if (c->closed) return; }   // after `done`, nothing more
                        struct Push {
                            Interpreter& I; bool on;
                            ~Push() { if (on && !I.reactStack_.empty()) I.reactStack_.pop_back(); }
                        } push{I2, (bool)c};
                        if (c) I2.reactStack_.push_back(c);
                        for (size_t i = 0; i < pnames.size(); i++)
                            phEnv->define(pnames[i], i < args.size() ? args[i] : Value::any());
                        if (!args.empty()) phEnv->define("$_", args[0]);
                        try { I2.callCallable(blkCopy, args); }
                        catch (NextEx&) {}
                        catch (DoneEx&) {}
                        catch (LastEx&) {        // `last`: this subscription ends, with its LAST
                            if (endSub()) { closeTap(I2); I2.runLastPhasers(lastP, c); release(); }
                        }
                        catch (RakuError& e) {   // a die in the block: the react dies with it
                            if (endSub()) {
                                if (c) {
                                    std::lock_guard<std::mutex> lk(c->m);
                                    if (!c->quitFlag) { c->quitFlag = true; c->quitErr = e.payload.t == VT::Nil ? Value::str(e.message) : e.payload; }
                                    c->closed = true; c->cv.notify_all();
                                }
                                closeTap(I2); release();
                            }
                        }
                    };
                    Value emitW; emitW.t = VT::Code; emitW.setCode(makePayload<Callable>());
                    emitW.code()->builtin = [gate, onEmit](Interpreter& I2, ValueList& args) -> Value {
                        ValueList a = args;
                        Interpreter* ip = &I2;
                        gate([ip, onEmit, a] { onEmit(*ip, a); });
                        return Value::any();
                    };
                    // the block's LAST and QUIT phasers are this subscription's
                    // done and quit (`LAST { done }` ends the react)
                    Value doneW; doneW.t = VT::Code; doneW.setCode(makePayload<Callable>());
                    doneW.code()->builtin = [gate, lastP, release, wctx, endSub](Interpreter& I2, ValueList&) -> Value {
                        Interpreter* ip = &I2;
                        gate([ip, lastP, release, wctx, endSub] {
                            if (endSub()) { ip->runLastPhasers(lastP, wctx.lock()); release(); }
                        });
                        return Value::any();
                    };
                    Value quitW; quitW.t = VT::Code; quitW.setCode(makePayload<Callable>());
                    quitW.code()->builtin = [gate, wctx, quitP, release, endSub](Interpreter& I2, ValueList& a) -> Value {
                        Interpreter* ip = &I2;
                        ValueList args = a;
                        gate([ip, wctx, quitP, release, endSub, args] {
                            if (!endSub()) return;
                            auto c = wctx.lock();
                            if (c) ip->reactStack_.push_back(c);
                            for (auto& p : quitP) { ValueList one = args; try { ip->callCallable(p, one); } catch (...) {} }
                            if (c) ip->reactStack_.pop_back();
                            if (c && quitP.empty()) {   // unhandled: fatal to the react
                                std::lock_guard<std::mutex> lk(c->m);
                                if (!c->quitFlag) { c->quitFlag = true; c->quitErr = args.empty() ? Value::str("quit") : args[0]; }
                                c->closed = true; c->cv.notify_all();
                            }
                            release();
                        });
                        return Value::any();
                    };
                    Value tap = I.tapSupply(s, emitW, doneW, quitW);
                    if (tap.t == VT::Hash && tap.ext() && tap.hash() && tap.hash()->count("wired"))
                        es->handle = std::static_pointer_cast<TapHandle>(tap.ext());
                    // what the subscribe delivered (and whatever arrives until the
                    // body is over) goes out afterwards, in order
                    auto flush = [es]() {
                        for (;;) {
                            std::vector<std::function<void()>> items;
                            {   std::lock_guard<std::mutex> lk(es->m);
                                if (es->held.empty()) { es->holding = false; return; }
                                items.swap(es->held);
                            }
                            for (auto& f : items) f();
                        }
                    };
                    if (ctx) {
                        std::lock_guard<std::mutex> lk(ctx->m);
                        if (es->handle) ctx->extTaps.push_back(es->handle);   // closed with the react
                        ctx->deferred.push_back(flush);
                        ctx->cv.notify_all();
                    }
                    else flush();
                    return tap;
                }
            }
            if (s.t == VT::Hash && s.hashKind == "Supply") {
                if (s.hash()->count("supplier")) {
                    // live supply: register a tap; count it as a react source so the
                    // enclosing react blocks until this supplier signals done.
                    //
                    // The block's LAST/QUIT phasers ARE this tap's done/quit
                    // handlers — `$supplier.done`/`.quit` fan out to them. Without
                    // them a LAST never fired and, worse, `.quit` left the react
                    // source live: the whole react waited forever on a supply that
                    // had already failed. The phasers close over a shim env that
                    // mirrors each call's parameter bindings, so `LAST { say $c }`
                    // sees the last value (same trick as the from-list path below).
                    auto phEnv = std::make_shared<Env>();
                    phEnv->parent = blk.code() ? blk.code()->closure : nullptr;
                    ValueList lastP, quitP;
                    scanSupplyPhasers(blk, &lastP, &quitP, nullptr, phEnv);
                    std::vector<std::string> pnames;
                    if (blk.code() && blk.code()->params)
                        for (auto& p : *blk.code()->params) if (!p.name.empty()) pnames.push_back(p.name);
                    Value blkInner = blk, shim; shim.t = VT::Code; shim.setCode(makePayload<Callable>());
                    shim.code()->builtin = [blkInner, phEnv, pnames](Interpreter& I2, ValueList& args) -> Value {
                        for (size_t i = 0; i < pnames.size(); i++)
                            phEnv->define(pnames[i], i < args.size() ? args[i] : Value::any());
                        if (!args.empty()) phEnv->define("$_", args[0]);
                        ValueList one = args;
                        return I2.callCallable(blkInner, one); // control exceptions reach the tap loop
                    };
                    Value tapRec = Value::makeHash();
                    (*tapRec.hash())["emit"] = lastP.empty() ? blk : shim; // the shim costs a frame: only when a LAST needs it
                    // Carry the Supply's transform chain (head/grep/map/…) onto the tap,
                    // each step with its OWN fresh state — same as tapSupply's live branch.
                    // Without it `whenever $s.Supply.head(1)` never limits and, worse,
                    // never reports completion, so the enclosing react waits forever.
                    if (s.hash()->count("chain")) {
                        Value chain = Value::array();
                        for (auto& step : *(*s.hash())["chain"].arr()) {
                            Value s2 = Value::makeHash(); *s2.hash() = *step.hash();
                            { Value st0 = Value::makeHash();
          (*st0.hash())["t0"] = Value::number(epochNowSecs());   // when this subscription began
          (*s2.hash())["state"] = st0; }
                            chain.arr()->push_back(s2);
                        }
                        (*tapRec.hash())["chain"] = chain;
                    }
                    std::shared_ptr<ReactCtx> rctx;
                    if (!I.reactStack_.empty()) {
                        rctx = I.reactStack_.back();
                        tapRec.extM() = rctx;
                        { std::lock_guard<std::mutex> lk(rctx->m); rctx->liveSources++; }
                    }
                    if (!lastP.empty()) {
                        Value doneW; doneW.t = VT::Code; doneW.setCode(makePayload<Callable>());
                        doneW.code()->builtin = [lastP](Interpreter& I2, ValueList&) -> Value {
                            I2.runLastPhasers(lastP, nullptr);
                            return Value::any();
                        };
                        (*tapRec.hash())["done"] = doneW;
                    }
                    {   // the supply quitting ends THIS subscription: a QUIT phaser
                        // handles it (like a CATCH), and with no QUIT phaser the
                        // exception is fatal to the whole react, as in Rakudo.
                        std::weak_ptr<ReactCtx> wctx = rctx;
                        auto th = tapRec.hashS(); // shared: the quit lambda can fire after tapRec's last Value copy dies
                        Value quitW; quitW.t = VT::Code; quitW.setCode(makePayload<Callable>());
                        quitW.code()->builtin = [quitP, wctx, th](Interpreter& I2, ValueList& a) -> Value {
                            (*th)["closed"] = Value::boolean(true);
                            auto c = wctx.lock();
                            // run the phasers under the react ctx: `done` inside a
                            // QUIT block has to find the react it belongs to
                            if (c) I2.reactStack_.push_back(c);
                            for (auto& p : quitP) { ValueList one = a; try { I2.callCallable(p, one); } catch (...) {} }
                            if (c) I2.reactStack_.pop_back();
                            if (c) {
                                std::lock_guard<std::mutex> lk(c->m);
                                if (quitP.empty() && !c->quitFlag) { // nothing handled it
                                    c->quitFlag = true;
                                    c->quitErr = a.empty() ? Value::str("quit") : a[0];
                                    c->closed = true;
                                }
                                if (c->liveSources > 0) c->liveSources--;
                                c->cv.notify_all();
                            }
                            return Value::any();
                        };
                        (*tapRec.hash())["quit"] = quitW;
                    }
                    Value sup = (*s.hash())["supplier"];
                    // an `.on-close` hook on the supply belongs to each of its taps
                    if (s.hash()->count("closers")) (*tapRec.hash())["closers"] = (*s.hash())["closers"];
                    // S-09: registration and the replay of what a
                    // Supplier::Preserving kept are ONE step. Between them a
                    // producer on another thread would slip a live value in front
                    // of the replay, and the tap would see the stream out of
                    // order (Roast supplier-preserving.t emits from a `start`).
                    if (sup.t == VT::Hash && sup.hash()->count("taps")) {
                        std::lock_guard<std::recursive_mutex> regLk(supplierMutex(sup.hash()));
                        (*sup.hash())["taps"].arr()->push_back(tapRec);
                        I.replayPreserved(sup, tapRec);
                    }
                    // …and when the react is over, this tap is closed: its
                    // `on-close` hooks run, and the source stops feeding it.
                    if (rctx) {
                        auto rec = tapRec.hashS();
                        Value closeCb; closeCb.t = VT::Code; closeCb.setCode(makePayload<Callable>());
                        closeCb.code()->builtin = [rec, sup](Interpreter& I2, ValueList&) -> Value {
                            // its own flag: `done` inside the whenever marks the
                            // tap `closed` from the emit fan-out, and the hooks
                            // would then never run
                            ValueList hooks;
                            {   // under the supplier's lock, as the fan-out reads
                                // these keys: a producer on another thread may be
                                // emitting this very moment, and an unlocked insert
                                // into the record let it miss `closed` and deliver
                                // past `done` (Roast supplier-preserving.t)
                                std::unique_lock<std::recursive_mutex> lk;
                                if (sup.t == VT::Hash && sup.hash())
                                    lk = std::unique_lock<std::recursive_mutex>(supplierMutex(sup.hash()));
                                if ((*rec)["on-closed"].truthy()) return Value::any();
                                (*rec)["on-closed"] = Value::boolean(true);
                                (*rec)["closed"] = Value::boolean(true);
                                (*rec)["ended"] = Value::boolean(true);
                                auto cit = rec->find("closers");
                                if (cit != rec->end() && cit->second.arr()) hooks = *cit->second.arr();
                            }
                            for (auto& h : hooks)
                                if (h.t == VT::Code) { ValueList na; try { I2.callCallable(h, na); } catch (...) {} }
                            return Value::any();
                        };
                        std::lock_guard<std::mutex> lk(rctx->m);
                        rctx->closers.push_back(closeCb);
                    }
                    // The supplier already signalled done before this tap registered
                    // (eager worker ran first): close the tap now, so runReactLoop
                    // doesn't wait on a source that will never complete.
                    if (sup.t == VT::Hash && sup.hash()->count("done_state") &&
                        (*sup.hash())["done_state"].truthy() && tapRec.ext()) {
                        auto ctx = std::static_pointer_cast<ReactCtx>(tapRec.ext());
                        std::lock_guard<std::mutex> lk(ctx->m);
                        if (ctx->liveSources > 0) ctx->liveSources--;
                        ctx->cv.notify_all();
                    }
                    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
                }
                if (s.hash()->count("block")) {
                    // on-demand supply in a react: tap it with a quit hook that runs
                    // the whenever's QUIT phasers, and — with no QUIT phaser — fails
                    // the react, the quit being fatal (Cro::TCP's `whenever
                    // Connector.establish(...)` on a dead port). A `supply {…}` block
                    // that dies quits its tap (see tapSupply), so this is the path
                    // `whenever $producer { QUIT {…} }` arrives on.
                    std::shared_ptr<ReactCtx> rctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                    ValueList lastP, quitP;
                    scanSupplyPhasers(blk, &lastP, &quitP, nullptr);
                    Value emitW; emitW.t = VT::Code; emitW.setCode(makePayload<Callable>());
                    Value blkCopy = blk;
                    std::weak_ptr<ReactCtx> bodyCtx = rctx;
                    emitW.code()->builtin = [blkCopy, bodyCtx](Interpreter& I2, ValueList& args) -> Value {
                        // The body belongs to the REACT, though the value arrives
                        // inside the supply's emit (on whatever thread fed it): its
                        // `done` ends the react, not the supply delivering to it.
                        auto c = bodyCtx.lock();
                        decltype(I2.tctx_.tapStack) delivering;
                        delivering.swap(I2.tctx_.tapStack);
                        if (c) I2.reactStack_.push_back(c);
                        struct Restore {
                            Interpreter& I; decltype(I2.tctx_.tapStack)& d; bool pushed;
                            ~Restore() { if (pushed) I.reactStack_.pop_back(); I.tctx_.tapStack.swap(d); }
                        } restore{I2, delivering, (bool)c};
                        // …and a react that closes ends the activation delivering
                        // to it: its CLOSE phasers run and its next emit unwinds
                        // it (Roast syntax.t: a supply looping on `emit` until
                        // CLOSE says stop)
                        auto endDelivering = [&] {
                            if (!c || delivering.empty()) return;
                            { std::lock_guard<std::mutex> lk(c->m); if (!c->closed) return; }
                            auto src = delivering.back();
                            src->done = true;
                            src->clearQueue();
                            src->doneFired = true;
                            I2.closeTapHandle(src->tap);
                        };
                        ValueList one = args;
                        Value r = Value::any();
                        try { r = I2.callCallable(blkCopy, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                        endDelivering();
                        return r;
                    };
                    // The producer is a live react source until it says done or
                    // quits: an on-demand block whose own whenevers keep ticking
                    // outlives the react body, and without this the react returned
                    // at once and the program raced its own producer. `released`
                    // keeps the count balanced if done and quit both arrive.
                    auto released = std::make_shared<std::atomic<bool>>(false);
                    if (rctx) { std::lock_guard<std::mutex> lk(rctx->m); rctx->liveSources++; }
                    std::weak_ptr<ReactCtx> wctx = rctx;
                    auto release = [wctx, released]() {
                        if (released->exchange(true)) return;
                        if (auto c = wctx.lock()) {
                            std::lock_guard<std::mutex> lk(c->m);
                            if (c->liveSources > 0) c->liveSources--;
                            c->cv.notify_all();
                        }
                    };
                    Value quitW; quitW.t = VT::Code; quitW.setCode(makePayload<Callable>());
                    quitW.code()->builtin = [wctx, quitP, release](Interpreter& I2, ValueList& a) -> Value {
                        auto c = wctx.lock();
                        if (c) I2.reactStack_.push_back(c); // `done` in a QUIT block finds its react
                        for (auto& p : quitP) { ValueList one = a; try { I2.callCallable(p, one); } catch (...) {} }
                        if (c) I2.reactStack_.pop_back();
                        if (c && quitP.empty()) {   // unhandled: fatal to the react
                            std::lock_guard<std::mutex> lk(c->m);
                            if (!c->quitFlag) { c->quitFlag = true; c->quitErr = a.empty() ? Value::str("quit") : a[0]; }
                            c->closed = true; c->cv.notify_all();
                        }
                        release();
                        return Value::any();
                    };
                    Value doneW; doneW.t = VT::Code; doneW.setCode(makePayload<Callable>());
                    // rctx captured so a `done` inside a LAST phaser finds its
                    // react: this runs on the source's worker, where nothing has
                    // pushed it. (Log::Timeline's client waits on `LAST done`
                    // after the server hangs up, not on its timeout.)
                    doneW.code()->builtin = [lastP, release, rctx](Interpreter& I2, ValueList&) -> Value {
                        I2.runLastPhasers(lastP, rctx);
                        release();
                        return Value::any();
                    };
                    // the subscription ends with the react: `done` closes the tap,
                    // and the supply's CLOSE phasers run then (Cro's runner kills
                    // the services it started from one, on Ctrl-C)
                    Value tap = I.tapSupply(s, emitW, doneW, quitW);
                    if (rctx && tap.ext()) {
                        std::lock_guard<std::mutex> lk(rctx->m);
                        rctx->extTaps.push_back(std::static_pointer_cast<TapHandle>(tap.ext()));
                    }
                    return tap;
                }
                {   // from-list: drain AFTER the react body (deferred activation,
                    // issue #18); LAST phasers fire when the list is exhausted or
                    // a `last` ends the subscription. A die in the body escapes
                    // the drain and kills the react, as in Rakudo.
                    std::shared_ptr<ReactCtx> rctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                    // the phasers close over a shim env that mirrors each
                    // call's parameter bindings, so `LAST { "Done with $c" }`
                    // sees the LAST value of $c (Rakudo semantics)
                    auto phEnv = std::make_shared<Env>();
                    phEnv->parent = blk.code() ? blk.code()->closure : nullptr;
                    ValueList lastP, quitP;
                    scanSupplyPhasers(blk, &lastP, &quitP, nullptr, phEnv);
                    std::vector<std::string> pnames;
                    if (blk.code() && blk.code()->params)
                        for (auto& p : *blk.code()->params) if (!p.name.empty()) pnames.push_back(p.name);
                    Value blkInner = blk;
                    Value shim; shim.t = VT::Code; shim.setCode(makePayload<Callable>());
                    shim.code()->builtin = [blkInner, phEnv, pnames](Interpreter& I2, ValueList& args) -> Value {
                        for (size_t i = 0; i < pnames.size(); i++)
                            phEnv->define(pnames[i], i < args.size() ? args[i] : Value::any());
                        if (!args.empty()) phEnv->define("$_", args[0]);
                        ValueList one = args;
                        return I2.callCallable(blkInner, one); // control exceptions reach the tap loop
                    };
                    Interpreter* self = &I;
                    Value sCopy = s, blkCopy = shim;
                    auto drain = [self, sCopy, blkCopy, lastP, quitP, rctx]() {
                        if (rctx) { std::lock_guard<std::mutex> lk(rctx->m); if (rctx->closed) return; }
                        if (rctx) self->reactStack_.push_back(rctx);
                        // S-57: the source QUITTING is what this whenever's QUIT
                        // phasers are for — hand them the tap's quit rather than
                        // letting it unwind the react (Roast syntax.t's
                        // `whenever Supply.from-list(gather { die })`).
                        Value quitCb; quitCb.t = VT::Code; quitCb.setCode(makePayload<Callable>());
                        quitCb.code()->builtin = [quitP, rctx](Interpreter& I2, ValueList& a) -> Value {
                            Value ex = a.empty() ? Value::any() : a[0];
                            Value repl;
                            int r = quitP.empty() ? 1 : I2.runQuitPhasers(quitP, ex, repl);
                            if (r == 0) return Value::any();          // a when/default consumed it
                            Value out = r == 2 ? repl : ex;
                            if (rctx) {
                                std::lock_guard<std::mutex> lk(rctx->m);
                                if (!rctx->quitFlag) { rctx->quitFlag = true; rctx->quitErr = out; }
                                rctx->closed = true; rctx->cv.notify_all();
                            }
                            return Value::any();
                        };
                        try {
                            Value sv = sCopy;
                            ValueList ta{blkCopy, Value::pair("quit", quitCb)};
                            ta[1].namedArg = true;
                            self->methodCall(sv, "tap", ta);
                        }
                        catch (...) { if (rctx) self->reactStack_.pop_back(); throw; }
                        if (rctx) self->reactStack_.pop_back();
                        self->runLastPhasers(lastP, rctx);
                    };
                    // A Proc::Async stream registers EAGERLY. Tapping it only records
                    // the callback — nothing is emitted until the process runs — and
                    // the process runs inside the sibling `whenever $proc.start`,
                    // which is part of the same react body. Deferring the
                    // registration until after that body put it after the run, so
                    // the output had already been fed to an empty tap list and the
                    // block never fired.
                    bool procStream = s.t == VT::Hash && s.hashKind == "Supply" && s.hash() &&
                                      s.hash()->count("proc");
                    if (rctx && !procStream) {
                        { std::lock_guard<std::mutex> lk(rctx->m); rctx->deferred.push_back(drain); rctx->cv.notify_all(); }
                        Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
                    }
                    drain(); // no react ctx (bare whenever in a plain block): keep the eager order
                    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
                }
            }
            // `whenever $channel { … }` — one run per received value, completing when
            // the channel closes (Log::Async's tests pump their output through one)
            if (s.t == VT::Hash && s.hashKind == "Channel") {
                std::shared_ptr<ReactCtx> ctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                return I.spawnChannelWhenever(s, blk, ctx);
            }
            // whenever Promise.in(N) { … } — a timer: fire once after the real delay
            // as a react source, so it doesn't defeat a timeout guard by firing at t=0.
            if (s.t == VT::Hash && s.hashKind == "Promise" &&
                s.hash()->count("kind") && (*s.hash())["kind"].toStr() == "timer") {
                std::shared_ptr<ReactCtx> ctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                return I.spawnTimerWhenever(timerRemainingSecs(s), blk, ctx);
            }
            // whenever $proc.ready { … } — fires once with the PID if the process
            // has already run, and does NOT start it (that is `.start`'s job).
            if (s.t == VT::Hash && s.hashKind == "Promise" &&
                s.hash()->count("kind") && (*s.hash())["kind"].toStr() == "proc-ready") {
                Value pidv = Value::nil();
                if (s.hash()->count("proc") && (*s.hash())["proc"].hash()) {
                    auto& ph = *(*s.hash())["proc"].hash();
                    auto it = ph.find("pid"); if (it != ph.end()) pidv = it->second;
                }
                ValueList one{pidv};
                try { I.callCallable(blk, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            // whenever $proc.start { … } — a lazy Proc::Async promise: the process
            // runs when the promise is realized (await does the same); run it NOW,
            // then fire the block once with the finished proc (its .so/.exitcode
            // reflect the exit status — zef's curl/wget fetch checks `$_.so`).
            if (s.t == VT::Hash && s.hashKind == "Promise" &&
                s.hash()->count("kind") && (*s.hash())["kind"].toStr() == "proc") {
                I.runProcPromise(s, 0);
                Value procv = s.hash()->count("proc") ? (*s.hash())["proc"] : s;
                if (procv.hashKind == "Proc::Async") procv.hashKind = "Proc"; // the block sees a Proc, as await answers
                ValueList one{procv};
                try { I.callCallable(blk, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            // whenever Promise.allof($p1.start, $p2.start) / .anyof(…) — a
            // combinator over lazy process promises: realize it the way `await`
            // does (every process runs, its output reaching the taps already
            // registered), then fire once with the combinator
            if (s.t == VT::Hash && s.hashKind == "Promise" && !s.ext() &&
                s.hash()->count("kind") &&
                ((*s.hash())["kind"].toStr() == "allof" || (*s.hash())["kind"].toStr() == "anyof")) {
                ValueList aw{s};
                I.callBuiltin("await", aw);
                ValueList one{s};
                try { I.callCallable(blk, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            // whenever over a SETTLED Promise binds the block to its RESULT, not the
            // promise object. (An unkept one still fires immediately with the object —
            // the full async react registration is still an open item.)
            if (s.t == VT::Hash && s.hashKind == "Promise" && s.ext()) {
                auto ps = std::static_pointer_cast<PromiseState>(s.ext());
                bool done, broken; Value cause; std::string causeMsg;
                { std::lock_guard<std::mutex> lk(ps->m);
                  done = ps->done; broken = ps->broken; cause = ps->cause; causeMsg = ps->causeMsg; }
                if (done) {
                    if (broken) {
                        // a BROKEN promise quits the whenever: a QUIT phaser in the
                        // block handles it, otherwise the react itself dies with the
                        // cause (a refused .connect must fail the react, not run the
                        // block with Any — Cro::TCP's dies-ok relies on it)
                        Value ex = causeException(I, cause, causeMsg);
                        ValueList quitP;
                        scanSupplyPhasers(blk, nullptr, &quitP, nullptr);
                        // S-57: only a QUIT phaser whose when/default MATCHES consumes
                        // the break. A bare one runs and the react still dies with the
                        // cause — merely HAVING a phaser used to swallow it.
                        Value repl;
                        int r = quitP.empty() ? 1 : I.runQuitPhasers(quitP, ex, repl);
                        if (r == 0) { Value t = Value::makeHash(); t.hashKind = "Tap"; return t; }
                        throw RakuError{r == 2 ? repl : ex,
                                        causeMsg.empty() ? std::string("Promise broken") : causeMsg};
                    }
                    ValueList one{ps->result}; return I.callCallable(blk, one);
                }
                // UNKEPT promise in a react: REGISTER — fire the block once with the
                // result when it settles, counted as a live source. This is the
                // standard shutdown idiom (`whenever $kill { done }`); firing
                // immediately with the promise OBJECT ran `done` at registration
                // and tore the react down before its other whenevers wired up.
                if (!I.reactStack_.empty()) {
                    auto rctx = I.reactStack_.back();
                    { std::lock_guard<std::mutex> lk(rctx->m); rctx->liveSources++; }
                    ValueList quitP;
                    scanSupplyPhasers(blk, nullptr, &quitP, nullptr);
                    Interpreter* self = &I;
                    Value blkCopy = blk;
                    std::function<void()> fire = [self, blkCopy, ps, rctx, quitP]() {
                        // runs under the GIL, from the settler's thread (via ps->thens)
                        self->reactStack_.push_back(rctx);
                        if (!rctx->closed) {
                            if (ps->broken) {
                                // S-57 again: only a QUIT phaser that MATCHES consumes
                                // the break. A bare one runs and the quit still ends
                                // the react.
                                Value ex = causeException(*self, ps->cause, ps->causeMsg);
                                Value repl;
                                int r = quitP.empty() ? 1 : self->runQuitPhasers(quitP, ex, repl);
                                if (r != 0) {
                                    Value out = r == 2 ? repl : ex;
                                    std::lock_guard<std::mutex> lk(rctx->m);
                                    if (!rctx->quitFlag) { rctx->quitFlag = true; rctx->quitErr = out; }
                                    rctx->closed = true; rctx->cv.notify_all();
                                }
                            } else {
                                ValueList one{ps->result};
                                try { self->callCallable(blkCopy, one); }
                                catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                                catch (RakuError& e) {
                                    // a body that dies ends the react and rethrows
                                    // at the `react`, exactly as the broken-promise
                                    // arm above does. Ahead of catch(...), which
                                    // used to swallow it (`whenever start {…}`).
                                    Value ex = self->exceptionFor(e);
                                    std::lock_guard<std::mutex> lk(rctx->m);
                                    if (!rctx->quitFlag) { rctx->quitFlag = true; rctx->quitErr = ex; }
                                    rctx->closed = true; rctx->cv.notify_all();
                                }
                                catch (...) {}
                            }
                        }
                        self->reactStack_.pop_back();
                        { std::lock_guard<std::mutex> lk(rctx->m); if (rctx->liveSources > 0) rctx->liveSources--; rctx->cv.notify_all(); }
                    };
                    bool now = false;
                    { std::lock_guard<std::mutex> lk(ps->m); if (ps->done) now = true; else ps->thens.push_back(fire); }
                    if (now) fire();
                    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
                }
            }
            // S-60: any other value is coerced with Supply() — an Iterable is one
            // event per element, anything else one event — and then tapped, so
            // the react's own ordering rules apply to it like any other source.
            {
                Value coerced = coerceToSupply(s);
                ValueList a2{coerced, blk};
                return I.callBuiltin("whenever", a2);
            }
        }
        return Value::nil();
    };
    B["sleep"] = [](Interpreter& I, ValueList& a) -> Value {
        I.sleepYield(a.empty() ? 0 : a[0].toNum());  // GIL-released, full duration (see sleepYield)
        return Value::nil(); // sleep returns Nil (roast: `$nil = sleep(…); $nil === Nil`)
    };
    registerBuiltinsPart5();
}

} // namespace rakupp
