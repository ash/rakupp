// InterpreterCalls.cpp — calls, method invocation, lvalues and the assignment helpers
//
// One of the parts InterpreterParts.h lists; what they share is declared there.
#include "InterpreterParts.h"

namespace rakupp {
bool argIsNeverContainer(const Expr* e) {
    if (!e) return false;
    switch (e->kind) {
        case NK::Ternary: {
            auto* t = static_cast<const Ternary*>(e);
            return argIsNeverContainer(t->then.get()) && argIsNeverContainer(t->els.get());
        }
        case NK::Binary: {
            auto* b = static_cast<const Binary*>(e);
            if (b->op == "||" || b->op == "&&" || b->op == "//" ||
                b->op == "or" || b->op == "and")
                return argIsNeverContainer(b->lhs.get()) && argIsNeverContainer(b->rhs.get());
            return true;
        }
        case NK::IntLit: case NK::NumLit: case NK::StrLit: case NK::BoolLit:
        case NK::InterpStr: case NK::AllomorphLit: case NK::ChainExpr: case NK::Range:
            return true;
        // an ADVERBED subscript answers values, never the element's container:
        // `@a[0]:exists`, `%h<k>:v`, `@a[0;1]:kv` bound to `\x` cannot be assigned
        case NK::Index:
            return !static_cast<const Index*>(e)->adverb.empty();
        default: return false;
    }
}

// An `is rw` parameter is part of the SIGNATURE, so a candidate that wants one
// does not match an argument that can never be a container: `H.new("")` must
// reach `multi new(Str $s)` and not die inside `multi new(Str $s is rw)`.
// Text::CSV's in-memory handle is declared with exactly that pair, and the
// throw left `Text::IO::String.new(…)` answering a plain FileHandle.
//
// Only the first dispatch of a call is judged: a callwith/nextwith replaces the
// argument VALUES, and the expressions no longer describe them.
// Definedness as the LANGUAGE asks it, which is not always the representation.
// `with`, `without`, `//` and `orelse` all go through `.defined`, and a class
// may declare its own: Array::Sorted::Util reports "not found" as an
// `Int`-derived object whose `method defined(--> False)` is the entire signal,
// and `without finds(@a, $x) { … }` is how a caller reads it.
//
// Only a DECLARED method counts. Asking every object would put a dispatch on
// the hot path of every `//` in every program, so the class table is consulted
// first and the call happens only for the rare class that overrides it.
// `DEFINITE` and `so` are NOT routed here — Rakudo keeps both on the
// representation, and the probe in t/regression/lizmat-nqp-ops.raku pins that.
bool Interpreter::topicDefined(const Value& v) {
    // A TYPE OBJECT can override `defined` as much as an instance can, and
    // `andthen`/`orelse`/`with` are exactly where that override is observable —
    // S03-operators/andthen.t counts the calls.
    if (v.t == VT::Type) {
        auto ci = classes_.find(v.s.str());
        if (ci != classes_.end() && ci->second)
            for (ClassInfo* c = ci->second.get(); c; c = c->parent.get())
                if (c->methods.count("defined")) {
                    ValueList noArgs;
                    try { return methodCall(v, "defined", noArgs).truthy(); }
                    catch (...) { break; }
                }
    }
    if (v.t == VT::Object && v.obj() && v.obj()->cls) {
        for (ClassInfo* c = v.obj()->cls.get(); c; c = c->parent.get()) {
            auto it = c->methods.find("defined");
            if (it != c->methods.end()) {
                ValueList noArgs;
                try { return methodCall(v, "defined", noArgs).truthy(); }
                catch (...) { break; }   // a throwing override falls back to the representation
            }
        }
    }
    // testing a Failure's definedness (with/without/andthen/orelse/`//`)
    // handles it, as `.defined` does
    if (v.t == VT::Hash && v.hashKind == "Failure" && v.hash()) {
        (*const_cast<Value&>(v).hash())["handled"] = Value::boolean(true);
        return false;
    }
    return rtIsDefined(v);
}

bool Interpreter::methodMayYieldContainer(const std::string& name) {
    // the engine's own container accessors
    if (name == "AT-POS" || name == "AT-KEY" || name == "VAR" ||
        name == "BIND-POS" || name == "BIND-KEY" || name == "nl-in" || name == "nl-out")
        return true;
    for (auto& kv : classes_) {
        if (!kv.second) continue;
        for (auto& a : kv.second->attrs)
            if (a.pub && a.rw && a.name == name) return true;   // `has $.v is rw`
        auto mit = kv.second->methods.find(name);
        if (mit != kv.second->methods.end() && mit->second.code() && mit->second.code()->retRw)
            return true;                                        // `method m() is rw`
    }
    return false;
}

bool Interpreter::rwCandidateBinds(const Value& cand, const std::vector<ExprPtr>* rwArgs) {
    if (!rwArgs || !cand.code() || !cand.code()->params) return false;
    size_t pi = 0;
    for (auto& p : *cand.code()->params) {
        if (p.invocant) continue;
        if (p.slurpy) break;
        if (p.named) continue;
        if (pi >= rwArgs->size()) break;
        const Expr* ae = (*rwArgs)[pi].get();
        if (!ae || ae->kind == NK::Pair || (ae->kind == NK::Unary && static_cast<const Unary*>(ae)->op == "|")) break;
        if (p.isRw && ae->kind == NK::VarExpr) return true;
        pi++;
    }
    return false;
}

bool Interpreter::rwCandidateRejects(const Value& cand, size_t nargs,
                                     const std::vector<ExprPtr>* rwArgs, const ValueList* vals) {
    (void)nargs;
    if (!rwArgs || !cand.code() || !cand.code()->params) return false;
    size_t pi = 0;
    for (auto& p : *cand.code()->params) {
        if (p.invocant) continue;
        if (p.slurpy) break;      // mirrors setupRwLinks' positional indexing
        if (p.named) continue;
        if (pi >= rwArgs->size()) break;
        // Positional index and expression index agree only up to the first
        // argument that is not a plain positional: `|%init` flattens and
        // `:k(v)` is named, and after either one the two stop lining up.
        // (`self.new($s.Str, |%init)` is exactly that call.)
        if (const Expr* a0 = (*rwArgs)[pi].get()) {
            if (a0->kind == NK::Pair) break;
            if (a0->kind == NK::Unary && static_cast<const Unary*>(a0)->op == "|") break;
        }
        Expr* ae = (*rwArgs)[pi].get();
        // …and a METHOD CALL whose name cannot name a container is a value too.
        // Rakudo decides this on the thing actually passed: `f($obj.rwattr)`
        // reaches an `is rw` candidate and `f($str.Str)` does not.
        bool neverCont = argIsNeverContainer(ae) ||
                         (ae && ae->kind == NK::MethodCall &&
                          !static_cast<MethodCall*>(ae)->meta &&
                          !static_cast<MethodCall*>(ae)->methodExpr &&
                          !static_cast<MethodCall*>(ae)->hyper &&
                          !methodMayYieldContainer(static_cast<MethodCall*>(ae)->method));
        if (p.isRw && neverCont) return true;
        // a NATIVE `is rw` parameter binds only a native container: `int $rw is rw`
        // does not take an Int variable (it could not write back through it)
        if (p.isRw && vals && pi < vals->size() && !p.type.empty() && p.type != "str" &&
            ascii::islower((unsigned char)p.type[0]) && isNativeScalarName(p.type) &&
            !(*vals)[pi].natBits && !(*vals)[pi].natFloat)
            return true;
        pi++;
    }
    return false;
}

// A raw/rw SLURPY's elements, re-made as the arguments' containers: a `$x`
// argument gives $x's cell, an `@a` argument its elements' slots, in order.
// Only when every remaining argument is one of those (or a plain value that
// stays one element): anything that could flatten to an unknown count leaves
// the slurpy holding values, as before.
void Interpreter::bindSlurpyContainers(const Param& p, std::shared_ptr<Env>& env,
                                       const std::vector<ExprPtr>* rwArgs, size_t from) {
    Value* sv = env->local(p.name);
    if (!sv || sv->t != VT::Array || !sv->arr()) return;
    ValueList els = *sv->arr();
    size_t k = 0;
    bool any = false;
    for (size_t i = from; i < rwArgs->size(); i++) {
        const Expr* ae = (*rwArgs)[i].get();
        if (!ae) return;
        if (ae->kind == NK::Pair) continue;                     // a named argument
        if (ae->kind == NK::Unary && static_cast<const Unary*>(ae)->op == "|") return;
        if (ae->kind == NK::VarExpr) {
            const std::string& n = static_cast<const VarExpr*>(ae)->name;
            if (n.size() > 1 && n[0] == '@') {
                Value* av = tctx_.cur->find(n);
                if (!av || av->t != VT::Array || !av->arr() || av->isList || av->ext()) return;
                auto arr = av->arrS();
                for (size_t j = 0; j < arr->size(); j++, k++) {
                    if (k >= els.size()) return;
                    els[k] = makeArraySlotProxy(arr, j);
                }
                any = any || !arr->empty();
                continue;
            }
            if (n.size() > 1 && n[0] == '$') {
                if (k >= els.size()) return;
                Value c;
                if (containerElemFor(ae, c)) { els[k] = std::move(c); any = true; }
                k++;
                continue;
            }
            return;                                             // `%h` and the rest spread
        }
        if (ae->kind == NK::IntLit || ae->kind == NK::StrLit || ae->kind == NK::NumLit ||
            ae->kind == NK::InterpStr || ae->kind == NK::Index) { k++; continue; }
        return;                                                 // a call, a range: unknown count
    }
    if (!any || k != els.size()) return;
    *sv->arr() = std::move(els);
    sv->markHoldsContainers();
}

// Bind hyper element slots: like setupRwLinks but the caller supplies container
// slots DIRECTLY (positional, aligned with the params); a null slot means the
// argument was immutable — assigning that param dies.
void Interpreter::setupRwSlots(const std::vector<Param>* params, std::shared_ptr<Env>& env,
                               const std::vector<Value*>* slots) {
    if (!params || !slots) return;
    size_t pi = 0;
    for (auto& p : *params) {
        if (p.named) continue;
        if (p.invocant) continue;  // an explicit `C:U:` invocant consumes no arg slot
        if (p.slurpy) break;
        if ((p.isRw || p.isRaw || p.sigil == '\\') && pi < slots->size()) {
            if (Value* s = (*slots)[pi]) {
                env->x().rwDirect[p.name] = s;
                Value* ip = env->local(p.name);
                env->x().rwSynced[p.name] = ip ? *ip : Value::any();
            }
            else env->x().rwDead.insert(p.name);
            anyRwLinks_ = true;
        }
        pi++;
    }
}

// After a mutation through a variable target, push the new value through the
// caller's argument expression if the variable is a linked rw/raw param.
// `\target` bound to `@a[*;0;0]` and then assigned: the value is spread over
// the SLICE's slots, as `@a[*;0;0] = …` itself spreads it, rather than stored
// whole in the first one. True when `target` was such a slice and is done.
bool Interpreter::assignMultiDimSlice(Expr* target, const Value& rhs) {
    if (!target || target->kind != NK::Index) return false;
    auto* ix = static_cast<Index*>(target);
    if (!ix->multiDim || !ix->adverb.empty() || !ix->index || ix->index->kind != NK::ListExpr)
        return false;
    auto* dims = static_cast<ListExpr*>(ix->index.get());
    ValueList keys; bool anyMulti = false;
    for (auto& de : dims->items) {
        if (de->kind == NK::Whatever) { anyMulti = true; keys.push_back(Value::whatever()); continue; }
        Value k = eval(de.get());
        if (k.t == VT::Array || k.t == VT::Range || k.t == VT::Whatever || isSliceBlock(k)) anyMulti = true;
        keys.push_back(k);
    }
    if (!anyMulti) return false;
    Value* root = lvalue(ix->base.get(), /*asInvocant=*/true);
    if (!root) return false;
    std::vector<ValueList> tuples = expandDimTuples(*root, keys);
    ValueList vs = (rhs.t == VT::Array || rhs.t == VT::Range) ? rhs.flatten() : ValueList{rhs};
    size_t vi = 0;
    for (auto& tup : tuples) {
        Value* node = root;
        for (size_t d2 = 0; d2 < tup.size(); d2++) {
            if (node->t == VT::Hash || (ix->isHash && d2 + 1 == tup.size() && node->t != VT::Array)) {
                if (node->t != VT::Hash) *node = Value::makeHash();
                node = &(*node->hash())[tup[d2].toStr()];
            } else {
                if (node->t != VT::Array) *node = Value::array();
                long long i = tup[d2].toInt();
                if (i < 0) negIndexThrow(i);
                while ((long long)node->arr()->size() <= i) node->arr()->push_back(Value::any());
                node = &(*node->arr())[i];
            }
        }
        *node = vi < vs.size() ? vs[vi] : Value::any();
        vi++;
    }
    return true;
}

void Interpreter::rwWriteThrough(Expr* target) {
    if (!target) return;
    std::string name;
    if (target->kind == NK::VarExpr) name = static_cast<VarExpr*>(target)->name;
    else if (target->kind == NK::NameTerm) name = static_cast<NameTerm*>(target)->name;
    else return;
    Env* e = tctx_.cur.get();
    while (e && !e->local(name)) e = e->parent.get();
    if (!e) return;
    if (e->ex && e->ex->rwDead.count(name))
        throw RakuError{Value::typeObj("X::Assignment::RO"),
                        "Cannot modify an immutable value"};
    auto dit = e->xr().rwDirect.find(name);
    if (dit != e->xr().rwDirect.end()) {
        Value v = *e->local(name);
        *dit->second = v;
        e->x().rwSynced[name] = v;
        return;
    }
    if (!e->ex || e->ex->rwLinks.empty()) return;
    auto it = e->ex->rwLinks.find(name);
    if (it == e->ex->rwLinks.end()) return;
    Value v = *e->local(name);
    auto savedCur = tctx_.cur;
    tctx_.cur = it->second.second; // the caller's scope, where the arg expr lives
    // ONE hop only: write through to the caller's slot. Chaining further up on
    // every write is quadratic where an `is rw` cursor is threaded through a
    // recursion — JSON::Fast's `int $pos is rw` down parse-obj/array/value made
    // deep input O(n^2) — and it is redundant while the frames are live, since
    // each frame's return-time copyOutRw carries the write on up. (The
    // after-return closure case — IO::Capture::Simple's captured `$*OUT` writing
    // two rw hops away once the frames have gone — is NOT served by this path;
    // it needs the container model, big-area #2, and stays open.)
    try {
        Expr* tgt = peelIncDec(it->second.first);
        if (!assignMultiDimSlice(tgt, v))
            if (Value* lv = lvalue(tgt)) *lv = v;
    } catch (...) {}
    tctx_.cur = savedCur;
    // …and the ORIGINAL container behind the chain, when the hop above is not
    // it. While the frames are live the hop suffices — the intermediate frame
    // reads a fresh value, and its return-time copyOutRw carries the write on —
    // but a closure that writes AFTER they returned has only this: IO::Capture::
    // Simple's captured `$*OUT` prints into a parameter two `is rw` hops away
    // long after capture_on and capture_stdout_on are gone. One more write, not
    // a chain walk: setupRwLinks resolved the root once, at bind time.
    auto rt = e->ex->rwRoots.find(name);
    if (rt != e->ex->rwRoots.end()) {
        tctx_.cur = rt->second.second;
        try { if (Value* lv = lvalue(peelIncDec(rt->second.first))) *lv = v; } catch (...) {}
        tctx_.cur = savedCur;
    }
    e->x().rwSynced[name] = v;
}
// The C3 linearization of a class's CLASS ancestors (composed roles are not
// ancestors): the class, then the merge of its parents' linearizations and the
// parent list itself. `ok` turns false when no consistent order exists
// (`class confused is vh is hv` over `hv is h is v` / `vh is v is h`).
std::vector<ClassInfo*> c3Linearize(ClassInfo* c, bool& ok) {
    ok = true;
    std::vector<ClassInfo*> out;
    if (!c) return out;
    std::vector<ClassInfo*> parents;
    if (c->parent && !c->parent->isRole) parents.push_back(c->parent.get());
    for (auto& p : c->extraParents) if (p && !p->isRole) parents.push_back(p.get());
    std::vector<std::vector<ClassInfo*>> seqs;
    for (auto* p : parents) {
        bool pok = true;
        seqs.push_back(c3Linearize(p, pok));
        if (!pok) ok = false;
    }
    seqs.push_back(parents);
    out.push_back(c);
    for (;;) {
        bool any = false;
        for (auto& sq : seqs) if (!sq.empty()) { any = true; break; }
        if (!any) break;
        ClassInfo* pick = nullptr;
        for (auto& sq : seqs) {
            if (sq.empty()) continue;
            ClassInfo* cand = sq.front();
            bool inTail = false;
            for (auto& other : seqs)
                for (size_t i = 1; i < other.size() && !inTail; i++) if (other[i] == cand) inTail = true;
            if (!inTail) { pick = cand; break; }
        }
        if (!pick) { ok = false; break; }
        out.push_back(pick);
        for (auto& sq : seqs) if (!sq.empty() && sq.front() == pick) sq.erase(sq.begin());
    }
    return out;
}

Value Interpreter::evalInterp(InterpStr* s) {
    // Evaluate the parts first: a JUNCTION part autothreads the WHOLE string —
    // `my $j = 1|2; "v=$j"` is any("v=1", "v=2") in Rakudo, not "v=12".
    // Interpolation is concatenation, and infix ~ already autothreads; gluing the
    // eigenstates together instead silently produced text no engine would print.
    ValueList vals;
    vals.reserve(s->parts.size());
    bool anyJ = false;
    for (auto& p : s->parts) {
        vals.push_back(eval(p.get()));
        anyJ = anyJ || isJunction(vals.back());
        // a binary buffer has no string form: `"Foo: $buf"` is X::Buf::AsStr
        const Value& bv = vals.back();
        if (bv.t == VT::Str && (bv.hashKind == "Buf" || bv.hashKind == "Blob") &&
            bv.enumName != "utf8" && bv.enumName != "utf16" && bv.enumName != "utf32")
            throwTyped("X::Buf::AsStr", {{"method", "Str"}},
                       "Cannot use a " + std::string(bv.hashKind.c_str()) + " as a string, but you called the Str method on it");
    }
    if (anyJ) {
        // Expand one junction per recursion, substituting each eigenstate back in.
        // The KIND picks the order: all/none thread OUTSIDE any/one, which is how
        // the general infix autothreader nests `(1|2) + (3&4)` and matches Rakudo.
        // (Rakudo's `~` with two MIXED kinds nests differently again — an exotic
        // grouping this deliberately does not chase.)
        std::function<Value(ValueList&)> expand = [&](ValueList& parts) -> Value {
            size_t idx = parts.size(); bool idxTight = false;
            for (size_t k = 0; k < parts.size(); k++) {
                if (!isJunction(parts[k])) continue;
                bool tight = parts[k].enumName == "all" || parts[k].enumName == "none";
                if (idx == parts.size() || (tight && !idxTight)) { idx = k; idxTight = tight; }
                if (idxTight) break; // leftmost all/none wins outright
            }
            if (idx == parts.size()) {
                std::string acc;
                for (auto& v : parts) acc += strInStrContext(v);
                return Value::str(nfcNormalize(acc));
            }
            Value out = Value::array(); out.isList = true; out.enumName = parts[idx].enumName;
            Value jv = parts[idx]; // keep the junction alive while we substitute over it
            for (auto& eig : *jv.arr()) {
                parts[idx] = eig;  // an eigenstate may itself be a junction — recursion finds it
                Value sub = expand(parts);
                // Rakudo FLATTENS same-kind nesting in interpolation ("$a$b" with
                // two any-junctions is any(13, 14, 23, 24)) — though not for
                // arithmetic infixes, where it keeps any(any(…)) and we match that
                // separately. Splice same-kind children in.
                if (isJunction(sub) && sub.enumName == out.enumName)
                    for (auto& e2 : *sub.arr()) out.arr()->push_back(e2);
                else out.arr()->push_back(sub);
            }
            parts[idx] = jv;
            return out;
        };
        ValueList parts(vals.begin(), vals.end());
        return expand(parts);
    }
    std::string out;
    // interpolation is a Str:D context too: "[$m]" with $m a `Str but R` is the
    // VALUE, not the role's .Str (an explicit $m.Str still dispatches)
    for (auto& v : vals) out += strInStrContext(v);
    return Value::str(nfcNormalize(out)); // NFG: combining marks compose across part boundaries
}

// A `return-rw` hands a container OUT of the routine, so a parameter that is
// rw-LINKED to the caller — `is rw`, `is raw`, or a sigilless `\c` — has to
// resolve to the CALLER's slot rather than this frame's copy. The frame's
// copy-out runs AT RETURN, before the caller's assignment ever happens, so a
// write to the local slot went nowhere: `sub f(\c) is rw { return-rw c }; my $a
// = 0; f($a) = 1` left $a at 0 while `c = 5` INSIDE f wrote through fine.
// Only this path resolves that way — inside the routine `lvalue()` must keep
// answering the local slot, or a read after a write would see a stale value.
// The link carries the caller's argument EXPRESSION and scope, which is the
// same pair the return-time write-back re-evaluates.
Value* Interpreter::lvalueThroughRw(Expr* e) {
    // Only the OUTERMOST entry clears the mirror list — the walk below calls
    // itself to follow the chain, and a nested clear would drop the hops the
    // outer call had already recorded.
    struct DepthG { int& d; ~DepthG() { --d; } };
    if (rwThroughDepth_ == 0) {
        tctx_.rwMirror.clear(); tctx_.rwMirrorSigil = 0; tctx_.lvalueOutLocal = false;
        tctx_.lvalueOutCell.reset();
    }
    ++rwThroughDepth_;
    DepthG dg{rwThroughDepth_};
    // Only the outermost call is looking at the frame that is about to be
    // popped. The walk below re-enters with tctx_.cur set to a CALLER's scope,
    // and those frames stay live — a variable of theirs is the destination of
    // the write, not a dangling local.
    const bool ownFrame = rwThroughDepth_ == 1;
    if (e && (e->kind == NK::NameTerm || e->kind == NK::VarExpr)) {
        const std::string& nm = e->kind == NK::NameTerm
                              ? static_cast<NameTerm*>(e)->name
                              : static_cast<VarExpr*>(e)->name;
        for (Env* en = tctx_.cur.get(); en; en = en->parent.get()) {
            if (!en->local(nm)) continue;          // not this scope's variable
            if (!en->ex) break;
            auto it = en->ex->rwLinks.find(nm);
            if (it == en->ex->rwLinks.end()) break; // declared here, not rw-linked
            auto saved = tctx_.cur;
            tctx_.cur = it->second.second;          // the caller's scope
            Value* out = nullptr;
            // …and TRANSITIVELY: `h(\c) { return-rw g(c) }` links h's `c` to the
            // caller's argument, which may itself be a linked parameter one
            // frame further up. The chain is bounded by the live frames, and
            // unlike the return-time write-back — which walks one hop on
            // purpose, because chaining there is quadratic under a recursion
            // threading an `is rw` cursor — this runs once per `return-rw`.
            try { out = lvalueThroughRw(it->second.first); } catch (...) {}
            tctx_.cur = saved;
            if (out) {
                // the sigil of the CALLER's argument decides whether the slot is
                // a container: `Crane.set(%i, …)` must leave %i a Hash
                Expr* ce = it->second.first;
                if (ce && ce->kind == NK::VarExpr) {
                    const std::string& cn = static_cast<VarExpr*>(ce)->name;
                    if (!cn.empty() && (cn[0] == '%' || cn[0] == '@'))
                        tctx_.rwMirrorSigil = cn[0];
                }
                // The write now goes straight to the caller's container, which
                // leaves THIS frame's copy of the parameter behind: Crane's
                // `set` does `Crane::In.in(container, @path) = $value; container`
                // and returned the pre-assignment `{}`. Mirror the write into
                // the copies it travelled past — they are still live, and their
                // own return-time write-back would otherwise carry the stale
                // value back down over it.
                tctx_.rwMirror.push_back(en->local(nm));
                return out;
            }
            break;                                  // the caller's arg is no lvalue
        }
        // A local BOUND TO AN ELEMENT holds a Proxy, and its slot dies with the
        // frame the moment `return-rw` returns — so the pointer must not leave.
        // Crane's `at` is exactly that shape (`my $root := $container; $root :=
        // $root{$step}; return-rw $root`), and the caller wrote through the
        // dangling pointer into reused memory, then crashed reading the hash
        // back out of it. Flag it so the caller copies the VALUE instead: a
        // Proxy carries its own FETCH/STORE closures, so writing to the copy
        // still reaches the real container. Only a Proxy is treated this way —
        // every other slot keeps handing back its pointer, which is what an
        // outer lexical and an attribute need.
        // …and so does ANY local of this routine's OWN scope, Proxy or not. The
        // slot is in the frame that is about to be popped, so handing its
        // address out is a dangling write: `method AT-POS($p) is rw { my $val :=
        // callsame; $val }` — how PDF::COS::Tie::Array reads every element —
        // gave `$cs[0] = 'Lab'` a pointer into the dying frame, and the array
        // stayed empty. An outer lexical, an attribute and an rw-LINKED
        // parameter all still hand back their real pointer; only this call's own
        // declarations are copied out.
        // …and only for THIS routine's own frame: `sub f(\c) is rw { return-rw c }`
        // resolves `c` to the caller's `my $a`, and the recursive step above
        // arrives here with the caller's scope current. Copying THAT out sent
        // `f($a) = 1` to a temporary and left $a alone.
        bool ownScope = ownFrame;
        for (Env* en = tctx_.cur.get(); en; en = en->parent.get()) {
            Value* slot = en->local(nm);
            if (!slot) {
                if (en->routineFrame) ownScope = false;   // past this call's scopes
                continue;
            }
            const bool linked = en->ex && en->ex->rwLinks.count(nm);
            // a CELL outlives the frame for as long as something holds it —
            // an `is rw` parameter's is the caller's own variable — so its
            // address may leave; the hold covers a cell only this frame had
            if (!linked) {
                Value* raw = en->localRaw(nm);
                if (raw && raw->isCell()) { tctx_.lvalueOutCell = raw->cellS(); break; }
            }
            if (!linked &&
                (ownScope || (slot->t == VT::Hash && slot->hashKind == "Proxy")))
                tctx_.lvalueOutLocal = true;
            break;
        }
    }
    return lvalue(e);
}

// `$p.VAR.attr` where the container is a stamped Proxy SUBCLASS instance
// (`class History is Proxy { has @.history }`): the attribute's slot among
// the proxy's prefixed keys, and its sigil. Null for anything else. The
// variable is read raw — no FETCH runs to find its container.
Value* Interpreter::proxyAttrSlot(MethodCall* mc, char* sigilOut) {
    if (!mc || mc->meta || mc->hyper || mc->bang || mc->methodExpr || !mc->args.empty() || !mc->inv ||
        mc->inv->kind != NK::MethodCall) return nullptr;
    auto* vm = static_cast<MethodCall*>(mc->inv.get());
    if (vm->method != "VAR" || !vm->args.empty() || !vm->inv || vm->inv->kind != NK::VarExpr) return nullptr;
    Value* raw = tctx_.cur ? tctx_.cur->find(static_cast<VarExpr*>(vm->inv.get())->name) : nullptr;
    if (!raw || raw->t != VT::Hash || raw->hashKind != "Proxy" || !raw->hash()) return nullptr;
    auto ki = raw->hash()->find("\x01cls");
    if (ki == raw->hash()->end()) return nullptr;
    auto cit = classes_.find(ki->second.s);
    if (cit == classes_.end() || !cit->second) return nullptr;
    for (ClassInfo* ci = cit->second.get(); ci; ci = ci->parent.get())
        for (auto& at : ci->attrs)
            if (at.pub && at.name == mc->method) {
                if (sigilOut) *sigilOut = at.sigil;
                return &(*raw->hash())[std::string("\x01" "a") + at.sigil + "!" + at.name];
            }
    return nullptr;
}

// Does this assignment target name an `@` container — `@a`, an `@.a`
// attribute through its accessor (attrSigil, from the lvalue just taken), or
// an assignment whose own container is one? Such a container LIST-assigns
// what an OP= computes: `(@a ||= 42) += 10` leaves [11], not 11.
bool Interpreter::exprIsArrayContainer(Expr* e, char attrSigil) {
    while (e && e->kind == NK::Assign) {
        auto* a = static_cast<Assign*>(e);
        const std::string& op = a->op;
        const bool rOp = op.size() > 1 && op[0] == 'R' && op.back() == '=' &&
                         (op == "R=" || !ascii::isalnum((unsigned char)op[1]));
        e = rOp ? a->value.get() : a->target.get();
    }
    if (!e) return false;
    if (e->kind == NK::VarExpr) {
        const std::string& n = static_cast<VarExpr*>(e)->name;
        return !n.empty() && n[0] == '@';
    }
    return e->kind == NK::MethodCall && attrSigil == '@';
}

Value applyArith(const std::string& op, const Value& l, const Value& r);

// A `rx//` (or a bare `/…/` in value context) is a CLOSURE over the scope that
// built it: `sub mk($a) { rx/$a/ }` still matches $a's value after mk returns,
// because Rakudo re-reads the variable at match time from the regex's OWN
// lexical scope. rakupp interpolates at match time too, but against whatever
// scope is current then — so the variable was simply gone. Carry the creating
// scope on the value (only when the pattern actually names one, so ordinary
// regexes retain nothing).
// A retired Perl 5 metachar is a COMPILE-time error: Rakudo throws X::Obsolete
// when the regex is CONSTRUCTED, not when it is first matched. rakupp compiles
// lazily, which was invisible while a bare regex literal always matched — now
// that it evaluates to a Regex, `throws-like '/\Aabc/'` sees the silence.
// Only a pattern with NO interpolation is checked: its text is final, where an
// interpolating one is not known until the match. Memoised per pattern text, so
// a literal in a loop compiles for this check at most once.
static void rejectObsoleteRegex(const std::string& pat) {
    static std::mutex m;
    static std::map<std::string, std::string> seen;
    std::string obs;
    {
        std::lock_guard<std::mutex> lk(m);
        auto it = seen.find(pat);
        if (it != seen.end()) obs = it->second;
        else {
            rakupp::Regex rx(pat);
            obs = rx.ok() ? std::string() : rx.obsolete();
            seen.emplace(pat, obs);
        }
    }
    if (!obs.empty())
        throw RakuError{Value::typeObj("X::Obsolete"),
            "Unsupported use of " + obs + "; this Perl 5 metacharacter is gone in Raku"};
}
static Value regexClosingOver(std::string pat, const std::shared_ptr<Env>& sc) {
    Value v = Value::regex(std::move(pat));
    if (sc && (v.s.find('$') != std::string::npos || v.s.find('@') != std::string::npos))
        v.extM() = std::static_pointer_cast<void>(sc);
    else rejectObsoleteRegex(v.s.str());
    return v;
}

// A bare regex literal as a VALUE, through the node's closed-pattern cache: a
// literal whose source has no '$'/'@' splices to itself and closes over
// nothing, so after the first evaluation the final pattern (obsolete-check
// passed) is published on the node and every later evaluation skips the
// splice copy, the two scans, and rejectObsoleteRegex's mutex+map probe —
// `if /\d/` in a loop paid all of that per iteration (REVIEW-3.7 batch 3).
Value Interpreter::regexLitValue(RegexLit* rl) {
    if (const void* p = rl->closedPat.get())
        return Value::regex(*static_cast<const std::string*>(p));
    Value v = regexClosingOver(spliceRegexVars(rl->pattern), tctx_.cur);
    if (!v.ext() && rl->pattern.find('$') == std::string::npos &&
        rl->pattern.find('@') == std::string::npos) {
        auto* mine = new std::string(v.s.str());
        if (rl->closedPat.publish(mine) != mine) delete mine; // another thread won
    }
    return v;
}

// In value context (assignment RHS, colon-pair value), a bare `/pat/` is a Regex
// OBJECT, not an immediate match against $_ — so `:err(/pat/)` and `my $rx = /pat/`
// store a Regex that can be smartmatched later.
Value Interpreter::evalValueOf(Expr* e) {
    // …but an explicit `m//` matches even here: `my $m = m/b/` is the Match.
    if (e && e->kind == NK::RegexLit && !static_cast<RegexLit*>(e)->isM)
        return regexLitValue(static_cast<RegexLit*>(e));
    return eval(e);
}

Value Interpreter::iterationSourceOf(Value v) {
    // a Junction is ONE thing to iterate: `for any 1 { … }` sees the Junction
    if (isJunction(v)) { Value out = Value::array({v}); out.isList = true; return out; }
    // `for @a[3;2]` walks the six leaves, not the three rows.
    if (isMultiDimShaped(v)) { Value out = Value::array(shapedLeaves(v)); out.isList = true; return out; }
    if (v.t != VT::Object || !v.obj() || !v.obj()->cls) return v;
    if (v.obj()->cls->findMethod("pull-one")) return v;      // already an Iterator
    Value* itm = v.obj()->cls->findMethod("iterator");
    // a class built on an Array or a Hash (`class A is Array`) iterates the
    // container it boxes: `for self { … }` in its method walks the elements
    if (!itm && v.obj()->hasBoxed && !v.itemized &&
        ((v.obj()->boxed.t == VT::Array && v.obj()->boxed.arr() && v.obj()->boxed.enumType.empty()) ||
         (v.obj()->boxed.t == VT::Hash && v.obj()->boxed.hash() && v.obj()->boxed.hashKind.empty())))
        return v.obj()->boxed;
    if (!itm) return v;
    ValueList none;
    Value it = invokeMethod(*itm, v, none);
    if (it.t == VT::Object && it.obj() && it.obj()->cls && it.obj()->cls->findMethod("pull-one"))
        return it;
    if (it.t == VT::Hash && it.hashKind == "Iterator" && it.hash()) {
        auto items = it.hash()->find("items");
        if (items != it.hash()->end() && items->second.t == VT::Array && items->second.arr()) {
            long long pos = 0;
            auto p = it.hash()->find("pos");
            if (p != it.hash()->end()) pos = p->second.toInt();
            // An iterator over a sequence that is not produced yet — a gather,
            // `self.dir.iterator` in IO::Glob — has no items in its buffer until
            // something pulls: an untouched one is walked as the Seq it is, on
            // demand; a started one is produced first
            if (items->second.ext()) {
                if (pos <= 0) return items->second;
                forceLazy(items->second);
            }
            Value out = Value::array(); out.isList = true;
            for (size_t k = (size_t)std::max(0LL, pos); k < items->second.arr()->size(); k++)
                out.arr()->push_back((*items->second.arr())[k]);
            return out;
        }
    }
    return it.t == VT::Array || it.t == VT::Range ? it : v;
}

// The values an object contributes when it is assigned to a `%` container —
// see g_objListItems. `.list` wins over `.iterator` (that is the order Rakudo
// reaches them in), and an attribute's `handles` counts as supplying either:
// delegation is how a wrapper class says "my contents are that attribute's".
// `handles *` deliberately does NOT count — it is a fallback for methods that do
// not otherwise exist, and every object already has a `.list`.
// `for values %h { … }` iterates the hash's values AS CONTAINERS: writing to the
// topic writes into the hash (Color clips its channels with
// `clip-to 0, $_, 255 for values %r`, an `is rw` sub that assigns to $_).
// Returns the hash's storage when the loop's source is exactly that shape —
// `values %h` or `%h.values` over a %-variable — and null otherwise.
// Rakudo's `.grep` hands back the SAME containers it was given (`is raw`), so
// `for %h.values.grep(*.starts-with('@')) { $_ = … }` writes into the hash. The
// alias recognisers below only know the bare sources, so peel one `.grep` off
// first and let the loop skip the elements the predicate rejects.
Expr* Interpreter::peelGrepFilter(Expr* listExpr, Expr*& pred) {
    pred = nullptr;
    if (!listExpr || listExpr->kind != NK::MethodCall) return listExpr;
    auto* mc = static_cast<MethodCall*>(listExpr);
    if (mc->method != "grep" || mc->args.size() != 1 ||
        mc->meta || mc->hyper || mc->maybe || mc->methodExpr) return listExpr;
    pred = mc->args[0].get();
    return mc->inv.get();
}

bool Interpreter::grepFilterKeeps(Expr* pred, const Value& v) {
    if (!pred) return true;
    auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
    env->define("$_", v);
    auto saved = tctx_.cur; tctx_.cur = env;
    Value pv;
    try { pv = eval(pred); } catch (...) { tctx_.cur = saved; throw; }
    tctx_.cur = saved;
    return matcherAccepts(*this, v, pv);
}

// `for %h.pairs -> $p { $p.value += 100 }` — a Pair's VALUE is the hash's own
// container, so writing through it lands in the hash (Rakudo). Ours builds
// fresh Pairs, so the loop over this shape writes each one's value back. The
// hash itself is answered (not its map) so the Pair keeps an object key.
Value* Interpreter::pairsAliasSource(Expr* listExpr) {
    if (!listExpr || listExpr->kind != NK::MethodCall) return nullptr;
    auto* mc = static_cast<MethodCall*>(listExpr);
    if (mc->method != "pairs" || !mc->args.empty() || mc->meta || mc->hyper || mc->maybe || mc->methodExpr)
        return nullptr;
    if (!mc->inv || mc->inv->kind != NK::VarExpr) return nullptr;
    auto* ve = static_cast<VarExpr*>(mc->inv.get());
    if (ve->name.empty() || (ve->name[0] != '%' && ve->name[0] != '$' && ve->name[0] != '@') || ve->declare)
        return nullptr;
    Value* hv = tctx_.cur->find(ve->name);
    if (!hv) return nullptr;
    // …and `@a.pairs` over a real Array, whose elements are containers too
    if (ve->name[0] != '%' && hv->t == VT::Array && hv->arr() && !hv->isList && !hv->ext() &&
        !isJunction(*hv))
        return hv;
    if (ve->name[0] == '@' || hv->t != VT::Hash || !hv->hash() || !hv->hashKind.empty()) return nullptr;
    return hv;
}

PRef<ValueMap> Interpreter::valuesAliasSource(Expr* listExpr) {
    if (!listExpr) return nullptr;
    Expr* hashArg = nullptr;
    if (listExpr->kind == NK::Call) {
        auto* c = static_cast<Call*>(listExpr);
        if (c->name == "values" && c->args.size() == 1) hashArg = c->args[0].get();
    }
    else if (listExpr->kind == NK::MethodCall) {
        auto* mc = static_cast<MethodCall*>(listExpr);
        if (mc->method == "values" && mc->args.empty() && !mc->meta && !mc->hyper)
            hashArg = mc->inv.get();
    }
    if (!hashArg || hashArg->kind != NK::VarExpr) return nullptr;
    auto* ve = static_cast<VarExpr*>(hashArg);
    // `%h.values` — and equally `.values` on a $-scalar (usually the topic) that
    // HOLDS a hash: `with %p<a><b> { for .values { $_ = … } }` writes into the
    // very same storage, because a Hash Value shares its map.
    if (ve->name.empty() || (ve->name[0] != '%' && ve->name[0] != '$')) return nullptr;
    Value* hv = tctx_.cur->find(ve->name);
    if (!hv || hv->t != VT::Hash || !hv->hash() || !hv->hashKind.empty()) return nullptr;
    return hv->hashS();
}

// Does this loop source yield anything a write could land in? Rakudo's Array
// elements are containers, so almost any view of one aliases and a write to
// the topic reaches the array. These are the shapes that yield BARE VALUES,
// where Rakudo refuses the write outright with "Cannot assign to an immutable
// value" — and where ours, having nowhere to put it either, used to take it
// and drop it.
//
// A WHITELIST, deliberately. The honest test is "did this come from a
// container", which is the container/binding refactor; until then, marking a
// source immutable because we merely FAILED to find its alias would turn
// working programs into crashes — `for @a.grep(…) { $_ = 9 }` and
// `for f() { $_ = 9 }` (a sub returning `@g`) both alias under Rakudo and
// neither aliases here yet. So every rule below was checked to refuse only
// what Rakudo also refuses, and anything unrecognised stays as it was.
static bool literalOnlySource(const Expr* e) {
    if (!e) return false;
    switch (e->kind) {
        case NK::IntLit: case NK::NumLit: case NK::StrLit: case NK::BoolLit:
            return true;
        // An interpolated string BUILDS a Str, so the result is a fresh value
        // however the parts were computed — `for "$x" { $_ = 9 }` is refused
        // by Rakudo exactly as `for "abc"` is. No need to look inside. (A bare
        // `"abc"` parses as one of these, not as StrLit.)
        case NK::InterpStr:
            return true;
        // a Range yields fresh values whatever its endpoints are: `for ^8` and
        // `for $a..$b` hand out bare Ints, which Rakudo refuses to write through
        case NK::Range:
            return true;
        case NK::Unary:
            return static_cast<const Unary*>(e)->op == "^";
        case NK::ListExpr: {
            for (auto& it : static_cast<const ListExpr*>(e)->items)
                if (!literalOnlySource(it.get())) return false;
            return true;
        }
        case NK::ArrayLit: {
            // `<a b>` is a word LIST of bare values; `[1,2,3]` is an Array, and
            // its elements are containers — Rakudo runs `for [1,2,3] { $_ = 9 }`
            auto* al = static_cast<const ArrayLit*>(e);
            if (!al->isList) return false;
            for (auto& it : al->items)
                if (!literalOnlySource(it.get())) return false;
            return true;
        }
        case NK::MethodCall: {
            // a no-arg view of a literal is still bare values (`(1,2,3).reverse`).
            // With ARGS it could hand back anything a block names, so it is out.
            auto* mc = static_cast<const MethodCall*>(e);
            if (!mc->args.empty() || mc->meta || mc->hyper || mc->maybe || mc->methodExpr)
                return false;
            return literalOnlySource(mc->inv.get());
        }
        default: return false; // a Call can RETURN containers: `sub f() { @g }`
    }
}

bool Interpreter::immutableLoopSource(const Expr* e) {
    if (!e) return false;
    if (literalOnlySource(e)) return true;
    if (e->kind == NK::MethodCall) {
        auto* mc = static_cast<const MethodCall*>(e);
        if (!mc->args.empty() || mc->meta || mc->hyper || mc->maybe || mc->methodExpr)
            return false;
        // `.kv` and `.pairs` build FRESH keys and Pairs rather than passing the
        // elements along, and `.List` decontainerises — all three refuse the
        // write under Rakudo for an Array and a Hash alike. (`.values`,
        // `.list`, `.reverse` and `.sort` do pass them along, and alias.)
        return mc->method == "kv" || mc->method == "pairs" || mc->method == "List";
    }
    // `list(@a, @b)` flattens two arrays into a NEW List and Rakudo refuses the
    // write; `list(@a)` alone is the array's own view and aliases (see
    // valuesArrayAlias). `flat(@a, @b)` is NOT the same — it aliases — so this
    // names `list` and nothing else.
    if (e->kind == NK::Call) {
        auto* c = static_cast<const Call*>(e);
        return c->name == "list" && c->args.size() > 1;
    }
    return false;
}

// `$bag.values` / `.kv` / `.pairs` over an IMMUTABLE QuantHash (Bag, Mix,
// Set): the weights are bare values, so a write through the loop variable
// — or through a Pair's `.value` — is refused, where the same walk over a
// BagHash writes the weight back. Only a plain variable invocant is judged,
// read without evaluating anything.
bool Interpreter::immutableQuantSource(const Expr* e) {
    if (!e || e->kind != NK::MethodCall) return false;
    auto* mc = static_cast<const MethodCall*>(e);
    if (!mc->args.empty() || mc->meta || mc->hyper || mc->maybe || mc->methodExpr ||
        !(mc->method == "values" || mc->method == "kv" || mc->method == "pairs"))
        return false;
    if (!mc->inv || mc->inv->kind != NK::VarExpr) return false;
    auto* ve = static_cast<const VarExpr*>(mc->inv.get());
    if (ve->declare || ve->name.empty() || ve->name[0] != '$' || !tctx_.cur) return false;
    const Value* v = tctx_.cur->find(ve->name);
    if (!v || v->t != VT::Hash || !v->hash()) return false;
    const std::string k = v->hashKind.str();
    return k == "Bag" || k == "Mix" || k == "Set";
}

// `@a.values` and `@a.list` are VIEWS of the array, not copies of it: Rakudo
// hands the loop each element's own container, so a write through the topic
// lands in @a exactly as `for @a` does — `for @a.values { $_ = 9 }` leaves
// [9 9 9]. Ours copies the elements out, so the view has to be recognised by
// name and the real storage iterated instead. This is the array twin of
// valuesAliasSource above, and the two are tried in turn.
//
// Only the views that ARE the array, in order. `.grep`, `.reverse` and the
// rest of the chain also alias under Rakudo, because there every Array
// element is a container and these methods just pass them along; reproducing
// that here is the container/binding refactor, not a list of method names.
// Plain `for @a.grep(…)` does not alias today either, so stopping at the
// identity views keeps the two consistent.
PRef<ValueList> Interpreter::valuesArrayAlias(Expr* listExpr) {
    if (!listExpr) return nullptr;
    Expr* arrArg = nullptr;
    if (listExpr->kind == NK::Call) {
        auto* c = static_cast<Call*>(listExpr);
        if ((c->name == "values" || c->name == "list") && c->args.size() == 1)
            arrArg = c->args[0].get();
    }
    else if (listExpr->kind == NK::MethodCall) {
        auto* mc = static_cast<MethodCall*>(listExpr);
        if ((mc->method == "values" || mc->method == "list") && mc->args.empty() &&
            !mc->meta && !mc->hyper && !mc->maybe && !mc->methodExpr)
            arrArg = mc->inv.get();
    }
    if (!arrArg || arrArg->kind != NK::VarExpr) return nullptr;
    auto* ve = static_cast<VarExpr*>(arrArg);
    // `@a.values`, and equally `.values` on a `$`-scalar HOLDING an array
    // (`my $r = [1,2,3]; for $r.values`), which shares the same storage
    if (ve->name.empty() || (ve->name[0] != '@' && ve->name[0] != '$') || ve->declare)
        return nullptr;
    // lvalue(), not a lexical lookup: an ATTRIBUTE is not in the enclosing Env,
    // and `for @!n { $_ = 9 }` already writes through, so `for @!n.values` has
    // to as well. Safe to resolve because the invocant is a bare VarExpr —
    // taking the lvalue of a method CHAIN would evaluate it and consume state,
    // which is why nothing wider is accepted here.
    Value* av = nullptr;
    try { av = lvalue(arrArg); } catch (...) { return nullptr; }
    if (!av || av->t != VT::Array || !av->arr() || av->isList) return nullptr; // a List is immutable
    return av->arrS();
}

// `for @$rgb { … }` / `for @($rgb)` iterates the ARRAY BEHIND the scalar, and the
// topic aliases its elements the same way `for @a` does — Color clips a colour
// tuple in place with `clip-to 0, $_, 255 for @$rgb`. The list-context operator
// itself copies (`@(…)` decontainerises), so the loop has to reach the container.
PRef<ValueList> Interpreter::derefArrayAlias(Expr* listExpr) {
    // `for @b[*] { $_ = 9 }`: the whole slice names every slot of THAT array,
    // holes included, so the topic aliases each one as `for @b` does
    if (listExpr && listExpr->kind == NK::Index) {
        auto* ix = static_cast<Index*>(listExpr);
        if (!ix->isHash && !ix->multiDim && ix->adverb.empty() && ix->index &&
            ix->index->kind == NK::Whatever && !static_cast<WhateverExpr*>(ix->index.get())->hyper &&
            ix->base && ix->base->kind == NK::VarExpr) {
            auto* sv = static_cast<VarExpr*>(ix->base.get());
            if (sv->name.size() > 1 && sv->name[0] == '@' && !sv->declare)
                if (Value* av = tctx_.cur->find(sv->name))
                    if (av->t == VT::Array && av->arr() && !av->isList && !av->ext()) return av->arrS();
        }
        return nullptr;
    }
    if (!listExpr || listExpr->kind != NK::Unary) return nullptr;
    auto* u = static_cast<Unary*>(listExpr);
    // `for |@a { … }`: slipping an array in is still iterating THAT array, so
    // the topic aliases its elements exactly as `for @a` does
    if (u->op == "|" && u->operand && u->operand->kind == NK::VarExpr) {
        auto* sv = static_cast<VarExpr*>(u->operand.get());
        if (sv->name.size() > 1 && sv->name[0] == '@' && !sv->declare)
            if (Value* av = tctx_.cur->find(sv->name))
                if (av->t == VT::Array && av->arr() && !av->isList) return av->arrS();
        return nullptr;
    }
    // `for $rgb<> { $_ = … }` is the same reach: the zen slice decontainerises the
    // very same Array, so the topic must alias its elements, not copies of them
    if (u->op != "ctx@" && u->op != "decont") return nullptr;
    if (!u->operand || u->operand->kind != NK::VarExpr) return nullptr;
    auto* ve = static_cast<VarExpr*>(u->operand.get());
    if (ve->name.empty() || ve->name[0] != '$') return nullptr;
    Value* v = tctx_.cur->find(ve->name);
    if (!v || v->t != VT::Array || !v->arr() || v->isList) return nullptr; // a List is immutable
    return v->arrS();
}

// `for @a.reverse` — the reversed view of an Array hands out the array's own
// elements (Rakudo's reverse returns the containers, holes included), so the
// loop walks the real storage backwards. Only the plain no-argument call on
// an `@`-variable.
PRef<ValueList> Interpreter::reverseArrayAlias(Expr* listExpr) {
    if (!listExpr || listExpr->kind != NK::MethodCall) return nullptr;
    auto* mc = static_cast<MethodCall*>(listExpr);
    if (mc->method != "reverse" || !mc->args.empty() || mc->hyper || mc->meta || mc->methodExpr ||
        !mc->inv || mc->inv->kind != NK::VarExpr) return nullptr;
    auto* sv = static_cast<VarExpr*>(mc->inv.get());
    if (sv->name.size() < 2 || sv->name[0] != '@' || sv->declare) return nullptr;
    Value* av = tctx_.cur->find(sv->name);
    if (!av || av->t != VT::Array || !av->arr() || av->isList || av->ext()) return nullptr;
    return av->arrS();
}

// `@a.grep(PRED)` over a real Array: the array's storage and the positions of
// the elements the predicate keeps, in order — the containers grep hands back.
// The predicate is evaluated ONCE and called per element, as grep does.
std::shared_ptr<std::pair<PRef<ValueList>, std::vector<size_t>>>
Interpreter::grepArrayView(Expr* e) {
    Expr* pred = nullptr;
    Expr* src = peelGrepFilter(e, pred);
    if (!pred || !src || src->kind != NK::VarExpr) return nullptr;
    auto* sv = static_cast<VarExpr*>(src);
    if (sv->name.size() < 2 || sv->name[0] != '@' || sv->declare) return nullptr;
    Value* av = tctx_.cur->find(sv->name);
    if (!av || av->t != VT::Array || !av->arr() || av->isList || av->ext()) return nullptr;
    auto view = std::make_shared<std::pair<PRef<ValueList>, std::vector<size_t>>>();
    view->first = av->arrS();
    Value pv = eval(pred);
    for (size_t i = 0; i < view->first->size(); i++)
        if (matcherAccepts(*this, (*view->first)[i], pv)) view->second.push_back(i);
    return view;
}

// `for $c, $m, $y, $k { … }` — every item is a CONTAINER, so the topic aliases it
// and a writing body updates all four (Color clamps a CMYK tuple with
// `clip-to 0, $_, 1 for $c, $m, $y, $k`). Only a list made ENTIRELY of scalar
// variables qualifies; anything else is a plain list of values.
// The writable slot a `given`/`with` topic aliases, or null when there is none.
// A plain scalar variable qualifies, and so does a hash/array ELEMENT — HTTP::Tiny's
// own test suite rewrites its expected bodies with
// `$_ = Buf[uint8].new: .encode with %want<content>`. A literal, a call result or a
// whole array/hash has nowhere to write back to. `skip` means the body will not run
// (`with` on an undefined topic), and then the slot is not taken at all, so a missing
// key is not autovivified just by being tested.
Value* Interpreter::topicAliasSlot(Expr* topic, bool skip, bool allowObject) {
    if (!topic || skip) return nullptr;
    if (topic->kind == NK::VarExpr) {
        auto* tv = static_cast<VarExpr*>(topic);
        if (tv->name.empty() || tv->name[0] != '$' || tv->declare) return nullptr;
    }
    else if (topic->kind == NK::Index) {
        // Only an element of a PLAIN Hash or Array. Taking the lvalue of anything
        // else can create the very thing it is inspecting — `with
        // $match<authority> { … }` over a Match would autovivify a key into the
        // parse result and wreck it (URI's grammar walk is written that way).
        auto* ix = static_cast<Index*>(topic);
        if (!ix->base) return nullptr;
        Value* base = nullptr;
        try { base = lvalue(ix->base.get(), /*asInvocant=*/true); } catch (...) { return nullptr; }
        if (!base) return nullptr;
        bool plain = (base->t == VT::Hash && base->hash() && base->hashKind.empty()) ||
                     (base->t == VT::Array && base->arr() && !base->isList);
        // …and an OBJECT backed by one, when the caller has already established
        // that the block assigned: `with self<ID> { } else { $_ = [$.id xx 2] }`
        // is how PDF initialises a document ID, and self is a Hash-backed
        // PDF::COS::Dict whose own ASSIGN-KEY is what the store must go through.
        if (!plain && allowObject && base->t == VT::Object && base->obj())
            plain = true;
        if (!plain) return nullptr;
    }
    else return nullptr;
    try { return lvalue(topic); } catch (...) { return nullptr; }
}

// A `with`/`given` topic that is an ELEMENT of an object writes back the way
// `$obj<k> = v` does — through the object's own ASSIGN-KEY / ASSIGN-POS. The raw
// slot topicAliasSlot() hands back reaches the object's underlying store
// directly, which for a class that keeps its real container behind those methods
// lands beside the one the next read consults: PDF::COS::Tie::Hash ties every
// entry in ASSIGN-KEY, so `with self<ID> { } else { $_ = [$.id xx 2] }` — how a
// PDF document gets its ID — assigned into nothing at all.
bool Interpreter::topicWriteThroughObject(Expr* topic, const Value& v) {
    if (!topic || topic->kind != NK::Index) return false;
    auto* ix = static_cast<Index*>(topic);
    if (!ix->index || ix->multiDim || !ix->adverb.empty() || !ix->base) return false;
    Value base;
    try { base = eval(ix->base.get()); } catch (...) { return false; }
    if (base.t != VT::Object || !base.obj() || !base.obj()->cls) return false;
    const char* meth = ix->isHash ? "ASSIGN-KEY" : "ASSIGN-POS";
    bool has = base.obj()->cls->findMethod(meth) != nullptr;
    for (ClassInfo* c = base.obj()->cls.get(); c && !has; c = c->parent.get())
        for (auto& at : c->attrs)          // …or delegated: `has %!s handles <ASSIGN-KEY>`
            for (auto& h : at.handles)
                if (h == meth || h == "*") { has = true; break; }
    if (!has) return false;
    Value k;
    try { k = eval(ix->index.get()); } catch (...) { return false; }
    try { methodCall(base, meth, ValueList{k, v}); } catch (...) { return false; }
    return true;
}

bool Interpreter::scalarListAlias(Expr* listExpr, std::vector<Value*>& slots) {
    // `for $pair.value -> $v is rw { … }` — the loop aliases the pair's own
    // value container (a Pair built from a container shares it)
    if (listExpr && listExpr->kind == NK::MethodCall) {
        auto* mc = static_cast<MethodCall*>(listExpr);
        if (mc->method == "value" && mc->args.empty() && !mc->meta && !mc->hyper && !mc->methodExpr &&
            mc->inv && mc->inv->kind == NK::VarExpr) {
            Value* p = nullptr;
            try { p = lvalue(mc->inv.get()); } catch (...) { p = nullptr; }
            if (p && p->t == VT::Pair && p->pairVal() && !p->pairValRO) {
                Value* slot = p->pairVal();
                if (slot->t == VT::Array || slot->t == VT::Hash) return false;   // iterates, not one item
                slots.push_back(slot);
                return true;
            }
        }
        return false;
    }
    if (!listExpr || listExpr->kind != NK::ListExpr) return false;
    auto* le = static_cast<ListExpr*>(listExpr);
    if (le->items.empty()) return false;
    // Which members can be aliased: a plain `$x`, and an ARRAY spelled `@a` or
    // slipped as `|@a` — whose ELEMENTS each get a slot, so the classic
    // trim-everything loop (`for $name, $a, $b, |@rest { s/^\s+//; s/\s+$// }`)
    // writes back through every one of them, not just the scalars.
    auto arrayOf = [&](Expr* e) -> Value* {
        Expr* inner = e;
        if (inner && inner->kind == NK::Unary && static_cast<Unary*>(inner)->op == "|")
            inner = static_cast<Unary*>(inner)->operand.get();
        if (!inner || inner->kind != NK::VarExpr) return nullptr;
        auto* ve = static_cast<VarExpr*>(inner);
        if (ve->name.size() < 2 || ve->name[0] != '@' || ve->declare) return nullptr;
        Value* v = tctx_.cur->find(ve->name);
        return (v && v->t == VT::Array && v->arr()) ? v : nullptr;
    };
    for (auto& it : le->items) {
        if (arrayOf(it.get())) continue;
        if (!it || it->kind != NK::VarExpr) return false;
        auto* ve = static_cast<VarExpr*>(it.get());
        if (ve->name.size() < 2 || ve->name[0] != '$' || ve->declare) return false;
        if (!tctx_.cur->find(ve->name)) return false;
    }
    for (auto& it : le->items) {
        if (Value* av = arrayOf(it.get())) {
            for (auto& el : *av->arr()) slots.push_back(&el);
            continue;
        }
        slots.push_back(lvalue(it.get()));
    }
    return true;
}

// A Proxy is a CONTAINER: reading it as a value runs its FETCH. That already
// happened for one held in a variable, but not for one handed back by a method —
// `$elem<id>`, where AT-KEY returns a Proxy (XML::Element's attributes), came back
// as the Proxy's own guts. Assignment targets go through lvalue() and never here.
// `T($v)` — the coercion a `T() $param` or `my T() $x` asks for. Rakudo tries the
// value's own method named T first and falls back to the type's COERCE/new; a
// QUALIFIED name has no matching method on a built-in (Str has `.IO`, not
// `.IO::Path`), which is what `IO::Path() :$filename` in XML's from-xml-file hits.
// Coerce a container's ELEMENTS in place, for `my Int() @a` / `my Hash() %h`.
// A `%` coerces the VALUE of each pair and leaves the key alone; an `@` coerces
// each element. Undefined values are left as they are — Rakudo's
// Array[Str(Any)] holds a Str type object without stringifying it — and a Pair
// list is what a hash assignment arrives as before the store builds the hash.
void Interpreter::coerceElems(Value& v, const std::string& ct, char sigil) {
    auto one = [&](Value& e) { if (rtIsDefined(e)) e = coerceToType(e, ct); };
    // COPY before coercing. The right-hand side of `my Hash() %o = %defaults`
    // is the source container itself — one shared_ptr, not a snapshot — so
    // coercing its entries where they lie rewrote %defaults' own values, and
    // the two ended up sharing every Hash: a later `%o<headers><k> = '+'`
    // wrote through into the Map it was copied from.
    if (v.t == VT::Hash && v.hash()) {
        auto fresh = makePayload<ValueMap>(*v.hash());
        v.setHash(fresh);
    }
    else if (v.t == VT::Array && v.arr()) {
        auto fresh = makePayload<ValueList>(*v.arr());
        v.setArr(fresh);
    }
    if (sigil == '%') {
        if (v.t == VT::Hash && v.hash()) { for (auto& kv : *v.hash()) one(kv.second); return; }
        if (v.t == VT::Pair) {
            if (Value* pv = v.pairVal()) { auto nv = makePayload<Value>(*pv); one(*nv); v.setPairVal(nv); }
            return;
        }
        if (v.t == VT::Array && v.arr()) { for (auto& e : *v.arr()) coerceElems(e, ct, '%'); return; }
        return;
    }
    if (v.t == VT::Array && v.arr()) { for (auto& e : *v.arr()) one(e); return; }
    one(v);
}

// Rakudo's coercion protocol into a USER type (a class, or a role through its
// pun), once the value's own `.Target` method has had its chance: the type's
// COERCE — every candidate along the MRO, less a parent's SUBMETHOD, a plain
// method ending the walk because it hides what is further up — and, when no
// candidate takes the value, the type's own `new`, with `$*COERCION-TYPE`
// telling it why it was called. What answers must BE the target, or the
// coercion is impossible: a COERCE written as `C1.new(…)` fails for a subclass
// of C1 (S12-coercion/coercion-methods.t). Which candidates take the value is
// asked the way `.cando` asks it, so a COERCE that dies inside is heard.
Value Interpreter::coerceThroughType(const Value& v, const std::string& target, const std::string& coercion) {
    auto cit = classes_.find(target);
    ClassInfo* tc = cit != classes_.end() ? cit->second.get() : nullptr;
    auto impossible = [&](const std::string& why) {
        throwTypedV("X::Coerce::Impossible",
            {{"target-type", Value::typeObj(target)}, {"from-type", Value::typeObj(v.typeName())}},
            "Impossible coercion from '" + v.typeName() + "' into '" + target + "': " + why);
    };
    if (!tc) impossible("no acceptable coercion method found");
    auto candidatesOf = [&](const char* name) {
        ValueList out;
        std::set<ClassInfo*> seen;
        std::function<bool(ClassInfo*)> walk = [&](ClassInfo* k) -> bool {   // false: stop here
            if (!k || !seen.insert(k).second) return true;
            auto mit = k->methods.find(name);
            if (mit != k->methods.end() && mit->second.t == VT::Code && mit->second.code()) {
                const auto& c = mit->second.code();
                if (c->isMultiDispatcher) {
                    for (auto& cand : c->candidates)
                        if (cand.t == VT::Code && cand.code() && !cand.code()->isProto &&
                            !(cand.code()->isSubmethod && k != tc))
                            out.push_back(cand);
                }
                else if (!(c->isSubmethod && k != tc)) { out.push_back(mit->second); return false; }
            }
            if (!walk(k->parent.get())) return false;
            for (auto& e : k->extraParents) if (!walk(e.get())) return false;
            return true;
        };
        walk(tc);
        ValueList taking;
        for (auto& c : out) if (scoreCandidate(c, ValueList{v}) >= 0) taking.push_back(c);
        return taking;
    };
    auto dispatch = [&](const char* name, ValueList cands) {
        Value disp; disp.t = VT::Code; disp.setCode(makePayload<Callable>());
        disp.code()->name = name;
        disp.code()->isMultiDispatcher = true;
        disp.code()->isMethod = true;
        disp.code()->candidates = std::move(cands);
        return invokeMethod(disp, Value::typeObj(target), ValueList{v});
    };
    Value r;
    const char* via = "COERCE";
    if (ValueList co = candidatesOf("COERCE"); !co.empty()) r = dispatch("COERCE", std::move(co));
    else {
        ValueList nw = candidatesOf("new");
        if (nw.empty()) impossible("no acceptable coercion method found");
        via = "new";
        auto frame = std::make_shared<Env>();
        frame->parent = tctx_.cur;
        frame->define("$*COERCION-TYPE", Value::typeObj(coercion.empty() ? target + "(Any)" : coercion));
        auto saved = tctx_.cur;
        tctx_.cur = frame;
        try { r = dispatch("new", std::move(nw)); }
        catch (...) { tctx_.cur = saved; throw; }
        tctx_.cur = saved;
    }
    if (r.t == VT::Hash && r.hashKind == "Failure") return r;
    if (!typeOrSubsetMatches(r, target))
        impossible(std::string("method ") + via + " returned " + (isDefined(r) ? "an instance" : "a type object") +
                   " of " + r.typeName());
    return r;
}

Value Interpreter::coerceToType(const Value& v, const std::string& type) {
    // A value that IS already the target type is not coerced at all — Rakudo's
    // coercion protocol only runs when it has to. Without this, `Mu:D(Int) $a`
    // (a way of saying "take anything, definite") died looking for a `.Mu`
    // method on an Int.
    // A Map is not a Hash. Both are one VT here, so the nominal check below
    // calls a Map a Hash and the coercion never ran — `my Hash() %o = %defaults`
    // left Text::Table::Simple's inner Maps immutable and its `.append` built an
    // Array. Rakudo's Map is Hash's PARENT (`Map ~~ Hash` is False here too),
    // and the same holds for the set family, so ask `.Hash`.
    if (type == "Hash" && v.t == VT::Hash && !v.hashKind.empty() && v.hashKind != "Hash")
        return methodCall(v, "Hash", ValueList{});
    if (type == "Mu" || type == "Any" || typeOrSubsetMatches(v, type)) return v;
    try { return methodCall(v, type, ValueList{}); }
    catch (RakuError& e) {
        // only a MISSING coercer means "try another route"; a coercer that ran
        // and refused (`"a".Date` — X::Temporal::InvalidFormat, `"a".Int` —
        // X::Str::Numeric) is the answer
        const Value& p = e.payload;
        bool notFound = (p.t == VT::Type && p.s == "X::Method::NotFound") ||
                        (p.t == VT::Object && p.obj() && p.obj()->cls && p.obj()->cls->name == "X::Method::NotFound");
        if (!notFound && (type == "Date" || type == "DateTime" || type == "Int" || type == "Num" ||
                          type == "Rat" || type == "Numeric" || type == "Real" || type == "Complex"))
            throw;
        // …and a USER coercion method that died is the answer too: `my
        // Str(Source) $e = Source.new` with `method Str { die … }` dies with
        // that (S02-types/nominalizables.t)
        if (!notFound && v.t == VT::Object && v.obj() && v.obj()->cls && v.obj()->cls->findMethod(type))
            throw;
    }
    if (type == "IO::Path") return methodCall(v, "IO", ValueList{});
    // a user type coerces through its own COERCE, then through a `new` of its OWN
    // that takes the value (coerceThroughType) — never the default constructor:
    // `-> Foo() $x {…}("42")` is an error in Rakudo when Foo says nothing about
    // coercing, and answering `Foo.new("42")` would hide it
    auto ci = classes_.find(type);
    if (ci != classes_.end() && ci->second && (ci->second->findMethod("COERCE") || ci->second->findMethod("new")))
        return coerceThroughType(v, type, "");
    if (size_t sep = type.rfind("::"); sep != std::string::npos) {
        try { return methodCall(v, type.substr(sep + 2), ValueList{}); }
        catch (RakuError&) {}
    }
    // an ENUM coerces by value (or by member name): `A(Any) $x` given 0 is A::b,
    // what `A(0)` answers
    {
        auto ep = enumPairs_.find(type);
        if (ep != enumPairs_.end() && ep->second.arr())
            for (auto& p : *ep->second.arr()) {
                Value* pv = p.pairVal();
                bool hit = (v.t == VT::Str && !v.isAllomorph()) ? p.s.str() == v.s.str()
                                                                  : (pv && valueEq(*pv, v));
                if (!hit) continue;
                if (Value* ev = tctx_.cur->find(type + "::" + p.s.str())) return *ev;
                if (Value* ev = tctx_.cur->find(p.s.str())) return *ev;
            }
    }
    throw RakuError{Value::typeObj("X::Coerce::Impossible"),
        "Impossible coercion from '" + v.typeName() + "' into '" + type + "'"};
}

Value Interpreter::deproxy(Value v) {
    if (v.t == VT::Hash && v.hashKind == "Proxy" && v.hash()) {
        { size_t idx = 0; if (slotProxyTarget(v, idx)) return slotProxyRead(v); }   // compact slot: no call
        auto it = v.hash()->find("FETCH");
        if (it == v.hash()->end()) return v;
        // FETCH may be written as a `method` — XML::Element's is — in which case the
        // Proxy is its INVOCANT, not its first positional.
        if (it->second.t == VT::Code && it->second.code() && it->second.code()->isMethod)
            return invokeMethod(it->second, v, {});
        return callCallable(it->second, { v });
    }
    return v;
}

// `$proxy = v` runs STORE. Rakudo hands the container in as well, so a
// `sub ($, $v)` STORE — the spelling every module uses — takes the value in its
// SECOND parameter; a one-parameter STORE still gets just the value.
Value Interpreter::proxyStore(const Value& proxy, const Value& v) {
    { size_t idx = 0; if (slotProxyTarget(proxy, idx)) return slotProxyWrite(proxy, v); }   // compact slot: no call
    auto it = proxy.hash()->find("STORE");
    if (it == proxy.hash()->end()) return v;
    // `method ($val)` takes the Proxy as its invocant; `sub ($, $v)` takes it as the
    // first positional. Both spellings appear in the wild — XML uses the method form.
    if (it->second.t == VT::Code && it->second.code() && it->second.code()->isMethod)
        return invokeMethod(it->second, proxy, { v });
    return codeArity(it->second) >= 2 ? callCallable(it->second, { proxy, v })
                                      : callCallable(it->second, { v });
}

Value Interpreter::makePathProxy(std::shared_ptr<Env> scope, Expr* path) {
    Value proxy = Value::makeHash(); proxy.hashKind = "Proxy";
    slotProxyPair(proxy,
        [scope, path](Interpreter& I, ValueList&) -> Value {
            auto saved = I.tctx_.cur; I.tctx_.cur = scope;
            Value r;
            try { r = I.eval(path); } catch (...) { I.tctx_.cur = saved; throw; }
            I.tctx_.cur = saved;
            return r;
        },
        [scope, path](Interpreter& I, ValueList& sa) -> Value {
            Value nv = sa.empty() ? Value::any() : sa[0];
            auto saved = I.tctx_.cur; I.tctx_.cur = scope;
            try { if (Value* slot = I.lvalue(path)) *slot = nv; }
            catch (...) { I.tctx_.cur = saved; throw; }
            I.tctx_.cur = saved;
            return nv;
        });
    return proxy;
}

// `$a := $b` binds $a to the CONTAINER $b holds, not to the slot named $b —
// which is the whole difference between the two operators. Assignment writes
// through a shared container, so `$b = 5` is visible as `$a`; REBINDING does
// not, because `$b := 5` gives the name $b a different container and leaves $a
// holding the one it was bound to.
//
// A slot-name alias cannot express that: it follows the NAME, so a rebound
// source dragged every alias along with it. `my $prev := $pulled` inside a loop
// that rebinds `$pulled` each turn therefore saw the CURRENT item as its
// "previous" one, which is Hash::int's push (and the PDF family behind it)
// reporting `expected int but got Str`.
//
// So the source's slot is PROMOTED on first alias: its value moves into a cell
// both names then proxy. A later `$b := …` overwrites $b's slot outright,
// dropping its proxy and leaving the cell — and $a — exactly as they were.
const char* kCellKey = "\x01" "cell";   // \x eats hex digits: keep the tag separate

// What `=:=` compares: the cell a Proxy-bound slot shares, the element a
// compact slot alias (take-rw, `my $x := @a[1]`) names, else the slot itself.
// A real cell (Value::isCell) arrives here already dereferenced, as the address
// of the Value every bound name reaches.
const void* Interpreter::containerId(const Value* slot) {
    for (int hop = 0; slot && hop < 16; hop++) {
        if (auto c = cellOfProxy(slot)) return c.get();
        size_t ix = 0;
        if (slot->t == VT::Hash && slot->hashKind == "Proxy" && slot->hash())
            if (ValueList* arr = slotProxyTarget(*slot, ix))
                if (ix < arr->size()) { slot = &(*arr)[ix]; continue; }
        return slot;
    }
    return slot;
}

// The shared cell a VARIABLE's container lives in, from the scope that owns it:
// its storage slot is promoted on first use (Value::promoteToCell), and a slot
// an older bind already made a Proxy-cell answers with that cell. Null for a
// slot holding any other Proxy — a user's, or an env-slot alias.
PRef<Value> Interpreter::varCell(Env* owner, const std::string& name) {
    Value* raw = owner ? owner->localRaw(name) : nullptr;
    if (!raw) return nullptr;
    if (raw->isCell()) return raw->cellS();
    if (auto c = cellOfProxy(raw)) return c;
    if (raw->t == VT::Hash && raw->hashKind == "Proxy") return nullptr;
    return raw->promoteToCell();
}

// The shared cell behind the VARIABLE an already-evaluated expression names —
// `$v`, or the one `my $v = 42` / `my $ = 42` just declared — promoted on first
// use, so a Pair built from it (`a => $v`) holds that very container, as
// Rakudo's does. Null for anything else: a value, an element, `$_`, a
// parameter still linked the old way — and a name bound to a VALUE (a
// readonly parameter, `my $x := 42`), which has no container to share: a Pair
// over it holds the value, read-only, and `*boundToValue` says so. (Sharing
// that slot handed its readonly mark to whatever copied the Pair's value —
// `C.new(q => $v)` left the attribute unassignable.)
PRef<Value> Interpreter::exprVarCell(const Expr* e, bool* boundToValue) {
    if (e && e->kind == NK::Assign) {
        auto* as = static_cast<const Assign*>(e);
        if (as->op != "=" || !as->target || as->target->kind != NK::VarExpr ||
            !static_cast<const VarExpr*>(as->target.get())->declare) return nullptr;
        e = as->target.get();
    }
    if (!e || e->kind != NK::VarExpr || !tctx_.cur) return nullptr;
    const std::string& n = static_cast<const VarExpr*>(e)->name;
    // A SIGILLESS name bound to a container (`sub f(\t)` handed `$x`) IS that
    // container — `$!t := t` and `my $z := t` alias the caller's variable. One
    // bound to a value (`f(42)`) has none, and stays out.
    if (!n.empty() && (ascii::isalpha((unsigned char)n[0]) || n[0] == '_')) {
        Env* own = nullptr;
        Value* raw = tctx_.cur->findRaw(n, &own);
        if (raw && own && raw->isCell()) return varCell(own, n);
        return nullptr;
    }
    // (an anonymous `my $` is named `$` + "\x01anonN" by the parser)
    if (n.size() < 2 || n[0] != '$' || n == "$_" ||
        !(ascii::isalpha((unsigned char)n[1]) || n[1] == '_' || n[1] == '\x01')) return nullptr;
    Env* own = nullptr;
    Value* raw = tctx_.cur->findRaw(n, &own);
    if (!raw || !own) return nullptr;
    if (own->ex && (own->ex->rwLinks.count(n) || own->ex->rwDirect.count(n))) return nullptr;
    if (const Value* v = raw->deref(); v->readonly || v->immutableBind) {
        if (boundToValue) *boundToValue = true;
        return nullptr;
    }
    return varCell(own, n);
}

// `($a, 42)[k]` with a single, in-range Int subscript: the literal's item k.
Expr* Interpreter::listLiteralItem(Index* ix) {
    if (!ix || ix->isHash || !ix->index || !ix->adverb.empty() || ix->multiDim || !ix->base ||
        ix->base->kind != NK::ListExpr) return nullptr;
    auto* le = static_cast<ListExpr*>(ix->base.get());
    if (!plainListLiteral(le) || ix->index->kind == NK::ListExpr || ix->index->kind == NK::Range ||
        ix->index->kind == NK::Whatever) return nullptr;
    Value k = eval(ix->index.get());
    if (k.t != VT::Int) { pendingSubscripts_.emplace_back(ix->index.get(), k); return nullptr; }
    long long n = k.toInt();
    if (n < 0 || n >= (long long)le->items.size()) { pendingSubscripts_.emplace_back(ix->index.get(), k); return nullptr; }
    return le->items[(size_t)n].get();
}

// A list that holds CONTAINERS (Value::holdsContainers) read as a VALUE: a
// fresh buffer in which each container gives what it holds. Everything that
// walks elements raw — the built-in methods, the operators — gets this.
Value Interpreter::decontList(const Value& v) {
    if (!v.arr()) return v;
    Value out = v;
    out.setArr(makePayload<ValueList>(*v.arr()));
    out.xw().holdsCells = false;
    for (auto& e : *out.arr()) {
        if (e.t == VT::Hash && e.hashKind == "Proxy" && e.hash()) e = deproxy(e);
        else if (e.isCell()) e = *e.cellS();
        e.readonly = e.immutableBind = false;
    }
    return out;
}

// A LIST ELEMENT that is the container a variable expression names (`$a`,
// `my $ = 3`): a Proxy over the variable's cell, the form element storage
// reads through. False when `e` names no variable.
bool Interpreter::containerElemFor(const Expr* e, Value& out) {
    auto c = exprVarCell(e);
    if (!c) return false;
    out = makeSharedCellProxy(std::move(c));
    return true;
}
// …and is this element such a container (or a real cell)?
bool Interpreter::isContainerElem(const Value& v) {
    // (…and a Proxy over an element — what `my @s := @a[1, 2]` binds — takes
    // a write the same way)
    return v.isCell() || cellOfProxy(&v) != nullptr ||
           (v.t == VT::Hash && v.hashKind == "Proxy" && v.hash() && v.hash()->count("STORE"));
}

// The storage slot an element subscript names, WITHOUT autovivifying anything:
// a plain `@a[i]` or `%h{k}` of a built-in Array or Hash, reached through a
// variable or a chain of such subscripts. Null when the element does not exist
// or the shape is anything else (a slice, an adverb, a user container, …).
Value* Interpreter::peekElemSlot(Index* ix) {
    if (!ix || ix->multiDim || !ix->index || !ix->adverb.empty() || !ix->base) return nullptr;
    Value* bp = nullptr;
    if (ix->base->kind == NK::VarExpr) {
        try { bp = lvalue(ix->base.get(), /*asInvocant=*/true); } catch (RakuError&) { bp = nullptr; }
    }
    else if (ix->base->kind == NK::Index) bp = peekElemSlot(static_cast<Index*>(ix->base.get()));
    if (!bp) return nullptr;
    if (bp->t == VT::Hash && bp->hashKind == "Proxy") {
        auto c = cellOfProxy(bp);
        if (!c) return nullptr;
        bp = c.get();
    }
    Value idx = eval(ix->index.get());
    if (idx.t == VT::Array || idx.t == VT::Range || idx.t == VT::Whatever || idx.t == VT::Code) return nullptr;
    if (!ix->isHash) {
        if (bp->t != VT::Array || !bp->arr() || bp->ext() || idx.t != VT::Int) return nullptr;
        long long k = idx.toInt();
        if (k < 0 || k >= (long long)bp->arr()->size()) return nullptr;
        return &(*bp->arr())[(size_t)k];
    }
    if (bp->t != VT::Hash || !bp->hash() || !bp->hashKind.empty()) return nullptr;
    auto it = bp->hash()->find(hashSubKey(idx, bp));
    return it == bp->hash()->end() ? nullptr : &it->second;
}

Value Interpreter::makeEnvSlotProxy(std::shared_ptr<Env> owner, const std::string& src) {
    Value proxy = Value::makeHash(); proxy.hashKind = "Proxy";
    slotProxyPair(proxy,
        [owner, src](Interpreter& I, ValueList&) -> Value {
            Value* op = owner->local(src);
            if (!op) return Value::any();
            // a bound-to-bound chain: deref one more Proxy level
            if (op->t == VT::Hash && op->hashKind == "Proxy" && op->hash()) {
                auto f2 = op->hash()->find("FETCH");
                if (f2 != op->hash()->end()) { ValueList none; return I.callCallable(f2->second, none); }
            }
            return *op;
        },
        [owner, src](Interpreter& I, ValueList& sa) -> Value {
            Value nv = sa.empty() ? Value::any() : sa[0];
            Value* op = owner->local(src);
            if (op && op->t == VT::Hash && op->hashKind == "Proxy" && op->hash()) {
                auto s2 = op->hash()->find("STORE");
                if (s2 != op->hash()->end()) { ValueList one{nv}; return I.callCallable(s2->second, one); }
            }
            if (op) *op = nv; else owner->define(src, nv);
            return nv;
        });
    return proxy;
}

// The replacement is spliced by CHARACTER, and a Str mixin (`"asd" but R`)
// stays one: Rakudo writes the new text into the same kind of value, so
// `$t.substr-rw(1, 1) = "c"` leaves a Str+{R} holding "acd". A fresh object,
// though — another holder of the old value keeps the old text.
Value Interpreter::spliceStr(const Value& s, long long from, long long len, const Value& repl) {
    const bool mixin = s.t == VT::Object && s.obj() && s.obj()->hasBoxed && s.obj()->boxed.t == VT::Str;
    Value text = mixin ? s.obj()->boxed : s;
    long long n = methodCall(text, "chars", ValueList{}).toInt();
    long long f = std::min(from, n), e = len < 0 ? n : std::min(n, from + len);
    std::string out = methodCall(text, "substr", ValueList{Value::integer(0), Value::integer(f)}).toStr()
                    + repl.toStr()
                    + methodCall(text, "substr", ValueList{Value::integer(e)}).toStr();
    if (!mixin) return Value::str(out);
    auto od = makePayload<ObjectData>();
    od->cls = s.obj()->cls;
    od->attrs = s.obj()->attrs;
    od->boxed = Value::str(out);
    od->hasBoxed = true;
    Value nv = s;
    nv.setObj(od);
    return nv;
}

// `my $r := $str.substr-rw(0, 5)`, and the same call as the value of an `is raw`
// routine (`method AT-POS(\SELF: $i) is raw { SELF.substr-rw($i, 1) }`): the
// positions are fixed when the Proxy is made, as in Rakudo — a sibling proxy
// keeps pointing at the same place when this one changes the string's length.
Value Interpreter::substrRwProxy(std::shared_ptr<Env> owner, const std::string& vname, long long from,
                                 long long len) {
    auto cur = [owner, vname](Interpreter& I) -> Value {
        Value* p = owner->local(vname);
        return p ? I.deproxy(*p) : Value::str("");
    };
    Value proxy = Value::makeHash(); proxy.hashKind = "Proxy";
    Value fetch; fetch.t = VT::Code; fetch.setCode(makePayload<Callable>());
    fetch.code()->builtin = [cur, from, len](Interpreter& I, ValueList&) -> Value {
        Value s = cur(I);
        if (s.t == VT::Object && s.obj() && s.obj()->hasBoxed) s = s.obj()->boxed;
        ValueList sa{Value::integer(from)};
        if (len >= 0) sa.push_back(Value::integer(len));
        return I.methodCall(s, "substr", sa);
    };
    Value store; store.t = VT::Code; store.setCode(makePayload<Callable>());
    store.code()->builtin = [owner, vname, cur, from, len](Interpreter& I, ValueList& sa) -> Value {
        Value nv = sa.empty() ? Value::str("") : sa.back();
        Value out = I.spliceStr(cur(I), from, len, nv);
        if (Value* p = owner->local(vname)) *p = out;
        return nv;
    };
    (*proxy.hash())["FETCH"] = fetch;
    (*proxy.hash())["STORE"] = store;
    return proxy;
}

bool Interpreter::substrRwProxyOf(Expr* e, Value& out) {
    if (!e) return false;
    Expr* invE = nullptr; const std::vector<ExprPtr>* srArgs = nullptr; size_t argOfs = 0;
    if (e->kind == NK::MethodCall && static_cast<MethodCall*>(e)->method == "substr-rw") {
        auto* mc = static_cast<MethodCall*>(e);
        if (mc->meta || mc->hyper || mc->methodExpr) return false;
        invE = mc->inv.get(); srArgs = &mc->args;
    }
    else if (e->kind == NK::Call && static_cast<Call*>(e)->name == "substr-rw" &&
             !static_cast<Call*>(e)->args.empty()) {
        auto* c = static_cast<Call*>(e);
        invE = c->args[0].get(); srArgs = &c->args; argOfs = 1;
    }
    if (!invE) return false;
    // a `$` variable, or a sigilless one (`\SELF`, `\x`)
    std::string vname;
    if (invE->kind == NK::VarExpr) vname = static_cast<VarExpr*>(invE)->name;
    else if (invE->kind == NK::NameTerm) vname = static_cast<NameTerm*>(invE)->name;
    if (vname.empty() || (invE->kind == NK::VarExpr && vname[0] != '$')) return false;
    std::shared_ptr<Env> owner;
    for (auto en = tctx_.cur; en; en = en->parent) if (en->local(vname)) { owner = en; break; }
    if (!owner) return false;
    ValueList pos;
    for (size_t k = argOfs; k < srArgs->size(); k++) pos.push_back(eval((*srArgs)[k].get()));
    if (pos.empty() || pos.size() > 2) return false;
    for (auto& pv : pos) if (pv.t != VT::Int) return false;
    out = substrRwProxy(owner, vname, pos[0].toInt(), pos.size() > 1 ? pos[1].toInt() : -1);
    return true;
}


// The array-slot proxy is COMPACT: the target lives in the proxy itself (the two
// hidden keys below) and FETCH/STORE are one SHARED pair of builtins that read it
// back out of the proxy they are handed. A per-proxy closure pair cost two
// make_shared<Callable> plus two std::function allocations on top of the hash —
// about seven allocations for what is a pointer and an index — and BinaryHeap's
// sift-down builds roughly thirty of them per call, 38k calls for one
// `Graph.diameter`. deproxy/proxyStore read the keys directly and never call at
// all; the entries stay present, and callable, for the generic FETCH/STORE sites.
static const char* kSlotArr = "\x01arr";
static const char* kSlotIdx = "\x01idx";
// An array slot alias names a POSITION, where Rakudo's names the element's own
// Scalar. The two agree while the array only grows at the end (`my $x := @a[1];
// @a.push(9); $x = 5` still writes element 1) — but once the array SHRINKS, the
// position no longer means the element that was bound. Rakudo's alias keeps the
// value its container held; ours does too, from here: kSlotSize is the length at
// bind time and kSlotLast the value last read through the alias. Hash::Ordered's
// DELETE-KEY names an element, splices it away and returns the name, which
// otherwise answered the NEXT element's value.
static const char* kSlotSize = "\x01siz";
static const char* kSlotLast = "\x01lst";

// The `$` variables a `for` modifier's one-item list can END on:
// `.++ for do given 1 { when True { $a } }` iterates $a's container, not a copy
// of its value (S04-statements/when.t). The walk follows a `do`/block's last
// statement into a given, whose `when`/`default` blocks are each the given's
// value when they match. Every tail must be a plain variable; any other tail (a
// literal, a call) means the value may not be a container, and the answer is no.
static bool forTailVarsStmt(const Stmt* s, std::vector<const void*>& out);
bool forTailVarsExpr(const Expr* e, std::vector<const void*>& out) {
    if (!e || out.size() > 8) return false;
    switch (e->kind) {
        case NK::VarExpr: {
            auto* v = static_cast<const VarExpr*>(e);
            if (v->declare || v->viaPseudoPkg || v->name.size() < 2 || v->name[0] != '$' ||
                std::strchr("*?!.^:=~", v->name[1]))
                return false;
            out.push_back(e);
            return true;
        }
        case NK::Unary: {
            auto* u = static_cast<const Unary*>(e);
            return u->op == "do" && forTailVarsExpr(u->operand.get(), out);
        }
        case NK::BlockExpr: {
            auto* be = static_cast<const BlockExpr*>(e);
            if (be->isSub || be->body.empty()) return false;
            return forTailVarsStmt(be->body.back().get(), out);
        }
        default: return false;
    }
}
static bool forTailVarsBlock(const Block* b, std::vector<const void*>& out) {
    return b && !b->stmts.empty() && forTailVarsStmt(b->stmts.back().get(), out);
}
static bool forTailVarsStmt(const Stmt* s, std::vector<const void*>& out) {
    if (!s) return false;
    switch (s->kind) {
        case NK::ExprStmt: return forTailVarsExpr(static_cast<const ExprStmt*>(s)->e.get(), out);
        case NK::WhenStmt: return forTailVarsBlock(static_cast<const WhenStmt*>(s)->body.get(), out);
        case NK::GivenStmt: {
            auto* g = static_cast<const GivenStmt*>(s);
            if (g->modifier || !g->body || g->body->stmts.empty() || g->hasElse) return false;
            // a when/default that matches leaves the given with ITS value, so
            // every one of them is a tail — and so is the body's last statement
            for (auto& st : g->body->stmts)
                if (st->kind == NK::WhenStmt && !forTailVarsStmt(st.get(), out)) return false;
            const Stmt* last = g->body->stmts.back().get();
            return last->kind == NK::WhenStmt || forTailVarsStmt(last, out);
        }
        default: return false;
    }
}

// The container a Pair's value lives in, as a Proxy. `pairVal` is shared by
// every copy of the pair, so a write through this reaches the pair wherever it
// is held — which is what `$p.value = v` and `my $v := $p.value` both mean.
Value Interpreter::makePairCellProxy(PRef<Value> cell) {
    Value proxy = Value::makeHash(); proxy.hashKind = "Proxy";
    Value fetch; fetch.t = VT::Code; fetch.setCode(makePayload<Callable>());
    fetch.code()->builtin = [cell](Interpreter&, ValueList&) -> Value { return *cell; };
    Value store; store.t = VT::Code; store.setCode(makePayload<Callable>());
    static const std::vector<Param> kTwoPair(2);
    store.code()->params = &kTwoPair;                  // proxyStore's `sub ($, $v)` shape
    store.code()->builtin = [cell](Interpreter&, ValueList& sa) -> Value {
        if (sa.size() >= 2) *cell = sa[1];
        return *cell;
    };
    (*proxy.hash())["FETCH"] = fetch;
    (*proxy.hash())["STORE"] = store;
    return proxy;
}

Value Interpreter::makeArraySlotProxy(PRef<ValueList> arr, size_t idx) {
    // Built once. STORE carries a two-parameter signature so codeArity sends it
    // the proxy alongside the value (proxyStore's `sub ($, $v)` spelling); a
    // builtin never binds its params, so they cost nothing else.
    static const Value kFetch = [] {
        Value f; f.t = VT::Code; f.setCode(makePayload<Callable>());
        f.code()->builtin = [](Interpreter& I, ValueList& a) -> Value {
            if (a.empty() || !a[0].hash()) return Value::any();
            return I.slotProxyRead(a[0]);
        };
        return f;
    }();
    static const Value kStore = [] {
        Value st; st.t = VT::Code; st.setCode(makePayload<Callable>());
        static const std::vector<Param> kTwo(2);
        st.code()->params = &kTwo;
        st.code()->builtin = [](Interpreter& I, ValueList& a) -> Value {
            if (a.size() < 2 || !a[0].hash()) return a.empty() ? Value::any() : a.back();
            return I.slotProxyWrite(a[0], a[1]);
        };
        return st;
    }();
    Value proxy = Value::makeHash(); proxy.hashKind = "Proxy";
    Value av; av.t = VT::Array; av.setArr(arr);          // shares the storage
    (*proxy.hash())[kSlotArr] = std::move(av);
    (*proxy.hash())[kSlotIdx] = Value::integer((long long)idx);
    (*proxy.hash())[kSlotSize] = Value::integer((long long)arr->size());
    (*proxy.hash())[kSlotLast] = idx < arr->size() ? (*arr)[idx] : Value::any();
    (*proxy.hash())["FETCH"] = kFetch;
    (*proxy.hash())["STORE"] = kStore;
    return proxy;
}

// The compact slot's target, or null when this proxy is not one.
ValueList* Interpreter::slotProxyTarget(const Value& proxy, size_t& idxOut) {
    if (!proxy.hash()) return nullptr;
    auto ai = proxy.hash()->find(kSlotArr);
    if (ai == proxy.hash()->end() || !ai->second.arr()) return nullptr;
    auto ii = proxy.hash()->find(kSlotIdx);
    idxOut = ii == proxy.hash()->end() ? 0 : (size_t)ii->second.toInt();
    return ai->second.arr();
}

// True once the array has SHRUNK since the alias was made: the position it holds
// no longer names the element it was bound to (see kSlotSize).
static bool slotProxyDetached(const Value& proxy, const ValueList* arr) {
    auto it = proxy.hash()->find(kSlotSize);
    return it != proxy.hash()->end() && arr->size() < (size_t)it->second.toInt();
}

Value Interpreter::slotProxyRead(const Value& proxy) {
    size_t idx = 0;
    ValueList* arr = slotProxyTarget(proxy, idx);
    if (!arr) return Value::any();
    ParStripe ps(*this, arr);
    auto& ph = *proxy.hash();
    if (slotProxyDetached(proxy, arr)) {
        auto it = ph.find(kSlotLast);
        return it != ph.end() ? it->second : Value::any();
    }
    Value v = idx < arr->size() ? (*arr)[idx] : Value::any();
    ph[kSlotLast] = v;   // what the alias keeps if the array shrinks later
    return v;
}

Value Interpreter::slotProxyWrite(const Value& proxy, const Value& nv) {
    size_t idx = 0;
    ValueList* arr = slotProxyTarget(proxy, idx);
    if (!arr) return nv;
    ParStripe ps(*this, arr);
    (*proxy.hash())[kSlotLast] = nv;
    if (slotProxyDetached(proxy, arr)) return nv;  // written to the detached element
    while (arr->size() <= idx) arr->push_back(Value::any());
    (*arr)[idx] = nv;
    return nv;
}

Value Interpreter::makeHashSlotProxy(PRef<ValueMap> h, const std::string& key) {
    Value proxy = Value::makeHash(); proxy.hashKind = "Proxy";
    slotProxyPair(proxy,
        [h, key](Interpreter& I, ValueList&) -> Value {
            ParStripe ps(I, h.get());
            auto it = h->find(key);
            return it != h->end() ? it->second : Value::any();
        },
        [h, key](Interpreter& I, ValueList& sa) -> Value {
            Value nv = sa.empty() ? Value::any() : sa[0];
            ParStripe ps(I, h.get());
            (*h)[key] = nv;
            return nv;
        });
    return proxy;
}

Value Interpreter::makeCellProxy(const Value& init) {
    auto cell = makePayload<Value>(init);
    Value proxy = Value::makeHash(); proxy.hashKind = "Proxy";
    slotProxyPair(proxy,
        [cell](Interpreter&, ValueList&) -> Value { return *cell; },
        [cell](Interpreter&, ValueList& sa) -> Value {
            *cell = sa.empty() ? Value::any() : sa[0];
            return *cell;
        });
    return proxy;
}

// A hoisted INIT runs before the mainline, which means outside every routine
// and every loop — so control flow leaving it has nothing to leave. `sub {
// INIT return }` is the spec'd X::ControlFlow::Return (roast
// S04-statements/return.t), NOT a cooperative return that the mainline would
// honour: that flag silently ended the program, and the rest of return.t's
// own tests stopped emitting at exactly that line.
void Interpreter::runHoistedInit(Block* b) {
    auto escaped = [this]() -> void {
        tctx_.returning = false; tctx_.loopCtl = 0;
        throw RakuError{Value::typeObj("X::ControlFlow::Return"),
                        "Attempt to return outside of any Routine"};
    };
    try {
        if (b->stmtForm) execBlock(b, tctx_.cur); // `INIT my $x = …` declares in the enclosing scope
        else { auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; execBlock(b, sc); }
    }
    catch (ReturnEx&) { escaped(); }
    if (tctx_.returning) escaped();
    tctx_.loopCtl = 0; // a `next`/`last` here belongs to no loop; don't let it steer the mainline
}

Value Interpreter::gatherTake(const ValueList& items, const Value& ret) {
    if (tctx_.gatherStack.empty())
        // outside a gather there is nowhere for the value to go, and silently
        // handing it back would make take look like a no-op instead of an
        // error. Roast asserts the exception's parts: illegal => "take",
        // enclosing => "gather" (S04-statements/gather.t, issue 3416).
        throwTypedV("X::ControlFlow",
                    {{"illegal", Value::str("take")}, {"enclosing", Value::str("gather")}},
                    "take without gather");
    auto& coll = *tctx_.gatherStack.back();
    // "at least one element first" is a statement about what was ALREADY
    // collected, so it has to be read before this take pushes: testing coll
    // after the push made it true on the very first take, and a gather whose
    // first take lands after the budget — spent on setup BEFORE it, not on
    // takes — was declared lazy without having probed anything. Digest::SHA3's
    // outer gather is that shape: its list expression builds (and probes) the
    // endless inner gather, so its own first take is already past the budget.
    // Being declared lazy means being re-run on demand, and re-running a block
    // whose list expression is `samewith …` fails outright: by then the
    // dispatcher whose scope it needs is gone.
    const bool hadSome = !coll.empty();
    // A SLIP splices — that is the whole point of the operator, and `take` is no
    // exception: `take (($from..$to).Slip)` contributes the numbers, not one
    // Slip object. Kept whole, it reached a typed `my Int @` as a Slip and
    // failed the element type check (Text::CSV's column ranges).
    for (auto& x : items) {
        if (x.t == VT::Array && x.arr() && x.s == "Slip" && !x.itemized)
            for (auto& e : *x.arr()) coll.push_back(e);
        else coll.push_back(x);
    }
    // a gather running as a coroutine hands control back to its consumer here
    if (tctx_.curGather && tctx_.gatherStack.size() == 1) { gatherTakeYield(coll); return ret; }
    // a lazy gather stops the block once it has produced enough elements
    size_t lim = tctx_.gatherLimits.empty() ? 0 : tctx_.gatherLimits.back();
    if (lim && coll.size() >= lim) throw StopGatherEx{};
    // …and stops when the probe's TIME budget is spent, so a generator
    // whose takes get steadily more expensive cannot make declaring it
    // slow. A prefix of none is not a probe.
    long long dl = tctx_.gatherDeadlines.empty() ? 0 : tctx_.gatherDeadlines.back();
    if (dl && hadSome && nowMicros() > dl) throw StopGatherEx{};
    return ret;
}

// `take-rw EXPR` — take the STORAGE behind EXPR, not a copy of its value: what
// lands in the gather stream is a slot Proxy, so mutating the sequence's
// elements later writes through to the place they came from (issue #39):
//     sub f(@list) { gather for @list { take-rw $_ } }
//     for f(@a) { $_++ };   # @a's own elements step
// Which storage that is:
//   · a variable the topic of a running rw for-loop aliases → the ELEMENT slot
//     (the loop's own aliasing is copy-in/copy-out and ends with the iteration,
//     so binding the loop var's Env slot would go dead — tctx_.topicAliases
//     carries the live (array, index) instead);
//   · any other visible variable → its Env slot, exactly as `:=` binds it;
//   · an array/hash subscript → that element/entry's slot;
//   · anything else (an expression, a fresh `my $ = …`, a runtime-negative
//     index) → a fresh anonymous cell: still writable through the sequence,
//     just not aliased to anything — the permissive end of Rakudo's behaviour,
//     where a non-container take-rw hands back something read-only.
// Resolve take-rw's argument to a Proxy over the storage it names, or a plain
// (non-Proxy) Any when it names no storage this can reach.
Value Interpreter::takeRwSlotProxy(Expr* arg) {
    Value proxy;
    if (arg->kind == NK::VarExpr && !static_cast<VarExpr*>(arg)->declare) {
        const std::string& name = static_cast<VarExpr*>(arg)->name;
        std::shared_ptr<Env> owner;
        for (std::shared_ptr<Env> en = tctx_.cur; en; en = en->parent)
            if (en->local(name)) { owner = en; break; }
        if (owner) {
            for (auto it = tctx_.topicAliases.rbegin(); it != tctx_.topicAliases.rend(); ++it)
                if (it->scope == owner.get() && *it->var == name)
                    return makeArraySlotProxy(it->arr, it->idx);
            return makeEnvSlotProxy(owner, name);
        }
    }
    else if (arg->kind == NK::Index) {
        auto* ix = static_cast<Index*>(arg);
        if (ix->index && !ix->multiDim) {
            // resolve the base as an RVALUE: a Value copy shares its underlying
            // storage, so the slot proxy still aliases the real container — and
            // a missing base (`@spot[10][…]` probing a border neighbor) does
            // not autovivify a phantom row the way an lvalue descent would
            Value baseV = eval(ix->base.get());
            // the base may itself be a bound slot: reach the container it holds
            if (baseV.t == VT::Hash && baseV.hashKind == "Proxy" && baseV.hash())
                baseV = deproxy(baseV);
            if (baseV.t == VT::Array && baseV.arr() && !ix->isHash) {
                long long i = eval(ix->index.get()).toInt();
                if (i >= 0) return makeArraySlotProxy(baseV.arrS(), (size_t)i);
            }
            else if (baseV.t == VT::Hash && baseV.hash() && ix->isHash &&
                     (baseV.hashKind.empty() || baseV.hashKind == "Hash")) {
                return makeHashSlotProxy(baseV.hashS(), eval(ix->index.get()).toStr());
            }
        }
    }
    return proxy;
}

Value Interpreter::evalTakeRw(Call* c) {
    Expr* arg = c->args[0].get();
    auto isProxy = [](const Value& p) { return p.t == VT::Hash && p.hashKind == "Proxy"; };
    Value proxy = takeRwSlotProxy(arg);
    // `take-rw A // B` — the roast neighbor idiom. Rakudo's `//` hands take-rw
    // the lhs CONTAINER when its value is defined, so the aliasing survives
    // the default-or; mirror that by consulting A's slot first and falling to
    // B (usually a bare `next`) only when A holds nothing.
    if (!isProxy(proxy) && arg->kind == NK::Binary && static_cast<Binary*>(arg)->op == "//") {
        auto* b = static_cast<Binary*>(arg);
        Value lp = takeRwSlotProxy(b->lhs.get());
        if (isProxy(lp)) {
            if (isDefined(deproxy(lp))) proxy = lp;
            else arg = b->rhs.get(); // undefined: B is what gets taken (or fires control)
        }
    }
    if (!isProxy(proxy)) {
        Value v = eval(arg);
        // the argument's evaluation may have fired cooperative control flow
        // (that bare `next`): the loop runner acts on the flag only after this
        // statement, so pushing now would take a value the block asked to skip
        if (tctx_.loopCtl || tctx_.returning) return v;
        proxy = makeCellProxy(v);
    }
    ValueList one{proxy};
    return gatherTake(one, proxy);
}

bool Interpreter::objListItems(const Value& v, ValueList& out) {
    if (v.t != VT::Object || !v.obj() || !v.obj()->cls) return false;
    std::string which;
    if (v.obj()->cls->findMethod("list")) which = "list";
    else if (v.obj()->cls->findMethod("iterator")) which = "iterator";
    else {
        for (ClassInfo* c = v.obj()->cls.get(); c && which != "list"; c = c->parent.get())
            for (auto& a : c->attrs)
                for (auto& hn : a.handles) {
                    if (hn == "list") { which = "list"; break; }
                    if (hn == "iterator" && which.empty()) which = "iterator";
                }
    }
    if (which.empty()) return false;
    Value r = methodCall(v, which, {});
    if (which == "iterator") {
        if (r.t == VT::Object && r.obj() && r.obj()->cls) {
            Value* po = r.obj()->cls->findMethod("pull-one");
            if (!po) return false;
            for (;;) {
                ValueList none;
                Value item = invokeMethod(*po, r, none);
                if (item.t == VT::Type && item.s == "IterationEnd") break;
                out.push_back(item);
            }
            return true;
        }
        if (r.t == VT::Hash && r.hashKind == "Iterator" && r.hash()) {
            auto items = r.hash()->find("items");
            if (items == r.hash()->end() || items->second.t != VT::Array || !items->second.arr())
                return false;
            long long pos = 0;
            auto p = r.hash()->find("pos");
            if (p != r.hash()->end()) pos = p->second.toInt();
            forceLazy(items->second);   // a gather behind it: produce it first
            for (size_t k = (size_t)std::max(0LL, pos); k < items->second.arr()->size(); k++)
                out.push_back((*items->second.arr())[k]);
            return true;
        }
    }
    if (r.t == VT::Array && r.arr()) { out = *r.arr(); return true; }
    if (r.t == VT::Hash && r.hash()) { Value ps = hashToPairs(r); out = *ps.arr(); return true; }
    return false;
}

// The container-model arms every `:=` and every `f() = v` pass before the
// assignment proper (ROAST-TRACKS-PLAN track A). OUT of evalAssign's frame on
// purpose: every assignment runs through there, and the Values and exception
// arguments these arms build would widen it for all of them.
[[gnu::noinline]] void Interpreter::assignContainerPrologue(Assign* a, bool isBind) {
    // A literal or an operator's result names no container, so there is
    // nothing to bind: `0 := 1` is X::Bind, where `0 = 1` is X::Assignment::RO.
    if (isBind) {
        switch (a->target->kind) {
            case NK::IntLit: case NK::NumLit: case NK::StrLit: case NK::BoolLit:
            case NK::InterpStr: case NK::AllomorphLit: case NK::Binary: case NK::ChainExpr:
                throwTypedV("X::Bind", {}, "Cannot use bind operator with this left-hand side");
            default: break;
        }
    }
    // `f() = v` where `f` is a USER routine that is not `is rw`: the call RUNS,
    // and what it hands back is a value — a Proxy it returns is fetched on the
    // way out — so the assignment is then refused.
    if (a->target->kind == NK::Call && a->op.size() == 1 && a->op[0] == '=') {
        auto* c = static_cast<Call*>(a->target.get());
        Value* fp = c->name.empty() || c->callee ? nullptr : tctx_.cur->find(callAmpName(c));
        if (fp && fp->t == VT::Code && fp->code() && !fp->code()->builtin && !fp->code()->retRw) {
            bool anyRw = false;
            for (auto& cand : fp->code()->candidates)
                if (cand.t == VT::Code && cand.code() && cand.code()->retRw) { anyRw = true; break; }
            if (!anyRw) {
                Value got = evalCall(c);
                if (got.t == VT::Hash && got.hashKind == "Proxy" && got.hash()) got = deproxy(got);
                throwTypedV("X::Assignment::RO", {{"typename", Value::str(got.typeName())}, {"value", got}},
                            "Cannot modify an immutable " + got.typeName() + " (" + got.gist() + ")");
            }
        }
    }
    // `$x := v` gives the NAME a new container. When $x shares a cell with
    // other names (`my $y := $x`), the cell stays theirs, holding what it held:
    // the name's storage slot is detached from it before any bind path runs, so
    // the bind lands in the name's own slot and not in the shared Value
    // (`my $x = 1; my $y := $x; $x := 3` leaves $y at 1).
    if (isBind && a->target->kind == NK::VarExpr && !static_cast<VarExpr*>(a->target.get())->declare) {
        Value* traw = tctx_.cur->findRaw(static_cast<VarExpr*>(a->target.get())->name);
        if (traw && traw->isCell()) {
            Value fresh = *traw->deref();
            fresh.readonly = fresh.immutableBind = false;
            *traw = std::move(fresh);
        }
        // …and a name bound to a bare VALUE (`my $r := $v +& $m`) is rebound
        // just the same: the immutability was the value's, not the name's
        else if (traw && traw->readonly && traw->immutableBind)
            traw->readonly = traw->immutableBind = false;
    }
}

// Expand a multi-dimensional subscript into concrete index tuples.
//
// `@a[1; *; 2..3]` names a set of paths, not one: every Whatever, list or range
// dimension fans out against the container it is applied to, and a Callable
// dimension (`*-1`) resolves against THAT level's size rather than the top. The
// read path (evalIndex) and the write path (evalAssignInner) both need the same
// set, and each used to carry its own copy of this recursion — identical but for
// a comment, which is the shape a divergence starts from.
// The concrete indices a Range/list DIMENSION selects at an array level of `n`
// elements. An endless range (`0..*`, `1..Inf`, `^Inf`) stops at the array's
// last index, as Rakudo's postcircumfix does for `@a[0..*]`: Value::flatten()
// answers its 10,000-element safety prefix for one, and every index past the
// end became a trailing (Any) — issue #68, `@a[0..*; 0]`. A `lazy` range and a
// lazy list (`@a[0, 2 ... *; 0]`) stop at the array's end too, where Rakudo
// stops pulling them. A FINITE overrun (`@a[0..10]` on 4 elements) still
// yields one (Any) per missing slot, the same as the single-dim slice.
// A WhateverCode index resolves against the list's length, fed once PER
// PARAMETER: `*-1` is called with elems, and `*-2..*-1` — a two-star curry —
// with (elems, elems), which is Rakudo's WhateverCode.POSITIONS. Fed once, the
// second star saw no argument at all and the slice was `2..-1`: empty.
Value Interpreter::whateverPos(const Value& code, long long n) {
    long long ar = code.code() ? std::max(1LL, code.code()->whateverArity) : 1;
    ValueList as;
    for (long long i = 0; i < ar; i++) as.push_back(Value::integer(n));
    return callCallable(code, std::move(as));
}

ValueList Interpreter::dimKeysAt(const Value& dv, long long n) {
    if (dv.t == VT::Range && !dv.rNum() && dv.ofType().empty()) { // an integer Range
        long long lo = dv.rFrom() + (dv.rExFrom() ? 1 : 0);
        long long hi = dv.rTo() - (dv.rExTo() ? 1 : 0);
        if (dv.rTo() >= 9000000000000000000LL || dv.b) hi = std::min(hi, n - 1); // endless, or `lazy`
        if (dv.rFrom() <= -9000000000000000000LL) lo = std::max(lo, 0LL);
        ValueList out;
        for (long long k = lo; k <= hi; k++) out.push_back(Value::integer(k));
        return out;
    }
    if (dv.t == VT::Array && dv.ext() && dv.arr()) { // a lazy list: pull until it leaves the array
        ValueList out;
        for (size_t k = 0; ; k++) {
            if (k >= dv.arr()->size()) {
                materializeLazy(dv, k + 1);
                if (k >= dv.arr()->size()) break; // the source ran dry
            }
            const Value& e = (*dv.arr())[k];
            long long i = e.toInt();
            if (i < 0 || i >= n) break;
            out.push_back(e);
        }
        return out;
    }
    return dv.flatten();
}

std::vector<ValueList> Interpreter::expandDimTuples(const Value& root, const ValueList& keys) {
    std::vector<ValueList> tuples;
    std::function<void(const Value&, size_t, ValueList&)> expand =
        [&](const Value& node, size_t d, ValueList& pref) {
        if (d == keys.size()) { tuples.push_back(pref); return; }
        auto emit1 = [&](Value kk) {
            if (kk.t == VT::Code && kk.code())   // *-1 against this branch's size
                kk = (node.t == VT::Array && node.arr())
                   ? whateverPos(kk, (long long)node.arr()->size())
                   : callCallable(kk, ValueList{node});
            Value child = Value::any();
            if (node.t == VT::Array && node.arr()) {
                long long i = kk.toInt(), n = (long long)node.arr()->size();
                if (i < 0) { i += n; kk = Value::integer(i); }
                if (i >= 0 && i < n) child = (*node.arr())[i];
            } else if (node.t == VT::Hash && node.hash()) {
                auto it = node.hash()->find(kk.toStr());
                if (it != node.hash()->end()) child = it->second;
            }
            pref.push_back(kk);
            expand(child, d + 1, pref);
            pref.pop_back();
        };
        const Value& k0 = keys[d];
        // a BLOCK dimension (`@m[*; {0,1}]`) answers this level's indices when
        // called with its size — a slice, unlike the WhateverCode `*-1`
        Value kb;
        if (isSliceBlock(k0))
            kb = callCallable(k0, ValueList{Value::integer(
                     node.t == VT::Array && node.arr() ? (long long)node.arr()->size() : 0)});
        const Value& k = isSliceBlock(k0) ? kb : k0;
        if (k.t == VT::Whatever) {
            if (node.t == VT::Array && node.arr())
                for (long long i = 0; i < (long long)node.arr()->size(); i++) emit1(Value::integer(i));
            else if (node.t == VT::Hash && node.hash())
                for (auto& kv2 : *node.hash()) emit1(Value::str(kv2.first));
            return;
        }
        if (k.t == VT::Array || k.t == VT::Range) {
            // against an array level an endless/lazy range stops at its end (issue #68)
            ValueList ks = (node.t == VT::Array && node.arr())
                         ? dimKeysAt(k, (long long)node.arr()->size()) : k.flatten();
            for (auto& e2 : ks) emit1(e2);
            return;
        }
        emit1(k);
    };
    ValueList pref;
    expand(root, 0, pref);
    return tuples;
}

// Assign a VALUE across a parenthesised list of targets — `($a, $b) = …`,
// and equally `($a, $b) .= reverse`, which is a mutating method call whose
// target happens to be that list (Text::Levenshtein::Damerau swaps its two
// strings that way, and the generic lvalue path could only say "Target is
// not assignable"). Nested list targets destructure recursively.
void Interpreter::assignListTarget(ListExpr* lst, const Value& rhs, bool isBinding) {
    forceLazy(rhs);   // `my ($a, $b) = gather { … }`: the gather's elements
    // one-level list flattening (Raku): a List/Range spreads, but an itemized
    // `[...]` Array stays one element — so `my ($a,$b) = M, [7,8]` gives $b = [7,8].
    auto spread = [](const Value& r) -> ValueList {
        ValueList vals;
        if (r.t == VT::Array && r.arr()) {
            // `my ($a, $b) = @m[3;2]` takes the leaves in row-major order,
            // so $a is 1 and $b is 2 — not the first two ROWS.
            ValueList shaped;
            if (isMultiDimShaped(r)) shapedLeaves(r, shaped);
            for (auto& it : (isMultiDimShaped(r) ? shaped : *r.arr())) {
                if (it.t == VT::Range) { for (auto& e : it.flatten()) vals.push_back(e); }
                else {
                    // ONE level, and one level only: an inner list is ONE value,
                    // itemized — `my ($a, $b, $c) = (1, 2), 3` is
                    // `($(1, 2), 3, Any)`, not three loose values (sheet LA-31).
                    Value e = it;
                    if (e.t == VT::Array || e.t == VT::Hash) e.itemized = true;
                    vals.push_back(std::move(e));
                }
            }
        } else if (r.t == VT::Range) vals = r.flatten();
        else vals.push_back(r);
        return vals;
    };
    // Bind positionally; a nested list target (`my (\a, (\b, \c))`) recursively
    // destructures the corresponding element.
    std::function<void(ListExpr*, const Value&)> bind = [&](ListExpr* L, const Value& r) {
        ValueList vals = spread(r);
        if (!isBinding) decontCopiedElems(vals);   // assignment stores values
        size_t vi = 0; // value cursor (a slurpy @/% target consumes the rest)
        for (size_t i = 0; i < L->items.size(); i++) {
            Expr* tgt = L->items[i].get();
            // `my :($a, $b, $c = EXPR) := …` — a signature-literal target's
            // per-slot DEFAULT, which the parser leaves as the item `$c = EXPR`.
            // A value from the right-hand list wins; the default fills the slot
            // the list does not reach. PDF::COS::Tie binds a two-element
            // `(obj-num, gen-num)` into a three-parameter signature whose third
            // parameter defaults to `$.reader`.
            Expr* slotDefault = nullptr;
            if (tgt->kind == NK::Assign && static_cast<Assign*>(tgt)->op == "=") {
                auto* as = static_cast<Assign*>(tgt);
                slotDefault = as->value.get();
                tgt = as->target.get();
            }
            if (tgt->kind == NK::Whatever) { vi++; continue; } // `(*, $a) = …` skips a value
            // POSTCONSTRAINTS: `my ($a where 2, "foo") = …` — a `where` on a
            // target and a bare LITERAL target both ask the value they get
            // (a literal takes its value and stores it nowhere)
            if (!isBinding) {
                const bool lit = tgt->kind == NK::StrLit || tgt->kind == NK::IntLit ||
                                 tgt->kind == NK::NumLit || tgt->kind == NK::InterpStr;
                const Expr* w = tgt->kind == NK::VarExpr ? static_cast<VarExpr*>(tgt)->declWhereExpr : nullptr;
                if ((lit || w) && vi < vals.size()) {
                    const Value& v = vals[vi];
                    bool ok;
                    auto wenv = std::make_shared<Env>(); wenv->parent = tctx_.cur;
                    wenv->define("$_", v);
                    auto saved = tctx_.cur; tctx_.cur = wenv;
                    try {
                        Value cv = eval(const_cast<Expr*>(lit ? tgt : w));
                        if (cv.t == VT::Code && cv.code()) ok = boolify(callCallable(cv, ValueList{v}));
                        else ok = boolify(smartmatchValue("~~", v, cv));
                    } catch (...) { tctx_.cur = saved; throw; }
                    tctx_.cur = saved;
                    if (!ok) {
                        std::string nm = tgt->kind == NK::VarExpr ? static_cast<VarExpr*>(tgt)->name : std::string("<anon>");
                        throwTypedV("X::TypeCheck::Assignment",
                            {{"got", v}, {"expected", Value::typeObj("<anon>")}, {"symbol", Value::str(nm)}},
                            "Type check failed in assignment to " + nm + "; expected <anon> but got " +
                            v.typeName() + (isDefined(v) ? " (" + typeCheckRepr(v) + ")" : ""));
                    }
                    if (lit) { vi++; continue; }
                }
                else if (lit) { vi++; continue; }
            }
            // `(@c[1, 2], @c[3], @d) = …` — a SLICE target takes one value per
            // subscript, in order
            if (!isBinding && tgt->kind == NK::Index && static_cast<Index*>(tgt)->index &&
                !static_cast<Index*>(tgt)->multiDim &&
                ((static_cast<Index*>(tgt)->index->kind == NK::ListExpr &&
                  static_cast<ListExpr*>(static_cast<Index*>(tgt)->index.get())->items.size() > 1) ||
                 static_cast<Index*>(tgt)->index->kind == NK::Range)) {
                auto* ix = static_cast<Index*>(tgt);
                Value keysV = eval(ix->index.get());
                ValueList keys = keysV.t == VT::Array || keysV.t == VT::Range ? keysV.flatten() : ValueList{keysV};
                Value* bp = nullptr;
                try { bp = lvalue(ix->base.get(), /*asInvocant=*/true); } catch (RakuError&) {}
                if (bp && ((ix->isHash && bp->t == VT::Hash && bp->hash() && bp->hashKind.empty()) ||
                           (!ix->isHash && bp->t == VT::Array && bp->arr() && !bp->isList))) {
                    for (auto& k : keys) {
                        Value v = vi < vals.size() ? vals[vi] : Value::any();
                        vi++;
                        if (ix->isHash) (*bp->hash())[k.toStr()] = v;
                        else {
                            long long i = k.toInt();
                            if (i < 0) continue;
                            if ((size_t)i >= bp->arr()->size()) bp->arrRef().resize((size_t)i + 1, Value::any());
                            bp->arrRef()[(size_t)i] = v;
                        }
                    }
                    continue;
                }
            }
            if (tgt->kind == NK::VarExpr) {
                const std::string& nm = static_cast<VarExpr*>(tgt)->name;
                if (nm.size() >= 1 && (nm[0] == '@' || nm[0] == '%')) {
                    // A BINDING target list is a SIGNATURE, and `@x` in one is an
                    // ordinary positional parameter constrained to Positional —
                    // it takes ONE argument and binds to it, where the same shape
                    // under `=` slurps the rest. `my ($sql, @bind) := do given
                    // $pair { .key, .value }` is how Red reads a prepared
                    // statement, and slurping gave it a one-element list holding
                    // the array, whose only member then became a phantom bind
                    // parameter. (A value that is NOT Positional/Associative is
                    // a hard type error in Rakudo; here it keeps the old slurp
                    // rather than becoming a new way to fail.)
                    if (isBinding && vi < vals.size() &&
                        ((nm[0] == '@' && vals[vi].t == VT::Array) ||
                         (nm[0] == '%' && vals[vi].t == VT::Hash))) {
                        // …decontainerized: the list ELEMENT is an item, the
                        // `@x` it binds to is not (`@bind.map` iterates it)
                        Value bv = vals[vi]; bv.itemized = false;
                        *lvalue(tgt) = std::move(bv);
                        vi++;
                        continue;
                    }
                    // …but an `@x` BOUND to a List of containers (`my @slice :=
                    // %hash<b c>`) takes one value per container, as List.STORE
                    // does: `(@slice, *) = <A B C D>` fills those two and leaves
                    // the rest to the targets after it (S32-hash/slice.t)
                    if (!isBinding && nm[0] == '@' && !static_cast<VarExpr*>(tgt)->declare) {
                        Value* held = tctx_.cur->find(nm);
                        bool allCont = held && held->t == VT::Array && held->isList && !held->itemized &&
                                       held->arr() && !held->arr()->empty() && held->enumName.empty();
                        if (allCont)
                            for (auto& el : *held->arr()) if (!isContainerElem(el)) { allCont = false; break; }
                        if (allCont) {
                            Value list = *held;   // the elements share storage
                            for (auto& el : *list.arr()) {
                                Value nv = vi < vals.size() ? vals[vi] : Value::any();
                                vi++;
                                if (el.isCell()) *el.deref() = nv;
                                else proxyStore(el, nv);
                            }
                            continue;
                        }
                    }
                    // an @/% target slurps every remaining value; later targets get Any
                    Value rest = Value::array();
                    for (size_t j = vi; j < vals.size(); j++) {
                        Value e = vals[j];
                        // A `%` target takes the PAIRS of what it slurps, so the
                        // itemization the value carries as a list element (it is
                        // in a container) must not survive into the coercion:
                        // `my ($a, %h) = "x", {:k<v>}` fills %h from the hash.
                        if (nm[0] == '%' && e.t == VT::Hash) e.itemized = false;
                        rest.arr()->push_back(std::move(e));
                    }
                    vi = vals.size();
                    Value* lv = lvalue(tgt);
                    if (nm[0] == '%') { rest.isList = true; *lv = coerceHash(rest); }
                    else *lv = rest;
                    continue;
                }
            }
            const bool noValue = vi >= vals.size() && !slotDefault;
            Value v = vi < vals.size() ? vals[vi]
                    : slotDefault      ? eval(const_cast<Expr*>(slotDefault))
                                       : Value::any();
            vi++;
            // a TYPED slot the list does not reach holds its type object:
            // `my Str ($a) = ()` leaves $a as (Str)
            if (noValue && !isBinding && tgt->kind == NK::VarExpr) {
                const std::string& dt = static_cast<VarExpr*>(tgt)->declType;
                if (!dt.empty() && ascii::isupper((unsigned char)dt[0])) v = Value::typeObj(dt);
            }
            if (tgt->kind == NK::ListExpr) bind(static_cast<ListExpr*>(tgt), v);
            else {
                // A coercion-typed slot converts what it is handed, as the scalar
                // path does for `my Int() $x = "42"`: `my ($before, Int() $linenr)
                // = $line.split(…)` (Backtrace::Files) wants the number, not the Str.
                if (!isBinding && tgt->kind == NK::VarExpr) {
                    const std::string& ct = static_cast<VarExpr*>(tgt)->declCoerce;
                    if (!ct.empty()) v = coerceToType(v, ct);
                }
                Value* lv = lvalue(tgt);
                // a TYPED slot checks what it is given, as `my Str $x = 3` does:
                // `my (Str $x) = 3` dies
                if (!isBinding && tgt->kind == NK::VarExpr && !static_cast<VarExpr*>(tgt)->declType.empty()) {
                    const std::string& nm = static_cast<VarExpr*>(tgt)->name;
                    if (!nm.empty() && nm[0] == '$') {
                        enforceTypedAssign(nm, v);
                        lv = lvalue(tgt);
                    }
                }
                // A native container keeps its width across the store, exactly as
                // the scalar path does: `my uint32 ($a, $b) = …` has to wrap at 32
                // bits, and overwriting the Value outright threw the width away.
                int nb = lv->natBits; bool ns = lv->natSigned, nf = lv->natFloat;
                // (a native NUM slot refuses a Str as a native int one does)
                if (nb && nf && v.t == VT::Str && !v.isAllomorph() && v.hashKind.empty())
                    throwTypedV("X::TypeCheck::Assignment",
                        {{"got", v}, {"expected", Value::typeObj("num")}},
                        "Type check failed in assignment; expected num but got Str (" + typeCheckRepr(v) + ")");
                if (nb && !isBinding && nativeRefusesKind(v, nf))   // `my num ($a, $b) = 1/2, 2e0`
                    throwNativeKind(v, nb, nf, ns, tgt->kind == NK::VarExpr
                                                   ? static_cast<VarExpr*>(tgt)->name : std::string("$x"));
                if (!nb && !isBinding && v.natBits) dropNativeTags(v);   // see dropNativeTags
                *lv = v;
                if (nb && (lv->t == VT::Str || (lv->t == VT::Int && lv->big())))
                    nativeAssignCheck(*lv, nb, nf, tgt->kind == NK::VarExpr
                                           ? static_cast<VarExpr*>(tgt)->name : std::string("$x"), ns);
                if (nb) wrapNative(*lv, nb, ns, nf);
            }
        }
    };
    bind(lst, rhs);
}
// `@a[|| @dims] = …` / `%h{|| <a b>, "c"}:delete` (6.e): the runtime list names
// ONE dimension per element, which is exactly a `;` multidim subscript. Present
// the subscript as that — each dimension bound to a hidden `$` variable, so a
// list-valued one still slices — run `f` through the ordinary multidim
// machinery, and put the `||` back.
Value Interpreter::withDimslipAsMultiDim(Index* ix, const std::function<Value()>& f) {
    // the dimensions: each element of the slipped list. `|| @dims` slips what
    // @dims holds; `|| (0,1), 1` slips the comma list that follows, whose
    // items are `(0,1)` and `1` — `@a[(0,1);1]`
    ValueList path;
    auto addSlip = [&](const Expr* e) {
        Value dims = eval(static_cast<const Unary*>(e)->operand.get());
        if (dims.t == VT::Array && dims.arr()) for (auto& d : *dims.arr()) path.push_back(d);
        else if (dims.t == VT::Range) for (auto& d : dims.flatten()) path.push_back(d);
        else path.push_back(dims);
    };
    if (ix->index->kind == NK::ListExpr) {
        for (auto& it : static_cast<ListExpr*>(ix->index.get())->items) {
            if (it->kind == NK::Unary && static_cast<Unary*>(it.get())->op == "dimslip")
                path.push_back(eval(static_cast<Unary*>(it.get())->operand.get()));
            else path.push_back(eval(it.get()));
        }
    }
    else addSlip(ix->index.get());
    auto le = std::make_unique<ListExpr>();
    le->semicolon = true;
    for (size_t k = 0; k < path.size(); k++) {
        std::string nm = "$\x01dimslip" + std::to_string(k);
        tctx_.cur->define(nm, path[k]);
        auto ve = std::make_unique<VarExpr>(nm);
        ve->line = ix->line;
        le->items.push_back(std::move(ve));
    }
    ExprPtr saved = std::move(ix->index);
    const bool savedMD = ix->multiDim;
    ix->index = std::move(le);
    ix->multiDim = true;
    struct Restore { Index* ix; ExprPtr& saved; bool md;
        ~Restore() { ix->index = std::move(saved); ix->multiDim = md; } } restore{ix, saved, savedMD};
    return f();
}
// Set ops that return a Bool (membership/subset/equality). Over a junction
// operand these COLLAPSE per the junction kind (like `==`/`eq`); the Set-valued
// producers ((|)/(&)/(-)/…) instead build a junction of Sets.
bool isSetPredicateStr(const std::string& o) {
    static const std::set<std::string> ops = {
        "(elem)", "∈", "(!elem)", "∉", "(cont)", "∋", "(!cont)", "∌",
        "(<=)", "⊆", "(<)", "⊂", "(>=)", "⊇", "(>)", "⊃", "(==)", "≡", "(!=)", "≢", "(<>)",
    };
    return ops.count(o) > 0;
}
// …and a TYPE OBJECT decides the flavour too, but only under `(+)`: `Mix (+) Mix`
// is a Mix holding the type object twice, while `Mix (|) Mix` is a plain Set and
// `Mix (.) Mix` a Bag, both holding it as an ordinary element. (Rakudo's
// candidate sets differ exactly there — `(+)` has a Mixy pair that an UNDEFINED
// Mix still matches; the others resolve through Any.)
static int settyTierOperand(const std::string& op, const Value& v);
// An unhandled Failure operand detonates when a set operator uses it.
static void setOpCheckFailure(const Value& v) {
    if (v.t == VT::Hash && v.hashKind == "Failure") {
        std::string msg = "Failure";
        if (v.hash()) { auto it = v.hash()->find("message"); if (it != v.hash()->end()) msg = it->second.toStr(); }
        throw RakuError{Value::typeObj("X::AdHoc"), msg};
    }
}
static void setRep(const std::string& k, const Value& v); // defined with setWrap below
// Coerce one operand to key => weight at the JOINT tier. A plain Hash coerces
// per tier: truthy-filtered membership at Set tier, numeric counts at Bag/Mix
// tier ({a => 42, b => 0} is set <a>, but bag (a => 42)). Mix tier keeps
// negative and fractional weights; Set/Bag drop non-positive ones.
std::map<std::string, double> setWeights(const Value& v, int tier) {
    std::map<std::string, double> m;
    // Only an ASSOCIATIVE hash contributes its entries. Plenty of objects are
    // hash-backed here without being one — an Attribute or Parameter
    // meta-object, a Date, a Failure — and each of those is a single ELEMENT:
    // Red's `%!relationships ∪= $attr` added the Attribute's four fields.
    const bool assocHash = v.t == VT::Hash && v.hash() &&
        (v.hashKind.empty() || v.hashKind == "Map" || v.hashKind == "Hash" || v.hashKind == "Stash" ||
         v.hashKind.rfind("Set", 0) == 0 || v.hashKind.rfind("Bag", 0) == 0 || v.hashKind.rfind("Mix", 0) == 0);
    if (assocHash) {
        bool isSetK = v.hashKind.find("Set") == 0;
        bool countK = settyTier(v) >= 1;
        for (auto& kv : *v.hash()) {
            double w;
            if (isSetK) w = 1;
            else if (countK) w = kv.second.toNum();
            else if (tier == 0) { if (!kv.second.truthy()) continue; w = 1; }
            else w = kv.second.toNum();
            if (tier == 2 ? w == 0 : w <= 0) continue;
            // an operand that is ALREADY a quanthash carries its elements in the
            // counts' pairKey; forward them so the result can render them too
            // …and an OBJECT HASH indexes by identity, so its index is not an
            // element key: ask the element itself, or `%h{Any} (|) set(…)` came
            // out with two keys for one element and compared unequal to the
            // same set built any other way.
            // (an object hash may instead keep the key in its objKey table)
            const Value* ok = kv.second.elemKey() ? kv.second.elemKey().get()
                            : (!countK && !isSetK) ? v.hash()->objKey(kv.first) : nullptr;
            const std::string ek = (!countK && !isSetK && ok) ? baggyKeyStr(*ok) : kv.first;
            if (ok) setRep(ek, *ok);
            m[ek] += w;
        }
    } else if (v.t == VT::Array || v.t == VT::Range) {
        for (auto& x : v.flatten()) {
            if (x.t == VT::Pair) {
                // a pair in an uncoerced list carries its value as a weight; at
                // Set tier it collapses to presence (falsy pairs drop out), so
                // (:42a,:0b) is {a}, not a weighted map that would skew (^)
                double w = x.pairVal() ? x.pairVal()->toNum() : 0;
                if (tier == 0) { if (!(x.pairVal() && x.pairVal()->truthy())) continue; w = 1; }
                if (tier == 2 ? w == 0 : w <= 0) continue;
                m[x.s] += w;
            }
            // type objects ARE elements. They must key through baggyKeyStr like every
            // other element, or an operator result and a `set(…)` literal holding the
            // same type object end up with DIFFERENT keys and compare unequal.
            // At SET tier a repeated element is still just one element:
            // `(1,2,2,3) (-) (2,)` is Set(1 3), and `(1,1,2) (==) (1,2)` is
            // True. Counting occurrences here made every duplicate-bearing
            // list behave like a Bag in a plain set operation.
            else { std::string k = baggyKeyStr(x); setRep(k, x);
                   if (tier == 0) m[k] = 1; else m[k] += 1; }
        }
    } else if (v.t == VT::Pair) {
        double w = v.pairVal() ? v.pairVal()->toNum() : 0;
        if (tier == 0) { if (v.pairVal() && v.pairVal()->truthy()) m[v.s] = 1; }
        else if (!(tier == 2 ? w == 0 : w <= 0)) m[v.s] = w;
    } else if (v.t == VT::Type || v.t == VT::Any) {
        std::string k = baggyKeyStr(v); setRep(k, v);
        m[k] = 1; // (Set) (&) (Set) — the type object is a one-element set
    } else {
        // Nil is an ELEMENT too, like every other type object: `Nil (|) 1` is
        // Set(1 Nil) in Rakudo, not Set(1). Excluding it here also made the
        // scalar case disagree with the list case just above, which has always
        // kept a Nil inside a list.
        std::string k = baggyKeyStr(v); setRep(k, v); m[k] = 1;
    }
    return m;
}
// The ELEMENT behind each key seen while coercing operands. Quanthash keys are
// identity strings (`Int|42`), not renderings, so the original value has to travel
// alongside or the result would be a set of identity strings. Scoped to one
// operator evaluation: setWeights adds, setWrap reads.
static thread_local std::map<std::string, Value> g_setReps;
static void setRep(const std::string& k, const Value& v) {
    // identity keys are deterministic, so a stale entry is never WRONG — but a
    // long-running program would grow this forever, so cap it
    if (g_setReps.size() > 4096) g_setReps.clear();
    g_setReps.emplace(k, v);
}
// Is this operand a QuantHash at all, and is it the MUTABLE flavour? A set
// operator answers the mutable type when its LEFT operand is mutable —
// `SetHash (|) Set` is a SetHash, `Set (|) SetHash` a Set — which is how
// Rakudo decides it, and rakupp always answered the immutable one.
static bool isQuantHashVal(const Value& v) {
    return v.t == VT::Hash && (v.hashKind == "Set" || v.hashKind == "SetHash" ||
                               v.hashKind == "Bag" || v.hashKind == "BagHash" ||
                               v.hashKind == "Mix" || v.hashKind == "MixHash");
}
// wrap a weight map as the tier's type (Set / Bag / Mix, or their Hash twins)
Value setWrap(const std::map<std::string, double>& res, int tier, bool mut) {
    Value h = Value::makeHash();
    h.hashKind = tier == 2 ? (mut ? "MixHash" : "Mix")
               : tier == 1 ? (mut ? "BagHash" : "Bag")
                           : (mut ? "SetHash" : "Set");
    for (auto& kv : res) {
        if (tier == 2 ? kv.second == 0 : kv.second <= 0) continue;
        Value cnt = tier == 0 ? Value::boolean(true)
                  : kv.second == (double)(long long)kv.second ? Value::integer((long long)kv.second)
                                                              : Value::number(kv.second);
        auto rp = g_setReps.find(kv.first);
        if (rp != g_setReps.end()) {
            // a plain Str keys on its own content and needs no carried element
            const Value& e = rp->second;
            if (!(e.t == VT::Str && e.hashKind.empty() && e.enumName.empty() && !e.isAllomorph()))
                cnt.pairKeyM() = std::make_shared<Value>(e);
        }
        (*h.hash())[kv.first] = std::move(cnt);
    }
    return h;
}
static int settyTierOperand(const std::string& op, const Value& v) {
    int t = settyTier(v);
    if (t == 0 && v.t == VT::Type && (op == "(+)" || op == "\xE2\x8A\x8E")) {
        if (v.s == "Mix" || v.s == "MixHash") return 2;
        if (v.s == "Bag" || v.s == "BagHash") return 1;
    }
    return t;
}
Value setOp(const std::string& op, const Value& l, const Value& r) {
    setOpCheckFailure(l); setOpCheckFailure(r);
    // membership against a RANGE is an arithmetic bounds check — no
    // materialization, so 0..10**42 (and open-ended ranges) work
    auto rangeHas = [](const Value& rng, const Value& x) -> bool {
        if (rng.ofType() == "Str") { // Str range: string ordering between endpoints
            const std::string v = x.toStr();
            const RangeEnds* sre = rangeEnds(rng);
            const std::string lo = sre ? sre->from.toStr() : cpToU8((uint32_t)rng.rFrom());
            const std::string hi = sre ? sre->to.toStr() : cpToU8((uint32_t)rng.rTo());
            return (rng.rExFrom() ? v > lo : v >= lo) &&
                   (rng.rExTo() ? v < hi : v <= hi);
        }
        // A BIGINT endpoint does not fit `i`, which saturates to the sentinel the
        // endless test uses — so `0 .. 10**42` read as unbounded above and held
        // every larger number. The carried endpoint objects answer exactly.
        if (const RangeEnds* re = rangeEnds(rng)) {
            if (x.isNumeric() && re->from.isNumeric() && re->to.isNumeric()) {
                if (!applyArith(rng.rExFrom() ? ">" : ">=", x, re->from).truthy()) return false;
                return applyArith(rng.rExTo() ? "<" : "<=", x, re->to).truthy();
            }
        }
        double v = x.toNum();
        double lo = (double)rng.rFrom() + (rng.rExFrom() ? 1 : 0);
        if (rng.rTo() >= 9000000000000000000LL) return v >= lo; // huge/unbounded top
        double hi = (double)rng.rTo() - (rng.rExTo() ? 1 : 0);
        return v >= lo && v <= hi;
    };
    // MEMBERSHIP is an IDENTITY question — `'123' (elem) [123]` is False, the
    // Str and the Int are different elements — while the quanthash STORAGE keys
    // stay renderings (changing those was tried and costs ~700 roast assertions
    // until Hash keys carry real objects). So the membership tests walk the
    // operand's ELEMENTS and compare `.WHICH`, instead of asking the rendering-
    // keyed weight map. DSL::Shared's fuzzy matcher gates on exactly this: its
    // `(elem)` shortcut fired for a numeric word and skipped the regex walk.
    auto hasElem = [&](const Value& hay, const Value& needle) -> bool {
        const std::string want = whichOf(needle);
        auto elemMatches = [&](const Value& el) { return whichOf(el) == want; };
        if (hay.t == VT::Hash && hay.hash()) {
            bool setK = settyTier(hay) == 0 && !hay.hashKind.empty();
            for (auto& kv : *hay.hash()) {
                double w = hay.hashKind.empty() || setK ? (kv.second.truthy() ? 1.0 : 0.0)
                                                        : kv.second.toNum();
                if (w == 0) continue;
                // An OBJECT-KEYED hash stores its keys as strings (the standing
                // object-hash limitation) — the Int 13 in `my %h{Any}` arrives
                // here as "13", so identity comparison against the real Int can
                // only compare renderings until keys carry objects.
                bool keyShaped = hay.objKeyed ||
                                 hay.ofType().find(',') != std::string::npos; // Hash[V,K]
                if (keyShaped && !kv.second.elemKey()) {
                    // …and when the hash keys by IDENTITY the stored index IS the
                    // identity string, so ask for that one rather than the rendering:
                    // `%h{Any}` holding the Int 13 indexes it "Int|13", which no
                    // amount of stringifying the needle will ever equal.
                    if (kv.first == (hay.objKeyed ? objHashIndex(needle) : needle.toStr())) return true;
                    continue;
                }
                Value el = kv.second.elemKey() ? *kv.second.elemKey() : Value::str(kv.first);
                if (elemMatches(el)) return true;
            }
            return false;
        }
        if (hay.t == VT::Array || hay.t == VT::Range) {
            for (auto& x : hay.flatten()) {
                if (x.t == VT::Pair) { // pair→weight reading, as setWeights has it
                    if (!(x.pairVal() && x.pairVal()->truthy())) continue;
                    Value el = x.pairKey() ? *x.pairKey() : Value::str(x.s);
                    if (elemMatches(el)) return true;
                }
                else if (elemMatches(x)) return true;
            }
            return false;
        }
        if (hay.t == VT::Pair)
            return hay.pairVal() && hay.pairVal()->truthy() &&
                   elemMatches(hay.pairKey() ? *hay.pairKey() : Value::str(hay.s));
        return elemMatches(hay); // a scalar is a one-element set
    };
    if (op == "(elem)" || op == "∈" || op == "(!elem)" || op == "∉") {
        bool neg = (op == "(!elem)" || op == "∉");
        if (r.t == VT::Range) return Value::boolean(neg ? !rangeHas(r, l) : rangeHas(r, l));
        if (lazySetOperand(r)) throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + op + " a lazy list"};
        bool in = hasElem(r, l);
        return Value::boolean(neg ? !in : in);
    }
    if (op == "(cont)" || op == "∋" || op == "(!cont)" || op == "∌") {
        bool neg = (op == "(!cont)" || op == "∌");
        if (l.t == VT::Range) return Value::boolean(neg ? !rangeHas(l, r) : rangeHas(l, r));
        if (lazySetOperand(l)) throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + op + " a lazy list"};
        bool in = hasElem(l, r);
        return Value::boolean(neg ? !in : in);
    }
    if (lazySetOperand(l) || lazySetOperand(r))
        throw RakuError{Value::typeObj("X::Cannot::Lazy"),
                        "Cannot " + op + " a lazy list"};
    // joint tier: Mixy > Baggy > Setty; (+) and (.) are Baggy at minimum
    int tier = std::max({settyTierOperand(op, l), settyTierOperand(op, r), setOpMinTier(op)});
    auto a = setWeights(l, tier), b = setWeights(r, tier);
    auto at = [](std::map<std::string, double>& m, const std::string& k) { return m.count(k) ? m[k] : 0.0; };
    if (op == "(<=)" || op == "⊆" || op == "(<)" || op == "⊂" || op == "(>=)" || op == "⊇" ||
        op == "(>)" || op == "⊃" || op == "(==)" || op == "≡" ||
        op == "(!=)" || op == "≢" || op == "(<>)") {
        // Over the UNION of the keys, not each side's own: a Mix weight can be
        // NEGATIVE, and a key absent from a set has the virtual weight 0, which
        // is then GREATER than the weight the other side carries. Walking only
        // each side's own keys missed exactly that case, so `mix() (<=)
        // (a => -1).Mix` said True where 0 ≤ -1 is plainly False.
        bool aSubB = true, bSubA = true;
        std::set<std::string> keys;
        for (auto& kv : a) keys.insert(kv.first);
        for (auto& kv : b) keys.insert(kv.first);
        for (auto& k : keys) {
            double av = at(a, k), bv = at(b, k);
            if (av > bv) aSubB = false;
            if (bv > av) bSubA = false;
            if (!aSubB && !bSubA) break;
        }
        bool eq = aSubB && bSubA;
        if (op == "(==)" || op == "≡") return Value::boolean(eq);
        if (op == "(!=)" || op == "≢" || op == "(<>)") return Value::boolean(!eq);
        if (op == "(<=)" || op == "⊆") return Value::boolean(aSubB);
        if (op == "(>=)" || op == "⊇") return Value::boolean(bSubA);
        if (op == "(<)" || op == "⊂") return Value::boolean(aSubB && !eq);
        return Value::boolean(bSubA && !eq); // (>) ⊃
    }
    std::map<std::string, double> res;
    // (a key only one operand has keeps ITS weight — a Mix's negative one too)
    if (op == "(|)" || op == "∪") { res = a; for (auto& kv : b) res[kv.first] = res.count(kv.first) ? std::max(res[kv.first], kv.second) : kv.second; }
    else if (op == "(&)" || op == "∩") { for (auto& kv : a) if (b.count(kv.first)) res[kv.first] = std::min(kv.second, b[kv.first]); }
    else if (op == "(-)" || op == "∖") {
        if (tier == 2) { res = a; for (auto& kv : b) res[kv.first] = at(res, kv.first) - kv.second; } // Mix keeps negatives
        else for (auto& kv : a) { double d = kv.second - at(b, kv.first); if (d > 0) res[kv.first] = d; }
    }
    else if (op == "(^)" || op == "⊖") {
        // symmetric difference is the WEIGHT difference at every tier:
        // Bag(a b(2)) (^) Bag(a b) is bag(b) — for Sets it degenerates to
        // keys-in-exactly-one (|1-1| = 0 drops shared keys)
        for (auto& kv : a) { double d = kv.second - at(b, kv.first); res[kv.first] = d < 0 ? -d : d; }
        for (auto& kv : b) if (!a.count(kv.first)) res[kv.first] = kv.second;
    }
    else if (op == "(+)" || op == "⊎") { res = a; for (auto& kv : b) res[kv.first] += kv.second; }
    else if (op == "(.)" || op == "⊍") { for (auto& kv : a) if (b.count(kv.first)) res[kv.first] = kv.second * b[kv.first]; }
    // MUTABILITY follows the LEFT operand — except symmetric difference, whose
    // candidates are typed on BOTH sides, so `SetHash (^) <a b>` is a plain Set.
    bool mut = isMutableQuantHash(l) &&
               (!(op == "(^)" || op == "\xE2\x8A\x96") || isQuantHashVal(r));
    return setWrap(res, tier, mut);
}
// Multi-arg symmetric difference. Rakudo's (^)/⊖ is a genuine list operator, not
// a left fold: for each key the result weight is (largest − second-largest) over
// the operands' weights, where an operand lacking the key contributes 0. This
// reduces to |a−b| for two operands but diverges from a pairwise fold for three
// or more (e.g. Bag(a×42) ⊖ Bag(a×7) ⊖ Bag(a×43) is a×1, not a×8).
Value setSymDiffN(const ValueList& operands) {
    for (auto& o : operands) {
        setOpCheckFailure(o);
        if (lazySetOperand(o))
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot (^) a lazy list"};
    }
    int tier = setOpMinTier("(^)");
    for (auto& o : operands) tier = std::max(tier, settyTier(o));
    std::vector<std::map<std::string, double>> ws;
    ws.reserve(operands.size());
    std::set<std::string> keys;
    for (auto& o : operands) { ws.push_back(setWeights(o, tier)); for (auto& kv : ws.back()) keys.insert(kv.first); }
    std::map<std::string, double> res;
    const double NINF = -std::numeric_limits<double>::infinity();
    for (auto& k : keys) {
        // exactly one slot per operand: its weight for k, or 0 if it lacks k
        double t1 = NINF, t2 = NINF;
        for (auto& m : ws) {
            auto it = m.find(k);
            double w = it != m.end() ? it->second : 0.0;
            if (w > t1) { t2 = t1; t1 = w; }
            else if (w > t2) { t2 = w; }
        }
        res[k] = t1 - t2;
    }
    // The FIRST operand decides mutability — but at SETTY tier it only decides
    // when every operand is itself a QuantHash: `SetHash (^) <b c>` is a plain
    // Set while `SetHash (^) <b c>.Set` is a SetHash. From Baggy up the first
    // operand decides alone, so `BagHash (^) <a b c d>` stays a BagHash. (That
    // asymmetry is Rakudo's candidate set, measured operator by operator.)
    bool mut = !operands.empty() && isMutableQuantHash(operands[0]);
    if (mut && tier == 0) for (auto& o : operands) if (!isQuantHashVal(o)) { mut = false; break; }
    return setWrap(res, tier, mut);
}
// A set operator is N-ARY, and the tier of its answer is the JOINT tier of
// EVERY operand — not of each adjacent pair a left fold happens to see. `(-)`
// over (Set, Set, Mix) is Mixy from the start, so the first step must keep the
// keys whose weight has gone to zero or below: `[(-)] <a b c>, <c d e>, <e f>.Mix`
// ends with d at -1 and e at -2, and a Setty first step had already dropped both.
// Lifting every operand to the joint tier before folding is exactly that rule.
// Only for a MIXED-tier fold of three or more operands; std::nullopt means the
// caller's own left fold is already right. Two operands see their joint tier
// anyway — the pairwise operator computes exactly this — and so does a fold whose
// operands all share a tier; lifting there would only lose what the pairwise
// rules carry (the mutable flavour, the lazy-operand refusal).
std::optional<Value> setOpFoldN(const std::string& op, const ValueList& items) {
    if (items.size() <= 2) return std::nullopt;
    int tier = setOpMinTier(op), first = settyTier(items[0]);
    bool mixed = false;
    for (auto& v : items) { int t = settyTier(v); if (t != first) mixed = true; tier = std::max(tier, t); }
    if (!mixed) return std::nullopt;
    for (auto& v : items) {
        setOpCheckFailure(v);
        if (lazySetOperand(v))
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + op + " a lazy list"};
    }
    // A non-QuantHash operand reads at ITS OWN tier and is then re-wrapped: a
    // plain Hash is Setty, so its VALUES are truthiness and not weights.
    // `infix:<(-)>({:a<a>}, …, <e f>.Mix)` counts a once; reading "a" as a Mix
    // weight instead tried to numify it.
    auto lift = [tier, &op](const Value& v) {
        int own = std::max(settyTier(v), setOpMinTier(op));
        return own >= tier ? v : setWrap(setWeights(v, own), tier, isMutableQuantHash(v));
    };
    Value acc = lift(items[0]);
    for (size_t k = 1; k < items.size(); k++) acc = setOp(op, acc, lift(items[k]));
    return acc;
}

