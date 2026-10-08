// BuiltinsRegister.cpp — registerBuiltins, first two pieces: test functions, EVAL, atomics, gather/take, I/O subs
//
// One of the parts BuiltinsParts.h lists; what they share is declared there.
#include "BuiltinsParts.h"

namespace rakupp {
Value openStdStream(Interpreter& I, const Value& dash, const ValueList& args);   // MethodCallPart3.cpp: open('-')

// The child's stdin for a `:in($handle)` adverb. Rakudo hands the child the
// handle's OWN descriptor, so a child that inspects its stdin sees the plain
// file — macOS `script` does a tcgetattr on it, `test -f /dev/stdin` holds —
// and reads the file's bytes. rakupp's file handle carries a path rather than
// a descriptor, so the path is opened here (from the start: the reader keeps a
// line cursor, not a byte offset); an in-memory handle — a captured Proc.out
// or .err, $*ARGFILES — is spooled to an unlinked temp file; a descriptor-
// backed one (nqp::open's, a socket's) is dup'd. Returns the descriptor the
// child dup2s onto 0 — the caller closes it after the spawn — or -1 for
// `:in($*IN)`, which is inherit. `resolved` says whether the value was a
// usable handle at all; anything else keeps its old meaning (a Bool).
// Before this, `run` read ANY `:in` value as the Bool that selects the
// deferred piped mode and `shell` ignored `:in` outright, so the child got OUR
// stdin: Roast's Test::Util run-with-tty passes a file handle precisely so that
// `script` sees a plain fd, and under a harness whose stdin is a pipe or a
// socket S32-io/out-buffering.t's "prompt does not hang" either lost its
// input or died in tcgetattr — a flap in every Roast sweep.
static int stdinFdForHandle(const Value& h, bool& resolved) {
    resolved = false;
#if defined(_WIN32)
    (void)h;
    return -1;
#else
    if (h.t != VT::Hash || !h.hash()) return -1;
    const auto& fh = *h.hash();
    auto field = [&](const char* k) -> const Value* {
        auto it = fh.find(k); return it != fh.end() ? &it->second : nullptr;
    };
    if (h.hashKind == "FileHandle") {
        if (const Value* std_ = field("std")) { resolved = std_->toStr() == "in"; return -1; }
        // `:in($p.out)` of a child running over live pipes: this child reads
        // that one's output straight from its pipe — or, once part of it was
        // read here, the rest of it from a file
        if (const Value* lt = field("live-tok")) {
            const Value* le = field("live-err");
            const bool isErr = le && le->truthy();
            int fd = liveProcTakeFd(lt->toInt(), isErr);
            if (fd >= 0) { resolved = true; return fd; }
            std::string rest = liveProcReadAll(nullptr, lt->toInt(), isErr);
            Value& buf = (*h.hash())["buffer"];
            buf = Value::str((buf.t == VT::Str ? buf.s : std::string()) + rest);
            h.hash()->erase("live-tok");
            (*h.hash())["captured"] = Value::boolean(true);
            return stdinFdForHandle(h, resolved);
        }
        if (const Value* path = field("path")) {
            if (path->toStr().empty()) return -1;
            int fd = ::open(path->toStr().c_str(), O_RDONLY);
            if (fd < 0) return -1;
            resolved = true;
            return fd;
        }
        if (const Value* buf = field("buffer")) {
            std::string tmpl = tmpDirPath();
            if (tmpl.back() != '/') tmpl += '/';
            tmpl += "rakupp-stdin-XXXXXX";
            std::vector<char> name(tmpl.begin(), tmpl.end()); name.push_back('\0');
            int fd = ::mkstemp(name.data());
            if (fd < 0) return -1;
            ::unlink(name.data());
            const std::string content = buf->toStr();
            size_t off = 0;
            while (off < content.size()) {
                ssize_t n = ::write(fd, content.data() + off, content.size() - off);
                if (n <= 0) break;
                off += (size_t)n;
            }
            ::lseek(fd, 0, SEEK_SET);
            resolved = true;
            return fd;
        }
        return -1;
    }
    if (const Value* fdv = field("fd")) {
        int fd = ::dup((int)fdv->toInt());
        if (fd < 0) return -1;
        resolved = true;
        return fd;
    }
    return -1;
#endif
}

// `+values`: ONE non-itemized Iterable argument IS the list; anything else is
// an element, and an ITEMIZED list counts as one element rather than
// flattening. `skip(5, @a)` skips the array's elements while `skip(5, $a)`
// skips over the single item the `$` holds (Nil-Any sheet NA-49; roast
// S32-list/skip.t and head.t assert both readings side by side).
static Value slurpyValues(const ValueList& vs) {
    if (vs.size() == 1 && !vs[0].itemized &&
        (vs[0].t == VT::Array || vs[0].t == VT::Range))
        return vs[0];
    Value l = Value::array(); l.isList = true;
    *l.arr() = vs;
    return l;
}

// ---------------- named builtins ----------------
// Test helpers: pull a `:todo`/`:skip` directive and the description out of trailing args.
static std::string testDirective(const ValueList& a) {
    for (auto& x : a) if (x.t == VT::Pair && (x.s == "todo" || x.s == "skip")) {
        std::string why = x.pairVal() ? x.pairVal()->toStr() : "";
        std::string kind = x.s == "todo" ? "TODO" : "SKIP";
        return (why.empty() || why == "1" || why == "True") ? kind : kind + " " + why;
    }
    return "";
}
static std::string testDesc(const ValueList& a, size_t from) {
    for (size_t i = from; i < a.size(); i++) if (a[i].t != VT::Pair) return a[i].toStr();
    return "";
}

// `prompt` proper, shared by the builtin and by the `rakupp-prompt-hidden`
// probe so the two cannot drift apart. `forceHidden` is the probe's entry.
static Value promptImpl(ValueList& a, bool hidden) {
    const Value* msg = nullptr;
    for (const Value& v : a) {
        if (v.t == VT::Pair && v.namedArg) continue; // the callers rule on these
        if (!msg) msg = &v;
    }
    if (msg) { std::cout << msg->toStr(); std::cout.flush(); }
    std::string line;
    if (!hidden) {
        if (!std::getline(std::cin, line)) return Value::nil(); // EOF -> Nil
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Raku returns Str-with-val: numeric input is an allomorph
        return rakupp::valAllomorph(Value::str(line));
    }
    bool echoed = true;
    if (!readHiddenLine(line, echoed)) return Value::nil();
    // The Enter that ended the line was not echoed either, so the cursor is
    // still sitting after the prompt. Without this the next line of output is
    // written onto it.
    if (!echoed) { std::cout << "\n"; std::cout.flush(); }
    return Value::str(line);
}

void Interpreter::registerBuiltins() {
    auto& B = builtins_;

    // A variable's USER trait, called by the parser's desugaring right after the
    // declaration (`my $a is noted` — see Interpreter::applyVarTrait). The name
    // starts with \x01, so no program can spell it.
    B["\x01var-trait"] = [](Interpreter& I, ValueList& a) -> Value {
        return a.size() >= 2 ? I.applyVarTrait(a[0].toStr(), a[1].toStr(), a.size() > 2 ? &a[2] : nullptr)
                             : Value::nil();
    };

    // Native extension loading (include/rakupp/rakupp_ext.h). A BUILTIN rather than
    // something `use Rakupp::Ext` installs, because a module that wants a
    // compiled fast path with a portable fallback has to ask for it WITHOUT
    // writing anything Rakudo cannot compile:
    //
    //     my &load = try &::('rakupp-ext-load');   # Nil on Rakudo, sub here
    //
    // `&::(…)` is a runtime lookup, so that line compiles on both engines and
    // the module degrades to its pure-Raku path everywhere else. Reachable via
    // `use Rakupp::Ext` too, which is the discoverable spelling for code that is
    // rakupp-only by design.
    // `rakupp-sha1-hex` USED to be registered here, uppercase, for the
    // installer's short/ index keys — hashing a short string by spawning
    // `shasum` cost a subprocess per key and made `rakupp uninstall fez` look
    // hung. It is now the `digest` tag's primitive (DATA-PLAN P3), registered
    // below and LOWERCASE like every other `-hex` name in the ecosystem, which
    // is what DATA-PLAN asked for rather than having the tag work around it.
    // the installer `.uc`s the answer itself and always did.
    //
    // Two registrations of one name was the real hazard: the later one simply
    // replaced the earlier, so which SHA-1 a program got depended on the order
    // of two blocks in this file.

    // ---- the `json` tag's primitives (DATA-PLAN P1) -------------------------
    //
    // The same codec the JSON::Fast wrapper runs, reached by name instead of by
    // loading a module. What differs is only the policy for a case the codec
    // does not cover: the wrapper hands it back to JSON::Fast, and these have
    // nothing to hand it to, so they raise.
    //
    // The type stays X::AdHoc, deliberately: a CATCH written against
    // JSON::Fast still catches. The message is ours, and says where — the
    // module's does not, and the house rule is not to copy another
    // implementation's prose.
    B["rakupp-to-json"] = [](Interpreter& I, ValueList& a) -> Value {
        return jsonToJsonBody(I, a, [](const char* why) -> Value {
            throw RakuError{Value::typeObj("X::AdHoc"), std::string("to-json: ") + why};
        });
    };
    // The tag's diagnostic sub. `core` is what it reports when the compiler
    // answered the `use` — as against `native` for a distribution's compiled
    // extension, or the name of the module it stood aside for. Registered under
    // the same mechanical spelling as the rest so the tag table stays uniform.
    B["rakupp-json-backend"] = [](Interpreter&, ValueList&) -> Value {
        return Value::str("core");
    };
    B["rakupp-from-json"] = [](Interpreter& I, ValueList& a) -> Value {
        return jsonFromJsonBody(I, a, [](const char* why) -> Value {
            throw RakuError{Value::typeObj("X::AdHoc"),
                            std::string("from-json: ") + why};
        });
    };

    // ---- the `csv` tag's primitives (DATA-PLAN P2) --------------------------
    //
    // A port of CSV::Native's csv.c, in src/DataCsv.cpp — and a port rather
    // than a reimplementation on purpose: the extension is what that
    // distribution's suite pins, and re-deriving the edge cases is how two
    // implementations of one format come to disagree.
    B["rakupp-csv-backend"] = [](Interpreter&, ValueList&) -> Value {
        return Value::str("core");
    };
    B["rakupp-from-csv"] = [](Interpreter& I, ValueList& a) -> Value {
        return dataCsvFromCsv(I, a);
    };
    B["rakupp-to-csv"] = [](Interpreter& I, ValueList& a) -> Value {
        return dataCsvToCsv(I, a);
    };

    // ---- the `digest` tag's primitives (DATA-PLAN P3) -----------------------
    //
    // Fourteen names from one table, because a hand-written entry per algorithm
    // is fourteen chances to pair `sha384` with SHA-512's core. The algorithms
    // are src/Digest.cpp, shared with the Jupyter kernel; the Raku-facing half
    // is src/DataDigest.cpp.
    B["rakupp-digest-backend"] = [](Interpreter&, ValueList&) -> Value {
        return Value::str("core");
    };
    for (const char* algo : {"md5", "sha1", "sha224", "sha256", "sha384", "sha512"}) {
        B[std::string("rakupp-") + algo] = [algo](Interpreter& I, ValueList& a) -> Value {
            return dataDigestHash(I, a, algo, false);
        };
        B[std::string("rakupp-") + algo + "-hex"] = [algo](Interpreter& I, ValueList& a) -> Value {
            return dataDigestHash(I, a, algo, true);
        };
    }
    B["rakupp-hmac"]     = [](Interpreter& I, ValueList& a) -> Value { return dataDigestHmac(I, a, false); };
    B["rakupp-hmac-hex"] = [](Interpreter& I, ValueList& a) -> Value { return dataDigestHmac(I, a, true); };

    // ---- the `zlib` tag's primitives (DATA-PLAN P4) -------------------------
    //
    // RFC 1951/1950/1952 in src/Zlib.cpp, with no libz behind it — which is the
    // point: a dlopen'd system library is not there to be found inside an
    // `--exe` binary or in the WASM playground, and those are exactly where
    // Compress::Zlib's dependents are otherwise dead.
    B["rakupp-zlib-backend"] = [](Interpreter&, ValueList&) -> Value {
        return Value::str("core");
    };
    B["rakupp-compress"]   = [](Interpreter& I, ValueList& a) -> Value { return dataZlibCompress(I, a); };
    B["rakupp-uncompress"] = [](Interpreter& I, ValueList& a) -> Value { return dataZlibUncompress(I, a); };
    B["rakupp-gzslurp"]    = [](Interpreter& I, ValueList& a) -> Value { return dataZlibGzslurp(I, a); };
    B["rakupp-gzspurt"]    = [](Interpreter& I, ValueList& a) -> Value { return dataZlibGzspurt(I, a); };
    B["rakupp-crc32"]      = [](Interpreter& I, ValueList& a) -> Value { return dataZlibChecksum(I, a, true); };
    B["rakupp-adler32"]    = [](Interpreter& I, ValueList& a) -> Value { return dataZlibChecksum(I, a, false); };

    // ---- the `random` tag's primitives (DATA-PLAN P5) -----------------------
    //
    // The one tag where NOT using the OS primitive would be the error: a CSPRNG
    // is not a thing to implement. src/DataRandom.cpp asks getentropy(2),
    // getrandom(2), BCryptGenRandom or /dev/urandom, and refuses rather than
    // degrading to anything predictable if none of them answers.
    B["rakupp-random-backend"] = [](Interpreter&, ValueList&) -> Value {
        return Value::str("core");
    };
    B["rakupp-crypt_random_buf"]     = [](Interpreter& I, ValueList& a) -> Value { return dataRandomBuf(I, a); };
    B["rakupp-crypt_random"]         = [](Interpreter& I, ValueList& a) -> Value { return dataRandomInt(I, a); };
    B["rakupp-crypt_random_uniform"] = [](Interpreter& I, ValueList& a) -> Value { return dataRandomUniform(I, a); };
    // repo.lock — the store is also zef's and Rakudo's, and a writer that
    // ignores the lock can corrupt it under a concurrent zef. IO::Handle
    // .lock is a stub here (buffered handles carry no live fd), so the
    // installer takes the lock through these instead. POSIX flock; on
    // Windows the installer proceeds unlocked (cross-tool locking there is
    // out of scope, and saying so beats pretending).
    B["rakupp-repo-lock"] = [](Interpreter&, ValueList& a) -> Value {
#ifndef _WIN32
        if (a.empty()) return Value::integer(-1);
        int fd = ::open(a[0].toStr().c_str(), O_CREAT | O_RDWR, 0666);
        if (fd < 0) return Value::integer(-1);
        if (::flock(fd, LOCK_EX) != 0) { ::close(fd); return Value::integer(-1); }
        return Value::integer(fd);
#else
        return Value::integer(-1);
#endif
    };
    B["rakupp-repo-unlock"] = [](Interpreter&, ValueList& a) -> Value {
#ifndef _WIN32
        if (!a.empty()) {
            int fd = (int)a[0].toInt();
            if (fd >= 0) { ::flock(fd, LOCK_UN); ::close(fd); }
        }
#endif
        return Value::boolean(true);
    };
    // G1 (GRAMMAR-PLAN): after a FAILED .parse/.subparse on this thread,
    // answers { pos => Int (characters), rule => Str } — the furthest point a
    // named rule failed at, and which rule. Any after a success. A rakupp
    // extension (Rakudo grammars have no diagnostics API); reach it portably
    // with the same `try &::('rakupp-parse-diagnosis')` idiom as ext-load.
    B["rakupp-parse-diagnosis"] = [](Interpreter&, ValueList&) -> Value {
        auto& d = grammarParseDiag();
        if (!d.valid) return Value::any();
        Value h = Value::makeHash();
        (*h.hash())["pos"] = Value::integer(d.pos);
        (*h.hash())["rule"] = Value::str(d.rule);
        return h;
    };
    B["rakupp-ext-load"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        // An extension resolves `rk_*` from its host. The rakupp executable
        // exports them; a COMPILED program does so only when the compiler saw
        // that it hosts an extension (main.cpp's programHostsExtension). Where
        // it did not — a name built at run time, an EVAL, a module — the first
        // call into the extension would jump through an unbound stub and die
        // with SIGSEGV, after the load and the lookup had both reported
        // success. Ask the host first, and refuse in words instead.
#if !defined(_WIN32)
        if (!dlsym(RTLD_DEFAULT, "rk_int"))
            throw RakuError{Value::typeObj("X::AdHoc"),
                "This program cannot host a native extension: its own binary does not export the "
                "rk_* ABI. Compile it with the extension's loader visible in the source (a literal "
                "`rakupp-ext-load`), or run it with the interpreter."};
#endif
        std::string err;
        std::vector<std::pair<std::string, Value>> subs;
        Value ok = extLoadModule(a[0].toStr(), err, subs);
        if (!err.empty()) throw RakuError{Value::typeObj("X::AdHoc"), err};
        for (auto& s : subs) I.tctx_.cur->define("&" + s.first, s.second);
        return ok;
    };

    // `trait_mod:<of>($routine, Type)` — the return-type trait, spelled as a CALL.
    // rakupp does not constrain a routine by its return type, so the engine has
    // nothing to record; what matters is that the call SUCCEEDS. A user
    // `trait_mod:<is>` that delegates to it first (Path::Finder's `is constraint`
    // opens with `trait_mod:<of>($method, Path::Finder:D)`) otherwise died on the
    // very first line, and the trait dispatcher — which reads a throw as "not this
    // handler's trait" — dropped the whole trait silently.
    B["trait_mod:<of>"] = [](Interpreter&, ValueList& a) -> Value {
        return a.empty() ? Value::any() : a[0];
    };
    B["say"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() == 1) return rtBSay(I, a[0]);
        std::string out;
        for (auto& v : a) out += I.gistOf(v);
        out += "\n"; return I.ioEmit(out, "$*OUT", false);
    };
    B["print"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string out; for (auto& v : a) out += I.strInStrContext(v);
        return I.ioEmit(out, "$*OUT", false);
    };
    B["put"] = [](Interpreter& I, ValueList& a) -> Value {
        // a Junction argument AUTOTHREADS the call: `put 1 & 2` writes "1\n2\n"
        // (unlike `print`, whose per-eigenstate output simply runs together).
        for (size_t i = 0; i < a.size(); i++) {
            const Value& j = a[i];
            if (j.t == VT::Array && j.arr() &&
                (j.enumName == "any" || j.enumName == "all" || j.enumName == "one" || j.enumName == "none")) {
                for (auto& e : *j.arr()) { ValueList a2 = a; a2[i] = e; I.callBuiltin("put", a2); }
                return Value::boolean(true);
            }
        }
        std::string out; for (auto& v : a) out += I.strInStrContext(v); out += "\n";
        return I.ioEmit(out, "$*OUT", false);
    };
    B["gist"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string out; bool first = true;
        for (auto& v : a) { if (!first) out += " "; first = false; out += I.gistOf(v); }
        return Value::str(out);
    };
    B["WHAT"] = [](Interpreter& I, ValueList& a) -> Value {
        return a.empty() ? Value::any() : I.methodCall(a[0], "WHAT", ValueList{});
    };
    B["note"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return I.ioEmit("Noted\n", "$*ERR", true); // no-arg default
        std::string out; for (auto& v : a) out += I.gistOf(v); out += "\n";
        return I.ioEmit(out, "$*ERR", true);
    };
    B["warn"] = [](Interpreter& I, ValueList& a) -> Value {
        if (I.quietDepth_ > 0) return Value::boolean(true); // muted inside quietly {…}
        std::string msg;
        if (a.empty()) msg = "Warning: something's wrong";
        else for (auto& v : a) msg += I.gistOf(v);
        // a dynamically-enclosing CONTROL {} sees the CX::Warn first; if it
        // .resume's, the warning is handled and nothing prints
        if (I.runControlWarn(msg)) return Value::boolean(true);
        // …and it goes through `$*ERR`, exactly as `note` does — writing to
        // std::cerr directly walked past a dynamically-overridden handle, so a
        // module that redirects $*ERR to capture output (silently, Trap) saw
        // every `note` and no `warn`.
        // …and where it was warned FROM. Rakudo prints the innermost frame only,
        // which is the right amount for a warning: the reader wants the line to
        // go look at, not the whole chain. RAKUPP_BACKTRACE=full gives the rest.
        return I.ioEmit(msg + "\n" + I.warnFrame(), "$*ERR", true);
    };
    // The handlers run where the `die` stands (dispatchBeforeUnwind), as in
    // Rakudo: a `.resume` makes this call answer Nil, and the rest of the
    // expression goes on — `my $r = (die "x")` declares $r.
    // (Only when a CATCH could be the one: under a bare `try` — or no handler
    // at all — the error is thrown straight away, as it always was.)
    B["die"] = [](Interpreter& I, ValueList& a) -> Value {
        const auto& cf = I.tctx_.catchFrames;
        if (cf.empty() || (!cf.back().catchBlk && !cf.back().fence)) throw I.dieError(a);
        return I.dieDispatching(a);
    };
    // Re-dispatch to the next candidate (currently: a built-in shadowed by a user method).
    // callsame/callwith return its result; nextsame/nextwith return it FROM the current routine.
    // `lastcall` marks the current candidate as the final one: a subsequent
    // callsame/nextsame finds no more candidates (returns Nil / an empty result).
    // Frames below the current routine activation's floor belong to a CALLER's
    // dispatch: invisible here. A visible-empty stack inside someone's dispatch
    // means "nothing further" — soft Nil (Rakudo: a bottom method's nextsame
    // does not die); a truly empty stack is the hard no-dispatcher error.
    auto dispTop = [](Interpreter& I) -> Interpreter::RedispatchCtx* {
        if (I.redispatchStack_.size() <= I.tctx_.redispatchFloor) return nullptr;
        return &I.redispatchStack_.back();
    };
    // …and with no frame at all there may still be a BUILT-IN under the running
    // method: `method clone { … callsame … }` overrides Mu.clone, and a user
    // method that overrides a built-in gets no redispatch frame (see
    // ExecContext::builtinFallback — one per method call would be too dear).
    // `skipOwn` re-enters the dispatch with the invocant's own methods bypassed,
    // which lands exactly on the built-in. The breadcrumb is cleared for the
    // duration, so a redispatch that somehow reaches the same method again
    // cannot loop on it. Returns nullopt when this activation has no built-in
    // behind it — then the no-dispatcher error stands, as before.
    auto builtinNext = [](Interpreter& I, const ValueList* with) -> std::optional<Value> {
        ExecContext::BuiltinFallback fb = I.tctx_.builtinFallback;
        if (!fb.name || !fb.self || !fb.args || fb.frame != I.tctx_.curRoutineFrame)
            return std::nullopt;
        struct Restore { ExecContext& t; ExecContext::BuiltinFallback s;
                         ~Restore() { t.builtinFallback = s; } } r{I.tctx_, fb};
        I.tctx_.builtinFallback = ExecContext::BuiltinFallback{};
        // nothing further up the MRO to defer to: the redispatch yields Nil
        // (`method nw($a) { nextwith(42) }` on a class with no parent method)
        try {
            return I.methodCall(*fb.self, *fb.name, with ? *with : *fb.args,
                                nullptr, /*skipOwn=*/true);
        } catch (RakuError& e) {
            const Value& p = e.payload;
            std::string tn = p.t == VT::Type ? p.s.str()
                           : (p.t == VT::Object && p.obj() && p.obj()->cls ? p.obj()->cls->name : std::string());
            if (tn == "X::Method::NotFound" &&
                e.message.find("'" + *fb.name + "'") != std::string::npos)
                return Value::nil();
            throw;
        }
    };
    B["lastcall"] = [dispTop](Interpreter& I, ValueList&) -> Value {
        if (auto* d = dispTop(I)) d->lastcall = true;
        return Value::boolean(true);
    };
    B["callsame"] = [dispTop, builtinNext](Interpreter& I, ValueList&) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (auto b = builtinNext(I, nullptr)) return *b;
            if (I.redispatchStack_.empty()) I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("callsame")}},
                              "callsame is not in the dynamic scope of a dispatcher");
            return Value::nil(); // exhausted chain bottom
        }
        if (d->lastcall) return Value::nil(); // trimmed by lastcall
        if (d->wrapperFrame) { if (d->spent) return Value::nil(); d->spent = true; }
        return d->next(d->sameArgs);
    };
    B["callwith"] = [dispTop, builtinNext](Interpreter& I, ValueList& a) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (auto b = builtinNext(I, &a)) return *b;
            if (I.redispatchStack_.empty()) I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("callwith")}},
                              "callwith is not in the dynamic scope of a dispatcher");
            return Value::nil();
        }
        if (d->lastcall) return Value::nil();
        if (d->wrapperFrame) { if (d->spent) return Value::nil(); d->spent = true; }
        return d->next(a);
    };
    B["nextsame"] = [dispTop, builtinNext](Interpreter& I, ValueList&) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (auto b = builtinNext(I, nullptr)) throw ReturnEx{*b};
            if (I.redispatchStack_.empty()) I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("nextsame")}},
                              "nextsame is not in the dynamic scope of a dispatcher");
            throw ReturnEx{Value::nil()};
        }
        if (d->lastcall) throw ReturnEx{Value::nil()};
        throw ReturnEx{d->next(d->sameArgs)};
    };
    // `nextcallee` — the next-less-specific candidate as a Callable, WITHOUT
    // calling it. `my &orig = nextcallee; orig(self, |c)` is how a `.wrap`
    // wrapper reaches the routine it wrapped (Method::Protected's lock wrapper).
    B["nextcallee"] = [dispTop, builtinNext](Interpreter& I, ValueList&) -> Value {
        auto* d = dispTop(I);
        std::function<Value(ValueList)> nextFn;
        // (a frame that can tell there is no next candidate answers Nil, unless
        // the core routine of that name is next in line)
        const bool exhausted = d && d->hasNext && !d->hasNext();
        if (d && !d->lastcall && !exhausted) nextFn = d->next;
        else if (auto b = builtinNext(I, nullptr)) { Value bv = *b; nextFn = [bv](ValueList) { return bv; }; }
        else {
            if (I.redispatchStack_.empty())
                I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("nextcallee")}},
                              "nextcallee is not in the dynamic scope of a dispatcher");
            return Value::nil();
        }
        // the returned Callable runs the captured next candidate with WHATEVER
        // args it is handed (the wrapper passes `self, |c` back through). Taken
        // inside a METHOD it is that next method, called with the invocant
        // first — `n(self, $x)` — while the frame's own continuation binds the
        // invocant itself and wants the rest only.
        const bool inMethod = d && !d->wrapperFrame && I.tctx_.curRoutineVal &&
                              I.tctx_.curRoutineVal->t == VT::Code && I.tctx_.curRoutineVal->code() &&
                              I.tctx_.curRoutineVal->code()->isMethod;
        return Value::closure([nextFn, inMethod](ValueList& a) -> Value {
            ValueList copy = a;
            if (inMethod && !copy.empty()) copy.erase(copy.begin());
            return nextFn(std::move(copy));
        });
    };
    B["samewith"] = [dispTop](Interpreter& I, ValueList& a) -> Value {
        // re-dispatch the CURRENT routine from scratch with new args, returning its result
        auto* d = dispTop(I);
        if (!d || !d->restart)
            I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("samewith")}},
                              "samewith is not in the dynamic scope of a dispatcher");
        return d->restart(a);
    };
    B["nextwith"] = [dispTop, builtinNext](Interpreter& I, ValueList& a) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (auto b = builtinNext(I, &a)) throw ReturnEx{*b};
            if (I.redispatchStack_.empty()) I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("nextwith")}},
                              "nextwith is not in the dynamic scope of a dispatcher");
            throw ReturnEx{Value::nil()};
        }
        if (d->lastcall) throw ReturnEx{Value::nil()};
        throw ReturnEx{d->next(a)};
    };
    B["fail"] = [](Interpreter& I, ValueList& a) -> Value {
        // Return an (undefined) Failure from the enclosing sub carrying an exception:
        // `fail $ex` / `fail "msg"` (→ X::AdHoc) / bare `fail` (picks up $!). `//` /
        // .defined treat it as undefined, so a fallback value is chosen.
        Value ex;
        // `fail $failure` RE-ARMS it: the new Failure carries the same exception
        // (unhandled again) — `foo() orelse fail $_` passes the error up
        if (!a.empty() && a[0].t == VT::Hash && a[0].hashKind == "Failure" && a[0].hash() &&
            a[0].hash()->count("exception")) {
            ex = (*a[0].hash())["exception"];
        } else if (!a.empty() && a[0].t == VT::Object) {
            ex = a[0];
        } else if (!a.empty()) {
            auto it = I.classes_.find("X::AdHoc");
            if (it != I.classes_.end()) {
                ex.t = VT::Object; ex.setObj(makePayload<ObjectData>()); ex.obj()->cls = it->second;
                ex.obj()->attrs["message"] = Value::str(a[0].toStr());
                // `fail %h` keeps the value as the exception's PAYLOAD, as Rakudo's
                // X::AdHoc does (Text::SubParsers reports a failed parse that way)
                ex.obj()->attrs["payload"] = a[0];
            } else ex = Value::str(a[0].toStr());
        } else {
            // the ROUTINE's own `$!`, as `die` reads it: an error the caller
            // caught is not this routine's to fail with — a bare `fail` in a
            // sub called after a `try` says "Failed" (Rakudo)
            Value* be = nullptr;
            for (Env* en = I.tctx_.cur.get(); en; en = en->parent.get()) {
                if ((be = en->local("$!"))) break;
                if (en->routineFrame) break;
            }
            if (be && be->t != VT::Nil && be->t != VT::Type) ex = *be;
        }
        // a bare `fail` with no $! still carries an exception — X::AdHoc
        // "Failed" — so `.exception.message` answers rather than dying on Any
        if (ex.t != VT::Object)
            ex = I.makeTypedEx("X::AdHoc", {}, ex.t == VT::Str ? ex.s.str() : std::string("Failed"));
        Value f = rakuppNewFailure();
        (*f.hash())["exception"] = ex;
        (*f.hash())["message"] = ex.obj() && ex.obj()->attrs.count("message")
                             ? ex.obj()->attrs["message"] : Value::str("Failed");
        // with no ROUTINE to return from, `fail` behaves like `die` (Rakudo):
        // the exception itself is thrown where the fail was written — and so
        // it does under `use fatal`, which a `try` block's scope is
        if (I.tctx_.curRoutineFrame == 0 || I.fatalHere()) {
            std::string msg = (*f.hash())["message"].toStr();
            throw RakuError{ex, msg};
        }
        throw ReturnEx{f};
    };
    // val(Str) — a fully-numeric string becomes the matching allomorph
    // (IntStr/RatStr/NumStr/ComplexStr: the number AND its source spelling);
    // anything else passes through unchanged. prompt() routes its line here.
    // The definition lives in Interpreter.cpp so MAIN's argv gets the identical
    // conversion — the two used to differ, and that is issue #11.
    auto valAllomorph = [](const Value& v) -> Value { return rakupp::valAllomorph(v); };
    B["val"] = [valAllomorph](Interpreter&, ValueList& a) -> Value {
        return a.empty() ? Value::nil() : valAllomorph(a[0]);
    };
    // `prompt($message?, :hidden)`.
    //
    // The nameds are PARTITIONED OFF FIRST, and that is a fix rather than
    // bookkeeping: this took `a[0]` as the message unconditionally, so
    // `prompt(:hidden)` with no message printed the pair — "hidden\tTrue" —
    // as the prompt string.
    //
    // `:hidden` is an extension; Rakudo's prompt has no nameds at all and dies
    // on any of them. It returns a plain Str, NOT the allomorph an ordinary
    // prompt returns, and the difference is not cosmetic: a numeric password
    // would come back an IntStr, and an IntStr serialises through JSON::Fast
    // as the NUMBER 1234 — a secret silently retyped, with any leading zero
    // gone. A secret is a string.
    // `prompt($message?)` — and NOTHING else. Rakudo's prompt has two
    // signatures, `()` and `($msg)`, so every named argument is a caller error
    // there; this used to accept and silently discard them.
    //
    // `:hidden` in particular is NOT spelled here on purpose. The engine can
    // read a line without echoing it — that is `rakupp-prompt-hidden` below —
    // but putting the adverb on `prompt` itself would mint a dialect: the
    // program would run here and die on every other Raku, and the divergence
    // would only surface on the day it was ported. A module that probes for
    // the primitive and falls back to `stty` gives the same source one meaning
    // everywhere, so the adverb belongs to the module and the capability to
    // the engine.
    B["prompt"] = [](Interpreter& I, ValueList& a) -> Value {
        for (const Value& v : a)
            if (v.t == VT::Pair && v.namedArg)
                throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                                "prompt takes no named arguments (got :" + v.s + ")"};
        // a `$*IN`/`$*OUT` that is not the process's own (a `temp $*IN =
        // $file.open`) is where prompt talks: it prints to $*OUT and reads
        // one line from $*IN with THAT handle's chomp and nl-in
        auto dyn = [&](const char* n) -> Value {
            Value* p = I.tctx_.cur ? I.tctx_.cur->find(n) : nullptr;
            return p ? *p : Value::any();
        };
        auto isStd = [](const Value& h, const char* which) {
            return h.t == VT::Hash && h.hash() && h.hash()->count("std") &&
                   (*h.hash()).at("std").toStr() == which;
        };
        Value in = dyn("$*IN"), out = dyn("$*OUT");
        bool customIn = in.t != VT::Any && !isStd(in, "in");
        bool customOut = out.t != VT::Any && !isStd(out, "out");
        if (customIn || customOut) {
            if (!a.empty()) {
                std::string msg = I.strOf(a[0]);
                if (customOut) { I.methodCall(out, "print", ValueList{Value::str(msg)}); try { I.methodCall(out, "flush", ValueList{}); } catch (...) {} }
                else { std::cout << msg << std::flush; }
            }
            if (customIn) {
                Value line = I.methodCall(in, "get", ValueList{});
                if (line.t == VT::Str) return I.callBuiltin("val", ValueList{line});
                return line;
            }
            ValueList none; return promptImpl(none, false);
        }
        return promptImpl(a, false);
    };
    // The capability, as a primitive rather than an adverb — this is what a
    // portable module probes for with `try &::('rakupp-prompt-hidden')`,
    // taking the engine's echo suppression when it is there and `stty` when it
    // is not. Optional message, plain Str back (never the allomorph `prompt`
    // returns — a numeric secret is not a number), Nil at end of input.
    B["rakupp-prompt-hidden"] = [](Interpreter&, ValueList& a) -> Value { return promptImpl(a, true); };
    B["__qx__"] = [](Interpreter& I, ValueList& a) -> Value { // qx// / qqx// shell capture
        std::string cmd = a.empty() ? "" : a[0].toStr();
        I.syncEnvToProcess();   // the child sees `%*ENV<X> = …` made before it
        std::string outp; char buf[4096]; size_t n;
#if defined(_WIN32)
        FILE* p = _popen(cmd.c_str(), "r");
        if (!p) return Value::str("");
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) outp.append(buf, n);
        _pclose(p);
