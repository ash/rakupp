// MethodCallPart1b.cpp — segment 1b of the method-dispatch chain
//
// One of the parts BuiltinsParts.h lists; what they share is declared there.
#include "BuiltinsParts.h"
#include "Sandbox.h"

namespace rakupp {

// Segment 1b of the method-dispatch chain, split out of methodCallInner for
// compile time. The chain is ORDER-SENSITIVE (an earlier arm shadows a later
// one), so these arms run after the ones above and before methodCallPart1c. nullopt = "not handled here".
std::optional<Value> Interpreter::methodCallPart1b(const Value& inv, const MName& m, ValueList& args,
                                                   const std::vector<ExprPtr>* rwArgs) {
    // NativeCall Pointer[T]: `Pointer.new($addr)` / `Pointer[int32].new(...)`.
    if (inv.t == VT::Type && (inv.s == "Pointer" || inv.s.rfind("Pointer[", 0) == 0) &&
        (m == "new" || m == "allocate")) {
        // --sandbox: an address made from a number is a way to read any byte
        // of the process; the NULL `Pointer.new` is harmless
        if (g_sandboxChecks && (!args.empty() || m == "allocate"))
            sandboxRefuse(*this, "Pointer." + (const std::string&)m, SandboxCap::Ffi);
        std::string et = inv.s.rfind("Pointer[", 0) == 0 ? inv.s.substr(8, inv.s.size() - 9) : inv.ofType();
        void* p = args.empty() ? nullptr : (void*)(intptr_t)ncRawAddr(args[0]);
        return ncMakePointer(et.empty() ? "Pointer" : "Pointer[" + et + "]", p);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Pointer") {
        long long addr = inv.hash()->count("addr") ? (*inv.hash())["addr"].toInt() : 0;
        std::string of = inv.hash()->count("of") ? (*inv.hash())["of"].toStr() : "";
        // A pointer is numerically its address, an Int — and Raku++ lets it be
        // a REAL one too, so `$p < $end` orders addresses. Rakudo's Pointer has
        // no .Real and dies there; the address is the only sensible answer.
        if (m == "Int" || m == "Numeric" || m == "Real") return Value::integer(addr);
        if (m == "abs") { Value a = Value::integer(addr); return methodCall(a, "abs", args); }
        // An instantiated Pointer is DEFINED whatever it points at — `Pointer.new`
        // is a real object holding NULL. Emptiness is .Bool's job, and the two
        // were the same test here, which inverted both against Rakudo.
        if (m == "defined") return Value::boolean(true);
        if (m == "Bool" || m == "so") return Value::boolean(addr != 0);
        // Rakudo prints the address in HEX and spells NULL out; `.raku` is the
        // constructor form, not the angle-bracket gist — with the package and
        // the element type, as `Pointer[Handle].new.raku` is in Rakudo.
        // (`.gist` names the package and not the element type, as Rakudo's does;
        // Rakudo's `.Str` shows an object id, so it keeps the address here)
        if (m == "gist") return Value::str("NativeCall::Types::" + ncPointerText("Pointer", "", addr));
        if (m == "Str") return Value::str(ncPointerText("Pointer", of, addr));
        if (m == "raku") return Value::str("NativeCall::Types::Pointer" + std::string(of.empty() ? "" : "[" + of + "]") +
                                           ".new(" + std::to_string(addr) + ")");
        // `.deref` on a Pointer[T] where T is a NativeCall CLASS (CPointer or
        // CStruct) hands back a T sitting at the pointed-to address, not the raw
        // machine word. `Font::FreeType`'s BUILD is `$!raw = $p.deref` with
        // `$p` a Pointer[FT_Library], and an Int there failed the attribute's
        // type check — the last thing between that dist (and thirteen behind it)
        // and its test suite. A scalar element type keeps reading a scalar.
        if (m == "deref") {
            if (g_sandboxChecks) sandboxRefuse(*this, "Pointer.deref", SandboxCap::Ffi);
            if (!of.empty() && ascii::isupper((unsigned char)of[0])) {
                auto cit = classes_.find(of);
                if (cit == classes_.end()) cit = classes_.find(resolveClassAlias(of));
                auto ci = cit != classes_.end() ? cit->second : nullptr;
                if (ci && (ci->repr == "CPointer" || ci->repr == "CStruct" || ci->repr == "CUnion")) {
                    // NULL derefs to a FAILURE, as upstream's does — not to the
                    // type object, which would read as a perfectly good T.
                    if (!addr) {
                        Value f = rakuppNewFailure();
                        (*f.hash())["exception"] = Value::typeObj("X::AdHoc");
                        (*f.hash())["message"]   = Value::str("Can not dereference a NULL Pointer");
                        return f;
                    }
                    // The ADDRESS is the handle, not a slot holding one. An
                    // `is rw` out-parameter here already collapses a level: the
                    // callee writes through `&slot` and the copy-back stores what
                    // it wrote, so after `FT_Init_FreeType($p)` the Pointer holds
                    // the FT_Library itself. Reading 8 more bytes through it gave
                    // a garbage handle that answered a garbage version. This is
                    // the same value `nativecast(FT_Library, $p)` produces, which
                    // is how the level was settled.
                    Value o = Value::object(makePayload<ObjectData>());
                    o.obj()->cls = ci;
                    o.obj()->attrs["__native_ptr"] = Value::integer(addr);
                    return o;
                }
            }
            return ncReadElem(addr, of, 0);
        }
        // pointer arithmetic in ELEMENTS, as NativeHelpers::Pointer grafts onto
        // Pointer: `.succ`/`.pred` step one element, `.add($n)` steps n. A
        // `void *` has no element size and dies, as in C.
        if (m == "succ" || m == "pred" || m == "add") {
            int w = of.empty() ? 0 : ncElemSize(of);   // a scalar's width, else pointer-sized
            if (w == 0) throw RakuError{Value::typeObj("X::AdHoc"), "Can't do arithmetic with a void pointer"};
            long long n = m == "add" ? (args.empty() ? 0 : args[0].toInt()) : (m == "succ" ? 1 : -1);
            return ncMakePointer("Pointer[" + of + "]", (void*)(intptr_t)(addr + n * w));
        }
        // an UNPARAMETERISED Pointer is C's `void *`, and that is what Rakudo's
        // `Pointer.of` answers — NativeHelpers::Pointer refuses arithmetic on
        // exactly this test, and "Pointer" made it look like an 8-byte element.
        if (m == "of") return Value::typeObj(of.empty() ? "void" : of);
    }
    // live CArray[T] over native memory (returned by a native call): element read
    if (inv.t == VT::Hash && inv.hashKind == "CArray" && inv.hash()->count("addr")) {
        // a LIVE array (from C, or a nativecast view) has no known length —
        // Rakudo dies the same way; NativeHelpers::Blob's suite asserts it
        // (…unless it is a struct field's view of a CArray stored there, whose
        // length the field read records as `elems`)
        if (m == "elems" && inv.hash()->count("elems")) return (*inv.hash())["elems"];
        // …and lists its elements: `.list`, `.Array`, `.Seq` and the like
        if ((m == "list" || m == "List" || m == "Array" || m == "Seq" || m == "values") &&
            inv.hash()->count("elems")) {
            long long n = (*inv.hash())["elems"].toInt();
            Value out = Value::array(); out.isList = m != "Array";
            if (m == "Seq" || m == "values") out.s = "Seq";
            for (long long k = 0; k < n; k++) out.arr()->push_back(methodCall(inv, "AT-POS", ValueList{Value::integer(k)}));
            return out;
        }
        if (m == "elems")
            throw RakuError{Value::typeObj("X::AdHoc"),
                            "Don't know how many elements a C array returned from a library has"};
        long long addr = (*inv.hash())["addr"].toInt();
        std::string of = inv.hash()->count("of") ? (*inv.hash())["of"].toStr() : "int64";
        if (m == "AT-POS" || m == "[]") return ncClassElem(ncReadElem(addr, of, args.empty() ? 0 : args[0].toInt()), nullptr, of, args.empty() ? 0 : args[0].toInt());
        if (m == "Numeric" || m == "Int") return Value::integer(addr);
        if (m == "defined") return Value::boolean(true);   // as for Pointer above
        if (m == "Bool") return Value::boolean(addr != 0);
        if (m == "of" && !of.empty()) return Value::typeObj(of);
    }
    if (inv.t == VT::Type && (inv.s == "CArray" || inv.s.rfind("CArray[", 0) == 0)) {
        std::string et = inv.s.rfind("CArray[", 0) == 0 ? inv.s.substr(7, inv.s.size() - 8)
                                                        : inv.ofType(); // parameter lives in ofType
        int esz = Interpreter::ncElemSize(et); // pointer element types are 8, not int32
        if (m == "new") {
            std::string bytes;
            ValueList strArgs;   // CArray[Str]: the array owns the strings it points at
            // `CArray[uint8].new($blob)` — a Blob/Buf argument supplies its BYTES as
            // the elements (IO::Socket::Async::SSL hands a PKCS12 file to
            // d2i_PKCS12 this way); it numified to one element before
            ValueList items;
            if (args.size() == 1 && args[0].t == VT::Str &&
                (args[0].hashKind == "Buf" || args[0].hashKind == "Blob"))
                items = args[0].blobList();
            else items = flattenArgs(args);
            for (auto& a : items) {
                if (et == "num32") { float f = (float)a.toNum(); bytes.append((const char*)&f, 4); }
                else if (et == "num64") { double d = a.toNum(); bytes.append((const char*)&d, 8); }
                else if (et == "Str") { bytes.append((size_t)esz, '\0'); strArgs.push_back(a); }
                else {
                    size_t at = bytes.size(); bytes.append((size_t)esz, '\0');
                    Interpreter::ncWriteElem((long long)(intptr_t)(bytes.data() + at), et, 0, a);
                }
            }
            Value c = Value::str(bytes); c.hashKind = "CArray";
            // A CArray IS a native buffer: its address is what C is handed, so the
            // storage must be SHARED by every copy of the value rather than
            // duplicated on the first copy. Without this, `nativecast(Pointer, $c)`
            // could only ever point at whichever copy it happened to be given.
            c.s.promote();
            // the pointers can only be filled once `c` exists to own the strings
            for (size_t k = 0; k < strArgs.size(); k++) {
                long long p = Interpreter::ncOwnStrElem(c, strArgs[k]);
                std::memcpy(c.s.mutInPlace() + k * (size_t)esz, &p, sizeof p);
            }
            // CArray[SomeCStruct]: each slot holds its object's address, and
            // the array holds the object
            if (ncElemClass(et))
                for (size_t k = 0; k < items.size(); k++) ncKeepClassElem(c, (long long)k, items[k]);
            c.enumName = et; // remember the element type
            return c;
        }
        // `CArray[int32].of` is the element type. Only the PARAMETERISED one
        // answers it — a bare `CArray.of` is no method under Rakudo either.
        if (m == "of" && !et.empty()) return Value::typeObj(et);
        if (m == "allocate") {
            long long n = args.empty() ? 0 : args[0].toInt();
            if (n < 0) throw RakuError{Value::typeObj("X::AdHoc"), // Rakudo's message for a negative count
                "Unable to allocate an array of " + std::to_string((unsigned long long)n) + " elements"};
            Value c = Value::str(std::string((size_t)n * esz, '\0')); c.hashKind = "CArray";
            c.s.promote();   // shared storage, as `new` above
            // a struct slot gets a fresh zeroed member, as Rakudo's allocate
            // does (nqp::create: no BUILD runs); a CPointer slot stays NULL
            auto eci = ncElemClass(et);
            if (eci && eci->repr != "CPointer")
                for (long long k = 0; k < n; k++) {
                    void* mem = calloc(1, (size_t)std::max<long long>(ncStructSize(eci.get()), 1));
                    Value o = Value::object(makePayload<ObjectData>());
                    o.obj()->cls = eci;
                    o.obj()->attrs["__native_ptr"] = Value::integer((long long)(intptr_t)mem);
                    o.obj()->attrs["__cstruct_owned"] = Value::boolean(true);
                    long long p = (long long)(intptr_t)mem;
                    std::memcpy(c.s.mutInPlace() + k * (size_t)esz, &p, sizeof p);
                    ncKeepClassElem(c, k, o);
                }
            c.enumName = et;
            return c;
        }
    }
    if (inv.t == VT::Str && inv.hashKind == "CArray" && m == "of" && !inv.enumName.empty())
        return Value::typeObj(inv.enumName.str());
    if (inv.t == VT::Str && inv.hashKind == "CArray" && m == "elems") {
        const std::string& et = inv.enumName;
        int esz = Interpreter::ncElemSize(et);
        return Value::integer((long long)(inv.s.size() / esz));
    }
    if (inv.t == VT::Str && inv.hashKind == "CArray" && m == "AT-POS" && args.size() == 1)
        return ncLocalAt(inv, args[0].toInt());
    // `.clone` is a second array: every copy of the Value shares one buffer
    // (C is handed its address), so the copy is of the BYTES. The strings and
    // objects the slots point at stay owned by both (the shared `ext`).
    if (inv.t == VT::Str && inv.hashKind == "CArray" && m == "clone" && args.empty()) {
        Value c = Value::str(std::string(inv.s.str()));
        c.hashKind = "CArray";
        c.enumName = inv.enumName;
        c.s.promote();
        c.extM() = inv.ext();
        return c;
    }
    // A locally-built CArray lists its ELEMENTS, decoded by its type — the
    // logical size Rakudo tracks is our byte length over the element width.
    // Digest::SHA256::Native pre-sizes one with `$hash[127] = 0`, lets the C
    // side fill the bytes, and reads the digest back with `.list».chr`;
    // falling through to the generic Str path answered ONE element (itself).
    if (inv.t == VT::Str && inv.hashKind == "CArray" &&
        (m == "list" || m == "values" || m == "List" || m == "Array" || m == "Seq" ||
         // …and every other whole-list question asks THROUGH the element list:
         // `.all ~~ Numeric` is how a module validates a CArray argument, and
         // the generic Str path junction'd the ARRAY as one item (and `Z`/`map`
         // walked its raw BYTES). One decode, then the ordinary list dispatch.
         m == "all" || m == "any" || m == "one" || m == "none" ||
         m == "map" || m == "grep" || m == "first" || m == "sort" || m == "reverse" ||
         m == "sum" || m == "min" || m == "max" || m == "join" || m == "kv" ||
         m == "pairs" || m == "keys" || m == "head" || m == "tail" || m == "flat" ||
         m == "cache" || m == "reduce" || m == "batch" || m == "rotor")) {
        std::string et = inv.enumName.empty() ? std::string("int64") : inv.enumName.str();
        int w = Interpreter::ncElemSize(et);
        long long n = w > 0 ? (long long)(inv.s.size() / (size_t)w) : 0;
        Value out = Value::array(); out.isList = (m != "Array");
        for (long long i = 0; i < n; i++)
            out.arr()->push_back(ncClassElem(ncReadElem((long long)(intptr_t)inv.s.data(), et, i), &inv, et, i));
        if (m == "Seq") out.s = "Seq";
        static const std::set<std::string> direct = {"list", "values", "List", "Array", "Seq"};
        if (direct.count(m)) return out;
        return methodCall(out, m, args, rwArgs); // the rest re-dispatch on the list
    }
    // Encoding::Registry / streaming decoder — the Rakudo encoding API that
    // Cro's HTTP parsers drive. The decoder is a stateful byte buffer with
    // line-separator-aware consumption; our strings are byte strings, so
    // iso-8859-1/ascii/utf-8 all pass bytes through unchanged.
    // Rakudo::Internals — platform probes modules use at BEGIN time.
    // NativeHelpers::Blob picks its libc via `Rakudo::Internals.IS-WIN`.
    if (inv.t == VT::Type && inv.s == "Rakudo::Internals") {
        if (m == "IS-WIN") {
#ifdef _WIN32
            return Value::boolean(true);
#else
            return Value::boolean(false);
#endif
        }
        // NORMALIZE_ENCODING('UTF-8') → 'utf8' — the canonical spelling of an
        // encoding name. It is Rakudo-internal, but a module that decodes a
        // BYTE STREAM has to call it: the spelling is what
        // `Encoding::Registry.find` is keyed by. IO::Socket::Async::SSL opens
        // its character Supply with exactly this call, so without it the whole
        // supply block threw before emitting anything and `.Supply(:enc(…))`
        // on a TLS connection yielded NOTHING — while `.Supply(:bin)` worked.
        // That one missing method is the single biggest dependency blocker in
        // the ecosystem.
        //
        // The rule, oracle-checked: lowercase, then map the aliases. Anything
        // unknown comes back lowercased and otherwise untouched (`Utf_8` is
        // `utf_8`, and `UTF-32` is `utf-32` — only `utf32` is a real name).
        if (m == "NORMALIZE_ENCODING" && args.size() >= 1) {
            std::string n = args[0].toStr();
            for (auto& c : n) c = (char)ascii::tolower((unsigned char)c);
            static const std::map<std::string, std::string> alias = {
                {"utf-8", "utf8"},        {"utf-16", "utf16"},
                {"utf-16le", "utf16le"},  {"utf-16be", "utf16be"},
                {"latin1", "iso-8859-1"}, {"latin-1", "iso-8859-1"},
                {"iso_8859-1", "iso-8859-1"},
                {"utf-8-c8", "utf8-c8"},
            };
            auto it = alias.find(n);
            return Value::str(it == alias.end() ? n : it->second);
        }
        // REGISTER-DYNAMIC '$*NAME', { PROCESS::<$NAME> = … } — the initializer
        // a module supplies for a process-wide dynamic it owns. Rakudo defers it
        // to the variable's first lookup; we run it at registration instead,
        // which needs no hook in every lookup path and differs only in WHEN.
        // The one case where that is visible is a value already in place, so an
        // existing binding is left alone rather than overwritten.
        if (m == "REGISTER-DYNAMIC" && args.size() >= 2) {
            std::string name = args[0].toStr();
            if (!name.empty() && global_ && !global_->find(name)) {
                ValueList none;
                callCallable(args[1], none);
            }
            return Value::any();
        }
    }
    // ---- REPL — a read-eval scope as an object -----------------------------
    // Rakudo's REPL is a class, and a whole family of modules drives it
    // DIRECTLY rather than through EVAL, because EVAL forgets: the sandbox in
    // Jupyter::Kernel — copied verbatim into Text::CodeProcessing, and from
    // there into the notebook/weaving dists — wants `my $x = 42` typed in one
    // cell to still be there in the next. The idiom is always the same three
    // lines: `nqp::getcomp('Raku')`, `REPL.new($compiler, {})`, then
    // `.repl-eval($code, $exception, :outer_ctx(…), :interactive(1))`.
    //
    // Rakudo persists the scope by handing back the eval'd code's CONTEXT and
    // taking it again as :outer_ctx next time. rakupp keeps the scope on the
    // REPL object instead (in `ext`), which is the same promise with none of
    // the context plumbing: two REPLs are two independent sessions, and
    // :outer_ctx is accepted and ignored. $*MAIN_CTX therefore stays
    // undefined, which the sandboxes already handle — they only assign
    // $!save_ctx `if $*MAIN_CTX`.
    if (inv.t == VT::Type && inv.s == "REPL" && m == "new") {
        Value r = Value::makeHash();
        r.hashKind = "REPL";
        if (!args.empty()) (*r.hash())["compiler"] = args[0];
        auto sess = std::make_shared<Env>();
        sess->parent = global_;
        // A ROUTINE frame, so a dynamic the session does not declare itself is
        // looked for in the CALLER rather than in global: a weaver wraps each
        // chunk in `my $*OUT = $*OUT but role {…}` to capture its output, and
        // walking through to the global $*OUT would print past the capture.
        sess->routineFrame = true;
        r.extM() = std::static_pointer_cast<void>(sess);
        return r;
    }
    if (inv.t == VT::Hash && inv.hashKind == "REPL") {
        if (m == "repl-eval") {
            auto sess = std::static_pointer_cast<Env>(inv.ext());
            if (!sess) throw RakuError{Value::typeObj("X::AdHoc"), "REPL has no session scope"};
            std::string code = args.empty() ? "" : args[0].toStr();
            Value out;
            bool failed = false; RakuError err;
            {
                // The line runs in the SESSION scope, with the caller's frame
                // still on the dynamic stack: `my` lands in the session (that
                // is the persistence), while $*OUT/$*ERR and every other
                // dynamic resolve exactly where they would have at the call.
                auto saved = tctx_.cur;
                Env* savedState = tctx_.curStateEnv;
                tctx_.dynStack.push_back(saved.get());
                struct Guard {
                    ExecContext& t; std::shared_ptr<Env> cur; Env* st;
                    ~Guard() { t.cur = std::move(cur); t.curStateEnv = st; t.dynStack.pop_back(); }
                } g{tctx_, saved, savedState};
                tctx_.cur = sess;
                tctx_.curStateEnv = sess.get(); // mainline `state` belongs to the session
                try { out = evalString(code, /*mainlinePH=*/true); }
                catch (FeatureNotBuilt&) { throw; } // a SLIM stub: loud, never a reported "line failed"
                catch (RakuError& e) { failed = true; err = e; }
            }
            if (!failed) return out;
            // Rakudo reports a failed line through the second parameter (it is
            // declared raw, so the assignment reaches the caller's variable)
            // and returns Nil. Do the same through the caller's argument
            // expression; with no lvalue to write — a literal, a call whose
            // arguments this dispatch never saw — the exception is thrown
            // instead, which the sandboxes' own CATCH picks up.
            if (rwArgs && rwArgs->size() > 1 && args.size() > 1 && args[1].t != VT::Pair) {
                Value* lv = nullptr;
                try { lv = lvalue((*rwArgs)[1].get()); } catch (RakuError&) {}
                if (lv) { *lv = exceptionFor(err); return Value::nil(); }
            }
            throw err;
        }
        // Whether the last line was cut off mid-expression (`my $x = 42 +`).
        // rakupp answers False: a REPL asks for a continuation line only when
        // it is reading from a human, and this object is being driven by a
        // program, which has no more lines to offer — the same reading Rakudo
        // takes with multi-line input disabled, and the one the weavers want,
        // since an unfinished chunk has to become a visible error.
        if (m == "input-incomplete") return Value::boolean(false);
        if (m == "ctxsave") return Value::nil(); // the context is the object; nothing to save
        if (m == "compiler") return inv.hash()->count("compiler") ? (*inv.hash())["compiler"] : Value::any();
    }
    // The built-in JSON codec, under two names. Rakupp::Internals::JSON is
    // the first-party, durable one — what rakupp's own tooling calls.
    // Rakudo::Internals::JSON is COMPATIBILITY surface: real ecosystem code
    // (zef, OpenSSL, Pakku — see docs/dev/ecosystem/RAKUDO-INTERNALS.md)
    // calls Rakudo's internal class because the language offers no
    // dependency-free JSON, and an implementation that runs real code
    // inherits that. The plan of record: someday the Rakudo spelling warns
    // that the program leans on another implementation's internals —
    // deliberately NOT yet, while the battery still measures those dists.
    if (inv.t == VT::Type && (inv.s == "Rakudo::Internals::JSON" ||
                              inv.s == "Rakupp::Internals::JSON")) {
        if (m == "from-json") {
            std::string j = args.empty() ? "" : args[0].toStr();
            size_t i = 0; Value out;
            JsonCfg cfg;
            if (!jsonParseValue(j, i, out, cfg))
                throw RakuError{Value::typeObj("X::AdHoc"), "Invalid JSON"};
            // trailing content after the top-level value is a parse error in
            // JSON::Fast, and this class advertises its semantics (the
            // internal jsonParseDoc reader stays lenient on purpose — META
            // files are trusted input, user JSON is not)
            jsonSkipWs(j, i, cfg);
            if (i != j.size())
                throw RakuError{Value::typeObj("X::AdHoc"), "Invalid JSON"};
            return out;
        }
        if (m == "to-json") {
            // pretty/spec flags are accepted but ignored (compact output)
            return Value::str(args.empty() ? "null" : jsonEncode(args[0]));
        }
    }
    // Rakupp::Internals::Blob — the engine half of the rakulib
    // NativeHelpers::Blob shadow. The ecosystem dist of that name reads
    // MoarVM's REPR memory layout by design (it scans object headers for a
    // sentinel), which no other engine can satisfy; what its DEPENDENTS
    // actually need is a data pointer into a Blob/CArray and bytes back from
    // a pointer, and those are engine primitives here.
    if (inv.t == VT::Type && inv.s == "Rakupp::Internals::Blob") {
        // --sandbox: raw addresses in and out, so native code's business
        if (g_sandboxChecks) sandboxRefuse(*this, "Rakupp::Internals::Blob." + (const std::string&)m, SandboxCap::Ffi);
        // Pointers handed to C must outlive the Value COPY they were taken
        // from: a promoted CowStr's body is retained in a ring (sharing the
        // caller's buffer, so C sees the same bytes), an inline small is
        // copied into it. 256 live buffers is far beyond any driver's
        // in-flight set; the ring exists so the process never leaks unboundedly.
        static std::deque<Ref<const StrBody>> retained;
        static std::deque<std::string> smalls;
        if (m == "addr") {
            if (args.empty()) return Value::integer(0);
            Value& b = args[0];
            if (b.t == VT::Hash && b.hash() && b.hash()->count("addr"))
                return ncMakePointer("Pointer", (void*)(intptr_t)(*b.hash())["addr"].toInt());
            // a CStruct instance carries its native body's address already —
            // the CStruct shadow's pointer-to reads it here
            if (b.t == VT::Object && b.obj()) {
                auto it = b.obj()->attrs.find("__native_ptr");
                if (it != b.obj()->attrs.end())
                    return ncMakePointer("Pointer", (void*)(intptr_t)it->second.toInt());
            }
            if (b.t != VT::Str) return Value::integer(0);
            const void* p;
            if (auto body = b.s.bodyPtr()) {
                retained.push_back(body);
                if (retained.size() > 256) retained.pop_front();
                p = body->str().data();
            }
            else {
                smalls.push_back(b.s.str());
                if (smalls.size() > 256) smalls.pop_front();
                p = smalls.back().data();
            }
            return ncMakePointer("Pointer", (void*)p);
        }
        if (m == "read") {   // read(addr-or-Pointer, bytes, kind) -> a kinded Blob
            long long addr = args.size() > 0 ? Interpreter::ncRawAddr(args[0]) : 0;
            long long n    = args.size() > 1 ? args[1].toInt() : 0;
            std::string kind = args.size() > 2 ? args[2].toStr() : "Buf";
            if (!addr || n < 0) throw RakuError{Value::typeObj("X::AdHoc"), "Blob.read: null pointer"};
            Value r = Value::str(std::string((const char*)(intptr_t)addr, (size_t)n));
            r.hashKind = kind == "utf8" ? "utf8" : kind;
            identify(r);
            return r;
        }
        if (m == "managed") { // a byte-backed CArray owns its storage; a live one borrows
            return Value::boolean(!args.empty() && args[0].t == VT::Str);
        }
    }
    if (inv.t == VT::Type && inv.s == "Encoding::Registry" && (m == "find" || m == "register")) {
        static std::map<std::string, Value> userEncodings; // fc name → registered Encoding
        auto fc = [](std::string s) { for (auto& c : s) c = (char)ascii::tolower((unsigned char)c); return s; };
        if (m == "register") {
            // pull name + alternative-names off the given Encoding-doing object
            // Every name — the main one and the alternatives — must be free;
            // an overlap is X::Encoding::AlreadyRegistered naming the clash.
            if (!args.empty()) {
                Value& enc = args[0];
                std::vector<std::string> names;
                try { names.push_back(methodCall(enc, "name", {}).toStr()); } catch (RakuError&) {}
                try {
                    Value alts = methodCall(enc, "alternative-names", {});
                    for (auto& a : alts.flatten()) names.push_back(a.toStr());
                } catch (RakuError&) {}   // the method is optional
                for (auto& n : names)
                    if (userEncodings.count(fc(n)))
                        throwTyped("X::Encoding::AlreadyRegistered", {{"name", n}},
                                   "An encoding with name '" + n + "' has already been registered");
                for (auto& n : names) userEncodings[fc(n)] = enc;
            }
            return Value::nil();
        }
        std::string name = args.empty() ? "utf-8" : args[0].toStr();
        std::string key = fc(name);
        auto uit = userEncodings.find(key);
        if (uit != userEncodings.end()) return uit->second;
        static const std::set<std::string> known = {
            "utf8", "utf-8", "utf8-c8", "utf-8-c8", "ascii", "iso-8859-1", "latin-1", "latin1",
            "utf16", "utf-16", "utf16le", "utf-16le", "utf16-le", "utf-16-le",
            "utf16be", "utf-16be", "utf16-be", "utf-16-be",
            "windows932", "windows-932", "windows1251", "windows-1251",
            "windows1252", "windows-1252", "gb2312", "gb18030", "shiftjis"};
        if (!known.count(key))
            throwTyped("X::Encoding::Unknown", {{"name", name}},
                       "Unknown string encoding '" + name + "'");
        // the CANONICAL name, and the others it answers to (Rakudo's table)
        static const std::vector<std::pair<std::string, std::vector<std::string>>> canon = {
            {"utf8", {"utf-8"}},
            {"utf8-c8", {"utf-8-c8"}},
            {"ascii", {}},
            {"iso-8859-1", {"latin-1", "latin1"}},
            {"utf16", {"utf-16"}},
            {"utf16le", {"utf-16le", "utf16-le", "utf-16-le"}},
            {"utf16be", {"utf-16be", "utf16-be", "utf-16-be"}},
            {"windows-932", {"windows932", "shiftjis"}},
            {"gb2312", {}},
            {"gb18030", {}},
            {"windows-1251", {"windows1251"}},
            {"windows-1252", {"windows1252"}},
        };
        std::string cname = key;
        Value altv = Value::array(); altv.isList = true;
        for (auto& ce : canon) {
            bool hit = ce.first == key;
            for (auto& a : ce.second) if (a == key) hit = true;
            if (!hit) continue;
            cname = ce.first;
            for (auto& a : ce.second) altv.arr()->push_back(Value::str(a));
            break;
        }
        Value e = Value::makeHash(); e.hashKind = "Encoding";
        (*e.hash())["name"] = Value::str(cname);
        (*e.hash())["alternative-names"] = altv;
        return e;
    }
    if (inv.t == VT::Hash && inv.hashKind == "Encoding") {
        if (m == "name") return (*inv.hash())["name"];
        if (m == "alternative-names") {
            auto it = inv.hash()->find("alternative-names");
            if (it != inv.hash()->end()) return it->second;
            Value none = Value::array(); none.isList = true; return none;
        }
        if (m == "decoder") {
            Value d = Value::makeHash(); d.hashKind = "Decoder";
            (*d.hash())["buffer"] = Value::str("");
            (*d.hash())["enc"] = Value::str(canonEncodingName((*inv.hash())["name"].toStr()));
            // until set-line-separators says otherwise a CRLF ends a line too,
            // and :chomp takes all of it (Rakudo's default pair)
            Value seps = Value::array();
            seps.arr()->push_back(Value::str("\n"));
            seps.arr()->push_back(Value::str("\r\n"));
            (*d.hash())["seps"] = seps;
            return d;
        }
        if (m == "encoder") { // stateless: our strings are already UTF-8 bytes
            Value e = Value::makeHash(); e.hashKind = "Encoder";
            (*e.hash())["name"] = (*inv.hash())["name"];
            // `:replacement` (True: the encoding's default) / `:replacement('f')`
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "replacement" && a.pairVal())
                    (*e.hash())["replacement"] = *a.pairVal();
            return e;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Encoder") {
        // encode-chars(Str) → Blob of the encoded bytes (CBOR::Simple uses utf8,
        // which is our internal string representation, so bytes pass through).
        if (m == "encode-chars" || m == "encode") {
            // any encoding other than UTF-8 goes through Str.encode, which knows
            // them all — and carries the encoder's :replacement along
            std::string enc = inv.hash()->count("name") ? (*inv.hash())["name"].toStr() : std::string("utf8");
            std::string canon = canonEncodingName(enc);
            if (!canon.empty() && canon != "utf8" && canon != "utf-8") {
                ValueList ea{Value::str(enc)};
                auto ri = inv.hash()->find("replacement");
                if (ri != inv.hash()->end()) {
                    Value p = Value::pair("replacement", ri->second); p.namedArg = true;
                    ea.push_back(p);
                }
                Value src = Value::str(args.empty() ? std::string() : args[0].toStr());
                return methodCall(src, "encode", ea);
            }
            Value b = Value::str(args.empty() ? std::string() : args[0].toStr());
            b.hashKind = "Blob";
            return b;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Decoder") {
        Value& buf = (*inv.hash())["buffer"];
        // The buffer holds BYTES in the decoder's encoding. Strings are UTF-8
        // inside, so for that one (and utf8-c8) bytes are chars already; any
        // other encoding decodes what it hands out, and looks for a line
        // separator by its encoded bytes. Treating every buffer as UTF-8 read
        // Cro's latin-1 header "...æµ¥" as "...浥": E6 B5 A5 is valid UTF-8.
        const std::string enc = inv.hash()->count("enc") ? (*inv.hash())["enc"].toStr() : std::string();
        const bool native = enc.empty() || enc == "utf8" || enc == "utf8-c8";
        const bool wide = enc.rfind("utf16", 0) == 0;
        auto chars = [&](const std::string& bytes) -> Value {
            if (native || bytes.empty()) return Value::str(nfcNormalize(bytes));
            Value blob = Value::str(bytes); blob.hashKind = "Buf"; blob.ofTypeM() = "uint8";
            return methodCall(blob, "decode", ValueList{Value::str(enc)});
        };
        auto sepBytes = [&](const std::string& sep) -> std::string {
            if (native) return sep;
            return methodCall(Value::str(sep), "encode", ValueList{Value::str(enc)}).s.str();
        };
        if (m == "add-bytes") { if (!args.empty()) buf.s += args[0].s; return inv; }
        if (m == "set-line-separators") {
            Value seps = Value::array();
            for (auto& a : flattenArgs(args)) seps.arr()->push_back(Value::str(a.toStr()));
            (*inv.hash())["seps"] = seps;
            return inv;
        }
        if (m == "consume-line-chars") {
            bool chomp = false, eof = false;
            for (auto& a : args) if (a.t == VT::Pair) {
                bool on = !a.pairVal() || a.pairVal()->truthy();
                if (a.s == "chomp") chomp = on;
                else if (a.s == "eof") eof = on;
            }
            size_t best = std::string::npos, bestLen = 0;
            if (inv.hash()->count("seps"))
                for (auto& sep : *(*inv.hash())["seps"].arr()) {
                    if (sep.toStr().empty()) continue;
                    const std::string ss = sepBytes(sep.toStr());
                    // a match must start on a grapheme boundary: the LF of a
                    // CRLF is not one, so an explicit "\n" leaves "a\r\nb" whole
                    const std::string cr = sep.toStr()[0] == '\n' ? sepBytes("\r") : std::string();
                    auto inside = [&](size_t at) {
                        return (wide && at % 2) ||
                               (!cr.empty() && at >= cr.size() && buf.s.str().compare(at - cr.size(), cr.size(), cr) == 0);
                    };
                    size_t pos = buf.s.find(ss);
                    while (pos != std::string::npos && inside(pos)) pos = buf.s.find(ss, pos + 1);
                    if (pos == std::string::npos) continue;
                    // earliest match wins; on a tie the longer separator wins
                    if (pos < best || (pos == best && ss.size() > bestLen)) { best = pos; bestLen = ss.size(); }
                }
            if (best == std::string::npos) {
                if (eof && !buf.s.empty()) { std::string all = buf.s; buf.s.clear(); return chars(all); }
                return Value::typeObj("Str"); // no complete line yet
            }
            std::string line = buf.s.substr(0, chomp ? best : best + bestLen);
            buf.s.erase(0, best + bestLen);
            return chars(line);
        }
        if (m == "bytes-available") return Value::integer((long long)buf.s.size());
        if (m == "consume-exactly-bytes") {
            size_t n = args.empty() ? 0 : (size_t)args[0].toInt();
            if (buf.s.size() < n) return Value::typeObj("Blob");
            Value b = Value::str(buf.s.substr(0, n)); b.hashKind = "Blob";
            buf.s.erase(0, n);
            return b;
        }
        if (m == "consume-all-chars") {
            std::string all = buf.s; buf.s.clear(); return chars(all);
        }
        // `consume-available-chars` is what a STREAM decoder yields as bytes
        // arrive, and it is not the same as draining the buffer: the tail may
        // not be a whole character yet, and even a whole one may not be a whole
        // GRAPHEME — the next chunk could open with a combining mark. Draining
        // regardless turned a multi-byte character split across two socket
        // writes into two replacement characters, so
        // IO::Socket::Async::SSL's own encoding test read "П¸ВО" for "ПИВО".
        //
        // Two hold-backs, both oracle-checked:
        //   * an incomplete trailing UTF-8 sequence, and
        //   * the final grapheme — unless it ends in a control that nothing can
        //     extend or join (LF, TAB, NUL are emitted; CR is not, because CRLF
        //     is one cluster; a space is not, because a mark can attach to it).
        if (m == "consume-available-chars") {
            if (!native) {
                // whole code units only; a trailing CR waits for a possible LF
                size_t end = wide ? buf.s.size() & ~(size_t)1 : buf.s.size();
                const std::string cr = sepBytes("\r");
                if (end >= cr.size() && buf.s.str().compare(end - cr.size(), cr.size(), cr) == 0) end -= cr.size();
                std::string out = buf.s.str().substr(0, end);
                buf.s.erase(0, end);
                return chars(out);
            }
            size_t end = utf8TextPrefixLen(buf.s.str());
            std::string out = buf.s.str().substr(0, end);
            buf.s.erase(0, end);
            return chars(out);
        }
        if (m == "consume-all-bytes" || m == "consume-available-bytes") {
            Value b = Value::str(buf.s); b.hashKind = "Blob"; buf.s.clear(); return b;
        }
        if (m == "is-empty") return Value::boolean(buf.s.empty());
    }
    // IO::Socket::Async — the async TCP surface Cro drives. listen() returns a
    // Supply that binds/accepts when tapped (see tapSupply); connect() returns a
    // kept Promise of a connected socket.
    if (inv.t == VT::Type && inv.s == "IO::Socket::Async") {
        if (g_sandboxChecks) sandboxRefuse(*this, "IO::Socket::Async." + (const std::string&)m, SandboxCap::Net);
        // UDP: bind-udp binds now, so a taken port dies here; a `udp` client
        // gets its descriptor with the first datagram it sends (or its first tap)
        if (m == "bind-udp" || m == "udp") {
            auto st = std::make_shared<UdpSockState>();
            Value s = Value::makeHash(); s.hashKind = "AsyncSocket"; s.extM() = st;
            (*s.hash())["udp"] = Value::boolean(true);
            ValueList pos;
            for (auto& a : args) {
                if (a.t != VT::Pair) { pos.push_back(a); continue; }
                if (a.s == "broadcast") st->broadcast = !a.pairVal() || a.pairVal()->truthy();
                else if (a.s == "enc" && a.pairVal()) (*s.hash())["enc"] = Value::str(canonEncodingName(a.pairVal()->toStr()));
            }
            if (m == "udp") return s;
            const std::string host = pos.size() > 0 ? pos[0].toStr() : std::string("0.0.0.0");
            const long long port = pos.size() > 1 ? pos[1].toInt() : 0;
            if (port < 0 || port > 65535)
                throwTyped("X::AdHoc", {}, "UDP port " + std::to_string(port) + " is outside 0..65535");
            sockaddr_storage addr{}; socklen_t addrLen = 0;
            if (!asyncSockAddrFwd(host, (int)port, addr, addrLen))
                throwTyped("X::AdHoc", {}, "Cannot resolve UDP host '" + host + "'");
            int fd = udpOpen(addr.ss_family, st->broadcast);
            if (fd < 0) throwTyped("X::AdHoc", {}, "Cannot create a UDP socket");
            if (::bind(fd, (sockaddr*)&addr, addrLen) < 0) {
                const std::string why = std::strerror(errno);
                ::close(fd);
                throwTyped("X::AdHoc", {}, "Cannot bind UDP " + host + ":" + std::to_string(port) + ": " + why);
            }
            st->fd = fd;
            return s;
        }
        if (m == "listen") {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash())["kind"] = Value::str("async-listen");
            (*s.hash())["host"] = args.size() > 0 ? Value::str(args[0].toStr()) : Value::str("localhost");
            (*s.hash())["port"] = args.size() > 1 ? Value::integer(args[1].toInt()) : Value::integer(0);
            // `:enc` is the encoding every socket it accepts reads and writes in
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "enc" && a.pairVal()) (*s.hash())["enc"] = Value::str(canonEncodingName(a.pairVal()->toStr()));
            return s;
        }
        if (m == "connect") {
            std::string host = args.size() > 0 ? args[0].toStr() : "localhost";
            int port = args.size() > 1 ? (int)args[1].toInt() : 0;
            sockaddr_storage addr{}; socklen_t addrLen = 0;
            bool okAddr = asyncSockAddrFwd(host, port, addr, addrLen);
            int fd = okAddr ? ::socket(addr.ss_family, SOCK_STREAM, 0) : -1;
            auto ps = std::make_shared<PromiseState>();
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
            if (fd < 0) {
                ps->done = true; ps->broken = true; ps->causeMsg = "Cannot create socket";
                ps->cause = Value::typeObj("X::IO");
                (*p.hash())["status"] = Value::str("Broken");
                return p;
            }
            bool parked = gilPark();
            int rc = ::connect(fd, (sockaddr*)&addr, addrLen);
            gilUnpark(parked);
            if (rc < 0) {
                ::close(fd);
                ps->done = true; ps->broken = true;
                ps->cause = Value::typeObj("X::IO"); ps->causeMsg = "Cannot connect to " + host + ":" + std::to_string(port);
                (*p.hash())["status"] = Value::str("Broken");
                return p;
            }
            ps->done = true; ps->result = makeAsyncSocket(fd);
            for (auto& a : args)   // `:enc`: the socket reads and writes in it
                if (a.t == VT::Pair && a.s == "enc" && a.pairVal())
                    (*ps->result.hash())["enc"] = Value::str(canonEncodingName(a.pairVal()->toStr()));
            (*p.hash())["status"] = Value::str("Kept");
            (*p.hash())["result"] = ps->result;
            return p;
        }
    }
    // A received datagram: .decode / .encode hand back a NEW datagram with the
    // payload converted (the original keeps its own)
    if ((m == "decode" || m == "encode") && inv.t == VT::Object && inv.obj()->cls &&
        inv.obj()->cls->name == "IO::Socket::Async::Datagram") {
        const Value data = inv.obj()->attrs.count("data") ? inv.obj()->attrs.at("data") : Value::any();
        const bool isStr = data.t == VT::Str && data.hashKind.empty();
        if (m == "decode" && isStr) throwTyped("X::AdHoc", {}, "This datagram holds a Str already; there is nothing to decode");
        if (m == "encode" && !isStr) throwTyped("X::AdHoc", {}, "This datagram holds bytes already; there is nothing to encode");
        Value o; o.t = VT::Object; o.setObj(makePayload<ObjectData>());
        o.obj()->cls = inv.obj()->cls;
        o.obj()->attrs = inv.obj()->attrs;
        o.obj()->attrs["data"] = methodCall(data, m, args);
        return o;
    }
    // A UDP socket: each print-to / write-to is one datagram, sent at once and
    // answered with a kept Promise of its byte count; .Supply emits one value
    // per datagram received (decoded whole — a datagram is never joined to the
    // next, not even a lone combining mark).
    if (inv.t == VT::Hash && inv.hashKind == "AsyncSocket" && inv.hash()->count("udp")) {
        auto st = udpState(inv);
        if (st && (m == "print-to" || m == "write-to")) {
            ValueList pos;
            for (auto& a : args) if (a.t != VT::Pair) pos.push_back(a);
            if (pos.size() < 3) throwTyped("X::AdHoc", {}, m + " takes a host, a port and the data to send");
            const std::string host = pos[0].toStr();
            const long long port = pos[1].toInt();
            std::string data;
            if (m == "write-to") {
                const Value& b = pos[2];
                const bool blob = b.t == VT::Str && (b.hashKind == "Buf" || b.hashKind == "Blob" || b.hashKind == "blob8" ||
                                                     b.hashKind == "buf8" || b.hashKind == "utf8");
                if (!blob)
                    throwTyped("X::TypeCheck::Binding::Parameter", {},
                               "write-to sends a Blob, not a " + methodCall(b, "^name", {}).toStr());
                data = b.s;
            }
            else {
                data = pos[2].toStr();
                if (inv.hash()->count("enc")) {
                    const std::string enc = (*inv.hash())["enc"].toStr();
                    if (!enc.empty() && enc != "utf8" && enc != "utf-8") data = encodeTextEnc(data, enc);
                }
            }
            sockaddr_storage addr{}; socklen_t addrLen = 0;
            if (port < 0 || port > 65535 || !asyncSockAddrFwd(host, (int)port, addr, addrLen))
                throwTyped("X::AdHoc", {}, "Cannot send a UDP datagram to " + host + ":" + std::to_string(port));
            long long sent = -1; bool closed;
            {   std::lock_guard<std::mutex> lk(st->m);
                closed = st->closed;
                if (!closed && st->fd < 0) st->fd = udpOpen(addr.ss_family, st->broadcast);
                if (!closed && st->fd >= 0)
                    sent = (long long)::sendto(st->fd, (const char*)data.data(), data.size(), 0, (sockaddr*)&addr, addrLen);
            }
            auto ps = std::make_shared<PromiseState>();
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
            ps->done = true;
            if (sent >= 0) { ps->result = Value::integer(sent); (*p.hash())["status"] = Value::str("Kept"); (*p.hash())["result"] = ps->result; }
            else {
                ps->broken = true; ps->cause = Value::typeObj("X::IO");
                ps->causeMsg = closed ? "The UDP socket is closed" : "UDP send failed";
                (*p.hash())["status"] = Value::str("Broken");
            }
            return p;
        }
        if (st && m == "Supply") {
            if (st->closed) return methodCall(Value::typeObj("Supply"), "from-list", ValueList{});
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash())["kind"] = Value::str("udp-read");
            (*s.hash())["socket"] = inv;
            bool bin = false, datagram = false;
            for (auto& a : args) if (a.t == VT::Pair) {
                const bool on = !a.pairVal() || a.pairVal()->truthy();
                if (a.s == "bin") bin = on;
                else if (a.s == "datagram") datagram = on;
            }
            (*s.hash())["bin"] = Value::boolean(bin);
            (*s.hash())["datagram"] = Value::boolean(datagram);
            if (inv.hash()->count("enc")) (*s.hash())["enc"] = (*inv.hash())["enc"];
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "enc" && a.pairVal())
                    (*s.hash())["enc"] = Value::str(canonEncodingName(a.pairVal()->toStr()));
            return s;
        }
        if (st && m == "close") {
            (*inv.hash())["closed"] = Value::boolean(true);
            std::unique_lock<std::mutex> lk(st->m);
            if (st->closed) return Value::boolean(true);
            st->closed = true;
            if (st->readers == 0) {
                if (st->fd >= 0) ::close(st->fd);
                st->fd = -1;
                return Value::boolean(true);
            }
            // A reader may still be polling the descriptor; it lets go within
            // one poll tick, and the port is free once it has. Closing from
            // inside the tap itself cannot wait for that: its worker lets go
            // when the handler returns.
            if (std::find(st->readerTids.begin(), st->readerTids.end(), std::this_thread::get_id()) != st->readerTids.end())
                return Value::boolean(true);
            bool parked = gilPark();
            st->cv.wait_for(lk, std::chrono::milliseconds(250), [&] { return st->readers == 0; });
            lk.unlock();
            gilUnpark(parked);
            return Value::boolean(true);
        }
        if (m == "enc") return inv.hash()->count("enc") ? (*inv.hash())["enc"] : Value::str("utf-8");
        if (st && m == "native-descriptor") { std::lock_guard<std::mutex> lk(st->m); return Value::integer(st->fd); }
        if (m == "socket-host" || m == "peer-host") return Value::typeObj("Str");
        if (m == "socket-port" || m == "peer-port") return Value::typeObj("Int");
    }
    // A connected async socket: .Supply taps a read worker; write/print are
    // synchronous sends answered with a kept Promise (Cro awaits them via
    // `whenever $socket.write(…) {}`).
    if (inv.t == VT::Hash && inv.hashKind == "AsyncSocket") {
        int fd = inv.hash()->count("fd") ? (int)(*inv.hash())["fd"].toInt() : -1;
        if (m == "Supply") {
            // a closed socket has nothing left to read: its Supply is done at once
            if (inv.hash()->count("closed"))
                return methodCall(Value::typeObj("Supply"), "from-list", ValueList{});
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash())["kind"] = Value::str("async-read");
            (*s.hash())["socket"] = inv;
            bool bin = false;
            for (auto& a : args) if (a.t == VT::Pair && a.s == "bin") bin = !a.pairVal() || a.pairVal()->truthy();
            (*s.hash())["bin"] = Value::boolean(bin);
            // the characters are decoded in the socket's encoding, or in the
            // one this Supply names (`$c.Supply(:enc<latin-1>)`)
            if (inv.hash()->count("enc")) (*s.hash())["enc"] = (*inv.hash())["enc"];
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "enc" && a.pairVal())
                    (*s.hash())["enc"] = Value::str(canonEncodingName(a.pairVal()->toStr()));
            return s;
        }
        if (m == "write" || m == "print" || m == "put" || m == "say") {
            std::string data = args.empty() ? "" : args[0].toStr();
            if (m == "put" || m == "say") data += "\n";
            // text goes out in the socket's encoding (a `.write` Blob is bytes already)
            if (m != "write" && inv.hash()->count("enc")) {
                const std::string enc = (*inv.hash())["enc"].toStr();
                if (!enc.empty() && enc != "utf8" && enc != "utf-8") data = encodeTextEnc(data, enc);
            }
            auto ps = std::make_shared<PromiseState>();
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
            ssize_t off = 0; bool ok = fd >= 0;
            while (ok && off < (ssize_t)data.size()) {
                // no gilPark: send on a local socket won't block meaningfully,
                // and parking would clobber a caller already parked on this
                // thread's one-slot park context
                ssize_t n = ::send(fd, data.data() + off, data.size() - off, 0);
                if (n <= 0) { ok = false; break; }
                off += n;
            }
            ps->done = true;
            if (ok) { ps->result = Value::integer((long long)data.size()); (*p.hash())["status"] = Value::str("Kept"); (*p.hash())["result"] = ps->result; }
            else { ps->broken = true; ps->cause = Value::typeObj("X::IO"); ps->causeMsg = "Socket write failed"; (*p.hash())["status"] = Value::str("Broken"); }
            return p;
        }
        if (m == "close") {
            if (fd >= 0) { ::shutdown(fd, SHUT_WR); }
            (*inv.hash())["closed"] = Value::boolean(true);
            return Value::boolean(true);
        }
        if (m == "native-descriptor") return Value::integer(fd);
        if (m == "socket-host" || m == "socket-port" || m == "peer-host" || m == "peer-port") {
            auto it = inv.hash()->find(m);
            return it != inv.hash()->end() ? it->second : Value::any();
        }
    }
    // CompUnit::PrecompilationId.new-from-string($src) — an opaque id; the
    // source spelling is as good an identity as any here.
    if (inv.t == VT::Type && inv.s == "CompUnit::PrecompilationId" && m == "new-from-string") {
        Value o; o.t = VT::Object; o.setObj(makePayload<ObjectData>());
        o.obj()->cls = classes_["CompUnit::PrecompilationId"];
        o.obj()->attrs["id"] = Value::str(args.empty() ? "" : args[0].toStr());
        return o;
    }
    // CompUnit::Loader.load-source($blob) — compile and run a source blob, and
    // hand back the CompUnit::Handle for it. `.unit` is the compilation unit's
    // static lexpad; the one entry anything asks for is `$?PACKAGE`, and the
    // answer is GLOBAL — a `BEGIN EVAL` inside the source compiles in its own
    // unit and must not leave its package behind (roast S29-context/eval.t).
    if (inv.t == VT::Type && inv.s == "CompUnit::Loader" &&
        (m == "load-source" || m == "load-source-file")) {
        std::string text = args.empty() ? std::string() : args[0].toStr(); // a Blob's bytes ARE its UTF-8
        if (m == "load-source-file") {
            if (g_sandboxChecks) sandboxRefuse(*this, "CompUnit::Loader.load-source-file", SandboxCap::Read);
            std::ifstream in(text);
            if (!in) throwFailedOpen(text);
            std::ostringstream ss; ss << in.rdbuf();
            text = ss.str();
        }
        evalString(text, /*mainlinePH=*/true);
        Value unit = Value::makeHash();
        (*unit.hash())["$?PACKAGE"] = Value::typeObj("GLOBAL");
        Value h; h.t = VT::Object; h.setObj(makePayload<ObjectData>());
        h.obj()->cls = classes_["CompUnit::Handle"];
        h.obj()->attrs["unit"] = unit;
        return h;
    }
    // CompUnit::PrecompilationRepository::Default.try-load($dependency) — compile
    // the dependency's source and hand back a handle whose `.unit` holds its
    // `$=pod`. The file is wrapped as a module the way Pod::Load's own string
    // path does it, so a script's mainline never runs twice and its pod is what
    // comes out. A failed compile answers Nil, which is what try-load means.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
        inv.obj()->cls->name == "CompUnit::PrecompilationRepository::Default" &&
        (m == "try-load" || m == "load")) {
        std::string src;
        if (!args.empty() && args[0].t == VT::Object && args[0].obj()) {
            auto it = args[0].obj()->attrs.find("src");
            if (it != args[0].obj()->attrs.end()) src = ioFsPath(it->second);
        }
        std::ifstream in(src);
        if (!in) {
            if (m == "load") throwTyped("X::AdHoc", {}, "Cannot load " + src);
            return Value::nil();
        }
        std::ostringstream ss; ss << in.rdbuf();
        std::string text = ss.str();
        // Rakudo refuses to precompile `use lib`: it is a compile-time statement
        // that reshapes the repository chain, so a module loaded THROUGH the
        // precompilation store (which is what this path is) dies with
        // "'use lib' cannot be precompiled and thus cannot be used in a module".
        // Pod::Load's own suite loads a script that opens with `use lib <. ./t>`
        // and asserts that refusal reaches it as X::Pod::Load::SourceErrors. The
        // scan is by statement start, skipping comment lines and pod blocks.
        {
            std::istringstream ls(text);
            std::string ln; int lineNo = 0; bool inPod = false;
            while (std::getline(ls, ln)) {
                lineNo++;
                size_t a = ln.find_first_not_of(" \t");
                if (a == std::string::npos) continue;
                std::string t = ln.substr(a);
                if (t[0] == '=') { // pod directive: `=begin`/`=pod` open, `=end`/`=cut` close
                    if (t.rfind("=begin", 0) == 0 || t.rfind("=pod", 0) == 0) inPod = true;
                    else if (t.rfind("=end", 0) == 0 || t.rfind("=cut", 0) == 0) inPod = false;
                    continue;
                }
                if (inPod || t[0] == '#') continue;
                if (t.rfind("use lib", 0) == 0 && (t.size() == 7 || !(ascii::isalnum((unsigned char)t[7]) || t[7] == '-' || t[7] == ':')))
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        "===SORRY!=== Error while compiling " + src + "\n"
                        "'use lib' cannot be precompiled and thus cannot be used in a module\n"
                        "at " + src + ":" + std::to_string(lineNo)};
            }
        }
        // Precompilation never RUNS a mainline, and `$=pod` is a parse-time
        // product here — so the pod DOM is read straight off the source, and a
        // `unit module` file (Pod::Load's own t/unit.pod6) needs no wrapping
        // that would make it illegal.
        Value pod = Value::array();
        *pod.arr() = parsePod(text);
        Value unit = Value::makeHash();
        (*unit.hash())["$=pod"] = pod;
        Value h; h.t = VT::Object; h.setObj(makePayload<ObjectData>());
        h.obj()->cls = classes_["CompUnit::Handle"];
        h.obj()->attrs["unit"] = unit;
        return h;
    }
    // `Proc.new(:out, :err, :merge)` — an UNSPAWNED Proc. Rakudo's Proc is a
    // class you may build first and run later (`.spawn`/`.shell`), and the
    // adverbs chosen here decide what the later run captures. Clipboard reads a
    // pasteboard exactly so: `my $proc = Proc.new(:out); $proc.shell($cmd);
    // $proc.out.slurp(:close)`. Until now Proc objects existed only as run()'s
    // answer, and `Proc.new` was a missing method.
    if (inv.t == VT::Type && inv.s == "Proc" && m == "new") {
        Value p = Value::makeHash(); p.hashKind = "Proc";
        (*p.hash())["exitcode"] = Value::integer(-1);
        (*p.hash())["out-str"] = Value::str("");
        (*p.hash())["err-str"] = Value::str("");
        (*p.hash())["unspawned"] = Value::boolean(true);
        for (auto& a : args) {
            if (a.t != VT::Pair) continue;
            bool on = a.pairVal() ? a.pairVal()->truthy() : true;
            if (a.s == "out" || a.s == "err" || a.s == "merge" || a.s == "in")
                (*p.hash())["want-" + a.s.str()] = Value::boolean(on);
        }
        return p;
    }
    // CompUnit::DependencySpecification.new(:short-name<Foo>, …) — a module dependency
    // descriptor. Requires a Str short-name; the version/auth/api matchers default True.
    if (inv.t == VT::Type && inv.s == "CompUnit::DependencySpecification" && m == "new") {
        Value shortName; bool haveSN = false;
        for (auto& a : args) if (a.t == VT::Pair && a.s == "short-name") { shortName = a.pairVal() ? *a.pairVal() : Value::any(); haveSN = true; }
        if (!haveSN || shortName.t != VT::Str)
            throw RakuError{Value::typeObj("X::AdHoc"), "CompUnit::DependencySpecification requires a Str :short-name"};
        Value o = Value::makeHash(); o.hashKind = "DependencySpec";
        (*o.hash())["short-name"] = shortName;
        for (const char* k : {"version-matcher", "auth-matcher", "api-matcher"}) {
            Value v = Value::boolean(true);
            for (auto& a : args) if (a.t == VT::Pair && a.s == k && a.pairVal()) v = *a.pairVal();
            (*o.hash())[k] = v;
        }
        return o;
    }
    // Buf/Blob.new(elem, elem, …) — a byte buffer, stored as a Str of bytes.
    // blob16/32/64 (and utf16/32) pack each element as a little-endian word;
    // ofType carries the element type (Digest's blob32 word arithmetic).
    // the named element-width types (blob8, buf32, …) and the equivalent
    // parameterized spellings (`Blob[uint8]`, `Buf[uint32]`) build the same thing
    if (inv.t == VT::Type && (m == "new" || m == "allocate") &&
        (inv.s == "buf8" || inv.s == "blob8" || inv.s == "utf8" ||
         inv.s == "buf16" || inv.s == "blob16" || inv.s == "utf16" ||
         inv.s == "buf32" || inv.s == "blob32" || inv.s == "utf32" ||
         inv.s == "buf64" || inv.s == "blob64" ||
         inv.s.rfind("Blob[", 0) == 0 || inv.s.rfind("Buf[", 0) == 0 ||
         ((inv.s == "Blob" || inv.s == "Buf") && !inv.ofType().empty()))) {
        // the width comes from the name (blob32) or the parameter (Blob[uint32])
        const std::string& wsrc = inv.ofType().empty() ? inv.s : inv.ofType();
        int w = wsrc.find("16") != std::string::npos ? 2
              : wsrc.find("32") != std::string::npos ? 4
              : wsrc.find("64") != std::string::npos ? 8 : 1;
        std::string bytes;
        std::function<void(const Value&)> add = [&](const Value& v) {
            if ((v.t == VT::Array || v.t == VT::Range) && !(v.t == VT::Array && !v.arr())) { for (auto& e : v.flatten()) add(e); }
            else if (v.t == VT::Str && (v.hashKind == "Blob" || v.hashKind == "Buf")) bytes += v.s; // copy an existing buffer's bytes
            else {
                // low bits, not a saturated toInt(): a 64-bit word above 2^63-1 is
                // ordinary in a blob64 (SHA-512's constants are full of them)
                unsigned long long x = (v.t == VT::Int && v.big())
                    ? v.big()->toU64Wrap() : (unsigned long long)v.toInt();
                for (int k = 0; k < w; k++) bytes += (char)(unsigned char)((x >> (8 * k)) & 0xFF);
            }
        };
        if (m == "allocate") {
            long long n2 = args.empty() ? 0 : args[0].toInt();
            if (n2 < 0) throw RakuError{Value::typeObj("X::AdHoc"), // Rakudo's message for a negative count
                "Unable to allocate an array of " + std::to_string((unsigned long long)n2) + " elements"};
            if (args.size() > 1 && args[1].t != VT::Pair) {
                // `.allocate(10, pattern)` — filled with the pattern (a list, a
                // Blob, or one value), repeated as far as it goes
                const Value& pat = args[1];
                if ((pat.t == VT::Str && pat.hashKind.empty() && !pat.isAllomorph()) || pat.t == VT::Object)
                    throwTypedV("X::TypeCheck", {{"got", pat}, {"operation", Value::str("allocate")}},
                                "Type check failed in allocate; expected Int but got " + pat.typeName());
                std::string one;
                std::swap(one, bytes);
                add(pat);                        // the pattern's bytes, as .new would lay them out
                std::swap(one, bytes);
                bytes.clear();
                if (!one.empty())
                    while ((long long)bytes.size() < n2 * w) bytes += one;
                bytes.resize((size_t)(n2 * w), '\0');
            }
            else bytes.assign((size_t)(n2 * w), '\0');
        }
        else for (auto& a : args) add(a);
        Value b = Value::str(bytes); // buf*/Buf[T] are the mutable spellings
        b.hashKind = (inv.s.rfind("buf", 0) == 0 || inv.s.rfind("Buf", 0) == 0) ? "Buf" : "Blob";
        // blob8 IS Blob[uint8] — the [T] always shows — but a SIGNED parameter
        // keeps its sign: `Buf[int8].new(255)[0]` is -1, not 255. The element
        // type was forced unsigned here, so every signed buffer read back as a
        // magnitude (Binary::Structured decodes signed fields this way).
        // the parameter may arrive as the ofType ("int8") or inside the name
        // ("Buf[int8]"), and `buf8`/`blob8` are unsigned by their own spelling
        std::string et = wsrc;
        if (size_t lb = et.find('['); lb != std::string::npos) et = et.substr(lb + 1, et.find(']', lb) - lb - 1);
        bool signedElem = et.compare(0, 3, "int") == 0;
        b.ofTypeM() = (signedElem ? "int" : "uint") + std::to_string(w * 8);
        // …and a typed Blob is NAMED by it: `blob8.new(1).^name` and
        // `Blob[uint8].new(1).^name` are "Blob[uint8]" (Buf shows its [T] from
        // the element type; Blob reads its name)
        if (b.hashKind == "Blob") b.enumName = "Blob[" + b.ofType() + "]";
        b.s.promote();   // a native buffer needs stable, shared storage
        if (b.hashKind == "Buf") identify(b);
        return b;
    }
    if (inv.t == VT::Type &&
        (inv.s == "Set" || inv.s == "SetHash" || inv.s == "Bag" || inv.s == "BagHash" ||
         inv.s == "Mix" || inv.s == "MixHash") && m == "new-from-pairs") {
        // pairs contribute key => WEIGHT (unlike .new, where a Pair is an element)
        ValueList items;
        for (auto& a : args) {
            if (a.t == VT::Range && a.rTo() >= 9000000000000000000LL)
                throwTyped("X::Cannot::Lazy", {{"what", inv.s}},
                           "Cannot create a " + inv.s + " from a lazy list");
            if (a.t == VT::Array || a.t == VT::Range) for (auto& x : a.flatten()) items.push_back(x);
            else items.push_back(a);
        }
        for (auto& x : items)
            if (x.t != VT::Pair && x.t != VT::Str && !x.isNumeric())
                throw RakuError{Value::typeObj("X::AdHoc"),
                                "Found invalid value " + x.gist() + " in " + inv.s + ".new-from-pairs"};
        return makeBaggy(items, inv.s, /*pairsAsElements=*/false);
    }
    if (inv.t == VT::Type &&
        (inv.s == "Set" || inv.s == "SetHash" || inv.s == "Bag" || inv.s == "BagHash" ||
         inv.s == "Mix" || inv.s == "MixHash") && m == "new") {
        // single-arg rule: one iterable arg contributes its elements (an itemized
        // `$[...]` resists and stays whole); with several args each arg is ONE
        // element (`Set.new(@a, [3,4])` has two elements)
        ValueList pos; // bare `a => "b"` is a NAMED arg — .new swallows it silently
        for (auto& a : args) if (!a.namedArg) pos.push_back(a);
        ValueList items;
        for (auto& a : pos) {
            bool lazy = endlessLazy(a) || declLazyLive(a) || (a.t == VT::Range && a.rTo() >= 9000000000000000000LL);
            if (!lazy && a.t == VT::Range) { ValueList none; lazy = methodCall(a, "is-lazy", none).truthy(); }
            if (lazy)
                throwTyped("X::Cannot::Lazy", {{"what", inv.s}},
                           "Cannot create a " + inv.s + " from a lazy list");
        }
        // a quanthash arg is ONE element (Bag.new(set <a b c>) has 1 elem);
        // a plain Hash still iterates its pairs under the single-arg rule
        bool wholeQuant = pos.size() == 1 && pos[0].t == VT::Hash && quantValueType(pos[0].hashKind);
        if (pos.size() == 1 && !pos[0].itemized && !wholeQuant) {
            for (auto& x : toList(pos[0])) items.push_back(x);
        }
        else for (auto& a : pos) items.push_back(a);
        // a COERCION key type (`Set.^parameterize(Int())`, keyof `Int(Any)`)
        // coerces each element to its target, which is also where a bad one
        // croaks (X::Str::Numeric for "a" → Int)
        std::string coerceTarget;
        {
            const std::string of = inv.ofType();
            size_t lp = of.find('(');
            if (lp != std::string::npos && lp > 0 && of.back() == ')') coerceTarget = of.substr(0, lp);
        }
        if (!coerceTarget.empty())
            for (auto& x : items) {
                if (x.t == VT::Pair && x.pairKey()) continue;
                x = coerceToType(x, coerceTarget);
                if (x.t == VT::Hash && x.hashKind == "Failure") sinkValue(x);   // "a" as an Int key croaks (X::Str::Numeric)
            }
        Value out = makeBaggy(items, inv.s, /*pairsAsElements=*/true);
        if (!coerceTarget.empty()) { if (out.hash()) out.ofTypeM() = inv.ofType(); return out; }
        if (!inv.ofType().empty() && out.hash()) { // Set[Str].new(...) enforces the key type
            for (auto& kv : *out.hash()) {
                Value orig = kv.second.elemKey() ? *kv.second.elemKey() : Value::str(kv.first);
                if (!typeOrSubsetMatches(orig, inv.ofType()))
                    throw RakuError{Value::typeObj("X::TypeCheck::Binding"),
                        "Type check failed for " + inv.s + " key; expected " +
                        inv.ofType() + " but got " + orig.gist()};
            }
            out.ofTypeM() = inv.ofType();
        }
        return out;
    }
    if (inv.t == VT::Hash && inv.hashKind == "StrDistance") {
        auto fld = [&](const char* k) { auto it = inv.hash()->find(k); return it != inv.hash()->end() ? it->second : Value::str(""); };
        if (m == "before" || m == "after") return fld(m.c_str());
        if (m == "Str" || m == "gist") return fld("after"); // "$dist" interpolates the resulting string
        if (m == "Bool") return Value::boolean(fld("before").toStr() != fld("after").toStr());
        if (m == "Rat" || m == "FatRat" || m == "Numeric" || m == "Int" || m == "Num" || m == "chars") {
            // a tr/// result carries the substitution count; .new-built ones numify to .after.chars
            long long c = strDistance(fld("before").toStr(), fld("after").toStr());
            if (m == "Num") return Value::number((double)c);
            if (m == "Int" || m == "Numeric" || m == "chars") return Value::integer(c);
            Value v = Value::rat(BigInt(c), BigInt(1));
            if (m == "FatRat") v.fatRatM() = true;
            return v;
        }
    }
    if (inv.t == VT::Str && inv.hashKind == "Version") {
        if (m == "parts") { // numeric parts as Ints, everything else as Strs
            Value out = Value::array(); out.isList = true;
            const std::string& s = inv.s;
            size_t i = 0;
            while (i < s.size()) {
                unsigned char c = s[i];
                if (ascii::isdigit(c)) { size_t j = i; while (j < s.size() && ascii::isdigit((unsigned char)s[j])) j++;
                    out.arr()->push_back(Value::integer(std::atoll(s.substr(i, j - i).c_str()))); i = j; }
                else if (ascii::isalpha(c)) { size_t j = i; while (j < s.size() && ascii::isalpha((unsigned char)s[j])) j++;
                    out.arr()->push_back(Value::str(s.substr(i, j - i))); i = j; }
                // a '*' part is the STRING "*", as Rakudo stores it — a Whatever
                // here made META6's `$ver.parts[0] eq 'v'` curry into a (truthy)
                // WhateverCode, which shifted v"*" down to an EMPTY version and
                // let Test::META's asterisk check pass vacuously
                else if (c == '*') { out.arr()->push_back(Value::str("*")); i++; }
                // an underscore is a PART, not a separator: `1.2.1_01` is (1 2 1 _ 1)
                else if (c == '_') { out.arr()->push_back(Value::str("_")); i++; }
                // …and a non-ASCII letter (α, β) is a word part like an ASCII one
                else if (c >= 0x80) { size_t j = i; while (j < s.size() && (unsigned char)s[j] >= 0x80) j++;
                    out.arr()->push_back(Value::str(s.substr(i, j - i))); i = j; }
                else i++;
            }
            return out;
        }
        if (m == "Str") return Value::str(inv.s);
        if (m == "gist") return Value::str("v" + inv.s);
        // .raku round-trips: only a version whose spelling can follow a bare
        // `v` (it starts with a digit) prints as the literal; Rakudo spells
        // Version.new('*') out in full, since `v*` is not valid source
        if (m == "raku")
            return Value::str(!inv.s.empty() && ascii::isdigit((unsigned char)inv.s[0])
                              ? "v" + inv.s : "Version.new('" + inv.s + "')");
        if (m == "plus") return Value::boolean(!inv.s.empty() && inv.s.back() == '+');
        if (m == "whatever") return Value::boolean(inv.s.find('*') != std::string::npos);
    }
    if (inv.t == VT::Type && inv.s == "Slip" && m == "new") {
        Value sl = Value::array(args); sl.isList = true; sl.s = "Slip"; return sl;
    }
    if (inv.t == VT::Type && (m == "Baggy" || m == "Setty" || m == "Mixy")) {
        // quanthash coercion types: Set.Baggy is Bag, BagHash.Setty is SetHash, …
        static const std::map<std::string, std::map<std::string, std::string>> co = {
            {"Baggy", {{"Set","Bag"},{"SetHash","BagHash"},{"Bag","Bag"},{"BagHash","BagHash"},{"Mix","Mix"},{"MixHash","MixHash"}}},
            {"Setty", {{"Set","Set"},{"SetHash","SetHash"},{"Bag","Set"},{"BagHash","SetHash"},{"Mix","Set"},{"MixHash","SetHash"}}},
            {"Mixy",  {{"Set","Mix"},{"SetHash","MixHash"},{"Bag","Mix"},{"BagHash","MixHash"},{"Mix","Mix"},{"MixHash","MixHash"}}},
        };
        auto ci2 = co.find(m); auto ti = ci2->second.find(inv.s);
        if (ti != ci2->second.end()) return Value::typeObj(ti->second);
    }
    if (inv.t == VT::Type && inv.s == "Bool" && (m == "pick" || m == "roll")) {
        ValueList tf{Value::boolean(false), Value::boolean(true)};
        Value l = Value::array(tf); l.isList = true;
        return methodCall(l, m, args); // Bool.pick(*) shuffles (False, True)
    }
    if (inv.t == VT::Type && inv.s == "IO::Path" && m == "new") {
        // the path's SPEC: `:SPEC(…)`, else the dynamic $*SPEC. A built-in
        // flavor makes a path of that flavor; a user subclass of IO::Spec
        // rides along for `.SPEC` to answer (S32-io/io-path.t)
        Value specArg;
        for (auto& a : args) if (a.t == VT::Pair && a.s == "SPEC" && a.pairVal()) specArg = *a.pairVal();
        if (specArg.t != VT::Type)
            if (Value* sp = findDynamicLenient("$*SPEC")) specArg = *sp;
        std::string flavor, userSpec;
        if (specArg.t == VT::Type) {
            const std::string sn = specArg.s.str();
            if (sn.rfind("IO::Spec::", 0) == 0) {
                const std::string fl = sn.substr(10);
                if (fl == "Win32" || fl == "Cygwin" || fl == "QNX") flavor = fl;
            }
            else if (sn != "IO::Spec" && classes_.count(sn)) userSpec = sn;
        }
        std::string path; bool havePositional = false;
        for (auto& a : args) if (a.t != VT::Pair) { path = a.toStr(); havePositional = true; break; }
        // the parts constructor: `.new(:basename, :dirname, :volume)` builds the
        // path from the three pieces `.parts` takes it apart into, so a program
        // can put back what it took apart. A `.` dirname contributes nothing.
        if (!havePositional) {
            std::string vol, dir, base;
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) {
                if (a.s == "volume") vol = a.pairVal()->toStr();
                else if (a.s == "dirname") dir = a.pairVal()->toStr();
                else if (a.s == "basename") base = a.pairVal()->toStr();
            }
            if (!base.empty()) {
                // (a flavored SPEC joins the pieces its own way)
                Value jr;
                ValueList ja{Value::str(vol), Value::str(dir), Value::str(base)};
                if (!flavor.empty() && ioSpecMethod(*this, "IO::Spec::" + flavor, "join", ja, jr)) path = jr.toStr();
                else {
                    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
                    path = vol + (dir.empty() || dir == "." ? "" : (dir == "/" ? "/" : dir + "/")) + base;
                }
                havePositional = true;
            }
        }
        // an empty path names no file, and is refused where it is written
        // rather than at some later open of ""
        if (!havePositional || path.empty())
            throw RakuError{Value::typeObj("X::AdHoc"),
                            "Must specify a non-empty string as a path"};
        rejectNulPath(path);
        Value p = Value::str(path); p.hashKind = "IO";
        if (!flavor.empty()) p.enumName = flavor;
        if (!userSpec.empty()) p.enumType = userSpec;
        // the `:CWD` is the directory this path is relative to; it rides in
        // ofType, which a path value has no other use for. Captured from the
        // current $*CWD by default (Rakudo's model); an explicit :CWD wins.
        p.ofTypeM() = cwdName();
        for (auto& a : args)
            if (a.t == VT::Pair && a.s == "CWD" && a.pairVal()) p.ofTypeM() = a.pairVal()->toStr();
        return p;
    }
    // IO::Spec::Unix / ::Win32 — the per-OS path grammar an IO::Path routes
    // through. A type object with class methods; `.new` answers itself.
    // IO::Spec::Unix / ::Win32 — only the pieces that were missing; everything
    // else (curupdir, rootdir, devnull, canonpath, …) already has a handler
    // further down and must not be shadowed here.
    if (inv.t == VT::Type && inv.s.rfind("IO::Spec::", 0) == 0) {
        if (m == "new") return inv;
        if (m == "dir-sep") return Value::str(inv.s.find("Win32") != std::string::npos ? "\\" : "/");
    }
    // IO::Path flavors: the path value keeps its OS flavor in enumName and
    // routes volume/dirname/basename/cleanup through that IO::Spec
    if (inv.t == VT::Type && inv.s.rfind("IO::Path::", 0) == 0 && m == "new") {
        static const std::set<std::string> kPathFlavors = {"Unix", "Win32", "Cygwin", "QNX"};
        std::string fl = inv.s.substr(10);
        if (kPathFlavors.count(fl)) {
            // the parts constructor, as IO::Path's: `.new(:volume<C:>, :$basename)`
            // joins the pieces with the flavor's separator (a `.` dirname adds none)
            std::string path;
            bool haveParts = false;
            if (!args.empty() && args[0].t == VT::Pair) {
                std::string vol, dir, base;
                for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "volume") vol = a.pairVal()->toStr();
                    else if (a.s == "dirname") dir = a.pairVal()->toStr();
                    else if (a.s == "basename") base = a.pairVal()->toStr();
                }
                if (!base.empty()) {
                    const std::string sep = fl == "Win32" ? "\\" : "/";
                    path = vol + (dir.empty() || dir == "." ? "" : dir + sep) + base;
                    haveParts = true;
                }
            }
            if (!haveParts && (args.empty() || args[0].t == VT::Pair || args[0].toStr().empty()))
                throw RakuError{Value::typeObj("X::AdHoc"),
                                "Must specify a non-empty string as a path"};
            if (!haveParts) path = args[0].toStr();
            rejectNulPath(path);
            Value p = Value::str(path); p.hashKind = "IO"; p.enumName = fl;
            p.ofTypeM() = cwdName();
            return p;
        }
    }
    if (inv.t == VT::Type && inv.s == "CurrentThreadScheduler" && m == "new") {
        Value v = Value::makeHash(); v.hashKind = "Scheduler";
        (*v.hash())["name"] = Value::str("CurrentThreadScheduler");
        (*v.hash())["sync"] = Value::boolean(true);
        return v;
    }
    if (inv.t == VT::Hash && inv.hashKind == "Scheduler") {
        if (m == "cue" && !args.empty()) {
            Value code = args[0];
            double delay = 0, every = 0; long long times = 0;
            bool sawIn = false, sawAt = false, sawTimes = false;
            Value stopF, catchF;
            for (auto& a : args) {
                if (a.t != VT::Pair || !a.pairVal()) continue;
                if (a.s == "in") sawIn = true;
                if (a.s == "at") sawAt = true;
                if (a.s == "times") sawTimes = true;
                if (a.s == "in" || a.s == "at") {
                    double v = a.s == "at" ? instantSecsOf(*a.pairVal()) : a.pairVal()->toNum();
                    if (std::isnan(v)) throw RakuError{Value::typeObj("X::Scheduler::CueInNaNSeconds"),
                        "Cannot pass NaN as a number of seconds to Scheduler.cue"};
                    delay = a.s == "in" ? v : std::max(0.0, v - epochNowSecs()); // :at is absolute, on the `now` clock (whole-second time() lost the fraction)
                }
                else if (a.s == "every") {
                    every = a.pairVal()->toNum();
                    if (std::isnan(every)) throw RakuError{Value::typeObj("X::Scheduler::CueInNaNSeconds"),
                        "Cannot pass NaN as a number of seconds to Scheduler.cue"};
                    if (std::isinf(every)) every = 0; // ±Inf every: run once, immediately
                }
                else if (a.s == "times") times = a.pairVal()->toInt();
                else if (a.s == "stop") stopF = *a.pairVal();
                else if (a.s == "catch") catchF = *a.pairVal();
            }
            if (catchF.t != VT::Code && inv.hash()->count("uncaught_handler"))
                catchF = (*inv.hash())["uncaught_handler"]; // scheduler-level handler
            if (sawIn && sawAt)
                throw RakuError{Value::typeObj("X::Scheduler::Cue"), "Cannot specify both :at and :in"};
            if (every > 0 && sawTimes && stopF.t == VT::Code)
                throw RakuError{Value::typeObj("X::Scheduler::Cue"), "Cannot specify :every, :times and :stop together"};
            if (inv.hash()->count("sync")) { // CurrentThreadScheduler: run inline, now
                bool sawEvery = false;
                for (auto& a : args) if (a.t == VT::Pair && a.s == "every") sawEvery = true;
                if (sawEvery) // no repetition on the inline scheduler, as in Rakudo
                    throw RakuError{Value::typeObj("X::Scheduler::Cue"),
                        "Cannot specify :every in cue on the CurrentThreadScheduler"};
                if (std::isinf(delay) && delay > 0) { // :in(Inf)/:at(Inf): never runs (-Inf runs NOW)
                    Value c = Value::makeHash(); c.hashKind = "Cancellation";
                    c.extM() = std::make_shared<CueState>();
                    return c;
                }
                long long target = times > 0 ? times : 1;
                for (long long i = 0; i < target; i++) {
                    if (stopF.t == VT::Code) { ValueList na; if (callCallable(stopF, na).truthy()) break; }
                    try { ValueList na; callCallable(code, na); }
                    catch (const RakuError& e) {
                        if (catchF.t != VT::Code) throw;
                        ValueList ca{exceptionFor(e)}; callCallable(catchF, ca);
                    }
                }
                Value c = Value::makeHash(); c.hashKind = "Cancellation";
                c.extM() = std::make_shared<CueState>();
                return c;
            }
            if (delay < 0 || std::isnan(delay)) delay = 0; // past instants / -Inf run immediately
            return cueJob(code, delay, every, times, stopF, catchF);
        }
        if (m == "loads") {
            sleepYield(0.002); // let cued workers run — a `1 while .loads` spin must not starve them
            return Value::integer(cuedLoads_.load());
        }
    }
    return std::nullopt;   // not handled here — fall through to the next segment
}

} // namespace rakupp