// The type object of an enum is the tagged list of its pairs (see the enum
// declaration arm); a member VALUE carries the same tag but is not a list.
bool isEnumTypeObject(const Value& v) {
    return v.t == VT::Array && !v.enumType.empty() && v.enumName.empty() && !v.itemized &&
           g_revInterp && g_revInterp->enumPairs_.count(std::string(v.enumType.str())) &&
           g_revInterp->enumPairs_[std::string(v.enumType.str())].arr() == v.arr();
}

// The repetition count of `xx`. `*` and `+Inf` are legitimate — they ask for an
// endless list — but `NaN` and `-Inf` are not counts at all, and Rakudo refuses
// them with the same X::Numeric::CannotConvert that `Int(NaN)` gives.
void rtXxCountCheck(const Value& v) {
    if (v.t != VT::Num) return;
    if (std::isnan(v.n) || (std::isinf(v.n) && v.n < 0))
        throw RakuError{Value::typeObj("X::Numeric::CannotConvert"),
                        "Cannot convert " + v.toStr() + " to Int"};
}
// Is `op` the reverse metaop over a WORD base — `Rcmp`, `Rdiv`, `Rmin`? The
// symbolic forms are recognised by the non-alphanumeric character after the R,
// but a word base is alphanumeric and would make every identifier starting with
// R (Range, Rat…) look like one, so the bases are listed rather than guessed.
bool reverseWordOp(const std::string& op) {
    static const std::set<std::string> bases = {
        "cmp", "leg", "eqv", "eq", "ne", "lt", "gt", "le", "ge", "before", "after",
        "unicmp", "coll", "div", "mod", "gcd", "lcm", "min", "max", "minmax", "x", "xx",
        "and", "or", "xor", "andthen", "orelse", "notandthen"};
    // the Rs STACK: `RRxx` is a real (if pointless) spelling, and each one
    // reverses again — so count the run rather than looking at just the first
    size_t i = 0; while (i < op.size() && op[i] == 'R') i++;
    if (i == 0 || i >= op.size()) return false;
    // A SYMBOLIC base after two or more Rs (`RR-`, `RRR+`) is a reverse metaop
    // too; the one-R symbolic form is recognised by its own test at each call
    // site, which looks only at the character after the R.
    if (!ascii::isalnum((unsigned char)op[i])) return i >= 2;
    return bases.count(op.substr(i)) > 0;
}

