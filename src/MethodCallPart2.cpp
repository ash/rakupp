#include "CNumeric.h"
#include "AsciiCtype.h"
#include "MethodCallSegment.h"
#include <chrono> // DateTime.now subsecond stamp (portable — MSVC has no sys/time.h)
#if !defined(_WIN32)
#include <unistd.h> // gethostname — $*KERNEL.hostname
#endif

// Segment 2 of the method-dispatch chain, split out of methodCallInner.
//
// It is a SEGMENT, not a category: the chain is ORDER-SENSITIVE — an earlier arm
// shadows a later one — so these arms run after methodCallInner's own arms and before segment 3. Add a new arm
// where its priority belongs, not where it reads nicely.
//
// std::optional lets every arm keep its original `return X;` verbatim, so a
// `return` inside a nested lambda still means what it always did. nullopt =
// "not handled here".
namespace rakupp {

// An attribute's .type carries the CONTAINER shape, as in Rakudo:
// `has License @.licenses` answers Positional[License] (and %-attrs
// Associative[T]) — JSON::Unmarshal's array multi dispatches on exactly
// that, and flattening to the element type sent typed-array attributes
// to the Mu fallback (the Test::META chain's last wall).
static Value attrTypeValue(const ClassAttr& a) {
    if (a.sigil == '@') {
        Value v = Value::typeObj("Positional");
        v.ofTypeM() = a.type;
        return v;
    }
    if (a.sigil == '%') {
        Value v = Value::typeObj("Associative");
        v.ofTypeM() = a.type;
        return v;
    }
    return Value::typeObj(a.type.empty() ? "Mu" : a.type);
}

// The Attribute meta-object for `a`, declared by `ownerName`. ONE builder — the
// `.^attributes` and `.^attribute_table` copies had drifted apart (only one of
// them set `built`). It is cached on the ClassAttr because a user `trait_mod:<is>`
// mixes roles into it at declaration time and `.^attributes` must return the same
// object, not a fresh one that has forgotten the trait ever ran.
Value attributeMetaObject(ClassAttr& a, const std::string& ownerName) {
    if (a.metaObj.t == VT::Hash && a.metaObj.hash()) return a.metaObj;
    Value at = Value::makeHash(); at.hashKind = "Attribute";
    (*at.hash())["name"] = Value::str(std::string(1, a.sigil) + "!" + a.name);
    (*at.hash())["type"] = attrTypeValue(a);
    (*at.hash())["readonly"] = Value::boolean(!a.rw);
    (*at.hash())["has_accessor"] = Value::boolean(a.pub);
    // public attrs are always built; a private one only via `is built`
    // (what JSON::Marshal's is_built probe asks)
    (*at.hash())["built"] = Value::boolean(a.pub || a.built);
    (*at.hash())["package"] = Value::typeObj(ownerName);
    for (auto& ut : a.userTraits) (*at.hash())["trait:" + ut.first] = ut.second;
    a.metaObj = at;
    return at;
}

// `.can` on a built-in-backed method: every class news/blesses/gists, and a
// grammar parses (IETF::RFC_Grammar gates on `$g.can('parse')`). The answer is a
// stub callable that dispatches for real if someone actually invokes it.
//
// Both `.can` arms — the one on a class and the one on an instance — needed this
// and each carried a copy, list of universal names included.
static Value builtinCanStub(const std::string& mn, bool isGrammar) {
    static const std::set<std::string> universal = {
        "new", "bless", "gist", "Str", "raku", "perl", "so", "defined",
        "can", "isa", "does", "WHAT", "WHICH", "WHERE", "clone"};
    if (!universal.count(mn) && !(isGrammar && (mn == "parse" || mn == "subparse")))
        return Value::nil();
    Value stub; stub.t = VT::Code; stub.setCode(std::make_shared<Callable>());
    stub.code()->name = mn; stub.code()->isMethod = true;
    std::string mnc = mn;
    stub.code()->builtin = [mnc](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::any();
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], mnc, std::move(rest));
    };
    return stub;
}