#else
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) return Value::str("");
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) outp.append(buf, n);
        pclose(p);
#endif
        return Value::str(outp);
    };
    B["dd"] = [](Interpreter& I, ValueList& a) -> Value {
        // dd renders .raku, not .gist — and it must DISPATCH, so a type with a
        // .raku of its own (a user class, $*RAKU) shows that rather than a
        // generic rendering. Plain Str keeps its quoted short form.
        std::string out;
        for (size_t i = 0; i < a.size(); i++) {
            if (i) out += ", ";
            if (a[i].t == VT::Str && a[i].hashKind.empty()) { out += "\"" + a[i].s + "\""; continue; }
            ValueList none;
            out += I.methodCall(a[i], "raku", none).toStr();
        }
        std::cerr << out << "\n";
        return a.empty() ? Value::any() : a[0];
    };
    // mathematical constants (callable as bare terms: pi, tau, e, and π τ 𝑒)
    // NB: pi/tau/e are TERMS (handled in NameTerm eval), not subs — calling `pi()` must die.
    // junction list-op constructors: all(...)/any(...)/none(...)/one(...)
    for (const char* jn : {"all", "any", "none", "one"}) {
        std::string name = jn;
        B[name] = [name](Interpreter&, ValueList& a) -> Value {
            Value j = Value::array(); j.enumName = name;
            // one-arg rule: any(@a) spreads the single iterable one level;
            // any(x, y, …) keeps each argument as ONE eigenstate (lists whole)
            if (a.size() == 1 && a[0].t == VT::Array && a[0].arr())
                for (auto& x : *a[0].arr()) j.arr()->push_back(x);
            else if (a.size() == 1 && a[0].t == VT::Range)
                for (auto& x : a[0].flatten()) j.arr()->push_back(x);
            else
                for (auto& v : a) j.arr()->push_back(v);
            return j;
        };
    }

    // --- Test module ---
    B["plan"] = [](Interpreter& I, ValueList& a) -> Value {
        I.usedTest_ = true;
        // plan skip-all => "reason" : emit an empty SKIP plan and exit the test file
        bool skipAll = false; std::string reason;
        for (auto& x : a) {
            if (x.t == VT::Pair && x.s == "skip-all") { skipAll = true; reason = x.pairVal() ? x.pairVal()->toStr() : ""; }
            else if (x.t == VT::Str && x.s == "skip-all") skipAll = true;
        }
        if (skipAll && I.subtestDepth_ > 0) {
            // inside a subtest it RETURNS from the subtest's Sub, leaving the
            // subtest a pass — which needs a Sub to return from: a Block body
            // is refused (Rakudo says so rather than skipping the whole file)
            if (!I.subtestIsSub_.empty() && !I.subtestIsSub_.back())
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Cannot use `plan skip-all` inside a subtest whose body is a Block; make it a Sub"};
            I.planned_ = 0;
            std::cout << std::string(4 * I.subtestDepth_, ' ') << "1..0 # SKIP " << reason << "\n" << std::flush;
            throw ReturnEx{Value::nil()};
        }
        if (skipAll) { I.planned_ = 0; std::cout << "1..0 # SKIP " << reason << "\n" << std::flush; throw ExitEx{0}; }
        // `plan *` means "no plan" — the count comes from done-testing, and nothing
        // is printed up front (File::Which's suite opens with it)
        if (!a.empty() && a[0].t == VT::Whatever) return Value::boolean(true);
        if (!a.empty()) { I.planned_ = a[0].toInt(); std::cout << std::string(4 * I.subtestDepth_, ' ') << "1.." << I.planned_ << "\n"; }
        return Value::boolean(true);
    };
    B["ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool c = !a.empty() && a[0].truthy();
        I.emitTest(c, testDesc(a, 1), testDirective(a));
        return Value::boolean(c);
    };
    B["nok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool c = a.empty() || !a[0].truthy();
        I.emitTest(c, testDesc(a, 1), testDirective(a));
        return Value::boolean(c);
    };
    // eq honouring a Junction expected value: `is $got, ("a"|"b")` autothreads the
    // comparison and collapses per the junction's kind (any/all/one/none).
    auto isEq = [](const Value& got, const Value& exp) -> bool {
        auto scalarEq = [](const Value& g, const Value& e) {
            // Two undefined values match regardless of how they stringify: Rakudo's
            // `is` treats an undefined expected as a definedness check, so
            // `is @a[11], Any` passes even though a bare undef stringifies to '' but
            // `Any` gists to '(Any)'. This is additive to the stringify compare below
            // so a defined-empty got still matches an undefined expected (`is Nil, ''`).
            if (!defined(g) && !defined(e)) return true;
            // Same-size lists compare elementwise FIRST so an undefined element
            // matches an undefined expected (`is %h{<B C>}, (Any, Any)` — the
            // sides stringify differently but are equal). Only an all-elements
            // match short-circuits; anything else falls through to the plain
            // string compare, so this can only ADD passes.
            if (g.t == VT::Array && e.t == VT::Array && g.arr() && e.arr() &&
                g.arr()->size() == e.arr()->size() && !g.arr()->empty()) {
                bool all = true;
                for (size_t i = 0; i < g.arr()->size() && all; i++) {
                    const Value& gi = (*g.arr())[i];
                    const Value& ei = (*e.arr())[i];
                    if (!defined(gi) && !defined(ei)) continue;
                    if (gi.toStr() != ei.toStr()) all = false;
                }
                if (all) return true;
            }
            // Otherwise Rakudo compares stringified values with `eq` (Test::is), so
            // `is 1/3, 0.333333` passes on matching decimal forms. (Exact-numeric
            // comparison lives in is-approx / cmp-ok, not plain `is`.)
            return g.toStr() == e.toStr();
        };
        if (exp.t == VT::Array && exp.arr() &&
            (exp.enumName == "any" || exp.enumName == "all" || exp.enumName == "one" || exp.enumName == "none")) {
            // (…a junction GOT threads inside each branch: `is $x cmp any(…),
            // any(Same, Less)` holds when any pair matches)
            auto gotEq = [&](const Value& e) {
                if (!(got.t == VT::Array && got.arr() && (got.enumName == "any" || got.enumName == "all" ||
                                                           got.enumName == "one" || got.enumName == "none")))
                    return scalarEq(got, e);
                JunctionCollapse gj(got.enumName);
                for (auto& g : *got.arr()) { gj.feed(scalarEq(g, e)); if (gj.done()) break; }
                return gj.verdict();
            };
            JunctionCollapse jc(exp.enumName);     // short-circuits; see Value.h
            for (auto& br : *exp.arr()) { jc.feed(gotEq(br)); if (jc.done()) break; }
            return jc.verdict();
        }
        // …and a junction GOT autothreads the same way: `is any(@names), 'a'`
        // collapses per the junction's kind (HTTP::UserAgent's header tests)
        if (got.t == VT::Array && got.arr() &&
            (got.enumName == "any" || got.enumName == "all" || got.enumName == "one" || got.enumName == "none")) {
            JunctionCollapse jc(got.enumName);
            for (auto& br : *got.arr()) { jc.feed(scalarEq(br, exp)); if (jc.done()) break; }
            return jc.verdict();
        }
        return scalarEq(got, exp);
    };
    // An object argument (e.g. an exception in `is $!, 'msg'`) compares by its Str —
    // which for an Exception is its .message, matching `~$!` (via strOf).
    // …and a LIST of objects compares by the same rule, element by element:
    // `is $elem.contents, 'text'` where .contents is a list of XML::Text nodes.
    auto isStrify = [](Interpreter& I, Value& v) {
        // …a Proxy too: it is a container, and `is` compares the value it holds.
        auto proxyish = [](const Value& e) {
            return e.t == VT::Hash && e.hashKind == "Proxy" && e.hash();
        };
        if (v.t == VT::Object || proxyish(v)) { v = Value::str(I.strOf(v)); return; }
        if (v.t == VT::Array && v.arr() && v.enumName.empty())
            for (auto& e : *v.arr())
                if (e.t == VT::Object || proxyish(e)) { v = Value::str(I.strOf(v)); return; }
    };
    B["is"] = [isEq, isStrify](Interpreter& I, ValueList& a) -> Value {
        Value got = a.size() > 0 ? a[0] : Value::any();
        Value exp = a.size() > 1 ? a[1] : Value::any();
        isStrify(I, got); isStrify(I, exp);
        bool c = isEq(got, exp);
        std::string dir = testDirective(a);
        std::string diag = (!c && dir.empty()) ? "# expected: '" + exp.toStr() + "'\n# got:      '" + got.toStr() + "'\n" : "";
        I.emitTest(c, testDesc(a, 2), dir, diag);
        return Value::boolean(c);
    };
    B["isnt"] = [isEq, isStrify](Interpreter& I, ValueList& a) -> Value {
        Value got = a.size() > 0 ? a[0] : Value::any();
        Value exp = a.size() > 1 ? a[1] : Value::any();
        // Test's `isnt(Mu $got, Mu:D $expected)` is `!$got.defined || $got ne $expected`:
        // an undefined got is never the same as a defined expectation
        const bool gotUndefDefExp = !defined(got) && defined(exp);
        isStrify(I, got); isStrify(I, exp);
        bool c = gotUndefDefExp || !isEq(got, exp);
        I.emitTest(c, testDesc(a, 2), testDirective(a)); // adverbs are not the description
        return Value::boolean(c);
    };
    auto likeTest = [](Interpreter& I, ValueList& a, bool want) -> Value {
        // `like` stringifies its subject: a non-Str (an Int, or an object with a
        // .Str) is matched by what it stringifies TO, not by an empty string
        std::string got = a.empty() ? "" : I.strOf(a[0]);
        bool m = false;
        if (a.size() > 1) {
            if (a[1].t == VT::Regex) m = I.regexMatch(got, a[1].s).truthy();
            else throw RakuError{Value::typeObj("X::Multi::NoMatch"), // Rakudo's signature is (…, Regex:D $expected, …)
                                 "Cannot resolve caller like(Str:D, " + a[1].typeName() + "); the expected value must be a Regex"};
        }
        bool c = (m == want);
        std::string dir = testDirective(a);
        std::string diag = (!c && dir.empty()) ? "# got: '" + got + "'\n" : "";
        I.emitTest(c, testDesc(a, 2), dir, diag);
        return Value::boolean(c);
    };
    B["like"]   = [likeTest](Interpreter& I, ValueList& a) -> Value { return likeTest(I, a, true); };
    B["unlike"] = [likeTest](Interpreter& I, ValueList& a) -> Value { return likeTest(I, a, false); };
    B["is-deeply"] = [](Interpreter& I, ValueList& a) -> Value {
        // is-deeply IS `eqv` — type-strict — and not the looser structural
        // compare. Rakudo's Test.rakumod computes `$got eqv $expected`, so
        // `is-deeply "11", 11` FAILS there; routing this through deepEq made it
        // pass here, which is the dangerous shape: a suite that goes green
        // without agreeing on a single type. The one adjustment Test.rakumod
        // makes is its Seq candidates, which `.cache` a Seq operand into a List
        // before comparing (`(1,2).Seq eqv (1,2)` is False, but
        // `is-deeply (1,2).Seq, (1,2)` passes).
        auto cached = [](const Value& v) -> Value {
            if (v.t == VT::Array && v.isList && v.s == "Seq") {
                forceLazy(v);
                Value c = v; c.s = ""; // Seq.cache is a List
                return c;
            }
            return v;
        };
        // applyArith, not valueEqv directly: `eqv` autothreads a Junction
        // operand there, and `is-deeply 1, 1|2` relies on it.
        bool c = a.size() >= 2 && applyArith("eqv", cached(a[0]), cached(a[1])).truthy();
        I.emitTest(c, testDesc(a, 2), testDirective(a)); // adverbs are not the description
        if (!c && a.size() >= 2) { // failure diagnostics (stderr), Rakudo-style
            std::cerr << "# expected: " << rakuRepr(a[1]) << "\n"
                      << "#      got: " << rakuRepr(a[0]) << "\n";
        }
        return Value::boolean(c);
    };
    B["cmp-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        // cmp-ok($a, $op, $b, $desc) — $op may be an operator NAME or a Code
        // (the roast idiom `cmp-ok $x, &[!==], $y`).
        bool c = false;
        if (a.size() >= 3 && a[1].t == VT::Code) {
            c = I.callCallable(a[1], ValueList{a[0], a[2]}).truthy();
        }
        else if (a.size() >= 3) {
            std::string op = a[1].toStr();
            const Value& x = a[0]; const Value& y = a[2];
            // an operator the CALLER declared (`sub infix:<◀>`) is found where it is
            // written — the name is looked up in the caller's scope
            Value* user = I.tctx_.cur ? I.tctx_.cur->find("&infix:<" + op + ">") : nullptr;
            if (user && user->t == VT::Code) {
                c = I.callCallable(*user, ValueList{x, y}).truthy();
                I.emitTest(c, a.size() > 3 ? a[3].toStr() : "");
                return Value::boolean(c);
            }
            // the numeric fast path only fits actual numbers — a Version (or any
            // tagged value) must go through the real operator (`cmp-ok $v, '>',
            // v0.0.0` flattened both sides to 0 and failed; Log::Async's suite)
            bool bothNum = x.isNumeric() && y.isNumeric();
            // a JUNCTION on either side threads through the real operator —
            // `cmp-ok "/", 'eq', <\ />.any` is True (S32-io/io-path.t); the
            // string shortcut below compared against the text "any(…)"
            if ((isJunction(x) || isJunction(y)) && op != "~~" && op != "!~~")
                c = applyArith(op, x, y).truthy();
            else if (bothNum && op == "==") c = x.toNum() == y.toNum();
            else if (bothNum && op == "!=") c = x.toNum() != y.toNum();
            else if (bothNum && op == "<") c = x.toNum() < y.toNum();
            else if (bothNum && op == ">") c = x.toNum() > y.toNum();
            else if (bothNum && op == "<=") c = x.toNum() <= y.toNum();
            else if (bothNum && op == ">=") c = x.toNum() >= y.toNum();
            else if (op == "eq") c = x.toStr() == y.toStr();
            else if (op == "ne") c = x.toStr() != y.toStr();
            // `~~` is the FULL matcher: a block on the right is CALLED with the
            // value (`cmp-ok $out, '~~', { .contains: "FOO" & "bar" }` — Roast's
            // Test::Util run-with-tty judges a child's STDOUT that way), a regex
            // is matched, a junction of matchers threaded, an ACCEPTS object
            // asked. applyArith knows none of that and answered False for every
            // block, so S32-io/out-buffering.t's "prompt does not hang" failed
            // with both expected words sitting in the captured output.
            else if (op == "~~")  c = matcherAccepts(I, x, y);
            else if (op == "!~~") c = !matcherAccepts(I, x, y);
            else {
                // an operator nothing knows is a FAILED test, not an abandoned one
                try { c = applyArith(op, x, y).truthy(); } // ===, eqv, before/after, user ops…
                catch (RakuError& e) {
                    if (e.message.rfind("Unsupported operator", 0) != 0) throw;
                    I.emitTest(false, a.size() > 3 ? a[3].toStr() : "", "",
                               "# Could not use '" + op + "' as a comparator.\n");
                    return Value::boolean(false);
                }
            }
        }
        // On failure, present the operands via .raku (the "presentable" form) — not
        // .Str, which some objects make die — and name the matcher like Rakudo.
        std::string diag;
        if (!c && a.size() >= 3) {
            auto pres = [&](const Value& v) { Value vv = v; return I.methodCall(vv, "raku", ValueList{}).toStr(); };
            std::string mstr = a[1].t == VT::Code
                ? [&]{ Value m = a[1]; return I.methodCall(m, "gist", ValueList{}).toStr(); }()
                : "'infix:<" + a[1].toStr() + ">'";
            diag = "# expected: " + pres(a[2]) + "\n#  matcher: " + mstr + "\n#      got: " + pres(a[0]) + "\n";
        }
        I.emitTest(c, a.size() > 3 ? a[3].toStr() : "", "", diag);
        return Value::boolean(c);
    };
    B["todo"] = [](Interpreter& I, ValueList& a) -> Value { // todo($reason, $count=1): mark next tests TODO
        I.todoReason_ = a.empty() ? "" : a[0].toStr();
        I.todoRemaining_ = a.size() > 1 ? (int)a[1].toInt() : 1;
        return Value::boolean(true);
    };
    B["pass"] = [](Interpreter& I, ValueList& a) -> Value { I.emitTest(true, a.empty() ? "" : a[0].toStr()); return Value::boolean(true); };
    B["flunk"] = [](Interpreter& I, ValueList& a) -> Value { I.emitTest(false, a.empty() ? "" : a[0].toStr()); return Value::boolean(false); };
    B["diag"] = [](Interpreter&, ValueList& a) -> Value { std::cerr << "# " << (a.empty() ? "" : a[0].toStr()) << "\n"; return Value::boolean(true); };
    B["skip"] = [](Interpreter& I, ValueList& a) -> Value {
        // the COUNT comes second and is a whole number: `skip 2, 'reason'` has
        // its arguments backwards, and Test says so rather than skipping
        if (a.size() > 1 && !(a[1].t == VT::Int || (a[1].t == VT::Num && a[1].toNum() == (double)(long long)a[1].toNum())))
            throw RakuError{Value::typeObj("X::AdHoc"),
                            "skip() takes the reason first and a whole number of tests second "
                            "(were the arguments given backwards?)"};
        long n = (a.size() > 1) ? a[1].toInt() : 1;
        std::string reason = a.empty() ? "" : a[0].toStr();
        // Rakudo's `ok N - # SKIP reason`, with the reason's own `#` escaped
        std::string esc;
        for (char ch : reason) { if (ch == '#') esc += " \\#"; else esc += ch; }
        for (long k = 0; k < n; k++) I.emitTest(true, "", "SKIP " + esc);
        return Value::boolean(true);
    };
    B["dies-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        int callLine = I.testLine(); // the block runs first; report OUR line, not its
        bool died = false;
        if (!a.empty() && a[0].t == VT::Code) {
            // the block's value is SUNK, and sinking an unhandled Failure throws it —
            // `dies-ok { $c.to-string('bogus') }` over a routine that `fail`s (Color)
            try { I.sinkValue(I.callCallable(a[0], {})); } // a sunk Failure or failed Proc throws
            catch (RakuError&) { died = true; }
            // a loop-control exception with no enclosing loop is a death (X::ControlFlow);
            // inside one it ESCAPES to it, recording no test (Rakudo)
            catch (NextEx&) { if (I.loopNest_ > 0) throw; died = true; }
            catch (LastEx&) { if (I.loopNest_ > 0) throw; died = true; }
            catch (RedoEx&) { if (I.loopNest_ > 0) throw; died = true; }
        }
        I.restoreTestLine(callLine);
        I.emitTest(died, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(died);
    };
    B["lives-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        int callLine = I.testLine();
        bool lived = true;
        if (!a.empty() && a[0].t == VT::Code) {
            try { I.sinkValue(I.callCallable(a[0], {})); } // a sunk Failure or failed Proc throws
            catch (RakuError&) { lived = false; }
            // loop control with no loop is a death, as dies-ok counts it; inside
            // one it escapes to it (see dies-ok)
            catch (NextEx&) { if (I.loopNest_ > 0) throw; lived = false; }
            catch (LastEx&) { if (I.loopNest_ > 0) throw; lived = false; }
            catch (RedoEx&) { if (I.loopNest_ > 0) throw; lived = false; }
        }
        I.restoreTestLine(callLine);
        I.emitTest(lived, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(lived);
    };
    B["use-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        int callLine = I.testLine();
        std::string mod = a.empty() ? "" : a[0].toStr();
        // The argument is a `use` STATEMENT's module spec, adverbs and all:
        // `use-ok 'NativeLibs:v<0.0.9>'` (NativeLibs' own suite). loadModule takes
        // the bare name plus a version REQUIREMENT, so split them here — the whole
        // string named no module at all and every such use-ok reported a failure.
        std::string bare = mod, verReq;
        for (size_t i = 0; i + 1 < mod.size(); i++) {
            // …and a `::` in the NAME is not an adverb colon. The guard skipped
            // the second colon of the pair but not the first, so
            // `Sway::Config:auth<zef:CIAvash>` read its adverb as ":Config:auth",
            // fell out of the loop and asked the loader for the whole string.
            if (mod[i] != ':' || (i && mod[i - 1] == ':') || mod[i + 1] == ':') continue;
            size_t lt = mod.find('<', i);
            size_t gt = lt == std::string::npos ? std::string::npos : mod.find('>', lt);
            if (lt == std::string::npos || gt == std::string::npos) break;
            std::string adv = mod.substr(i + 1, lt - i - 1);
            if (adv == "ver" || adv == "v") verReq = mod.substr(lt + 1, gt - lt - 1);
            if (adv == "ver" || adv == "v" || adv == "auth" || adv == "api") {
                if (bare.size() > i) bare = mod.substr(0, i);
                i = gt;
            }
            else break;
        }
        // Rakudo's use-ok is `EVAL "use $code"`: the module is loaded and its
        // imports land in the EVAL's own scope, which is then dropped. Into
        // the caller's they leaked, and an exported MAIN was then RUN with the
        // test's arguments (#124: a CLI's t/00-load.t started the app)
        bool ok = true;
        {
            auto scope = std::make_shared<Env>();
            scope->parent = I.tctx_.cur;
            struct CurG { ExecContext& t; std::shared_ptr<Env> s; ~CurG() { t.cur = s; } } cg{I.tctx_, I.tctx_.cur};
            I.tctx_.cur = scope;
            try { I.loadModule(bare, {}, /*doImport=*/true, /*quiet=*/false, verReq); } catch (...) { ok = false; }
        }
        I.restoreTestLine(callLine);
        I.emitTest(ok, a.size() > 1 ? a[1].toStr() : ("The module can be use-d ok: " + mod));
        return Value::boolean(ok);
    };
    B["can-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        // can-ok($obj, 'method', $desc?) — the default description names the type
        // and the method, the way Rakudo's Test does
        bool c = false;
        std::string meth = a.size() > 1 ? a[1].toStr() : "";
        if (a.size() >= 2) c = I.methodCall(a[0], "can", ValueList{Value::str(meth)}).truthy();
        std::string desc = a.size() > 2 ? a[2].toStr()
                                        : "An object of type '" +
                                          (a.empty() ? std::string() : a[0].typeName()) +
                                          "' can do the method '" + meth + "'";
        I.emitTest(c, desc);
        return Value::boolean(c);
    };
    B["does-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        // does-ok($obj, Role, $desc?) — role/type membership via .does
        bool c = false;
        if (a.size() >= 2) c = I.methodCall(a[0], "does", ValueList{a[1]}).truthy();
        std::string desc;
        for (size_t i = 2; i < a.size(); i++) if (a[i].t == VT::Str) { desc = a[i].s; break; }
        I.emitTest(c, desc);
        return Value::boolean(c);
    };
    B["isa-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        // the "type" argument may be a type object, a type NAME string, or any
        // other value — a plain value stands for its own type (isa-ok 3, 3)
        std::string want = a.size() > 1
            ? (a[1].t == VT::Type ? a[1].s : a[1].t == VT::Str ? a[1].toStr() : a[1].typeName())
            : "";
        std::string got = a.empty() ? "Any" : a[0].typeName();
        // ONE default description for both exit paths — the fast path used to say
        // "isa Int" and the ancestry fallback said nothing at all
        std::string desc = a.size() > 2 ? a[2].toStr() : "The object is-a '" + want + "'";
        // the real .isa knows allomorphs and user-class chains — consult it first,
        // then .does, because that is what Rakudo's isa-ok is: a ROLE is never
        // something you `.isa`, so `isa-ok $obj, SomeRole` (and `isa-ok $buf,
        // buf8`, buf8 being a curried role there) would fail on both engines
        // otherwise. It stops short of full smartmatch — `isa-ok 5, 1..10` and
        // `isa-ok "abc", /b/` fail on Rakudo, and .does keeps them failing.
        if (a.size() > 1) {
            for (const char* probe : {"isa", "does"}) {
                ValueList ia{a[1]};
                Value r = I.methodCall(a[0], probe, ia);
                if (r.truthy()) {
                    I.emitTest(true, desc);
                    return Value::boolean(true);
                }
            }
            // a SUBSET target passes for a value the subset accepts
            // (`isa-ok 5, UInt` — Date::Event walks its enum map this way);
            // still short of smartmatch: the target must be a TYPE object
            if (a[1].t == VT::Type &&
                (a[1].s == "UInt" ? a[0].t == VT::Int && !(a[0].big() ? a[0].big()->sign < 0 : a[0].i < 0)
                                  : I.subsetMatches(a[1].s, a[0]))) {
                I.emitTest(true, desc);
                return Value::boolean(true);
            }
        }
        static const std::map<std::string, std::set<std::string>> isa = {
            {"Int", {"Int", "Cool", "Numeric", "Real", "Any", "Mu"}},
            {"Num", {"Num", "Cool", "Numeric", "Real", "Any", "Mu"}},
            {"Str", {"Str", "Cool", "Stringy", "Any", "Mu"}},
            {"Bool", {"Bool", "Int", "Cool", "Numeric", "Real", "Any", "Mu"}},
            {"Sub", {"Sub", "Routine", "Block", "Code", "Callable", "Any", "Mu"}},
            {"Method", {"Method", "Routine", "Block", "Code", "Callable", "Any", "Mu"}},
            {"Block", {"Block", "Code", "Callable", "Any", "Mu"}},
            {"Array", {"Array", "List", "Any", "Mu", "Positional"}},
            {"array", {"array", "Array", "List", "Any", "Mu", "Positional", "Iterable"}},
            {"Seq", {"Seq", "List", "Any", "Mu", "Positional", "Iterable"}},
            {"IO::Path", {"IO::Path", "IO", "Cool", "Any", "Mu"}},
            {"IO::Path::Unix", {"IO::Path::Unix", "IO::Path", "IO", "Cool", "Any", "Mu"}},
            {"IO::Path::Win32", {"IO::Path::Win32", "IO::Path", "IO", "Cool", "Any", "Mu"}},
            {"IO::Path::Cygwin", {"IO::Path::Cygwin", "IO::Path", "IO", "Cool", "Any", "Mu"}},
            {"IO::Path::QNX", {"IO::Path::QNX", "IO::Path", "IO", "Cool", "Any", "Mu"}},
            {"Version", {"Version", "Any", "Mu"}},
            {"Blob", {"Blob", "Positional", "Stringy", "Any", "Mu"}}, // a Buf does Blob; a Blob is NOT a Buf
            {"Compiler", {"Compiler", "Any", "Mu"}},
            {"Hash", {"Hash", "Map", "Any", "Mu", "Associative"}},
            {"Pod::Block", {"Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Named", {"Pod::Block::Named", "Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Para", {"Pod::Block::Para", "Pod::Block", "Any", "Mu"}},
            {"Pod::FormattingCode", {"Pod::FormattingCode", "Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Code", {"Pod::Block::Code", "Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Comment", {"Pod::Block::Comment", "Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Table", {"Pod::Block::Table", "Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Declarator", {"Pod::Block::Declarator", "Pod::Block", "Any", "Mu"}},
            {"Pod::Heading", {"Pod::Heading", "Pod::Block", "Any", "Mu"}},
            {"Pod::Item", {"Pod::Item", "Pod::Block", "Any", "Mu"}},
        };
        // walk a class's ancestry: the built-in isa map, plus a user class's parent
        // chain (incl. a native parent like `is Str`) and extra `is` parents.
        std::function<bool(const std::string&)> ancestorHas = [&](const std::string& cn) -> bool {
            if (cn == want) return true;
            auto mit = isa.find(cn);
            if (mit != isa.end() && mit->second.count(want)) return true;
            auto cit = I.classes_.find(cn);
            if (cit != I.classes_.end() && cit->second) {
                if (cit->second->parent && ancestorHas(cit->second->parent->name)) return true;
                if (!cit->second->nativeParent.empty() && ancestorHas(cit->second->nativeParent)) return true;
                for (auto& ep : cit->second->extraParents) if (ep && ancestorHas(ep->name)) return true;
            }
            return false;
        };
        bool c = ancestorHas(got);
        I.emitTest(c, desc);
        return Value::boolean(c);
    };
    B["is-approx"] = [](Interpreter& I, ValueList& a) -> Value {
        // Complex-aware: compare as points in the plane, |got - exp|
        // …and an Object numifies through its own `.Bridge`/`.Numeric`: a
        // `does Real` class read as 0 here, so `is-approx $one, $one` compared
        // 1 against 0 and every Bridge assertion in S32-num/real-bridge.t that
        // put the custom type on the EXPECTED side failed.
        auto re = [&I](const Value& v) { return v.t == VT::Complex ? v.n : numValueOf(I, v); };
        auto im = [](const Value& v) { return v.t == VT::Complex ? v.im() : 0.0; };
        double gr = a.size() > 0 ? re(a[0]) : 0, gi = a.size() > 0 ? im(a[0]) : 0;
        double er = a.size() > 1 ? re(a[1]) : 0, ei = a.size() > 1 ? im(a[1]) : 0;
        double tol = 1e-5;
        std::string desc;
        bool haveRel = false, haveAbs = false, havePosTol = false;
        double relTol = 0, absTol = 0;
        // named :rel-tol / :abs-tol arrive as positional Pairs; a bare numeric 3rd
        // arg is the (relative) tolerance, a Str is the description.
        for (size_t i = 2; i < a.size(); i++) {
            if (a[i].t == VT::Pair) {
                std::string k = a[i].s; double val = a[i].pairVal() ? a[i].pairVal()->toNum() : 0;
                if (k == "rel-tol") { haveRel = true; relTol = val; }
                else if (k == "abs-tol") { haveAbs = true; absTol = val; }
            } else if (a[i].isNumeric() && !havePosTol) { tol = a[i].toNum(); havePosTol = true; }
            else if (a[i].t == VT::Str && desc.empty()) desc = a[i].toStr();
        }
        double diff = std::hypot(gr - er, gi - ei);
        double gm = std::hypot(gr, gi), em = std::hypot(er, ei);
        bool c;
        if (haveRel || haveAbs) {
            c = true;
            if (haveAbs) c = c && (diff <= absTol);
            if (haveRel) { double mx = std::max(gm, em); c = c && (mx == 0 ? true : diff / mx <= relTol); }
        } else if (havePosTol) {
            c = diff <= tol; // a positional tolerance is ABSOLUTE (Test.rakumod); it was scaled by the magnitude
        } else {
            // Rakudo's default: relative 1e-6, or absolute 1e-5 when the expected value is tiny
            c = em < 1e-6 ? diff <= 1e-5 : diff / std::max(gm, em) <= 1e-6;
        }
        I.emitTest(c, desc);
        return Value::boolean(c);
    };
    B["throws-like"] = [](Interpreter& I, ValueList& a) -> Value {
        // throws-like BLOCK|Str, TYPE, matchers…, reason? — a SUBTEST in
        // Test.rakumod, and one here too: "code dies", "right exception type
        // (T)", then one ".KEY matches VALUE" per named matcher.
        //
        // It used to emit a single flat ok that measured only "something threw".
        // That cost twice over: the type and matcher assertions were never made
        // (20 of the 184 S03-operators/arith.t runs under Rakudo), and the
        // description came from a[2] — the slot the FIRST named matcher occupies
        // — so `numerator => 3, 'Modulo zero…'` reported itself as "numerator\t3".
        //
        // RAKUPP_LOOSE_THROWS_LIKE restores the measure-nothing behaviour for
        // pricing that policy against the Roast numbers. It still prints the
        // sub-TAP, but the type and matcher lines go out as SKIPs rather than as
        // claims — a run says what it did not check instead of implying it did.
        static const bool loose = std::getenv("RAKUPP_LOOSE_THROWS_LIKE") != nullptr;
        Value type; bool haveType = false;
        std::string reason;
        ValueList matchers;
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i].t == VT::Pair && a[i].namedArg) matchers.push_back(a[i]);
            else if (!haveType && a[i].t == VT::Type) { type = a[i]; haveType = true; }
            else if (a[i].t == VT::Str && reason.empty()) reason = a[i].s;
        }
        const std::string tname = haveType ? type.s : std::string("Exception");
        if (reason.empty()) reason = "did we throws-like " + tname + "?";
        // Rakudo's skip puts the marker in the DESCRIPTION slot ("ok 2 - # SKIP …"),
        // which is why these do not go through emitTest's directive argument.
        auto skipLine = [&](const std::string& why) { I.emitTest(true, "# SKIP " + why); };
        bool verdict = I.runSubtestFrame(reason, [&]() {
            const int n = 2 + (int)matchers.size();
            I.planned_ = n;   // the plan is printed BEFORE the code runs, as Test.rakumod's is
            std::cout << std::string(4 * I.subtestDepth_, ' ') << "1.." << n << "\n";
            bool threw = false; Value thrown;
            try {
                // the block's result is SUNK — `throws-like { run … }` throws through Proc.sink
                if (a.empty()) {}
                else if (a[0].t == VT::Code) I.sinkValue(I.callCallable(a[0], {}));
                else if (a[0].t == VT::Str) I.sinkValue(I.evalOwnScope(a[0].s));
                // …and anything else has ALREADY been evaluated, so what arrived
                // is whatever it produced: a Failure that has not detonated yet
                // is the throw this is asking about. Roast calls
                // `throws-like rindex(…), X::OutOfRange` in exactly that shape,
                // where Rakudo's own throws-like reaches the death by
                // stringifying the argument (Str sheet ST-27).
                else I.sinkValue(a[0]);
            } catch (RakuError& e) { threw = true; thrown = I.exceptionFor(e); }
            I.emitTest(threw, !a.empty() && a[0].t == VT::Str ? "'" + a[0].s + "' died" : "code dies");
            if (!threw) {  // nothing to inspect: every later assertion is skipped, not failed
                for (int k = 1; k < n; k++) skipLine("Code did not die, can not check exception");
                return;
            }
            // `Exception` matches whatever was thrown, so it is not worth a smartmatch.
            bool typeOk = true;
            if (loose) skipLine("right exception type (" + tname + ") not checked");
            else {
                typeOk = !haveType || tname == "Exception" || applyArith("~~", thrown, type).truthy();
                I.emitTest(typeOk, "right exception type (" + tname + ")");
            }
            for (auto& mp : matchers) {
                Value want = mp.pairVal() ? *mp.pairVal() : Value::any();
                std::string mdesc = "." + mp.s + " matches " + I.gistOf(want);
                if (!typeOk)  { skipLine("wrong exception type"); continue; }
                if (loose)    { skipLine(mdesc + " not checked"); continue; }
                // Rakudo lets a matcher naming an attribute the exception lacks
                // blow the whole subtest up ("No such method"); failing that one
                // assertion says the same thing and still fails the subtest, but
                // leaves the remaining matchers legible.
                Value got; bool have = true;
                try { got = I.methodCall(thrown, mp.s, ValueList{}); }
                catch (RakuError&) { have = false; }
                I.emitTest(have && (want.t == VT::Code ? I.callCallable(want, ValueList{got}).truthy()
                                                       : I.smartmatchValue("~~", got, want).truthy()), mdesc);
            }
        });
        return Value::boolean(verdict);
    };
    B["fails-like"] = [](Interpreter& I, ValueList& a) -> Value {
        // Like throws-like, but the code is expected to RETURN a Failure (a soft
        // `fail`) rather than throw outright. Either a returned Failure or a thrown
        // error counts as failing. (The exception-type / matcher args are accepted
        // but, as with throws-like, not deeply checked.)
        bool failed = false;
        // fails-like BLOCK, TYPE, matchers…, desc? — passes only when the block
        // RETURNS an UNHANDLED Failure whose exception matches TYPE and every
        // named matcher. A thrown exception is NOT a pass (that's throws-like);
        // neither is a Failure the block already handled (.so / .Bool).
        std::string desc;
        ValueList matchers;
        for (size_t i = 2; i < a.size(); i++) {
            if (a[i].t == VT::Pair && a[i].namedArg) matchers.push_back(a[i]);
            else if (a[i].t == VT::Str && desc.empty()) desc = a[i].s;
        }
        // Judge a returned Failure: its exception matches TYPE, and every named
        // matcher matches the exception's attribute — or, when the exception is
        // only a TYPE (`"msg".Failure` carries message/payload beside a bare
        // X::AdHoc; String::Utils' shorten answers that), the Failure's own slot.
        auto judge = [&](const Value& fh) -> bool {
            if (!(fh.t == VT::Hash && fh.hashKind == "Failure" && fh.hash())) return false;
            if (fh.hash()->count("handled") && (*fh.hash())["handled"].truthy()) return false;
            Value ex = fh.hash()->count("exception") ? (*fh.hash())["exception"] : Value::any();
            bool ok = true;
            if (a.size() > 1 && a[1].t == VT::Type && a[1].s != "Exception")
                ok = applyArith("~~", ex, a[1]).truthy();
            for (auto& mp : matchers) {
                if (!ok) break;
                Value want = mp.pairVal() ? *mp.pairVal() : Value::boolean(true);
                if (want.t == VT::Bool)
                    throw RakuError{Value::typeObj("X::Match::Bool"),
                        "Cannot use Bool as matcher for '" + mp.s + "'; did you mean to smartmatch the attribute?"};
                Value got; bool have = false;
                if (ex.t != VT::Object && fh.hash()->count(mp.s)) { got = (*fh.hash())[mp.s]; have = true; }
                if (!have) {
                    try { got = I.methodCall(ex, mp.s, ValueList{}); have = true; }
                    catch (RakuError&) { have = false; }
                }
                if (!have) { ok = false; break; }
                // a Regex matcher (`message => /…/`) needs the regex ENGINE, which
                // the value-only applyArith cannot reach — it answered False for
                // every one of them, so `fails-like …, message => /…/` never passed.
                ok = want.t == VT::Code ? I.callCallable(want, ValueList{got}).truthy()
                                        : I.smartmatchValue("~~", got, want).truthy();
            }
            return ok;
        };
        if (!a.empty()) {
            try {
                Value r;
                if (a[0].t == VT::Code) r = I.callCallable(a[0], {});
                else if (a[0].t == VT::Str) r = I.evalOwnScope(a[0].s);
                failed = judge(r);
            } catch (ReturnEx&) {
                // `fail` in a bare block unwinds as the return of the enclosing
                // routine — there is none inside the block, so it lands here.
                // Rakudo's verdict for that is "expected code to fail but it
                // threw": not a pass. (Left to propagate, it unwound the test
                // file's own mainline.)
                failed = false;
            } catch (RakuError& e) {
                if (e.payload.t == VT::Type && e.payload.s == "X::Match::Bool") throw; // matcher misuse propagates
                failed = false; // thrown exception: fails-like does not pass
            }
        }
        I.emitTest(failed, desc);
        return Value::boolean(failed);
    };
    registerBuiltinsPart2();
}

// registerBuiltins, continued. Split for compile time: each piece ends by
// calling the next, so the registrations run in the original order (a later
// one of the same name still replaces an earlier one).
// `:env` as Rakudo takes it — `.hash` of whatever was passed, not only a
// bare Hash. A Hash contributes its pairs, a Pair itself, and a list folds
// left to right with later keys winning, which is what makes
// `:env(%*ENV, K => V)` "the parent's environment plus one". Loose elements
// pair up consecutively, as `.hash` does everywhere else (the general
// coercion is the list -> Hash branch in MethodCallTail.cpp).
//
// False means "no environment can be read out of this", and the caller then
// leaves the child inheriting — which is what no :env at all does.
bool envPairsFrom(const Value& v, std::map<std::string, std::string>& out) {
    if (v.t == VT::Hash && v.hash()) {
        for (auto& kv : *v.hash()) out[kv.first] = kv.second.toStr();
        return true;
    }
    if (v.t == VT::Pair) {
        out[v.s] = v.pairVal() ? v.pairVal()->toStr() : "";
        return true;
    }
    if (v.t == VT::Array && v.arr()) {
        const ValueList& items = *v.arr();
        for (size_t i = 0; i < items.size(); i++) {
            if (envPairsFrom(items[i], out)) continue;      // a Hash, a Pair or a nested list
            if (i + 1 >= items.size())
                throw RakuError{Value::typeObj("X::Hash::Store::OddNumber"),
                                "Odd number of elements found where hash initializer expected"};
            std::string key = items[i].toStr();             // sequenced: ++i must not run before the key
            out[key] = items[++i].toStr();
        }
        return true;
    }
    return false;
}

// A large `combinations($n, $k)` is a lazy Seq that KNOWS its count:
// `+combinations(100, 70)` is the binomial C(100, 70) without building a single
// combination, and iterating it walks the index combinations in order.
static Value lazyIndexCombinations(long long n, long long k) {
    Value cnt = Value::integer(0);
    if (k >= 0 && k <= n) {
        cnt = Value::integer(1);
        const long long kk = std::min(k, n - k);
        for (long long i = 1; i <= kk; i++)   // C(n, i) = C(n, i-1) * (n-i+1) / i, exact
            cnt = applyArith("div", applyArith("*", cnt, Value::integer(n - kk + i)), Value::integer(i));
    }
    auto c = std::make_shared<std::vector<long long>>();
    auto started = std::make_shared<bool>(false);
    auto st = std::make_shared<LazySeqState>();
    st->infinite = true;      // never materialised whole
    st->hasCount = true; st->countVal = cnt;
    st->appendNext = [c, started, n, k](ValueList& cache) -> bool {
        if (k < 0 || k > n) return false;
        if (!*started) {
            *started = true;
            for (long long i = 0; i < k; i++) c->push_back(i);
        }
        else {
            long long i = k - 1;
            while (i >= 0 && (*c)[(size_t)i] == n - k + i) i--;
            if (i < 0) return false;
            (*c)[(size_t)i]++;
            for (long long j = i + 1; j < k; j++) (*c)[(size_t)j] = (*c)[(size_t)j - 1] + 1;
        }
        Value combo = Value::array(); combo.isList = true;
        for (long long v : *c) combo.arr()->push_back(Value::integer(v));
        cache.push_back(combo);
        return true;
    };
    Value out = Value::array(); out.isList = true; out.s = "Seq";
    out.extM() = st;
    return out;
}

static Value builtinMkdir(Interpreter& I, ValueList& a) {
    if (a.empty()) return Value::boolean(false);
    rejectNulPath(a[0].toStr()); // (the sub created the name truncated at the NUL; the method refused)
    std::string path = I.ioFsPath(a[0]);
    long long mode = 0777;   // mkdir($path, 0o700) — the sub's positional mode
    for (size_t i = 1; i < a.size(); i++) {
        if (a[i].t == VT::Pair && a[i].namedArg && a[i].s == "mode" && a[i].pairVal()) mode = a[i].pairVal()->toInt();
        else if (a[i].t == VT::Int) mode = a[i].toInt();
    }
    // mkdir -p, but HONEST about the outcome — the mirror of the method
    // arm in MethodCallPart3.cpp, which tells the story (issue #26): the
    // old form swallowed every error and answered success. Success is the
    // IO::Path (as Rakudo answers, not the Str this used to hand back);
    // failure is the soft X::IO::Mkdir Failure that detonates when sunk.
    std::string acc;
    int err = 0;
    // A prefix that is already a directory is fine whatever errno says:
    // Windows answers mkdir("C:") with EACCES, not EEXIST, and the walk
    // used to stop at the drive (issue #107). There `\` separates too.
    for (size_t i = 0; i <= path.size(); i++) {
#ifdef _WIN32
        const bool sep = i < path.size() && (path[i] == '/' || path[i] == '\\');
#else
        const bool sep = i < path.size() && path[i] == '/';
#endif
        if (i == path.size() || sep) {
            if (!acc.empty() && ::mkdir(acc.c_str(), (int)mode) != 0 && errno != EEXIST) {
                int e = errno;
                struct stat pst;
                if (!(::stat(acc.c_str(), &pst) == 0 && S_ISDIR(pst.st_mode))) {
                    err = e;
                    break;
                }
            }
            if (i < path.size()) acc += path[i];
        } else acc += path[i];
    }
    struct stat st;
    bool isDir = false;
    if (::stat(path.c_str(), &st) == 0) isDir = S_ISDIR(st.st_mode);
    else if (!err) err = errno;
    if (!isDir) {
        if (!err) err = EEXIST;   // the path exists, and is not a directory
        char ob[24]; snprintf(ob, sizeof ob, "0o%llo", (unsigned long long)mode);
        return I.ioFailure("X::IO::Mkdir",
                           {{"path", Value::str(path)},
                            {"mode", Value::integer(mode)},
                            {"os-error", Value::str(std::string("Failed to mkdir: ") + std::strerror(err))}},
                           "Failed to create directory '" + path + "' with mode '" + std::string(ob) +
                           "': Failed to mkdir: " + std::strerror(err));
    }
    Value p = Value::str(path); p.hashKind = "IO";
    p.ofTypeM() = I.cwdName();
    return p;
}

// The program's standard input: $*IN, whatever it is now.
static Value stdinNow(Interpreter& I) {
    Value* slot = Interpreter::findDynamicLenient("$*IN");
    return slot ? *slot : I.dynVar("$*IN");
}

// `open('-')` is $*IN and `open('-', :w)` $*OUT (openStdStream). 6.d
// deprecates the spelling; an IO::Path '-' is reported as Rakudo reports it,
// as the path and as the handle opened on it. False for any other path.
static bool openDash(Interpreter& I, ValueList& a, Value& out) {
    for (auto& x : a) {
        if (x.t == VT::Pair) continue;
        if (!(x.t == VT::Str && (x.hashKind.empty() || x.hashKind == "IO") && x.toStr() == "-")) return false;
        if (I.langRev_ >= 1) {
            const char* with = "$*IN or $*OUT";
            if (x.hashKind == "IO") {
                I.noteDeprecation("", "\"-\".IO", "", with, I.testLine());
                I.noteDeprecation("", "IO::Handle.new(:path(\"-\"))", "", with, I.testLine());
            }
            else I.noteDeprecation("", "open(\"-\")", "", with, I.testLine());
        }
        out = openStdStream(I, Value(), a);
        return true;
    }
    return false;
}

void Interpreter::registerBuiltinsPart2() {
    auto& B = builtins_;
    // (the EVAL moves the current line into its own text: a failure is reported
    // at the CALLER's line, where the test is written)
    // Rakudo's Test runs the string inside a ROUTINE of its own
    // (`eval_exception`), so a `fail` — or a `return` — at the string's top
    // level returns from THAT, and the helper judges the value it hands back:
    // defined is "died", undefined "lived". `eval-lives-ok 'map -> $x, $y {
    // ... }, 1..6'` lives there because a stub FAILS (advent2009-day20.t).
    // True when the code returned something (into `returned`).
    static const auto evalAsRoutine = [](Interpreter& I, const std::string& code, Value& returned) {
        ExecContext& t = I.tctx_;
        ++t.frameTop;
        struct Frame {
            ExecContext& t; uint64_t top, rf;
            Frame(ExecContext& x) : t(x), top(x.frameTop), rf(x.curRoutineFrame) { x.curRoutineFrame = x.frameTop; }
            ~Frame() { t.frameTop = top - 1; t.curRoutineFrame = rf; }
        } frame{t};
        try { I.evalOwnScope(code); }
        catch (ReturnEx& r) { returned = r.v; return true; }
        if (t.returning) { t.returning = false; returned = t.returnV; return true; }
        return false;
    };
    B["eval-lives-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool lived = true;
        const int line = I.curLine_;
        std::string why;
        try {
            Value rv;
            if (!a.empty() && evalAsRoutine(I, a[0].toStr(), rv) && rtIsDefined(rv)) {
                lived = false;
                why = rv.toStr();
            }
        }
        catch (RakuError& e) {
            lived = false;
            why = e.message;
            if (why.rfind("EVAL parse error: ", 0) == 0) why = why.substr(18);
        }
        I.curLine_ = line;
        I.emitTest(lived, a.size() > 1 ? a[1].toStr() : "");
        if (!lived) std::cerr << "# Error: " << why << "\n";   // Rakudo's diag of what died
        return Value::boolean(lived);
    };
    B["eval-dies-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool died = false;
        const int line = I.curLine_;
        try {
            Value rv;
            if (!a.empty() && evalAsRoutine(I, a[0].toStr(), rv)) died = rtIsDefined(rv);
        }
        catch (RakuError&) { died = true; }
        I.curLine_ = line;
        I.emitTest(died, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(died);
    };
    B["EVAL"] = [](Interpreter& I, ValueList& a) -> Value {
        Value code; bool haveCode = false, checkOnly = false;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.s == "check") {
                checkOnly = v.pairVal() ? v.pairVal()->truthy() : true;
            } else if (v.t == VT::Pair && v.s == "lang") {
                std::string lang = v.pairVal() ? v.pairVal()->toStr() : "";
                if (lang != "Raku" && lang != "Perl6")
                    // the payload slot takes the exception TYPE, not a message —
                    // a Str payload only promotes to a real exception when it
                    // starts with "X::", so this used to surface as a bare Str
                    I.throwTypedV("X::Eval::NoSuchLang", {{"lang", Value::str(lang)}},
                                  "No compiler available for language '" + lang + "'");
            } else if (v.t != VT::Pair && !haveCode) { code = v; haveCode = true; }
        }
        if (!haveCode) return Value::any();
        // A Callable is not a program. `EVAL { ... }` was Perl 5's block eval,
        // and in Raku it is almost always a `try` that lost its keyword — so
        // Rakudo names that rather than stringifying the block and compiling
        // its gist (roast S29-context/eval.t, "block EVAL is gone").
        if (code.t == VT::Code)
            throw RakuError{Value::typeObj("X::AdHoc"),
                "EVAL() in Raku is intended to evaluate strings or ASTs, "
                "did you mean 'try'?"};
        // `EVAL $node` — the SUB form over a RakuAST tree, which is how
        // Intl::Format::Number runs the formatters it builds
        // (`EVAL format-number-rakuast |c`). Rakudo takes a node here as
        // readily as a string; without this arm the node would be stringified
        // and its gist compiled, which is a different program or none at all.
        if (code.t == VT::Object && code.obj() && code.obj()->cls &&
            isRakuAstName(code.obj()->cls->name))
            return rakuAstEval(I, code);
        // control flow may not escape an EVAL: a top-level `return`/`next`/… in
        // the string is X::ControlFlow, not a silent unwind of the whole program
        // evalString itself converts escaping control flow (routine-aware),
        // and the unit is a lexical scope of its own (see evalOwnScope)
        return I.evalOwnScope(code.toStr(), checkOnly);
    };
    // EVALFILE($path, :$lang) — Rakudo's is `EVAL slurp($filename), :$lang`,
    // so the file is read first (a missing one dies before the language is
    // looked at) and every adverb rides through to EVAL unchanged.
    B["EVALFILE"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string path; bool havePath = false;
        ValueList fwd;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.namedArg) { fwd.push_back(v); continue; }
            if (!havePath) { path = I.ioFsPath(v); havePath = true; }
        }
        if (!havePath) return Value::any();
        rejectNulPath(path);
        std::ifstream in(path);
        if (!in) throwFailedOpen(path);
        std::ostringstream ss; ss << in.rdbuf();
        std::string text = ss.str();
        if (text.find('\r') != std::string::npos) {   // same CRLF -> LF as slurp
            std::string outT; outT.reserve(text.size());
            for (size_t i = 0; i < text.size(); i++) {
                if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
                outT += text[i];
            }
            text.swap(outT);
        }
        ValueList args; args.push_back(Value::str(text));
        for (auto& n : fwd) args.push_back(n);
        // while this file's top level runs it IS the file being compiled: a
        // routine declared in it records the path in its .declFile, and a
        // backtrace taken from its mainline names the path rather than the
        // program that EVALFILE'd it (S29-context/evalfile.t asserts exactly
        // that). The path goes in AS GIVEN — Rakudo's backtrace does not
        // absolutise it. Same RAII switch a module load makes.
        struct DFGuard { std::string& f; std::string s; ~DFGuard() { f = s; } }
            dfG{I.curDeclFile_, I.curDeclFile_};
        I.curDeclFile_ = path;
        return I.callBuiltin("EVAL", args);
    };
    // Default @*ARGS -> argument-list conversion (the built-in ARGS-TO-CAPTURE).
    // main-refactored.t adjudicates the rules; see rakuppMainCapture below.
    B["RUN-MAIN-args-to-capture"] = [](Interpreter& I, ValueList& a) -> Value {
        // The DEFAULT ARGS-TO-CAPTURE: parse the live @*ARGS per the CLI rules
        // main-refactored.t adjudicates. Rakudo drives the real command line
        // and an explicit RUN-MAIN through the same default-args-to-capture,
        // so this delegates to the same rtMainArgs as the interpreter's MAIN
        // auto-invoke (--name / --name=v / -n=v / :n=v named, repeats collect,
        // values AND positionals val()-allomorphed, `--` consumed with the
        // rest positional, --/name negates, the named-anywhere opt).
        (void)a;
        auto dynFind = [&](const char* n) -> Value* {
            if (Value* p = I.tctx_.cur->find(n)) return p;
            for (auto it = I.tctx_.dynStack.rbegin(); it != I.tctx_.dynStack.rend(); ++it)
                if (*it) if (Value* p = (*it)->find(n)) return p;
            return nullptr;
        };
        std::vector<std::string> argv;
        if (Value* av = dynFind("@*ARGS"))
            if (av->t == VT::Array && av->arr())
                for (auto& x : *av->arr()) argv.push_back(x.toStr());
        bool namedAnywhere = false;
        if (Value* smo = dynFind("%*SUB-MAIN-OPTS"))
            if (smo->t == VT::Hash && smo->hash()) {
                auto it = smo->hash()->find("named-anywhere");
                namedAnywhere = it != smo->hash()->end() && it->second.truthy();
            }
        ValueList margs = rtMainArgs(argv, namedAnywhere, &I);
        Value cap = Value::array(); cap.hashKind = "Capture"; *cap.arr() = std::move(margs);
        return cap;
    };
    // RUN-MAIN(&main, $mainline-result) — the 2018.10 command-line protocol:
    //   @*ARGS -> ARGS-TO-CAPTURE -> dispatch &main -> on failure GENERATE-USAGE
    //   (or the legacy USAGE), then exit through &*EXIT.
    // A user sub of any of those names, visible where RUN-MAIN is called,
    // REPLACES the built-in step, and is also what &*ARGS-TO-CAPTURE /
    // &*GENERATE-USAGE answer inside it. MAIN_HELPER is never called.
    // the built-in composers by their operator names: `circumfix:<[ ]>(1, 2)`
    // is `[1, 2]` (single-argument rule), `circumfix:<{ }>(…)` a Hash
    B["circumfix:<[ ]>"] = [](Interpreter& I, ValueList& a) -> Value {
        Value out = Value::array();
        if (a.size() == 1 && (a[0].t == VT::Array || a[0].t == VT::Range) && !a[0].itemized)
            *out.arr() = a[0].flatten();
        else for (auto& x : a) out.arr()->push_back(x);
        (void)I; return out;
    };
    B["circumfix:<{ }>"] = [](Interpreter& I, ValueList& a) -> Value {
        Value h = Value::makeHash();
        ValueList flat;
        for (auto& x : a) { if (x.t == VT::Array && x.arr()) for (auto& e : *x.arr()) flat.push_back(e); else flat.push_back(x); }
        for (size_t k = 0; k < flat.size(); k++) {
            if (flat[k].t == VT::Pair) (*h.hash())[flat[k].s] = flat[k].pairVal() ? *flat[k].pairVal() : Value::any();
            else if (k + 1 < flat.size()) { (*h.hash())[flat[k].toStr()] = flat[k + 1]; k++; }
        }
        (void)I; return h;
    };
    B["RUN-MAIN"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty() || a[0].t != VT::Code) return Value::any();
        Value mainSub = a[0];
        auto lookup = [&](const char* n) -> Value* {
            if (Value* p = I.tctx_.cur->find(n)) return p;
            for (auto it = I.tctx_.dynStack.rbegin(); it != I.tctx_.dynStack.rend(); ++it)
                if (*it) if (Value* p = (*it)->find(n)) return p;
            return nullptr;
        };
        // A 6.c unit may carry its own pre-2018.06 MAIN_HELPER: RUN-MAIN hands
        // the whole job to it — `MAIN_HELPER($retval)`, or from 2018.06 on
        // `MAIN_HELPER($in-as-argsfiles, $retval)` — and MAIN is not called.
        if (Value* mh = I.tctx_.cur ? I.tctx_.cur->find("&MAIN_HELPER") : nullptr)
            if (mh->t == VT::Code && mh->code() &&
                I.methodCall(*mh, "count", ValueList{}).toInt() >= 1) {   // a helper TAKES the retval
                Value helper = *mh;
                long long n = I.methodCall(helper, "count", ValueList{}).toInt();
                Value retval = a.size() > 1 ? a[1] : Value::nil();
                return n >= 2 ? I.callCallable(helper, ValueList{Value::boolean(false), retval})
                              : I.callCallable(helper, ValueList{retval});
            }
        // the raw @*ARGS as an Array, for the ARGS-TO-CAPTURE hook
        Value argsArr = Value::array();
        if (Value* av = lookup("@*ARGS"))
            if (av->t == VT::Array && av->arr()) *argsArr.arr() = *av->arr();

        // --- 1. args -> capture -------------------------------------------
        Value userA2C;
        if (Value* p = lookup("&*ARGS-TO-CAPTURE")) userA2C = *p;
        if (userA2C.t != VT::Code) if (Value* p = lookup("&ARGS-TO-CAPTURE")) userA2C = *p;
        ValueList margs;
        {   // &*ARGS-TO-CAPTURE is the sub actually in force while it runs
            if (userA2C.t == VT::Code) {
                // &*ARGS-TO-CAPTURE names the sub actually in force while it runs
                auto scope = std::make_shared<Env>(); scope->parent = I.tctx_.cur;
                scope->define("&*ARGS-TO-CAPTURE", userA2C);
                auto saved = I.tctx_.cur; I.tctx_.cur = scope;
                I.tctx_.dynStack.push_back(scope.get());
                struct R { Interpreter& I; std::shared_ptr<Env> s; ~R(){ I.tctx_.cur = s; I.tctx_.dynStack.pop_back(); } } r{I, saved};
                Value cap = I.callCallable(userA2C, ValueList{mainSub, argsArr});
                if (cap.t == VT::Array && cap.arr()) {
                    margs = *cap.arr();                       // a Capture IS the arg list…
                    for (auto& m : margs)                   // …its Pairs are NAMED args
                        if (m.t == VT::Pair) m.namedArg = true;
                } else if (cap.t != VT::Any && cap.t != VT::Nil) margs.push_back(cap);
            } else {
                Value cap = I.callBuiltin("RUN-MAIN-args-to-capture", ValueList{});
                if (cap.t == VT::Array && cap.arr()) margs = *cap.arr();
            }
        }
        // did the user ask for help? (drives the exit code)
        bool wantsHelp = false;
        for (auto& m : margs) if (m.t == VT::Pair && m.s == "help" && m.namedArg &&
                                  (!m.pairVal() || m.pairVal()->truthy())) wantsHelp = true;

        // --- 2. dispatch --------------------------------------------------
        bool matches = true;
        if (mainSub.code() && mainSub.code()->isMultiDispatcher) {
            matches = false;
            for (auto& cand : mainSub.code()->candidates)
                if (I.scoreCandidate(cand, margs) >= 0) { matches = true; break; }
        } else if (mainSub.code() && mainSub.code()->params) {
            matches = I.scoreCandidate(mainSub, margs) >= 0;
        }
        if (matches) return I.callCallable(mainSub, margs);

        // --- 3. no candidate: usage ---------------------------------------
        Value userGU;
        if (Value* p = lookup("&*GENERATE-USAGE")) userGU = *p;
        if (userGU.t != VT::Code) if (Value* p = lookup("&GENERATE-USAGE")) userGU = *p;
        Value usageText;
        if (userGU.t == VT::Code) {
            Value inForce = userGU;
            auto scope = std::make_shared<Env>(); scope->parent = I.tctx_.cur;
            scope->define("&*GENERATE-USAGE", inForce);
            auto saved = I.tctx_.cur; I.tctx_.cur = scope;
            I.tctx_.dynStack.push_back(scope.get());
            struct R { Interpreter& I; std::shared_ptr<Env> s; ~R(){ I.tctx_.cur = s; I.tctx_.dynStack.pop_back(); } } r{I, saved};
            ValueList ga{mainSub};
            for (auto& m : margs) ga.push_back(m);
            usageText = I.callCallable(userGU, ga);
        } else if (Value* p = lookup("&USAGE"); p && p->t == VT::Code) {
            I.callCallable(*p, ValueList{});   // the legacy hook PRINTS; it returns nothing useful
        } else {
            usageText = Value::str(I.mainUsage());
        }
        if (usageText.t == VT::Str && !usageText.s.empty())
            (wantsHelp ? std::cout : std::cerr) << usageText.s << "\n";

        // --- 4. exit through &*EXIT (so a test can intercept) --------------
        Value code = Value::integer(wantsHelp ? 0 : 2);
        if (Value* e = lookup("&*EXIT"); e && e->t == VT::Code)
            return I.callCallable(*e, ValueList{code});
        throw ExitEx{(int)code.toInt()};
    };
    B["samemark"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return a.empty() ? Value::any() : a[0];
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "samemark", rest);
    };
    B["exit"] = [](Interpreter& I, ValueList& a) -> Value {
        int code = (int)(a.empty() ? 0 : a[0].toInt());
        // `exit` ends the PROCESS, wherever it is called from. Thrown on a worker
        // thread the ExitEx would only break that thread's Promise, leaving the
        // main thread to run on — `start { sleep 3; exit }` has to stop everything.
        if (std::this_thread::get_id() != I.mainThreadId()) {
            std::cout.flush(); std::cerr.flush();
            _exit(code);
        }
        throw ExitEx{code};
    };
    // stub / yada operators
    B["!!!"] = [](Interpreter&, ValueList& a) -> Value { throw RakuError{Value::typeObj("X::StubCode"), a.empty() ? "Stub code executed" : a[0].toStr()}; };
    // `...` FAILS, as Rakudo's does: the routine it stands in answers a Failure
    // carrying X::StubCode, which throws when it is used or sunk — and with no
    // routine around it, it throws where it stands. (`!!!` always throws.)
    B["..."] = [](Interpreter& I, ValueList& a) -> Value {
        const std::string msg = a.empty() ? std::string("Stub code executed") : a[0].toStr();
        ValueList fa{I.makeTypedEx("X::StubCode", {}, msg)};
        return I.callBuiltin("fail", fa);
    };
    // `???` WARNS — through `warn`, so a CONTROL block sees the CX::Warn
    B["???"] = [](Interpreter& I, ValueList& a) -> Value {
        ValueList wa{Value::str(a.empty() ? std::string("Stub code executed") : a[0].toStr())};
        return I.callBuiltin("warn", wa);
    };
    // run(prog, *@args, :timeout(N)) -> { out => Str, exitcode => Int, timedout => Bool }
    // `:out($fh)` / `:err($fh)` on run() and shell() alike: the adverb is not a
    // flag but a SINK. Read as a mere boolean it captures the stream and drops
    // it, which is how HTTP::Tinyish::Curl fetched every page as an empty body,
    // and how App::RaCoCo's `shell(…, :out($fh))` wrote an empty file.
    static const auto asSink = [](const Value* pv) {
        return pv && ((pv->t == VT::Hash && pv->hashKind == "FileHandle") || pv->t == VT::Object);
    };
    // The child has already finished, so this is a copy rather than a live
    // redirection — the handle sees the whole stream at once, in order, which is
    // what a caller that closes and reads the file afterwards wants.
    static const auto drainTo = [](Interpreter& I, Value& sink, bool have, const std::string& text) {
        if (!have || text.empty()) return;
        ValueList pa{Value::str(text)};
        I.methodCall(sink, "print", pa);
        // …and FLUSH it. Rakudo hands the child the handle's own descriptor, so
        // the bytes are in the file the moment the child exits; a buffered
        // `print` here is not on disk until the handle closes. App::RaCoCo
        // slurps the file with the handle still open (`will leave { .close }`)
        // and read an empty one.
        if (sink.t == VT::Hash && sink.hashKind == "FileHandle") {
            ValueList none;
            I.methodCall(sink, "flush", none);
        }
    };
    B["run"] = [](Interpreter& I, ValueList& a) -> Value {
        std::vector<std::string> argv; bool wantOut = false, wantIn = false, wantErr = false;
        int outMode = -1, errMode = -1; // -1 unspecified (inherit/echo), 0 :!x (discard), 1 :x (capture)
        int inFd = -1; bool haveInHandle = false; // `:in($handle)`: the child's stdin itself
        Value inFrom;   // `:in($other.out)` of a child still waiting to run
        std::vector<std::string> envKV; bool haveEnv = false; std::string cwd;
        double timeoutSec = 0;
        // `:merge` — stdout and stderr as ONE captured stream, read back through
        // `.out`. It was not parsed at all, so it fell through as an unknown
        // named argument: nothing was captured, the child wrote straight to our
        // own descriptors, and `.out.slurp` came back EMPTY. Every suite that
        // checks a script's combined output this way saw "" against whatever it
        // expected (as-cli-arguments' twelve tests, and the eight dists behind it).
        bool merge = false;
        Value outSink, errSink;
        bool binPipes = false;
        // A default-constructed Value is Any, not Nil — "was a sink given?" needs
        // its own flag, and testing `.t == VT::Nil` for it (as this did) answered
        // "yes" for every un-adverbed run.
        bool haveOutSink = false, haveErrSink = false;
        for (auto& v : flattenArgs(a)) {
            if (v.t == VT::Pair) {
                if (v.s == "out") { wantOut = v.pairVal() ? v.pairVal()->truthy() : true; outMode = wantOut ? 1 : 0;
                                    if (asSink(v.pairVal())) { outSink = *v.pairVal(); haveOutSink = true; } }
                else if (v.s == "err") { wantErr = v.pairVal() ? v.pairVal()->truthy() : true; errMode = wantErr ? 1 : 0;
                                    if (asSink(v.pairVal())) { errSink = *v.pairVal(); haveErrSink = true; } }
                else if (v.s == "in") {
                    // the `.out` of a child that has not RUN yet (`:in($sh1.out)`
                    // while $sh1 still waits on its own stdin): this one waits
                    // too, and takes that output as its input when it runs
                    if (v.pairVal() && v.pairVal()->t == VT::Hash && v.pairVal()->hash() &&
                        v.pairVal()->hash()->count("proc-owner")) {
                        Value owner = (*v.pairVal()->hash())["proc-owner"];
                        if (owner.t == VT::Hash && owner.hash() && owner.hash()->count("deferred") &&
                            !owner.hash()->count("ran")) {
                            inFrom = owner; wantIn = true;
                            continue;
                        }
                    }
                    // a HANDLE is the child's stdin (stdinFdForHandle); a Bool
                    // keeps the deferred piped mode below
                    bool resolved = false;
                    int fd = v.pairVal() ? stdinFdForHandle(*v.pairVal(), resolved) : -1;
                    if (resolved) { inFd = fd; haveInHandle = true; }
                    else wantIn = v.pairVal() ? v.pairVal()->truthy() : true;
                }
                // :timeout(N) — a rakupp extension (Rakudo's run has no such
                // adverb): SIGKILL the child's process group after N seconds.
                // Advertised in this builtin's comment since the initial
                // commit, but never actually parsed — every caller silently
                // ran unbounded, which the stress suite discovered when a
                // livelocked child pinned the machine for 25 minutes.
                else if (v.s == "timeout" && v.pairVal()) timeoutSec = v.pairVal()->toNum();
                else if (v.s == "env" && v.pairVal()) {
                    // :env(...) — the child's ENTIRE environment (Rakudo
                    // semantics). Only a bare Hash was accepted here; every
                    // other spelling fell past this branch and was dropped in
                    // SILENCE, so `:env(PROBE => 'M')` — one variable and
                    // nothing else, in Rakudo — handed the child the parent's
                    // whole environment instead. A child meant to run isolated
                    // was not isolated, and nothing said so.
                    std::map<std::string, std::string> env;  // ordered: later keys win, and the block wants sorting anyway
                    if (envPairsFrom(*v.pairVal(), env)) {
                        haveEnv = true;
                        envKV.clear();
                        for (auto& kv : env) envKV.push_back(kv.first + "=" + kv.second);
                    }
                }
                else if (v.s == "cwd" && v.pairVal()) cwd = v.pairVal()->toStr(); // was silently ignored too
                else if (v.s == "merge") merge = v.pairVal() ? v.pairVal()->truthy() : true;
                // :bin — the pipes carry bytes: `.out.slurp` is a Buf, not a Str
                else if (v.s == "bin") binPipes = v.pairVal() ? v.pairVal()->truthy() : true;
            }
            else argv.push_back(v.toStr());
        }
        // `:merge` implies capture — the merged stream is delivered through
        // `.out`, so there is nothing to merge into unless stdout is captured —
        // and it WINS over an explicit `:err`. Rakudo sends both streams to
        // `.out` for `run(:merge, :err)`; keeping them apart there was this
        // implementation's own invention, and the cross-engine probe caught it.
        if (merge) { if (outMode == -1) { outMode = 1; wantOut = true; } errMode = -1; }
        Value av = Value::array(); av.isList = true; for (auto& s : argv) av.arr()->push_back(Value::str(s));
        Value p = Value::makeHash(); p.hashKind = "Proc"; // standard Proc object
        if (binPipes) (*p.hash())["bin"] = Value::boolean(true);
        (*p.hash())["argv"] = av; // for .command
        I.syncEnvToProcess(); // child inherits any %*ENV changes the program made
#if !defined(_WIN32)
        // A PIPE asked for — `:in`, `:out` or `:err` — and the child runs from
        // here over live pipes, as Rakudo's does: `run` returns at once, `.in`
        // writes reach the child as they are made, and `.out.get` / `.lines`
        // read what it has written so far (see liveProcStart). A sink (`:out($fh)`),
        // a `:timeout` or an input still waiting on another child keep the
        // captured path below.
        {
            const bool pipeIn = wantIn && !haveInHandle && inFrom.t != VT::Hash;
            const bool pipeOut = outMode == 1, pipeErr = errMode == 1;
            if ((pipeIn || pipeOut || pipeErr) && !haveOutSink && !haveErrSink && timeoutSec <= 0 &&
                !argv.empty()) {
                SpawnStdio io;
                io.stdinFd = inFd;
                io.captureOut = pipeOut;
                io.outToNull = outMode == 0;
                io.captureErr = pipeErr;
                io.errToNull = errMode == 0;
                io.mergeErr = merge;
                long long pid = 0; std::string spawnErr;
                long long tok = liveProcStart(argv, cwd, haveEnv ? &envKV : nullptr, io, pipeIn, pid, spawnErr);
                if (tok) {
                    if (inFd >= 0) ::close(inFd);    // the child holds its own copy
                    (*p.hash())["live-tok"] = Value::integer(tok);
                    (*p.hash())["pid"] = Value::integer(pid);
                    if (pipeOut) (*p.hash())["live-out"] = Value::boolean(true);
                    if (pipeErr) (*p.hash())["live-err"] = Value::boolean(true);
                    (*p.hash())["out-str"] = Value::str("");
                    (*p.hash())["err-str"] = Value::str("");
                    return p;
                }
                // could not start: the captured path below reports why
            }
        }
#endif
        if (wantIn && !haveInHandle) {
            // Defer spawning: the process runs when its stdin is written via
            // `.in.spurt(...)`, so we can feed input and capture output together.
            (*p.hash())["deferred"] = Value::boolean(true);
            if (haveEnv) { Value ev = Value::array(); for (auto& kv : envKV) ev.arr()->push_back(Value::str(kv)); (*p.hash())["env-kv"] = ev; }
            if (!cwd.empty()) (*p.hash())["cwd"] = Value::str(cwd);
            // The spawn happens later, in ProcIn's `print`/`close`, so what the
            // adverbs asked for has to travel with the Proc. Without them that
            // path captured stdout and sent stderr to /dev/null whatever was
            // written, so `run(cmd, :in, :out, :err)` read back an empty `.err`.
            (*p.hash())["out-mode"] = Value::integer(outMode);
            (*p.hash())["err-mode"] = Value::integer(errMode);
            (*p.hash())["out-str"] = Value::str("");
            (*p.hash())["err-str"] = Value::str("");
            (*p.hash())["exitcode"] = Value::integer(0);
            if (inFrom.t == VT::Hash) {
                (*p.hash())["in-from"] = inFrom;
                (*p.hash())["pending-in"] = Value::str("");   // reading its output runs it
            }
            return p;
        }
        std::string out, err; int code; bool timedout;
        // :err captures; :!err captures-and-discards (so probes like
        // `zrun('git','--help', :!out, :!err)` stay silent); unspecified inherits.
        long long childPid = 0;
        // No `:out` (and no sink to fill): the child gets OUR stdout, so its
        // output appears as it is produced. Capturing it and echoing at exit —
        // what this used to do — made every streaming child silent until it
        // finished (issue #51: a runner relaying a build's progress).
        int outSpawn = (outMode == -1 && !haveOutSink) ? -1 : (outMode == 0 ? 0 : 1);
        std::string spawnErr;   // set only when the program could not be started at all
        spawnCapture(argv, timeoutSec, out, code, timedout, &I, errMode != -1 ? &err : nullptr, cwd, &childPid,
                     haveEnv ? &envKV : nullptr, errMode == -1, outSpawn, nullptr, nullptr, inFd,
                     merge, &spawnErr);
#if !defined(_WIN32)
        if (inFd >= 0) ::close(inFd); // the child holds its own copy
#endif
        // Deliver a redirected stream to its handle.
        drainTo(I, outSink, haveOutSink, out);
        drainTo(I, errSink, haveErrSink, err);
        // Neither stream needs echoing any more: an un-adverbed child wrote to
        // our own descriptors while it ran.
        storeProcStatus(p, code); // exitcode + signal
        // A command that never started reports exit code -1 — not the 127 the
        // stillborn child happened to exit with — and carries the reason, which
        // the sink appends as Rakudo's `(OS error = …)` line.
        if (!spawnErr.empty()) (*p.hash())["os-error"] = Value::str(spawnErr);
        (*p.hash())["out-str"] = Value::str(out);
        (*p.hash())["err-str"] = Value::str(err);
        (*p.hash())["timedout"] = Value::boolean(timedout); // the shape the comment above promises
        if (childPid) (*p.hash())["pid"] = Value::integer(childPid);
        return p;
    };
    // shell(CMD, :out, :err) — run CMD through the system shell (`/bin/sh -c CMD`),
    // so redirections/pipes in CMD work. Returns a Proc; +$proc is the exit status.
    B["shell"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string cmd; bool wantOut = false, wantErr = false, merge = false;
        bool binPipes = false;
        int outMode = -1, errMode = -1; // -1 unspecified, 0 :!x discard, 1 :x capture
        int inFd = -1; // `:in($handle)`: the child's stdin itself (a Bool `:in` is not a shell() mode)
        std::vector<std::string> envKV; bool haveEnv = false; std::string cwd;
        Value outSink, errSink; bool haveOutSink = false, haveErrSink = false;
        for (auto& v : flattenArgs(a)) {
            if (v.t == VT::Pair) {
                if (v.s == "out") { wantOut = v.pairVal() ? v.pairVal()->truthy() : true; outMode = wantOut ? 1 : 0;
                                    if (asSink(v.pairVal())) { outSink = *v.pairVal(); haveOutSink = true; } }
                else if (v.s == "err") { wantErr = v.pairVal() ? v.pairVal()->truthy() : true; errMode = wantErr ? 1 : 0;
                                    if (asSink(v.pairVal())) { errSink = *v.pairVal(); haveErrSink = true; } }
                else if (v.s == "in" && v.pairVal()) { bool resolved = false; int fd = stdinFdForHandle(*v.pairVal(), resolved); if (resolved) inFd = fd; }
                else if (v.s == "merge") merge = v.pairVal() ? v.pairVal()->truthy() : true; // as in run(), above
                else if (v.s == "bin") binPipes = v.pairVal() ? v.pairVal()->truthy() : true;
                // :cwd — where the command runs. Parsed by run() since it was
                // first reported and never here, so it was accepted and
                // ignored: the command ran in THIS process's directory, and
                // a :cwd naming a directory that does not exist ran it anyway
                // and exited 0, which is the answer a caller is least able to
                // notice.
                else if (v.s == "cwd" && v.pairVal()) cwd = v.pairVal()->toStr();
                // :env — the same adverb run() takes, and the same silence when
                // it was missing: shell() did not parse it at all, so a command
                // handed a deliberate environment got this process's instead.
                else if (v.s == "env" && v.pairVal()) {
                    std::map<std::string, std::string> env;
                    if (envPairsFrom(*v.pairVal(), env)) {
                        haveEnv = true;
                        envKV.clear();
                        for (auto& kv : env) envKV.push_back(kv.first + "=" + kv.second);
                    }
                }
            }
            else if (cmd.empty()) cmd = v.toStr();
        }
        // `shell` runs its argument through the SYSTEM command processor, which is
        // cmd.exe on Windows — `/bin/sh` does not exist there, so every shell()
        // call failed with exitcode -1 and no output (issue #10).
#if defined(_WIN32)
        const char* comspec = std::getenv("COMSPEC");
        std::vector<std::string> argv = {comspec && *comspec ? comspec : "cmd.exe", "/c", cmd};
#else
        std::vector<std::string> argv = {"/bin/sh", "-c", cmd};
#endif
        I.syncEnvToProcess(); // child inherits any %*ENV changes the program made
        std::string out, err; int code = 0; bool timedout = false;
        long long childPid = 0;
        if (merge) { if (outMode == -1) { outMode = 1; wantOut = true; } errMode = -1; } // as in run(), above
#if !defined(_WIN32)
        // `:out` / `:err` pipes are live, as in run() above
        if ((outMode == 1 || errMode == 1) && !haveOutSink && !haveErrSink) {
            SpawnStdio io;
            io.stdinFd = inFd;
            io.captureOut = outMode == 1; io.outToNull = outMode == 0;
            io.captureErr = errMode == 1; io.errToNull = errMode == 0;
            io.mergeErr = merge;
            long long pid = 0; std::string spawnErr;
            long long tok = liveProcStart(argv, cwd, haveEnv ? &envKV : nullptr, io, false, pid, spawnErr);
            if (tok) {
                if (inFd >= 0) ::close(inFd);
                Value p = Value::makeHash(); p.hashKind = "Proc";
                Value av = Value::array(); av.isList = true; av.arr()->push_back(Value::str(cmd));
                if (binPipes) (*p.hash())["bin"] = Value::boolean(true);
                (*p.hash())["argv"] = av;
                (*p.hash())["live-tok"] = Value::integer(tok);
                (*p.hash())["pid"] = Value::integer(pid);
                if (outMode == 1) (*p.hash())["live-out"] = Value::boolean(true);
                if (errMode == 1) (*p.hash())["live-err"] = Value::boolean(true);
                (*p.hash())["out-str"] = Value::str("");
                (*p.hash())["err-str"] = Value::str("");
                return p;
            }
        }
#endif
        int outSpawn = (outMode == -1 && !haveOutSink) ? -1 : (outMode == 0 ? 0 : 1);
        spawnCapture(argv, 0, out, code, timedout, &I, errMode != -1 ? &err : nullptr, cwd, &childPid,
                     haveEnv ? &envKV : nullptr, errMode == -1, outSpawn, nullptr, nullptr, inFd,
                     merge);  // no `:out`: the child writes to ours, live
#if !defined(_WIN32)
        if (inFd >= 0) ::close(inFd);
#endif
        drainTo(I, outSink, haveOutSink, out);
        drainTo(I, errSink, haveErrSink, err);
        Value p = Value::makeHash(); p.hashKind = "Proc";
        Value av = Value::array(); av.isList = true; av.arr()->push_back(Value::str(cmd));
        if (binPipes) (*p.hash())["bin"] = Value::boolean(true);
        (*p.hash())["argv"] = av; // .command — shell reports the command string
        storeProcStatus(p, code); // exitcode + signal
        (*p.hash())["out-str"] = Value::str(out);
        (*p.hash())["err-str"] = Value::str(err);
        if (childPid) (*p.hash())["pid"] = Value::integer(childPid);
        return p;
    };
    // full-barrier — a sequentially-consistent memory fence (the ⚛ family's
    // companion; under the GIL it is trivially a no-op that must still exist)
    B["full-barrier"] = [](Interpreter&, ValueList&) -> Value {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        return Value::nil();
    };
    B["make"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.empty() ? Value::any() : (a.size() == 1 ? a[0] : Value::array(a));
        if (!I.tctx_.makeTargets.empty()) I.tctx_.makeTargets.back()->setPairVal(makePayload<Value>(v));
        else {
            // outside an action: the `$/` in scope takes it, and with no
            // Match there `make` has nothing to attach to
            Value* m = I.tctx_.cur ? I.tctx_.cur->find("$/") : nullptr;
            if (m && m->t == VT::Match) m->setPairVal(makePayload<Value>(v));
            else I.throwTypedV("X::Make::MatchRequired", {{"got", m ? *m : Value::nil()}},
                     "The make function expects $/ to contain a Match, but it contains " +
                     (m && m->t != VT::Nil ? m->typeName() : std::string("Nil")));
        }
        return v;
    };
    B["take"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() > 1) {
            // `take 1, 2, 3` takes ONE item, the List of its arguments —
            // `(gather { take 1, 2; take 3, 4 }).elems` is 2. Only a lone Slip
            // argument splices (see gatherTake); a slip written among other
            // arguments has already been flattened into the List.
            Value v = Value::array(a); v.isList = true;
            ValueList one{v};
            return I.gatherTake(one, v);
        }
        Value v = a.size() == 1 ? a[0] : Value::array(a);
        return I.gatherTake(a, v);
    };
    // take-rw is normally a special form (evalCall takes its argument by
    // EXPRESSION and proxies the storage it names — see evalTakeRw). This is
    // the indirect-call fallback (`&take-rw($x)`, .map(&take-rw)): the argument
    // arrives as a detached copy, so the best on offer is a fresh writable
    // cell — the sequence's element can still be assigned to, it just no
    // longer aliases the caller's variable.
    B["take-rw"] = [](Interpreter& I, ValueList& a) -> Value {
        Value proxy = I.makeCellProxy(a.empty() ? Value::any() : a[0]);
        ValueList one{proxy};
        return I.gatherTake(one, proxy);
    };
    // `succeed EXPR` exits the enclosing `when`/`given`, making the given evaluate to EXPR;
    // `proceed` leaves the current `when` but keeps testing later ones.
    B["succeed"] = [](Interpreter&, ValueList& a) -> Value {
        Value v = a.empty() ? Value::any() : (a.size() == 1 ? a[0] : Value::array(a));
        throw BreakGivenEx{v, !a.empty()};
    };
    B["proceed"] = [](Interpreter&, ValueList&) -> Value { throw ProceedEx{}; };
    // `sub dir(Cool $path = '.', Mu :$test)` — the path is the first POSITIONAL
    // and defaults to `.`. Issue #62: this took a[0] blindly, so a named-only
    // call `dir(test => /csv$/)` stringified the Pair, opendir failed on
    // "test\t…", and the answer was a silent empty list. The listing itself
    // lived here as a second, worse copy of IO::Path.dir (its own `.`/`..`
    // rule, `./x` entries, `ACCEPTS` on a Block died) — now the sub is the
    // method, as in Rakudo: `$path.IO.dir(:$test)`.
    B["dir"] = [](Interpreter& I, ValueList& a) -> Value {
        Value path; bool havePath = false;
        ValueList named;
        for (auto& x : a) {
            if (x.t == VT::Pair && x.namedArg) named.push_back(x);
            else if (!havePath) { path = x; havePath = true; }
        }
        Value io;
        if (havePath && path.hashKind == "IO") io = path; // an IO argument's own :CWD wins
        else {
            io = Value::str(havePath ? path.toStr() : "."); io.hashKind = "IO";
            io.ofTypeM() = I.cwdName();   // entries capture the CALL-TIME base, as Rakudo's do
        }
        return I.methodCall(io, "dir", named);
    };
    B["mkdir"] = builtinMkdir;
    B["rmdir"] = [](Interpreter& I, ValueList& a) -> Value {
        // `rmdir()` removes nothing and can only be a mistake — the same
        // reading Rakudo gives it, and the same exception.
        if (a.empty()) throw RakuError{Value::typeObj("X::NoZeroArgMeaning"),
            "The () form of 'rmdir' is reserved"};
        // the list form answers the names it DID remove, so a caller can see
        // which of a batch survived; a single failed name gives an empty list
        Value ok = Value::array();
        for (auto& p : a) {
            rejectNulPath(p.toStr());
            if (::rmdir(I.ioFsPath(p).c_str()) == 0) ok.arr()->push_back(p);
        }
        return ok;
    };
    B["spurt"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Hash && a[0].hash() && a[0].hash()->count("mode")) { // an IO::Handle: its own .spurt writes through it
            Value inv = a[0]; ValueList rest(a.begin() + 1, a.end()); return I.methodCall(inv, "spurt", rest);
        }
        if (!a.empty()) rejectNulPath(a[0].toStr());
        if (a.empty()) return Value::boolean(false);
        bool append = false, createonly = false;
        std::string content;
        bool haveContent = false;
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i].t == VT::Pair && a[i].namedArg) {
                if (a[i].s == "append") append = a[i].pairVal() && a[i].pairVal()->truthy();
                else if (a[i].s == "createonly" || a[i].s == "x") createonly = a[i].pairVal() && a[i].pairVal()->truthy();
            }
            else if (!haveContent) { content = a[i].toStr(); haveContent = true; }
        }
        std::string path = I.ioFsPath(a[0]);
        if (createonly) { std::ifstream probe(path); if (probe) { // a Failure, not a quiet False (as the method form)
            // X::AdHoc and an ABSOLUTE path, which is what a refused open says
            // everywhere else here — X::IO::Exists is not a Raku type, and a
            // relative name in the message does not say which file it meant.
            std::string abs = path;
            if (abs.empty() || abs[0] != '/') {
                char cb[4096];
                if (getcwd(cb, sizeof cb)) abs = std::string(cb) + "/" + path;
            }
            return I.ioFailure("X::AdHoc",
                               {{"path", Value::str(abs)}, {"os-error", Value::str("File exists")}},
                               "Failed to open file " + abs + ": File exists"); } }
        content = I.encodeTextEnc(content, Interpreter::encAdverb(a)); // `:enc`, and binary — as the method form
        std::ofstream out(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        if (!out) { // a Failure that detonates when sunk
            int err = errno;
            Value f = rakuppNewFailure();
            (*f.hash())["exception"] = Value::typeObj("X::IO::Spurt");
            (*f.hash())["message"] = Value::str("Failed to open file " + path + ": " + std::strerror(err));
            return f;
        }
        out << content;
        return Value::boolean(true);
    };
    B["slurp"] = [](Interpreter& I, ValueList& a) -> Value {
        // slurp() = $*ARGFILES.slurp: the files named in @*ARGS, else stdin
        bool onlyNamed = true;
        for (auto& x : a) if (!(x.t == VT::Pair && x.namedArg)) { onlyNamed = false; break; }
        if (onlyNamed) {
            VarExpr af("$*ARGFILES");
            Value h = I.eval(&af);
            return I.methodCall(h, "slurp", a);
        }
        // Delegate to the METHOD form: one reader, one rule set. The old copy
        // here opened in text mode with no :bin arm at all — its own comment
        // claimed ":bin routes to the method" while `slurp $p, :bin` returned
        // a CRLF-squeezed Str where `$p.IO.slurp(:bin)` returned the raw Blob.
        Value io = a[0];
        // `slurp('-')` is $*IN.slurp, and unlike `'-'.IO.slurp` not deprecated
        if (io.t == VT::Str && io.hashKind.empty() && io.toStr() == "-")
            return I.methodCall(stdinNow(I), "slurp", ValueList(a.begin() + 1, a.end()));
        if (io.t != VT::Hash && io.hashKind != "IO") { // a path: dispatch as IO, not bare Str
            rejectNulPath(io.toStr());       // (a Str invocant must NOT slurp — see the method's guard)
            io = Value::str(io.toStr());     // an IO::Path passes through AS-IS: rebuilding
            io.hashKind = "IO";              // it here dropped the path's own :CWD base
        }
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(io, "slurp", rest);
    };
    // lines() / get() / words() with no arg read from $*ARGFILES: the files named
    // in @*ARGS (awk/perl -n style), or standard input when there are none.
    B["lines"] = [](Interpreter& I, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        std::string line;
        // the STRING to split may sit behind a named argument
        // (`lines(:!chomp, "a\nb")`), so look past the adverbs for it
        // …and it is the first POSITIONAL whatever its type: `lines` is Cool, so
        // `lines(42)` is "42".lines. Requiring a Str let every other type fall
        // through to the $*IN branch below, which blocks on stdin.
        const Value* src = nullptr; ValueList named;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.namedArg) { named.push_back(v); continue; }
            if (!src) src = &v;
            else named.push_back(v);            // a limit / :count rides along
        }
        if (src) return I.methodCall(*src, "lines", named);
        Value argv = I.liveArgs();
        if (argv.arr() && !argv.arr()->empty()) {
            // $*ARGFILES itself, so what this reads is gone for the next reader
            VarExpr af("$*ARGFILES");
            Value h = I.eval(&af);
            return I.methodCall(h, "lines", named);
        }
        if (stdinClosed()) return I.methodCall(stdinNow(I), "lines", named);   // …which refuses, closed
        // Standard input is STREAMED, one line per pull. Slurping to EOF first is
        // a deadlock whenever the writer is still open — which is precisely how a
        // `-ne` child is driven through a Proc::Async pipe, so `last if /2/` never
        // got to run and the parent's `await` never returned (roast
        // S29-os/system.t). The `-n`/`-p` record loop is written over this.
        {
            auto st = std::make_shared<LazySeqState>();
            st->streaming = true;
            // …but it ENDS: an eager context (`my @l = lines`, `reverse lines`)
            // reads it all, as a handle's `.lines` does; only `for` walks it live
            st->finiteSource = true;
            st->appendNext = [](ValueList& cache) -> bool {
                std::string l;
                if (!std::getline(std::cin, l)) return false;
                if (!l.empty() && l.back() == '\r') l.pop_back();
                cache.push_back(Value::str(l));
                return true;
            };
            out.extM() = st;
        }
        return out;
    };
    B["get"] = [](Interpreter& I, ValueList& a) -> Value {
        // `get($fh)` reads from that handle
        for (auto& v : a)
            if (!(v.t == VT::Pair && v.namedArg) && v.t != VT::Any && v.t != VT::Type && v.t != VT::Nil)
                return I.methodCall(v, "get", ValueList{});
        // get() is $*ARGFILES.get: the files in @*ARGS when there are any
        {
            Value argv = I.liveArgs();
            if (argv.arr() && !argv.arr()->empty()) {
                VarExpr af("$*ARGFILES");
                Value h = I.eval(&af);
                return I.methodCall(h, "get", ValueList{});
            }
        }
        // …else $*IN.get, which knows the handle's own nl-in / chomp
        VarExpr in("$*IN");
        Value h = I.eval(&in);
        return I.methodCall(h, "get", ValueList{});
    };
    B["words"] = [](Interpreter& I, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        std::string all, w;
        // The source is the first POSITIONAL, whatever its type — `words` is a Cool
        // routine, so `words(42)` is "42".words. Selecting it by VALUE TAG left every
        // other type falling through to the `$*IN` branch, which blocks on stdin.
        if (!a.empty() && a[0].t != VT::Pair) {
            ValueList rest(a.begin() + 1, a.end());
            return I.methodCall(a[0], "words", rest);
        }
        // words() is $*ARGFILES.words — a $*ARGFILES the program set, else
        // the files in @*ARGS, else standard input
        {
            Value* set = I.tctx_.cur ? I.tctx_.cur->find("$*ARGFILES") : nullptr;
            Value argv = I.liveArgs();
            if ((set && set->t == VT::Hash) || (argv.arr() && !argv.arr()->empty())) {
                VarExpr af("$*ARGFILES");
                Value h = I.eval(&af);
                return I.methodCall(h, "words", ValueList(a.begin(), a.end()));
            }
        }
        if (stdinClosed()) return I.methodCall(stdinNow(I), "words", ValueList(a.begin(), a.end()));   // refuses
        { std::ostringstream ss; ss << std::cin.rdbuf(); all = ss.str(); noteStdinAtEnd(); } // words() = $*IN.words
        std::istringstream ws(all);
        while (ws >> w) out.arr()->push_back(Value::str(w));
        return out;
    };
    B["open"] = [](Interpreter& I, ValueList& a) -> Value { // sub form: open($path, :r/:w/:a)
        if (Value h; openDash(I, a, h)) return h;   // `open('-')` is standard input
        // the path is the first POSITIONAL — `open :w, $path` puts the adverb first,
        // and taking args[0] blindly opened a file literally named "w\tTrue"
        std::string path;
        for (auto& x : a) if (x.t != VT::Pair) { path = I.ioFsPath(x); break; }
        rejectNulPath(path);
        std::string mode = "r"; bool excl = false;
        bool rwAppend = false;   // `:ra` — read, and every write lands at the end
        for (auto& x : a) if (x.t == VT::Pair) {
            if (x.pairVal() && !x.pairVal()->truthy()) continue;   // `:!w` asks for nothing
            if (x.s == "w") mode = "w"; else if (x.s == "a") mode = "a"; else if (x.s == "r") mode = "r";
            else if (x.s == "rw") mode = "rw";           // read/write, create if missing, NO truncate
            else if (x.s == "ra") { mode = "rw"; rwAppend = true; }
            else if (x.s == "rx") { mode = "rw"; excl = true; }
            else if (x.s == "update") mode = "update";   // read/write, must exist
            else if (x.s == "exclusive" || x.s == "x") excl = true; // create-new-or-fail (O_EXCL)
        }
        // The SPELLED-OUT form Rakudo also takes: `:mode<ro|wo|rw>` with
        // `:create`/`:truncate`/`:append`. `$f.open(:mode<wo>, :create).close`
        // is the idiomatic `touch` (roast's own filetest.t does it), and
        // without this the adverbs were ignored, the handle opened read-only,
        // and the open threw "no such file or directory".
        {
            std::string modeAdv; bool wantCreate = false, wantTrunc = false, wantApp = false;
            for (auto& x : a) if (x.t == VT::Pair) {
                bool on = !x.pairVal() || x.pairVal()->truthy();
                if (x.s == "mode" && x.pairVal()) modeAdv = x.pairVal()->toStr();
                else if (x.s == "create")   wantCreate = on;
                else if (x.s == "truncate") wantTrunc = on;
                else if (x.s == "append")   wantApp = on;
            }
            if (!modeAdv.empty() || wantCreate || wantTrunc || wantApp) {
                if (modeAdv == "ro") mode = "r";
                else if (wantApp && modeAdv == "rw") { mode = "rw"; rwAppend = true; }
                else if (wantApp) mode = "a";
                else if (wantTrunc) mode = "w";
                else if (wantCreate) mode = "rw";       // create if missing, keep the content
                else if (modeAdv == "wo" || modeAdv == "rw") mode = "update"; // must exist
            }
        }
        // :nl-in(...) — custom input line separator(s); .lines/.get honour it
        Value nlIn;
        for (auto& x : a) if (x.t == VT::Pair && x.s == "nl-in" && x.pairVal()) nlIn = *x.pairVal();
        if (excl) { // File::Temp opens `:rw, :exclusive` to claim a fresh name
            std::ifstream probe(path);
            if (probe) { // a FAILURE, as every other refused open here is
                Value f = rakuppNewFailure();
                (*f.hash())["exception"] = Value::typeObj("X::IO::Exclusive");
                (*f.hash())["message"] = Value::str("Failed to open file " + path + ": File exists");
                return f;
            }
            if (mode == "r") mode = "w"; // bare :x implies write-create (Rakudo's :x)
            if (mode == "update") mode = "rw"; // `:mode<rw>, :create, :exclusive` creates it
        }
        // TRY the open, do not assume it. A read handle carries no OS descriptor —
        // every read reopens the path — so nothing later in the program
        // is in a position to notice that the file could never be opened at all.
        // Without this, `open("/no/such/dir/f", :w)` handed back a live handle,
        // every write through it was dropped, and the program heard about it at
        // some unrelated slurp much later (issue #71). The errno is the message:
        // answering "no such file or directory" for a permission error names the
        // wrong cause, which is the confusion the whole check exists to prevent.
        //
        // X::AdHoc, deliberately — as every failed open here does, and as Rakudo
        // does. The docs name no type for this ("Fails with appropriate exception
        // if the open fails"), so the only thing a program can be written against
        // is what Rakudo throws, and `CATCH { when X::AdHoc {…} }` is what code in
        // the wild contains. A more precise name (this file once had X::IO::Spurt,
        // X::IO::Exists, X::IO::Exclusive, X::IO::Open — none of which exist in
        // Rakudo at all) reads better and silently escapes every such CATCH.
        // A DIRECTORY is the one exception: Rakudo has a real type for it.
        int openedFd = -1;
        {
            struct stat st;
            if (::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { // Rakudo's own type and wording
                Value f = rakuppNewFailure();
                Value ex = Value::typeObj("X::IO::Directory");
                (*f.hash())["exception"] = ex;
                (*f.hash())["trying"] = Value::str("open"); // `.trying` names the operation refused
                (*f.hash())["path"] = Value::str(path);
                (*f.hash())["message"] = Value::str("'" + path + "' is a directory, cannot do '.open' on a directory");
                return f;
            }
            int err = 0;
            if (mode == "r" || mode == "update") { // both need the file to exist
                std::ifstream probe(path);
                if (!probe) err = errno;
            }
#if !defined(_WIN32)
            // :w and :a hold the descriptor every write goes through, opened
            // once here as Rakudo's is (a second open at the first write
            // truncated again, and a watcher saw it)
            if (!err && (mode == "w" || mode == "a")) {
                openedFd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC |
                                                (mode == "w" ? O_TRUNC : 0), 0666);
                if (openedFd < 0) err = errno;
            }
            else
#endif
            if (!err && mode != "r") { // :w truncates; :a, :rw and :update keep what is there
                std::ofstream create(path, mode == "w" ? std::ios::trunc : std::ios::app);
                if (!create) err = errno;                  // and the file exists from here on, as Rakudo's does
            }
            if (err) { // a Failure that detonates when used or sunk — `my $fh = open …; if $fh {…}` works (it threw)
                Value f = rakuppNewFailure();
                (*f.hash())["exception"] = Value::typeObj("X::IO::Open"); // see throwFailedOpen
                (*f.hash())["message"] = Value::str("Failed to open file " + path + ": " + std::strerror(err));
                (*f.hash())["os-error"] = Value::str(std::strerror(err));
                (*f.hash())["path"] = Value::str(path);
                return f;
            }
        }
        Value h = Value::makeHash(); h.hashKind = "FileHandle";
        (*h.hash())["path"] = Value::str(path);
        (*h.hash())["mode"] = Value::str(mode);
        (*h.hash())["buffer"] = Value::str("");
        if (openedFd >= 0) {
            Value fv = Value::integer(openedFd);
            fv.extM() = std::make_shared<WriteFd>(openedFd);
            (*h.hash())["wfd"] = fv;
        }
        if (rwAppend) (*h.hash())["rwappend"] = Value::boolean(true);
        // :bin — the handle reads BYTES, so `seek`/`tell` are byte offsets
        // rather than the line-boundary emulation a text handle gets
        for (auto& x : a) if (x.t == VT::Pair && x.s == "bin" && x.pairVal() && x.pairVal()->truthy())
            (*h.hash())["bin"] = Value::boolean(true);
        // :enc(...) — the handle's text encoding; every read through it decodes
        // with this instead of assuming the bytes are already UTF-8. The name is
        // canonicalized here (`latin1` is `iso-8859-1`), and an unknown one is
        // refused at the open rather than at the first odd character.
        for (auto& x : a) if (x.t == VT::Pair && (x.s == "enc" || x.s == "encoding") &&
                              x.pairVal() && x.pairVal()->t != VT::Any) {
            if (h.hash()->count("bin"))
                throw RakuError{Value::typeObj("X::IO::BinaryAndEncoding"),
                    "Cannot open a handle in binary mode with an encoding"};
            bool known = true;
            std::string canon = canonEncodingName(x.pairVal()->toStr(), &known);
            if (!known)
                I.throwTyped("X::Encoding::Unknown", {{"name", x.pairVal()->toStr()}},
                             "Unknown string encoding '" + x.pairVal()->toStr() + "'");
            (*h.hash())["encoding"] = Value::str(canon);
            // plain `utf16` (no byte order named) writes a BOM first — once, into
            // a file that is empty when opened for writing
            if ((canon == "utf16" || canon == "utf-16") && (mode == "w" || mode == "a" || mode == "rw")) {
                struct stat bst;
                if (::stat(path.c_str(), &bst) == 0 && bst.st_size == 0) {
                    std::ofstream bom(path, std::ios::binary | std::ios::app);
                    bom.write("\xFF\xFE", 2);
                    (*h.hash())["wrote"] = Value::boolean(true);   // the first print must not truncate it away
                }
            }
        }
        if (nlIn.t != VT::Any) (*h.hash())["nl-in"] = nlIn;
        // :nl-out(...) — what .say / .put / .print-nl end a record with
        for (auto& x : a)
            if (x.t == VT::Pair && x.s == "nl-out" && x.pairVal() && x.pairVal()->t == VT::Str)
                (*h.hash())["nl-out"] = *x.pairVal();
        // :chomp / :!chomp — whether lines come back without their terminator
        for (auto& x : a) if (x.t == VT::Pair && x.s == "chomp")
            (*h.hash())["chomp"] = Value::boolean(!x.pairVal() || x.pairVal()->truthy());
        // :out-buffer(N) / :!out-buffer — how many bytes the handle may hold
        // back before they must reach the file. Absent, it keeps the default
        // block; :!out-buffer (False) makes every write land immediately, which
        // is how a program writes a log another process is tailing.
        for (auto& x : a) if (x.t == VT::Pair && x.s == "out-buffer")
            (*h.hash())["out-buffer"] =
                Value::integer(outBufferSize(x.pairVal() ? *x.pairVal() : Value::boolean(true)));
        if (mode != "r") I.registerWriteHandle(h.hashS()); // flush at exit if not closed
        return h;
    };
    // symlink($target, $name) / link($target, $name) — the sub forms Rakudo
    // exposes (File::Find's suite builds a symlinked directory with the sub form
    // before walking it). `readlink` is a METHOD only, as in Rakudo.
    B["symlink"] = [](Interpreter& I, ValueList& a) -> Value {
        bool absolute = true;
        ValueList pos;
        for (auto& x : a) {
            if (x.t == VT::Pair && x.s == "absolute") { absolute = !x.pairVal() || x.pairVal()->truthy(); continue; }
            pos.push_back(x);
        }
        if (pos.size() < 2) return Value::boolean(false);
        std::string target = absolute ? I.ioFsPath(pos[0]) : pos[0].toStr(), name = I.ioFsPath(pos[1]);
        // Rakudo absolutizes the TARGET (unless `:!absolute`). It matters: a
        // relative target is read by the OS relative to the LINK's directory,
        // not the cwd, so `symlink("t/dir1/d", "t/dir2/link")` would otherwise dangle.
        if (absolute && !target.empty() && target[0] != '/') {
            char cbuf[4096];
            if (getcwd(cbuf, sizeof cbuf)) target = std::string(cbuf) + "/" + target;
        }
        if (platform_symlink(target.c_str(), name.c_str()) != 0) {
            std::string msg = "Failed to create symlink called '" + name + "' on target '" + target +
                              "': " + std::strerror(errno);
            return I.ioFailure("X::IO::Symlink",
                               {{"target", Value::str(target)}, {"name", Value::str(name)},
                                {"os-error", Value::str(std::strerror(errno))}}, msg);
        }
        return Value::boolean(true);
    };
    B["link"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::boolean(false);
        std::string target = I.ioFsPath(a[0]), name = I.ioFsPath(a[1]);
        if (platform_link(target.c_str(), name.c_str()) != 0) {
            // a Failure, as Rakudo's `link` answers — carrying what it tried
            Value f = rakuppNewFailure();
            (*f.hash())["exception"] = Value::typeObj("X::IO::Link");
            (*f.hash())["message"] = Value::str("Failed to create link called '" + name + "' on target '" +
                                                target + "': " + std::strerror(errno));
            (*f.hash())["target"] = Value::str(target);
            (*f.hash())["name"] = Value::str(name);
            (*f.hash())["os-error"] = Value::str(std::strerror(errno));
            return f;
        }
        return Value::boolean(true);
    };
    // 6.d hands back the list of paths it removed; 6.e takes one path and hands
    // back one Bool (its multi-path form still answers the old way, deprecated).
    // A path that was already absent counts as removed in both — Rakudo says
    // True for a file that does not exist, and the point of the call is the
    // state afterwards, not who did it.
    B["unlink"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) throw RakuError{Value::typeObj("X::NoZeroArgMeaning"),
            "The () form of 'unlink' is reserved"};
        auto gone = [](const std::string& p) { return ::unlink(p.c_str()) == 0 || errno == ENOENT; };
        if (I.sixE() && a.size() == 1) return Value::boolean(gone(I.ioFsPath(a[0])));
        Value ok = Value::array();   // an Array, as Rakudo's `my @ok` is
        for (auto& f : a) if (gone(I.ioFsPath(f))) ok.arr()->push_back(f);
        return ok;
    };
    // sub forms of the IO::Path methods (Shell::Command calls them this way)
    B["copy"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::boolean(false);
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "copy", rest);
    };
    B["rename"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::boolean(false);
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "rename", rest);
    };
    B["move"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::boolean(false);
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "move", rest);
    };
    B["close"] = [](Interpreter& I, ValueList& a) -> Value { // sub form: close($fh)
        if (a.empty()) return Value::boolean(true);
        return I.methodCall(a[0], "close", {});
    };
    B["getc"] = [](Interpreter& I, ValueList& a) -> Value { // sub form: getc($fh)
        if (a.empty()) return Value::nil();
        return I.methodCall(a[0], "getc", {});
    };
    B["chmod"] = [](Interpreter&, ValueList& a) -> Value { // chmod MODE, @paths → the paths changed
        Value out = Value::array();   // an Array, as Rakudo's `my @ok` is
        if (a.empty()) return out;
        // a permission string like IO.mode's "0777" is octal; an Int (0o644) is itself
        mode_t mode = a[0].t == VT::Str ? (mode_t)strtol(a[0].s.c_str(), nullptr, 8)
                                        : (mode_t)a[0].toInt();
        for (size_t k = 1; k < a.size(); k++) {
            std::string p = a[k].toStr();
            if (::chmod(p.c_str(), mode) == 0) out.arr()->push_back(a[k]);
        }
        return out;
    };
    B["skip-rest"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string reason = a.empty() ? "" : a[0].toStr();
        long remaining = (I.planned_ > 0 ? I.planned_ : 0) - I.testNum_;
        for (long k = 0; k < remaining; k++) I.emitTest(true, "", "SKIP " + reason);
        return Value::boolean(true);
    };
    B["subtest"] = [](Interpreter& I, ValueList& a) -> Value {
        Value code; std::string desc;
        for (auto& v : a) {
            if (v.t == VT::Code) code = v;
            else if (v.t == VT::Str) desc = v.s;
            // `subtest "title" => {…}` — the Pair form runs its body like any
            // other (it was once left unrun; every such subtest passed VACUOUSLY,
            // which inflated whole dist suites — AttrX::Mooish's most of all)
            else if (v.t == VT::Pair && v.pairVal()) {
                desc = v.s;
                if (v.pairVal()->t == VT::Code) code = *v.pairVal();
            }
        }
        struct SubMark {
            Interpreter& I;
            SubMark(Interpreter& i, bool isSub) : I(i) { I.subtestIsSub_.push_back(isSub); }
            ~SubMark() { I.subtestIsSub_.pop_back(); }
        } mark{I, code.t == VT::Code && code.code() && !code.code()->isBlock};
        return Value::boolean(I.runSubtestFrame(desc, [&]() {
            if (code.t == VT::Code) I.callCallable(code, {});
        }));
    };
    B["done-testing"] = [](Interpreter& I, ValueList&) -> Value {
        if (I.planned_ < 0) { std::cout << std::string(4 * I.subtestDepth_, ' ') << "1.." << I.testNum_ << "\n"; I.planned_ = I.testNum_; }
        // True only if every test passed and the ran count matched the plan.
        return Value::boolean(I.failCount_ == 0 && I.planned_ == I.testNum_);
    };
    B["done_testing"] = B["done-testing"];
    // bail_out(reason?) — emit "Bail out!" and stop the whole test run immediately.
    B["bail-out"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string reason;
        for (auto& v : a) if (v.t != VT::Pair) { reason = v.toStr(); break; }
        std::cout << "Bail out!" << (reason.empty() ? "" : " " + reason) << "\n" << std::flush;
        I.bailedOut_ = true;
        throw ExitEx{255};
    };
    B["bail_out"] = B["bail-out"];

    // --- utility functions ---
    B["abs"] = [](Interpreter& I, ValueList& a) -> Value { return rtBAbs(I, a.empty() ? Value::any() : a[0]); };
    // Comparison operators usable as subs: `cmp($a,$b)`, `$a leg $b`, etc.
    for (auto op : {"cmp", "leg", "before", "after"})
        B[op] = [op](Interpreter&, ValueList& a) -> Value { return a.size() >= 2 ? applyArith(op, a[0], a[1]) : Value::any(); };
    // List routines that delegate to the method of the same name. An ADVERB the
    // routine understands is forwarded as a method argument rather than swept into
    // the list — `unique @a, as => {.abs}` was uniquing the adverb along with the
    // elements. The names are an allow-list so a genuine Pair ELEMENT still counts
    // as data (`unique (a => 1), (a => 1)`).
    // `rotor(CYCLE…, LIST)` — the 6.e sub form takes the cycle FIRST and the
    // iterable LAST, the opposite way round from the method. Sweeping every
    // positional into the list made `rotor(3, 'a'..'h')` rotor the cycle too.
    B["rotor"] = [](Interpreter& I, ValueList& a) -> Value {
        ValueList pos, opts;
        for (auto& x : a) {
            if (x.t == VT::Pair && x.namedArg) opts.push_back(x); // `:partial` may come last
            else pos.push_back(x);
        }
        if (pos.empty()) return Value::array();
        Value list = pos.back();                       // the LAST positional is the iterable
        ValueList cycle(pos.begin(), pos.end() - 1);   // everything before it is the cycle
        for (auto& o : opts) cycle.push_back(o);
        return I.methodCall(list, "rotor", cycle);
    };
    for (auto nm : {"permutations", "combinations", "unique", "repeated", "squish", "flat"})
        B[nm] = [nm](Interpreter& I, ValueList& a) -> Value {
            static const std::set<std::string> adv = {"as", "with", "partial"};
            ValueList items, opts;
            for (auto& x : a) {
                if (x.t == VT::Pair && adv.count(x.s)) opts.push_back(x);
                else items.push_back(x);
            }
            // `permutations($n)` and `combinations($n, $k)` name the RANGE `^$n`,
            // not an element: `combinations(3, 2)` is ((0,1),(0,2),(1,2)) and
            // `permutations(3)` the six orderings of 0,1,2. A negative count is
            // the empty range (Nil-Any sheet NA-49; roast permutations.t,
            // combinations.t). The Iterable form below is unchanged.
            // The bound keeps a huge `$n` from being materialised: nothing we
            // could build would be an answer, and the old reading is at least
            // instant. (`+permutations(30)` wants the COUNT without the lists,
            // which is a separate, lazy answer we do not have yet.)
            const long long kMax = std::string(nm) == "permutations" ? 9 : 16;
            if ((std::string(nm) == "permutations" || std::string(nm) == "combinations") &&
                !items.empty() &&
                (items[0].t == VT::Int || items[0].t == VT::Num || items[0].t == VT::Rat ||
                 items[0].t == VT::Complex) &&
                (items[0].t == VT::Complex ? (long long)items[0].n : items[0].toInt()) <= kMax) {
                long long n = std::max(0LL, items[0].t == VT::Complex ? (long long)items[0].n : items[0].toInt());
                Value src = Value::array(); src.isList = true;
                for (long long k = 0; k < n; k++) src.arr()->push_back(Value::integer(k));
                ValueList rest(items.begin() + 1, items.end());
                for (auto& r : rest) if (r.t == VT::Complex) r = Value::number(r.n);
                for (auto& o : opts) rest.push_back(o);
                return I.methodCall(src, nm, rest);
            }
            // …and a LARGE count is a lazy Seq that KNOWS its length: `+permutations(30)`
            // is 30! without building a single ordering
            if (std::string(nm) == "permutations" && items.size() == 1 && items[0].t == VT::Int &&
                !items[0].big() && items[0].toInt() > kMax && items[0].toInt() <= 100000) {
                const long long n = items[0].toInt();
                Value fact = Value::integer(1);
                for (long long k = 2; k <= n; k++) fact = applyArith("*", fact, Value::integer(k));
                auto idx = std::make_shared<std::vector<size_t>>((size_t)n);
                for (long long k = 0; k < n; k++) (*idx)[(size_t)k] = (size_t)k;
                auto started = std::make_shared<bool>(false);
                auto st = std::make_shared<LazySeqState>();
                st->infinite = true;      // never materialised whole
                st->hasCount = true; st->countVal = fact;
                st->appendNext = [idx, started](ValueList& cache) -> bool {
                    if (*started && !std::next_permutation(idx->begin(), idx->end())) return false;
                    *started = true;
                    Value perm = Value::array(); perm.isList = true;
                    for (size_t i : *idx) perm.arr()->push_back(Value::integer((long long)i));
                    cache.push_back(perm);
                    return true;
                };
                Value out = Value::array(); out.isList = true; out.s = "Seq";
                out.extM() = st;
                return out;
            }
            // …and a large `combinations($n, $k)` the same way: `+combinations(100, 70)`
            // is the binomial C(100, 70) without building a single combination
            if (std::string(nm) == "combinations" && items.size() == 2 && items[0].t == VT::Int &&
                items[1].t == VT::Int && !items[0].big() && !items[1].big() &&
                items[0].toInt() > kMax && items[0].toInt() <= 100000) {
                return lazyIndexCombinations(items[0].toInt(), items[1].toInt());
            }
            // `combinations(@list, $k)` — an ITERABLE first argument is the list
            // and what follows is the argument, not more elements. The other
            // four (`unique(1, 1, 2)`, `squish(…)`, …) take `+values`, so their
            // several arguments ARE the list.
            if (std::string(nm) == "combinations" && items.size() > 1 &&
                (items[0].t == VT::Array || items[0].t == VT::Range ||
                 (items[0].t == VT::Hash && (items[0].hashKind.empty() || items[0].hashKind == "Map"))))
                return I.methodCall(items[0], nm, ValueList(items.begin() + 1, items.end()));
            // (+values: a lone ITEMIZED list is one value — `unique($l)` for
            // `my $l = (5, 7)` is `((5 7))`, as Rakudo answers)
            const bool plusValues = std::string(nm) == "unique" || std::string(nm) == "repeated" ||
                                    std::string(nm) == "squish";
            Value v = items.size() == 1 && !(plusValues && items[0].itemized && items[0].s != "Seq" &&
                                             (items[0].t == VT::Array || items[0].t == VT::Hash))
                ? items[0] : Value::array(items);
            return I.methodCall(v, nm, opts);
        };
    // Same-named-method routines that forward the REMAINING args, invocant first:
    // rotate(@a,$n), substr($s,$f,$c), head(@a,$n), trim($s), samecase($s,$pat), …
    for (auto nm : {"rotate", "substr", "substr-rw", "trim", "trim-leading",
                    "trim-trailing", "flip", "tc", "tclc", "wordcase", "pairs", "antipairs", "chop",
                    "samecase", "samemark", "chomp"}) // head/tail: count-first forms below
        if (!B.count(nm)) B[nm] = [nm](Interpreter& I, ValueList& a) -> Value {
            if (a.empty()) return Value::nil();
            Value inv = a[0]; ValueList rest(a.begin() + 1, a.end());
            return I.methodCall(inv, nm, rest);
        };
    // head/tail sub forms take the count FIRST: head(5, @list) → @list.head(5);
    // the one-arg form is the method with no count (first/last element).
    for (auto nm : {"head", "tail"})
        B[nm] = [nm](Interpreter& I, ValueList& a) -> Value {
            if (a.empty()) return Value::nil();
            if (a.size() == 1) { ValueList none; return I.methodCall(a[0], nm, none); }
            Value n = a[0];
            ValueList rest(a.begin() + 1, a.end());
            ValueList ma{n}; return I.methodCall(slurpyValues(rest), nm, ma);
        };
    // pick/roll sub forms take the count FIRST: pick(3, @list) → @list.pick(3).
    for (auto nm : {"pick", "roll"})
        B[nm] = [nm](Interpreter& I, ValueList& a) -> Value {
            if (a.empty()) return Value::any();
            Value n = a[0];
            ValueList rest(a.begin() + 1, a.end());
            ValueList ma{n}; return I.methodCall(slurpyValues(rest), nm, ma);
        };
    // …and `skip` is BOTH a list sub and Test's skip-this-many-tests. Rakudo
    // tells them apart by signature: Test's takes a reason and a count, the
    // list one a count and `+values`. A numeric first argument with anything
    // after it is the LIST form — roast S32-list/skip.t calls `skip(5, @a)`
    // with `use Test` in scope and wants the list (Nil-Any sheet NA-49).
    {
        auto testSkip = B["skip"];
        B["skip"] = [testSkip](Interpreter& I, ValueList& a) -> Value {
            // (`skip 2, 'reason'` — a lone STRING after the number — is Test's, with
            // its arguments backwards: it dies rather than skipping a list)
            const bool listForm =
                a.size() >= 2 && !(a.size() == 2 && a[1].t == VT::Str) &&
                (a[0].t == VT::Int || a[0].t == VT::Num || a[0].t == VT::Rat ||
                 a[0].t == VT::Whatever || a[0].t == VT::Code);
            if (!listForm) return testSkip(I, a);
            Value n = a[0];
            ValueList rest(a.begin() + 1, a.end());
            ValueList ma{n}; return I.methodCall(slurpyValues(rest), "skip", ma);
        };
    }
    B["srand"] = [](Interpreter&, ValueList& a) -> Value { // reseed the RNG; returns the seed
        long long seed = a.empty() ? (long long)::time(nullptr) : a[0].toInt();
        srandSeed(seed);
        return Value::integer(seed);
    };
    // `reduce &f, LIST` / `produce &f, LIST` delegate to the METHOD of the same
    // name — that is where the operator's associativity is read off the
    // callable's name, so `produce &[**], (2,3,4)` folds right like the method.
    for (const char* rf : {"reduce", "produce"}) {
        std::string rname = rf;
        B[rname] = [rname](Interpreter& I, ValueList& a) -> Value {
            if (a.empty()) return rname == "reduce" ? Value::any()
                                                    : [] { Value o = Value::array(); o.isList = true; return o; }();
            Value f = a[0];
            ValueList items;
            // The ONE-ARG RULE, as Rakudo has it: a single Positional argument
            // spreads to its elements (`reduce &f, @a`, `reduce &f, 1..4`), and
            // several arguments are taken as they are — `reduce &f, "I", (1,2),
            // (3,4)` folds over three items, the last two being Lists. Deep
            // `flatten()`ing every argument destroyed exactly that: Digest::SHA2's
            // 16-word block arrived as sixteen separate values, so `$block[$t]`
            // was undefined for every t but 0.
            if (a.size() == 2 && (a[1].t == VT::Array || a[1].t == VT::Range)) {
                if (a[1].t == VT::Array && a[1].arr()) for (auto& x : *a[1].arr()) items.push_back(x);
                else items = a[1].flatten();
            }
            else for (size_t i = 1; i < a.size(); i++) items.push_back(a[i]);
            Value list = Value::array(items); list.isList = true;
            ValueList ma{f};
            return I.methodCall(list, rname, ma);
        };
    }
    B["cis"] = [](Interpreter& I, ValueList& a) -> Value {
        double x = a.empty() ? 0.0 : numValueOf(I, a[0]);   // a custom Real bridges
        return Value::complex(std::cos(x), std::sin(x)); // e^(ix)
    };
    B["unpolar"] = [](Interpreter& I, ValueList& a) -> Value { // Complex from (magnitude, angle)
        double r = a.empty() ? 0.0 : numValueOf(I, a[0]);   // …and so does the angle
        double th = a.size() > 1 ? numValueOf(I, a[1]) : 0.0;
        return Value::complex(r * std::cos(th), r * std::sin(th));
    };
    B["sqrt"] = [](Interpreter& I, ValueList& a) -> Value { return rtBSqrt(I, a.empty() ? Value::integer(0) : a[0]); };
    B["roots"] = [](Interpreter& I, ValueList& a) -> Value {
        // roots($x, $n): the $n n-th complex roots of $x (principal first,
        // stepping by 2π/n). n < 1 or NaN input yields a single NaN.
        Value out = Value::array(); out.isList = true; out.s = "Seq"; // Rakudo hands back a Seq
        double re, im;
        Value x = a.empty() ? Value::integer(0) : a[0];
        if (x.t == VT::Complex) { re = x.n; im = x.im(); }
        else { re = x.toNum(); im = 0.0; }
        long long n = a.size() > 1 ? a[1].toInt() : 1;
        // a degenerate root count (or a NaN operand) is a bare NaN, not a
        // one-element list of one
        if (n < 1 || std::isnan(re) || std::isnan(im)) return Value::number(std::nan(""));
        // the ONE first root is handed back bare, not as a list of one
        if (n == 1) return Value::complex(re, im);
        double mag = std::pow(std::hypot(re, im), 1.0 / (double)n);
        double ang = std::atan2(im, re) / (double)n;
        for (long long k = 0; k < n; k++) {
            double th = ang + 2.0 * M_PI * (double)k / (double)n;
            out.arr()->push_back(Value::complex(mag * std::cos(th), mag * std::sin(th)));
        }
        return out;
    };
    B["floor"] = [](Interpreter& I, ValueList& a) -> Value { return rtBFloor(I, a.empty() ? Value::integer(0) : a[0]); };
    B["ceiling"] = [](Interpreter& I, ValueList& a) -> Value { return rtBCeiling(I, a.empty() ? Value::integer(0) : a[0]); };
    B["round"] = [](Interpreter& I, ValueList& a) -> Value { // delegate so a scale arg (round($x, 0.1)) and NaN/Inf are honoured
        if (a.empty()) return Value::integer(0);
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "round", rest);
    };
    B["truncate"] = [](Interpreter& I, ValueList& a) -> Value { return rtBTruncate(I, a.empty() ? Value::integer(0) : a[0]); };
    B["exp"] = [](Interpreter& I, ValueList& a) -> Value {
        // …and the BASE travels with it: `exp($z, 2)` is `2 ** $z`, so the
        // rest of the argument list has to reach the method (it was dropped).
        if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) {
            ValueList rest(a.begin() + 1, a.end()); return I.methodCall(a[0], "exp", rest); }
        // `exp($x, $base)` is `$base ** $x`, which keeps an Int base EXACT —
        // `exp(2, 10)` is the Int 100, not 100e0 — and matches the method form.
        if (a.size() >= 2) return applyArith("**", a[1], a[0]);
        return rtBExp(I, a.empty() ? Value::integer(0) : a[0]); };
    // Trigonometry (radians). Also available as methods below.
    {
        struct TF { const char* name; double (*fn)(double); };
        static const TF tfs[] = {
            {"sin", std::sin}, {"cos", std::cos}, {"tan", std::tan},
            {"asin", std::asin}, {"acos", std::acos}, {"atan", std::atan},
            {"sinh", std::sinh}, {"cosh", std::cosh}, {"tanh", std::tanh},
            {"asinh", rakuAsinh}, {"acosh", rakuAcosh}, {"atanh", std::atanh},
        };
        for (auto& tf : tfs) {
            auto f = tf.fn; std::string name = tf.name;
            B[tf.name] = [f, name](Interpreter& I, ValueList& a) -> Value {
                return rtBMath1(I, a.empty() ? Value::integer(0) : a[0], name.c_str(), f);
            };
        }
        B["sec"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "sec", none); }
            return Value::number(1.0 / std::cos(a.empty()?0:a[0].toNum()));
        };
        B["cosec"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "cosec", none); }
            return Value::number(1.0 / std::sin(a.empty()?0:a[0].toNum()));
        };
        B["cotan"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "cotan", none); }
            return Value::number(1.0 / std::tan(a.empty()?0:a[0].toNum()));
        };
        B["asec"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "asec", none); }
            return Value::number(std::acos(1.0 / (a.empty()?1:a[0].toNum())));
        };
        B["acosec"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "acosec", none); }
            return Value::number(std::asin(1.0 / (a.empty()?1:a[0].toNum())));
        };
        B["acotan"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "acotan", none); }
            return Value::number(std::atan(1.0 / (a.empty()?1:a[0].toNum())));
        };
        // atan2(y, x) is y.atan2(x): a user Real bridges through the method
        B["atan2"]  = [](Interpreter& I, ValueList& a){
            if (!a.empty() && (a[0].t == VT::Object || (a.size() > 1 && a[1].t == VT::Object))) {
                ValueList rest(a.begin() + 1, a.end());
                return I.methodCall(a[0], "atan2", rest);
            }
            double y=a.empty()?0:a[0].toNum(), x=a.size()>1?a[1].toNum():1.0; return Value::number(std::atan2(y,x)); };
        B["sech"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "sech", none); }
            return Value::number(1.0 / std::cosh(a.empty()?0:a[0].toNum()));
        };
        B["cosech"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "cosech", none); }
            return Value::number(1.0 / std::sinh(a.empty()?0:a[0].toNum()));
        };
        B["cotanh"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "cotanh", none); }
            return Value::number(1.0 / std::tanh(a.empty()?0:a[0].toNum()));
        };
        B["asech"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "asech", none); }
            return Value::number(std::acosh(1.0 / (a.empty()?1:a[0].toNum())));
        };
        B["acosech"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "acosech", none); }
            return Value::number(std::asinh(1.0 / (a.empty()?1:a[0].toNum())));
        };
        B["acotanh"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "acotanh", none); }
            return Value::number(std::atanh(1.0 / (a.empty()?1:a[0].toNum())));
        };
    }
    B["log"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Complex) { ValueList rest(a.begin() + 1, a.end()); return I.methodCall(a[0], "log", rest); }
        double x = a.empty() ? 0 : numValueOf(I, a[0]);
        if (a.size() >= 2) return Value::number(std::log(x) / std::log(numValueOf(I, a[1]))); // log($x, $base)
        return rtBLog(I, a.empty() ? Value::integer(0) : a[0]); };
    B["log10"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Complex) return I.methodCall(a[0], "log10", {});
        return rtBLog10(I, a.empty() ? Value::integer(0) : a[0]); };
    B["log2"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Complex) return I.methodCall(a[0], "log2", {});
        return rtBLog2(I, a.empty() ? Value::integer(0) : a[0]); };
    registerBuiltinsPart3();
}

} // namespace rakupp