// In-place `*=` on a BigInt accumulator. Every guard here is a field that
// `dst = Value::bigint(...)` would have RESET and an in-place mutation would
// instead preserve: an enum identity, a native width, a readonly binding, an
// itemized/Buf tag, a payload slot, or any other cold-block member. Sole
// ownership is two counts, not one — one Value pointing at this cold block, and
// one cold block pointing at this magnitude — because ValueExt is copy-on-write,
// so a shared block hands the same BigInt to a Value that never asked to be
// changed.
bool rtMulAssignBig(Value& dst, const Value& r) {
    // NOT while worker threads are live. `dst = applyArith(...)` already races
    // there — a torn Value copy — but writing over the magnitude is strictly
    // worse: growing it reallocates, so a racing reader that already loaded
    // mag.data() reads freed memory rather than a stale-but-valid limb, and
    // use_count() is itself a check a sibling thread can invalidate before the
    // write. Same predicate ParStripe engages on, for the same reason.
    if (g_cbInterp && g_cbInterp->parallelMode_ &&
        g_cbInterp->liveWorkers_.load(std::memory_order_relaxed) > 0) return false;
    // Sole owner in BOTH dimensions, and each catches a real aliasing shape the
    // other misses. A copy of the Value shares the cold block, so `x.big` stays
    // at one use while two Values plainly reach the magnitude; and a Range built
    // over a big endpoint aliases the SAME shared_ptr<BigInt> into a different
    // cold block (`r.bigM() = to.big()`), so the block can be unshared while the
    // magnitude is not.
    if (dst.x_.use_count() != 1) return false;
    ValueExt& x = *dst.x_;
    if (!x.big || x.big.use_count() != 1) return false;
    if (!dst.enumName.empty() || !dst.enumType.empty() || !dst.hashKind.empty() ||
        dst.itemized || dst.readonly || dst.natBits || dst.p_ || dst.isList ||
        dst.objKeyed || dst.namedArg)
        return false;
    if (x.ratN || x.ratD || x.pairKey || x.elemDefault || x.ext || x.shape || !x.ofType.empty())
        return false;
    const long long m = r.i;
    if (m == 0) { dst = Value::integer(0); return true; }
    // A multiplier past one limb needs the general schoolbook product; only the
    // one-limb case can be written back over the accumulator in a single pass.
    unsigned long long um = m < 0 ? (unsigned long long)(-(m + 1)) + 1ull : (unsigned long long)m;
    if (um >= BigInt::BASE) return false;
    BigInt& b = *x.big;
    b.mulLimbInPlace((uint32_t)um);
    if (m < 0) b.sign = -b.sign;
    // |b| only grew, so it cannot have fallen back inside a machine word — but
    // Value::bigint checks, so this checks too, and the two stay interchangeable.
    if (b.fitsLL()) dst = Value::integer(b.toLL());
    return true;
}
bool objMethodStrHook(const Value& v, const char* method, std::string& out) {
    // a user class's TYPE OBJECT gists by the class's own method too
    if (g_revInterp && v.t == VT::Type && !v.s.empty() && std::strcmp(method, "gist") == 0) {
        auto ci = g_revInterp->classes_.find(v.s);
        if (ci == g_revInterp->classes_.end() || !ci->second || ci->second->isRole) return false;
        Value* m = ci->second->findMethod(method);
        if (!m || m->t != VT::Code || !m->code() || m->code()->builtin) return false;
        static thread_local int tdepth = 0;
        if (tdepth > 8) return false;
        struct D { D() { tdepth++; } ~D() { tdepth--; } } d;
        ValueList none;
        try { out = g_revInterp->invokeMethod(*m, v, none).toStr(); }
        catch (...) { return false; }
        return true;
    }
    if (!g_revInterp || v.t != VT::Object || !v.obj() || !v.obj()->cls) return false;
    Value* m = v.obj()->cls->findMethod(method);
    if (!m || m->t != VT::Code || !m->code() || m->code()->builtin) return false;
    static thread_local int depth = 0;
    if (depth > 8) return false;
    struct D { D() { depth++; } ~D() { depth--; } } d;
    ValueList none;
    try { out = g_revInterp->invokeMethod(*m, v, none).toStr(); }
    catch (...) { return false; }
    return true;
}
static const bool g_objMethodStrInstalled = ((g_objMethodStr = &objMethodStrHook), true);
// `f o g` / `f ∘ g`: a callable computing f(g(…)). When f takes several
// arguments, g's result list is SLIPPED into it (`(* + *) o { $_ + 7, $_ * 6 }`
// — Rakudo's `f |g |args`); the composition takes what g takes (a 2-ary g maps
// two at a time) and returns what f returns.
Value composeCode(const Value& fV, const Value& gV) {
    Value code; code.t = VT::Code; code.setCode(makePayload<Callable>());
    bool slip = false;
    if (fV.t == VT::Code && g_revInterp) {
        ValueList none;
        try { slip = g_revInterp->methodCall(fV, "count", none).toNum() > 1; } catch (...) {}
    }
    code.code()->builtin = [fV, gV, slip](Interpreter& I, ValueList& a) -> Value {
        Value gr = I.callCallable(gV, a);
        if (slip && (gr.t == VT::Array || gr.t == VT::Range) && !gr.itemized)
            return I.callCallable(fV, gr.flatten());
        return I.callCallable(fV, ValueList{ gr });
    };
    if (gV.t == VT::Code && gV.code()) {
        code.code()->params = gV.code()->params;
        code.code()->placeholders = gV.code()->placeholders;
        if (gV.code()->isWhateverCode && gV.code()->whateverArity > 1)
            code.code()->whateverArity = gV.code()->whateverArity;
    }
    if (fV.t == VT::Code && fV.code()) code.code()->retType = fV.code()->retType;
    return code;
}