std::optional<Value> Interpreter::methodCallPart2(const Value& inv, const MName& m, ValueList& args,
                                     const std::vector<ExprPtr>* rwArgs) {
    if (inv.t == VT::Hash && inv.hashKind == "Supply") {
        // This arm REWRITES the invocant (drainSupplyBlock) and then reads it for
        // the rest of the block. The parameter is a const reference — the dispatch
        // path stopped copying the invocant on every call — so take the copy here,
        // where it is paid only by Supply methods. The shadow keeps the rest of the
        // block exactly as it was.
        Value invLocal = inv;
        Value& inv = invLocal;
        // .schedule-on($scheduler) hops emissions onto that scheduler in Rakudo;
        // rakupp's taps already run cooperatively, so the identity is the
        // faithful translation (Cro's HTTP/2 frame tests pipe through it)
        if (m == "schedule-on") return inv;
        // Supply.Promise: a Promise kept with the LAST value the Supply emits when it
        // is done (broken if it quits). Drives the supply via tapSupply. Cro coerces
        // `Promise(supply {…})` here (body parsers).
        if (m == "Promise") {
            Value p = Value::makeHash(); p.hashKind = "Promise";
            auto ps = std::make_shared<PromiseState>();
            p.extM() = ps;
            (*p.hash())["status"] = Value::str("Planned");
            auto ph = p.hashS(); // shared: settle lives on the supplier, the Promise may die first
            auto last = std::make_shared<Value>(Value::any());
            auto settle = [ps, ph](bool broke, Value v) {
                std::vector<std::function<void()>> fire;
                { std::lock_guard<std::mutex> lk(ps->m);
                  if (ps->done) return;
                  ps->done = true; if (broke) { ps->broken = true; ps->cause = v; ps->causeMsg = v.toStr(); } else ps->result = v;
                  fire.swap(ps->thens); ps->cv.notify_all(); }
                (*ph)["status"] = Value::str(broke ? "Broken" : "Kept");
                if (!broke) (*ph)["result"] = v;
                for (auto& f : fire) f();
            };
            Value emitCb; emitCb.t = VT::Code; emitCb.setCode(std::make_shared<Callable>());
            emitCb.code()->builtin = [last](Interpreter&, ValueList& a) -> Value { if (!a.empty()) *last = a[0]; return Value::any(); };
            Value doneCb; doneCb.t = VT::Code; doneCb.setCode(std::make_shared<Callable>());
            doneCb.code()->builtin = [settle, last](Interpreter&, ValueList&) -> Value { settle(false, *last); return Value::any(); };
            Value quitCb; quitCb.t = VT::Code; quitCb.setCode(std::make_shared<Callable>());
            quitCb.code()->builtin = [settle](Interpreter&, ValueList& a) -> Value { settle(true, a.empty() ? Value::str("quit") : a[0]); return Value::any(); };
            tapSupply(inv, emitCb, doneCb, quitCb);
            return p;
        }
        // on-demand (block-holding) supply: .tap wires it for real; everything
        // else drains it eagerly (legacy value semantics) and continues below.
        // Inside a react block the legacy eager tap is kept (its whenever/done
        // bookkeeping predates the tap stack).
        // Kind-based live supplies (async socket read, OS signals, listen) are
        // driven by tapSupply, which spawns the I/O worker — `.tap` on them must
        // route there, not fall through to the from-list/eager path (which would
        // silently return an empty Tap and never start the worker).
        if ((m == "tap" || m == "act") && inv.hash()->count("kind")) {
            std::string k = inv.hash()->at("kind").toStr();
            if (k == "async-read" || k == "async-listen" || k == "signal" || k == "interval") {
                Value emit = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
                Value done, quit;
                for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "emit") emit = *a.pairVal();
                    else if (a.s == "done") done = *a.pairVal();
                    else if (a.s == "quit") quit = *a.pairVal();
                }
                return tapSupply(inv, emit, done, quit);
            }
        }
        if (inv.hash()->count("block")) {
            if (m == "live") return Value::boolean(false);
            if (m == "Supply") return inv;
            if ((m == "tap" || m == "act") && (reactStack_.empty() || !tctx_.tapStack.empty())) {
                Value emit = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
                Value done, quit;
                for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "emit") emit = *a.pairVal();
                    else if (a.s == "done") done = *a.pairVal();
                    else if (a.s == "quit") quit = *a.pairVal();
                }
                return tapSupply(inv, emit, done, quit);
            }
            // identity/introspection must NOT run the block: isa-ok $supply, Supply
            // (Cro's composer tests) would otherwise tap the pipeline a second time
            bool introspect = m == "isa" || m == "does" || m == "WHAT" || m == "WHICH"
                || m == "WHERE" || m == "HOW" || m == "^name" || m == "defined"
                || m == "so" || m == "Bool" || m == "gist" || m == "raku" || m == "perl";
            if (m == "Channel") {
                // LIVE conversion: each emit queues as it happens, done closes the
                // channel, quit fails it. Eager-draining here would snapshot-and-
                // close, so a later $supplier.emit into the pipeline would hit
                // "receive on a closed channel" (Cro::Core drives establish().Channel
                // exactly that way in its connection-conditional tests).
                Value c = Value::makeHash(); c.hashKind = "Channel";
                (*c.hash())["queue"] = Value::array();
                (*c.hash())["closed"] = Value::boolean(false);
                auto ps = std::make_shared<PromiseState>();
                c.extM() = ps;
                Value cp = Value::makeHash(); cp.hashKind = "Promise"; cp.extM() = ps;
                (*cp.hash())["status"] = Value::str("Planned");
                (*c.hash())["closedPromise"] = cp;
                auto ch = c.hashS();
                auto settle = [ch, ps](bool failed, Value cause) {
                    std::lock_guard<std::recursive_mutex> lk(Interpreter::atomicStripe(ch.get()));
                    (*ch)["closed"] = Value::boolean(true);
                    if (failed) (*ch)["failCause"] = cause;
                    if ((*ch)["queue"].arr()->empty()) {
                        std::lock_guard<std::mutex> lk(ps->m);
                        if (!ps->done) {
                            if (failed) { ps->broken = true; ps->cause = cause; ps->causeMsg = cause.toStr(); }
                            else ps->result = Value::boolean(true);
                            ps->done = true;
                        }
                        ps->cv.notify_all();
                        (*(*ch)["closedPromise"].hash())["status"] = Value::str(failed ? "Broken" : "Kept");
                    }
                };
                Value emitCb; emitCb.t = VT::Code; emitCb.setCode(std::make_shared<Callable>());
                emitCb.code()->builtin = [ch](Interpreter&, ValueList& a) -> Value {
                    std::lock_guard<std::recursive_mutex> lk(Interpreter::atomicStripe(ch.get()));
                    if (!a.empty()) (*ch)["queue"].arr()->push_back(a[0]);
                    return Value::any();
                };
                Value doneCb; doneCb.t = VT::Code; doneCb.setCode(std::make_shared<Callable>());
                doneCb.code()->builtin = [settle](Interpreter&, ValueList&) -> Value {
                    settle(false, Value::any()); return Value::any();
                };
                Value quitCb; quitCb.t = VT::Code; quitCb.setCode(std::make_shared<Callable>());
                quitCb.code()->builtin = [settle](Interpreter&, ValueList& a) -> Value {
                    settle(true, a.empty() ? Value::str("quit") : a[0]); return Value::any();
                };
                tapSupply(inv, emitCb, doneCb, quitCb);
                return c;
            }
            if (!introspect) inv = drainSupplyBlock(inv);
        }
        bool listy = inv.hash()->count("values");
        auto vals = [&]() -> ValueList { return listy ? *(*inv.hash())["values"].arr() : ValueList{}; };
        auto mkSupply = [&](ValueList v) { Value s = Value::makeHash(); s.hashKind = "Supply"; Value a = Value::array(); *a.arr() = std::move(v); (*s.hash())["values"] = a; return s; };
        if (m == "live") return Value::boolean(inv.hash()->count("supplier") > 0);
        if (m == "Supply") return inv;
        if (m == "on-close") { // callback fires when the tapping supply/react block ends
            // inside a REAL supply activation the callback belongs to that
            // activation's tap: it runs when the tap closes (e.g. via `done`)
            if (!args.empty() && !tctx_.tapStack.empty() && tctx_.tapStack.back()->tap) {
                auto& th = tctx_.tapStack.back()->tap;
                std::lock_guard<std::mutex> lk(th->m);
                if (!th->closed) { th->closePhasers.push_back(args[0]); return inv; }
            }
            // no real tap: the callback belongs to the INNERMOST enclosing
            // context — an eager supply drain (tapStack) or a react block
            if (!args.empty() && !tctx_.tapStack.empty()) { tctx_.tapStack.back()->closers.push_back(args[0]); return inv; }
            if (!args.empty() && !reactStack_.empty()) {
                auto ctx = reactStack_.back();
                std::lock_guard<std::mutex> lk(ctx->m);
                ctx->closers.push_back(args[0]);
            }
            return inv;
        }
        if (m == "list" || m == "List" || m == "Seq" || m == "eager") { Value o = Value::array(); *o.arr() = vals(); o.isList = true; return o; }
        // .comb/.words/.lines all concatenate the stream FIRST and then run the Str
        // method. Applied per MESSAGE instead, `.words` over "Hello Word!".comb
        // yielded one "word" per character.
        // `.lines` on a PROCESS stream is a stream of lines, not a value to render:
        // it marks the Supply, and the split happens when the tap is fed (below).
        if (m == "lines" && !listy && inv.hash()->count("proc")) {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            *s.hash() = *inv.hash();
            (*s.hash())["split"] = Value::str("lines");
            return s;
        }
        if ((m == "comb" || m == "words" || m == "lines") && listy) {
            std::string all; for (auto& v : vals()) all += v.toStr();
            Value res = methodCall(Value::str(all), m, args, rwArgs);
            ValueList segs; if (res.t == VT::Array && res.arr()) segs = *res.arr();
            return mkSupply(std::move(segs));
        }
        if (m == "split" && listy) {
            // Supply.split concatenates the stream and splits by the needle; unlike
            // Str.split, a LIMIT keeps the first N CLEAN pieces (not N-1 + the rest),
            // and :skip-empty drops empties (before the limit is applied).
            std::string all; for (auto& v : vals()) all += v.toStr();
            Value needle; bool haveNeedle = false, skipEmpty = false;
            bool haveLimit = false; double limit = 0;
            for (auto& a : args) {
                if (a.t == VT::Pair) { if (a.s == "skip-empty") skipEmpty = !a.pairVal() || a.pairVal()->truthy(); continue; }
                if (!haveNeedle) { needle = a; haveNeedle = true; continue; }
                if (!haveLimit) { haveLimit = true;
                    if (a.t == VT::Whatever) limit = INFINITY;
                    else if (a.t == VT::Code) limit = callCallable(a, {}).toNum();
                    else limit = a.toNum(); // Inf / "Inf" / 3 / "3" / -1 …
                }
            }
            Value res = methodCall(Value::str(all), "split", ValueList{needle});
            ValueList segs; if (res.t == VT::Array && res.arr()) segs = *res.arr();
            if (skipEmpty) { ValueList keep; for (auto& s : segs) if (!s.toStr().empty()) keep.push_back(s); segs.swap(keep); }
            if (haveLimit) {
                if (limit < 0 || (limit == 0)) segs.clear();
                else if (std::isfinite(limit) && (double)segs.size() > limit) segs.resize((size_t)limit);
            }
            return mkSupply(std::move(segs));
        }
        if (m == "Channel" && inv.hash()->count("supplier")) {
            // A live (supplier-backed) Supply → Channel: register a tap on the
            // supplier that pushes each emitted value into the channel queue, so
            // both `.receive`/`.poll` (the queue) and `.Supply` (re-expose) see
            // the live stream. `.Channel` must forward emits, not snapshot.
            Value c = Value::makeHash(); c.hashKind = "Channel";
            Value q = Value::array(); auto qarr = q.arrS(); // shared: the emit lambda outlives a temporary channel
            (*c.hash())["queue"] = q;
            (*c.hash())["closed"] = Value::boolean(false);
            (*c.hash())["supplier"] = (*inv.hash())["supplier"];
            Value tapRec = Value::makeHash();
            Value emitCb; emitCb.t = VT::Code; emitCb.setCode(std::make_shared<Callable>());
            emitCb.code()->builtin = [qarr](Interpreter&, ValueList& a) -> Value {
                if (!a.empty()) qarr->push_back(a[0]);
                return Value::any();
            };
            (*tapRec.hash())["emit"] = emitCb;
            Value sup = (*inv.hash())["supplier"];
            if (sup.t == VT::Hash && sup.hash()->count("taps")) (*sup.hash())["taps"].arr()->push_back(tapRec);
            return c;
        }
        if (m == "Channel") { // drain a (from-list) Supply into a closed Channel
            Value c = Value::makeHash(); c.hashKind = "Channel";
            Value q = Value::array(); *q.arr() = vals(); (*c.hash())["queue"] = q;
            (*c.hash())["closed"] = Value::boolean(true);
            auto ps = std::make_shared<PromiseState>(); ps->done = true; ps->result = Value::boolean(true); c.extM() = ps;
            Value cp = Value::makeHash(); cp.hashKind = "Promise"; cp.extM() = ps; (*cp.hash())["status"] = Value::str("Kept");
            (*c.hash())["closedPromise"] = cp;
            return c;
        }
        if (m == "elems") return Value::integer((long long)vals().size());
        if (m == "tap" || m == "act") {
            Value emit = args.empty() ? Value::nil() : args[0];
            Value done, quit;
            for (auto& a : args) if (a.t == VT::Pair) { if (a.s == "done" && a.pairVal()) done = *a.pairVal(); else if (a.s == "quit" && a.pairVal()) quit = *a.pairVal(); }
            if (inv.hash()->count("supplier")) {
                // live Supply: register the callbacks with the Supplier; emit/done fan out later
                Value tapRec = Value::makeHash();
                (*tapRec.hash())["emit"] = emit; (*tapRec.hash())["done"] = done; (*tapRec.hash())["quit"] = quit;
                // carry any transform chain, giving each step its own fresh mutable state
                if (inv.hash()->count("chain")) {
                    Value chain = Value::array();
                    for (auto& step : *(*inv.hash())["chain"].arr()) {
                        Value s2 = Value::makeHash(); *s2.hash() = *step.hash();
                        (*s2.hash())["state"] = Value::makeHash();
                        chain.arr()->push_back(s2);
                    }
                    (*tapRec.hash())["chain"] = chain;
                }
                Value sup = (*inv.hash())["supplier"];
                if (sup.t == VT::Hash && sup.hash()->count("taps")) (*sup.hash())["taps"].arr()->push_back(tapRec);
                tapRec.hashKind = "Tap"; return tapRec; // shares the record's hash so .close can mark it closed
            }
            // eager: push every value to the emit callback, then run the done phaser
            // (or, if the supply block died, the quit callback with the reason).
            if (listy) {
                if (emit.t == VT::Code) for (auto& v : vals()) {
                    ValueList one{v};
                    // `next` in a whenever skips this value; `last` stops the stream
                    try { callCallable(emit, one); }
                    catch (NextEx&) {}
                    catch (LastEx&) { break; }
                    // `done` inside the block closes the enclosing react: stop emitting
                    if (!reactStack_.empty() && reactStack_.back()->closed) break;
                }
                if (inv.hash()->count("quit-reason")) {
                    if (quit.t == VT::Code) { ValueList one{(*inv.hash())["quit-reason"]}; callCallable(quit, one); }
                    else // unhandled: the supply's death propagates to the tapper (react dies)
                        throw RakuError{(*inv.hash())["quit-reason"],
                                        inv.hash()->count("quit-message") ? (*inv.hash())["quit-message"].toStr() : "Supply quit"};
                }
                else if (done.t == VT::Code) { ValueList none; callCallable(done, none); }
            } else if (!args.empty() && args[0].t == VT::Code && inv.hash()->count("proc")) {
                // register per-stream: stdout taps under "taps", stderr under "taps-err"
                Value proc = (*inv.hash())["proc"];
                const char* key = (*inv.hash())["stream"].toStr() == "stderr" ? "taps-err" : "taps";
                if (!proc.hash()->count(key)) (*proc.hash())[key] = Value::array();
                Value cb = args[0];
                // `$proc.stdout.lines` — the process's output arrives as ONE chunk, so
                // the line split happens here, at the tap: the block runs once per
                // line, without the trailing newline. (zef's test/build/fetch backends
                // are all written as `whenever $proc.stdout.lines { … }`.)
                if (inv.hash()->count("split") && (*inv.hash())["split"].toStr() == "lines") {
                    Value w; w.t = VT::Code; w.setCode(std::make_shared<Callable>());
                    w.code()->builtin = [cb](Interpreter& I, ValueList& a) -> Value {
                        std::string data = a.empty() ? "" : a[0].toStr();
                        for (size_t start = 0; start < data.size();) {
                            size_t nl = data.find('\n', start);
                            std::string line = nl == std::string::npos ? data.substr(start)
                                                                       : data.substr(start, nl - start);
                            if (!line.empty() && line.back() == '\r') line.pop_back();
                            try { I.callCallable(cb, ValueList{Value::str(line)}); }
                            catch (NextEx&) {}
                            catch (LastEx&) { break; }
                            if (nl == std::string::npos) break;
                            start = nl + 1;
                        }
                        return Value::any();
                    };
                    cb = w;
                }
                (*proc.hash())[key].arr()->push_back(cb);
            }
            Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
        }
        if (listy && (m == "min" || m == "max")) {
            // Supply.min/max is a *running* extreme: emit each value that is a new
            // minimum/maximum of the stream so far (compared by an optional &mapper).
            bool wantMax = (m == "max");
            Value mapper = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
            ValueList out; bool have = false; Value bestKey;
            for (auto& v : vals()) {
                Value key = v;
                if (mapper.t == VT::Code) { ValueList one{v}; key = callCallable(mapper, one); }
                if (!have || (wantMax ? valueCmp(key, bestKey) > 0 : valueCmp(key, bestKey) < 0)) { out.push_back(v); bestKey = key; have = true; }
            }
            return mkSupply(out);
        }
        if (listy && m == "do") { // run a block per value for its side effect; pass values through
            if (!args.empty() && args[0].t == VT::Code) for (auto& v : vals()) { ValueList one{v}; callCallable(args[0], one); }
            return mkSupply(vals());
        }
        if (listy && m == "grab") { // hand the whole stream (as $_) to a collector, emit its result
            if (!args.empty() && args[0].t == VT::Code) {
                Value listArg = Value::array(); *listArg.arr() = vals(); listArg.isList = true;
                ValueList one{listArg}; Value r = callCallable(args[0], one);
                return mkSupply(r.t == VT::Array ? *r.arr() : r.flatten());
            }
            return mkSupply(vals());
        }
        if (listy && (m == "produce" || m == "reduce")) { // scan (produce) / fold (reduce) over the stream
            Value op = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
            ValueList out; Value acc; bool first = true;
            for (auto& v : vals()) {
                if (first) { acc = v; first = false; }
                else if (op.t == VT::Code) { ValueList two{acc, v}; acc = callCallable(op, two); }
                if (m == "produce") out.push_back(acc);
            }
            if (m == "reduce") return mkSupply(first ? ValueList{} : ValueList{acc});
            return mkSupply(out);
        }
        if (listy && m == "minmax") { // emit the running (min..max) Range after each value
            Value mapper = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
            ValueList out; Value mn, mx, mnK, mxK; bool first = true;
            for (auto& v : vals()) {
                Value key = v;
                if (mapper.t == VT::Code) { ValueList one{v}; key = callCallable(mapper, one); }
                bool changed = first;
                if (first) { mn = mx = v; mnK = mxK = key; first = false; }
                else { if (valueCmp(key, mnK) < 0) { mn = v; mnK = key; changed = true; } if (valueCmp(key, mxK) > 0) { mx = v; mxK = key; changed = true; } }
                if (!changed) continue; // only emit when the running min..max actually widens
                // build the running Range from the actual endpoint values (Str or Int).
                // rakupp has no string-Range value, so a string range is emitted eagerly
                // flattened — matching how a `"a".."e"` literal evaluates on the other side.
                if (mn.t == VT::Str || mx.t == VT::Str) {
                    Value rg = Value::array();
                    std::string cur = mn.toStr(), end = mx.toStr();
                    for (int g = 0; g < 100000; g++) {
                        if (cur.length() > end.length() || (cur.length() == end.length() && cur > end)) break;
                        rg.arr()->push_back(Value::str(cur));
                        if (cur == end) break;
                        cur = strSucc(cur);
                    }
                    out.push_back(rg);
                } else out.push_back(Value::range(mn.toInt(), mx.toInt(), false, false));
            }
            return mkSupply(out);
        }
        if (listy && (m == "zip" || m == "merge")) {
            // $s.zip($other, …) — the invocant is the first stream; reuse the class-method logic.
            ValueList a2; a2.push_back(inv); for (auto& a : args) a2.push_back(a);
            return methodCall(Value::typeObj("Supply"), m, a2, rwArgs);
        }
        // Live-Supply combinators: build a lazy transform chain that runs per emitted
        // value when the resulting Supply is tapped (see applyTapChain + emit fan-out).
        if (!listy && inv.hash()->count("supplier") &&
            (m == "map" || m == "grep" || m == "head" || m == "skip" ||
             m == "first" || m == "unique" || m == "squish")) {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash())["supplier"] = (*inv.hash())["supplier"];
            Value chain = Value::array();
            if (inv.hash()->count("chain")) *chain.arr() = *(*inv.hash())["chain"].arrS();
            Value step = Value::makeHash();
            (*step.hash())["op"] = Value::str(m);
            for (auto& a : args) if (a.t != VT::Pair) { (*step.hash())["arg"] = a; break; }
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal() && (a.s == "as" || a.s == "with")) (*step.hash())[a.s] = *a.pairVal();
            (*step.hash())["state"] = Value::makeHash();
            chain.arr()->push_back(step);
            (*s.hash())["chain"] = chain;
            return s;
        }
        // Same for a kind-based live supply (Supply.interval, signal(…), the async
        // socket streams): keep the whole spec — every `kind` check downstream must
        // still recognise it — and record the step. wrapSupplyChain applies it when
        // the supply is finally tapped or `whenever`ed. Without this the combinator
        // fell through to the generic Hash path and called the block ONCE, with the
        // spec hash itself as the topic.
        if (!listy && !inv.hash()->count("supplier") && inv.hash()->count("kind") &&
            (m == "map" || m == "grep" || m == "head" || m == "skip" ||
             m == "first" || m == "unique" || m == "squish")) {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            *s.hash() = *inv.hash();
            Value chain = Value::array();
            if (inv.hash()->count("chain")) *chain.arr() = *(*inv.hash())["chain"].arrS();
            Value step = Value::makeHash();
            (*step.hash())["op"] = Value::str(m);
            for (auto& a : args) if (a.t != VT::Pair) { (*step.hash())["arg"] = a; break; }
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal() && (a.s == "as" || a.s == "with")) (*step.hash())[a.s] = *a.pairVal();
            (*step.hash())["state"] = Value::makeHash();
            chain.arr()->push_back(step);
            (*s.hash())["chain"] = chain;
            return s;
        }
        if (listy && (m == "map" || m == "grep" || m == "head" || m == "tail" || m == "skip" ||
                      m == "first" ||
                      m == "reverse" || m == "sort" || m == "unique" || m == "squish" || m == "rotor" ||
                      m == "rotate" || m == "sum" ||
                      m == "batch" || m == "lines" || m == "words" || m == "flat" ||
                      m == "classify" || m == "categorize" || m == "start" || m == "schedule-on" ||
                      m == "stable" || m == "delayed" || m == "migrate" || m == "on-demand")) {
            // Delegate list-transform semantics to the Array method dispatcher, then re-wrap.
            Value arr = Value::array(); *arr.arr() = vals(); arr.isList = true;
            if (m == "start" || m == "schedule-on" || m == "stable" || m == "delayed" ||
                m == "migrate" || m == "on-demand" || m == "batch") return inv; // scheduling no-ops
            Value r = methodCall(arr, m, args, rwArgs);
            if (r.t == VT::Array) return mkSupply(*r.arr());
            return mkSupply(ValueList{r});
        }
        if (m == "wait") {
            // Rakudo BLOCKS here until the supply completes. Returning True at
            // once made `$supplier.Supply.wait` a no-op, so every use of it as
            // a barrier silently raced — Log::Async's `done` is exactly that
            // (`start { sleep 0.1; $.source.done }; $.source.Supply.wait`), and
            // its remove-tap test passed or failed on interpreter speed alone.
            // A list-backed Supply is already complete; a live one waits on the
            // Supplier that feeds it.
            if (!listy && inv.hash()->count("supplier")) {
                Value sup = (*inv.hash())["supplier"];
                if (sup.t == VT::Hash && sup.hash()) {
                    for (;;) {
                        bool finished;
                        {   // the supplier's own stripe — done/quit are written under it
                            std::lock_guard<std::recursive_mutex> lk(supplierMutex(sup.hash()));
                            finished = (sup.hash()->count("done_state") && (*sup.hash())["done_state"].truthy())
                                    || (sup.hash()->count("quit_state") && (*sup.hash())["quit_state"].truthy());
                        }
                        if (finished) break;
                        sleepYield(0.001);   // releases the GIL, so the emitter can run
                    }
                }
            }
            return Value::boolean(true);
        }
        if (m == "done" || m == "close" || m == "quit") return Value::boolean(true);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Tap") {
        // .close removes the tap from its source: mark it closed so emit skips it;
        // a wired tap (on-demand supply / async socket) also tears down its
        // inner taps, CLOSE phasers, and I/O workers via the TapHandle.
        if (m == "close") {
            if (inv.hash()) (*inv.hash())["closed"] = Value::boolean(true);
            if (inv.ext() && inv.hash() && inv.hash()->count("wired") && (*inv.hash())["wired"].truthy())
                closeTapHandle(std::static_pointer_cast<TapHandle>(inv.ext()));
            return Value::boolean(true);
        }
        if (m == "emit" || m == "done" || m == "quit") return Value::boolean(true);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Attribute") {
        auto& h = *inv.hash();
        if (m == "name") return h.count("name") ? h["name"] : Value::str("");
        if (m == "type" || m == "of" || m == "returns") return h.count("type") ? h["type"] : Value::typeObj("Mu");
        if (m == "package") return h.count("package") ? h["package"] : Value::any();
        // the JSON::Name / JSON::Unmarshal / JSON::Marshal attribute-role
        // surface: the traits were stored on the ClassAttr at declaration time
        // (see userTraits) instead of mixing roles into this meta-object.
        if (m == "json-name" && h.count("trait:json-name")) return h["trait:json-name"];
        // META6's MetaAttribute::Specification accessors, off the stored
        // `is specification(Optionality, Version?)` trait payload
        if ((m == "optionality" || m == "spec-version") && h.count("trait:specification")) {
            Value& sp = h["trait:specification"];
            bool isList = sp.t == VT::Array && sp.arr();
            if (m == "optionality") return isList ? (sp.arr()->empty() ? Value::any() : (*sp.arr())[0]) : sp;
            if (isList && sp.arr()->size() > 1) return (*sp.arr())[1];
            Value v0 = Value::str("0"); v0.hashKind = "Version"; return v0;
        }
        if ((m == "unmarshal" || m == "marshal") && !args.empty()) {
            std::string tk = m == "unmarshal" ? "trait:unmarshalled-by" : "trait:marshalled-by";
            if (h.count(tk)) {
                Value& by = h[tk];
                // `is (un)marshalled-by(-> $d {…})` calls the code with the
                // value. The METHOD-NAME spellings differ by direction:
                // unmarshal calls it on the attribute's TYPE with the JSON
                // datum ($type."$meth"($json)); marshal calls it ON THE VALUE
                // with no arguments — `$value."$meth"()`, Nil when undefined
                // (JSON::Marshal's CustomMarshallerMethod, 030-trait.t).
                if (by.t == VT::Code) return callCallable(by, ValueList{args[0]});
                if (m == "marshal") {
                    Value d = methodCall(args[0], "defined", ValueList{});
                    if (!d.truthy()) return Value::any();
                    return methodCall(args[0], by.toStr(), ValueList{});
                }
                Value ty = args.size() > 1 ? args[1]
                         : h.count("type") ? h["type"] : Value::typeObj("Mu");
                return methodCall(ty, by.toStr(), ValueList{args[0]});
            }
            return args[0]; // no custom (un)marshaller: identity
        }
        if (m == "readonly") return h.count("readonly") ? h["readonly"] : Value::boolean(true);
        if (m == "rw") return Value::boolean(h.count("readonly") && !h["readonly"].truthy());
        if (m == "has_accessor") return h.count("has_accessor") ? h["has_accessor"] : Value::boolean(false);
        // builders that predate the `built` key fall back to has_accessor —
        // public means built, which is Rakudo's default too
        if (m == "is_built")
            return h.count("built") ? h["built"]
                 : h.count("has_accessor") ? h["has_accessor"] : Value::boolean(false);
        if (m == "gist" || m == "Str") return h.count("name") ? h["name"] : Value::str("");
        if (m == "defined" || m == "Bool") return Value::boolean(true);
        // read/write the attribute's value on an instance through the meta-object —
        // the core MOP operations marshallers (JSON::Marshal etc.) drive.
        if ((m == "get_value" || m == "set_value") && !args.empty()) {
            std::string an = h.count("name") ? h["name"].toStr() : "";
            char sigil = an.empty() ? '$' : an[0];
            while (!an.empty() && (an[0]=='$'||an[0]=='@'||an[0]=='%'||an[0]=='&'||an[0]=='!'||an[0]=='.')) an = an.substr(1);
            Value obj = args[0];
            if (obj.t == VT::Object && obj.obj()) {
                if (m == "set_value" && args.size() > 1) {
                    Value v = args[1];
                    if (sigil == '@' && v.t == VT::Range) { Value a = Value::array(); *a.arr() = v.flatten(); a.isList = true; v = a; }
                    obj.obj()->attrs[an] = v;
                    return v;
                }
                auto it = obj.obj()->attrs.find(an);
                if (it != obj.obj()->attrs.end()) return it->second;
                // uninitialised: the attribute's declared default kind
                return sigil == '@' ? Value::array() : sigil == '%' ? Value::makeHash() : Value::any();
            }
            // a TYPE object has no attribute storage — Rakudo throws, and
            // Data::Dump's `try get_value // … // 'undefined'` chain expects it
            throw RakuError{Value::typeObj("X::Method::NotFound"),
                "Cannot look up attributes in a " + obj.typeName() + " type object"};
        }
        // no `.hash` on an Attribute either (the guts would leak into a dump)
        if (m == "hash")
            throw RakuError{Value::typeObj("X::Method::NotFound"),
                "No such method 'hash' for invocant of type 'Attribute'"};
        if (m == "container_descriptor" || m == "container") return inv; // enough for `.of`/rw queries
        if (m == "package" || m == "declaring_package") return h.count("package") ? h["package"] : Value::typeObj("Mu");
        // An accessor of a role a trait mixed in (`$a does R` put R's attributes
        // into this same map): `$a.where` after META6's `is customary`. Last,
        // so it can never shadow a real Attribute method.
        {
            auto ai = h.find(m);
            if (ai != h.end()) return ai->second;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Failure") {
        Value ex = inv.hash()->count("exception") ? (*inv.hash())["exception"] : Value::typeObj("Exception");
        if (m == "exception") return failureException(inv);
        if (m == "defined" || m == "Bool" || m == "so") {
            (*inv.hash())["handled"] = Value::boolean(true); // testing a Failure marks it handled
            return Value::boolean(false);
        }
        if (m == "not") { (*inv.hash())["handled"] = Value::boolean(true); return Value::boolean(true); }
        if (m == "handled") return inv.hash()->count("handled") ? (*inv.hash())["handled"] : Value::boolean(false);
        if (m == "self" || m == "Failure") return inv;
        // .throw keeps the Failure's own exception TYPE and message — routing an
        // unthrown X::Str::Numeric through X::AdHoc lost both.
        if (m == "throw" || m == "sink") {
            auto mm = inv.hash()->find("message");
            std::string msg = mm != inv.hash()->end() ? mm->second.toStr() : ex.toStr();
            if (ex.t == VT::Object) throw RakuError{ex, msg};
            throw RakuError{ex.t == VT::Type ? ex : Value::typeObj("X::AdHoc"), msg};
        }
        // .message reads the diagnostic without detonating; .Str/.gist USE the
        // value, so an UNHANDLED Failure throws there (Rakudo). The NUMERIC
        // coercions use it just as much — `.Int` on an unhandled Failure used to
        // return the hash's element count, a 2 out of nowhere.
        if (m == "Str" || m == "gist" || m == "Int" || m == "Num" || m == "Rat" ||
            m == "Numeric" || m == "Real" || m == "FatRat" || m == "Complex") {
            auto h = inv.hash()->find("handled");
            if (h == inv.hash()->end() || !h->second.truthy()) {
                auto mm = inv.hash()->find("message");
                throw RakuError{ex, mm != inv.hash()->end() ? mm->second.toStr() : ex.toStr()};
            }
        }
        if (m == "message" || m == "Str" || m == "gist") {
            auto mm = inv.hash()->find("message");
            if (mm != inv.hash()->end()) return mm->second;
            return methodCall(ex, m, args, rwArgs);
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Pod") {
        auto& h = *inv.hash();
        if (m == "name")     return h.count("name") ? h["name"] : Value::str("");
        if (m == "type")     return h.count("type") ? h["type"] : Value::str("");
        if (m == "contents") return h.count("contents") ? h["contents"] : Value::array();
        if (m == "level")    return h.count("level") ? h["level"] : Value::integer(1);
        if (m == "config")   return h.count("config") ? h["config"] : Value::makeHash();
        if (m == "WHAT")     return Value::typeObj(h.count("podclass") ? h["podclass"].s.str() : std::string("Pod::Block"));
        if (m == "defined" || m == "Bool") return Value::boolean(true);
        if (m == "Str" || m == "gist" || m == "raku") {
            // stringify to the concatenated text of the contents (paragraphs/children)
            std::function<std::string(const Value&)> flat = [&](const Value& v) -> std::string {
                if (v.t == VT::Str) return v.s;
                if (v.t == VT::Hash && v.hashKind == "Pod" && v.hash()->count("contents")) {
                    std::string o; for (auto& c : *(*v.hash())["contents"].arr()) o += flat(c); return o;
                }
                if (v.t == VT::Array && v.arr()) { std::string o; for (auto& c : *v.arr()) o += flat(c); return o; }
                return v.toStr();
            };
            return Value::str(flat(inv));
        }
    }
    if ((inv.t == VT::Type && inv.s == "Kernel") ||
        (inv.t == VT::Hash && inv.hashKind == "Kernel")) {
        if (m == "endian") { // all supported targets are little-endian
            Value e = Value::enumVal("LittleEndian", 1); e.enumType = "Endian"; return e;
        }
        // $*KERNEL.bits — the process's native pointer width, an Int. (It used to
        // fall through to the name, so `$*KERNEL.bits == 64` compared "darwin".)
        if (m == "bits") return Value::integer((long long)(sizeof(void*) * 8));
        if (m == "hardware" || m == "arch") {
#if !defined(_WIN32)
            struct utsname u;
            if (uname(&u) == 0) return Value::str(u.machine);
#endif
            return Value::str("");
        }
        // Kernel.hostname — the host's name, and callable on the TYPE OBJECT:
        // Sys::Hostname's whole body is `sub hostname() { Kernel.hostname }`.
        // Without an arm of its own it fell to the lenient accessor below and
        // answered the kernel's name ("darwin") for every machine.
        if (m == "hostname") {
#if !defined(_WIN32)
            char buf[256];
            if (gethostname(buf, sizeof(buf)) == 0) { buf[sizeof(buf) - 1] = 0; return Value::str(buf); }
#endif
            return Value::str("localhost");
        }
    }
    if (inv.t == VT::Hash && (inv.hashKind == "Distro" || inv.hashKind == "Kernel" || inv.hashKind == "VM")) {
        std::string name = inv.hash()->count("name") ? (*inv.hash())["name"].toStr() : "";
        // `$*VM.request-garbage-collection` — the one hook Raku offers to ask
        // for finalization. Runs the pending-DESTROY sweep (see Interpreter.h).
        if (m == "request-garbage-collection") { runPendingDestroys(); return Value::boolean(true); }
        if (m == "name" || m == "Str" || m == "gist" || m == "auth" || m == "desc") return Value::str(name);
        if (m == "is-win") return Value::boolean(false);
        if (m == "version") return Value::str("0");
        if (m == "signature") return Value::str("");
        if (m == "path-sep") return Value::str(":");
        if (m == "release") { // kernel release string (uname -r)
#if !defined(_WIN32)
            struct utsname u;
            if (uname(&u) == 0) return Value::str(u.release);
#endif
            return Value::str("0");
        }
        if (m == "cpu-cores") { unsigned n = std::thread::hardware_concurrency(); return Value::integer(n ? (long long)n : 1); }
        if (m == "archname" || m == "cpu-arch") return Value::str("x86_64");
        // $*VM.platform-library-name("…/lib/ssl".IO) → "…/lib/libssl.dylib":
        // prepend `lib` to the basename and append the platform extension. Used
        // by OpenSSL::NativeLib et al. to build the `is native` library name.
        if (m == "platform-library-name" && !args.empty()) {
            std::string p = args[0].toStr();
            size_t slash = p.find_last_of('/');
            std::string dir = slash == std::string::npos ? "" : p.substr(0, slash + 1);
            std::string base = slash == std::string::npos ? p : p.substr(slash + 1);
#if defined(_WIN32)
            const char* ext = ".dll"; const char* pre = "";
#elif defined(__APPLE__)
            const char* ext = ".dylib"; const char* pre = "lib";
#else
            const char* ext = ".so"; const char* pre = "lib";
#endif
            // don't double-prefix if it already starts with lib
            std::string libbase = base.compare(0, 3, "lib") == 0 ? base : pre + base;
            Value r = Value::str(dir + libbase + ext); r.hashKind = "IO"; return r;
        }
        return Value::str(name); // lenient: any other Distro/Kernel/VM accessor
    }
    if (inv.t == VT::Hash && inv.hashKind == "Proc") { // standard Proc from run()
        if (m == "exitcode") return (*inv.hash())["exitcode"];
        if (m == "timedout") { // rakupp extension, set when :timeout(N) fired
            auto it = inv.hash()->find("timedout");
            return it != inv.hash()->end() ? it->second : Value::boolean(false);
        }
        if (m == "signal") return Value::integer(0);
        if (m == "so" || m == "Bool") return Value::boolean((*inv.hash())["exitcode"].toInt() == 0);
        if (m == "command") { auto it = inv.hash()->find("argv"); return it != inv.hash()->end() ? it->second : Value::array(); }
        if (m == "in") { Value h = inv; h.hashKind = "ProcIn"; return h; } // writable stdin handle (shares hash)
        if (m == "out" || m == "err") { Value h = Value::makeHash(); h.hashKind = "FileHandle"; (*h.hash())["buffer"] = (*inv.hash())[m == "out" ? "out-str" : "err-str"]; (*h.hash())["mode"] = Value::str("r"); (*h.hash())["captured"] = Value::boolean(true); return h; }
        if (m == "sink" || m == "self") return inv;
        if (m == "pid") return Value::integer(0);
    }
    if (inv.t == VT::Hash && inv.hashKind == "ProcIn") { // $proc.in — feed stdin, which runs a deferred proc
        if (m == "print" || m == "spurt" || m == "write" || m == "say") {
            std::string input = args.empty() ? "" : args[0].toStr();
            if (m == "say") input += "\n";
            std::vector<std::string> argv;
            auto it = inv.hash()->find("argv");
            if (it != inv.hash()->end() && it->second.arr()) for (auto& x : *it->second.arr()) argv.push_back(x.toStr());
            // run(..., :in, :env(...), :cwd(...)) stashed these on the Proc
            std::vector<std::string> envKV; bool haveEnv = false;
            auto ei = inv.hash()->find("env-kv");
            if (ei != inv.hash()->end() && ei->second.arr()) {
                haveEnv = true;
                for (auto& x : *ei->second.arr()) envKV.push_back(x.toStr());
            }
            std::string cwd;
            auto ci = inv.hash()->find("cwd");
            if (ci != inv.hash()->end()) cwd = ci->second.toStr();
            std::string out; int code;
            spawnWithInput(argv, input, out, code, this, haveEnv ? &envKV : nullptr, cwd);
            (*inv.hash())["out-str"] = Value::str(out);      // shared hash: $proc.out.slurp sees this
            (*inv.hash())["exitcode"] = Value::integer(code);
            return Value::boolean(true);
        }
        if (m == "close") return Value::boolean(true);
    }
    if (inv.t == VT::Hash && (inv.hashKind == "Promise" || inv.hashKind == "Vow")) {
        auto ps = inv.ext() ? std::static_pointer_cast<PromiseState>(inv.ext()) : nullptr;
        std::string kind = inv.hash()->count("kind") ? (*inv.hash())["kind"].toStr() : "";

        // keep / break — settle a manual promise (or the vow that controls it).
        // Settling takes the promise's vow; once vowed (explicitly via .vow or
        // implicitly by a prior keep/break), only the Vow object may settle it.
        auto takeVow = [&]() {
            if (inv.hashKind == "Vow") return;
            bool vowed = inv.hash()->count("vowed");
            std::string st = inv.hash()->count("status") ? (*inv.hash())["status"].toStr() : "";
            if (vowed || st == "Kept" || st == "Broken")
                throw RakuError{Value::typeObj("X::Promise::Vowed"),
                                "Access denied to keep/break this Promise; already vowed"};
            (*inv.hash())["vowed"] = Value::boolean(true);
        };
        if (m == "keep") {
            takeVow();
            Value v = args.empty() ? Value::boolean(true) : args[0];
            std::vector<std::function<void()>> fire;
            if (ps) { std::lock_guard<std::mutex> lk(ps->m); if (!ps->done) { ps->result = v; ps->done = true; } fire.swap(ps->thens); ps->cv.notify_all(); }
            (*inv.hash())["status"] = Value::str("Kept"); (*inv.hash())["result"] = v;
            for (auto& f : fire) f(); // run `.then` continuations now that it's settled
            return inv;
        }
        if (m == "break") {
            takeVow();
            Value c = args.empty() ? Value::str("Died") : args[0];
            // A non-exception cause (e.g. break("msg")) is wrapped in X::AdHoc so
            // that `$p.cause.message` works, mirroring `die "msg"`.
            if (c.t != VT::Object) {
                auto xit = classes_.find("X::AdHoc");
                if (xit != classes_.end()) {
                    Value ex; ex.t = VT::Object; ex.setObj(std::make_shared<ObjectData>());
                    ex.obj()->cls = xit->second; ex.obj()->attrs["message"] = Value::str(c.toStr());
                    c = ex;
                }
            }
            std::vector<std::function<void()>> fire;
            if (ps) { std::lock_guard<std::mutex> lk(ps->m); if (!ps->done) { ps->broken = true; ps->cause = c; ps->causeMsg = c.toStr(); ps->done = true; } fire.swap(ps->thens); ps->cv.notify_all(); }
            (*inv.hash())["status"] = Value::str("Broken"); (*inv.hash())["cause"] = c;
            for (auto& f : fire) f();
            return inv;
        }
        if (m == "vow") { takeVow(); Value v = inv; v.hashKind = "Vow"; return v; }

        // Fold the state of an anyof/allof combinator lazily from its children.
        auto childState = [&](Value& c, bool& done, bool& broken) {
            done = broken = false;
            if (c.ext()) { auto s = std::static_pointer_cast<PromiseState>(c.ext()); done = s->done; broken = s->broken; }
            else if (c.hash() && c.hash()->count("status")) { auto s = (*c.hash())["status"].toStr(); broken = (s == "Broken"); done = (s == "Kept" || s == "Broken"); }
        };
        auto comboStatus = [&]() -> std::string {
            if (!inv.hash()->count("promises")) return "Kept";
            auto& kids = *(*inv.hash())["promises"].arr();
            if (kids.empty()) return "Kept";
            if (kind == "anyof") { for (auto& c : kids) { bool d, b; childState(c, d, b); if (d) return "Kept"; } return "Planned"; }
            bool all = true; // allof: Kept once every child has settled (a broken child doesn't fail it)
            for (auto& c : kids) { bool d, b; childState(c, d, b); if (!d) { all = false; break; } }
            return all ? "Kept" : "Planned";
        };

        std::string st;
        if (kind == "anyof" || kind == "allof") st = comboStatus();
        else if (ps) st = ps->done ? (ps->broken ? "Broken" : "Kept") : "Planned";
        else st = inv.hash()->count("status") ? (*inv.hash())["status"].toStr() : "Kept";

        // Return the PromiseStatus enum value (matches the Planned/Broken/Kept
        // barewords), so both `is $p.status, Kept` and `~$p.status eq 'Kept'` hold.
        if (m == "status") return Value::enumVal(st, st == "Planned" ? 0 : st == "Broken" ? 1 : 2);
        if (m == "Bool" || m == "so") return Value::boolean(st != "Planned");
        if (m == "cause") { if (ps && ps->broken) return ps->cause; auto it = inv.hash()->find("cause"); return it != inv.hash()->end() ? it->second : Value::nil(); }
        if (m == "result") {
            if (kind == "anyof" || kind == "allof") return Value::boolean(true);
            if (ps) { awaitPromise(ps); if (ps->broken) throw RakuError{ ps->cause, ps->causeMsg.empty() ? std::string("Promise broken") : ps->causeMsg }; return ps->result; }
            auto it = inv.hash()->find("result"); if (it != inv.hash()->end()) return it->second;
            auto pr = inv.hash()->find("proc"); if (pr != inv.hash()->end()) return pr->second; return Value::nil();
        }
        if (m == "then") {
            // Deferred: the block runs only once the promise settles, receiving the
            // (identical) promise; its return keeps the new Promise, a throw breaks it.
            Value cb = args.empty() ? Value::nil() : args[0];
            Value parent = inv; // shares hash/ext with the promise → `$res === $orig` holds
            auto childPs = std::make_shared<PromiseState>();
            Value np = Value::makeHash(); np.hashKind = "Promise"; np.extM() = childPs;
            (*np.hash())["status"] = Value::str("Planned");
            Interpreter* self = this;
            std::function<void()> run = [self, cb, parent, childPs]() mutable {
                Value res; bool broke = false; Value cause; std::string cmsg;
                try { if (cb.t == VT::Code) { ValueList one{ parent }; res = self->callCallable(cb, one); } }
                catch (const RakuError& e) { broke = true; cause = e.payload; cmsg = e.message; }
                catch (...) { broke = true; }
                std::vector<std::function<void()>> chain;
                { std::lock_guard<std::mutex> lk(childPs->m);
                  if (broke) { childPs->broken = true; childPs->cause = cause; childPs->causeMsg = cmsg; }
                  else childPs->result = res;
                  childPs->done = true; chain.swap(childPs->thens); childPs->cv.notify_all(); }
                for (auto& f : chain) f();
            };
            bool now = false;
            if (ps) { std::lock_guard<std::mutex> lk(ps->m); if (ps->done) now = true; else ps->thens.push_back(run); }
            else now = true;
            if (now) run();
            return np;
        }
    }
    if (inv.t == VT::Type && inv.s == "IO::Special") {
        if (m == "Str" || m == "gist" || m == "path") return Value::str("");
    }
    if (inv.t == VT::Type && inv.s == "Stash") {
        if (m == "new") { Value h = Value::makeHash(); h.hashKind = "Stash"; return h; }
    }
    if (inv.t == VT::Type && (inv.s == "Uni" || inv.s == "NFC" || inv.s == "NFD" || inv.s == "NFKC" || inv.s == "NFKD")) {
        if (m == "new") {
            std::vector<uint32_t> in;
            for (auto& a : args) {
                if (a.t == VT::Pair) continue;
                if (a.t == VT::Array || a.t == VT::Range) { // a codepoint LIST flattens: Uni.new(@cps)
                    for (auto& x : a.flatten()) in.push_back((uint32_t)x.toInt());
                } else in.push_back((uint32_t)a.toInt());
            }
            if (inv.s != "Uni") in = uniNormalize(in, inv.s == "NFD" ? 0 : inv.s == "NFC" ? 1 : inv.s == "NFKD" ? 2 : 3);
            Value out = Value::array(); out.s = (inv.s == "Uni" ? std::string("Uni") : inv.s.str()); for (uint32_t c : in) out.arr()->push_back(Value::integer((long long)c));
            return out;
        }
    }
    // a Uni / NFC / NFD / NFKC / NFKD value is an array of codepoints tagged in `s`
    if (inv.t == VT::Array && (inv.s == "Uni" || inv.s == "NFC" || inv.s == "NFD" || inv.s == "NFKC" || inv.s == "NFKD")) {
        if (m == "NFC" || m == "NFD" || m == "NFKC" || m == "NFKD") {
            std::vector<uint32_t> in; if (inv.arr()) for (auto& x : *inv.arr()) in.push_back((uint32_t)x.toInt());
            auto norm = uniNormalize(in, m == "NFD" ? 0 : m == "NFC" ? 1 : m == "NFKD" ? 2 : 3);
            Value out = Value::array(); out.s = m; for (uint32_t c : norm) out.arr()->push_back(Value::integer((long long)c));
            return out;
        }
        if (m == "list" || m == "List" || m == "values" || m == "Seq" || m == "cache") { Value out = Value::array(); out.isList = true; if (inv.arr()) out.setArr(inv.arrS()); return out; }
        if (m == "codes" || m == "elems") return Value::integer(inv.arr() ? (long long)inv.arr()->size() : 0);
        // .gist lives in Value::gist now, so `say $u` and an interpolated $u agree
        // with it. Only .raku is here — it genuinely differs.
        if (m == "raku") {
            char buf[16];
            std::string body;
            if (inv.arr()) for (size_t i = 0; i < inv.arr()->size(); i++) {
                if (i) body += ", ";
                snprintf(buf, sizeof buf, "0x%04x", (unsigned)(*inv.arr())[i].toInt()); // lowercase, as Rakudo
                body += buf;
            }
            // Rakudo reprs the CONSTRUCTOR plus the normalisation: Uni.new(…).NFD
            return Value::str("Uni.new(" + body + ")" + (inv.s == "Uni" ? "" : "." + inv.s));
        }
        if (m == "Str" || m == "Stringy") {
            // Raku Strs are NFG (NFC-normalized under the hood): canonically
            // equivalent codepoint orders must yield the SAME Str, so normalize
            // on the way from Uni to Str (mass-equality.t).
            std::vector<uint32_t> in; if (inv.arr()) for (auto& x : *inv.arr()) in.push_back((uint32_t)x.toInt());
            auto norm = uniNormalize(in, 1 /*NFC*/);
            std::string s; for (uint32_t c : norm) s += cpToUtf8(c);
            return Value::str(s);
        }
    }
    if (inv.t == VT::Type && inv.s == "Complex") {
        if (m == "new") return Value::complex(args.size() > 0 ? args[0].toNum() : 0.0,
                                              args.size() > 1 ? args[1].toNum() : 0.0);
    }
    // Num had no constructor, so `Num.new(⅓)` fell through to the generic
    // type-object `.new` and answered 0 for every argument.
    if (inv.t == VT::Type && inv.s == "Num" && m == "new")
        return Value::number(args.empty() ? 0.0 : args[0].toNum());
    if (inv.t == VT::Type && m == "Range" &&
        (inv.s == "Int" || inv.s == "Rat" || inv.s == "FatRat" || inv.s == "Num" || inv.s == "UInt")) {
        // The endpoints are ±Inf. The range stays int-backed and saturated for
        // arithmetic, but it must also CARRY them: without RangeEnds it rendered
        // as -9223372036854775808..9223372036854775807, and `Rat.Range eqv
        // -Inf..Inf` only passed because the old comparison expanded both sides.
        // Int is exclusive at both ends (no Int is infinite), UInt starts at 0.
        bool uint = inv.s == "UInt";
        bool exFrom = inv.s == "Int", exTo = inv.s == "Int" || uint;
        Value r = Value::range(uint ? 0 : LLONG_MIN, LLONG_MAX, exFrom, exTo);
        attachRangeEnds(r, uint ? Value::integer(0) : Value::number(-INFINITY),
                        Value::number(INFINITY));
        return r;
    }
    // `Rational[Int,Int].new(3,10)` — the parameterized role constructs a Rat
    // (JSON::Fast's roundtrip test builds one directly)
    if (inv.t == VT::Type && (inv.s == "Rat" || inv.s == "FatRat" || inv.s == "Rational") && m == "new") {
        BigInt n = args.size() > 0 ? args[0].toBig() : BigInt(0);
        BigInt d = args.size() > 1 ? args[1].toBig() : BigInt(1);
        Value v = Value::ratZ(std::move(n), std::move(d));
        if (inv.s == "FatRat") v.fatRatM() = true;
        // Rat denominators are capped at uint64 (FatRat is arbitrary): a wider one
        // degrades to Num at construction too — Rat.new(10**400, 9**999).Str is "0"
        // (the value underflows a double), matching the arithmetic spill rule.
        else if (v.ratD() && !v.ratD()->fitsU64()) return Value::number(v.toNum());
        return v;
    }
    if (inv.t == VT::Type && (inv.s == "IO::String" || inv.s == "Text::IO::String")) {
        if (m == "new") {
            std::string data = args.empty() ? "" : args[0].toStr();
            Value h = Value::makeHash(); h.hashKind = "FileHandle";
            (*h.hash())["path"] = Value::str(""); (*h.hash())["mode"] = Value::str("r");
            Value lines = Value::array();
            std::istringstream is(data); std::string line;
            while (std::getline(is, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.arr()->push_back(Value::str(line));
            }
            (*h.hash())["lines"] = lines; (*h.hash())["pos"] = Value::integer(0);
            return h;
        }
    }
    if (inv.t == VT::Hash && (inv.hashKind == "Lock" || inv.hashKind == "Lock::Async")) {
        auto st = inv.ext() ? std::static_pointer_cast<LockState>(inv.ext()) : nullptr;
        if (m == "protect" || m == "protect-or-queue-on-recursion") {
            if (args.empty() || args[0].t != VT::Code) return args.empty() ? Value::any() : args[0];
            if (st) { // real mutual exclusion, released even if the block throws
                // Acquire with the GIL RELEASED when contended: a worker that holds
                // the GIL and blocks here would otherwise deadlock the lock's holder
                // (which needs the GIL to run its protected block and release).
                if (!st->m.try_lock()) { bool parked = gilPark(); st->m.lock(); gilUnpark(parked); }
                std::lock_guard<std::recursive_mutex> lk(st->m, std::adopt_lock);
                return callCallable(args[0], {});
            }
            return callCallable(args[0], {});
        }
        if (m == "lock" || m == "acquire") { if (st) { if (!st->m.try_lock()) { bool parked = gilPark(); st->m.lock(); gilUnpark(parked); } } return Value::boolean(true); }
        if (m == "unlock" || m == "release") { if (st) st->m.unlock(); return Value::boolean(true); }
        if (m == "condition") { Value v = Value::makeHash(); v.hashKind = "Lock"; return v; }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Semaphore") {
        auto st = inv.ext() ? std::static_pointer_cast<SemaphoreState>(inv.ext()) : nullptr;
        if (m == "acquire") {
            if (st) { std::unique_lock<std::mutex> lk(st->m); st->cv.wait(lk, [&]{ return st->count > 0; }); st->count--; }
            return Value::boolean(true);
        }
        if (m == "release") {
            if (st) { std::lock_guard<std::mutex> lk(st->m); st->count++; st->cv.notify_one(); }
            return Value::boolean(true);
        }
        if (m == "try_acquire" || m == "try-acquire") {
            if (!st) return Value::boolean(true);
            std::lock_guard<std::mutex> lk(st->m);
            if (st->count > 0) { st->count--; return Value::boolean(true); }
            return Value::boolean(false);
        }
    }

    // user-defined class: type-object methods (.new and custom constructors)
    // DateTime / Date constructors
    if (inv.t == VT::Type && (inv.s == "DateTime" || inv.s == "Date")) {
        // a `:formatter(&code)` is stored and applied by .Str (Rakudo's stringifier hook)
        Value formatter; bool haveFmt = false;
        // A formatter need not be a bare Code: `does Callable` + `method CALL-ME`
        // is how DateTime::Format ships one, and callCallable already invokes
        // that. Requiring VT::Code here dropped the argument on the floor and the
        // DateTime silently stringified as ISO-8601 instead.
        for (auto& a : args) if (a.t == VT::Pair && a.s == "formatter" && a.pairVal() &&
                                 (a.pairVal()->t == VT::Code || a.pairVal()->t == VT::Object))
            { formatter = *a.pairVal(); haveFmt = true; }
        auto mk = [&](long long y, long long mo, long long d, long long h, long long mi, Value sec, long long posix, long long tz) {
            // reject out-of-range fields (Rakudo dies): month 1..12, day 1..days-in-month,
            // and for DateTime hour 0..23, minute 0..59 (seconds are leap-checked separately).
            {
                if (mo < 1 || mo > 12)
                    throwTyped("X::OutOfRange",
                        {{"what", "Month"}, {"got", std::to_string(mo)}, {"range", "1..12"}},
                        "Month out of range. Is: " + std::to_string(mo) + ", should be in 1..12");
                static const int mlen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                long long dim = mlen[mo - 1];
                if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) dim = 29;
                if (d < 1 || d > dim)
                    throwTyped("X::OutOfRange",
                        {{"what", "Day"}, {"got", std::to_string(d)}, {"range", "1.." + std::to_string(dim)}},
                        "Day out of range. Is: " + std::to_string(d) + ", should be in 1.." + std::to_string(dim));
                if (inv.s == "DateTime") {
                    if (h < 0 || h > 23)
                        throwTyped("X::OutOfRange",
                            {{"what", "Hour"}, {"got", std::to_string(h)}, {"range", "0..23"}},
                            "Hour out of range. Is: " + std::to_string(h) + ", should be in 0..23");
                    if (mi < 0 || mi > 59)
                        throwTyped("X::OutOfRange",
                            {{"what", "Minute"}, {"got", std::to_string(mi)}, {"range", "0..59"}},
                            "Minute out of range. Is: " + std::to_string(mi) + ", should be in 0..59");
                }
            }
            Value v = Value::makeHash(); v.hashKind = inv.s;
            (*v.hash())["year"] = Value::integer(y); (*v.hash())["month"] = Value::integer(mo); (*v.hash())["day"] = Value::integer(d);
            (*v.hash())["hour"] = Value::integer(h); (*v.hash())["minute"] = Value::integer(mi);
            (*v.hash())["second"] = sec; // exact: Int, or Rat/Num for fractional seconds
            (*v.hash())["posix"] = Value::integer(posix);
            if (haveFmt) (*v.hash())["formatter"] = formatter;
            if (inv.s == "DateTime") (*v.hash())["timezone"] = Value::integer(tz);
            return v;
        };
        // leap seconds: second 60 must land at 23:59:60 UTC on a real historical
        // leap-second day; 61+ is always out of range.
        auto checkLeap = [&](long long y, long long mo, long long d, long long h, long long mi, long long s, long long tz) {
            if (s < 0)
                throw RakuError{Value::typeObj("X::OutOfRange"),
                    "Second out of range. Is: " + std::to_string(s) + ", should be in 0..^62"};
            if (s < 60) return;
            if (s >= 61)
                throw RakuError{Value::typeObj("X::OutOfRange"),
                    "Second out of range. Is: " + std::to_string(s) + ", should be in 0..^61"};
            long long ep = civilToDays(y, mo, d) * 86400 + h * 3600 + mi * 60 + 59 - tz;
            long long uDays = ep >= 0 ? ep / 86400 : -((-ep + 86399) / 86400);
            long long uy, umo, ud; daysToCivil(uDays, uy, umo, ud);
            long long ymd = uy * 10000 + umo * 100 + ud;
            static const std::set<long long> leapDays = {
                19720630, 19721231, 19731231, 19741231, 19751231, 19761231, 19771231,
                19781231, 19791231, 19810630, 19820630, 19830630, 19850630, 19871231,
                19891231, 19901231, 19920630, 19930630, 19940630, 19951231, 19970630,
                19981231, 20051231, 20081231, 20120630, 20150630, 20161231};
            if (ep % 86400 != 86399 || !leapDays.count(ymd))
                throwTyped("X::OutOfRange",
                    {{"what", "Second"}, {"got", "60"}, {"range", "0..^60"},
                     {"comment", "or leap second not allowed here"}},
                    "Second out of range. Is: 60, should be in 0..^60"
                    " (or leap second not allowed here)");
        };
        if (m == "now" || m == "today") {
            // DateTime.now carries fractional seconds (Rakudo prints six digits;
            // Log::Async's default-format test matches `'.' \d+` in the stamp).
            // Date.today ignores the second slot entirely. Both fields derive
            // from ONE microsecond reading so the fraction can't straddle a
            // second boundary. std::chrono, not gettimeofday: MSVC has no
            // sys/time.h.
            long long us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            time_t t = (time_t)(us / 1000000); struct tm* lt = localtime(&t);
            Value sec = inv.s == "Date" ? Value::integer(lt->tm_sec)
                      : Value::number((double)lt->tm_sec + (double)(us % 1000000) / 1e6);
            return mk(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, sec, (long long)t, tzOffsetDyn());
        }
        if (m == "new") {
            if (args.empty()) // `DateTime.new()` / `Date.new()` — must provide arguments
                throw RakuError{Value::typeObj("X::Temporal"),
                    "Cannot call " + inv.s + ".new with no arguments"};
            long long y = 0, mo = 1, d = 1, h = 0, mi = 0, tz = 0;
            Value secV = Value::integer(0);
            // `*` in the DAY slot means "the last day of that month" (Date.new(2042, 2, *)
            // is 2042-02-28). It reaches here as a Whatever/WhateverCode, which numifies
            // to 0 and used to trip the 1..days-in-month check. Year and month may be
            // parsed after the day, so record it and resolve once both are known.
            bool lastDay = false;
            auto isWhatever = [](const Value& v) {
                return v.t == VT::Whatever || (v.t == VT::Code && v.code() && v.code()->isWhateverCode);
            };

            std::vector<Value> pos;
            bool isoStr = false;
            bool haveNamedField = false;
            size_t posN = 0;
            for (auto& a : args) if (a.t != VT::Pair) posN++;
            for (auto& a : args) {
                if (a.t == VT::Pair) {
                    long long val = a.pairVal() ? a.pairVal()->toInt() : 0;
                    if (a.s == "year") { y = val; haveNamedField = true; } else if (a.s == "month") { mo = val; haveNamedField = true; } else if (a.s == "day") { d = val; haveNamedField = true; if (a.pairVal() && isWhatever(*a.pairVal())) lastDay = true; }
                    else if (a.s == "hour") { h = val; haveNamedField = true; } else if (a.s == "minute") { mi = val; haveNamedField = true; }
                    else if (a.s == "second") { secV = a.pairVal() ? *a.pairVal() : Value::integer(0); haveNamedField = true; } // exact (frac OK)
                    else if (a.s == "timezone") tz = val;
                    // `DateTime.new(date => Date.new(…), hour => 1)` — the date
                    // argument supplies year/month/day, the rest default to 0
                    else if (a.s == "date" && a.pairVal() && a.pairVal()->t == VT::Hash && a.pairVal()->hash()) {
                        auto& dh = *a.pairVal()->hash();
                        auto get = [&](const char* k, long long dflt) {
                            auto it = dh.find(k); return it == dh.end() ? dflt : it->second.toInt();
                        };
                        y = get("year", y); mo = get("month", mo); d = get("day", d);
                        haveNamedField = true;
                    }
                } else if (a.t == VT::Str && a.s.find('-', 1) == std::string::npos &&
                           !a.s.empty() && ascii::isdigit((unsigned char)a.s[0]) &&
                           posN == 1) {
                    // the SINGLE string positional that is NOT ISO-shaped (no
                    // dashes): "2012/04" etc. — invalid temporal format
                    // (multiple positionals are the y,m,d form, digits legal)
                    throwTyped("X::Temporal::InvalidFormat",
                        {{"invalid-str", a.s},
                         {"format", inv.s == "Date" ? "yyyy-mm-dd" : "an ISO 8601 timestamp"}},
                        "Invalid " + inv.s + " string '" + a.s +
                        "'; use " + (inv.s == "Date" ? "yyyy-mm-dd" : "an ISO 8601 timestamp") + " instead");
                } else if (a.t == VT::Str && a.s.find('-', 1) != std::string::npos) {
                    // ISO 8601: YYYY-MM-DD[THH:MM:SS[.frac]][Z|±HH:MM|±HHMM]
                    isoStr = true;
                    const std::string& is = a.s;
                    double fs = 0;
                    (void)sscanf(is.c_str(), "%lld-%lld-%lld", &y, &mo, &d);
                    size_t tp = is.find_first_of("Tt"); // ISO 8601 allows a lowercase 't'
                    if (tp != std::string::npos) {
                        std::string tstr = is.substr(tp + 1);
                        for (auto& c : tstr) if (c == ',') c = '.'; // comma decimal separator for seconds
                        (void)sscanf(tstr.c_str(), "%lld:%lld", &h, &mi);
                        // seconds via cnum, not sscanf %lf: the host's LC_NUMERIC
                        // must not decide whether ".43" parses
                        if (size_t sc1 = tstr.find(':'); sc1 != std::string::npos) {
                            if (size_t sc2 = tstr.find(':', sc1 + 1); sc2 != std::string::npos)
                                fs = cnum::strtod(tstr.c_str() + sc2 + 1, nullptr);
                        }
                        // fractional seconds are EXACT — `:00.43` is the Rat 43/100,
                        // not a double, so .day-fraction and friends stay rational
                        secV = (fs == (long long)fs) ? Value::integer((long long)fs) : Value::number(fs);
                        {
                            size_t c1 = tstr.find(':');
                            size_t c2 = c1 == std::string::npos ? c1 : tstr.find(':', c1 + 1);
                            if (c2 != std::string::npos) {
                                std::string st;
                                for (size_t k = c2 + 1; k < tstr.size() &&
                                     (ascii::isdigit((unsigned char)tstr[k]) || tstr[k] == '.'); k++) st += tstr[k];
                                size_t dot = st.find('.');
                                if (dot != std::string::npos && dot + 1 < st.size()) {
                                    std::string digits = st.substr(0, dot) + st.substr(dot + 1);
                                    BigInt den(1); for (size_t k = dot + 1; k < st.size(); k++) den = den * BigInt(10LL);
                                    secV = Value::ratZ(BigInt::fromString(digits), den);
                                }
                            }
                        }
                        size_t zp = is.find_first_of("Zz+-", tp + 1);
                        if (zp != std::string::npos) {
                            if (is[zp] == 'Z' || is[zp] == 'z') tz = 0;
                            else {
                                // the offset must be exactly ±HH or ±HH:MM (2-digit fields)
                                std::string off = is.substr(zp + 1);
                                auto dig = [](char c) { return c >= '0' && c <= '9'; };
                                bool ok = (off.size() == 2 && dig(off[0]) && dig(off[1])) ||              // ±HH
                                          (off.size() == 4 && dig(off[0]) && dig(off[1]) && dig(off[2]) && dig(off[3])) || // ±HHMM
                                          (off.size() == 5 && off[2] == ':' && dig(off[0]) && dig(off[1]) && dig(off[3]) && dig(off[4])); // ±HH:MM
                                if (!ok)
                                    throw RakuError{Value::typeObj("X::DateTime::InvalidFormat"),
                                        "Invalid DateTime string '" + is + "'"};
                                long long oh = (off[0] - '0') * 10 + (off[1] - '0');
                                long long om = off.size() == 4 ? (off[2] - '0') * 10 + (off[3] - '0')
                                             : off.size() == 5 ? (off[3] - '0') * 10 + (off[4] - '0') : 0;
                                if (om >= 60)
                                    throw RakuError{Value::typeObj("X::OutOfRange"),
                                        "Minute out of range. Is: " + std::to_string(om) + ", should be in 0..59"};
                                tz = (is[zp] == '-' ? -1 : 1) * (oh * 3600 + om * 60);
                            }
                        }
                    }
                } else pos.push_back(a);
            }
            if (haveNamedField && !pos.empty()) // `DateTime.new(:2016year, 42)` — no mixing
                throw RakuError{Value::typeObj("X::Temporal"),
                    "Cannot mix a named date component with a positional argument to " + inv.s + ".new"};
            // DateTime.new($dt) / Date.new($dt) — a Dateish (or Instant) argument
            // is the INSTANT it names, not a year. It fell through to the
            // positional-fields arm below, where `.toInt()` made the whole
            // DateTime a year and `DateTime.new($x + $min)` answered 0008-01-01.
            // (Reached constantly here: this engine's DateTime + Num is a
            // DateTime, where Rakudo's is an Instant, so a module adding a
            // random offset to a bound hands .new exactly this.)
            if (!isoStr && pos.size() == 1 && pos[0].t == VT::Hash &&
                (pos[0].hashKind == "DateTime" || pos[0].hashKind == "Date" ||
                 pos[0].hashKind == "Instant")) {
                Value posix = methodCall(pos[0], pos[0].hashKind == "Instant" ? "Num" : "posix",
                                         ValueList{Value::pair("real", Value::boolean(true))});
                pos[0] = posix;
            }
            if (!isoStr && inv.s == "DateTime" && pos.size() == 1 && pos[0].isNumeric()) {
                // DateTime.new($posix) — seconds since the epoch (frac OK); a :timezone
                // shifts the displayed civil time (posix itself stays the same instant)
                double pep = pos[0].toNum();
                long long ip = (long long)std::floor(pep);
                double frac = pep - (double)ip;
                long long lt = ip + tz;
                long long days = lt >= 0 ? lt / 86400 : -((-lt + 86399) / 86400);
                long long rem = lt - days * 86400;
                daysToCivil(days, y, mo, d);
                h = rem / 3600; mi = (rem % 3600) / 60;
                long long si = rem % 60;
                secV = frac != 0.0 ? Value::number(si + frac) : Value::integer(si);
                return mk(y, mo, d, h, mi, secV, ip, tz);
            }
            if (!isoStr) {
                if (pos.size() >= 1) y = pos[0].toInt();
                if (pos.size() >= 2) mo = pos[1].toInt();
                if (pos.size() >= 3) { if (isWhatever(pos[2])) lastDay = true; else d = pos[2].toInt(); }
                if (pos.size() >= 4) h = pos[3].toInt();   // DateTime.new(y, m, d, H, M, S)
                if (pos.size() >= 5) mi = pos[4].toInt();
                if (pos.size() >= 6) secV = pos[5];        // exact (frac OK)
            }
            if (lastDay) { // resolve `*` now that the year and month are known
                static const int mlen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                d = (mo >= 1 && mo <= 12) ? mlen[mo - 1] : 31;
                if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) d = 29;
            }
            long long sInt = secV.toInt(); // floor for epoch/leap math
            if (secV.toNum() < 0) // a fractional negative second (-1/2) truncates to 0, so check the value
                throw RakuError{Value::typeObj("X::OutOfRange"),
                    "Second out of range. Is: " + secV.toStr() + ", should be in 0..^62"};
            if (inv.s == "DateTime") checkLeap(y, mo, d, h, mi, sInt, tz);
            long long ep = civilToDays(y, mo, d) * 86400 + h * 3600 + mi * 60 + (sInt >= 60 ? 59 : sInt) - tz;
            return mk(y, mo, d, h, mi, secV, ep, tz);
        }
    }
    if (inv.t == VT::Hash && (inv.hashKind == "DateTime" || inv.hashKind == "Date")) {
        auto fld = [&](const char* k) { auto it = inv.hash()->find(k); return it != inv.hash()->end() ? it->second.toInt() : 0; };
        // a stored `:formatter(&code)` drives .Str and .gist — `say` shows the
        // formatted form too (Dateish gist delegates to Str)
        if ((m == "Str" || m == "gist") && inv.hash()->count("formatter") &&
            ((*inv.hash())["formatter"].t == VT::Code || (*inv.hash())["formatter"].t == VT::Object)) {
            ValueList fa{inv};
            return Value::str(callCallable((*inv.hash())["formatter"], fa).toStr());
        }
        // Dates enumerate day by day: .succ/.pred step a whole day (Range
        // iteration and `for $d1..$d2` rely on this)
        if ((m == "succ" || m == "pred") && inv.hashKind == "Date")
            return makeDate(civilToDays(fld("year"), fld("month"), fld("day")) + (m == "succ" ? 1 : -1));
        // with no formatter of its own a Date/DateTime answers the Callable TYPE
        // object — the attribute's declared type, not a bare Any
        if (m == "formatter") return inv.hash()->count("formatter") ? (*inv.hash())["formatter"]
                                                                  : Value::typeObj("Callable");
        if (m == "second" || m == "whole-second") {
            auto it = inv.hash()->find("second");
            Value sv = it != inv.hash()->end() ? it->second : Value::integer(0);
            return m == "whole-second" ? Value::integer(sv.toInt()) : sv; // .second keeps the fraction
        }
        if (m == "posix") { // `:real` keeps the fractional seconds
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "real" && (!a.pairVal() || a.pairVal()->truthy())) {
                    Value sec = inv.hash()->count("second") ? (*inv.hash())["second"] : Value::integer(0);
                    Value frac = applyArith("-", sec, Value::integer(sec.toInt()));
                    return applyArith("+", Value::integer(fld("posix")), frac);
                }
            return Value::integer(fld("posix"));
        }
        if (m == "year" || m == "month" || m == "day" || m == "hour" || m == "minute")
            return Value::integer(fld(m.c_str()));
        if (m == "hh-mm-ss") { char b[16]; snprintf(b, sizeof b, "%02lld:%02lld:%02lld", fld("hour"), fld("minute"), fld("second")); return Value::str(b); }
        if (m == "day-of-month") return Value::integer(fld("day")); // alias for .day
        if (m == "weekday-of-month") return Value::integer((fld("day") - 1) / 7 + 1);
        if (m == "DateTime") { // Date → DateTime (midnight); DateTime → self
            if (inv.hashKind == "DateTime") return inv;
            ValueList mk{Value::integer(fld("year")), Value::integer(fld("month")),
                         Value::integer(fld("day"))};
            // 6.e added `:timezone`, and its DEFAULT is $*TZ rather than UTC —
            // so a bare .DateTime moves too, not just one given an argument.
            // The 6.c candidate takes nothing, so before 6.e the argument is
            // silently dropped and the answer is always UTC.
            if (sixE()) {
                bool given = false;
                for (auto& a2 : args)
                    if (a2.t == VT::Pair && a2.s == "timezone" && a2.pairVal()) {
                        mk.push_back(Value::pair("timezone", *a2.pairVal()));
                        given = true;
                    }
                if (!given) mk.push_back(Value::pair("timezone", Value::integer(tzOffsetDyn())));
            }
            return methodCall(Value::typeObj("DateTime"), "new", mk);
        }
        if (m == "Instant") { // posix seconds tagged Instant (rakupp `now` is raw posix)
            auto sit = inv.hash()->find("second");
            double sec = sit != inv.hash()->end() ? sit->second.toNum() : 0.0;
            long long ep = civilToDays(fld("year"), fld("month"), fld("day")) * 86400 +
                           fld("hour") * 3600 + fld("minute") * 60 - fld("timezone");
            Value v = Value::number((double)ep + sec); v.hashKind = "Instant"; return identify(v);
        }
        if ((m == "timezone" || m == "offset") && inv.hashKind == "DateTime") return Value::integer(fld("timezone"));
        if ((m == "in-timezone" || m == "utc" || m == "local") && inv.hashKind == "DateTime") {
            long long newTz = m == "utc" ? 0 : m == "local" ? tzOffsetDyn()
                            : (args.empty() ? 0 : args[0].toInt());
            auto sit = inv.hash()->find("second");
            Value secV = sit != inv.hash()->end() ? sit->second : Value::integer(0);
            long long sInt = secV.toInt();
            double frac = secV.toNum() - (double)sInt; // fractional seconds survive the shift
            long long leap = sInt >= 60 ? 1 : 0;
            long long ep = civilToDays(fld("year"), fld("month"), fld("day")) * 86400 +
                           fld("hour") * 3600 + fld("minute") * 60 +
                           (leap ? 59 : sInt) - fld("timezone");
            long long lt = ep + newTz;
            long long days = lt >= 0 ? lt / 86400 : -((-lt + 86399) / 86400);
            long long rem = lt - days * 86400;
            long long y, mo, d; daysToCivil(days, y, mo, d);
            long long outSec = rem % 60 + leap;
            Value v = Value::makeHash(); v.hashKind = "DateTime";
            // a conversion keeps the formatter: `$dt.utc`, `.local`, `.clone`,
            // `.in-timezone`, `.later`, `.earlier` all stay formatted, as in Rakudo
            if (inv.hash()->count("formatter")) (*v.hash())["formatter"] = (*inv.hash())["formatter"];
            (*v.hash())["year"] = Value::integer(y); (*v.hash())["month"] = Value::integer(mo); (*v.hash())["day"] = Value::integer(d);
            (*v.hash())["hour"] = Value::integer(rem / 3600); (*v.hash())["minute"] = Value::integer((rem % 3600) / 60);
            (*v.hash())["second"] = frac != 0.0 ? Value::number(outSec + frac) : Value::integer(outSec);
            (*v.hash())["posix"] = Value::integer(ep); (*v.hash())["timezone"] = Value::integer(newTz);
            return v;
        }
        if (m == "raku" && inv.hashKind == "DateTime") {
            char buf[160];
            snprintf(buf, sizeof buf,
                "DateTime.new(:year(%lld), :month(%lld), :day(%lld), :hour(%lld), :minute(%lld), :second(%lld), :timezone(%lld))",
                fld("year"), fld("month"), fld("day"), fld("hour"), fld("minute"), fld("second"), fld("timezone"));
            return Value::str(buf);
        }
        if (m == "mm-dd-yyyy" || m == "dd-mm-yyyy") { // US / European date strings
            char buf[48];
            if (m == "mm-dd-yyyy") snprintf(buf, sizeof buf, "%02lld-%02lld-%04lld", fld("month"), fld("day"), fld("year"));
            else                   snprintf(buf, sizeof buf, "%02lld-%02lld-%04lld", fld("day"), fld("month"), fld("year"));
            return Value::str(buf);
        }
        if (m == "Str" || m == "gist" || m == "yyyy-mm-dd" || m == "Date") {
            if (m == "Date") return makeDate(civilToDays(fld("year"), fld("month"), fld("day")));
            // one ISO 8601 formatter, shared with the value model
            return Value::str(dateGist(*inv.hash(), inv.hashKind == "Date" || m == "yyyy-mm-dd"));
        }
        if (m == "day-of-week" || m == "dow") { // 1=Monday .. 7=Sunday (Sakamoto's algorithm)
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
            long long yy = (mo < 3) ? y - 1 : y;
            int sak = (int)(((yy + yy / 4 - yy / 100 + yy / 400 + t[(mo - 1) % 12] + d) % 7 + 7) % 7); // 0=Sun
            return Value::integer((sak + 6) % 7 + 1);
        }
        if ((m == "later" || m == "earlier") && inv.hashKind == "Date") {
            long long sign = (m == "later") ? 1 : -1;
            long long days = 0, months = 0, years = 0;
            ValueList units;
            for (auto& a : args) { if (a.t == VT::Array && a.arr()) for (auto& x : *a.arr()) units.push_back(x); else units.push_back(a); }
            for (auto& a : units) if (a.t == VT::Pair && a.pairVal()) {
                long long v = a.pairVal()->toInt();
                if (a.s == "day" || a.s == "days") days += v;
                else if (a.s == "week" || a.s == "weeks") days += 7 * v;
                else if (a.s == "month" || a.s == "months") months += v;
                else if (a.s == "year" || a.s == "years") years += v;
            }
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            if (months || years) {
                long long total = (y * 12 + (mo - 1)) + sign * (years * 12 + months);
                y = total >= 0 ? total / 12 : -((-total + 11) / 12);
                mo = total - y * 12 + 1;
                // clamp to the target month's length (2026-01-31 +1 month -> 2026-02-28)
                static const int mlen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                long long lim = mlen[mo - 1];
                if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) lim = 29;
                if (d > lim) d = lim;
            }
            return makeDate(civilToDays(y, mo, d) + sign * days);
        }
        if ((m == "later" || m == "earlier") && inv.hashKind == "DateTime") {
            long long sign = (m == "later") ? 1 : -1;
            long long secs = 0, days = 0, months = 0, years = 0;
            ValueList units; // `.later((:2hours, :30minutes))` passes the units in a list
            for (auto& a : args) { if (a.t == VT::Array && a.arr()) for (auto& x : *a.arr()) units.push_back(x); else units.push_back(a); }
            for (auto& a : units) if (a.t == VT::Pair && a.pairVal()) {
                long long v = a.pairVal()->toInt();
                if      (a.s == "second" || a.s == "seconds") secs   += v;
                else if (a.s == "minute" || a.s == "minutes") secs   += 60 * v;
                else if (a.s == "hour"   || a.s == "hours")   secs   += 3600 * v;
                else if (a.s == "day"    || a.s == "days")    days   += v;
                else if (a.s == "week"   || a.s == "weeks")   days   += 7 * v;
                else if (a.s == "month"  || a.s == "months")  months += v;
                else if (a.s == "year"   || a.s == "years")   years  += v;
            }
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            long long h = fld("hour"), mi = fld("minute"), tz = fld("timezone");
            double secF = inv.hash()->count("second") ? (*inv.hash())["second"].toNum() : 0.0;
            long long sInt = (long long)std::floor(secF); double frac = secF - (double)sInt;
            // calendar units first: shift year/month, clamp the day into the month
            if (months || years) {
                long long total = (y * 12 + (mo - 1)) + sign * (years * 12 + months);
                y = total >= 0 ? total / 12 : -((-total + 11) / 12);
                mo = total - y * 12 + 1;
                static const int mlen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                long long lim = mlen[mo - 1];
                if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) lim = 29;
                if (d > lim) d = lim;
            }
            // A LEAP second (:60) only exists on the day it was inserted — moving
            // off that day clamps it to :59 rather than rolling into midnight.
            if (sInt == 60 && (days || months || years || secs)) sInt = 59;
            // fixed duration: fold days + time into an absolute count, then re-split
            long long dayNum = civilToDays(y, mo, d) + sign * days;
            long long totSec = h * 3600 + mi * 60 + sInt + sign * secs;
            long long carry = totSec >= 0 ? totSec / 86400 : -((-totSec + 86399) / 86400);
            dayNum += carry; totSec -= carry * 86400;
            daysToCivil(dayNum, y, mo, d);
            long long nh = totSec / 3600, nmi = (totSec % 3600) / 60, nsec = totSec % 60;
            Value v = Value::makeHash(); v.hashKind = "DateTime";
            // a conversion keeps the formatter: `$dt.utc`, `.local`, `.clone`,
            // `.in-timezone`, `.later`, `.earlier` all stay formatted, as in Rakudo
            if (inv.hash()->count("formatter")) (*v.hash())["formatter"] = (*inv.hash())["formatter"];
            (*v.hash())["year"] = Value::integer(y); (*v.hash())["month"] = Value::integer(mo); (*v.hash())["day"] = Value::integer(d);
            (*v.hash())["hour"] = Value::integer(nh); (*v.hash())["minute"] = Value::integer(nmi);
            (*v.hash())["second"] = frac != 0.0 ? Value::number((double)nsec + frac) : Value::integer(nsec);
            (*v.hash())["timezone"] = Value::integer(tz);
            (*v.hash())["posix"] = Value::integer(dayNum * 86400 + totSec - tz);
            return v;
        }
        if ((m == "truncated-to" || m == "truncate-to") &&
            (inv.hashKind == "DateTime" || inv.hashKind == "Date") && !args.empty()) {
            std::string u = args[0].toStr();
            long long y = fld("year"), mo = fld("month"), d = fld("day"), h = fld("hour"), mi = fld("minute");
            double sec = inv.hash()->count("second") ? (*inv.hash())["second"].toNum() : 0.0;
            long long si = (long long)std::floor(sec);
            if (u == "second")      sec = (double)si;
            else if (u == "minute") { sec = 0; }
            else if (u == "hour")   { sec = 0; mi = 0; }
            else if (u == "day")    { sec = 0; mi = 0; h = 0; }
            else if (u == "week")   { sec = 0; mi = 0; h = 0;
                long long dn = civilToDays(y, mo, d); long long wd = ((dn % 7) + 3 + 7) % 7; // 0=Mon
                dn -= wd; daysToCivil(dn, y, mo, d); }
            else if (u == "month")  { sec = 0; mi = 0; h = 0; d = 1; }
            else if (u == "year")   { sec = 0; mi = 0; h = 0; d = 1; mo = 1; }
            if (inv.hashKind == "Date") return makeDate(civilToDays(y, mo, d));
            long long tz = fld("timezone");
            long long ep = civilToDays(y, mo, d) * 86400 + h * 3600 + mi * 60 + si - tz;
            Value v = Value::makeHash(); v.hashKind = "DateTime";
            // a conversion keeps the formatter: `$dt.utc`, `.local`, `.clone`,
            // `.in-timezone`, `.later`, `.earlier` all stay formatted, as in Rakudo
            if (inv.hash()->count("formatter")) (*v.hash())["formatter"] = (*inv.hash())["formatter"];
            (*v.hash())["year"] = Value::integer(y); (*v.hash())["month"] = Value::integer(mo); (*v.hash())["day"] = Value::integer(d);
            (*v.hash())["hour"] = Value::integer(h); (*v.hash())["minute"] = Value::integer(mi);
            (*v.hash())["second"] = (u == "second" && sec != std::floor(sec)) ? Value::number(sec) : Value::integer((long long)sec);
            (*v.hash())["timezone"] = Value::integer(tz); (*v.hash())["posix"] = Value::integer(ep);
            return v;
        }
        if (m == "is-leap-year" && (inv.hashKind == "Date" || inv.hashKind == "DateTime")) {
            long long y = fld("year");
            return Value::boolean((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
        }
        if (m == "days-in-month" || m == "last-date-in-month") {
            long long y = fld("year"), mo = fld("month");
            static const int mlen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            long long dim = (mo >= 1 && mo <= 12) ? mlen[mo - 1] : 30;
            if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) dim = 29;
            if (m == "days-in-month") return Value::integer(dim);
            return makeDate(civilToDays(y, mo, dim)); // last-date-in-month → a Date
        }
        if (m == "first-date-in-month")
            return makeDate(civilToDays(fld("year"), fld("month"), 1));
        if (m == "day-of-year") {
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            return Value::integer(civilToDays(y, mo, d) - civilToDays(y, 1, 1) + 1);
        }
        if (m == "days-in-year" && (inv.hashKind == "Date" || inv.hashKind == "DateTime")) {
            long long y = fld("year");
            return Value::integer(((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365);
        }
        if (m == "week-number" || m == "week-year" || m == "week") {
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            long long ordinal = civilToDays(y, mo, d) - civilToDays(y, 1, 1) + 1;
            static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
            auto dow = [&](long long yy, long long mm, long long dd) -> int { // 1=Mon..7=Sun
                long long yr = (mm < 3) ? yy - 1 : yy;
                int sak = (int)(((yr + yr / 4 - yr / 100 + yr / 400 + t[(mm - 1) % 12] + dd) % 7 + 7) % 7);
                return (sak + 6) % 7 + 1;
            };
            auto weeksInYear = [](long long yy) -> long long { // ISO 8601: 52 or 53
                auto p = [](long long a) { return (int)(((a + a / 4 - a / 100 + a / 400) % 7 + 7) % 7); };
                return (p(yy) == 4 || p(yy - 1) == 3) ? 53 : 52;
            };
            long long wd = dow(y, mo, d);
            long long week = (10 + ordinal - wd) / 7, wyear = y;
            if (week < 1) { wyear = y - 1; week = weeksInYear(y - 1); }
            else if (week > weeksInYear(y)) { wyear = y + 1; week = 1; }
            if (m == "week-number") return Value::integer(week);
            if (m == "week-year")   return Value::integer(wyear);
            Value o = Value::array({Value::integer(wyear), Value::integer(week)}); o.isList = true; return o;
        }
        if (m == "daycount") { // days since the MJD epoch (1858-11-17)
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            return Value::integer(civilToDays(y, mo, d) + 40587);
        }
        if (m == "day-fraction") {
            // a UTC day with a leap second is 86401 seconds long (finite frozen list)
            static const std::set<long long> leapDays = {
                19720630, 19721231, 19731231, 19741231, 19751231, 19761231, 19771231,
                19781231, 19791231, 19810630, 19820630, 19830630, 19850630, 19871231,
                19891231, 19901231, 19920630, 19930630, 19940630, 19951231, 19970630,
                19981231, 20051231, 20081231, 20120630, 20150630, 20161231};
            long long ymd = fld("year") * 10000 + fld("month") * 100 + fld("day");
            long long dayLen = 86400 + (leapDays.count(ymd) ? 1 : 0);
            // stay in Value arithmetic so an exact (Rat) second keeps the answer
            // exact: 12:23:00.43 of an ordinary day is 4458043/8640000
            Value sec = inv.hash()->count("second") ? (*inv.hash())["second"] : Value::integer(0);
            Value total = applyArith("+", applyArith("+",
                              Value::integer(fld("hour") * 3600),
                              Value::integer(fld("minute") * 60)), sec);
            return applyArith("/", total, Value::integer(dayLen));
        }
        if (m == "julian-date" || m == "modified-julian-date") {
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            // exact, like .day-fraction: the day count is an Int and the time of
            // day a Rat, so the sum stays rational
            Value sec = inv.hash()->count("second") ? (*inv.hash())["second"] : Value::integer(0);
            Value frac = applyArith("/", applyArith("+", applyArith("+",
                              Value::integer(fld("hour") * 3600),
                              Value::integer(fld("minute") * 60)), sec),
                          Value::integer(86400));
            // 2440587.5 = the Julian date of the civil epoch 1970-01-01T00:00
            Value jd = applyArith("+", applyArith("+",
                           Value::integer(civilToDays(y, mo, d)),
                           Value::ratZ(BigInt(4881175LL), BigInt(2LL))), frac);
            return m == "julian-date" ? jd
                 : applyArith("-", jd, Value::ratZ(BigInt(4800001LL), BigInt(2LL)));
        }
        // the timezone offset in bigger units (the raw one is seconds)
        if (m == "offset-in-minutes" && inv.hashKind == "DateTime")
            return applyArith("/", Value::integer(fld("timezone")), Value::integer(60));
        if (m == "offset-in-hours" && inv.hashKind == "DateTime")
            return applyArith("/", Value::integer(fld("timezone")), Value::integer(3600));
        if (m == "truncated-to" || m == "earlier" || m == "later") return inv; // best-effort (weeks etc.)
    }

    // `.new` on a scalar built-in type object → that type's default value. This is
    // what real Raku does (Str.new → "", Int.new → 0) and lets `augment class Str {…}`
    // methods be reached via `Str.new.themethod`.
    // A BacktraceFrame (from Exception.backtrace): file/line/code plus the
    // predicates Backtrace consumers grep by (Log::Async::Context)
    if (inv.t == VT::Hash && inv.hashKind == "BacktraceFrame" && inv.hash()) {
        if (m == "file" || m == "line" || m == "code") {
            auto it = inv.hash()->find(m.s);
            return it != inv.hash()->end() ? it->second : Value::any();
        }
        if (m == "is-hidden" || m == "is-setting" || m == "is-routine")
            return Value::boolean(false);
        if (m == "subname") {
            auto it = inv.hash()->find("code");
            return Value::str(it != inv.hash()->end() && it->second.code() ? it->second.code()->name : "");
        }
        if (m == "gist" || m == "Str") {
            auto fl = inv.hash()->find("file"); auto ln = inv.hash()->find("line");
            return Value::str("  in block at " + (fl != inv.hash()->end() ? fl->second.toStr() : "") +
                              " line " + (ln != inv.hash()->end() ? ln->second.toStr() : "0"));
        }
    }
    // A CallFrame (from `callframe`): .file / .line / .code, and `<unit>` as the
    // code's name at mainline, where there is no enclosing routine.
    if (inv.t == VT::Hash && inv.hashKind == "CallFrame" && inv.hash()) {
        if (m == "file" || m == "line") {
            auto it = inv.hash()->find(m.s);
            return it != inv.hash()->end() ? it->second : Value::any();
        }
        if (m == "code" || m == "callframe" || m == "my" || m == "annotations") {
            auto it = inv.hash()->find("code");
            if (m == "code") {
                if (it != inv.hash()->end()) return it->second;
                Value u; u.t = VT::Code; u.setCode(std::make_shared<Callable>());
                u.code()->name = "<unit>";        // the mainline is not a routine
                return u;
            }
            return Value::makeHash();
        }
        if (m == "gist" || m == "Str") {
            auto fl = inv.hash()->find("file"); auto ln = inv.hash()->find("line");
            return Value::str((fl != inv.hash()->end() ? fl->second.toStr() : "") + " at line " +
                              (ln != inv.hash()->end() ? ln->second.toStr() : "0"));
        }
    }
    // $?DISTRIBUTION — the compiling module's distribution. `.meta` is the parsed
    // META6.json (zef reads <version>/<ver>/<api>/<auth> from it), `.prefix` the
    // checkout root; `.content` reads a file listed in the meta.
    if (inv.t == VT::Hash && inv.hashKind == "Distribution" && inv.hash()) {
        if (m == "meta") { auto it = inv.hash()->find("meta"); return it != inv.hash()->end() ? it->second : Value::makeHash(); }
        if (m == "prefix") { auto it = inv.hash()->find("prefix"); return it != inv.hash()->end() ? it->second : Value::any(); }
        if (m == "name" || m == "Str" || m == "gist") {
            auto it = inv.hash()->find("meta");
            if (it != inv.hash()->end() && it->second.hash()) {
                auto n = it->second.hash()->find("name");
                if (n != it->second.hash()->end()) return Value::str(n->second.toStr());
            }
            return Value::str("Distribution");
        }
    }
    // `Match.new(:orig(…), :from(…), :pos(…), :list(…), :hash(…))` — the inverse of
    // Match.raku, so a match round-trips through EVAL (S05-match/raku.t does exactly
    // that). Every part is optional; what is absent is simply empty.
    if (inv.t == VT::Type && inv.s == "Match" && m == "new") {
        std::string orig; long long from = 0, pos = 0;
        Value list, hash;
        for (auto& a : args) {
            if (a.t != VT::Pair || !a.pairVal()) continue;
            if (a.s == "orig") orig = a.pairVal()->toStr();
            else if (a.s == "from") from = a.pairVal()->toInt();
            else if (a.s == "pos" || a.s == "to") pos = a.pairVal()->toInt();
            else if (a.s == "list") list = *a.pairVal();
            else if (a.s == "hash") hash = *a.pairVal();
        }
        if (from < 0) from = 0;
        if (pos < from) pos = from;
        std::string text = (size_t)from <= orig.size()
                         ? orig.substr((size_t)from, (size_t)(pos - from)) : std::string();
        Value mv = Value::matchVal(text, (long)from, (long)pos);
        mv.extM() = std::make_shared<std::string>(orig);
        if (list.t == VT::Array && list.arr()) *mv.arr() = *list.arr();
        if (hash.t == VT::Hash && hash.hash()) *mv.hash() = *hash.hash();
        return mv;
    }
    if (inv.t == VT::Type && inv.s == "Failure" && m == "new") {
        // Failure.new (no args) picks up the current $! as its exception.
        Value ex; bool haveEx = false;
        for (auto& a : args) if (a.t == VT::Object) { ex = a; haveEx = true; } // Failure.new($ex) / :exception
        if (!haveEx) { Value* be = tctx_.cur->find("$!"); if (be && be->t != VT::Nil && be->t != VT::Type) ex = *be; }
        Value f = Value::makeHash(); f.hashKind = "Failure";
        (*f.hash())["exception"] = ex;
        return f;
    }
    if (inv.t == VT::Type && inv.s == "Proxy" && m == "new") {
        // Proxy.new(:FETCH(method(){…}), :STORE(method($v){…})) — a container whose
        // reads call FETCH and whose writes call STORE (see VarExpr eval / evalAssign).
        Value p = Value::makeHash(); p.hashKind = "Proxy";
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal())
            { if (a.s == "FETCH" || a.s == "STORE") (*p.hash())[a.s] = *a.pairVal(); }
        return p;
    }
    if (inv.t == VT::Type && m == "new") {
        const std::string& t = inv.s;
        if (t == "Str" || t == "Cool") return Value::str("");
        if (t == "Int") return Value::integer(0);
        if (t == "Num" || t == "Real" || t == "Numeric") return Value::number(0.0);
        if (t == "Bool") return Value::boolean(false);
        // `Mu.new` / `Any.new` — an instance of the bare root type: defined (so
        // truthy), gisting as `Mu.new`. It carries no attributes of its own.
        if (t == "Mu" || t == "Any") {
            Value v = Value::makeHash(); v.hashKind = t; return v;
        }
    }
    if (inv.t == VT::Type && (inv.s == "List" || inv.s == "Array" || inv.s == "Seq" || inv.s == "array") && m == "new") {
        if (inv.s == "array" && inv.ofType().empty()) // native arrays need a type parameter
            throw RakuError{Value::typeObj("X::MustBeParametric"),
                            "Must first parameterize the vector type, e.g.: array[int32]"};
        // Seq.new(iterator-object): a user object doing Iterator drains by pull-one
        if (inv.s == "Seq" && args.size() == 1 && args[0].t == VT::Object && args[0].obj() &&
            args[0].obj()->cls && args[0].obj()->cls->findMethod("pull-one")) {
            Value* po = args[0].obj()->cls->findMethod("pull-one");
            Value v = Value::array(); v.isList = true; v.s = "Seq";
            for (;;) {
                ValueList none;
                Value x = invokeMethod(*po, args[0], none);
                if (x.t == VT::Type && x.s == "IterationEnd") break;
                v.arr()->push_back(x);
            }
            return v;
        }
        Value v = Value::array(); v.isList = (inv.s == "List" || inv.s == "Seq"); v.ofTypeM() = inv.ofType();
        std::vector<long long> dims;
        ValueList seed;
        for (auto& a : args) {
            if (a.t == VT::Pair && a.s == "shape") {
                if (a.pairVal()) for (auto& d : a.pairVal()->flatten()) dims.push_back(d.toInt());
                continue;
            }
            for (auto& x : toList(a)) seed.push_back(x);
        }
        if (!dims.empty()) { // shaped array — pre-sized, row-major, tagged with .shape()
            std::string et = v.ofType() == "Any" || v.ofType() == "Mu" ? "" : v.ofType();
            Value s = makeShapedContainer(dims, et, seed.empty() ? nullptr : &seed);
            s.isList = v.isList;
            return s;
        }
        *v.arr() = seed;
        return v;
    }
    if (inv.t == VT::Type && (inv.s == "Hash" || inv.s == "Map") && m == "Map")
        return Value::typeObj("Map"); // Hash.Map on the type object is the Map type
    if (inv.t == VT::Type && (inv.s == "Hash" || inv.s == "Map") && m == "new") {
        Value v = Value::makeHash(); v.ofTypeM() = inv.ofType();
        if (inv.s == "Map") v.hashKind = "Map"; // a Map is a distinct (immutable) type
        // The arguments FLATTEN before they are paired up, and they flatten all the
        // way down — `Hash.new((("a","1"),("b","2")))` is {a => 1, b => 2}, same as
        // the flat form. Spreading only ONE level left each inner list whole, so the
        // first became a stringified key and the second its value:
        // `{"a 1" => $("b","2")}`. That is what scrambled a Hash built from a zip
        // (`Hash.new: @keys Z @values`) in rakupp#12.
        //
        // An ITEMIZED list does not flatten — `Hash.new(($("a","1"),$("b","2")))`
        // really is `{"a 1" => $("b","2")}` in Rakudo — and a Pair is not a list, so
        // neither is descended into.
        ValueList items;
        std::function<void(const Value&)> spread = [&](const Value& x) {
            if ((x.t == VT::Array || x.t == VT::Range) && !x.itemized && x.t != VT::Pair) {
                for (auto& e : toList(x)) spread(e);
            } else items.push_back(x);
        };
        for (auto& a : args) spread(a);
        for (size_t k = 0; k < items.size(); k++) {
            // a HASH argument contributes its own pairs (`Hash.new({ :$curi, :@dists })`
            // and `Hash.new(%other)` — zef's list-installed builds its rows that way);
            // it used to be paired up as a KEY, so the hash came back empty
            if (items[k].t == VT::Hash && items[k].hash() && !items[k].itemized) {
                for (auto& kv : *items[k].hash()) (*v.hash())[kv.first] = kv.second;
                continue;
            }
            if (items[k].t == VT::Pair) (*v.hash())[items[k].s] = items[k].pairVal() ? *items[k].pairVal() : Value::any();
            else if (k + 1 < items.size()) { std::string key = items[k].toStr(); (*v.hash())[key] = items[k + 1]; k++; }
        }
        return v;
    }
    if (inv.t == VT::Type && inv.s == "IterationBuffer" && m == "new") {
        Value v = Value::makeHash(); v.hashKind = "IterationBuffer";
        Value items = Value::array();
        for (auto& a : args) for (auto& x : toList(a)) items.arr()->push_back(x);
        (*v.hash())["items"] = items;
        return v;
    }
    if (inv.t == VT::Type) {
        if (inv.s.rfind("IO::Spec", 0) == 0) { Value r; if (ioSpecMethod(*this, inv.s, m, args, r)) return r; }
        // `.CREATE` is the low-level allocator: an instance with attributes at their
        // declared defaults and NO BUILD/TWEAK run. Delegating to .new/.bless would
        // pass the one documented example (`Mu.CREATE.defined` is True) while being
        // semantically wrong, so it builds the ObjectData directly.
        if (m == "CREATE") {
            auto od = std::make_shared<ObjectData>();
            auto it = classes_.find(resolveClassAlias(inv.s));
            if (it != classes_.end()) {
                od->cls = it->second;
                for (auto* c = it->second.get(); c; c = c->parent.get())
                    for (auto& at : c->attrs)
                        if (!od->attrs.count(at.name))
                            od->attrs[at.name] = at.type.empty() ? Value::any() : Value::typeObj(at.type);
            }
            return Value::object(od);
        }
        // .^attributes on the built-in Date/DateTime: the introspection
        // JSON::Unmarshal walks to rebuild a value from its parts (its tests
        // mirror Rakudo's internals, daycount and formatter included). Names,
        // order and flags match Rakudo's; every type is Any — the rebuild
        // treats the parts opaquely and .new ignores unknown nameds exactly
        // as Rakudo's does. Other built-ins keep answering the empty list.
        if (m == "attributes" && !classes_.count(inv.s) &&
            (inv.s == "DateTime" || inv.s == "Date")) {
            static const char* dtA[] = {"$!hour", "$!minute", "$!second", "$!timezone",
                                        "$!year", "$!month", "$!day", "$!daycount", "&!formatter"};
            static const char* dA[]  = {"$!year", "$!month", "$!day", "$!daycount", "&!formatter"};
            Value out = Value::array(); out.isList = true;
            auto one = [&](const char* nm) {
                Value at = Value::makeHash(); at.hashKind = "Attribute";
                (*at.hash())["name"] = Value::str(nm);
                (*at.hash())["type"] = Value::typeObj("Any");
                (*at.hash())["readonly"] = Value::boolean(true);
                (*at.hash())["has_accessor"] = Value::boolean(true);
                (*at.hash())["is_built"] = Value::boolean(true);
                (*at.hash())["package"] = Value::typeObj(inv.s);
                out.arr()->push_back(at);
            };
            if (inv.s == "DateTime") for (auto* n : dtA) one(n);
            else                     for (auto* n : dA)  one(n);
            return out;
        }
        // .^mro / .mro on a built-in type → the class-only linearisation (roles like
        // Real/Numeric are excluded, matching Rakudo's Int.^mro == (Int Cool Any Mu)).
        if (m == "mro" && !classes_.count(inv.s)) {
            Value out = Value::array(); out.isList = true;
            for (auto& a : typeAncestry(inv.s)) if (!isBuiltinRole(a)) out.arr()->push_back(Value::typeObj(a));
            if (out.arr()->empty()) { out.arr()->push_back(Value::typeObj(inv.s)); out.arr()->push_back(Value::typeObj("Any")); out.arr()->push_back(Value::typeObj("Mu")); }
            return out;
        }
        if (m == "parents" && !classes_.count(inv.s)) { // built-in type: () by default, full chain with :all
            Value out = Value::array(); out.isList = true;
            bool all = false;
            for (auto& a : args) if (a.t == VT::Pair && a.s == "all" && (!a.pairVal() || a.pairVal()->truthy())) all = true;
            if (all) { bool self = true;
                for (auto& a : typeAncestry(inv.s)) { if (self) { self = false; continue; } if (!isBuiltinRole(a)) out.arr()->push_back(Value::typeObj(a)); } }
            return out;
        }
        // The whole MOP-mutator surface below is gated on the invocant being a
        // user-declared type, so `Int.^add_method` died. The storage and the reader
        // already exist: builtinExt_ is what `augment class Int {…}` writes to and
        // what method dispatch consults for every non-Object invocant. Only the
        // writer was missing. Kept to CORE types — isKnownTypeName blanket-accepts
        // any X::/Metamodel::/IO:: prefix, which would let a typo mint a method.
        if (!classes_.count(inv.s) && isCoreTypeName(inv.s)) {
            if (m == "add_method" && args.size() >= 2) {
                noteSymbolMutation("runtime .^add_method on a built-in type");
                builtinExt_[inv.s][args[0].toStr()] = args[1];
                return args[1];
            }
            if (m == "compose" || m == "publish_method_cache" || m == "invalidate_method_caches")
                return inv;
        }
        // `.^ver` / `.^auth` / `.^api` on a PACKAGE (`module Zef:ver(…):auth(…)`) —
        // a package has no ClassInfo, so its adverbs come from pkgMeta_. Everything
        // else answers Rakudo's defaults for a type that declares none: "" for auth,
        // the language version for ver. zef's plugin loader reads all three.
        if (m == "ver" || m == "auth" || m == "api") {
            // A subset answers with the language revision it was declared under
            // — `subset Even of Int …; Even.^ver` is 6.d in a 6.d unit and 6.e
            // in a 6.e one. Types that declare their own :ver fall through.
            if (m == "ver") {
                auto sit = subsets_.find(inv.s);
                if (sit != subsets_.end())
                    // A plain Str, as Rakudo's is: `E.^ver.^name` is Str there
                    // and `E.^ver ~~ Version` is False. Tagging it Version would
                    // print v6.d and answer that type check the other way.
                    return Value::str(sit->second.langRev == 0 ? "6.c"
                                    : sit->second.langRev == 1 ? "6.d" : "6.e");
            }
            auto pit = pkgMeta_.find(inv.s);
            std::string v = pit == pkgMeta_.end() ? std::string()
                          : (m == "ver" ? pit->second.ver : m == "auth" ? pit->second.auth : pit->second.api);
            if (!classes_.count(inv.s) || pit != pkgMeta_.end()) {
                if (m == "auth") return Value::str(v);
                if (v.empty() && m == "ver") { Value ver = Value::str("6.c"); ver.hashKind = "Version"; return ver; }
                if (v.empty()) return Value::any();
                if (m == "ver") { if (v[0] == 'v') v.erase(0, 1); Value ver = Value::str(v); ver.hashKind = "Version"; return ver; }
                return Value::str(v);
            }
        }
        auto cit = classes_.find(inv.s);
        if (cit != classes_.end()) {
            auto ci = cit->second;
            // grammar entry points
            if ((m == "parse" || m == "subparse" || m == "parsefile") && (ci->isGrammar || ci->findRule("TOP"))) {
                bool sub = (m == "subparse");
                // the built-in parse behavior (also the `next` candidate for a user override)
                auto builtinParse = [this, ci, sub](ValueList a) -> Value {
                    std::string startRule = "TOP"; Value actions;
                    for (auto& arg : a) {
                        if (arg.t == VT::Pair && arg.s == "rule") startRule = arg.pairVal() ? arg.pairVal()->toStr() : "TOP";
                        if (arg.t == VT::Pair && arg.s == "actions" && arg.pairVal()) actions = *arg.pairVal();
                    }
                    // an undefined parse target dies (Rakudo: warns on Any-to-Str
                    // coercion, then dies calling .chars on it) instead of
                    // silently parsing "" and returning Nil
                    if (!a.empty() && (a[0].t == VT::Any || a[0].t == VT::Nil))
                        throw RakuError{Value::typeObj("X::Method::NotFound"),
                            "No such method 'chars' for invocant of type '" +
                            a[0].typeName() + "'"};
                    std::string input = a.empty() ? "" : a[0].toStr();
                    Value r = grammarParse(ci.get(), input, sub, startRule, actions);
                    // From 6.e a FAILED .parse is a Failure carrying
                    // X::Syntax::Confused, not a bare Nil — 6.e gives grammars a
                    // base class whose parse reports where it stopped. .subparse
                    // keeps answering with what it managed to match.
                    if (sixE() && !sub && (r.t == VT::Nil || r.t == VT::Any || r.t == VT::Type)) {
                        Value f = Value::makeHash(); f.hashKind = "Failure";
                        (*f.hash())["exception"] = Value::typeObj("X::Syntax::Confused");
                        (*f.hash())["message"]   = Value::str("Confused");
                        return f;
                    }
                    return r;
                };
                if (m == "parsefile") { // slurp the file, then parse its contents
                    std::string input = args.empty() ? "" : args[0].toStr();
                    std::ifstream in(input); std::ostringstream ss; ss << in.rdbuf(); input = ss.str();
                    // Rakudo's parsefile matches the file contents verbatim,
                    // trailing newline included (rule sigspace absorbs it)
                    ValueList a2 = args; if (!a2.empty()) a2[0] = Value::str(input); else a2.push_back(Value::str(input));
                    return builtinParse(a2);
                }
                // A user-defined `method parse`/`subparse` (e.g. YAMLish wires :actions via nextwith)
                // runs first; the built-in is its redispatch target.
                if (Value* um = ci->findMethod(m)) {
                    RedispatchCtx prc; prc.next = builtinParse; prc.sameArgs = args;
                    redispatchStack_.push_back(std::move(prc));
                    Value r;
                    // ownFrame: the frame just pushed belongs to the invoked
                    // method — its redispatch floor must sit BELOW it, or
                    // nextwith/callsame can't reach the built-in parse
                    try { r = invokeMethod(*um, inv, args, rwArgs, /*ownFrame=*/true); }
                    catch (...) { redispatchStack_.pop_back(); throw; }
                    redispatchStack_.pop_back();
                    return r;
                }
                return builtinParse(args);
            }
            // metamodel (.^find_method / .^add_method / .^methods / .^lookup / .^can)
            if (m == "find_method" || m == "lookup") {
                std::string mn = args.empty() ? "" : args[0].toStr();
                Value* um = ci->findMethod(mn);
                return um ? *um : Value::nil();
            }
            if (m == "declares_method") { // locally declared (not inherited)?
                std::string mn = args.empty() ? "" : args[0].toStr();
                auto it = ci->methods.find(mn);
                return it != ci->methods.end() ? it->second : Value::boolean(false);
            }
            if (m == "find_method_qualified" && args.size() >= 2) { // ($type, $name)
                std::string tn = args[0].t == VT::Type ? args[0].s : args[0].typeName();
                std::string mn = args[1].toStr();
                auto tit = classes_.find(tn);
                if (tit != classes_.end()) { auto mit = tit->second->methods.find(mn); if (mit != tit->second->methods.end()) return mit->second; }
                Value* um = ci->findMethod(mn);
                return um ? *um : Value::nil();
            }
            if (m == "add_method") { // .^add_method($name, $code)
                if (args.size() >= 2) {
                    noteSymbolMutation("runtime .^add_method");
                    Value add = args[1];
                    // Adding a bare PROTO under a second name (Method::Also aliases a
                    // `proto method … is also<…>`) must alias the whole multi GROUP,
                    // or the alias runs the proto body — `{ * }` — instead of
                    // dispatching. Rakudo re-adds each candidate with
                    // ^add_multi_method; here the dispatcher already holds them, so
                    // install that.
                    if (add.t == VT::Code && add.code() && add.code()->isProto)
                        for (auto& kv : ci->methods) {
                            if (!(kv.second.t == VT::Code && kv.second.code() &&
                                  kv.second.code()->isMultiDispatcher)) continue;
                            bool holdsIt = false;
                            for (auto& cand : kv.second.code()->candidates)
                                if (cand.code() == add.code()) { holdsIt = true; break; }
                            if (holdsIt) { add = kv.second; break; }
                        }
                    ci->methods[args[0].toStr()] = add;
                }
                return args.size() >= 2 ? args[1] : Value::nil();
            }
            if (m == "add_role" && !args.empty()) { // .^add_role(Role) — runtime composition
                // The compile-time compose loop also merges multis and detects
                // conflicts; a runtime add takes the simple child-wins merge, which
                // is what the trait-time uses in the wild need (JSON::Class tags its
                // exception wrappers with Rakudo's X::Wrapper this way).
                std::string rn = args[0].t == VT::Type ? args[0].s : args[0].typeName();
                auto rit = classes_.find(rn);
                if (rit == classes_.end()) rit = classes_.find(resolveClassAlias(rn));
                if (rit == classes_.end() || !rit->second->isRole)
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        rn + " is not a known role, so " + ci->name + " cannot .^add_role it"};
                noteSymbolMutation("runtime .^add_role");
                for (auto& kv : rit->second->methods)
                    if (!ci->methods.count(kv.first)) ci->methods[kv.first] = kv.second;
                for (auto& at : rit->second->attrs) {
                    bool have = false;
                    for (auto& ex : ci->attrs) if (ex.name == at.name) { have = true; break; }
                    if (!have) ci->attrs.push_back(at);
                }
                ci->doneRoles.insert(rn);
                for (auto& dr : rit->second->doneRoles) ci->doneRoles.insert(dr);
                return inv;
            }
            // rakupp composes types eagerly, so .^compose is a no-op returning the
            // type (modules call it after add_method/add_attribute to finalize).
            // These bare metamodel names must NOT shadow a user method of the same
            // name — `Cro.compose(...)` is a real method, not a MOP finalize call.
            if ((m == "compose" || m == "compose_repr" || m == "publish_method_cache" ||
                 m == "publish_type_cache" || m == "compose_attributes" || m == "set_rw" ||
                 m == "invalidate_method_caches" || m == "publish_boolification_spec") &&
                !(ci && ci->findMethod(m)))
                return inv;
            if (m == "set_name" || m == "set_shortname") { if (!args.empty()) ci->name = args[0].toStr(); return inv; }
            // the Versioning SETTERS: only the getters existed, while the howOps
            // forwarding table already rewrote `$t.HOW.set_ver($t, v)` into
            // `$t.^set_ver(v)` — faithfully forwarding to a method that was not there.
            // `:ver<0.0.1>` stores a BARE "0.0.1" and the getter re-adds the v by
            // tagging hashKind "Version", so a `v0.0.1` literal must be stripped or
            // it comes back as vv0.0.1.
            if (m == "set_ver" && !args.empty()) {
                std::string vs = args[0].toStr();
                if (!vs.empty() && vs[0] == 'v') vs.erase(0, 1);
                ci->ver = vs; return inv;
            }
            if (m == "set_auth" && !args.empty()) { ci->auth = args[0].toStr(); return inv; }
            if (m == "set_api"  && !args.empty()) { ci->api  = args[0].toStr(); return inv; }
            if (m == "ver")  return ci->ver.empty()  ? Value::any() : ([&]{ Value v = Value::str(ci->ver);  v.hashKind = "Version"; return v; }());
            if (m == "auth") return Value::str(ci->auth);
            if (m == "api")  return ci->api.empty() ? Value::any() : Value::str(ci->api);
            if (m == "attribute_table") { // Hash: '$!name' => Attribute
                Value out = Value::makeHash();
                for (auto& a : ci->attrs)
                    (*out.hash())[std::string(1, a.sigil) + "!" + a.name] =
                        attributeMetaObject(a, ci->name);
                return out;
            }
            if (m == "add_attribute" && !args.empty()) { // .^add_attribute(Attribute.new(...))
                Value av = args[0];
                if (av.t == VT::Hash && av.hashKind == "Attribute" && av.hash()) {
                    ClassAttr a;
                    std::string an = av.hash()->count("name") ? (*av.hash())["name"].toStr() : "";
                    a.sigil = an.empty() ? '$' : an[0];
                    while (!an.empty() && (an[0]=='$'||an[0]=='@'||an[0]=='%'||an[0]=='&'||an[0]=='!'||an[0]=='.')) an = an.substr(1);
                    a.name = an;
                    a.type = av.hash()->count("type") && (*av.hash())["type"].t == VT::Type ? (*av.hash())["type"].s.str() : std::string();
                    a.rw = av.hash()->count("readonly") ? !(*av.hash())["readonly"].truthy() : false;
                    a.pub = av.hash()->count("has_accessor") ? (*av.hash())["has_accessor"].truthy() : false;
                    noteSymbolMutation("runtime .^add_attribute");
                    ci->attrs.push_back(a);
                }
                return av;
            }
            if (m == "can") {
                std::string mn = args.empty() ? "" : args[0].toStr();
                Value* um = ci->findMethod(mn);
                Value out = Value::array(); out.isList = true;
                if (um) out.arr()->push_back(*um);
                // BUILT-IN methods answer .can too: every class news/blesses/gists,
                // and a grammar parses (IETF::RFC_Grammar gates on `.can('parse')`)
                if (out.arr()->empty()) {
                    Value stub = builtinCanStub(mn, ci->isGrammar);
                    if (stub.t == VT::Code) out.arr()->push_back(stub);
                }
                return out;
            }
            if (m == "methods" || m == "method_names" || m == "method_table") {
                // `:local` → only this class's own methods; otherwise walk the user
                // inheritance chain (parents + roles), stopping before Any/Mu.
                // .^method_names is the method TABLE's names, so it is local by
                // definition — `class B is A` answers B's own methods only, and
                // an inherited `foo` is not among them (Rakudo agrees).
                // .^method_table is that same local table AS the Hash it is —
                // name => Method (DBIish gates its API on `{$_}:exists` over it).
                bool names = (m == "method_names");
                bool table = (m == "method_table");
                bool local = names || table;
                std::vector<std::string> tblNames;
                for (auto& a : args) if (a.t == VT::Pair && a.s == "local")
                    local = a.pairVal() ? a.pairVal()->truthy() : true;
                Value out = Value::array(); out.isList = true;
                std::set<ClassInfo*> visited; // dedup by class (MRO), not by method name
                std::set<std::string> seen;   // ...except inside one flattened table
                std::function<void(ClassInfo*)> walk = [&](ClassInfo* c) {
                    if (!c || !visited.insert(c).second) return;
                    for (auto& kv : c->methods) {
                        // private (!p) methods stay out, as in Rakudo
                        if (!kv.first.empty() && kv.first[0] == '!') continue;
                        if (local && !seen.insert(kv.first).second) continue;
                        out.arr()->push_back(names ? Value::str(kv.first) : kv.second);
                        if (table) tblNames.push_back(kv.first);
                    }
                    // a PUBLIC attribute's auto-generated accessor is a method too
                    // (Rakudo lists it; Data::Dump renders `method public () …`)
                    for (auto& at : c->attrs) {
                        if (!at.pub || c->methods.count(at.name)) continue;
                        if (local && !seen.insert(at.name).second) continue;
                        if (names) { out.arr()->push_back(Value::str(at.name)); continue; }
                        Value stub; stub.t = VT::Code; stub.setCode(std::make_shared<Callable>());
                        stub.code()->name = at.name; stub.code()->isMethod = true;
                        std::string an = at.name;
                        stub.code()->retType = at.type.empty() ? "" : at.type;
                        stub.code()->builtin = [an](Interpreter& I, ValueList& a) -> Value {
                            if (a.empty()) return Value::any();
                            ValueList rest(a.begin() + 1, a.end());
                            return I.methodCall(a[0], an, std::move(rest));
                        };
                        out.arr()->push_back(stub);
                        if (table) tblNames.push_back(at.name);
                    }
                    if (local) {
                        // a composed role's methods are FLATTENED into the class, so they
                        // belong to its own table: `class C does R` answers `rm` to both
                        // .^method_names and .^methods(:local), as Rakudo does. Class
                        // parents stay out — those are inherited, not local.
                        if (c->parent && c->parent->isRole) walk(c->parent.get());
                        for (auto& p : c->extraParents) if (p && p->isRole) walk(p.get());
                        return;
                    }
                    walk(c->parent.get());
                    for (auto& p : c->extraParents) walk(p.get());
                };
                walk(ci.get());
                if (table) {
                    Value h = Value::makeHash();
                    for (size_t i = 0; i < tblNames.size() && i < out.arr()->size(); i++)
                        (*h.hash())[tblNames[i]] = (*out.arr())[i];
                    return h;
                }
                return out;
            }
            if (m == "roles" || m == "role_typecheck_list") { // composed roles
                Value out = Value::array(); out.isList = true;
                for (auto& rn : ci->doneRoles) out.arr()->push_back(Value::typeObj(rn));
                return out;
            }
            if (m == "parents") { // immediate parents; composed roles are not parents
                Value out = Value::array(); out.isList = true;
                if (ci->parent && !ci->parent->isRole) out.arr()->push_back(Value::typeObj(ci->parent->name));
                for (auto& p : ci->extraParents) if (p && !p->isRole) out.arr()->push_back(Value::typeObj(p->name));
                // a built-in parent answers with its ancestry minus the hidden/
                // universal tail, matching Rakudo: G.^parents is (Grammar Match
                // Capture) — no Cool (hidden), no Any/Mu
                if (out.arr()->empty() && !ci->nativeParent.empty()) {
                    const auto& anc = typeAncestry(ci->nativeParent);
                    if (anc.empty() || anc[0] != ci->nativeParent)
                        out.arr()->push_back(Value::typeObj(ci->nativeParent));
                    else for (auto& a : anc)
                        if (a != "Any" && a != "Mu" && a != "Cool" && !isBuiltinRole(a))
                            out.arr()->push_back(Value::typeObj(a));
                }
                if (out.arr()->empty() && !ci->isRole) out.arr()->push_back(Value::typeObj("Any"));
                return out;
            }
            if (m == "mro") { // method resolution order: self, ancestors, then Any, Mu
                Value out = Value::array(); out.isList = true;
                // Depth-first over the primary + additional (multiple-inheritance) parents,
                // then dedup keeping the LAST occurrence — the C3 order for simple diamonds
                // (D is B is C, B/C is A → D, B, C, A).
                std::vector<std::string> lin;
                std::function<void(ClassInfo*)> visit = [&](ClassInfo* c) {
                    if (!c) return;
                    lin.push_back(c->name);
                    if (c->parent) visit(c->parent.get());
                    for (auto& p : c->extraParents) visit(p.get());
                };
                visit(ci.get());
                for (size_t i = 0; i < lin.size(); i++) {
                    bool later = false;
                    for (size_t j = i + 1; j < lin.size(); j++) if (lin[j] == lin[i]) { later = true; break; }
                    if (!later) out.arr()->push_back(Value::typeObj(lin[i]));
                }
                // a built-in parent anywhere up the primary chain contributes
                // its class-only ancestry: G,Grammar,Match,Capture,Cool,Any,Mu
                // for a grammar, F,DateTime,Any,Mu for `class F is DateTime`
                for (ClassInfo* c = ci.get(); c; c = c->parent.get())
                    if (!c->nativeParent.empty()) {
                        const auto& anc = typeAncestry(c->nativeParent);
                        if (anc.empty() || anc[0] != c->nativeParent) // no table entry: the parent alone
                            out.arr()->push_back(Value::typeObj(c->nativeParent));
                        else for (auto& a : anc)
                            if (a != "Any" && a != "Mu" && !isBuiltinRole(a))
                                out.arr()->push_back(Value::typeObj(a));
                        break;
                    }
                out.arr()->push_back(Value::typeObj("Any"));
                out.arr()->push_back(Value::typeObj("Mu"));
                return out;
            }
            if (m == "attributes") { // Attribute objects: .name ($!x), .type, .readonly
                bool local = false;
                for (auto& a : args) if (a.t == VT::Pair && a.s == "local") local = a.pairVal() ? a.pairVal()->truthy() : true;
                Value out = Value::array(); out.isList = true;
                std::set<ClassInfo*> visited;
                std::function<void(ClassInfo*)> walk = [&](ClassInfo* c) {
                    if (!c || !visited.insert(c).second) return;
                    for (auto& a : c->attrs) out.arr()->push_back(attributeMetaObject(a, c->name));
                    if (local) return;
                    walk(c->parent.get());
                    for (auto& p : c->extraParents) walk(p.get());
                };
                walk(ci.get());
                return out;
            }
            // `new`: a user-defined `new` (often a multi) coexists with the default
            // Mu.new. Use a custom candidate only if one matches the args; otherwise
            // fall back to default construction (named args / no args).
            if (m == "new") {
                Value* um = ci->findMethod("new");
                bool useCustom = um != nullptr;
                if (um && um->code() && um->code()->isMultiDispatcher) {
                    useCustom = false;
                    bool hasProto = false;
                    for (auto& cand : um->code()->candidates) {
                        if (cand.code() && cand.code()->isProto) { hasProto = true; continue; }
                        if (scoreCandidate(cand, args) >= 0) { useCustom = true; break; }
                    }
                    // A class that writes its own `proto method new(|)` REPLACES the
                    // default constructor: with no candidate matching, the call is an
                    // error, not a default construction. (Without a proto the multis
                    // only ADD to Mu.new, which still takes named attributes.)
                    if (!useCustom && hasProto) return invokeMethod(*um, inv, args, rwArgs);
                }
                // through the CHAIN, so a callwith/callsame inside the custom new
                // has a dispatcher (AttrProxy.new's callwith → the builtin Proxy.new)
                if (useCustom) return invokeMethodChain(m, ci.get(), inv, args, rwArgs);
            } else if (ci->findMethodForCall(m, langRev_ < 2)) {
                return invokeMethodChain(m, ci.get(), inv, args, rwArgs);
            }
            // accessing an attribute (public accessor) on a type object is illegal
            if (const ClassAttr* at = ci->findAttr(m)) {
                if (at->pub) throw RakuError{Value::typeObj("X::Method::NotFound"),
                    "Cannot look up attributes in a " + inv.s + " type object"};
            }
            if (m == "new" || m == "bless") {
                // An X::IO exception composes its .message from its attributes,
                // as Rakudo does — the class carries no method of its own, so the
                // text is built here, once, at construction.
                if (ci->name.compare(0, 6, "X::IO:") == 0) {
                    bool haveMsg = false;
                    std::map<std::string, std::string> a;
                    for (auto& g : args)
                        if (g.t == VT::Pair) {
                            if (g.s == "message") haveMsg = true;
                            a[g.s] = g.pairVal() ? g.pairVal()->toStr() : "";
                            if (g.s == "mode" && g.pairVal()) { // '0o755', octal, 3 digits
                                char buf[32]; snprintf(buf, sizeof buf, "%03llo",
                                                       (unsigned long long)g.pairVal()->toInt());
                                a["mode"] = buf;
                            }
                        }
                    const std::string& n = ci->name;
                    std::string msg;
                    auto oserr = [&] { return ": " + a["os-error"]; };
                    if      (n == "X::IO::Dir")     msg = "Failed to get the directory contents of '" + a["path"] + "'" + oserr();
                    else if (n == "X::IO::Rmdir")   msg = "Failed to remove the directory '" + a["path"] + "'" + oserr();
                    else if (n == "X::IO::Unlink")  msg = "Failed to remove the file '" + a["path"] + "'" + oserr();
                    else if (n == "X::IO::Chdir")   msg = "Failed to change the working directory to '" + a["path"] + "'" + oserr();
                    else if (n == "X::IO::Cwd")     msg = "Failed to get the working directory" + oserr();
                    else if (n == "X::IO::Symlink") msg = "Failed to create symlink called '" + a["name"] + "' on target '" + a["target"] + "'" + oserr();
                    else if (n == "X::IO::Link")    msg = "Failed to create link called '" + a["name"] + "' on target '" + a["target"] + "'" + oserr();
                    else if (n == "X::IO::Rename")  msg = "Failed to rename '" + a["from"] + "' to '" + a["to"] + "'" + oserr();
                    else if (n == "X::IO::Copy")    msg = "Failed to copy '" + a["from"] + "' to '" + a["to"] + "'" + oserr();
                    else if (n == "X::IO::Move")    msg = "Failed to move '" + a["from"] + "' to '" + a["to"] + "'" + oserr();
                    else if (n == "X::IO::Mkdir")   msg = "Failed to create directory '" + a["path"] + "' with mode '0o" + a["mode"] + "'" + oserr();
                    else if (n == "X::IO::Chmod")   msg = "Failed to set the mode of '" + a["path"] + "' to '0o" + a["mode"] + "'" + oserr();
                    else if (n == "X::IO::DoesNotExist") msg = "Failed to find '" + a["path"] + "' while trying to do '." + a["trying"] + "'";
                    if (!haveMsg && !msg.empty()) args.push_back(Value::pair("message", Value::str(msg)));
                }
                // NativeCall CStruct: allocate zeroed native memory and set fields
                // from named args, so the instance can be passed to / read from C.
                if (ci->repr == "CStruct" || ci->repr == "CPPStruct" || ci->repr == "CUnion") {
                    long long size = Interpreter::ncStructSize(ci.get());
                    void* mem = calloc(1, size ? (size_t)size : 1);
                    auto od = std::make_shared<ObjectData>();
                    od->cls = ci;
                    od->attrs["__native_ptr"] = Value::integer((long long)(intptr_t)mem);
                    od->attrs["__cstruct_owned"] = Value::boolean(true);
                    Value self = Value::object(od);
                    for (auto& arg : args) if (arg.t == VT::Pair && arg.pairVal()) {
                        std::string type; long long off = Interpreter::ncFieldOffset(ci.get(), arg.s, type);
                        if (off >= 0) Interpreter::ncWriteElem((long long)(intptr_t)mem + off, type, 0, *arg.pairVal());
                    }
                    if (Value* build = ci->findMethod("BUILD")) invokeMethod(*build, self, args);
                    if (Value* tweak = ci->findMethod("TWEAK")) invokeMethod(*tweak, self, args);
                    maybeRegisterDestroy(self);
                    return self;
                }
                // A class subclassing a native container (`class A is Array`): the
                // instance is an object backed by a native Array/Hash (via ObjectData.boxed),
                // so it indexes/pushes natively while .WHAT answers the user type.
                std::string nb;
                for (ClassInfo* c = ci.get(); c && nb.empty(); c = c->parent.get()) nb = c->nativeParent;
                if (nb == "Set" || nb == "SetHash" || nb == "Bag" || nb == "BagHash" ||
                    nb == "Mix" || nb == "MixHash") {
                    // `class MySet is Set`: back the instance with a real quanthash
                    // built from the args, so .elems/.keys/{k} dispatch to it
                    auto od = std::make_shared<ObjectData>();
                    od->cls = ci; od->hasBoxed = true;
                    od->boxed = methodCall(Value::typeObj(nb), "new", args);
                    Value self = Value::object(od);
                    if (Value* build = ci->findMethod("BUILD")) invokeMethod(*build, self, args);
                    if (Value* tweak = ci->findMethod("TWEAK")) invokeMethod(*tweak, self, args);
                    maybeRegisterDestroy(self);
                    return self;
                }
                if (nb == "Array" || nb == "List" || nb == "Hash" || nb == "Map") {
                    auto od = std::make_shared<ObjectData>();
                    od->cls = ci; od->hasBoxed = true;
                    if (nb == "Hash" || nb == "Map") od->boxed = Value::makeHash();
                    else { od->boxed = Value::array(); od->boxed.isList = (nb == "List"); }
                    od->boxed.ofTypeM() = inv.ofType(); // A[Int] -> element type on the box
                    for (auto& arg : args)
                        if (arg.t == VT::Pair) {
                            const ClassAttr* at = ci->findAttr(arg.s);
                            if (at && at->pub)
                                od->attrs[arg.s] = arg.pairVal() ? *arg.pairVal() : Value::any();
                        }
                    Value self = Value::object(od);
                    if (Value* build = ci->findMethod("BUILD")) invokeMethod(*build, self, args);
                    if (Value* tweak = ci->findMethod("TWEAK")) invokeMethod(*tweak, self, args); // post-BUILD hook
                    maybeRegisterDestroy(self);
                    return self;
                }
                // A class subclassing a scalar built-in with its own `.new` (DateTime,
                // Date): box the built-in and keep the user object's identity/attrs.
                if (nb == "DateTime" || nb == "Date") {
                    auto od = std::make_shared<ObjectData>(); od->cls = ci; od->hasBoxed = true;
                    std::vector<ClassInfo*> chain;
                    for (ClassInfo* c = ci.get(); c; c = c->parent.get()) chain.push_back(c);
                    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
                        for (auto& at : (*it)->attrs) {
                            Value dv;
                            if (at.hasDefVal) dv = at.defVal;
                            else if (at.def) { // close over the declaring class's scope
                                auto sv = tctx_.cur;
                                if ((*it)->declEnv) tctx_.cur = (*it)->declEnv;
                                try { dv = eval(const_cast<Expr*>(at.def)); }
                                catch (...) { tctx_.cur = sv; throw; }
                                tctx_.cur = sv;
                            } else dv = at.sigil == '@' ? Value::array()
                                      : at.sigil == '%' ? Value::makeHash() : Value::any();
                            if (at.objKeyed && dv.t == VT::Hash) dv.objKeyed = true;
                            od->attrs[at.name] = dv;
                        }
                    ValueList builtinArgs;
                    for (auto& a : args) {
                        if (a.t == VT::Pair && ci->findAttr(a.s)) od->attrs[a.s] = a.pairVal() ? *a.pairVal() : Value::any();
                        else builtinArgs.push_back(a);
                    }
                    od->boxed = methodCall(Value::typeObj(nb), "new", builtinArgs);
                    Value self = Value::object(od);
                    if (Value* build = ci->findMethod("BUILD")) invokeMethod(*build, self, args);
                    if (Value* tweak = ci->findMethod("TWEAK")) invokeMethod(*tweak, self, args);
                    maybeRegisterDestroy(self);
                    return self;
                }
                auto od = std::make_shared<ObjectData>();
                od->cls = ci;
                // attr defaults evaluate with `self` in scope, so a default
                // CLOSURE (`has $.cl = { self.foo }`) captures the new object
                Value selfEarly = Value::object(od);
                auto savedDenv = tctx_.cur;
                struct EnvRestore {
                    Interpreter& I; std::shared_ptr<Env> e;
                    ~EnvRestore() { I.tctx_.cur = e; }
                } envRestore{*this, savedDenv};
                std::vector<ClassInfo*> chain;
                for (ClassInfo* c = ci.get(); c; c = c->parent.get()) chain.push_back(c);
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    // each level's defaults close over ITS declaration scope
                    // (class-body constants/lexicals — `constant %Glyphs` in
                    // Font::AFM must resolve from another module's `.new`),
                    // not over whatever scope the CALLER happens to be in
                    auto denv = std::make_shared<Env>();
                    denv->parent = (*it)->declEnv ? (*it)->declEnv : savedDenv;
                    denv->define("self", selfEarly);
                    tctx_.cur = denv;
                    for (auto& at : (*it)->attrs) {
                        // The value the slot holds when it has no explicit default.
                        // A native-typed scalar takes its zero (`has atomicint $.n`
                        // starts at 0); a named type takes its TYPE OBJECT (not Any),
                        // so a `.= new` default reads that type object as its invocant
                        // (`has T $.x .= new` == `$!x = T.new`) — Rakudo semantics.
                        Value seed = at.sigil == '@' ? Value::array()
                                   : at.sigil == '%' ? Value::makeHash() : Value::any();
                        if (at.objKeyed && seed.t == VT::Hash) seed.objKeyed = true;
                        if (at.sigil == '$' && !at.type.empty()) {
                            if (at.type == "atomicint" || at.type == "byte" ||
                                at.type.rfind("int", 0) == 0 || at.type.rfind("uint", 0) == 0)
                                seed = Value::integer(0);
                            else if (at.type.rfind("num", 0) == 0) seed = Value::number(0);
                            else if (at.type == "str") seed = Value::str("");
                            else if (ascii::isupper((unsigned char)at.type[0])) seed = Value::typeObj(at.type);
                        }
                        bool userContainer = false;
                        if (!at.containerIs.empty() && at.sigil == '%') {
                            static const std::set<std::string> quant = {
                                "Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash"};
                            if (quant.count(at.containerIs))
                                seed = makeBaggy({}, at.containerIs); // has %.a is Set — empty Setty
                            else if (classes_.count(at.containerIs)) {
                                userContainer = true;
                                // a USER type: the attribute IS an instance of it, so
                                // its methods are reachable. DBIish declares
                                // `has %.Converter is DBDish::TypeConverter` and then
                                // calls `.convert` on it; as a plain Hash there was no
                                // such method and nothing said which line was at fault.
                                ValueList none;
                                seed = methodCall(Value::typeObj(at.containerIs), "new", none);
                            }
                        }
                        // Pre-seed the slot so a self-referential default (`.= new`,
                        // or one reading $!this-attr) sees the seed, not an unset Any.
                        od->attrs[at.name] = seed;
                        Value dv = at.hasDefVal ? at.defVal
                                 : at.def ? eval(const_cast<Expr*>(at.def))
                                          : seed;
                        // the SIGIL is a container type: `has @.a = (1,2)` holds an
                        // Array and `has %.h = (a=>1)` a Hash, so `.WHAT` answers
                        // (Array)/(Hash) and the default renderer shows [1, 2] /
                        // {:a(1)} rather than the List and Pair the initialiser
                        // happened to produce.
                        // …but a USER container type is the value: coercing it to
                        // the sigil would turn the object straight back into the
                        // plain Hash it was declared not to be.
                        if (!userContainer) dv = coerceToSigil(dv, at.sigil);
                        od->attrs[at.name] = dv;
                    }
                }
                tctx_.cur = savedDenv;
                // the default constructor binds nameds to declared PUBLIC attributes
                // only; anything else is silently ignored (Rakudo semantics — an
                // unknown name must NOT enter the attr store, or `$.name` inside a
                // method would see it instead of dying with X::Method::NotFound)
                for (auto& arg : args)
                    if (arg.t == VT::Pair) {
                        const ClassAttr* at = ci->findAttr(arg.s);
                        // `is built` opts a PRIVATE attr into construction-by-name —
                        // that is the trait's whole purpose (JSON::Class binds its
                        // $!declarant this way)
                        if (at && (at->pub || at->built))
                            od->attrs[arg.s] = coerceToSigil(arg.pairVal() ? *arg.pairVal() : Value::any(), at->sigil);
                    }
                // enforce an attribute type smiley (`has Int:D $.a` / `has Int:U $.a`)
                // on the FINAL slot value, matching Rakudo's X::TypeCheck::Attribute::Default.
                // Only when the attr actually received a value (an explicit default or a
                // construction arg); a bare `has T:D $.x` with neither is a compile-time
                // concern (X::Syntax::Variable::MissingInitializer) we don't model here.
                // the DEFAULT constructor takes named arguments only — a class
                // that wants positionals writes its own .new or a BUILD
                // …but a class deriving a BUILT-IN (`is Num`, `is Str`) inherits
                // that type's constructor, which does take a positional
                bool nativeBased = false;
                for (ClassInfo* c2 = ci.get(); c2; c2 = c2->parent.get())
                    // the implicit Grammar ancestor brings no positional
                    // constructor — Rakudo's G.new(42) refuses like any class
                    if (!c2->nativeParent.empty() && c2->nativeParent != "Grammar") { nativeBased = true; break; }
                if (!nativeBased && !ci->findMethod("new") && !ci->findMethod("BUILD"))
                    for (auto& arg : args)
                        if (arg.t != VT::Pair)
                            throwTypedV("X::Constructor::Positional",
                                        {{"type", Value::typeObj(ci->name)}},
                                        "Default constructor for '" + ci->name +
                                        "' only takes named arguments");
                // `is required` — construction must supply a value
                for (auto cit = chain.rbegin(); cit != chain.rend(); ++cit)
                    for (auto& at : (*cit)->attrs) {
                        if (!at.required) continue;
                        // `is required` means SUPPLIED AT CONSTRUCTION — a default
                        // of its own does not excuse it
                        bool gotArg = false;
                        for (auto& arg : args)
                            if (arg.t == VT::Pair && arg.s == at.name) { gotArg = true; break; }
                        if (!gotArg)
                            throwTypedV("X::Attribute::Required",
                                        {{"name", Value::str("$!" + at.name)},
                                         {"why", Value::str(at.requiredWhy)}},
                                        "The attribute '$!" + at.name + "' is required" +
                                        (at.requiredWhy.empty()
                                             ? std::string(", ")
                                             : " because " + at.requiredWhy + ",\n") +
                                        "but you did not provide a value for it.");
                    }
                for (auto cit = chain.rbegin(); cit != chain.rend(); ++cit)
                    for (auto& at : (*cit)->attrs) {
                        if (!at.defConstraint) continue;
                        bool gotArg = false;
                        for (auto& arg : args)
                            if (arg.t == VT::Pair && arg.s == at.name) { gotArg = true; break; }
                        if (!(at.def || at.hasDefVal || gotArg)) continue;
                        Value cur = od->attrs.count(at.name) ? od->attrs[at.name] : Value::any();
                        bool defd = defined(cur);
                        if ((at.defConstraint == 1 && !defd) || (at.defConstraint == 2 && defd))
                            throwTypedV("X::TypeCheck::Attribute::Default",
                                        {{"name", Value::str("$!" + at.name)}},
                                        "Type check failed on attribute '$!" + at.name +
                                        "'; expected " + at.type +
                                        (at.defConstraint == 1 ? ":D but got " : ":U but got ") +
                                        cur.typeName());
                    }
                Value self = Value::object(od);
                // bless does not re-run BUILD-from-new args the same way, but running
                // BUILD here matches the common `self.bless(:attr(...))` usage.
                if (Value* build = ci->findMethod("BUILD")) invokeMethod(*build, self, args);
                if (Value* tweak = ci->findMethod("TWEAK")) invokeMethod(*tweak, self, args); // post-BUILD hook
                maybeRegisterDestroy(self);
                return self;
            }
            // `SubDateTime.now` / `.today` — a type-level method not on the user class
            // dispatches to its built-in parent; box the result to keep the subclass.
            {
                std::string nb;
                for (ClassInfo* c = ci.get(); c && nb.empty(); c = c->parent.get()) nb = c->nativeParent;
                if ((nb == "DateTime" || nb == "Date") && !ci->findMethod(m) &&
                    m != "raku" && m != "gist" && m != "Str") {
                    Value r = methodCall(Value::typeObj(nb), m, args, rwArgs);
                    if (r.t == VT::Hash && (r.hashKind == "DateTime" || r.hashKind == "Date")) {
                        auto od = std::make_shared<ObjectData>(); od->cls = ci; od->hasBoxed = true; od->boxed = r;
                        std::vector<ClassInfo*> chain;
                        for (ClassInfo* c = ci.get(); c; c = c->parent.get()) chain.push_back(c);
                        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
                            for (auto& at : (*it)->attrs) {
                                Value dv;
                                if (at.hasDefVal) dv = at.defVal;
                                else if (at.def) { // declaring class's scope, as above
                                    auto sv = tctx_.cur;
                                    if ((*it)->declEnv) tctx_.cur = (*it)->declEnv;
                                    try { dv = eval(const_cast<Expr*>(at.def)); }
                                    catch (...) { tctx_.cur = sv; throw; }
                                    tctx_.cur = sv;
                                } else dv = at.sigil == '@' ? Value::array()
                                          : at.sigil == '%' ? Value::makeHash() : Value::any();
                                if (at.objKeyed && dv.t == VT::Hash) dv.objKeyed = true;
                                od->attrs[at.name] = dv;
                            }
                        return Value::object(od);
                    }
                    return r;
                }
            }
            if (m == "raku") return Value::str(inv.s); // type-object .raku is the bare name
            if (m == "gist") return Value::str("(" + inv.s + ")");
            if (m == "Str") return Value::str(""); // type objects stringify empty
        }
    }
    // exception object .throw / .fail: raise it (message from its .message method).
    // A class that defines a method of that name WINS — otherwise `$obj.throw(…)`
    // never reached the user's code and threw the invocant itself, so whatever
    // the method meant to raise was lost and CATCH received the object. Same
    // guard the `.backtrace` fallback below already uses.
    if ((m == "throw" || m == "rethrow" || m == "fail") && inv.t == VT::Object && inv.obj() &&
        !(inv.obj()->cls && inv.obj()->cls->findMethod(m))) {
        // record the backtrace at THROW time on the object itself — the thrown
        // value is shared, so a caught `$exception.backtrace` reads it back
        // (Log::Async::Context throws a fresh Exception exactly for the walk)
        if (m == "throw" && !inv.obj()->attrs.count("__bt"))
            inv.obj()->attrs["__bt"] = captureBacktrace();
        std::string msg;
        if (Value* mm = inv.obj()->cls ? inv.obj()->cls->findMethod("message") : nullptr)
            { try { ValueList none; msg = invokeMethod(*mm, inv, none).toStr(); } catch (...) {} }
        if (msg.empty()) { // no method — a plain `has $.message` attribute still speaks
            auto ma = inv.obj()->attrs.find("message");
            if (ma != inv.obj()->attrs.end() && rtIsDefined(ma->second)) msg = ma->second.toStr();
        }
        throw RakuError{inv, msg.empty() ? inv.typeName() : msg};
    }
    // `.backtrace` on an exception object: the frames its .throw recorded
    // (captured NOW for a never-thrown one). A class defining its own
    // backtrace method still wins — this only fills the built-in gap.
    if (m == "backtrace" && inv.t == VT::Object && inv.obj() &&
        !(inv.obj()->cls && inv.obj()->cls->findMethod("backtrace"))) {
        auto it = inv.obj()->attrs.find("__bt");
        return it != inv.obj()->attrs.end() ? it->second : captureBacktrace();
    }
    // user object: dispatch to class methods / public accessors first
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) {
        auto ci = inv.obj()->cls;
        if (Value* um0 = ci->findMethodForCall(m, langRev_ < 2)) {
            // a role's STUB method (`method body-serializer-selector() { ... }`)
            // is satisfied by the class's public attribute of the same name —
            // the accessor must win over executing the stub
            bool stubOverAttr = um0->t == VT::Code && um0->code() && um0->code()->isStub;
            if (stubOverAttr) {
                const ClassAttr* at0 = ci->findAttr(m);
                stubOverAttr = at0 && at0->pub;
            }
            // `self.new(:$name)` from inside a method, where the class writes only
            // `multi method new(Str $xml)`: with no proto those multis ADD to the
            // default constructor rather than replacing it, so a call none of them
            // takes falls through to it. (The type-object path already knows this;
            // the instance path was throwing before it got there.)
            bool passToDefault = false;
            if (m == "new" && um0->t == VT::Code && um0->code() && um0->code()->isMultiDispatcher) {
                passToDefault = true;
                for (auto& cand : um0->code()->candidates) {
                    if (cand.code() && cand.code()->isProto) { passToDefault = false; break; }
                    if (scoreCandidate(cand, args) >= 0) { passToDefault = false; break; }
                }
            }
            if (!stubOverAttr && !passToDefault)
                return invokeMethodChain(m, ci.get(), inv, args, rwArgs);
        }
        if (m == "clone") { // shallow copy, with :name(val) attribute overrides
            Value nv = inv; auto ni = std::make_shared<ObjectData>();
            ni->cls = inv.obj()->cls; ni->attrs = inv.obj()->attrs;
            for (auto& a : args) if (a.t == VT::Pair) ni->attrs[a.s] = a.pairVal() ? *a.pairVal() : Value::any();
            nv.setObj(ni); return nv;
        }
        // a grammar INSTANCE (`Grammar.new`) parses just like the type object
        if ((m == "parse" || m == "subparse" || m == "parsefile") && (ci->isGrammar || ci->findRule("TOP")))
            return methodCall(Value::typeObj(ci->name), m, args, rwArgs);
        // `self.bless(...)` / `$obj.new(...)` on an INSTANCE builds a fresh object
        // of its class (Cro::Uri's add(): `(self ?? $!create !! Cro::Uri).bless(|%parts)`)
        if (m == "bless" || m == "new")
            return methodCall(Value::typeObj(ci->name), m, std::move(args), rwArgs);
        const ClassAttr* at = ci->findAttr(m);
        if (at && at->pub) {
            auto it = inv.obj()->attrs.find(m);
            // X::AdHoc.message IS its payload stringified (Rakudo defines the
            // method that way), so `X::AdHoc.new(payload => "boom").message`
            // answers "boom" and not an undefined attribute. Only the
            // hand-built form needs it — `die "boom"` sets both.
            if ((it == inv.obj()->attrs.end() || !rtIsDefined(it->second)) &&
                m == "message" && ci->name == "X::AdHoc") {
                auto pl = inv.obj()->attrs.find("payload");
                if (pl != inv.obj()->attrs.end() && rtIsDefined(pl->second)) return Value::str(pl->second.toStr());
            }
            return it != inv.obj()->attrs.end() ? it->second : Value::any();
        }
        // `has %.h handles <iterator list …>` — a NAMED delegation is a real method
        // on the class in Rakudo, so it outranks every universal fallback below.
        // It has to be decided here and not in the tail (where `handles *` still
        // lives): names like .iterator/.list/.Str exist for every object, so a
        // delegation of one would never be reached if the built-in answered first.
        // That is what left zef's config wrapper — `class :: { has %.hash handles
        // <… iterator list …> }` — iterating as a single opaque object.
        for (ClassInfo* c = ci.get(); c; c = c->parent.get())
            for (auto& a : c->attrs)
                for (auto& hn : a.handles)
                    if (hn == m) {
                        auto ait = inv.obj()->attrs.find(a.name);
                        Value target = ait != inv.obj()->attrs.end() ? ait->second : Value::any();
                        if ((target.t == VT::Any || target.t == VT::Nil) && !a.type.empty())
                            target = Value::typeObj(a.type); // an unset typed attr delegates to its type object
                        return methodCall(target, m, std::move(args), rwArgs);
                    }
        // Real-role bridge: numeric coercions/methods the class doesn't define
        // dispatch through .Bridge BEFORE the generic Cool handlers (else `.Int`
        // would numify the object itself to 0)
        static const std::set<std::string> bridgeable = {
            "Int", "Num", "Rat", "FatRat", "Numeric", "Real", "Complex", "Str", "gist",
            "abs", "floor", "ceiling", "round", "truncate", "sign", "sqrt", "succ", "pred",
            "exp", "log", "log10", "log2", "sin", "cos", "tan", "asin", "acos", "atan",
            "atan2", "sec", "cosec", "cotan", "sinh", "cosh", "tanh", "isNaN", "narrow",
            "base", "chr", "fmt"};
        if (bridgeable.count(m)) {
            if (Value* br = ci->findMethod("Bridge")) {
                Value bv = invokeMethod(*br, inv, {});
                return methodCall(bv, m, std::move(args), rwArgs);
            }
        }
        // else fall through to universal methods (.defined/.WHAT/.gist/...)
    }

    // `*.method` -> a WhateverCode that applies the method to its argument
    if (inv.t == VT::Whatever) {
        // introspection metamethods do NOT autocurry: *.WHAT is (Whatever)
        if (m == "WHAT") return Value::typeObj("Whatever");
        if (m == "HOW" || m == "WHO" || m == "VAR" || m == "WHICH" || m == "raku")
            { /* fall through to the generic paths below with the Whatever value */ }
        else {
        Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
        std::string mc = m; ValueList ar = args;
        code.code()->builtin = [mc, ar](Interpreter& I, ValueList& a) -> Value {
            Value arg = a.empty() ? Value::any() : a[0];
            ValueList aa = ar;
            return I.methodCall(arg, mc, aa);
        };
        return code;
        }
    }

    // Code introspection / currying
    if (inv.t == VT::Code && inv.code()) {
        if (m == "assuming") { // partial application: &f.assuming(a,b)(c) == f(a,b,c)
            Value orig = inv; ValueList pre = args;
            Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
            code.code()->builtin = [orig, pre](Interpreter& I, ValueList& a) -> Value {
                // a `*` in the priming is a hole: the call's next positional
                // fills it, so `f.assuming(*, 2)(1)` calls `f(1, 2)`
                ValueList pos, nam;
                for (auto& x : a) (x.t == VT::Pair ? nam : pos).push_back(x);
                ValueList all; size_t pi = 0;
                for (auto& p : pre) {
                    if (p.t == VT::Whatever) { if (pi < pos.size()) all.push_back(pos[pi++]); }
                    else all.push_back(p);
                }
                for (; pi < pos.size(); pi++) all.push_back(pos[pi]);
                for (auto& n : nam) all.push_back(n); // the caller's nameds override the primed ones
                return I.callCallable(orig, all);
            };
            // residual signature: params the priming bound disappear; the rest
            // (unbound positional tail, unbound nameds, slurpies) remain
            if (inv.code()->params || inv.code()->hasPrimed) {
                std::vector<Value> posArgs; std::map<std::string, Value> namedBound;
                for (auto& a : pre) {
                    if (a.t == VT::Pair) namedBound[a.s] = a.pairVal() ? *a.pairVal() : Value::any();
                    else posArgs.push_back(a); // a `*` here is a hole, not a value
                }
                // `::T $a` primed with an Int makes every later `T` an Int
                std::map<std::string, std::string> typeBind;
                // priming args that can't bind throw right here, as Rakudo's do —
                // but only for a sub whose signature is the whole story (a body
                // using @_ / $^a placeholders takes whatever it's given)
                Value bindErr = Value::nil();
                bool checkBind = !inv.code()->usesArgs && inv.code()->placeholders.empty();
                std::set<std::string> namedSeen; bool posSlurpy = false, namedSlurpy = false;
                // priming an already-primed sub re-primes its residual signature
                std::vector<const Param*> src;
                if (inv.code()->hasPrimed) for (auto& sp : inv.code()->primedParams) src.push_back(sp.get());
                else if (inv.code()->params) for (auto& p : *inv.code()->params) src.push_back(&p);
                size_t pos = 0;
                code.code()->hasPrimed = true;
                for (const Param* pp : src) {
                    const Param& p = *pp;
                    if (p.invocant) continue;
                    auto keep = [&]() -> Param& {
                        code.code()->primedParams.push_back(signatureParamCopy(p));
                        Param& q = *code.code()->primedParams.back();
                        auto tb = typeBind.find(q.type);
                        if (tb != typeBind.end()) { q.type = tb->second; q.typeCapture = false; }
                        return q;
                    };
                    if (p.named) {
                        // any alias layer of `:b(:c($a))` can be the one primed
                        std::vector<std::string> keys = p.aliasKeys;
                        keys.push_back(!p.namedKey.empty() ? p.namedKey
                                     : p.name.size() > 1 ? p.name.substr(1) : p.name);
                        if (p.aliasBoth && p.name.size() > 1) keys.push_back(p.name.substr(1));
                        auto it = namedBound.end();
                        for (auto& k : keys) {
                            namedSeen.insert(k);
                            if (it == namedBound.end()) it = namedBound.find(k);
                        }
                        if (it == namedBound.end()) { keep(); continue; }
                        // a primed named param stays bindable — the caller may
                        // override it — but is now optional, defaulting to the
                        // primed value
                        Param& q = keep();
                        q.required = false; q.optional = true;
                        q.defaultRaku = rakuRepr(it->second);
                    }
                    else if (p.slurpy) { // a `|c` capture takes both kinds
                        if (p.sigil != '%') posSlurpy = true;
                        if (p.sigil == '%' || p.sigil == '\\' || p.sigil == '|') namedSlurpy = true;
                        keep();
                    }
                    else if (pos < posArgs.size()) {
                        const Value& av = posArgs[pos++];
                        if (av.t == VT::Whatever) { keep(); continue; } // hole: still unbound
                        if (p.typeCapture && !p.type.empty()) typeBind[p.type] = av.typeName();
                        // `Int @a` constrains the ELEMENTS, not the Array — only a
                        // scalar param's type checks against the argument itself
                        else if (!p.type.empty() && !p.coerce && p.sigil == '$' &&
                                 checkBind && bindErr.t == VT::Nil) {
                            auto tb = typeBind.find(p.type); // a bound `::T` checks as its type
                            const std::string& want = tb != typeBind.end() ? tb->second : p.type;
                            if (!typeOrSubsetMatches(av, want))
                                bindErr = makeTypedEx("X::TypeCheck::Binding",
                                    {{"expected", Value::typeObj(want)}, {"got", Value::typeObj(av.typeName())},
                                     {"symbol", Value::str(p.name)}},
                                    "Type check failed in binding " + p.name + "; expected " + want +
                                    " but got " + av.typeName());
                        }
                    }
                    else keep();
                }
                // Rakudo doesn't throw when a priming can't bind — it mixes a
                // Failure into the returned Code, so the call site sees it
                if (checkBind && bindErr.t == VT::Nil && pos < posArgs.size() && !posSlurpy)
                    bindErr = makeTypedEx("X::AdHoc", {},
                        "Too many positionals passed; expected " + std::to_string(pos) +
                        " arguments but got " + std::to_string(posArgs.size()));
                if (checkBind && bindErr.t == VT::Nil && !namedSlurpy)
                    for (auto& kv : namedBound)
                        if (!namedSeen.count(kv.first)) {
                            bindErr = makeTypedEx("X::AdHoc", {},
                                "Unexpected named argument '" + kv.first + "' passed");
                            break;
                        }
                if (bindErr.t != VT::Nil)
                    throw RakuError{bindErr, bindErr.obj() && bindErr.obj()->attrs.count("message")
                                             ? bindErr.obj()->attrs["message"].toStr() : "binding failed"};
            }
            return code;
        }
        if (m == "arity") {
            if (inv.code()->isWhateverCode) return Value::integer(std::max(1LL, inv.code()->whateverArity));
            long long n = 0;
            if (inv.code()->params) { for (auto& p : *inv.code()->params) if (!p.slurpy && !p.named && !p.optional) n++; }
            else n = (long long)inv.code()->placeholders.size();
            return Value::integer(n);
        }
        if (m == "count") { // required + optional positionals; a slurpy makes it Inf
            if (inv.code()->isWhateverCode) return Value::integer(std::max(1LL, inv.code()->whateverArity));
            long long n = 0; bool slurpy = false;
            if (inv.code()->params) for (auto& p : *inv.code()->params) {
                if (p.named) continue;
                if (p.slurpy) slurpy = true; else n++;
            } else n = (long long)inv.code()->placeholders.size();
            return slurpy ? Value::number(std::numeric_limits<double>::infinity()) : Value::integer(n);
        }
        if (m == "name") return Value::str(inv.code()->name);
        if (m == "returns" || m == "of")
            return inv.code()->retType.empty() ? Value::typeObj("Mu") : Value::typeObj(inv.code()->retType);
        if (m == "signature") return makeSignature(inv.code());
        if (m == "yada") return Value::boolean(inv.code()->isStub);   // a `{ ... }` / `{ !!! }` body
        if (m == "multi" || m == "is_dispatcher") return Value::boolean(inv.code()->isMultiDispatcher);
        if (m == "candidates") {
            Value out = Value::array(); out.isList = true;
            if (inv.code()->isMultiDispatcher) for (auto& c : inv.code()->candidates) out.arr()->push_back(c);
            else out.arr()->push_back(inv);
            return out;
        }
        // &candidate.dispatcher — the proto its dispatch group hangs off.
        // Carried by synthesized groups (&trait_mod:<is>); a dispatcher
        // answers itself, and a plain sub answers Mu, as in Rakudo.
        if (m == "dispatcher") {
            if (inv.code()->dispatcherC) {
                Value d; d.t = VT::Code; d.setCode(inv.code()->dispatcherC);
                return d;
            }
            if (inv.code()->isMultiDispatcher) return inv;
            return Value::typeObj("Mu");
        }
        // &routine.wrap(&wrapper): push a wrapper in front of the routine. Because
        // the Callable is shared (shared_ptr), every reference — including calls
        // through the routine's name — sees the wrap. Returns a handle for .unwrap.
        if (m == "wrap" && !args.empty()) {
            inv.code()->wrappers.push_back(args[0]);
            noteSymbolMutation("routine .wrap");
            Value h = Value::makeHash(); h.hashKind = "WrapHandle";
            (*h.hash())["routine"] = inv;               // keep the Callable alive
            (*h.hash())["wrapper"] = args[0];           // identity for targeted .unwrap
            return h;
        }
        // &routine.unwrap($handle) / .unwrap — remove a wrapper. With a handle, remove
        // that specific wrapper; otherwise pop the most-recent one (LIFO).
        if (m == "unwrap") {
            auto& ws = inv.code()->wrappers;
            if (!args.empty() && args[0].t == VT::Hash && args[0].hashKind == "WrapHandle" &&
                args[0].hash()->count("wrapper")) {
                const Value& target = (*args[0].hash())["wrapper"];
                for (size_t k = ws.size(); k-- > 0; )
                    if (ws[k].code() == target.code()) { ws.erase(ws.begin() + k); break; }
            }
            else if (!ws.empty()) ws.pop_back();
            noteSymbolMutation("routine .unwrap");
            return inv;
        }
    }

    // $handle.restore — undo the wrap this WrapHandle came from (sugar for
    // &routine.unwrap($handle))
    if (inv.t == VT::Hash && inv.hashKind == "WrapHandle" && m == "restore") {
        Value routine = inv.hash()->count("routine") ? (*inv.hash())["routine"] : Value();
        if (routine.t == VT::Code && routine.code()) {
            ValueList one{inv};
            return methodCall(routine, "unwrap", one);
        }
        return Value::boolean(false);
    }
    // CompUnit::DependencySpecification accessors.
    if (inv.t == VT::Hash && inv.hashKind == "DependencySpec") {
        if (m == "short-name" || m == "version-matcher" || m == "auth-matcher" || m == "api-matcher")
            return inv.hash()->count(m) ? (*inv.hash())[m] : Value::any();
    }
    // IO::Socket::INET connection/listener methods.
    if (inv.t == VT::Hash && inv.hashKind == "Socket") {
        int fd = (int)(*inv.hash())["fd"].toInt();
        if (m == "accept") {
            bool p = gilPark(); int cfd = ::accept(fd, nullptr, nullptr); gilUnpark(p);
            if (cfd < 0) return Value::nil();
            Value s = Value::makeHash(); s.hashKind = "Socket"; (*s.hash())["fd"] = Value::integer(cfd);
            return s;
        }
        // The port/address this socket is actually bound to. Asked of a listener
        // opened on `:localport(0)`, which is how a test gets a free port without
        // guessing one — the answer is only knowable after bind, from the OS.
        if (m == "localport" || m == "localhost" || m == "peerport" || m == "peerhost") {
            sockaddr_in sa{};
            socklen_t sl = sizeof(sa);
            bool peer = m[0] == 'p';
            int rc = peer ? ::getpeername(fd, (sockaddr*)&sa, &sl)
                          : ::getsockname(fd, (sockaddr*)&sa, &sl);
            if (rc < 0) return Value::nil();
            if (m == "localport" || m == "peerport") return Value::integer(ntohs(sa.sin_port));
            auto given = inv.hash()->find(m);   // the name as the caller wrote it
            if (given != inv.hash()->end()) return given->second;
            char buf[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
            return Value::str(buf);
        }
        if (m == "recv" || m == "read") {
            size_t want = 65536;
            if (!args.empty() && args[0].isNumeric()) {
                // `Inf` is a real argument here, not a mistake: it means "whatever
                // has arrived". OpenSSL's bio-read calls `$.net-read.()`, whose
                // closure defaults to `-> $n = Inf { $s.recv($n, :bin) }`, so the
                // very first read of an SSL handshake asked for infinity — and
                // sizing the buffer from it threw std::bad_alloc before the socket
                // was touched. Anything absurd is capped for the same reason; recv
                // is "up to" that many bytes, so a smaller buffer is still correct.
                static const size_t kCap = 64u * 1024 * 1024;
                double d = args[0].toNum();
                if (std::isfinite(d) && d >= 0) want = (size_t)std::min<double>(d, (double)kCap);
            }
            // `:bin` asks for BYTES, and `.read` is always binary — both must answer a
            // Buf, not a Str. Returning a Str made `Buf.new.append($chunk)` append
            // the string instead: it compiles, runs, and simply never matches, which
            // is how an HTTP header loop can spin forever with no error anywhere.
            bool bin = (m == "read");
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "bin") bin = !a.pairVal() || a.pairVal()->truthy();
            std::vector<char> buf(want ? want : 1);
            size_t got = 0;
            bool p = gilPark();
            // `.read($n)` answers EXACTLY $n bytes, blocking until it has them or
            // the peer closes; `.recv($n)` answers at most $n and returns what has
            // arrived. One recv() served both, so a reader that asked for a fixed
            // number got however much happened to be in the first packet — which is
            // silent until a message straddles a packet boundary, and then a chunked
            // HTTP body fails to parse its own chunk header.
            while (got < buf.size()) {
                ssize_t n = ::recv(fd, buf.data() + got, buf.size() - got, 0);
                if (n <= 0) { if (n < 0 && got == 0) { gilUnpark(p); return Value::nil(); } break; }
                got += (size_t)n;
                if (m != "read") break;   // recv is "up to", not "exactly"
            }
            gilUnpark(p);
            Value r = Value::str(std::string(buf.data(), got)); // got==0 => "" (peer closed)
            if (bin) { r.hashKind = "Buf"; identify(r); }
            return r;
        }
        // `.get` — one line, buffered: bytes are pulled as they arrive, and the
        // carry-over between calls lives in the handle ("linebuf"). A socket's
        // nl-in is ["\n", "\r\n"], so the terminator is '\n' with an optional
        // '\r' before it, both consumed and neither returned; Nil at EOF with
        // nothing buffered. LWP::Simple's local test servers read the request
        // this way — `Nil while $client.get.chars` — and without a socket .get
        // the call fell through to the FILE handle's reader and blocked both
        // ends of the conversation. (A later .recv does not see linebuf: the
        // suites read lines on one side of a socket and bytes on the other,
        // never both on one side.)
        if (m == "get" || m == "lines") {
            Value& lbv = (*inv.hash())["linebuf"];
            if (lbv.t != VT::Str) lbv = Value::str("");
            auto getOne = [&](bool& eof) -> Value {
                for (;;) {
                    std::string& lb = lbv.s.mut();
                    size_t nl = lb.find('\n');
                    if (nl != std::string::npos) {
                        std::string line = lb.substr(0, nl);
                        lb.erase(0, nl + 1);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        return Value::str(line);
                    }
                    char buf[8192];
                    bool p = gilPark();
                    ssize_t n = ::recv(fd, buf, sizeof buf, 0);
                    gilUnpark(p);
                    if (n <= 0) {
                        eof = true;
                        if (lb.empty()) return Value::nil();
                        std::string line = std::move(lb);
                        lb.clear();
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        return Value::str(line);
                    }
                    lb.append(buf, (size_t)n);
                }
            };
            bool eof = false;
            if (m == "get") return getOne(eof);
            Value out = Value::array(); out.isList = true;
            while (!eof) {
                Value l = getOne(eof);
                if (l.t == VT::Nil) break;
                out.arr()->push_back(l);
            }
            return out;
        }
        if (m == "print" || m == "write" || m == "send" || m == "put") {
            std::string data = args.empty() ? "" : args[0].toStr(); // Blob is a byte-Str
            if (m == "put") data += "\n";
            size_t off = 0;
            bool p = gilPark();
            while (off < data.size()) { ssize_t n = ::send(fd, data.data() + off, data.size() - off, 0); if (n <= 0) break; off += (size_t)n; }
            gilUnpark(p);
            return Value::boolean(true);
        }
        if (m == "close") { if (fd >= 0) ::close(fd); (*inv.hash())["fd"] = Value::integer(-1); return Value::boolean(true); }
    }

    if (inv.t == VT::Range && (m == "pick" || m == "roll") && inv.big()) {
        // a Range with a BIG upper endpoint (`^(2**100)`): uniform BigInt draws
        // in [rFrom, bound) by limb-wise rejection sampling
        BigInt bound = *inv.big();
        if (!inv.rExTo()) bound = bound + BigInt(1);
        BigInt span = bound - BigInt(inv.rFrom());
        if (span.sign > 0) {
            auto draw = [&]() -> Value {
                const auto& sm = span.mag;
                BigInt c;
                for (;;) {
                    c.mag.assign(sm.size(), 0);
                    for (size_t k = 0; k + 1 < sm.size(); k++) c.mag[k] = (uint32_t)(randDouble() * 1e9);
                    c.mag.back() = (uint32_t)(randDouble() * ((double)sm.back() + 1)); // top limb ≤ span's top
                    c.sign = 1; c.trim();
                    if (BigInt::cmpMag(c, span) < 0) break;
                }
                return Value::bigint(c + BigInt(inv.rFrom()));
            };
            if (args.empty()) return draw();
            bool all = args[0].t == VT::Whatever || (args[0].isNumeric() && std::isinf(args[0].toNum()));
            long long n = all ? 0 : args[0].toInt(); // pick(*) over an astronomic range is degenerate
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            if (m == "pick") {
                std::set<std::string> seen; // distinct draws, keyed by decimal form
                while ((long long)out.arr()->size() < n) {
                    Value v = draw();
                    if (seen.insert(v.toStr()).second) out.arr()->push_back(v);
                }
            }
            else for (long long i = 0; i < n; i++) out.arr()->push_back(draw());
            return out;
        }
    }
    if (inv.t == VT::Range && (m == "pick" || m == "roll")) {
        long long lo = inv.rFrom(), hi = inv.rTo();
        // integer spans sample directly — flattening ^2**40 (or ^2**20, 200 times) hangs
        if (hi >= lo && (unsigned long long)(hi - lo) >= 1024) {
            unsigned long long span = (unsigned long long)(hi - lo) + 1; // 0 == full 64-bit width
            auto draw = [&]() -> long long {
                unsigned long long r = ((unsigned long long)(randDouble() * 4294967296.0) << 32)
                                     | (unsigned long long)(randDouble() * 4294967296.0);
                return lo + (long long)(span ? r % span : r);
            };
            if (args.empty()) return Value::integer(draw());
            bool all = args[0].t == VT::Whatever ||
                       (args[0].isNumeric() && std::isinf(args[0].toNum()));
            // pick(*) shuffles the whole range when that is sane; a 2**64 request is degenerate
            long long n = all ? (span && span <= (1ULL << 22) ? (long long)span : 0) : args[0].toInt();
            if (m == "pick" && span && (unsigned long long)n > span) n = (long long)span;
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            if (m == "pick") {
                std::set<long long> seen;
                while ((long long)out.arr()->size() < n) {
                    long long v = draw();
                    if (seen.insert(v).second) out.arr()->push_back(Value::integer(v));
                }
            }
            else for (long long i = 0; i < n; i++) out.arr()->push_back(Value::integer(draw()));
            return out;
        }
    }
    // universal
    bool isFH = (inv.t == VT::Hash && inv.hashKind == "FileHandle");
    // An object deriving the BUILTIN IO::Handle is a handle, not a Mu that
    // prints itself: $cap.say("x") writes "x" through its WRITE sink, not
    // the object's own gist. Fall through to the handle-protocol shim.
    bool isUserHandle = false;
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) {
        std::string nb;
        for (ClassInfo* c = inv.obj()->cls.get(); c && nb.empty(); c = c->parent.get())
            nb = c->nativeParent;
        isUserHandle = nb == "IO::Handle";
    }
    if (m == "say" && !isFH && !isUserHandle) return ioEmit(gistOf(inv) + "\n", "$*OUT", false);
    if (m == "print" && !isFH && !isUserHandle) return ioEmit(strOf(inv), "$*OUT", false);
    if (m == "put" && !isUserHandle) return ioEmit(strOf(inv) + "\n", "$*OUT", false);
    if (m == "note") return ioEmit(gistOf(inv) + "\n", "$*ERR", true);
    if (m == "Str" || (inv.t == VT::Type && m == "Stringy")) {
        // type objects stringify empty (with a warning in Rakudo) — but
        // IterationEnd is a SENTINEL, and stringifies to its own name
        if (inv.t == VT::Type) return Value::str(inv.s == "IterationEnd" ? inv.s.str() : std::string());
        // `Int.Str(:superscript)` / `(:subscript)` render the digits (and a leading
        // minus) in the Unicode super/subscript forms. Note ¹²³ are NOT in the
        // U+2070 run — a `0x2070 + d` table is wrong for exactly those three.
        if (inv.t == VT::Int) {
            static const char* sup[] = {"⁰","¹","²","³","⁴",
                                        "⁵","⁶","⁷","⁸","⁹"};
            static const char* sub[] = {"₀","₁","₂","₃","₄",
                                        "₅","₆","₇","₈","₉"};
            bool up = false, down = false;
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal() && a.pairVal()->truthy()) {
                    if (a.s == "superscript") up = true;
                    else if (a.s == "subscript") down = true;
                }
            if (up || down) {
                std::string out;
                for (char c : inv.toStr()) {
                    if (c == '-') out += up ? "⁻" : "₋";
                    else if (c >= '0' && c <= '9') out += (up ? sup : sub)[c - '0'];
                    else out += c;
                }
                return Value::str(out);
            }
        }
        return Value::str(inv.toStr());
    }
    if ((m == "Int" || m == "Num" || m == "Real" || m == "Rat" || m == "FatRat") && inv.t == VT::Complex) {
        // Complex → Real conversions need |im| within $*TOLERANCE (default 1e-15),
        // so Num(exp i*π) works but a tightened tolerance throws (X::Numeric::Real)
        double tol = toleranceDyn();
        if (std::fabs(inv.im()) > tol * std::max(1.0, std::fabs(inv.n)))
            throw RakuError{Value::typeObj("X::Numeric::Real"),
                            "Cannot convert " + cnum::to_string(inv.n) + (inv.im() < 0 ? "" : "+") +
                            cnum::to_string(inv.im()) + "i to " + m + ": imaginary part not zero"};
        Value re = Value::number(inv.n);
        if (m == "Int") return Value::integer((long long)inv.n);
        if (m == "Rat" || m == "FatRat") return methodCall(re, m, {});
        return re; // Num / Real
    }
    if (inv.t == VT::Complex && (m == "floor" || m == "ceiling" || m == "round" || m == "truncate")) {
        // A Complex rounds PER COMPONENT, and `.round($scale)` rounds to a multiple
        // of the scale — `(1.256-3.875i).round(0.1)` is 1.3-3.9i. The scale used to
        // be dropped here. Each component goes through the scalar path rather than
        // repeating the arithmetic: that one is exact (Rat), and doing it again in
        // doubles gave -3.9000000000000004 for the imaginary part.
        auto f = [&](double x) {
            ValueList a2 = args;
            return methodCall(numifyStr(Value::number(x).toStr()), m, a2, nullptr).toNum();
        };
        return Value::complex(f(inv.n), f(inv.im()));
    }
    if (m == "Int") {
        // ±Inf / NaN cannot convert to Int (X::Numeric::CannotConvert)
        if (inv.t == VT::Num && !std::isfinite(inv.n))
            throw RakuError{Value::typeObj("X::Numeric::CannotConvert"),
                            "Cannot convert " + inv.toStr() + " to Int"};
        // a zero-denominator Rat FAILS on Int coercion (a Failure, not a throw —
        // fails-like requires the returned unhandled Failure)
        if (inv.t == VT::Rat && inv.ratD() && inv.ratD()->isZero()) {
            Value f = Value::makeHash(); f.hashKind = "Failure";
            (*f.hash())["exception"] = Value::typeObj("X::Numeric::DivideByZero");
            return f;
        }
        // Converting a string that carries an Nl/No numeral (Roman, circled,
        // Tamil ௰, …) is not supported — only Nd digits are numeric. (X::Str::Numeric)
        if (inv.t == VT::Str)
            for (size_t bi = 0; bi < inv.s.size(); ) {
                unsigned char b0 = inv.s[bi];
                int len = b0 < 0x80 ? 1 : b0 >= 0xF0 ? 4 : b0 >= 0xE0 ? 3 : 2;
                uint32_t cp = b0 < 0x80 ? b0 : (b0 & (0xFF >> (len + 1)));
                for (int k = 1; k < len && bi + k < inv.s.size(); k++) cp = (cp << 6) | ((unsigned char)inv.s[bi + k] & 0x3F);
                bi += len;
                if (cp >= 0x80) { std::string gc = uniGeneralCategory(cp);
                    if (gc == "Nl" || gc == "No")
                        throw RakuError{Value::typeObj("X::Str::Numeric"), "Cannot convert string to number: a numeral in category '" + gc + "' is not a digit"}; }
            }
        // A string / match text wider than int64 must stay EXACT — route through
        // the BigInt-aware parse, not the lossy long-long toInt() (which returns 0
        // on overflow). Int stays Int; Rat/Num truncate toward zero.
        if (inv.t == VT::Str || inv.t == VT::Match) {
            Value nv = numifyStrFailure(inv.toStr()); // a non-number is a Failure, like `+$str`
            if (nv.t == VT::Hash && nv.hashKind == "Failure") return nv;
            if (nv.t == VT::Int) return nv;
            if (nv.t == VT::Rat || nv.t == VT::Num) return methodCall(nv, "Int", ValueList{});
        }
        // A Range is a Cool, and a Cool container numifies to its ELEMENT COUNT:
        // `(1..5).Int` is 5, not 0. Value::toInt has no way to count one (a Str
        // range and an endless one both need the interpreter), so the coercion
        // methods ask .elems.
        if (inv.t == VT::Range) { ValueList none; return methodCall(inv, "elems", none); }
        // …and an Int that outgrew long long stays exact: toInt() saturates, so
        // `(2**64).Int` answered 9223372036854775807.
        if (inv.t == VT::Int && inv.big()) return Value::bigint(*inv.big());
        return Value::integer(inv.toInt());
    }
    if (m == "isNaN") {
        if (inv.t == VT::Num) return Value::boolean(std::isnan(inv.n));
        if (inv.t == VT::Rat) return Value::boolean(inv.ratD() && inv.ratD()->isZero() && inv.ratN() && inv.ratN()->isZero()); // 0/0
        if (inv.t == VT::Int || inv.t == VT::Bool) return Value::boolean(false);
    }
    if (m == "Num") {
        if ((inv.t == VT::Str && inv.hashKind.empty() && !inv.isAllomorph()) || inv.t == VT::Match) {
            Value nv = numifyStrFailure(inv.toStr());
            if (nv.t == VT::Hash && nv.hashKind == "Failure") return nv;
        }
        if (inv.t == VT::Range) { ValueList none; return Value::number(methodCall(inv, "elems", none).toNum()); }
        return Value::number(inv.toNum());
    }
    if (m == "Numeric" || m == "Real") {
        // a string numifies via the type-preserving ladder ("1"->Int, "1.5"->Rat,
        // "1e0"->Num), like `+$str` — and a non-number is that same Failure.
        if (inv.t == VT::Str) return numifyStrFailure(inv.s);
        if (inv.t == VT::Match) return numifyStrFailure(inv.toStr());
        // an already-numeric value is ITSELF: `3.Numeric` is an Int and `(-4/3).Real`
        // a Rat. Going through toNum() forced everything to Num.
        if (inv.t == VT::Int || inv.t == VT::Rat || inv.t == VT::Num) {
            // …but an ENUM VALUE numifies to a PLAIN Int. Returning it unchanged
            // kept its enumName, so `b.Numeric` and `+b` rendered as `b` rather
            // than 1, while `.Int` and `.value` (which build a fresh Int) were
            // right — the same value answering three ways.
            if (!inv.enumName.empty()) {
                Value n = inv; n.enumName.clear(); n.enumType.clear();
                return n;
            }
            return inv;
        }
        if (inv.t == VT::Bool) return Value::integer(inv.b ? 1 : 0);
        // a Cool CONTAINER numifies to its element count, and as an Int:
        // `(1,2).Numeric` is 2, not 2e0.
        if (inv.t == VT::Range || inv.t == VT::Array ||
            (inv.t == VT::Hash && (inv.hashKind.empty() || inv.hashKind == "Hash" || inv.hashKind == "Map")))
            { ValueList none; return methodCall(inv, "elems", none); }
        return Value::number(inv.toNum());
    }
    if (m == "Bool" || m == "so") {
        if (inv.t == VT::Object) return Value::boolean(boolify(inv)); // honours user Bool / Real Bridge
        if (inv.t == VT::Range) return Value::boolean(boolify(inv));  // 6.e: emptiness, not "has endpoints"
        return Value::boolean(inv.truthy());
    }
    if (m == "not") {
        if (inv.t == VT::Object) return Value::boolean(!boolify(inv));
        if (inv.t == VT::Range) return Value::boolean(!boolify(inv));
        return Value::boolean(!inv.truthy());
    }
    if (m == "defined") return Value::boolean(defined(inv));
    if (m == "DEFINITE") return Value::boolean(defined(inv)); // defined instance vs type/undef
    // Mu.return: return the invocant from the enclosing routine
    // (Cro's serializer selectors: `.return if .is-applicable(...)`)
    if (m == "return") throw ReturnEx{inv};
    if (m == "return-rw") throw ReturnEx{inv};
    // On a HOW — the persistent metaobject or a bare Metamodel::* type:
    // .archetypes answers the standard booleans and ^can/can admits the
    // meta-methods this HOW actually dispatches. JSON::Unmarshal's ClassLike
    // subset gates on `.HOW.archetypes.nominal && .HOW.^can('attributes')`;
    // without these it fell to the plain-Hash multi and unmarshal returned
    // Hash+{JSON::Class} instead of the typed object (the License::SPDX /
    // Test::META chain).
    {
        bool howInv = (inv.t == VT::Type && inv.s.rfind("Metamodel::", 0) == 0) ||
                      (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
                       inv.obj()->cls->name == "Metamodel::ClassHOW");
        if (howInv) {
            if (m == "archetypes") {
                Value a = Value::makeHash(); a.hashKind = "Archetypes";
                bool role = inv.t == VT::Type && inv.s.find("Role") != std::string::npos;
                (*a.hash())["nominal"]       = Value::boolean(!role);
                (*a.hash())["nominalizable"] = Value::boolean(false);
                (*a.hash())["parametric"]    = Value::boolean(role);
                (*a.hash())["generic"]       = Value::boolean(false);
                (*a.hash())["coercive"]      = Value::boolean(false);
                (*a.hash())["definite"]      = Value::boolean(false);
                (*a.hash())["augmentable"]   = Value::boolean(!role);
                return a;
            }
            if ((m == "can" || m == "^can") && !args.empty()) {
                static const std::set<std::string> howCan = {
                    "attributes", "methods", "method_names", "method_table", "name", "archetypes", "add_method",
                    "add_attribute", "compose", "roles", "parents", "mro"};
                return Value::boolean(howCan.count(args[0].toStr()) > 0);
            }
        }
    }
    // the Archetypes value itself: every query is a stored boolean (absent = False)
    if (inv.t == VT::Hash && inv.hashKind == "Archetypes") {
        auto it = inv.hash()->find(m);
        return it != inv.hash()->end() ? it->second : Value::boolean(false);
    }
    if (m == "can") { // Mu.can($name): list of matching methods ([] if none)
        std::string mn = args.empty() ? "" : args[0].toStr();
        Value out = Value::array(); out.isList = true;
        // A BUILT-IN value answers .can too. Everything below this was gated on a
        // user ClassInfo, so `Date.new(…).can('day-of-week')` was False even
        // though the method plainly works — and a module that GATES on .can, as
        // Date::Calendar::Strftime does for %u and %V, silently emitted the
        // format specifier instead of the value. The Dateish set is enumerated
        // against Rakudo's own answers rather than guessed.
        if (inv.t == VT::Hash && (inv.hashKind == "Date" || inv.hashKind == "DateTime")) {
            static const std::set<std::string> dateish = {
                "year", "month", "day", "day-of-week", "day-of-month", "day-of-year",
                "week", "week-number", "week-year", "days-in-month", "is-leap-year",
                "daycount", "yyyy-mm-dd", "dd-mm-yyyy", "mm-dd-yyyy",
                "later", "earlier", "truncated-to", "Str", "gist", "raku", "clone",
                "DateTime", "Date", "defined", "new" };
            static const std::set<std::string> timeOnly = {
                "hour", "minute", "second", "whole-second", "timezone",
                "utc", "local", "in-timezone", "posix" };
            static const std::set<std::string> dateOnly = { "succ", "pred" };
            bool isDT = inv.hashKind == "DateTime";
            if (dateish.count(mn) || (isDT ? timeOnly.count(mn) : dateOnly.count(mn))) {
                Value stub; stub.t = VT::Code; stub.setCode(std::make_shared<Callable>());
                stub.code()->name = mn; stub.code()->isMethod = true;
                std::string mnc = mn;
                stub.code()->builtin = [mnc](Interpreter& I, ValueList& a) -> Value {
                    if (a.empty()) return Value::any();
                    ValueList rest(a.begin() + 1, a.end());
                    return I.methodCall(a[0], mnc, std::move(rest));
                };
                out.arr()->push_back(stub);
                return out;
            }
        }
        ClassInfo* ci = nullptr;
        if (inv.t == VT::Object && inv.obj()) ci = inv.obj()->cls.get();
        else if (inv.t == VT::Type) { auto it = classes_.find(resolveClassAlias(inv.s)); if (it != classes_.end()) ci = it->second.get(); }
        if (ci) if (Value* um = ci->findMethod(mn)) out.arr()->push_back(*um);
        // a public attribute's auto-generated accessor answers .can too
        // (Cro's router gates on `$handler.can('method')` for `has $.method`)
        if (ci && out.arr()->empty()) {
            for (ClassInfo* c2 = ci; c2; c2 = c2->parent.get()) {
                const ClassAttr* at = c2->findAttr(mn);
                if (at && at->pub) {
                    Value stub; stub.t = VT::Code; stub.setCode(std::make_shared<Callable>());
                    stub.code()->name = mn; stub.code()->isMethod = true;
                    std::string mnc = mn;
                    stub.code()->builtin = [mnc](Interpreter& I, ValueList& a) -> Value {
                        if (a.empty()) return Value::any();
                        ValueList rest(a.begin() + 1, a.end());
                        return I.methodCall(a[0], mnc, std::move(rest));
                    };
                    out.arr()->push_back(stub);
                    break;
                }
            }
        }
        // BUILT-IN methods answer .can too: every class news/blesses/gists, and a
        // grammar parses (IETF::RFC_Grammar gates on `$g.can('parse')`). A stub
        // callable that dispatches for real if someone actually invokes it.
        if (ci && out.arr()->empty()) {
            Value stub = builtinCanStub(mn, ci->isGrammar);
            if (stub.t == VT::Code) out.arr()->push_back(stub);
        }
        // a BUILTIN value (Match, IO::Path, …) answers .can by PROBING, the same
        // oracle .^lookup uses: dispatch the name on a sentinel and read the
        // answer off X::Method::NotFound. Data::Dump renders a Match through
        // `qw<made pos hash from list orig>.grep({ $obj.^can($_) })`.
        // …and a bare BUILTIN TYPE object probes on a sentinel VALUE of the
        // type: `Str.can('Int')` is True (the .Int conversion method), which is
        // how DBDish::TypeConverter picks `$type($datum)` over `$type.new(...)`
        Value probeInv = inv;
        if (!ci && inv.t == VT::Type && !classes_.count(inv.s)) {
            if (inv.s == "Str") probeInv = Value::str("");
            else if (inv.s == "Int") probeInv = Value::integer(0);
            else if (inv.s == "Num") probeInv = Value::number(0);
            else if (inv.s == "Bool") probeInv = Value::boolean(false);
        }
        if (!ci && out.arr()->empty() && !mn.empty() &&
            probeInv.t != VT::Object && probeInv.t != VT::Type && probeInv.t != VT::Any && probeInv.t != VT::Nil &&
            // Date/DateTime share one dispatch surface here, so a probe cannot
            // tell them apart — the curated Dateish list above is authoritative
            probeInv.hashKind != "Date" && probeInv.hashKind != "DateTime") {
            static const std::set<std::string> kUnsafe = {
                "print", "say", "put", "note", "printf", "write", "spurt", "open",
                "mkdir", "rmdir", "symlink", "link", "unlink", "rename", "copy",
                "move", "chdir", "close", "flush", "seek", "run", "shell", "exit",
                "throw", "rethrow", "sink", "emit", "send", "recv", "start",
                "sleep", "kill", "signal", "await", "react", "trans", "subst-mutate"};
            if (!kUnsafe.count(mn)) {
                Value probe = probeInv;
                if (probe.hashKind == "IO") probe.s = "/nonexistent/rakupp-can-probe";
                bool notFound = false;
                try { ValueList none; methodCall(probe, mn, none); }
                catch (RakuError& e) {
                    const Value& pl = e.payload;
                    notFound = (pl.t == VT::Type && pl.s == "X::Method::NotFound") ||
                               (pl.t == VT::Object && pl.obj() && pl.obj()->cls &&
                                pl.obj()->cls->name == "X::Method::NotFound");
                }
                catch (...) {}
                if (!notFound) {
                    Value stub; stub.t = VT::Code; stub.setCode(std::make_shared<Callable>());
                    stub.code()->name = mn; stub.code()->isMethod = true;
                    std::string mnc = mn;
                    stub.code()->builtin = [mnc](Interpreter& I, ValueList& a) -> Value {
                        if (a.empty()) return Value::any();
                        ValueList rest(a.begin() + 1, a.end());
                        return I.methodCall(a[0], mnc, std::move(rest));
                    };
                    out.arr()->push_back(stub);
                }
            }
        }
        return out;
    }
    if (inv.t == VT::Type && m == "raku") return Value::str(inv.s); // Int.raku -> "Int" (no parens)
    // An OBJECT's gist is the interpreter's — Class.new(attr => …), a user .gist
    // method, an exception's message. Value::gist() has no access to any of that
    // and falls back to `Class<obj>`, so `say $x` and `say $x.gist` disagreed.
    if (m == "gist") return Value::str(inv.t == VT::Object ? gistOf(inv) : inv.gist());
    if (m == "raku") return Value::str(rakuRepr(inv));
    if (m == "Slip") { // a Slip flattens into any list-building context (from-list, list literals)
        if (inv.t == VT::Array) { Value r = inv; r.isList = true; r.s = "Slip"; return r; }
        if (inv.t == VT::Range) { Value r = Value::array(); *r.arr() = inv.flatten(); r.isList = true; r.s = "Slip"; return r; }
        return inv;
    }
    // IO::Special: the .path of the standard streams ("<STDOUT>" etc.)
    if (inv.t == VT::Str && inv.hashKind == "IO::Special") {
        if (m == "Str" || m == "what" || m == "gist") return Value::str(inv.s);
        if (m == "IO") return inv;
        if (m == "e") return Value::boolean(true);
        if (m == "d" || m == "f" || m == "l" || m == "x" || m == "z") return Value::boolean(false);
        if (m == "s") return Value::integer(0);
        if (m == "r") return Value::boolean(inv.s == "<STDIN>");
        if (m == "w") return Value::boolean(inv.s != "<STDIN>");
        if (m == "modified" || m == "accessed" || m == "changed") return Value::typeObj("Instant");
        if (m == "mode") return Value::nil();
        if (m == "raku") return Value::str("IO::Special.new(\"" + inv.s + "\")");
        if (m == "WHICH") { Value w = Value::str("IO::Special|" + inv.s); w.hashKind = "ObjAt"; return w; }
    }
    // a Blob/Buf (Str-tagged internally) is Positional over its BYTES, not a scalar
    if (inv.t == VT::Str && (inv.hashKind == "Blob" || inv.hashKind == "Buf")) {
        long long bn = inv.blobElems();
        if (m == "list" || m == "List" || m == "Array" || m == "values" ||
            m == "Seq" || m == "flat" || m == "eager" || m == "cache") {
            Value out = Value::array(); out.isList = (m != "Array");
            *out.arr() = inv.blobList();
            return out;
        }
        if (m == "elems") return Value::integer(bn);
        if (m == "head") return bn == 0 ? Value::any() : inv.blobElemAt(0);
        if (m == "tail") return bn == 0 ? Value::any() : inv.blobElemAt(bn - 1);
        if (m == "AT-POS" && !args.empty()) {
            long long i = args[0].toInt();
            if (i < 0) i += bn;
            return (i >= 0 && i < bn) ? inv.blobElemAt(i) : Value::any();
        }
    }
    // `.elems` on a type object is 1 — a type object is a one-element list of
    // itself, exactly like any other scalar. `.serial` answers the invocant
    // (only a Supply has anything to decide there).
    if (m == "elems" && (inv.t == VT::Type || inv.t == VT::Whatever)) return Value::integer(1);
    if (m == "serial" && inv.t != VT::Object) return inv;
    // .list/.List/.flat/.eager on a *scalar* (Int/Str/Num/Rat/Bool/Complex/Pair/type object)
    // yields a one-element list. Restricted to scalar types so list/array/range/seq values —
    // which carry their own list semantics upstream — are never re-wrapped.
    if ((m == "list" || m == "List" || m == "Seq" || m == "flat" || m == "eager" || m == "cache" || m == "lazy") &&
        (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Str ||
         inv.t == VT::Bool || inv.t == VT::Complex || inv.t == VT::Pair || inv.t == VT::Type ||
         inv.t == VT::Any || inv.t == VT::Nil)) {
        Value o = Value::array(); o.isList = true; o.arr()->push_back(inv);
        if (m == "Seq") o.s = "Seq";
        return o;
    }
    // .deepmap/.duckmap/.nodemap on a non-Iterable map the one element it stands
    // for — including a TYPE OBJECT, which is how `deepmap *.self, Array` reaches
    // here (it must not be a "no such method").
    if ((m == "deepmap" || m == "duckmap" || m == "nodemap") && !args.empty() &&
        (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Str ||
         inv.t == VT::Bool || inv.t == VT::Complex || inv.t == VT::Pair ||
         inv.t == VT::Type || inv.t == VT::Any || inv.t == VT::Nil)) {
        Value o = Value::array(); o.isList = true; o.arr()->push_back(inv);
        return methodCall(o, m, args, rwArgs);
    }
    if (m == "toggle" &&
        (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Str ||
         inv.t == VT::Bool || inv.t == VT::Complex || inv.t == VT::Pair)) {
        // Any.toggle: a non-iterable is a one-element list
        Value o = Value::array(); o.isList = true; o.arr()->push_back(inv);
        return methodCall(o, "toggle", args, rwArgs);
    }
    if (m == "sink") return Value::nil(); // Mu.sink: evaluate for side effects, yield Nil (user `sink` dispatched earlier)
    if (m == "VAR" || m == "self") return inv; // container introspection: value is its own container
    if (m == "item") { // .item: decontainerize to a single item (itemize a container)
        Value v = inv;
        if (v.t == VT::Array || v.t == VT::Hash) v.itemized = true;
        return v;
    }
    // Bool is an enum (False => 0, True => 1): .key is the name, .value the ordinal.
    if (inv.t == VT::Bool && m == "key")   return Value::str(inv.b ? "True" : "False");
    if (inv.t == VT::Bool && m == "value") return Value::integer(inv.b ? 1 : 0);
    // .VAR.name on an anonymous container is "element" in Rakudo; some code (Text::CSV)
    // uses `@x.VAR.name ne "element"` to detect an explicitly-passed array.
    if (m == "name" && (inv.t == VT::Array || inv.t == VT::Hash)) return Value::str("element");
    // enum value introspection (VT::Int carrying an enumName, e.g. `medium` of `enum Size <...>`)
    if (inv.t == VT::Int && !inv.enumName.empty()) {
        if (m == "key") return Value::str(inv.enumName);
        // a non-Int enum value (`One => "Eins"`) rides in pairVal beside the ordinal
        if (m == "value") return inv.pairVal() ? *inv.pairVal() : Value::integer(inv.toInt());
        if (m == "pair") return Value::pair(inv.enumName,
                                            inv.pairVal() ? *inv.pairVal() : Value::integer(inv.toInt()));
        // TYPE-level queries reach the enum type object — the tagged pair-list the
        // declaration built. `.enums` was implemented only there, so `Mass.enums`
        // worked and `g.enums` fell off the ladder. The VT::Array guard matters:
        // the type object carries enumType too, and forwarding from it recurses.
        if ((m == "enums" || m == "elems" || m == "pick" || m == "roll") && !inv.enumType.empty())
            if (Value* et = tctx_.cur->find(inv.enumType))
                if (et->t == VT::Array) return methodCall(*et, m, args, rwArgs);
    }
    if (inv.t == VT::Match && (m == "made" || m == "ast")) return inv.pairVal() ? *inv.pairVal() : Value::nil();
    if (inv.t == VT::Match && m == "Str") return Value::str(inv.s);
    // The engine records BYTE offsets into the subject, but Raku reports GRAPHEME
    // offsets — so `"áb" ~~ /b/` must say 1, not 2, and a combining mark must not
    // shift the answer at all. Converted here on the way out rather than in the
    // engine, because prematch/postmatch below genuinely want the byte offsets.
    auto graphemeOff = [](const Value& mv, long byteOff) -> Value {
        if (byteOff <= 0) return Value::integer(0);
        // Without the subject there is nothing to count graphemes IN, so answer the
        // offset unchanged. Falling back to `mv.s` — the MATCHED text — silently
        // clamped the offset to the match's own length, which turned a submatch's
        // `.from` into its length.
        if (!mv.ext()) return Value::integer((long long)byteOff);
        const std::string& orig = *std::static_pointer_cast<std::string>(mv.ext());
        size_t b = std::min((size_t)byteOff, orig.size());
        for (size_t i = 0; i < b; i++)             // pure ASCII: byte == grapheme
            if ((unsigned char)orig[i] >= 0x80)
                return Value::integer((long long)uniGraphemeCount(utf8cp(orig.substr(0, b))));
        return Value::integer((long long)b);
    };
    // A LIST of matches answers the span it covers: `.from` of the first, `.to` of
    // the last. `$/.list.from` is how a :g match reports where its matches start.
    if ((m == "from" || m == "to") && inv.t == VT::Array && inv.arr() && !inv.arr()->empty()) {
        const Value& end = m == "from" ? inv.arr()->front() : inv.arr()->back();
        if (end.t == VT::Match) return graphemeOff(end, m == "from" ? end.rFrom() : end.rTo());
    }
    if (inv.t == VT::Match && (m == "from")) return graphemeOff(inv, inv.rFrom());
    if (inv.t == VT::Match && (m == "to")) return graphemeOff(inv, inv.rTo());
    // `.pos` is where the engine has got to — meaningful on a match IN PROGRESS
    // (inside a `{…}` block), where it is the end of what has matched so far.
    if (inv.t == VT::Match && (m == "pos")) return graphemeOff(inv, inv.rTo());
    // `.target` is `.orig` under its Cursor-era name
    if (inv.t == VT::Match && (m == "orig" || m == "target" || m == "prematch" || m == "postmatch")) {
        std::string orig = inv.ext() ? *std::static_pointer_cast<std::string>(inv.ext()) : inv.s;
        if (m == "orig" || m == "target") return Value::str(orig);
        if (m == "prematch") return Value::str(orig.substr(0, std::min((size_t)inv.rFrom(), orig.size())));
        return Value::str((size_t)inv.rTo() <= orig.size() ? orig.substr(inv.rTo()) : "");
    }
    // Match.join joins the POSITIONAL CAPTURES (a Match is a Capture):
    // UUID.Str splits the hex run with /(....)(....).../ then .join("-")
    if (inv.t == VT::Match && m == "join" && inv.arr()) {
        Value lst = Value::array(); lst.isList = true;
        for (auto& e : *inv.arr()) lst.arr()->push_back(e);
        return methodCall(lst, "join", args, rwArgs);
    }
    // .caps: every positional AND named capture as key=>Match pairs, one entry
    // per occurrence (lists unfolded), ordered by where each occurrence matched.
    if (inv.t == VT::Match && m == "caps") {
        std::vector<std::pair<Value, Value>> entries;
        auto addEntry = [&](const Value& key, const Value& v) {
            if (v.isList && v.arr()) { for (auto& e : *v.arr()) entries.push_back({key, e}); }
            else entries.push_back({key, v});
        };
        if (inv.arr()) for (size_t i = 0; i < inv.arr()->size(); i++)
            addEntry(Value::integer((long long)i), (*inv.arr())[i]);
        if (inv.hash()) for (auto& kv : *inv.hash()) {
            if (!kv.first.empty() && kv.first[0] == '\x01') continue;
            addEntry(Value::str(kv.first), kv.second);
        }
        std::stable_sort(entries.begin(), entries.end(),
                         [](const std::pair<Value, Value>& a, const std::pair<Value, Value>& b) {
                             return a.second.rFrom() < b.second.rFrom();
                         });
        Value o = Value::array(); o.isList = true;
        for (auto& e : entries) {
            Value p = Value::pair(e.first.t == VT::Str ? e.first.s : e.first.toStr(), e.second);
            if (e.first.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(e.first);
            o.arr()->push_back(std::move(p));
        }
        return o;
    }
    if (inv.t == VT::Match && (m == "keys" || m == "values" || m == "list"
                               || m == "hash" || m == "pairs" || m == "kv" || m == "elems")) {
        if (m == "hash") { Value h = Value::makeHash(); if (inv.hash()) *h.hash() = *inv.hash(); return h; }
        if (m == "elems") return Value::integer(inv.arr() ? (long long)inv.arr()->size() : 0);
        Value o = Value::array(); o.isList = true;
        // Set/Bag/Mix keep the element's original type in the count's pairKey.
        auto typedKey = [](const std::pair<const std::string, Value>& kv) {
            return kv.second.pairKey() ? *kv.second.pairKey() : Value::str(kv.first);
        };
        if (m == "keys") {
            if (inv.arr()) for (size_t i = 0; i < inv.arr()->size(); i++) o.arr()->push_back(Value::integer((long long)i));
            if (inv.hash()) for (auto& kv : *inv.hash()) o.arr()->push_back(typedKey(kv));
        } else if (m == "values" || m == "list") {
            if (inv.arr()) for (auto& e : *inv.arr()) o.arr()->push_back(e);
            if ((m == "values") && inv.hash()) for (auto& kv : *inv.hash()) o.arr()->push_back(kv.second);
        } else { // pairs / kv
            if (inv.arr()) for (size_t i = 0; i < inv.arr()->size(); i++) {
                if (m == "kv") { o.arr()->push_back(Value::integer((long long)i)); o.arr()->push_back((*inv.arr())[i]); }
                else o.arr()->push_back(Value::pair(std::to_string(i), (*inv.arr())[i]));
            }
            if (inv.hash()) for (auto& kv : *inv.hash()) {
                if (m == "kv") { o.arr()->push_back(typedKey(kv)); o.arr()->push_back(kv.second); }
                else { Value p = Value::pair(kv.first, kv.second); p.pairKeyM() = kv.second.pairKey(); o.arr()->push_back(std::move(p)); }
            }
        }
        return o;
    }
    // `.of` on a typed container: `my Int @a` / `my Int %h` → Int (Mu when untyped).
    // For Hash[V,K]/Array[T] the value/element type is the first parameter component.
    if (m == "of" && (inv.t == VT::Array || inv.t == VT::Hash)) {
        // a quanthash's ofType is its KEY parameter; .of is the fixed value type
        if (inv.t == VT::Hash) if (const char* vt = quantValueType(inv.hashKind)) return Value::typeObj(vt);
        if (inv.ofType().empty()) return Value::typeObj("Mu");
        std::string ot = inv.ofType(); auto c = ot.find(','); if (c != std::string::npos) ot = ot.substr(0, c);
        return Value::typeObj(ot);
    }
    if (m == "WHAT") {
        // typed container -> its parameterized type object (Array[Int] / Hash[Int,Str])
        if ((inv.t == VT::Array || inv.t == VT::Hash) && !inv.ofType().empty()) {
            Value ty = Value::typeObj(inv.t == VT::Array ? "Array" : "Hash");
            ty.ofTypeM() = inv.ofType();
            return ty;
        }
        if (inv.t == VT::Type) return inv; // a (parameterized) type object is its own .WHAT
        // native-container subclass instance parameterized as A[Int]
        if (inv.t == VT::Object && inv.obj() && inv.obj()->hasBoxed && inv.obj()->cls &&
            !inv.obj()->boxed.ofType().empty()) {
            Value ty = Value::typeObj(inv.obj()->cls->name); ty.ofTypeM() = inv.obj()->boxed.ofType(); return ty;
        }
        return Value::typeObj(inv.typeName());
    }
    if (m == "iterator") { // S07: make an Iterator over this value's elements
        Value it = Value::makeHash(); it.hashKind = "Iterator";
        Value items = Value::array();
        bool lazy = false;
        if (inv.t == VT::Array && inv.arr()) { *items.arr() = *inv.arr(); lazy = inv.b; }
        else if (inv.t == VT::Range) { *items.arr() = inv.flatten();
            lazy = inv.b || inv.rTo() >= 9000000000000000000LL; } // infinite / `lazy`-marked range
        else if (inv.t == VT::Hash) { // plain hash and Set/Bag/Mix iterate their pairs
            ValueList none;
            Value ps = methodCall(inv, "pairs", none, nullptr);
            if (ps.t == VT::Array && ps.arr()) *items.arr() = *ps.arr();
            // a hash has no promised order, so neither has its iterator
            (*it.hash())["nondeterministic"] = Value::boolean(true);
        }
        else if (inv.t != VT::Nil && inv.t != VT::Any) items.arr()->push_back(inv);
        (*it.hash())["items"] = items;
        (*it.hash())["pos"] = Value::integer(0);
        if (lazy) (*it.hash())["lazy"] = Value::boolean(true);
        return it;
    }
    // Date/DateTime clone rebuilds via `.new` so `:field(v)` overrides apply AND
    // validate (rejecting e.g. `.clone(month => 13)`), recomputing posix.
    if (m == "clone" && inv.t == VT::Hash && (inv.hashKind == "DateTime" || inv.hashKind == "Date") && inv.hash()) {
        std::map<std::string, Value> merged;
        // the formatter travels with the clone, like every other conversion
        for (const char* k : {"year", "month", "day", "hour", "minute", "second", "timezone", "formatter"})
            if (inv.hash()->count(k)) merged[k] = (*inv.hash())[k];
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) merged[a.s] = *a.pairVal();
        ValueList na; for (auto& kv : merged) na.push_back(Value::pair(kv.first, kv.second));
        return methodCall(Value::typeObj(inv.hashKind), "new", na);
    }
    if (m == "clone") { // non-object clone: shallow copy of containers, self for immutables
        if (inv.t == VT::Array) { Value nv = inv; nv.setArr(std::make_shared<ValueList>(*inv.arr())); return nv; }
        if (inv.t == VT::Hash)  { Value nv = inv; nv.setHash(std::make_shared<ValueMap>(*inv.hash())); return nv; }
        return inv; // Int/Num/Rat/Str/Bool/… are immutable — clone is the value itself
    }
    // `Metamodel::ClassHOW.new_type(:name, :ver, :auth)` — a class created at
    // RUNTIME. It registers like a declared one, so `.^add_method` and `.new`
    // then work on it exactly as they do on `class Foo { }`.
    if (inv.t == VT::Type && m == "new_type" &&
        (inv.s == "Metamodel::ClassHOW" || inv.s == "Metamodel::ParametricRoleHOW" ||
         inv.s == "Metamodel::ParametricRoleGroupHOW" ||
         // A PACKAGE/MODULE/GRAMMAR created at runtime builds the same way here —
         // rakupp models all of them with ClassInfo, and the HOW only decides what
         // the type reports about itself. Omitting PackageHOW meant
         // `Metamodel::PackageHOW.new_type(name => …)` threw "No such method", which
         // is what stopped roast's packages/S11-modules/lib/RuntimeCreatedPackage
         // from loading.
         inv.s == "Metamodel::PackageHOW" || inv.s == "Metamodel::ModuleHOW" ||
         inv.s == "Metamodel::GrammarHOW")) {
        auto ci = std::make_shared<ClassInfo>();
        ci->name = "<anon|1>";
        for (auto& a : args)
            if (a.t == VT::Pair && a.pairVal()) {
                if (a.s == "name") ci->name = a.pairVal()->toStr();
                else if (a.s == "ver") ci->ver = a.pairVal()->toStr();
                else if (a.s == "auth") ci->auth = a.pairVal()->toStr();
                else if (a.s == "api") ci->api = a.pairVal()->toStr();
            }
        noteSymbolMutation("runtime .new_type");
        classes_[ci->name] = ci;
        return Value::typeObj(ci->name);
    }
    // The HOW forms of the MOP operations take the type as their FIRST argument —
    // `$t.HOW.add_method($t, …)` — where the `.^` spelling passes it implicitly
    // as the invocant. Named explicitly: a metaclass also answers ORDINARY
    // methods (`.isa`, `.gist`), which must not be forwarded to their argument.
    if (((inv.t == VT::Type && inv.s == "Metamodel::ClassHOW") ||
         (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
          [&]{ for (ClassInfo* c = inv.obj()->cls.get(); c; c = c->parent.get())
                   if (c->name == "Metamodel::ClassHOW") return true; return false; }())) &&
        !args.empty() && args[0].t == VT::Type) {
        static const std::set<std::string> howOps = {
            "add_method", "add_attribute", "add_parent", "add_role", "add_fallback",
            "compose", "compose_repr", "compose_attributes", "set_name", "set_shortname",
            "set_ver", "set_auth", "set_api", "set_rw",
            "publish_method_cache", "publish_type_cache", "invalidate_method_caches"};
        if (howOps.count(m)) {
            ValueList rest(args.begin() + 1, args.end());
            return methodCall(args[0], "^" + m, rest, rwArgs);
        }
    }
    if (m == "HOW") {
        // A USER class gets ONE persistent metaobject, so `T.HOW does SomeRole`
        // mixins stick (Method::Also's AliasableClassHOW). Its class is named
        // Metamodel::ClassHOW, keeping `~~ Metamodel::ClassHOW` True. Built-ins
        // keep the plain type object.
        ClassInfo* hci = nullptr;
        if (inv.t == VT::Type) { auto it = classes_.find(inv.s); if (it != classes_.end()) hci = it->second.get(); }
        else if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) hci = inv.obj()->cls.get();
        if (hci) {
            if (hci->howObj.t != VT::Object) {
                if (!howClsInfo_) { howClsInfo_ = std::make_shared<ClassInfo>(); howClsInfo_->name = "Metamodel::ClassHOW"; }
                Value h; h.t = VT::Object; h.setObj(std::make_shared<ObjectData>());
                h.obj()->cls = howClsInfo_;
                h.obj()->attrs["__type"] = Value::typeObj(hci->name);
                hci->howObj = std::move(h);
            }
            return hci->howObj;
        }
        return Value::typeObj("Metamodel::ClassHOW"); // metaclass (its own .HOW returns a HOW too)
    }
    if (m == "WHO") { // package stash — the PERSISTENT one, see pkgStashes_
        std::string pkg = inv.t == VT::Type ? inv.s : inv.typeName();
        auto& stash = pkgStashes_[pkg];
        if (!stash) stash = std::make_shared<ValueMap>();
        if (global_) { // `our`-scoped symbols live as qualified globals; show them
            std::string pre = pkg + "::";
            for (auto& kv : global_->vars)
                if (kv.first.rfind(pre, 0) == 0 &&
                    kv.first.find("::", pre.size()) == std::string::npos)
                    (*stash)[kv.first.substr(pre.size())] = kv.second;
        }
        // an ENUM type's stash holds its values (`Bool::.values` is (True, False))
        if (pkg == "Bool") {
            (*stash)["True"]  = Value::boolean(true);
            (*stash)["False"] = Value::boolean(false);
        }
        else if (Value* et = tctx_.cur->find(pkg)) {
            if (et->t == VT::Array && et->arr() && !et->enumType.empty() && et->enumName.empty())
                for (auto& pr : *et->arr())
                    if (pr.t == VT::Pair)
                        if (Value* ev = tctx_.cur->find(pr.s)) (*stash)[pr.s] = *ev;
        }
        Value st; st.t = VT::Hash; st.setHash(stash); st.hashKind = "Stash"; st.s = pkg;
        return st;
    }
    if (m == "WHICH") return Value::str(whichOf(inv)); // whichOf is the one home for identity
    if (m == "WHERE") { // memory address of the value (an Int)
        const void* p = inv.t == VT::Object && inv.obj() ? (const void*)inv.obj()
                      : inv.t == VT::Array && inv.arr()  ? (const void*)inv.arr()
                      : inv.t == VT::Hash && inv.hash()  ? (const void*)inv.hash()
                      : (const void*)&inv;
        return Value::integer((long long)(intptr_t)p);
    }
    if (m == "DUMP") return Value::str(inv.t == VT::Type ? inv.s : inv.gist()); // debug snapshot (loose form)
    if (m == "does") { // .does(Role/Type) — role/type membership introspection
        if (args.empty()) return Value::boolean(false);
        // HOW form: `$obj.HOW.does($obj, Role)` — the metaclass takes (object, role).
        // The metaclass may be the plain type object OR a persistent .HOW metaobject.
        bool howInv = (inv.t == VT::Type && inv.s.rfind("Metamodel::", 0) == 0);
        if (!howInv && inv.t == VT::Object && inv.obj() && inv.obj()->cls)
            for (ClassInfo* c = inv.obj()->cls.get(); c && !howInv; c = c->parent.get())
                if (c->name.rfind("Metamodel::", 0) == 0) howInv = true;
        if (howInv && args.size() >= 2)
            return methodCall(args[0], "does", ValueList{args[1]}, rwArgs);
        std::string rn = args[0].t == VT::Type ? args[0].s : args[0].typeName();
        if (rn == "Any" || rn == "Mu") return Value::boolean(true);
        // an Attribute meta-object answering the JSON/META attribute-role
        // checks (`$attr.does(META6::MetaAttribute)`) — same trait-key mapping
        // `~~` uses (typeMatchesArg owns it, reached via typeOrSubsetMatches)
        if (inv.t == VT::Hash && inv.hashKind == "Attribute")
            return Value::boolean(typeOrSubsetMatches(inv, rn));
        // A byte buffer does the buffer roles — Blob, buf8/blob8, Positional —
        // and none of them is a CLASS it is an instance of, in either engine.
        // `~~` already knew (typeMatchesArg owns that table); `.does` walked the
        // class ancestry instead and answered False for all of them. That is
        // what failed `isa-ok md5(Blob.new), buf8`: Test's isa-ok is
        // `.isa || .does`, and .isa is False here on Rakudo too.
        if (inv.t == VT::Str && (inv.hashKind == "Blob" || inv.hashKind == "Buf" ||
                                 inv.hashKind == "utf8" || inv.hashKind == "CArray"))
            return Value::boolean(typeOrSubsetMatches(inv, rn));
        bool res = inv.typeName() == rn;
        ClassInfo* ci = nullptr;
        if (inv.t == VT::Object && inv.obj()) ci = inv.obj()->cls.get();
        else if (inv.t == VT::Type) { auto it = classes_.find(inv.s); if (it != classes_.end()) ci = it->second.get(); }
        if (!res && ci) {
            for (ClassInfo* c = ci; c; c = c->parent.get()) if (c->name == rn) { res = true; break; }
            if (!res) res = ci->doesRole(rn);
            // a built-in parent's ancestry counts: G.does(Grammar) and
            // G.does(Match) are True for a grammar, F.does(Real) for `is Int`
            if (!res)
                for (ClassInfo* c = ci; c && !res; c = c->parent.get())
                    if (!c->nativeParent.empty()) {
                        if (c->nativeParent == rn) res = true;
                        else for (auto& a : typeAncestry(c->nativeParent))
                            if (a == rn) { res = true; break; }
                    }
        }
        // a BUILT-IN value does the roles its ancestry lists (`Date.does(Dateish)`)
        if (!res && !ci)
            for (auto& a : typeAncestry(typeOfVal(inv))) if (a == rn) { res = true; break; }
        // a Code value does the Callable/Code/Routine/Block roles
        if (!res && inv.t == VT::Code &&
            (rn == "Callable" || rn == "Code" || rn == "Routine" || rn == "Block" || rn == "Sub"))
            res = true;
        // native numeric type objects do Real/Numeric; native `str` does Stringy
        if (!res && inv.t == VT::Type) {
            static const std::set<std::string> natNum = {
                "int","int8","int16","int32","int64","uint","uint8","uint16","uint32","uint64",
                "byte","num","num32","num64"};
            if (natNum.count(inv.s) && (rn == "Real" || rn == "Numeric")) res = true;
            else if (inv.s == "str" && rn == "Stringy") res = true;
        }
        return Value::boolean(res);
    }
    // a DEFINITENESS-constrained type object reports its smiley, and
    // `.^base_type` is the same type without it
    if (m == "name" || m == "^name") {
        // the metaclass reports Rakudo's full name; HOW.name($obj) names the OBJECT's type
        if (inv.t == VT::Type && inv.s == "Metamodel::ClassHOW") {
            if (m == "name" && !args.empty()) return Value::str(args[0].typeName());
            return Value::str("Perl6::Metamodel::ClassHOW");
        }
        // plain .name is NOT a universal method: a user-class instance with no
        // name method/attr dies X::Method::NotFound like Rakudo ($.name typo)
        if (m == "^name" || !(inv.t == VT::Object && inv.obj() && inv.obj()->cls))
            return Value::str(inv.typeName());
    }

    // Set/Bag/Mix coercions and queries
    // the ROLE coercers name the immutable member of each family
    if (m == "Setty" || m == "Baggy" || m == "Mixy")
        return methodCall(inv, m == "Setty" ? "Set" : m == "Baggy" ? "Bag" : "Mix", args, rwArgs);
    // a Capture is already one
    if (m == "Capture" && inv.t == VT::Array && inv.hashKind == "Capture") return inv;
    if (m == "Set" || m == "SetHash" || m == "Bag" || m == "BagHash" || m == "Mix" || m == "MixHash") {
        if (inv.t == VT::Range && inv.rTo() >= 9000000000000000000LL)
            throwTyped("X::Cannot::Lazy", {{"what", m}}, "Cannot " + m + " a lazy list");
        // the coercer flattens one level, but only through a bare LIST:
        // (@a, %h).Bag takes @a's elements and %h's pairs. A real Array's
        // elements are itemized ([4,[5,6]].Bag keeps [5,6] whole — ».Bag nodal)
        ValueList items;
        bool oneLevel = inv.t == VT::Array && inv.isList;
        for (auto& x : toList(inv)) {
            if (!oneLevel) { items.push_back(x); continue; }
            if (x.t == VT::Array && x.arr() && !x.itemized) {
                for (auto& e : *x.arr()) items.push_back(e);
            }
            else if (x.t == VT::Hash && x.hash() && !x.itemized &&
                     (x.hashKind.empty() || x.hashKind == "Map" || quantValueType(x.hashKind))) {
                for (auto& kv : *x.hash()) {
                    Value p = Value::pair(kv.first, kv.second);
                    p.pairKeyM() = kv.second.pairKey();
                    items.push_back(p);
                }
            }
            else items.push_back(x);
        }
        return makeBaggy(items, m);
    }
    return std::nullopt;   // not handled here — fall through to the next segment
}

} // namespace rakupp
