// BuiltinsRegister3.cpp — registerBuiltins, the last piece: sleep, signals, NativeCall subs, set operators
//
// One of the parts BuiltinsParts.h lists; what they share is declared there.
#include "BuiltinsParts.h"

namespace rakupp {

// The readable text of a broken Promise's cause. When the cause is a real
// exception object, its OWN `.message` (or `.Str`) is authoritative — the same
// accessors `die` consults — and it beats the string stored alongside it, which
// is only ever that object's gist. Reporting the stored one is why an uncaught
// `await` on a broken Promise said `X::Cro::HTTP::Error::Client<…>` where the
// class's own message() says "Server responded with 404 Not Found". A cause
// that is a plain string (`$p.break("text")`) has no accessors and keeps it.
static std::string causeMessageOf(Interpreter& I, const Value& cause, const std::string& stored) {
    if (cause.t == VT::Object && cause.obj()) {
        for (const char* acc : {"message", "Str"}) {
            try {
                ValueList none;
                Value m = I.methodCall(cause, acc, none);
                if (m.t == VT::Str && !m.s.empty()) return m.s.str();
            } catch (...) {}
        }
    }
    return stored.empty() ? std::string("Promise broken") : stored;
}

// registerBuiltins, continued. Split for compile time: each piece ends by
// calling the next, so the registrations run in the original order (a later
// one of the same name still replaces an earlier one).
void Interpreter::registerBuiltinsPart5() {
    auto& B = builtins_;
    // `sleep-until $instant` sleeps until that moment and answers whether it
    // actually waited — an instant already past answers False without sleeping.
    B["sleep-until"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        // measured against the same high-resolution clock `now` reads, so a
        // fraction-of-a-second target is not lost to truncation
        double now = epochNowSecs();
        double target = instantSecsOf(a[0]);
        if (target <= now) return Value::boolean(false);
        I.sleepYield(target - now);
        return Value::boolean(true);
    };
    // signal(SIGINT, …) — a Supply that emits the Signal enum value each time the
    // process receives one of the named OS signals. Standard Ctrl-C shutdown:
    // `react { whenever signal(SIGINT) { $server.stop; done } }`.
    B["signal"] = [](Interpreter&, ValueList& a) -> Value {
        Value s = Value::makeHash(); s.hashKind = "Supply";
        (*s.hash())["kind"] = Value::str("signal");
        Value sigs = Value::array();
        for (auto& v : a) { int n = signalNumberOf(v); if (n > 0) sigs.arr()->push_back(Value::integer(n)); }
        (*s.hash())["signals"] = sigs;
        return s;
    };
    B["sleep-timer"] = [](Interpreter& I, ValueList& a) -> Value {
        I.sleepYield(a.empty() ? 0 : a[0].toNum());
        // the time NOT slept — a Duration, and 0 unless the sleep was interrupted
        Value d = Value::number(0); d.hashKind = "Duration"; return identify(d);
    };
    B["done"] = [](Interpreter& I, ValueList&) -> Value {
        // `done` inside an on-demand supply activation ends its stream: fire the
        // downstream done callback and close the activation's inner taps.
        if (!I.tctx_.tapStack.empty()) {
            auto ctx = I.tctx_.tapStack.back();
            // S-63: a `done` reached from the TAPPER's callback while the supply
            // BODY is running has no supply of its own to end — the tap block is
            // not lexically inside one. Rakudo makes that a run-time error, and
            // so do we; the same call during a whenever's delivery is S-64, and
            // ends the supply.
            // …but only when there is no react to belong to: a `whenever` body in
            // a react is fed BY a supply block's own emit, and its `done` ends
            // that react (Roast syntax.t's synchronously-emitting source).
            if (ctx->emitting > 0 && ctx->inBody && !ctx->collect && I.reactStack_.empty())
                I.throwTypedV("X::ControlFlow",
                              {{"illegal", Value::str("done")}, {"enclosing", Value::str("supply or react")}},
                              "done without supply or react");
            ctx->done = true;
            ctx->clearQueue();    // S-54: nothing that was waiting still happens
            if (!ctx->collect) {
                // S-64: `done` from inside the TAPPER's own callback ends the
                // supply but does not call that tapper's done callback — it is
                // already inside the tap, and the stream stops under it. Inside a
                // REACT the "tapper" is the react's own plumbing, which must be
                // told, or the react waits for a source that has already stopped.
                if ((ctx->emitting == 0 || !I.reactStack_.empty()) && ctx->doneCb.t == VT::Code) {
                    ValueList na; try { I.callCallable(ctx->doneCb, na); } catch (...) {}
                }
                ctx->doneFired = true;
                I.closeTapHandle(ctx->tap);
            }
            throw DoneEx{}; // done also EXITS the enclosing whenever block / supply body (Rakudo)
        }
        // `done` inside a react block closes its loop.
        if (!I.reactStack_.empty()) {
            auto ctx = I.reactStack_.back();
            { std::lock_guard<std::mutex> lk(ctx->m); ctx->closed = true; ctx->cv.notify_all(); }
            throw DoneEx{}; // …and the enclosing whenever/react body
        }
        // outside a supply or a react there is nothing to end
        I.throwTypedV("X::ControlFlow",
                      {{"illegal", Value::str("done")}, {"enclosing", Value::str("supply or react")}},
                      "done without supply or react");
    };
    B["supply"] = [](Interpreter& I, ValueList& a) -> Value {
        // supply { … } is ON-DEMAND: the block runs when the supply is tapped
        // (tapSupply), with emit routed to the tap. Value-context consumers
        // (.list, for, await) drain it eagerly via drainSupplyBlock — the same
        // values the old eager model produced, just computed at consumption.
        Value s = Value::makeHash(); s.hashKind = "Supply";
        (*s.hash())["block"] = (!a.empty() && a.back().t == VT::Code) ? a.back() : Value::nil();
        return s;
    };
    B["emit"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.empty() ? Value::any() : a[0];
        static const bool kTapTrace = std::getenv("RAKUPP_TAP_TRACE") != nullptr; // hot path: probe once
        if (kTapTrace)
            fprintf(stderr, "[emit] depth=%zu kind=%s collect=%d cb=%d\n", I.tctx_.tapStack.size(),
                    v.typeName().c_str(),
                    I.tctx_.tapStack.empty() ? -1 : (int)!!I.tctx_.tapStack.back()->collect,
                    I.tctx_.tapStack.empty() ? -1 : (int)(I.tctx_.tapStack.back()->emitCb.t == VT::Code));
        if (!I.tctx_.tapStack.empty()) {
            auto ctx = I.tctx_.tapStack.back();
            // Nothing follows done (S-66) — and a body that goes on emitting is a
            // body that has not noticed: `supply { loop { emit … } }` ends when
            // its consumer says done, so the emit that comes next is where the
            // block is unwound. (The on-demand producer of S-15 keeps running:
            // its Supplier has its own emit route, not this one.)
            if (ctx->done) throw DoneEx{};
            // The body is running now, so its lexicals exist: a CLOSE phaser reads
            // and writes THEM (`until my $done { emit … }; CLOSE { $done = True }`
            // ends the loop when the consumer says done; S17-supply/syntax.t), not
            // the definition scope it was collected against
            if (ctx->inBody && !ctx->closeRebound && ctx->tap && I.tctx_.cur) {
                ctx->closeRebound = true;
                for (auto& cp : ctx->tap->closePhasers)
                    if (cp.t == VT::Code && cp.code() && !cp.code()->builtin && cp.code()->body)
                        cp.code()->closure = I.tctx_.cur;
            }
            if (ctx->collect) { ctx->collect->push_back(v); return Value::boolean(true); }
            if (ctx->emitCb.t == VT::Code) {
                ValueList one{v};
                ctx->emitting++;
                try { I.callCallable(ctx->emitCb, one); } catch (...) { ctx->emitting--; throw; }
                ctx->emitting--;
            }
            return Value::boolean(true);
        }
        if (!I.tctx_.supplyStack.empty()) { I.tctx_.supplyStack.back()->push_back(v); return Value::boolean(true); }
        // S-62: inside a react there IS a block, but nothing downstream to emit
        // into — Rakudo warns and carries on rather than dying.
        if (!I.reactStack_.empty()) {
            ValueList wa{Value::str("Useless use of emit in react")};
            I.callBuiltin("warn", wa);
            return Value::boolean(true);
        }
        I.throwTypedV("X::ControlFlow",
                      {{"illegal", Value::str("emit")}, {"enclosing", Value::str("supply or react")}},
                      "emit without supply or react");
    };
    // printf/sprintf take **@args — a list/array argument flattens into the values,
    // so `printf $fmt, $x, f()` where f returns (a, b) fills three directives.
    auto sprintfArgs = [](const ValueList& a) -> ValueList {
        ValueList rest;
        for (size_t i = 1; i < a.size(); i++) {
            // …but a JUNCTION is one value, not a list of its eigenstates: it has
            // no single rendering, and `sprintf("%d", 0^1)` must say so.
            if (a[i].t == VT::Array && a[i].arr() && !isJunction(a[i]))
                for (auto& x : *a[i].arr()) rest.push_back(x);
            else rest.push_back(a[i]);
        }
        return rest;
    };
    B["sprintf"] = [sprintfArgs](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        // a JUNCTION format autothreads (the format is a Cool parameter):
        // `sprintf ($o, ($o,).any).all` is a junction of formatted strings
        if (a[0].t == VT::Array && a[0].arr() &&
            (a[0].enumName == "any" || a[0].enumName == "all" ||
             a[0].enumName == "one" || a[0].enumName == "none")) {
            Value out = Value::array(); out.enumName = a[0].enumName;
            auto self = I.builtins_.find("sprintf");
            for (auto& e : *a[0].arr()) {
                ValueList sub = a; sub[0] = e;
                out.arr()->push_back(self->second(I, sub));
            }
            return out;
        }
        ValueList rest = sprintfArgs(a);
        return Value::str(doSprintf(a[0].t == VT::Object ? I.strOf(a[0]) : a[0].toStr(), rest, I.langRev_));
    };
    // Format object (6.e `q:o/…/` / `q:format/…/`): a callable sprintf template that
    // stringifies to its format string. Built by the parser from a flagged literal.
    B["__format__"] = [](Interpreter&, ValueList& a) -> Value {
        Value f = Value::makeHash(); f.hashKind = "Format";
        (*f.hash())["fmt"] = Value::str(a.empty() ? "" : a[0].toStr());
        return f;
    };
    B["printf"] = [sprintfArgs](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(true);
        // a JUNCTION format autothreads: one printf per eigenstate, nested
        // junctions included (`printf ($o, ($o,).any).all` prints twice)
        if (a[0].t == VT::Array && a[0].arr() &&
            (a[0].enumName == "any" || a[0].enumName == "all" ||
             a[0].enumName == "one" || a[0].enumName == "none")) {
            auto self = I.builtins_.find("printf");
            for (auto& e : *a[0].arr()) {
                ValueList sub = a; sub[0] = e;
                self->second(I, sub);
            }
            return Value::boolean(true);
        }
        // `printf($fmt, $junction)` PRINTS ONCE PER EIGENSTATE, in order — Rakudo has
        // a dedicated printf(Str(Cool), Junction:D) candidate. (sprintf does not:
        // there the junction stays one value.)
        if (a.size() == 2 && a[1].t == VT::Array && a[1].arr() &&
            (a[1].enumName == "any" || a[1].enumName == "all" ||
             a[1].enumName == "one" || a[1].enumName == "none")) {
            for (auto& e : *a[1].arr()) {
                ValueList one{e};
                I.ioEmit(doSprintf(a[0].t == VT::Object ? I.strOf(a[0]) : a[0].toStr(), one, I.langRev_), "$*OUT", false);
            }
            return Value::boolean(true);
        }
        ValueList rest = sprintfArgs(a);
        // ioEmit, not std::cout: it takes the output lock, and it honours a
        // rebound `$*OUT`. Writing the stream directly meant
        // `my $*OUT = open(…); printf(…)` printed to the terminal while `say`
        // on the next line went to the file.
        return I.ioEmit(doSprintf(a[0].t == VT::Object ? I.strOf(a[0]) : a[0].toStr(), rest, I.langRev_), "$*OUT", false);
    };
    // 6.e sub form: snip(PRED(s), *@list) — first arg is the predicate or a (p1,p2)
    // list of predicates; the rest is the list. Delegates to the .snip method.
    // 6.e sub form: trans(PAIR…, TARGET) — the transliteration pairs come first
    // and the thing to transliterate is the LAST positional, so it composes in a
    // feed. A list target maps element-wise, as the method would.
    B["trans"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return a.empty() ? Value::str("") : a.back();
        ValueList pairs(a.begin(), a.end() - 1);
        Value target = a.back();
        if (target.t == VT::Array && target.arr()) {
            Value out = Value::array(); out.isList = true;   // a Seq-ish list, not the Array back
            for (auto& el : *target.arr()) out.arr()->push_back(I.methodCall(el, "trans", pairs));
            return out;
        }
        return I.methodCall(target, "trans", pairs);
    };
    // 6.e sub form: snitch($value) notes it and hands it back; snitch(&tap, $value)
    // runs the tap instead. The value is returned unchanged either way — the
    // point of the routine is to see something without disturbing it.
    B["snitch"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::any();
        if (a.size() >= 2 && a[0].t == VT::Code)
            return I.methodCall(a[1], "snitch", ValueList{a[0]});
        return I.methodCall(a[0], "snitch", ValueList{});
    };
    B["snip"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::array();
        Value list = Value::array(); list.isList = true;
        for (size_t k = 1; k < a.size(); k++) for (auto& v : toList(a[k])) list.arr()->push_back(v);
        return I.methodCall(list, "snip", {a[0]});
    };
    B["map"] = [](Interpreter& I, ValueList& a) -> Value {
        // A LAZY source is mapped through the METHOD, which stays lazy and pulls
        // as its consumer asks. Building the result here instead walked only the
        // prefix already materialised, so `map &cis, (0, -tau/$n ... *)` came back
        // two elements long and the FFT's `Z*` twiddle silently ran short.
        if (a.size() == 2 && a[0].t == VT::Code && a[1].t == VT::Array && a[1].ext())
            return I.methodCall(a[1], "map", ValueList{a[0]});
        // …and so is ONE real Array: the method walks the array's own storage,
        // so `map { s/a/A/ }, @a` changes @a as `@a.map({ s/a/A/ })` does — its
        // topic is each ELEMENT, not a copy (S32-list/map.t)
        if (a.size() == 2 && a[0].t == VT::Code && a[1].t == VT::Array && a[1].arr() &&
            !a[1].isList && !a[1].itemized && !a[1].shape())
            return I.methodCall(a[1], "map", ValueList{a[0]});
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        auto emit = [&](const Value& v) {
            Value r = I.callCallable(a[0], {v});
            if (r.t == VT::Array && r.isList && r.s == "Slip")
                for (auto& x : *r.arr()) out.arr()->push_back(x);
            else out.arr()->push_back(r);
        };
        // a block taking SEVERAL elements at a time (`map -> $a, $b { … }, @list`)
        // is the method's business: it knows how to chunk by arity
        if (a.size() >= 2 && a[0].t == VT::Code && a[0].code() && a[0].code()->params) {
            size_t npos = 0;
            for (auto& p : *a[0].code()->params) if (!p.named && !p.slurpy) npos++;
            if (npos > 1 || !a[0].code()->placeholders.empty()) {
                Value lst = Value::array(); lst.isList = true;
                if (a.size() == 2) *lst.arr() = toList(a[1]);
                else for (size_t i = 1; i < a.size(); i++) {
                    if (a[i].t == VT::Array && a[i].isList && a[i].s == "Slip") for (auto& v : *a[i].arr()) lst.arr()->push_back(v);
                    else lst.arr()->push_back(a[i]);
                }
                return I.methodCall(lst, "map", ValueList{a[0]});
            }
        }
        if (a.size() >= 2 && a[0].t == VT::Code) {
            // the single-arg rule: ONE list argument is iterated; with SEVERAL,
            // each argument is one element (a parenthesized group stays whole —
            // Digest::RIPEMD maps a destructuring block over two tuples), except
            // a Slip, which always flattens in
            // …and an ITEMIZED one (`map {…}, $hash`, `${…}`) is one element
            if (a.size() == 2 && (a[1].t == VT::Hash || a[1].t == VT::Array) && a[1].itemized &&
                a[1].hashKind.empty() && a[1].enumName.empty())
                emit(a[1]);
            else if (a.size() == 2) { for (auto& v : toList(a[1])) emit(v); }
            else for (size_t i = 1; i < a.size(); i++) {
                if (a[i].t == VT::Array && a[i].isList && a[i].s == "Slip")
                    for (auto& v : *a[i].arr()) emit(v);
                else emit(a[i]);
            }
        }
        return out;
    };
    B["grep"] = [](Interpreter& I, ValueList& a) -> Value {
        // …and the same for grep: the method form pulls lazily, this one did not
        if (a.size() == 2 && a[1].t == VT::Array && a[1].ext())
            return I.methodCall(a[1], "grep", ValueList{a[0]});
        // …and over an ENDLESS range (`grep *.is-prime, ^∞` is lazy, grep.t)
        if (a.size() == 2 && a[1].t == VT::Range && !a[1].itemized && a[1].rTo() >= 9000000000000000000LL)
            return I.methodCall(a[1], "grep", ValueList{a[0]});
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        if (a.empty()) return out;
        Value mt = a[0];
        Value list = Value::array(); list.isList = true;
        ValueList margs{mt}, pos;
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i].t == VT::Pair && (a[i].s == "k" || a[i].s == "v" || a[i].s == "kv" || a[i].s == "p"))
                margs.push_back(a[i]); // adverb, pass through
            else pos.push_back(a[i]);
        }
        // `grep`'s list is a +@values slurpy (single-arg rule): ONE Positional arg
        // is iterated; with several args each is one element, so a bare `[]` stays
        // an element instead of flattening away and renumbering :kv/:p indices.
        // EXCEPT a Slip, which always flattens into the surrounding list (that's
        // what `.Slip` is for) — `grep &p, (…).Slip, (…).Slip` merges both.
        bool singleList = (pos.size() == 1 && (pos[0].t == VT::Array || pos[0].t == VT::Range) && !pos[0].itemized);
        for (auto& x : pos) {
            bool isSlip = (x.t == VT::Array && x.arr() && x.s == "Slip");
            if (isSlip || (singleList && (x.t == VT::Array || x.t == VT::Range))) {
                if (x.t == VT::Range) for (auto& e : x.flatten()) list.arr()->push_back(e);
                else for (auto& e : *x.arr()) list.arr()->push_back(e);
            } else list.arr()->push_back(x);
        }
        return I.methodCall(list, "grep", margs); // one implementation
    };
    B["first"] = [](Interpreter& I, ValueList& a) -> Value {
        // delegate to the method (like grep): any matcher works — Code, regex,
        // literal, junction — and the :k/:v/:kv/:p/:end adverbs pass through
        if (a.empty()) return Value::any();
        Value mt = a[0];
        Value list = Value::array(); list.isList = true;
        ValueList margs{mt}, pos;
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i].t == VT::Pair && (a[i].s == "k" || a[i].s == "v" || a[i].s == "kv" ||
                                       a[i].s == "p" || a[i].s == "end"))
                margs.push_back(a[i]); // adverb, pass through
            else pos.push_back(a[i]);
        }
        bool singleList = (pos.size() == 1 && (pos[0].t == VT::Array || pos[0].t == VT::Range) && !pos[0].itemized);
        for (auto& x : pos) {
            bool isSlip = (x.t == VT::Array && x.arr() && x.s == "Slip");
            if (isSlip || (singleList && (x.t == VT::Array || x.t == VT::Range))) {
                if (x.t == VT::Range) for (auto& e : x.flatten()) list.arr()->push_back(e);
                else for (auto& e : *x.arr()) list.arr()->push_back(e);
            } else list.arr()->push_back(x);
        }
        return I.methodCall(list, "first", margs); // one implementation
    };
    B["push"] = [](Interpreter& I, ValueList& a) -> Value {
        arrayOpArgs("push", a, false);
        // a List refuses resizing — the METHOD arm owns the X::Immutable throw —
        // and a TYPED array type-checks what it is given: the method does both
        if (!a.empty() && a[0].t == VT::Array && (a[0].isList || !a[0].ofType().empty())) { Value inv = a[0]; ValueList rest(a.begin() + 1, a.end()); return I.methodCall(inv, "push", rest); }
        // a Slip argument is its elements, as in any argument list:
        // `push @c, (1,2).Slip` adds two (Rakudo), as `@c.push: (1,2).Slip` does
        if (!a.empty() && a[0].t == VT::Array) {
            for (size_t i = 1; i < a.size(); i++) {
                if (a[i].t == VT::Array && a[i].arr() && a[i].s == "Slip")
                    for (auto& e : *a[i].arr()) a[0].arr()->push_back(e);
                else a[0].arr()->push_back(a[i]);
            }
            return a[0];
        }
        return Value::any();
    };
    B["pop"] = [](Interpreter& I, ValueList& a) -> Value {
        arrayOpArgs("pop", a, true);
        if (!a.empty() && a[0].t == VT::Array && a[0].ext() && std::static_pointer_cast<LazySeqState>(a[0].ext())->infinite)
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot pop a lazy list"};
        // a List refuses resizing — the METHOD arm owns the X::Immutable throw
        if (!a.empty() && a[0].t == VT::Array && a[0].isList) { ValueList none; return I.methodCall(a[0], "pop", none); }
        if (!a.empty() && a[0].t == VT::Array && !a[0].arr()->empty()) { Value v = a[0].arr()->back(); a[0].arr()->pop_back(); if (v.t == VT::Array) v.itemized = true; return v; }
        // empty: the METHOD's Failure, not a silent Any (see B["shift"])
        if (!a.empty() && a[0].t == VT::Array) { ValueList none; return I.methodCall(a[0], "pop", none); }
        return Value::any();
    };
    B["shift"] = [](Interpreter& I, ValueList& a) -> Value {
        arrayOpArgs("shift", a, true);
        if (!a.empty() && a[0].t == VT::Array && a[0].ext() && std::static_pointer_cast<LazySeqState>(a[0].ext())->infinite) I.materializeLazy(a[0], 1);
        // a List refuses resizing — the METHOD arm owns the X::Immutable throw
        if (!a.empty() && a[0].t == VT::Array && a[0].isList) { ValueList none; return I.methodCall(a[0], "shift", none); }
        if (!a.empty() && a[0].t == VT::Array && !a[0].arr()->empty()) { Value v = a[0].arr()->front(); a[0].arr()->erase(a[0].arr()->begin()); if (v.t == VT::Array) v.itemized = true; return v; }
        // empty: hand off to the METHOD so the sub answers the same Failure
        // (X::Cannot::Empty) instead of a silent Any
        if (!a.empty() && a[0].t == VT::Array) { ValueList none; return I.methodCall(a[0], "shift", none); }
        return Value::any();
    };
    // `flat(…)` delegates to the METHOD, whose recursive walk is the one that
    // stops at an itemized element wherever it sits — `flat 1, (2, $(5,6))` keeps
    // the `$(5,6)` whole, which a one-level flatten of each argument does not.
    // `flat(…)` opens each ARGUMENT one level (an Array counts, so `flat(@a)` is
    // its elements) and then descends through non-itemized sublists — an
    // itemized one stays whole wherever it sits, so `flat 1, (2, $(5,6))` keeps
    // the `$(5,6)`. Delegating wholesale to the METHOD is not the same thing:
    // that rule never opens an Array below the top level, which is what Cro's
    // router walks.
    B["flat"] = [](Interpreter& I, ValueList& a) -> Value {
        // ONE lazy argument stays lazy — `flat(42 xx *)` is the method's
        // answer, which keeps the laziness walking it eagerly here would not
        if (a.size() == 1 && !a[0].itemized &&
            ((a[0].t == VT::Array && a[0].arr() && a[0].ext()) ||
             (a[0].t == VT::Range && a[0].rTo() >= 9000000000000000000LL))) {
            ValueList none;
            return I.methodCall(a[0], "flat", none);
        }
        Value out = Value::seq();   // flat answers a Seq (Rakudo)
        // Same rule as `.flat`, and it is about the SLOT: a bare list slot
        // spreads its Iterable, an ARRAY's slot never does — array assignment
        // itemises each element. `flat [[1,2],[3]]` stays two elements, and so
        // does `my @a = (1,2),(3,4); flat @a`, List elements or not.
        std::function<void(const Value&, bool)> deeper = [&](const Value& x, bool ofArray) {
            if (x.t == VT::Array && x.arr() && !x.itemized && !ofArray)
                for (auto& e : *x.arr()) deeper(e, !x.isList);
            else if (x.t == VT::Range) for (auto& e : x.flatten()) out.arr()->push_back(e);
            else {
                // an element kept whole was kept BECAUSE it is in a container
                Value keep = x;
                if (ofArray && (keep.t == VT::Array || keep.t == VT::Hash)) keep.itemized = true;
                out.arr()->push_back(std::move(keep));
            }
        };
        for (auto& v : a) {
            if (v.itemized) { out.arr()->push_back(v); continue; }
            // a shaped array contributes its leaves: `flat @a[3;2]` is six values
            if (isMultiDimShaped(v)) { for (auto& e : shapedLeaves(v)) out.arr()->push_back(e); continue; }
            if (v.t == VT::Array && v.arr()) { for (auto& e : *v.arr()) deeper(e, !v.isList); continue; }
            if (v.t == VT::Range) { for (auto& e : v.flatten()) out.arr()->push_back(e); continue; }
            // A HASH flattens to its Pairs — an EMPTY one therefore contributes
            // nothing. Pushing the hash itself made `flat %new, @new` (URI's
            // `*@new, *%bad` query setter, with no named arguments) hand a Hash
            // to code expecting a Pair: "No such method 'value'".
            if (v.t == VT::Hash && v.hash() && v.hashKind.empty()) {
                for (auto& kv : *v.hash()) out.arr()->push_back(Value::pair(kv.first, kv.second));
                continue;
            }
            out.arr()->push_back(v);
        }
        return out;
    };
    // `cache(…)` the SUB always answers an Array — unlike `.cache` the method,
    // which keeps the invocant's own type (List-Array sheet LA-04).
    B["cache"] = [](Interpreter&, ValueList& a) -> Value {
        // …except a Seq, whose cache is a List of what it produced
        if (a.size() == 1 && a[0].t == VT::Array && a[0].s == "Seq" && !a[0].itemized && a[0].arr()) {
            Value l = Value::array(*a[0].arr()); l.isList = true; return l;
        }
        if (a.size() == 1) {
            if (a[0].t == VT::Range) return Value::array(a[0].flatten());
            if (a[0].t == VT::Array && a[0].arr() && !a[0].itemized) return Value::array(*a[0].arr());
            return Value::array(ValueList{a[0]});
        }
        Value out = Value::array();
        for (auto& v : a) out.arr()->push_back(v);
        return out;
    };
    B["slip"] = [](Interpreter&, ValueList& a) -> Value { // slip(4,5) spreads into the enclosing list
        // `slip()` with nothing to slip IS Empty, the singleton (sheet LA-06)
        if (a.empty()) return emptySlipSingleton();
        Value out = Value::array(); out.isList = true; out.s = "Slip";
        // the single-argument rule: ONE iterable argument is its elements;
        // several are each one element (`slip (2,3), 4` is ((2 3), 4))
        if (a.size() == 1) {
            if (a[0].t == VT::Array && a[0].arr() && !a[0].itemized) for (auto& x : *a[0].arr()) out.arr()->push_back(x);
            else if (a[0].t == VT::Range) for (auto& x : a[0].flatten()) out.arr()->push_back(x);
            else out.arr()->push_back(a[0]);
        }
        else for (auto& v : a) out.arr()->push_back(v);
        if (out.arr()->empty()) return emptySlipSingleton();
        return out;
    };
    // NB: no B["Slip"] — a bareword `Slip` must stay a type object (Slip.new);
    // the call form Slip(...) routes through the evalCall coercer block.
    B["roundrobin"] = [](Interpreter&, ValueList& a) -> Value {
        // interleave the input lists: round 0 = one from each, round 1 = next, … skipping exhausted lists
        std::vector<ValueList> lists;
        bool slip = false; // `:slip` flattens the rounds into one list
        for (auto& v : a) {
            if (v.t == VT::Pair && v.namedArg) { if (v.s == "slip") slip = !v.pairVal() || v.pairVal()->truthy(); continue; }
            // an ITEMIZED list is one element, not a list to take turns from
            ValueList l = ((v.t == VT::Array || v.t == VT::Range) && !v.itemized) ? v.flatten() : ValueList{v};
            lists.push_back(l);
        }
        size_t maxLen = 0; for (auto& l : lists) maxLen = std::max(maxLen, l.size());
        Value out = Value::seq();   // roundrobin answers a Seq (Rakudo)
        for (size_t i = 0; i < maxLen; i++) {
            if (slip) { for (auto& l : lists) if (i < l.size()) out.arr()->push_back(l[i]); continue; }
            Value round = Value::array(); round.isList = true;
            for (auto& l : lists) if (i < l.size()) round.arr()->push_back(l[i]);
            out.arr()->push_back(round);
        }
        return out;
    };
    // `lazy LIST` / `eager LIST` — rakupp lists are already index-materialised, so
    // both are identity passthroughs (single arg, or a List of the args).
    // A FINITE list made lazy (`lazy 1..3`, `lazy (1, 2, 3)`) is a Seq lazy by
    // DECLARATION, as in Rakudo: it hands out its elements as they are pulled,
    // has no `.elems` until it is made eager, gists as (...), and an Array
    // assigned from it stays lazy until it has been read to the end. An endless
    // Range and a list that is already lazy keep the flag they always had.
    B["lazy"] = [](Interpreter&, ValueList& a) -> Value {
        auto declared = [](ValueList items) -> Value {
            Value out = Value::array(); out.isList = true; out.s = "Seq"; out.b = true;
            auto st = std::make_shared<LazySeqState>();
            st->gatherSeq = true;
            st->declaredLazy = true;
            auto src = std::make_shared<ValueList>(std::move(items));
            auto at = std::make_shared<size_t>(0);
            st->appendNext = [src, at](ValueList& buf) -> bool {
                if (*at >= src->size()) return false;
                buf.push_back((*src)[(*at)++]);
                return true;
            };
            out.extM() = st;
            return out;
        };
        if (a.size() == 1) {
            Value v = a[0];
            const bool endless = v.t == VT::Range && v.rTo() >= 9000000000000000000LL;
            if (v.t == VT::Range && !endless && !v.itemized) return declared(v.flatten());
            if (v.t == VT::Array && !v.ext() && !v.itemized && v.arr()) return declared(*v.arr());
            if (v.t == VT::Range || v.t == VT::Array) v.b = true; // b marks laziness for .is-lazy
            return v;
        }
        return declared(a);
    };
    B["eager"] = [](Interpreter& I, ValueList& a) -> Value {
        // `eager` reads a Seq (SeqToken)
        if (a.size() == 1 && !a[0].itemized) I.seqUse(a[0], Interpreter::SeqUse::Iterate);
        // `eager $x` is `$x.eager`: it REIFIES, so a Range comes back as its
        // elements and a lazy Seq as a list. Handing the argument straight back
        // left `eager (^10+5)/2` as the Range `2.5..^7.5` where Rakudo gives
        // (2.5, 3.5, 4.5, 5.5, 6.5) — the sub form disagreed with the method
        // form on the same value (S02-types/range.t, RG-30).
        if (a.size() == 1) {
            if (a[0].t == VT::Range || (a[0].t == VT::Array && a[0].ext()))
                return I.methodCall(a[0], "eager", ValueList{});
            // a Seq reifies into a List: `eager (1,2).Seq` is (List)
            if (a[0].t == VT::Array && a[0].s == "Seq" && !a[0].itemized) {
                Value l = a[0]; l.s.clear(); l.isList = true; return l;
            }
            return a[0];
        }
        Value out = Value::array(); out.isList = true; for (auto& v : a) out.arr()->push_back(v); return out;
    };
    // `hyper EXPR` / `race EXPR` — the list-op spellings of `.hyper`/`.race`:
    // the values, all of them, in some order (the loop forms are statement
    // prefixes in the parser). Computed eagerly, which is a legal schedule.
    B["hyper"] = [](Interpreter& I, ValueList& a) -> Value { return I.callBuiltin("eager", a); };
    B["race"]  = [](Interpreter& I, ValueList& a) -> Value { return I.callBuiltin("eager", a); };
    // `pair($key, $value)` — the sub spelling of `$key => $value`, for a key
    // that is computed rather than written (sheet HM-17). A non-Str key keeps
    // its own type, so `pair(1, 2)` renders `1 => 2`.
    B["pair"] = [](Interpreter&, ValueList& a) -> Value {
        Value k = a.size() > 0 ? a[0] : Value::any();
        Value p = Value::pair(k.toStr(), a.size() > 1 ? a[1] : Value::any());
        if (k.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(k);
        return p;
    };
    B["hash"] = [](Interpreter&, ValueList& a) -> Value {
        Value h = Value::makeHash();
        ValueList items; // spread list args so hash(<a 1 b 2>) pairs up (and <1 2 3> dies)
        for (auto& v : a) {
            if (v.t == VT::Array && v.arr()) for (auto& x : *v.arr()) items.push_back(x);
            else if (v.t == VT::Hash && !v.hashKind.size()) { for (auto& kv : *v.hash()) (*h.hash())[kv.first] = kv.second; }
            else items.push_back(v);
        }
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].t == VT::Pair) (*h.hash())[items[i].s] = items[i].pairVal() ? *items[i].pairVal() : Value::any();
            else if (i + 1 < items.size()) { (*h.hash())[items[i].toStr()] = items[i + 1]; i++; }
            else throwHashOddNumber((long long)items.size(), items[i]);
        }
        return h;
    };
    // `item($x)` is `$x.item` — a container becomes ONE non-flattening thing
    B["item"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::any();
        // SEVERAL arguments itemize as ONE list — `item(1, 2)` is `$(1, 2)`,
        // not 1 (Nil-Any sheet NA-50). One argument itemizes that value.
        Value v = a.size() == 1 ? a[0] : Value::array(a);
        if (a.size() > 1) v.isList = true;
        if (v.t == VT::Array || v.t == VT::Hash) v.itemized = true;
        return v;
    };
    B["VAR"] = [](Interpreter&, ValueList& a) -> Value { return a.empty() ? Value::any() : a[0]; }; // container introspection: value is its own container
    B["sink"] = [](Interpreter& I, ValueList& a) -> Value {
        // sink EXPR / sink { … }: evaluate for side effects, and SINK the value
        // rather than merely dropping it. Rakudo's `sink` is `x.sink`, so an
        // unhandled Failure detonates here and an unsuccessful Proc throws
        // X::Proc::Unsuccessful — the same rule a statement in sink context
        // follows. Dropping it silently made `sink $proc` the one way to discard
        // a failed child without hearing about it (roast S29-os/system.t).
        for (auto& v : a) {
            if (v.t == VT::Code) { ValueList none; I.sinkReturnedValue(I.callCallable(v, none)); }
            else I.sinkReturnedValue(v);
        }
        return Value::nil();
    };
    // sub forms that delegate to the same-named method, invocant first
    B["splice"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::nil();
        Value inv = a[0]; ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(inv, "splice", rest);
    };
    B["zip"] = [](Interpreter& I, ValueList& a) -> Value { // n-ary zip, like [Z]
        ValueList items;
        Value with;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.namedArg && v.s == "with" && v.pairVal()) { with = *v.pairVal(); continue; }
            items.push_back(v);
        }
        // The single-argument rule (`+lol`): ONE iterable argument is the list of
        // lists to zip, not itself a list — `zip(((1,2),(3,4)))` is `((1,3),(2,4))`
        // and `zip @fitted.map(*.[^$max])` transposes the rows. Treating that one
        // argument as the only list wrapped every element a level too deep, and
        // Text::MiscUtils' text-columns handed `("",)` to a Str:D parameter.
        if (items.size() == 1 && items[0].t == VT::Array && items[0].arr())
            items = *items[0].arr();
        Value z = I.applyReduce("Z", items);
        if (with.t == VT::Code && z.arr()) { // zip(:with(&f)) folds each tuple with &f
            Value out = Value::seq();
            for (auto& t : *z.arr()) {
                ValueList parts = t.t == VT::Array && t.arr() ? *t.arr() : ValueList{t};
                // an OPERATOR folds with its own associativity, as `[**]` and
                // `[eqv]` would: `zip(…):with(&infix:<**>)` is right-associative
                const std::string& wn = with.code() ? with.code()->name : std::string();
                if (wn.size() > 8 && wn.compare(0, 7, "infix:<") == 0 && wn.back() == '>' && parts.size() > 1) {
                    out.arr()->push_back(I.applyReducePublic(wn.substr(7, wn.size() - 8), parts));
                    continue;
                }
                Value acc = parts.empty() ? Value::any() : parts[0];
                for (size_t k = 1; k < parts.size(); k++) acc = I.callCallable(with, {acc, parts[k]});
                out.arr()->push_back(acc);
            }
            return out;
        }
        return z;
    };
    B["classify"] = [](Interpreter& I, ValueList& a) -> Value {
        // `:into(%h)` classifies into an existing hash, APPENDING to its lists.
        Value* into = nullptr; ValueList pos, named;
        for (auto& x : a) {
            if (x.t == VT::Pair && x.s == "into" && x.pairVal()) into = x.pairVal();
            else if (x.t == VT::Pair && x.namedArg) named.push_back(x); // `:as` and friends
            else pos.push_back(x);
        }
        if (pos.size() < 2) return into ? *into : Value::makeHash();
        Value mapper = pos[0];
        Value list = pos.size() == 2 ? pos[1] : Value::array(ValueList(pos.begin() + 1, pos.end()));
        ValueList ma{mapper};
        for (auto& nv : named) ma.push_back(nv);
        Value res = I.methodCall(list, "classify", ma);
        if (!into) return res;
        if (into->t != VT::Hash || !into->hash()) *into = Value::makeHash();
        // into a QuantHash: each class COUNTS its members (a Setty just holds the key)
        {
            const std::string& qk = into->hashKind.str();
            const bool baggy = qk == "BagHash" || qk == "Bag" || qk == "MixHash" || qk == "Mix";
            const bool setty = qk == "SetHash" || qk == "Set";
            if ((baggy || setty) && res.hash()) {
                for (auto& kv : *res.hash()) {
                    long long n = kv.second.t == VT::Array && kv.second.arr() ? (long long)kv.second.arr()->size() : 1;
                    auto it = into->hash()->find(kv.first);
                    Value nv = setty ? Value::boolean(true)
                             : Value::integer((it != into->hash()->end() ? it->second.toInt() : 0) + n);
                    nv.pairKeyM() = kv.second.elemKey();
                    (*into->hash())[kv.first] = nv;
                }
                return *into;
            }
        }
        if (res.hash()) for (auto& kv : *res.hash()) { // append the grouped elements
            auto it = into->hash()->find(kv.first);
            if (it == into->hash()->end()) (*into->hash())[kv.first] = kv.second;
            else if (it->second.t == VT::Array && kv.second.t == VT::Array)
                for (auto& e : *kv.second.arr()) it->second.arr()->push_back(e);
        }
        return *into;
    };
    // sub forms of the mapper family: routine(&code, list) == list.routine(&code)
    for (const char* mf : {"categorize", "deepmap", "duckmap", "nodemap"}) {
        std::string mname = mf;
        B[mname] = [mname](Interpreter& I, ValueList& a) -> Value {
            // the ADVERBS (`:as`, `:into`) are not list elements — they ride
            // through to the method, which is where they mean something
            ValueList pos, named;
            for (auto& v : a) { if (v.t == VT::Pair && v.namedArg) named.push_back(v); else pos.push_back(v); }
            if (pos.size() < 2) return Value::array();
            Value list = pos.size() == 2 ? pos[1] : Value::array(ValueList(pos.begin() + 1, pos.end()));
            ValueList ma{pos[0]};
            for (auto& nv : named) ma.push_back(nv);
            return I.methodCall(list, mname, ma);
        };
    }
    B["quietly"] = [](Interpreter& I, ValueList& a) -> Value { // suppress warn() output; run block/return arg
        if (!a.empty() && a[0].t == VT::Code) {
            I.quietDepth_++;
            try { Value r = I.callCallable(a[0], {}); I.quietDepth_--; return r; }
            catch (...) { I.quietDepth_--; throw; }
        }
        return a.empty() ? Value::any() : a[0];
    };
    B["make-temp-file"] = [](Interpreter&, ValueList& a) -> Value {
        static long long ctr = 0; ctr++;
        std::string base = "/tmp/rakupp-tmp-" + std::to_string((long long)getpid()) + "-" + std::to_string(ctr);
        std::string content;
        for (auto& x : a) if (x.t == VT::Pair && x.s == "content") content = x.pairVal() ? x.pairVal()->toStr() : "";
        { std::ofstream out(base); out << content; }
        Value p = Value::str(base); p.hashKind = "IO"; return p;
    };
    B["make-temp-dir"] = [](Interpreter&, ValueList&) -> Value {
        static long long ctr = 0; ctr++;
        std::string base = "/tmp/rakupp-tmpdir-" + std::to_string((long long)getpid()) + "-" + std::to_string(ctr);
        ::mkdir(base.c_str(), 0700);
        Value p = Value::str(base); p.hashKind = "IO"; return p;
    };
    // `start` runs the block on a real worker thread, then cooperatively yields
    // the GIL until the worker reaches its first blocking point (or finishes). A
    // pure-compute block therefore runs to completion right away (its effects are
    // visible immediately, as under the old eager model), while a block that
    // sleeps/awaits releases the GIL and keeps running concurrently — which is
    // what lets genuinely-timed programs (sleep-sort) interleave. A thrown
    // exception becomes a Broken promise, rethrown at await.
    B["start"] = [](Interpreter& I, ValueList& a) -> Value {
        Value code; for (auto& x : a) if (x.t == VT::Code) code = x;
        if (code.t != VT::Code) { // `start VALUE` — an already-kept promise of the value
            auto ps = std::make_shared<PromiseState>(); ps->done = true; ps->result = a.empty() ? Value::any() : a[0];
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
            (*p.hash())["status"] = Value::str("Kept"); (*p.hash())["result"] = ps->result;
            return p;
        }
        Value p = I.spawnPromise(code);
        I.yieldToWorker();
        return p;
    };
    // NativeCall helpers: size of a native type; cglobal is a stub (0)
    // nativesizeof(T) — bytes T occupies in native memory. Scalars answer from
    // the SAME width table the marshaller uses (an independent one here used to
    // say `int` was 4 bytes while every call passed it as 8), and a
    // CStruct/CUnion class or instance answers its real laid-out size instead
    // of a flat pointer-width guess.
    B["nativesizeof"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::integer(8);
        ClassInfo* ci = nullptr;
        if (a[0].t == VT::Object && a[0].obj()) ci = a[0].obj()->cls.get();
        else if (a[0].t == VT::Type) {
            // …under the QUALIFIED name too: a role's `[::T]` parameter carries the
            // name as written at the parameterisation site, so `LinearArray[MYSQL_BIND]`
            // asked about a bare "MYSQL_BIND" while the registry key is
            // "DBDish::mysql::Native::MYSQL_BIND". Missing it answered the
            // pointer-sized default 8 for a 112-byte struct, and LinearArray then
            // calloc'd 16 bytes for an array C wrote 224 into.
            auto it = I.classes_.find(a[0].s);
            if (it == I.classes_.end()) it = I.classes_.find(I.resolveClassAlias(a[0].s));
            if (it != I.classes_.end()) ci = it->second.get();
        }
        if (ci && (ci->repr == "CStruct" || ci->repr == "CPPStruct" || ci->repr == "CUnion"))
            return Value::integer(Interpreter::ncStructSize(ci));
        std::string t = a[0].t == VT::Type ? a[0].s : a[0].toStr();
        return Value::integer(Interpreter::ncElemSize(t));
    };
    // parameterized native type name: `CArray[uint8]` is Type{s="CArray",
    // ofType="uint8"} — rebuild the "Name[elem]" string the FFI helpers expect.
    auto ncTypeName = [](const Value& v) -> std::string {
        if (v.t != VT::Type) return v.toStr();
        return (!v.ofType().empty() && v.s.find('[') == std::string::npos) ? v.s + "[" + v.ofType() + "]" : v.s;
    };
    // refresh($obj) — Rakudo re-reads a CStruct's native memory into the Raku
    // object after C wrote through the pointer. Here a CStruct attribute read
    // ALWAYS goes to native memory (there is no cached copy to invalidate), so
    // the call has nothing to do but answer Rakudo's 1.
    B["refresh"] = [](Interpreter&, ValueList&) -> Value { return Value::integer(1); };
    // explicitly-manage($str) — asks Rakudo to hand C a buffer that outlives the
    // call rather than a borrowed one. Our Str marshalling already owns every
    // buffer it passes (ncOwnStrElem keeps it alive for the value's lifetime),
    // so the string is returned unchanged; the point is that the NAME resolves,
    // since it is a DEFAULT export that dists call unconditionally.
    B["explicitly-manage"] = [](Interpreter&, ValueList& a) -> Value {
        return a.empty() ? Value::any() : a[0];
    };
    // check_routine_sanity(&sub) — Rakudo's own signature validator, warning
    // about parameter types NativeCall cannot marshal. The marshaller here
    // reports an unusable type at the call itself, with the offending type
    // named, so this answers True rather than duplicating the check.
    B["check_routine_sanity"] = [](Interpreter&, ValueList&) -> Value { return Value::boolean(true); };
    // guess_library_name($lib) — the file `is native($lib)` resolves to. Takes
    // the same shapes the trait does: a bare name, a full path, a (name, version)
    // list, or a provider sub that answers one.
    B["guess_library_name"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        Value v = a[0];
        if (v.t == VT::Code && v.code()) { ValueList none; v = I.callCallable(v, none); }
        std::string lib;
        if (v.t == VT::Array && v.arr() && !v.arr()->empty()) {
            // (name, version): Rakudo appends the version to the decorated name
            lib = (*v.arr())[0].toStr();
            std::string ver = v.arr()->size() > 1 ? (*v.arr())[1].toStr() : "";
            std::string got = I.ncGuessLibraryName(lib);
            if (!ver.empty() && got.find(ver) == std::string::npos) {
#if defined(__APPLE__)
                return Value::str("lib" + lib + "." + ver + ".dylib");
#else
                return Value::str("lib" + lib + ".so." + ver);
#endif
            }
            return Value::str(got);
        }
        lib = v.t == VT::Type ? v.s.str() : v.toStr();
        // a path (or anything with a directory part) is taken as written, as the trait does
        if (lib.find('/') != std::string::npos) return Value::str(lib);
        return Value::str(I.ncGuessLibraryName(lib));
    };
    B["cglobal"] = [ncTypeName](Interpreter& I, ValueList& a) -> Value {
        // the library may arrive as a PROVIDER sub — cglobal(&LIB, …) is the
        // Math::Libgsl family's spelling — and its ANSWER is the library name,
        // exactly as `is native(&LIB)` treats it
        std::string lib;
        if (a.size() > 0) {
            if (a[0].t == VT::Code && a[0].code()) { ValueList none; lib = I.callCallable(a[0], none).toStr(); }
            // An UNDEFINED library means the running program itself — `cglobal(Str,
            // 'errno', int32)` is the documented spelling, and taking the type's
            // NAME for it sent dlopen looking for a library called "Str".
            else if (a[0].t == VT::Type &&
                     (a[0].s == "Str" || a[0].s == "Any" || a[0].s == "Mu" || a[0].s == "Nil")) lib = "";
            // …and a (NAME, VERSION) list, which is what `is native` already
            // takes and what a dist writes when it needs a specific soname:
            // Font::FreeType's `our $FT-LIB = ('freetype', v6)` resolves to
            // libfreetype.6.dylib. Stringifying the list handed dlopen the
            // literal "freetype 6" — a name no file has ever had — so every
            // dist using the versioned form failed at its first symbol, with
            // thirteen more behind that one. The same guess `is native` uses.
            else if (a[0].t == VT::Array && a[0].arr() && !a[0].arr()->empty()) {
                const std::string nm = (*a[0].arr())[0].toStr();
                const std::string ver = a[0].arr()->size() > 1 ? (*a[0].arr())[1].toStr() : "";
                std::string got = I.ncGuessLibraryName(nm);
                if (!ver.empty() && got.find(ver) == std::string::npos)
#if defined(__APPLE__)
                    got = "lib" + nm + "." + ver + ".dylib";
#else
                    got = "lib" + nm + ".so." + ver;
#endif
                lib = got;
            }
            else lib = a[0].t == VT::Type ? a[0].s.str() : a[0].toStr();
        }
        std::string sym  = a.size() > 1 ? a[1].toStr() : "";
        std::string type = a.size() > 2 ? ncTypeName(a[2]) : "Pointer";
        return I.cglobal(lib, sym, type);
    };
    // nativecast(TargetType, $value): reinterpret a native pointer/handle as another
    // native type (Pointer, CArray[T], or a CStruct/CPointer class).
    B["nativecast"] = [ncTypeName](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::any();
        // `nativecast($signature, $ptr)` — the address becomes a CALLABLE with that
        // signature. This is the spelling a VARIADIC C function needs: its argument
        // list is only known at run time, so there is no `is native` sub to declare
        // and the signature is composed out of type objects instead (issue #84,
        // where `g_error_new` is reached this way). What comes back is the same
        // kind of Callable `is native` makes, with its symbol already resolved —
        // callNative marshals through libffi from `params`/`retType` and never has
        // to dlopen anything. It used to fall through to the plain-Int return at
        // the bottom, so the cast answered an address and calling it died
        // "Cannot invoke non-Callable value of type Int".
        if (a[0].t == VT::Hash && a[0].hashKind == "Signature" && a[0].hash()) {
            auto owned = std::make_shared<std::vector<Param>>();
            auto pit = a[0].hash()->find("params");
            if (pit != a[0].hash()->end() && pit->second.arr())
                for (auto& e : *pit->second.arr()) {
                    if (e.t != VT::Hash || !e.hash()) continue;
                    auto get = [&](const char* k) -> const Value* {
                        auto it = e.hash()->find(k); return it == e.hash()->end() ? nullptr : &it->second;
                    };
                    Param p;
                    if (const Value* v = get("name")) p.name = v->toStr();
                    if (!p.name.empty()) p.sigil = p.name[0];
                    const Value* tobj = get("type-obj");
                    p.type = tobj && tobj->t == VT::Type ? std::string(tobj->s.str())
                           : (get("type") ? get("type")->toStr() : std::string());
                    // an untyped parameter is unconstrained, which is what "" means here
                    if (p.type == "Any" || p.type == "Mu") p.type.clear();
                    if (const Value* v = get("named"))    p.named    = v->truthy();
                    if (const Value* v = get("slurpy"))   p.slurpy   = v->truthy();
                    if (const Value* v = get("optional")) p.optional = v->truthy();
                    if (const Value* v = get("rw"))       p.isRw     = v->truthy();
                    owned->push_back(std::move(p));
                }
            Value f = Value::closure([](ValueList&) { return Value::any(); }); // never reached: isNative wins
            f.code()->name = "nativecast";
            f.code()->isNative = true;
            f.code()->nativeSymCache = (void*)(intptr_t)Interpreter::ncRawAddr(a[1]);
            f.code()->params = owned.get();
            auto rit = a[0].hash()->find("returns");
            if (rit != a[0].hash()->end() && rit->second.t == VT::Type && rit->second.s != "Mu")
                f.code()->retType = std::string(rit->second.s.str());
            I.runtimeParams_.push_back(std::move(owned));
            return f;
        }
        std::string t = ncTypeName(a[0]);
        // A Callable has no address to take. This used to fall through to the
        // generic "nothing else matched" 0, so `nativecast(Pointer, &wndproc)`
        // handed C a NULL function pointer and the failure surfaced somewhere
        // else entirely — a window class that registered with no procedure.
        // Rakudo refuses the same cast; say instead where a callback DOES
        // become a C function pointer.
        if (a[1].t == VT::Code)
            throw RakuError{Value::typeObj("X::AdHoc"),
                "nativecast: a routine has no address to cast. A callback becomes a C function pointer "
                "where one is DECLARED — `sub f(&cb (Pointer, uint32 --> int64)) is native(...)` — so pass "
                "the routine as that argument instead"};
        long long addr = Interpreter::ncRawAddr(a[1]);
        if (t == "Pointer" || t.rfind("Pointer[", 0) == 0) return I.ncMakePointer(t, (void*)(intptr_t)addr);
        if (t == "CArray"  || t.rfind("CArray[", 0)  == 0) return I.ncMakeLiveCArray(t, (void*)(intptr_t)addr);
        if (t == "Str") return Value::str(addr ? std::string((const char*)(intptr_t)addr) : "");
        // a CStruct/CPointer class: box the address as an object of that class
        auto it = I.classes_.find(t);
        // …under its QUALIFIED name too. A role's `[::T]` parameter carries the
        // name as WRITTEN at the parameterisation site, so `LinearArray[MYSQL_BIND]`
        // hands the role a bare "MYSQL_BIND" while the registry key is
        // "DBDish::mysql::Native::MYSQL_BIND" — and `nativecast(T, $p)` inside the
        // role answered a bare Int instead of a struct handle.
        if (it == I.classes_.end()) it = I.classes_.find(I.resolveClassAlias(t));
        if (it != I.classes_.end()) {
            Value o; o.t = VT::Object; o.setObj(makePayload<ObjectData>());
            o.obj()->cls = it->second; o.obj()->attrs["__native_ptr"] = Value::integer(addr);
            return o;
        }
        return Value::integer(addr);
    };
    // Every NativeCall routine answers to its QUALIFIED name as well. Two of them
    // (guess_library_name, check_routine_sanity) are :ALL-only exports, so the
    // qualified spelling is the ONLY one a plain `use NativeCall` program has —
    // and it resolved nowhere.
    for (const char* n : {"nativecast", "nativesizeof", "cglobal", "refresh",
                          "explicitly-manage", "guess_library_name", "check_routine_sanity"})
        B[std::string("NativeCall::") + n] = B[n];
    B["await"] = [](Interpreter& I, ValueList& a) -> Value {
        // a bare `await` has nothing to wait for
        if (a.empty())
            throw RakuError{Value::typeObj("X::AdHoc"), "Must specify at least one Awaitable to await"};
        // resolve a Promise, running any pending Proc::Async work (with the timeout from an anyof timer)
        std::function<Value(Value&)> resolve = [&](Value& p) -> Value {
            // a subclassed Promise (`class Meows is Promise`) is awaited through
            // the Promise it boxes
            if (p.t == VT::Object && p.obj() && p.obj()->hasBoxed &&
                p.obj()->boxed().t == VT::Hash && p.obj()->boxed().hashKind == "Promise") {
                Value inner = p.obj()->boxed();
                return resolve(inner);
            }
            // a NESTED list of awaitables is awaited all the way down
            if (p.t == VT::Array && p.arr() && p.enumName.empty() && p.hashKind.empty()) {
                Value out = Value::array(); out.isList = true;
                for (auto& e : *p.arr()) { Value ee = e; out.arr()->push_back(resolve(ee)); }
                return out;
            }
            // A user class that `does Awaitable` (TAP::Parser is one) supplies the
            // thing to wait on: Rakudo asks it for `get-await-handle`, and such a
            // class almost always delegates that to a Promise attribute. Resolve
            // through that Promise — without this `await $parser` handed back the
            // PARSER (issue #34).
            if (p.t == VT::Object && p.obj() && p.obj()->cls &&
                I.typeOrSubsetMatches(p, "Awaitable")) {
                for (const char* acc : {"promise", "Promise"}) {
                    Value inner;
                    try { inner = I.methodCall(p, acc, {}); } catch (RakuError&) { continue; }
                    if (inner.t == VT::Hash && inner.hashKind == "Promise") return resolve(inner);
                }
                try { return I.methodCall(p, "result", {}); } catch (RakuError&) {}
                return p;
            }
            // `await $channel` is `$channel.receive`: the next value, waiting
            // for one; a closed, drained channel throws X::Channel::ReceiveOnClosed
            if (p.t == VT::Hash && p.hashKind == "Channel") {
                ValueList none; return I.methodCall(p, "receive", none);
            }
            // `await` a Supply drains it and yields its LAST emitted value.
            // S-52: a supply that QUIT has no value to give — the exception that
            // ended it is what `await` raises. (Rakudo hands back Any and sets
            // no `$!` when the quit happens while it is still subscribing; the
            // sheet flags that as a bug and it is not imitated here.)
            if (p.t == VT::Hash && p.hashKind == "Supply" && p.hash()) {
                if (p.hash()->count("quit-reason"))
                    throw RakuError{(*p.hash())["quit-reason"],
                                    p.hash()->count("quit-message") ? (*p.hash())["quit-message"].toStr() : "Supply quit"};
                if (p.hash()->count("values")) {
                    auto& vals = *(*p.hash())["values"].arr();
                    return vals.empty() ? Value::any() : vals.back();
                }
                ValueList na; return I.methodCall(p, "wait", na);
            }
            // a plain VALUE is nothing to await: Rakudo refuses it, in words that
            // changed with 6.d
            if (p.t == VT::Int || p.t == VT::Num || p.t == VT::Rat || p.t == VT::Str ||
                p.t == VT::Bool || p.t == VT::Complex || p.t == VT::Pair)
                throw RakuError{Value::typeObj("X::AdHoc"), I.langRev_ == 0
                    ? "Must specify a Promise, Channel, or Supply to await on (got a " + p.typeName() + ")"
                    : "Can only specify Awaitable objects to await (got a " + p.typeName() + ")"};
            if (p.t != VT::Hash || p.hashKind != "Promise") return p;
            // PromiseState-backed promise (start / spawnPromise): block until it
            // settles, rethrowing the cause if it was broken.
            if (p.ext()) {
                auto ps = std::static_pointer_cast<PromiseState>(p.ext());
                I.awaitPromise(ps);
                if (ps->broken) {
                    RakuError err{ ps->cause, causeMessageOf(I, ps->cause, ps->causeMsg) };
                    auto awaitBt = err.bt;
                    // the error happened in the WORKER; this thread's chain is
                    // merely where it was collected, so it goes under a label
                    if (ps->causeBt && !ps->causeBt->frames.empty()) {
                        err.altBt = err.bt;
                        err.altLabel = "Awaited at:";
                        err.bt = ps->causeBt;
                    }
                    // …and the exception says so: it does X::Await::Died, whose
                    // gist leads with where it was awaited (a copy — every
                    // awaiter of the promise gets its own)
                    // every awaiter gets its OWN copy: attaching frames writes to the
                    // object, and several threads may await one broken promise at once
                    if (err.payload.t == VT::Object && err.payload.obj()) {
                        Value own = err.payload;
                        own.setObj(makePayload<ObjectData>(*err.payload.obj()));
                        err.payload = own;
                    }
                    Value ex = I.exceptionFor(err);
                    if (ex.t == VT::Object && ex.obj()) {
                        Value mixed = I.mixinValue(ex, Value::typeObj("X::Await::Died"), true);
                        if (mixed.t == VT::Object && mixed.obj()) {
                            if (awaitBt && !awaitBt->frames.empty()) {
                                Value h = Value::any(); h.extM() = awaitBt;
                                mixed.obj()->attrs["__awaitbt"] = std::move(h);
                            }
                            err.payload = mixed;
                        }
                    }
                    throw err;
                }
                return ps->result;
            }
            std::string kind = p.hash()->count("kind") ? (*p.hash())["kind"].toStr() : "";
            if (kind == "timer") {
                // `await Promise.in($n)` / `.at($t)`: a real wait for the timer's
                // remainder, then Kept with True — it returned immediately before
                // (issue #41's family: a bare timer await was a no-op).
                double left = timerRemainingSecs(p);
                if (left > 0) I.sleepYield(left);
                (*p.hash())["status"] = Value::str("Kept");
                (*p.hash())["result"] = Value::boolean(true);
                return Value::boolean(true);
            }
            if (kind == "anyof" || kind == "allof") {
                Value* procP = nullptr;
                std::vector<Value*> procs;                      // EVERY process member, not the last
                std::vector<Value*> timers;                     // timer members (Promise.in/.at)
                std::vector<std::shared_ptr<PromiseState>> pss; // start/spawn promises in the combinator
                std::vector<Value*> psvals;
                if (p.hash()->count("promises")) for (auto& q : *(*p.hash())["promises"].arr()) {
                    if (q.t == VT::Hash && q.hashKind == "Promise") {
                        std::string k = q.hash()->count("kind") ? (*q.hash())["kind"].toStr() : "";
                        if (k == "timer") timers.push_back(&q);
                        else if (k == "proc") { procP = &q; procs.push_back(&q); }
                        else if (q.ext()) { pss.push_back(std::static_pointer_cast<PromiseState>(q.ext())); psvals.push_back(&q); }
                    }
                }
                // The nearest/farthest timer member's remaining delay, at this moment.
                auto timerLeft = [&](bool nearest) {
                    double L = nearest ? std::numeric_limits<double>::infinity() : 0;
                    for (auto* t : timers) {
                        double r = timerRemainingSecs(*t); if (r < 0) r = 0;
                        L = nearest ? std::min(L, r) : std::max(L, r);
                    }
                    return L;
                };
                // `Promise.allof($p1.start, $p2.start)` runs (drains, reaps) each
                // process; anyof keeps its single-process behaviour
                if (kind == "allof") for (auto* pp : procs) I.runProcPromise(*pp, timers.empty() ? 0 : timerLeft(true));
                else if (procP) I.runProcPromise(*procP, timers.empty() ? 0 : timerLeft(true));
                // WAIT until the fold says settled — anyof: one member, allof:
                // every one (a timer at its moment, a nested combinator by its
                // own fold) — on a latch the combinator's `.then` keeps, so the
                // wait ends the moment the deciding member settles. anyof used
                // to poll every 10 ms, and a nested combinator member was not
                // waited on at all. (Members were ignored once entirely, so
                // `await Promise.anyof: $todo, $time-up` returned at t=0 with
                // $todo still Planned — zef's fetch timeout wrap.)
                if (!Interpreter::promiseSettled(p)) {
                    auto latch = std::make_shared<PromiseState>();
                    I.thenCombinator(p, [latch]() {
                        { std::lock_guard<std::mutex> lk(latch->m); latch->done = true; }
                        latch->cv.notify_all();
                    });
                    I.awaitPromise(latch);
                }
                // reflect settled members onto their hashes so `.so`/`.status` read true
                for (size_t i = 0; i < pss.size(); i++) {
                    std::lock_guard<std::mutex> lk(pss[i]->m);
                    if (pss[i]->done && psvals[i]->hash()) {
                        (*psvals[i]->hash())["status"] = Value::str(pss[i]->broken ? "Broken" : "Kept");
                        if (!pss[i]->broken) (*psvals[i]->hash())["result"] = pss[i]->result;
                    }
                }
                // timer members whose moment has passed are Kept now
                for (auto* t : timers) if (timerRemainingSecs(*t) <= 0) {
                    (*t->hash())["status"] = Value::str("Kept");
                    (*t->hash())["result"] = Value::boolean(true);
                }
                (*p.hash())["status"] = Value::str("Kept");
                return p;
            }
            if (kind == "proc") {
                I.runProcPromise(p, 0);
                // Rakudo keeps the .start promise with a Proc — hand back the
                // finished proc (.exitcode/.so are the exit status), the same
                // thing the whenever handler passes to its block. Returning the
                // promise wrapper made `(await $p.start).exitcode` a method
                // error here while Rakudo prints the code.
                auto fin = p.hash()->find("proc");
                if (fin == p.hash()->end()) return p;
                // …and it IS a Proc (shared hash, re-kinded): a `multi` with a
                // `Proc $proc` candidate must match it — TAP::Status.new
                // (Proc::Async !~~ Proc) silently blessed an EMPTY status from
                // the still-Async-kinded value, zeroing every harness Wstat.
                Value pv = fin->second; pv.hashKind = "Proc";
                return pv;
            }
            if (kind == "proc-ready") {
                if (p.hash()->count("proc") && (*p.hash())["proc"].hash()) {
                    auto& ph = *(*p.hash())["proc"].hash();
                    Interpreter::ParStripe g(I, &ph);   // its worker writes the pid (runProcPromise)
                    auto it = ph.find("pid"); if (it != ph.end()) return it->second;
                }
                return Value::nil();
            }
            auto it = p.hash()->find("result"); return it != p.hash()->end() ? it->second : p; // plain/old-style
        };
        if (a.size() == 1 && a[0].t == VT::Array) {
            Value out = Value::array(); out.isList = true;
            for (auto& x : *a[0].arr()) out.arr()->push_back(resolve(x));
            return out;
        }
        if (a.size() == 1) return resolve(a[0]);
        // several awaitables: a result that is a Slip flattens into the list
        Value out = Value::array(); out.isList = true;
        for (auto& x : a) {
            Value r = resolve(x);
            if (r.t == VT::Array && r.arr() && r.s == "Slip") for (auto& e : *r.arr()) out.arr()->push_back(e);
            else out.arr()->push_back(r);
        }
        return out;
    };
    // set()/bag()/mix() flatten iterable args, but an itemized `$[...]` stays one
    // element, and a Pair arg is an ELEMENT (pair→count is only for coercions)
    auto settyArgs = [](ValueList& args) {
        ValueList out;
        for (auto& a : args) {
            if ((a.t == VT::Array || a.t == VT::Range) && !a.itemized) {
                ValueList sub = a.flatten();
                out.insert(out.end(), sub.begin(), sub.end());
            }
            else if (a.t == VT::Hash && !a.itemized && a.hash() &&
                     (a.hashKind.empty() || a.hashKind == "Map")) {
                // a plain Hash contributes its pairs (a quanthash stays ONE element)
                for (auto& kv : *a.hash()) {
                    Value p = Value::pair(kv.first, kv.second);
                    p.pairKeyM() = kv.second.elemKey();
                    out.push_back(p);
                }
            }
            else out.push_back(a);
        }
        return out;
    };
    // `set *..*` — an endless list cannot become a quanthash
    auto refuseLazy = [](Interpreter& I, ValueList& a, const char* what) {
        for (auto& x : a) {
            bool lazy = endlessLazy(x) || declLazyLive(x);
            if (!lazy && x.t == VT::Range) { ValueList none; lazy = I.methodCall(x, "is-lazy", none).truthy(); }
            if (lazy)
                I.throwTyped("X::Cannot::Lazy", {{"action", "coerce"}, {"what", what}},
                             std::string("Cannot coerce a lazy list onto a ") + what);
        }
    };
    B["set"] = [settyArgs, refuseLazy](Interpreter& I, ValueList& a) -> Value { refuseLazy(I, a, "Set"); ValueList i = settyArgs(a); return i.empty() ? emptyQuantSingleton("Set") : makeBaggy(i, "Set", true); };
    B["bag"] = [settyArgs, refuseLazy](Interpreter& I, ValueList& a) -> Value { refuseLazy(I, a, "Bag"); ValueList i = settyArgs(a); return i.empty() ? emptyQuantSingleton("Bag") : makeBaggy(i, "Bag", true); };
    B["mix"] = [settyArgs, refuseLazy](Interpreter& I, ValueList& a) -> Value { refuseLazy(I, a, "Mix"); ValueList i = settyArgs(a); return i.empty() ? emptyQuantSingleton("Mix") : makeBaggy(i, "Mix", true); };
    // `list(…)` builds a List of its ARGUMENTS — it does not flatten them, so
    // `list((1,2),(3,4))` has two elements. The one-arg rule still applies: a
    // lone Positional spreads, unless it is ITEMIZED (`$(1,2)` stays one thing).
    B["list"] = [](Interpreter&, ValueList& a) -> Value {
        // `list` of ONE Seq is that Seq, untouched (no caching, no copy)
        if (a.size() == 1 && a[0].t == VT::Array && a[0].s == "Seq" && !a[0].itemized) return a[0];
        Value out = Value::array(); out.isList = true;
        if (a.size() == 1 && (a[0].t == VT::Array || a[0].t == VT::Range) && !a[0].itemized) {
            for (auto& x : toList(a[0])) out.arr()->push_back(x);
            return out;
        }
        for (auto& v : a) out.arr()->push_back(v);
        return out;
    };
    B["unshift"] = [](Interpreter& I, ValueList& a) -> Value {
        arrayOpArgs("unshift", a, false);
        // a List refuses resizing — the METHOD arm owns the X::Immutable throw
        if (!a.empty() && a[0].t == VT::Array && a[0].isList) { Value inv = a[0]; ValueList rest(a.begin() + 1, a.end()); return I.methodCall(inv, "unshift", rest); }
        if (!a.empty() && a[0].t == VT::Array) { for (size_t i = a.size(); i > 1; i--) a[0].arr()->insert(a[0].arr()->begin(), a[i - 1]); return Value::integer((long long)a[0].arr()->size()); }
        return Value::any();
    };
}

} // namespace rakupp