// Whether a `where` expression reads only what its declaration scope gives
// it — so the Code it makes can be built once there instead of in a fresh
// scope holding `self`, the earlier parameters and `$_` at every check. A
// whitelist over node kinds: anything not named here (a regex, a symbolic
// reference, a declaration, a statement other than an expression) answers
// no. `inBlock` is true inside a block literal, which reads its variables
// when it runs and binds its own `$_`; outside one, only literals, `*`,
// type names and operators over them.
static bool whereReadsStatic(const Expr* e, const std::vector<Param>& sig, bool inBlock) {
    if (!e) return true;
    auto paramNamed = [&](const std::string& n) {
        for (auto& p : sig) {
            if (p.name == n || (!p.captureName.empty() && p.captureName == n)) return true;
            if (p.typeCapture && p.type == n) return true;
        }
        return false;
    };
    auto all = [&](const std::vector<ExprPtr>& v) {
        for (auto& x : v) if (!whereReadsStatic(x.get(), sig, inBlock)) return false;
        return true;
    };
    switch (e->kind) {
        case NK::IntLit: case NK::NumLit: case NK::StrLit: case NK::BoolLit:
        case NK::AllomorphLit: case NK::Whatever:
            return true;
        case NK::VarExpr: {
            // Outside a block literal a variable is read when the WhateverCode
            // is MADE (`* > $lim` keeps $lim's value then), so a Code built
            // once would keep a stale one: only a block reads it per call.
            if (!inBlock) return false;
            auto* v = static_cast<const VarExpr*>(e);
            const std::string& n = v->name;
            if (v->declare || n.size() < 2 || paramNamed(n)) return false;
            if (n[1] == '!' || n[1] == '.') return false;          // an attribute: self's
            return true;
        }
        case NK::NameTerm: {
            const std::string& n = static_cast<const NameTerm*>(e)->name;
            return n != "self" && !paramNamed(n) && n.rfind("::?", 0) != 0 && n.rfind("&?", 0) != 0;
        }
        case NK::Binary: { auto* b = static_cast<const Binary*>(e);
            return whereReadsStatic(b->lhs.get(), sig, inBlock) && whereReadsStatic(b->rhs.get(), sig, inBlock); }
        case NK::Unary: {
            auto* u = static_cast<const Unary*>(e);
            static const char* const kOk[] = {"-", "+", "!", "?", "~", "not", "so", "^", "+^", "~^", "?^", "++", "--"};
            bool ok = false;
            for (const char* k : kOk) if (u->op == k) { ok = true; break; }
            return ok && whereReadsStatic(u->operand.get(), sig, inBlock);
        }
        case NK::ChainExpr: return all(static_cast<const ChainExpr*>(e)->operands);
        case NK::Ternary: { auto* t = static_cast<const Ternary*>(e);
            return whereReadsStatic(t->cond.get(), sig, inBlock) && whereReadsStatic(t->then.get(), sig, inBlock) &&
                   whereReadsStatic(t->els.get(), sig, inBlock); }
        case NK::Range: { auto* r = static_cast<const RangeExpr*>(e);
            return whereReadsStatic(r->from.get(), sig, inBlock) && whereReadsStatic(r->to.get(), sig, inBlock); }
        case NK::Pair: { auto* p = static_cast<const PairExpr*>(e);
            return whereReadsStatic(p->keyExpr.get(), sig, inBlock) && whereReadsStatic(p->value.get(), sig, inBlock); }
        case NK::ListExpr: return all(static_cast<const ListExpr*>(e)->items);
        case NK::ArrayLit: return all(static_cast<const ArrayLit*>(e)->items);
        case NK::HashLit: return all(static_cast<const HashLit*>(e)->items);
        case NK::InterpStr: return all(static_cast<const InterpStr*>(e)->parts);
        case NK::Index: { auto* i = static_cast<const Index*>(e);
            return whereReadsStatic(i->base.get(), sig, inBlock) && whereReadsStatic(i->index.get(), sig, inBlock); }
        case NK::MethodCall: { auto* m = static_cast<const MethodCall*>(e);
            return !m->methodExpr && (inBlock || m->inv) && whereReadsStatic(m->inv.get(), sig, inBlock) &&
                   all(m->args); }
        case NK::Call: {
            if (!inBlock) return false;   // (evaluated when the WhateverCode is made, as a variable is)
            auto* c = static_cast<const Call*>(e);
            static const char* const kNo[] = {"EVAL", "EVALFALLBACK", "callsame", "callwith", "nextsame",
                                              "nextwith", "samewith", "nextcallee", "lastcall", "callframe",
                                              "return", "temp", "let"};
            for (const char* k : kNo) if (c->name == k) return false;
            return whereReadsStatic(c->callee.get(), sig, inBlock) && all(c->args);
        }
        case NK::BlockExpr: {
            auto* b = static_cast<const BlockExpr*>(e);
            if (b->isSub || b->isMethodTerm || !b->phaser.empty()) return false;
            for (auto& bp : b->params)
                if (bp.defaultVal || bp.whereExpr || bp.subSig || bp.codeSig) return false;
            for (auto& st : b->body) {
                if (st->kind != NK::ExprStmt) return false;
                if (!whereReadsStatic(static_cast<const ExprStmt*>(st.get())->e.get(), sig, true)) return false;
            }
            return true;
        }
        default: return false;
    }
}

namespace {
struct WhereCodeCache { const Env* scope; Value code; };
}

// The Code a parameter's `where` makes, built once for the declaration scope
// `scope` and reused by every later check from it — or null, when the `where`
// reads something per call (whereReadsStatic), does not make a Code, or was
// built for another scope (a candidate recreated by its enclosing routine).
// Built in a scope of its own under `scope`, so the Code closes over nothing a
// call supplies; the cache keeps that scope, and so `scope`, alive.
const Value* Interpreter::staticWhereCode(const Param& p, const std::vector<Param>& sig,
                                          const std::shared_ptr<Env>& scope) {
    signed char st = p.whereStatic;
    if (st < 0) p.whereStatic = st = whereReadsStatic(p.whereExpr.get(), sig, false) ? 1 : 0;
    if (st == 0 || !scope) return nullptr;
    if (auto* c = static_cast<const WhereCodeCache*>(p.whereCode.get()))
        return c->scope == scope.get() ? &c->code : nullptr;
    auto env = std::make_shared<Env>();
    env->parent = scope;
    auto saved = tctx_.cur; tctx_.cur = env;
    Value cv;
    try { cv = eval(p.whereExpr.get()); } catch (...) { tctx_.cur = saved; p.whereStatic = 0; return nullptr; }
    tctx_.cur = saved;
    if (!(cv.t == VT::Code && cv.code() && (cv.code()->isWhateverCode || cv.code()->isBlock))) {
        p.whereStatic = 0;
        return nullptr;
    }
    auto* mine = new WhereCodeCache{scope.get(), std::move(cv)};
    auto* won = static_cast<const WhereCodeCache*>(p.whereCode.publish(mine));
    if (won != mine) delete mine;
    return won->scope == scope.get() ? &won->code : nullptr;
}

// --- the multi-dispatch cache (INTERP-SPEED-PLAN tier 1, item 4) ------------
// A dispatcher remembers the argument shape of one dispatch and the candidate
// that won it, and a later call with the same shape goes straight to that
// candidate. Sound only where the winner cannot depend on anything but the
// shape, so both sides are narrow:
//   the arguments — each a plain Int, Num or Str (no tag, enum, allomorph or
//   native) or an instance of a class, keyed by its kind and its ClassInfo;
//   the candidates — every parameter a `$` positional typed by a name whose
//   match depends only on that key (an unconstrained one, a core numeric or
//   string type, a user class), with no `where`, literal, coercion, capture,
//   sub-signature, default, `is rw` or native type.
// The entry is published once and names its candidate by index; it is good
// while the candidate count and the symbol generation are what they were.
extern std::atomic<uint64_t> g_symbolGen;
namespace {
struct DispatchCacheEntry {
    uint64_t gen; uint32_t ncand; uint32_t nkey; uint32_t best; uint64_t key[8];
};
// One argument's key: its kind (and definedness), and its class. False when
// its match may depend on more than that.
inline bool dispatchArgKey(const Value& v, uint64_t& k0, uint64_t& k1) {
    if (!v.hashKind.empty() || !v.enumName.empty() || !v.enumType.empty()) return false;
    switch (v.t) {
        case VT::Int: case VT::Num: case VT::Str:
            if (v.isAllomorph()) return false;
            // a native's width, sign and kind are part of its shape
            k0 = (uint64_t)v.t | (uint64_t)v.natBits << 8 | (uint64_t)v.natSigned << 16 |
                 (uint64_t)v.natFloat << 17;
            k1 = 0;
            return true;
        case VT::Object:
            if (!v.obj() || !v.obj()->cls || v.obj()->hasBoxed) return false;
            k0 = (uint64_t)v.t; k1 = (uint64_t)(uintptr_t)v.obj()->cls.get(); return true;
        default: return false;
    }
}
inline bool dispatchKey(const Value* self, const ValueList& as, uint64_t* key, uint32_t& n) {
    n = 0;
    if (as.size() + (self ? 1 : 0) > 4) return false;
    if (self && !dispatchArgKey(*self, key[0], key[1])) return false;
    if (self) n = 2;
    for (auto& a : as) {
        if (a.t == VT::Pair && a.namedArg) return false;
        if (!dispatchArgKey(a, key[n], key[n + 1])) return false;
        n += 2;
    }
    return true;
}
}  // namespace

Callable::DispatchCacheSlot::~DispatchCacheSlot() {
    delete static_cast<const DispatchCacheEntry*>(p.load(std::memory_order_relaxed));
}

// Whether every candidate's parameters are ones a cached answer can stand for.
template <typename Classes, typename Subsets>
static bool dispatchCandidatesCacheable(const Callable& c, const Classes& classes, const Subsets& subsets) {
    static const std::set<std::string> kNominal = {"", "Any", "Mu", "Int", "Str", "Num", "Real",
                                                   "Numeric", "Cool", "Stringy"};
    for (auto& cand : c.candidates) {
        const Callable* cc = cand.code();
        if (!cc) return false;
        if (cc->isProto || cc->isProtoBody) continue;
        if (!cc->params || cc->isDefaultCand || !cc->wrappers.empty()) return false;
        for (auto& p : *cc->params) {
            if (p.slurpy && p.sigil == '%') continue;           // a method's implicit *%_
            if (p.named || p.slurpy || p.sigil != '$' || p.whereExpr || p.hadWhere || p.litVal ||
                p.subSig || p.codeSig || p.coerce || p.typeCapture || p.isRw || p.defaultVal ||
                p.optional || !p.shapeDims.empty())
                return false;
            if (kNominal.count(p.type)) continue;
            auto it = classes.find(p.type);
            if (it == classes.end() || !it->second || it->second->isRole || subsets.count(p.type) ||
                p.type.find('[') != std::string::npos)
                return false;
        }
    }
    return true;
}

const Value* Interpreter::dispatchCacheLookup(Callable& c, const Value* self, const ValueList& as) {
    auto* e = static_cast<const DispatchCacheEntry*>(c.dispatchCache.p.load(std::memory_order_acquire));
    if (!e || e->ncand != c.candidates.size() || e->gen != g_symbolGen.load(std::memory_order_relaxed))
        return nullptr;
    uint64_t key[8]; uint32_t n;
    if (!dispatchKey(self, as, key, n) || n != e->nkey) return nullptr;
    for (uint32_t k = 0; k < n; k++) if (key[k] != e->key[k]) return nullptr;
    return &c.candidates[e->best];
}

void Interpreter::dispatchCacheStore(Callable& c, const Value* self, const ValueList& as, const Value* best) {
    if (c.dispatchCache.p.load(std::memory_order_relaxed)) return;   // published once
    const uint32_t ncand = (uint32_t)c.candidates.size();
    if (c.dispatchCacheable < 0 || c.dispatchCacheN != ncand) {
        c.dispatchCacheable = dispatchCandidatesCacheable(c, classes_, subsets_) ? 1 : 0;
        c.dispatchCacheN = ncand;
    }
    if (c.dispatchCacheable != 1) return;
    DispatchCacheEntry k{};
    if (!dispatchKey(self, as, k.key, k.nkey) || best < c.candidates.data() ||
        best >= c.candidates.data() + ncand)
        return;
    auto* e = new DispatchCacheEntry(k);
    e->gen = g_symbolGen.load(std::memory_order_relaxed);
    e->ncand = ncand;
    e->best = (uint32_t)(best - c.candidates.data());
    const void* expected = nullptr;
    if (!c.dispatchCache.p.compare_exchange_strong(expected, e, std::memory_order_release,
                                                    std::memory_order_relaxed))
        delete e;
}


// --- does a block read `@_`? ---------------------------------------------
// A block called with arguments got an `@_` of them on every call, though
// Rakudo gives one only to a block that mentions it. Whether this one can is
// decided once: a whitelist walk of the body, nested blocks included (one
// called with no arguments falls through to the enclosing `@_`), where any
// node it does not know — an EVAL, a symbolic reference, a regex or a
// substitution whose text names `@_`/`%_` — counts as reading it.
static bool exprMayReadAtArgs(const Expr* e);
static bool stmtsMayReadAtArgs(const std::vector<StmtPtr>& v);
static bool paramsMayReadAtArgs(const std::vector<Param>& ps) {
    for (auto& p : ps)
        if (exprMayReadAtArgs(p.defaultVal.get()) || exprMayReadAtArgs(p.whereExpr.get()) ||
            (p.subSig && paramsMayReadAtArgs(*p.subSig)))
            return true;
    return false;
}
static bool blockMayReadAtArgs(const Block* b) { return b && stmtsMayReadAtArgs(b->stmts); }
static bool textNamesAtArgs(const std::string& t) {
    return t.find("@_") != std::string::npos || t.find("%_") != std::string::npos;
}
static bool exprMayReadAtArgs(const Expr* e) {
    if (!e) return false;
    auto any = [](const std::vector<ExprPtr>& v) {
        for (auto& x : v) if (exprMayReadAtArgs(x.get())) return true;
        return false;
    };
    switch (e->kind) {
        case NK::IntLit: case NK::NumLit: case NK::StrLit: case NK::BoolLit: case NK::AllomorphLit:
        case NK::Whatever: case NK::NameTerm: case NK::SelfTerm:
            return false;
        case NK::VarExpr: {
            auto* v = static_cast<const VarExpr*>(e);
            if (v->name == "@_" || v->name == "%_") return true;
            return exprMayReadAtArgs(v->declDefault.get()) || exprMayReadAtArgs(v->declShape.get());
        }
        case NK::Binary: { auto* b = static_cast<const Binary*>(e);
            return exprMayReadAtArgs(b->lhs.get()) || exprMayReadAtArgs(b->rhs.get()); }
        case NK::Unary: return exprMayReadAtArgs(static_cast<const Unary*>(e)->operand.get());
        case NK::Assign: { auto* a = static_cast<const Assign*>(e);
            return exprMayReadAtArgs(a->target.get()) || exprMayReadAtArgs(a->value.get()); }
        case NK::Call: { auto* c = static_cast<const Call*>(e);
            if (c->name == "EVAL" || c->name == "EVALFALLBACK" || c->name == "evalbytes") return true;
            return exprMayReadAtArgs(c->callee.get()) || any(c->args); }
        case NK::MethodCall: { auto* m = static_cast<const MethodCall*>(e);
            if (m->method == "EVAL") return true;
            return exprMayReadAtArgs(m->inv.get()) || exprMayReadAtArgs(m->methodExpr.get()) || any(m->args); }
        case NK::Index: { auto* i = static_cast<const Index*>(e);
            return exprMayReadAtArgs(i->base.get()) || exprMayReadAtArgs(i->index.get()); }
        case NK::Ternary: { auto* t = static_cast<const Ternary*>(e);
            return exprMayReadAtArgs(t->cond.get()) || exprMayReadAtArgs(t->then.get()) ||
                   exprMayReadAtArgs(t->els.get()); }
        case NK::Range: { auto* r = static_cast<const RangeExpr*>(e);
            return exprMayReadAtArgs(r->from.get()) || exprMayReadAtArgs(r->to.get()); }
        case NK::Pair: { auto* p = static_cast<const PairExpr*>(e);
            return exprMayReadAtArgs(p->keyExpr.get()) || exprMayReadAtArgs(p->value.get()); }
        case NK::ListExpr: return any(static_cast<const ListExpr*>(e)->items);
        case NK::ArrayLit: return any(static_cast<const ArrayLit*>(e)->items);
        case NK::HashLit: return any(static_cast<const HashLit*>(e)->items);
        case NK::InterpStr: return any(static_cast<const InterpStr*>(e)->parts);
        case NK::ChainExpr: return any(static_cast<const ChainExpr*>(e)->operands);
        case NK::NqpOp: return any(static_cast<const NqpOp*>(e)->args);
        case NK::RegexLit: return textNamesAtArgs(static_cast<const RegexLit*>(e)->pattern);
        case NK::SubstLit: { auto* sl = static_cast<const SubstLit*>(e);
            return textNamesAtArgs(sl->pattern) || textNamesAtArgs(sl->repl); }
        case NK::BlockExpr: { auto* b = static_cast<const BlockExpr*>(e);
            return paramsMayReadAtArgs(b->params) || stmtsMayReadAtArgs(b->body); }
        default: return true;   // SymbolicRef and anything not listed
    }
}
static bool stmtMayReadAtArgs(const Stmt* s) {
    if (!s) return false;
    switch (s->kind) {
        case NK::ExprStmt: return exprMayReadAtArgs(static_cast<const ExprStmt*>(s)->e.get());
        case NK::ReturnStmt: return exprMayReadAtArgs(static_cast<const ReturnStmt*>(s)->value.get());
        case NK::Block: return blockMayReadAtArgs(static_cast<const Block*>(s));
        case NK::LastStmt: case NK::NextStmt: case NK::RedoStmt: case NK::EmptyStmt: return false;
        case NK::IfStmt: { auto* i = static_cast<const IfStmt*>(s);
            for (auto& br : i->branches)
                if (exprMayReadAtArgs(br.first.get()) || blockMayReadAtArgs(br.second.get())) return true;
            for (auto& bp : i->branchParams) if (paramsMayReadAtArgs(bp)) return true;
            return paramsMayReadAtArgs(i->elseParams) || blockMayReadAtArgs(i->elseBlock.get()); }
        case NK::WhileStmt: { auto* w = static_cast<const WhileStmt*>(s);
            return exprMayReadAtArgs(w->cond.get()) || paramsMayReadAtArgs(w->params) ||
                   blockMayReadAtArgs(w->body.get()); }
        case NK::RepeatStmt: { auto* r = static_cast<const RepeatStmt*>(s);
            return exprMayReadAtArgs(r->cond.get()) || blockMayReadAtArgs(r->body.get()); }
        case NK::ForStmt: { auto* f = static_cast<const ForStmt*>(s);
            return exprMayReadAtArgs(f->list.get()) || paramsMayReadAtArgs(f->params) ||
                   blockMayReadAtArgs(f->body.get()); }
        case NK::LoopStmt: { auto* l = static_cast<const LoopStmt*>(s);
            return exprMayReadAtArgs(l->init.get()) || exprMayReadAtArgs(l->cond.get()) ||
                   exprMayReadAtArgs(l->incr.get()) || blockMayReadAtArgs(l->body.get()); }
        case NK::GivenStmt: { auto* g = static_cast<const GivenStmt*>(s);
            return exprMayReadAtArgs(g->topic.get()) || paramsMayReadAtArgs(g->params) ||
                   paramsMayReadAtArgs(g->elseParams) || blockMayReadAtArgs(g->body.get()) ||
                   blockMayReadAtArgs(g->elseBody.get()); }
        case NK::WhenStmt: { auto* w = static_cast<const WhenStmt*>(s);
            return exprMayReadAtArgs(w->cond.get()) || blockMayReadAtArgs(w->body.get()); }
        default: return true;   // declarations, `use`, and anything not listed
    }
}
static bool stmtsMayReadAtArgs(const std::vector<StmtPtr>& v) {
    for (auto& s : v) if (stmtMayReadAtArgs(s.get())) return true;
    return false;
}
bool Interpreter::blockTakesAtArgs(Callable& c) {
    signed char v = c.blockAtArgs;
    if (v < 0) {
        v = (!c.isBlock || !c.body || stmtsMayReadAtArgs(*c.body) ||
             (c.params && paramsMayReadAtArgs(*c.params))) ? 1 : 0;
        c.blockAtArgs = v;
    }
    return v != 0;
}


} // namespace rakupp
