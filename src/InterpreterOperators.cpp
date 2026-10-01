// InterpreterOperators.cpp — operators, mixins, phasers, gather, reductions and subscripts
//
// One of the parts InterpreterParts.h lists; what they share is declared there.
#include "InterpreterParts.h"

namespace rakupp {

// Is an operand a Whatever (or a curried WhateverCode) that came from somewhere
// ELSE — a variable, a return, an element — rather than from a `*` written at
// this site? Currying is syntactic, so only a written star composes.
bool Interpreter::whateverArrivedAsValue(Binary* b, const Value& l, const Value& r) {
    auto wish = [](const Value& v) {
        return v.t == VT::Whatever || (v.t == VT::Code && v.code() && v.code()->isWhateverCode);
    };
    if (!wish(l) && !wish(r)) return false;
    return !exprHasWhateverLit(b->lhs.get()) && !exprHasWhateverLit(b->rhs.get());
}

static Value mixinAttrDefault(const ClassAttr& a) {
    // an `@` or `%` attribute starts as an empty Array / Hash, as a class's own
    // does: `$r does R` with `has @.s is rw` then `push &b.s, …` (routines.t)
    if (a.sigil == '@') {
        Value v = Value::array();
        if (!a.type.empty() && a.type != "Any" && a.type != "Mu") v.ofTypeM() = a.type;
        return v;
    }
    if (a.sigil == '%') return Value::makeHash();
    // `has Mu:U $!x` starts as Mu, not Any — Red tests `$!relationship-model<>
    // =:= Mu` to learn that no model was loaded yet
    if (a.type == "Mu") return Value::typeObj("Mu");
    if (!a.type.empty() && a.type != "Any")
        return Value::typeObj(a.type);
    return Value::any();
}

Value Interpreter::mixinValue(Value base, const Value& rhs, bool copy, bool rhsIsLiteralList) {
    // Collect the role(s) and attribute Pair(s) from the RHS (a single role type,
    // a list of them, or a `:name(value)` Pair mixing one attribute).
    std::vector<ClassInfo*> roleInfos;
    std::vector<std::string> roleNames;
    ValueList pairs, valueMixins;
    std::function<void(const Value&)> collect = [&](const Value& v) {
        if (v.t == VT::Type) {
            roleNames.push_back(v.s);
            auto it = classes_.find(v.s);
            if (it != classes_.end()) roleInfos.push_back(it->second.get());
        } else if (v.t == VT::Pair) {
            pairs.push_back(v);
        } else if (v.t == VT::Array && v.arr() &&
                   (rhsIsLiteralList || &v != &rhs ||
                    std::all_of(v.arr()->begin(), v.arr()->end(),
                                [](const Value& e) { return e.t == VT::Type || e.t == VT::Pair; }))) {
            for (auto& e : *v.arr()) collect(e);
        } else {
            // `X but VALUE` mixes in a method named after VALUE's type that
            // returns it: 5 but "t" stringifies as "t"; 0 but True is true.
            // The role it composes is anonymous but NOT nameless — Rakudo calls
            // it `<anon|N>`, so `1 but 2` is an `Int+{<anon|1>}`. Without a name
            // the mixin rendered as the bare `Int+{}`.
            valueMixins.push_back(v);
            roleNames.push_back("<anon|" + std::to_string(++anonMixinSeq_) + ">");
        }
    };
    collect(rhs);

    // `$method does SomeRole` where $method is a ROUTINE: mix in place, for exactly
    // the reason the attribute path below does. A method trait_mod is handed the
    // Method object and mixes a marker role into it (`$method does Constraint($p)`,
    // Path::Finder tagging every matcher method with its precedence); Rakudo's
    // `does` mutates that object, so the class's method table — which shares the
    // Callable — sees it. Boxing into a fresh object left `^lookup('file') ~~
    // Constraint` False and the module refused its own keys.
    if (!copy && base.t == VT::Code && base.code() && !roleNames.empty()) {
        auto c = base.code();
        auto& mx = c->mixinsRW();
        for (auto& rn : roleNames)
            if (std::find(mx.roles.begin(), mx.roles.end(), rn) == mx.roles.end())
                mx.roles.push_back(rn);
        for (auto& p : pairs)
            if (p.pairVal()) c->mixinsRW().attrs[p.s] = *p.pairVal();
        // a role's own attributes exist even unset, so its accessors answer
        for (ClassInfo* role : roleInfos)
            if (role)
                for (auto& a : role->attrs)
                    c->mixinsRW().attrs.emplace(a.name, mixinAttrDefault(a));
        return base;
    }
    // `$a does SomeRole` where $a is an ATTRIBUTE meta-object: mix in place. The
    // meta-object is a Hash whose map is shared by every copy, so recording the
    // role (and its attributes' defaults) in that map is visible to the holder of
    // the ClassAttr — where boxing it into a fresh object, as the general path
    // does, would leave the trait's work in a value nobody can reach. This is the
    // shape every attribute trait_mod uses: `$a does MetaAttribute::Customary;
    // $a.where = 'unknown'` (META6), and the JSON:: ecosystem's traits likewise.
    // A Parameter meta-object is the same shape and needs the same treatment:
    // `$param does Formatted::Named(:$argument)` inside a `trait_mod:<is>` has
    // to land in the map the signature hands out, or `.signature.params` answers
    // a Parameter that has forgotten its own trait (Getopt::Long reads every
    // option spec back that way).
    if (base.t == VT::Hash &&
        (base.hashKind == "Attribute" || base.hashKind == "Parameter") && base.hash()) {
        // …and `$attr but R` is the same on a COPY of the map. Boxed into a
        // generic object instead, it stopped being an Attribute at all: Red's
        // `Attribute.new(…) but Red::Attr::Relationship[…]` (a relationship
        // transferred to a model alias) had no `.type`.
        if (copy) {
            Value nb = base;
            nb.setHash(makePayload<ValueMap>(*base.hash()));
            if (auto rit = nb.hash()->find(ATTR_ROLES_KEY);
                rit != nb.hash()->end() && rit->second.t == VT::Array && rit->second.arr()) {
                Value fresh = Value::array(); fresh.isList = true;
                *fresh.arr() = *rit->second.arr();
                rit->second = std::move(fresh);   // the copy's role list is its own
            }
            base = std::move(nb);
        }
        Value& roles = (*base.hash())[ATTR_ROLES_KEY];
        if (roles.t != VT::Array || !roles.arr()) { roles = Value::array(); roles.isList = true; }
        for (auto& rn : roleNames) roles.arr()->push_back(Value::str(rn));
        // Seed the role's attributes as plain keys, so the role's own accessors
        // (`$a.where`, `$a.optionality`) read and write them. Composed roles count:
        // META6 mixes in MetaAttribute::Specification, whose attributes are all
        // inherited from the MetaAttribute it does — miss those and the trait's
        // very next line, `$a.optionality = …`, has no slot to assign to.
        std::set<ClassInfo*> seeded;
        std::function<void(ClassInfo*)> seedRole = [&](ClassInfo* role) {
            if (!role || !seeded.insert(role).second) return;
            for (auto& a : role->attrs) {
                // A PRIVATE attribute is seeded under a key no method call can
                // reach. Under its bare name it became a de-facto accessor and
                // SHADOWED the role's own method of that name: Red::Attr::Column
                // has `has Red::Column $!column` beside `method column`, so
                // `$attr.column` answered the empty attribute — a Red::Column type
                // object — and never ran the method that builds one (issue #77).
                // A PUBLIC attribute keeps the bare key, which is what serves its
                // accessor and what `%!args` reads back through.
                const std::string key = a.pub ? a.name : privMixinKey(a.name);
                if (base.hash()->count(key)) continue;
                Value dv = mixinAttrDefault(a);
                if (a.hasDefVal) dv = a.defVal;
                else if (a.def) {
                    // evaluated as a construction would: `self` is the Attribute
                    // being mixed into (its own `$!x` already seeded above), and
                    // the role's parameters are in scope — Red's
                    // `has Bool $.no-prefetch = $!has-one // $no-prefetch // self.type ~~ Positional`
                    auto saved = tctx_.cur;
                    auto denv = std::make_shared<Env>();
                    denv->parent = role->declEnv ? role->declEnv : tctx_.cur;
                    denv->define("self", base);
                    for (ClassInfo* rb : roleInfos)
                        for (auto& b : rb->roleParamBindings)
                            if (!b.first.empty() && !denv->local(b.first)) denv->define(b.first, b.second);
                    for (auto& b : role->roleParamBindings)
                        if (!b.first.empty() && !denv->local(b.first)) denv->define(b.first, b.second);
                    tctx_.cur = denv;
                    try { dv = eval(const_cast<Expr*>(a.def)); } catch (...) { dv = Value::any(); }
                    tctx_.cur = saved;
                }
                (*base.hash())[key] = dv;
            }
            for (auto& sub : role->doneRoles) {
                roles.arr()->push_back(Value::str(sub));
                auto sit = classes_.find(sub);
                if (sit != classes_.end()) seedRole(sit->second.get());
            }
        };
        for (ClassInfo* role : roleInfos) seedRole(role);
        for (auto& p : pairs) (*base.hash())[p.s] = p.pairVal() ? *p.pairVal() : Value::any();
        return base;
    }

    PRef<ObjectData> obj;
    bool baseWasType = false; // `C but R` on a KNOWN class stays a TYPE OBJECT (C+{R})
    if (base.t == VT::Object && base.obj()) {
        obj = base.objS();
        if (copy) { // `but` works on a fresh copy; the original is untouched
            auto nd = makePayload<ObjectData>();
            nd->cls = obj->cls;
            nd->attrs = obj->attrs;
            nd->boxed = obj->boxed;
            nd->hasBoxed = obj->hasBoxed;
            obj = nd;
        }
    } else {
        // non-object base (`5 but Role`, `{} does R`): box the value so the mixed
        // object still coerces / dispatches to it. `does`/`but` are both copies here.
        obj = makePayload<ObjectData>();
        obj->boxed = base;
        obj->hasBoxed = true;
        // `True but False`: the mixed-in Bool is what the value now IS in every
        // coercion — Rakudo answers 0, "False" and False for +, ~ and ? (only
        // `.key` still says True). The anon role already answered `.Bool`; the
        // box underneath kept stringifying as "True" (integration/advent2010-day19.t).
        if (base.t == VT::Bool)
            for (auto& vm : valueMixins)
                if (vm.t == VT::Bool) obj->boxed = Value::boolean(vm.b);
        // A TYPE OBJECT of a class we know derives from THAT class, so its own
        // methods and grammar rules survive the mixin (`Base but GR` has to keep
        // parsing with Base's rules). Only an unknown/builtin type gets the bare
        // placeholder.
        auto known = base.t == VT::Type ? classes_.find(base.s) : classes_.end();
        if (known != classes_.end() && known->second) { obj->cls = known->second; baseWasType = true; }
        else {
            auto bc = std::make_shared<ClassInfo>();
            bc->name = base.typeName();
            bc->nativeParent = base.typeName();
            obj->cls = bc;
            // A BUILT-IN type object mixes in to a type object just as a user
            // class does — `Str but R` is the type `Str+{R}`, undefined, and
            // usable as a constraint. Only the known-class branch said so, so
            // `my constant StrType = Str but Type` was an INSTANCE: it was not
            // .defined the way Rakudo has it, nothing smartmatched against it,
            // and a `StrType:D` candidate could never bind. That is the whole of
            // Needle::Compile's mixed-in-type path (`implicit2explicit` read
            // every tagged needle as a plain string and lost its type).
            baseWasType = base.t == VT::Type;
        }
    }
    // A new anonymous class derived from the current one, composing the role(s).
    auto nc = std::make_shared<ClassInfo>();
    nc->parent = obj->cls;
    std::string suffix;
    for (auto& rn : roleNames) suffix += (suffix.empty() ? "" : ",") + rn;
    nc->name = obj->cls->name + "+{" + suffix + "}";
    for (ClassInfo* role : roleInfos) {
        for (auto& kv : role->methods) {
            // two roles mixed in together that both bring `multi method m`
            // contribute ONE dispatch group with every candidate — the second
            // replacing the first lost R5's `()` when R6 added `(Numeric)`
            auto have = nc->methods.find(kv.first);
            if (have != nc->methods.end() && have->second.t == VT::Code && kv.second.t == VT::Code &&
                have->second.code() && kv.second.code() &&
                have->second.code()->isMultiDispatcher && kv.second.code()->isMultiDispatcher) {
                auto merged = makePayload<Callable>(*have->second.code());
                for (auto& cand : kv.second.code()->candidates) merged->candidates.push_back(cand);
                Value mv = have->second; mv.setCode(merged);
                have->second = mv;
                continue;
            }
            nc->methods[kv.first] = kv.second;
        }
        for (auto& sub : role->doneRoles) nc->doneRoles.insert(sub);
        // …and its GRAMMAR RULES. A role composed at runtime brought its methods
        // and attributes but not its tokens, so `Base but SomeGrammarRole` had
        // nothing to parse with — which is how Lingua::NumericWordForms picks a
        // language (`WordFormParser but %langToRole{$lang}`).
        for (auto& kv : role->rules) {
            if (nc->rules.count(kv.first)) continue;
            nc->rules[kv.first] = kv.second;
            auto ki = role->ruleKind.find(kv.first);
            if (ki != role->ruleKind.end()) nc->ruleKind[kv.first] = ki->second;
            auto pi = role->ruleParams.find(kv.first);
            if (pi != role->ruleParams.end()) nc->ruleParams[kv.first] = pi->second;
        }
        for (auto& nm : role->ruleOrder)
            if (std::find(nc->ruleOrder.begin(), nc->ruleOrder.end(), nm) == nc->ruleOrder.end())
                nc->ruleOrder.push_back(nm);
        if (role->isGrammar) nc->isGrammar = true;
        // A parameterized role mixed in at RUNTIME binds its params too, exactly as
        // a class body's `does R` does — `Array[T] but JSON::Class` then `.to-json`
        // must still see $opt-in (JSON::Class 050-array.t). A pun (`R[3]`) already
        // carries its bindings, and they come first so they win over the defaults.
        for (auto& b : role->roleParamBindings) nc->roleParamBindings.push_back(b);
        ValueList noArgs;
        bindRoleParamsInto(nc.get(), role, noArgs, role->declEnv);
        // compose attrs and initialize each to its default, evaluated in the role's
        // own declaration scope (so `role { has $.x = $val }` captures $val).
        for (auto& a : role->attrs) {
            nc->attrs.push_back(a);
            if (obj->attrs.count(a.name)) continue;
            Value dv = mixinAttrDefault(a);
            if (a.hasDefVal) dv = a.defVal;
            else if (a.def) {
                auto saved = tctx_.cur;
                // …with the role's PARAMETERS bound: `role R[$val] { has $.attr = $val }`
                auto denv = std::make_shared<Env>();
                denv->parent = role->declEnv ? role->declEnv : tctx_.cur;
                for (auto& b : nc->roleParamBindings)
                    if (!b.first.empty() && !denv->local(b.first)) denv->define(b.first, b.second);
                tctx_.cur = denv;
                try { dv = eval(const_cast<Expr*>(a.def)); } catch (...) { dv = Value::any(); }
                tctx_.cur = saved;
            }
            obj->attrs[a.name] = dv;
        }
    }
    // …and the roles THOSE roles compose. Only the directly-named ones were
    // copied, so a role declared `role Leaf does Base` brought Base's NAME —
    // `.does(Base)` answered True — and none of what Base declares. PDF wraps
    // every scalar it reads that way (`unit role PDF::COS::Bool; also does
    // PDF::COS;`), so a coerced value could not answer the `.obj-num` its
    // serializer asks every object for. A directly-named role's own methods win,
    // which is why this is a second pass rather than part of the loop above.
    {
        std::set<ClassInfo*> composedRoles(roleInfos.begin(), roleInfos.end());
        std::function<void(ClassInfo*)> composeChain = [&](ClassInfo* role) {
            for (auto& sub : role->doneRoles) {
                nc->doneRoles.insert(sub);
                auto sit = classes_.find(sub);
                if (sit == classes_.end() || !sit->second) continue;
                ClassInfo* sr = sit->second.get();
                if (!composedRoles.insert(sr).second) continue;
                for (auto& kv : sr->methods) nc->methods.emplace(kv.first, kv.second);
                for (auto& kv : sr->rules) {
                    if (nc->rules.count(kv.first)) continue;
                    nc->rules[kv.first] = kv.second;
                    auto ki = sr->ruleKind.find(kv.first);
                    if (ki != sr->ruleKind.end()) nc->ruleKind[kv.first] = ki->second;
                    auto pi = sr->ruleParams.find(kv.first);
                    if (pi != sr->ruleParams.end()) nc->ruleParams[kv.first] = pi->second;
                }
                for (auto& nm : sr->ruleOrder)
                    if (std::find(nc->ruleOrder.begin(), nc->ruleOrder.end(), nm) == nc->ruleOrder.end())
                        nc->ruleOrder.push_back(nm);
                if (sr->isGrammar) nc->isGrammar = true;
                for (auto& a : sr->attrs) {
                    bool have = false;
                    for (auto& na : nc->attrs) if (na.name == a.name) { have = true; break; }
                    if (!have) nc->attrs.push_back(a);
                    if (obj->attrs.count(a.name)) continue;
                    Value dv = mixinAttrDefault(a);
                    if (a.hasDefVal) dv = a.defVal;
                    else if (a.def) {
                        auto saved = tctx_.cur;
                        if (sr->declEnv) tctx_.cur = sr->declEnv;
                        try { dv = eval(const_cast<Expr*>(a.def)); } catch (...) { dv = Value::any(); }
                        tctx_.cur = saved;
                    }
                    obj->attrs[a.name] = dv;
                }
                composeChain(sr);
            }
        };
        for (ClassInfo* role : roleInfos) if (role) composeChain(role);
    }
    for (auto& rn : roleNames) nc->doneRoles.insert(rn);
    // `True but (1, 1)` — two generated roles both supplying `.Int` conflict
    {
        std::set<std::string> seenT;
        for (auto& vm : valueMixins) {
            std::string tn = vm.typeName();
            if (!seenT.insert(tn).second)
                throwTypedV("X::Role::Method::Conflict", {{"method", Value::str(tn)}},
                            "Method '" + tn + "' must be resolved by class " + base.typeName() +
                            " because it exists in multiple roles");
        }
    }
    // `but VALUE` — a constant method named after the value's type (Str/Bool/Int/…).
    for (auto& vm : valueMixins) {
        Value method = Value::closure([vm](ValueList&) { return vm; });
        method.code()->isMethod = true;
        nc->methods[vm.typeName()] = method;
        // …and an ENUM value mixes in its enum as a role: a method per
        // member, True for the one mixed in (`$x but Maybe(Yes)` answers
        // `.Yes` True and `.No` False)
        if (!vm.enumName.empty() && !vm.enumType.empty()) {
            Value en;
            try {   // the enum TYPE as its name resolves (not a bare type object)
                NameTerm tn(std::string(vm.enumType.c_str()));
                en = methodCall(eval(&tn), "enums", ValueList{});
            } catch (...) {}
            if (en.t == VT::Hash && en.hash())
                for (auto& kv : *en.hash()) {
                    const bool mine = kv.first == vm.enumName.c_str();
                    Value m = Value::closure([mine](ValueList&) { return Value::boolean(mine); });
                    m.code()->isMethod = true;
                    if (!nc->methods.count(kv.first)) nc->methods[kv.first] = m;
                }
        }
    }
    // `but :name(value)` — mix one attribute with a public read accessor.
    for (auto& p : pairs) {
        ClassAttr ca; ca.name = p.s; ca.sigil = '$'; ca.pub = true;
        nc->attrs.push_back(ca);
        obj->attrs[p.s] = p.pairVal() ? *p.pairVal() : Value::any();
    }
    noteSymbolMutation("does/but mixin");
    classes_[nc->name] = nc;
    obj->cls = nc;
    // Mixing into a TYPE OBJECT answers a type object, as Rakudo does: `C but R`
    // is `C+{R}` with `.DEFINITE` False, and `.new`/`.parse` on it behave like
    // any other type. Boxing it into an instance made every type-level call —
    // a grammar's `.parse` above all — dispatch to the box instead.
    if (baseWasType) return Value::typeObj(nc->name);
    Value out; out.t = VT::Object; out.setObj(obj);
    // A role mixed in at RUNTIME runs its BUILD submethod NOW, on the object it
    // was mixed into — construction already happened, so this is the only point
    // at which a mixed-in role can initialise anything.
    // …and then its TWEAK, the same order construction runs them in
    for (const char* sub : {"BUILD", "TWEAK"})
        for (ClassInfo* role : roleInfos) {
            auto bi = role->methods.find(sub);
            if (bi == role->methods.end() || bi->second.t != VT::Code) continue;
            ValueList none;
            invokeMethod(bi->second, out, none);
        }
    return out;
}

// hyper prefix `-«(…)` / `--«%h`: apply the op per element, descending into
// nested arrays and hash values (keys kept); ++/-- mutate the elements in
// place through the shared containers and yield the new values (prefix).
// The container walk both hyper-unary forms do: descend nested arrays and
// ranges (a range hyper-applies as a list), keep hash keys and quanthash
// flavour, and hand each leaf to `leaf`. The two callers used to carry
// identical copies of this recursion and differ only in the leaf — which is
// the whole point of the operation, so it is the only part they still write.
// A hyper over a Set/Bag/Mix maps each WEIGHT and answers the same kind —
// a Set keeps the keys whose answer is true, a Bag the positive counts
Value Interpreter::hyperQuantWeights(const Value& inv, const std::function<Value(const Value&)>& f) {
    Value out = Value::makeHash(); out.hashKind = inv.hashKind; out.ofTypeM() = inv.ofType();
    const std::string& k = inv.hashKind.str();
    const bool setty = k == "Set" || k == "SetHash", mixy = k == "Mix" || k == "MixHash";
    for (auto& kv : *inv.hash()) {
        Value w = f(kv.second);
        Value stored;
        if (setty) { if (!boolify(w)) continue; stored = Value::boolean(true); }
        else if (mixy) { if (w.toNum() == 0) continue; stored = w; }
        else { Value n = w.t == VT::Int ? w : Value::integer((long long)w.toNum()); if (n.toInt() <= 0) continue; stored = n; }
        stored.pairKeyM() = kv.second.elemKey();
        (*out.hash())[kv.first] = std::move(stored);
    }
    return out;
}

Value Interpreter::hyperWalk(Value& v, const std::function<Value(Value&)>& leaf) {
    std::function<Value(Value&)> deep = [&](Value& x) -> Value {
        if (x.t == VT::Array && x.arr()) {
            Value out = Value::array(); out.isList = x.isList;
            for (auto& e : *x.arr()) out.arr()->push_back(deep(e));
            return out;
        }
        if (x.t == VT::Range) {
            Value out = Value::array(); out.isList = true;
            for (auto& e : x.flatten()) out.arr()->push_back(deep(e));
            return out;
        }
        if (x.t == VT::Hash && x.hash()) {
            Value out = Value::makeHash(); out.hashKind = x.hashKind;
            for (auto& kv : *x.hash()) (*out.hash())[kv.first] = deep(kv.second);
            return out;
        }
        return leaf(x);
    };
    return deep(v);
}

Value Interpreter::hyperUnary(const std::string& op, Value v) {
    // a user overload for an OBJECT leaf wins, as it does for the plain prefix
    Value* userOp = tctx_.cur ? tctx_.cur->find("&prefix:<" + op + ">") : nullptr;
    Value uf = userOp ? *userOp : Value();
    return hyperWalk(v, [&](Value& x) -> Value {
        if (userOp && x.t == VT::Object && x.obj() && x.obj()->cls) {
            try { return callCallable(uf, ValueList{x}); }
            catch (RakuError& e) {
                if (!(e.payload.t == VT::Type && e.payload.s == "X::Multi::NoMatch")) throw;
            }
        }
        if (op == "++" || op == "--") {
            Value nv = applyBinOp(op == "++" ? "+" : "-", x, Value::integer(1));
            x = nv;
            return nv;
        }
        if (op == "-") return applyBinOp("-", Value::integer(0), x);
        if (op == "+") {
            if (x.isAllomorph()) { // +IntStr strips to the pure numeric side
                Value nx = x; nx.hashKind.clear(); nx.s.clear(); return nx;
            }
            return applyBinOp("+", Value::integer(0), x);
        }
        if (op == "!") return Value::boolean(!boolify(x));
        if (op == "?") return Value::boolean(boolify(x));
        return Value::str(x.toStr()); // ~
    });
}

// hyper postfix `@a»++` / `%h»!` / `(2,3)»i`: descends nested arrays, keeps
// hash keys; ++/-- mutate in place and yield the OLD values (postfix).
Value Interpreter::hyperPostfixApply(const std::string& op, Value v) {
    Value* userPost = tctx_.cur->find("&postfix:<" + op + ">");
    Value res = hyperWalk(v, [&](Value& x) -> Value {
        if (op == "++" || op == "--") {
            Value old = x;
            x = applyBinOp(op == "++" ? "+" : "-", x, Value::integer(1));
            x.pairKeyM() = old.pairKey();
            return old;                       // postfix yields the OLD value
        }
        if (userPost) return callCallable(*userPost, ValueList{x});
        if (op == "i") return postfixI(x);
        throw RakuError{Value::typeObj("X::AdHoc"),
                        "No postfix:<" + op + "> operator for hyper"};
    });
    // a mutable QuantHash drops the keys whose weight ran out:
    // `$baghash»--` leaves only what weighed more than 1
    if ((op == "++" || op == "--") && v.t == VT::Hash && v.hash()) {
        const std::string k = v.hashKind.str();
        if (k == "BagHash" || k == "MixHash" || k == "SetHash") {
            for (auto it = v.hash()->begin(); it != v.hash()->end();) {
                const Value& w = it->second;
                bool gone = k == "MixHash" ? w.toNum() == 0 : w.toNum() <= 0;
                if (gone) it = v.hash()->erase(it);
                else {
                    if (k == "SetHash") { Value t = Value::boolean(true); t.pairKeyM() = w.pairKey(); it->second = t; }
                    ++it;
                }
            }
        }
    }
    return res;
}

// Prefix `+` and prefix `-` on a value, whole. This lived only inside
// evalUnary, and the WhateverCode that `+*` / `-*` curries into carried its own,
// much thinner copy — which had drifted badly. That copy numified through
// toNum() and boxed a Num, so `"123".comb.map(+*)` produced (1e0, 2e0, 3e0)
// where the same operator applied directly gives (1, 2, 3); a list numified to
// its element count as a Num rather than an Int, a Bool came back as a Bool,
// `-*` over a Rat gave a Num and over a Complex gave -0e0, and `-*` over a
// bignum saturated through toInt(). One implementation now, so the curried form
// and the direct one cannot disagree again.
//
// Every `op` reaching here is "+" or "-". The arms are verbatim from evalUnary
// and in the order it ran them, each keeping its own — now redundant — check of
// which of the two it serves, so that an arm stays readable and movable on its
// own. The last two always return, so this is total for both operators.
// prefix `~` on one value. A user `method Str` that answers a TYPE OBJECT is
// answering "no string" — `~$field` is `Str`, not "" — and strOf() cannot say
// so, because it has to flatten everything to a std::string. Text::CSV's
// :blank-is-undef fields are exactly this shape; `».Str`, which never goes
// through strOf, was already right about them.
Value Interpreter::prefixStringify(const Value& v) {
    if (v.t == VT::Object && v.obj() && v.obj()->cls)
        if (v.obj()->cls->findMethod("Str")) {
            // Dispatch it, rather than calling the resolved method: a `method Str`
            // that defers with `nextsame` needs the dispatcher frame `$o.Str` gets,
            // and calling the body straight died "not in the dynamic scope of a
            // dispatcher" for `~$o` alone (CSS::Writer's Str over Any.Str).
            ValueList none;
            Value r = methodCall(v, "Str", none);
            if (!isDefined(r)) return r;
            return Value::str(strOf(r));
        }
    return Value::str(strOf(v));
}
Value Interpreter::prefixNumeric(const std::string& op, const Value& v) {
    if (v.t == VT::Array) v.seqTouch();   // `+$s` is its count: a Seq is cached (SeqToken)
    if ((op == "+" || op == "-") && v.t == VT::Code && v.code() && !v.code()->isWhateverCode &&
        !v.code()->isBlock)
        throwCodeNumeric(v);
    // `+Mu` / `-Mu`: Mu is below Any, and the prefix has no candidate for it
    if ((op == "+" || op == "-") && v.t == VT::Type && v.s == "Mu")
        throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                        "Cannot resolve caller prefix:<" + op + ">(Mu:U); none of these signatures matches:\n    (\\a)"};
    // `+Any` / `-Int`: an undefined value used as a number is 0, with Rakudo's
    // warning (naming where it happened — the routine and the line)
    if ((op == "+" || op == "-") && (v.t == VT::Any || (v.t == VT::Type && v.s != "IterationEnd")))
        warnUninit("Use of uninitialized value of type " + (v.t == VT::Any ? std::string("Any") : v.typeName()) +
                   " in numeric context");
    // numeric prefix on an object uses its .Numeric (or .Bridge/.Int): `+$o`, `-$o`
    if ((op == "+" || op == "-") && v.t == VT::Object && v.obj() && v.obj()->cls) {
        for (const char* nm : {"Numeric", "Bridge", "Int"})
            if (Value* m = v.obj()->cls->findMethod(nm)) {
                ValueList none; Value n = invokeMethod(*m, v, none);
                if (op == "-") return n.t == VT::Int ? Value::integer(-n.toInt()) : Value::number(-n.toNum());
                return n;
            }
        // …and a class deriving a built-in numifies as the value it BOXES:
        // `+Int64.new(-42)` is -42, not the object's address.
        if (v.obj()->hasBoxed) {
            Value b = v.obj()->boxed;
            ValueList none;
            Value n = b.isNumeric() ? b : methodCall(b, "Numeric", none);
            if (op == "-") return n.t == VT::Int ? Value::integer(-n.toInt()) : Value::number(-n.toNum());
            return n;
        }
    }
    // a LAZY list (a handle's `.lines`, a gather) counts what it WILL hold,
    // not the part pulled so far: `+$fh.lines` reads to the end
    if ((op == "+" || op == "-") && v.t == VT::Array && v.ext() && v.hashKind.empty() && v.enumName.empty()) {
        ValueList none; Value n = methodCall(v, "elems", none);
        if (op == "-") return n.t == VT::Int ? Value::integer(-n.toInt()) : Value::number(-n.toNum());
        return n;
    }
    // …and a CAPTURE numifies to its POSITIONAL count: the named parts are not
    // elements, so `+\(2, 3, :a(7))` is 2, not 3.
    if ((op == "+" || op == "-") && v.t == VT::Array && v.hashKind == "Capture") {
        ValueList none; Value n = methodCall(v, "Numeric", none);
        return op == "-" ? Value::integer(-n.toInt()) : n;
    }
    // A Blob/Buf is Positional too, so numeric context is its ELEMENT COUNT, not a
    // numification of its bytes as text. `+"key".encode` is 3; rakupp was reading
    // the bytes as the string "key" and throwing "Cannot convert string to number".
    // Digest's HMAC pads its key with `if +$key < $block-size`, so every HMAC came
    // out wrong — the padding branch never ran.
    if ((op == "+" || op == "-") && v.t == VT::Str && !v.itemized &&
        (v.hashKind == "Blob" || v.hashKind == "Buf" || v.hashKind == "utf8")) {
        long long n = v.blobElems();
        return Value::integer(op == "-" ? -n : n);
    }
    // …but a Range with an infinite or NaN endpoint has no element count and
    // still numifies: `+(1..*)` is Inf and `+(1..NaN)` is NaN, where `.elems`
    // on either is a Failure (Range sheet RG-08). Reading the element count
    // gave the 10,000-element prefix an endless range hands out.
    if ((op == "+" || op == "-") && v.t == VT::Range) {
        double sp;
        if (rangeNumericSpecial(v, sp)) {
            if (op == "-") sp = -sp;
            return std::isfinite(sp) ? Value::integer((long long)sp) : Value::number(sp);
        }
    }
    // Numeric context of a list/array/hash/range is its element count —
    // except a Proc / Proc::Async, which numifies to its exit status (+$proc).
    if ((op == "+" || op == "-") &&
        (v.t == VT::Array || v.t == VT::Hash || v.t == VT::Range) &&
        !(v.t == VT::Hash && (v.hashKind == "Proc" || v.hashKind == "Proc::Async" ||
                              v.hashKind == "StrDistance"))) {
        // a Bag/Mix numifies to its .total (sum of counts/weights), possibly fractional
        if (v.t == VT::Hash && v.hash() &&
            (v.hashKind.rfind("Bag", 0) == 0 || v.hashKind.rfind("Mix", 0) == 0)) {
            double t = 0; bool allInt = true;
            for (auto& kv : *v.hash()) { t += kv.second.toNum(); if (kv.second.t != VT::Int && kv.second.t != VT::Bool) allInt = false; }
            if (op == "-") t = -t;
            return allInt ? Value::integer((long long)t) : Value::number(t);
        }
        // +$ptr is the ADDRESS, not an element count: a NativeCall Pointer is
        // a { addr, of } hash, so counting its keys numified every pointer to 2.
        if (v.t == VT::Hash && v.hash() && (v.hashKind == "Pointer" || v.hashKind == "CArray")) {
            auto it = v.hash()->find("addr");
            if (it != v.hash()->end()) {
                long long a = it->second.toInt();
                return Value::integer(op == "-" ? -a : a);
            }
        }
        // +$date is its DAYCOUNT and +$datetime its Instant (Rakudo's Dateish
        // Numeric) — counting the hash's fields gave a number that meant nothing.
        if (v.t == VT::Hash && v.hash() && (v.hashKind == "Date" || v.hashKind == "DateTime")) {
            ValueList none;
            Value n = methodCall(v, "Numeric", none);
            if (op == "-") return n.t == VT::Int ? Value::integer(-n.toInt()) : Value::number(-n.toNum());
            return n;
        }
        long long n;
        if (v.t == VT::Array) n = (long long)v.arr()->size();
        else if (v.t == VT::Hash) n = (long long)v.hash()->size();
        else n = (long long)v.flatten().size();
        return Value::integer(op == "-" ? -n : n);
    }
    if (op == "-") {
        // `0 - z`, not `(-re, -im)`: Raku's unary minus on a Complex subtracts
        // from zero, so `-i` has a POSITIVE zero real part (0 - 0 is +0, where
        // -0.0 is negative zero and `(-i).re.raku` then prints "-0e0").
        if (v.t == VT::Complex) return Value::complex(0.0 - v.n, 0.0 - v.im());
        if (v.t == VT::Int && v.big()) return Value::bigint(-(*v.big()));
        if (v.t == VT::Int || v.t == VT::Bool) return Value::integer(-v.toInt());
        if (v.t == VT::Rat) { Value r = Value::rat(-(*v.ratN()), *v.ratD()); r.fatRatM() = v.fatRat(); return r; }
        if (v.t == VT::Str || v.t == VT::Match) {
            Value n = v.t == VT::Str ? numifyStrFailure(v.s) : numifyStr(strOf(v));
            // negating the Failure USES it: `-"2 foo"` throws X::Str::Numeric
            if (n.t == VT::Hash && n.hashKind == "Failure") failureDetonate(n);
            if (n.t == VT::Complex) return Value::complex(0.0 - n.n, 0.0 - n.im());
            if (n.t == VT::Int && n.big()) return Value::bigint(-(*n.big()));
            if (n.t == VT::Int && !n.big()) return Value::integer(-n.toInt());
            return n.t==VT::Rat ? Value::rat(-(*n.ratN()),*n.ratD()) : Value::number(-n.toNum());
        }
        if (!isDefined(v)) return Value::integer(0); // `-$undef` is 0, an Int (Rakudo warns; the binary ladder agrees)
        return Value::number(-v.toNum());
    }
    if (op == "+") {
        if (v.t == VT::Bool) return Value::integer(v.b ? 1 : 0); // +True == 1
        if (v.isAllomorph()) { // +IntStr strips to the pure numeric side
            Value nv = v; nv.hashKind.clear(); nv.s.clear(); return nv;
        }
        if (v.t == VT::Match) return numifyStr(strOf(v)); // +$0 of digits is an Int, like +Str
        if (v.isNumeric() && !v.enumName.empty()) { // +EnumValue is a PLAIN Int, not the name
            Value nv = v; nv.enumName.clear(); nv.enumType.clear(); return nv;
        }
        // `+"a"` is an error, not an undefined value (Rakudo: X::Str::Numeric)
        if (v.t == VT::Complex) return v;            // `+(3i)` is 3i — Complex is Numeric
        if (!isDefined(v)) return Value::integer(0); // `+$undef` is 0, an Int
        return v.isNumeric() ? v : (v.t == VT::Str ? numifyStrFailure(v.s) : Value::number(v.toNum()));
    }
}

// ---- nested BEGIN / CHECK / INIT -------------------------------------------
// A phaser written inside a block or a closure runs before the code around it
// (BEGIN at compile time, CHECK at its end, INIT at the start of the run), once,
// and when it stands as an expression every later evaluation yields the value it
// computed. The lexicals it reads are the ones of its enclosing scopes as they
// exist at that moment — declared, not yet initialized — so each enclosing scope
// gets a STATIC environment, and that scope's first real run starts from it
// (`{ my $s; BEGIN { $s = 3 }; say $s }` says 3: `my $s;` does not reset it).
struct StaticPhaserRec {
    std::string kind;
    Block* blk = nullptr;          // statement form
    BlockExpr* be = nullptr;       // expression form
    std::vector<const std::vector<StmtPtr>*> chain;   // enclosing scopes, outermost first
};
static void spWalkBody(const std::vector<StmtPtr>& body, std::vector<const std::vector<StmtPtr>*>& chain,
                       std::vector<StaticPhaserRec>& out, bool newScope);
static void spWalkStmt(Stmt* s, std::vector<const std::vector<StmtPtr>*>& chain, std::vector<StaticPhaserRec>& out);
static void spWalkExpr(Expr* e, std::vector<const std::vector<StmtPtr>*>& chain, std::vector<StaticPhaserRec>& out) {
    if (!e) return;
    switch (e->kind) {
        case NK::BlockExpr: {
            auto* be = static_cast<BlockExpr*>(e);
            spWalkBody(be->body, chain, out, true);
            return;
        }
        case NK::Unary: {
            auto* u = static_cast<Unary*>(e);
            if (u->op == "do" && u->operand && u->operand->kind == NK::BlockExpr) {
                auto* be = static_cast<BlockExpr*>(u->operand.get());
                if (!be->phaser.empty()) {
                    if (!chain.empty()) out.push_back({be->phaser, nullptr, be, chain});
                    return;   // its own body belongs to that one run
                }
            }
            spWalkExpr(u->operand.get(), chain, out);
            return;
        }
        case NK::Assign: { auto* a = static_cast<Assign*>(e); spWalkExpr(a->target.get(), chain, out); spWalkExpr(a->value.get(), chain, out); return; }
        case NK::Binary: { auto* b = static_cast<Binary*>(e); spWalkExpr(b->lhs.get(), chain, out); spWalkExpr(b->rhs.get(), chain, out); return; }
        case NK::Call: { auto* c = static_cast<Call*>(e); spWalkExpr(c->callee.get(), chain, out); for (auto& a : c->args) spWalkExpr(a.get(), chain, out); return; }
        case NK::MethodCall: { auto* m = static_cast<MethodCall*>(e); spWalkExpr(m->inv.get(), chain, out); for (auto& a : m->args) spWalkExpr(a.get(), chain, out); return; }
        case NK::Ternary: { auto* t = static_cast<Ternary*>(e); spWalkExpr(t->cond.get(), chain, out); spWalkExpr(t->then.get(), chain, out); spWalkExpr(t->els.get(), chain, out); return; }
        case NK::ListExpr: for (auto& i : static_cast<ListExpr*>(e)->items) spWalkExpr(i.get(), chain, out); return;
        case NK::Pair: { auto* p = static_cast<PairExpr*>(e); spWalkExpr(p->keyExpr.get(), chain, out); spWalkExpr(p->value.get(), chain, out); return; }
        // an interpolation block is code like any other: `"{ BEGIN { … } }"`
        case NK::InterpStr: for (auto& p : static_cast<InterpStr*>(e)->parts) spWalkExpr(p.get(), chain, out); return;
        default: return;
    }
}
static void spWalkBody(const std::vector<StmtPtr>& body, std::vector<const std::vector<StmtPtr>*>& chain,
                       std::vector<StaticPhaserRec>& out, bool newScope) {
    if (newScope) chain.push_back(&body);
    for (auto& s : body) spWalkStmt(s.get(), chain, out);
    if (newScope) chain.pop_back();
}
static void spWalkStmt(Stmt* s, std::vector<const std::vector<StmtPtr>*>& chain, std::vector<StaticPhaserRec>& out) {
    if (!s) return;
    switch (s->kind) {
        case NK::Block: {
            auto* b = static_cast<Block*>(s);
            if (b->phaser == "BEGIN" || b->phaser == "CHECK" || b->phaser == "INIT") {
                // the TOP level keeps its own handling; a hoisted INIT already ran.
                // A statement-form `BEGIN stmt` joins the others, so the order
                // holds across both spellings (will.t: BEGIN a, `will begin` b,
                // BEGIN c); what it reaches that this pass cannot model — an
                // EVAL string, a routine declared around it — the filter in
                // runStaticPhasers leaves for the run-time path, as before.
                if (!chain.empty() && !b->initHoisted) out.push_back({b->phaser, b, nullptr, chain});
                return;
            }
            if (b->isCatch || !b->phaser.empty()) return;
            spWalkBody(b->stmts, chain, out, true);
            return;
        }
        case NK::ExprStmt: spWalkExpr(static_cast<ExprStmt*>(s)->e.get(), chain, out); return;
        case NK::ReturnStmt: spWalkExpr(static_cast<ReturnStmt*>(s)->value.get(), chain, out); return;
        case NK::IfStmt: { auto* f = static_cast<IfStmt*>(s);
            for (auto& br : f->branches) { spWalkExpr(br.first.get(), chain, out); if (br.second) spWalkBody(br.second->stmts, chain, out, true); }
            if (f->elseBlock) spWalkBody(f->elseBlock->stmts, chain, out, true); return; }
        case NK::ForStmt: { auto* f = static_cast<ForStmt*>(s);
            spWalkExpr(f->list.get(), chain, out); if (f->body) spWalkBody(f->body->stmts, chain, out, true); return; }
        case NK::WhileStmt: { auto* w = static_cast<WhileStmt*>(s);
            spWalkExpr(w->cond.get(), chain, out); if (w->body) spWalkBody(w->body->stmts, chain, out, true); return; }
        // …and a SUB's body: its INIT (`my $fh = INIT open(…)`) runs once, at program
        // start, not at each call (integration/advent2012-day15.t)
        case NK::SubDecl: { auto* sd = static_cast<SubDecl*>(s);
            if (!sd->isMethod && !sd->name.empty()) spWalkBody(sd->body, chain, out, true); return; }
        default: return;
    }
}
void spCallsE(const Expr* e, std::set<std::string>& out) {
    if (!e) return;
    switch (e->kind) {
        case NK::Call: { auto* c = static_cast<const Call*>(e); out.insert(c->name);
            spCallsE(c->callee.get(), out); for (auto& a : c->args) spCallsE(a.get(), out); return; }
        case NK::MethodCall: { auto* m = static_cast<const MethodCall*>(e); spCallsE(m->inv.get(), out);
            for (auto& a : m->args) spCallsE(a.get(), out); return; }
        case NK::Assign: { auto* a = static_cast<const Assign*>(e); spCallsE(a->target.get(), out); spCallsE(a->value.get(), out); return; }
        case NK::Binary: { auto* b = static_cast<const Binary*>(e); spCallsE(b->lhs.get(), out); spCallsE(b->rhs.get(), out); return; }
        case NK::Unary: spCallsE(static_cast<const Unary*>(e)->operand.get(), out); return;
        case NK::Ternary: { auto* t = static_cast<const Ternary*>(e); spCallsE(t->cond.get(), out); spCallsE(t->then.get(), out); spCallsE(t->els.get(), out); return; }
        case NK::ListExpr: for (auto& i : static_cast<const ListExpr*>(e)->items) spCallsE(i.get(), out); return;
        case NK::BlockExpr: for (auto& st : static_cast<const BlockExpr*>(e)->body) spCallsS(st.get(), out); return;
        case NK::SymbolicRef: out.insert("\x01other"); return;   // `&::("infix:<x>")` — a name decided at run time
        default: return;
    }
}
// What a scope declares with `my`, directly: `my $x;`, `my $x = …`, `my ($a, $b)`.
// A declaration the scope then gives a trait at run time (`my @a does R` is
// followed by `@a does R`) is left out: its static stand-in would lack it.
static void spDeclaredIn(const std::vector<StmtPtr>& body, std::vector<const VarExpr*>& out) {
    std::set<std::string> traited;
    for (auto& s : body) {
        if (!s || s->kind != NK::ExprStmt) continue;
        const Expr* e = static_cast<const ExprStmt*>(s.get())->e.get();
        if (e && e->kind == NK::Binary && (static_cast<const Binary*>(e)->op == "does" ||
                                           static_cast<const Binary*>(e)->op == "but")) {
            const Expr* l = static_cast<const Binary*>(e)->lhs.get();
            if (l && l->kind == NK::VarExpr) traited.insert(static_cast<const VarExpr*>(l)->name);
        }
    }
    std::vector<const VarExpr*> all;
    spDeclaredInRaw(body, all);
    for (auto* v : all) if (!traited.count(v->name)) out.push_back(v);
}
void spDeclaredInRaw(const std::vector<StmtPtr>& body, std::vector<const VarExpr*>& out) {
    auto one = [&](const Expr* e) {
        if (e && e->kind == NK::VarExpr) {
            auto* v = static_cast<const VarExpr*>(e);
            if (v->declare && v->declScope == "my" && v->name.size() > 1 && std::strchr("$@%", v->name[0])) out.push_back(v);
        }
        else if (e && e->kind == NK::ListExpr)
            for (auto& it : static_cast<const ListExpr*>(e)->items)
                if (it && it->kind == NK::VarExpr) {
                    auto* v = static_cast<const VarExpr*>(it.get());
                    if (v->declare && v->declScope == "my" && v->name.size() > 1 && std::strchr("$@%", v->name[0])) out.push_back(v);
                }
    };
    for (auto& s : body) {
        if (!s || s->kind != NK::ExprStmt) continue;
        const Expr* e = static_cast<const ExprStmt*>(s.get())->e.get();
        if (e && e->kind == NK::Assign) one(static_cast<const Assign*>(e)->target.get());
        else one(e);
    }
}
void Interpreter::runStaticPhasers(const std::vector<StmtPtr>& stmts, const std::shared_ptr<Env>& unitEnv,
                                   bool unitIsLive) {
    std::vector<StaticPhaserRec> recs;
    std::vector<const std::vector<StmtPtr>*> chain;
    for (auto& s : stmts) spWalkStmt(s.get(), chain, recs);
    if (recs.empty()) return;
    // A MAINLINE variable declared bare (`my $hist;`) is pre-declared before
    // any of this runs, and its declaration does not reset what a phaser put
    // there, so a phaser that reads or writes one may run early as well:
    // `"{ BEGIN { $hist ~= 'B' } }"` in an interpolation block.
    std::set<std::string> unitBare;
    if (!unitIsLive && unitEnv) {
        std::vector<const VarExpr*> decls;
        spDeclaredIn(stmts, decls);
        std::set<std::string> init;
        for (auto& st : stmts)
            if (st && st->kind == NK::ExprStmt) {
                const Expr* e = static_cast<const ExprStmt*>(st.get())->e.get();
                if (e && e->kind == NK::Assign && static_cast<const Assign*>(e)->target &&
                    (static_cast<const Assign*>(e)->op == "=" || static_cast<const Assign*>(e)->op == ":=") &&
                    static_cast<const Assign*>(e)->target->kind == NK::VarExpr)
                    init.insert(static_cast<const VarExpr*>(static_cast<const Assign*>(e)->target.get())->name);
            }
        // (`my $str ~= 'o'` is not an initializer that wipes: it appends to
        // whatever an INIT already left there)
        for (auto* v : decls) if (!init.count(v->name) && v->containerIs.empty()) unitBare.insert(v->name);
    }
    // Only a phaser whose variables all live in the scopes being made static —
    // or, for an EVAL, in the live code around it — runs early. One that reads
    // anything else (a mainline variable, an `our`, a container with a trait)
    // keeps running where it is written, as it always has.
    recs.erase(std::remove_if(recs.begin(), recs.end(), [&](const StaticPhaserRec& r) {
        std::set<std::string> ment;
        if (r.blk) collectMentionedB(r.blk, ment);
        else for (auto& st : r.be->body) collectMentionedS(st.get(), ment);
        std::set<std::string> declared, initialized;
        for (auto* scope : r.chain) {
            std::vector<const VarExpr*> decls;
            spDeclaredIn(*scope, decls);
            for (auto* v : decls) declared.insert(v->name);
            // a variable its scope INITIALIZES (`my $foo = 42`) keeps the phaser
            // in place: what reads it later (a `constant`, evaluated at compile
            // time in Rakudo) would see the initializer where it should see the
            // phaser's value
            for (auto& st : *scope)
                if (st && st->kind == NK::ExprStmt) {
                    const Expr* e = static_cast<const ExprStmt*>(st.get())->e.get();
                    if (e && e->kind == NK::Assign && static_cast<const Assign*>(e)->target &&
                        (static_cast<const Assign*>(e)->op == "=" || static_cast<const Assign*>(e)->op == ":=") &&
                        static_cast<const Assign*>(e)->target->kind == NK::VarExpr &&
                        static_cast<const VarExpr*>(static_cast<const Assign*>(e)->target.get())->declare)
                        initialized.insert(static_cast<const VarExpr*>(static_cast<const Assign*>(e)->target.get())->name);
                }
        }
        {   // no EVAL, and no routine the enclosing scopes declare
            std::set<std::string> calls;
            if (r.blk) for (auto& st : r.blk->stmts) spCallsS(st.get(), calls);
            else for (auto& st : r.be->body) spCallsS(st.get(), calls);
            if (calls.count("EVAL") || calls.count("\x01other")) return true;
            for (auto* scope : r.chain)
                for (auto& st : *scope)
                    if (st && st->kind == NK::SubDecl && calls.count(static_cast<SubDecl*>(st.get())->name))
                        return true;
        }
        for (auto& n : ment) {
            if (n.size() > 1 && n[0] == '&') return true;   // a routine variable: not modelled statically
            if (n.size() < 2 || !std::strchr("$@%", n[0])) continue;
            if (!(ascii::isalpha((unsigned char)n[1]) || n[1] == '_')) continue;   // $*x, $!x, $/, $_ …
            if (n == "$_") continue;
            if (initialized.count(n) && r.kind != "INIT") return true;   // an INIT's value is then wiped, as in Rakudo
            if (declared.count(n)) continue;
            if (unitIsLive && unitEnv && unitEnv->find(n)) continue;
            if (unitBare.count(n) && unitEnv->find(n)) continue;
            return true;
        }
        return false;
    }), recs.end());
    if (recs.empty()) return;
    auto staticEnvFor = [&](const StaticPhaserRec& r) -> std::shared_ptr<Env> {
        std::shared_ptr<Env> parent = unitEnv;
        for (auto* scope : r.chain) {
            auto it = staticEnvs_.find(scope);
            if (it == staticEnvs_.end()) {
                auto env = std::make_shared<Env>(); env->parent = parent;
                std::vector<const VarExpr*> decls;
                spDeclaredIn(*scope, decls);
                for (auto* v : decls) env->define(v->name, typedDefault(v->declType, v->name[0]));
                it = staticEnvs_.emplace(scope, env).first;
            }
            parent = it->second;
        }
        return parent;
    };
    auto runOne = [&](const StaticPhaserRec& r) {
        const void* key = r.blk ? (const void*)r.blk : (const void*)r.be;
        if (staticPhaserVal_.count(key)) return;
        auto senv = staticEnvFor(r);
        // A BEGIN written between a stub `sub f {...}` and the sub's real body
        // sees the STUB — at that point of the parse nothing else exists yet —
        // so calling it there is X::StubCode (S06-advanced/stub.t). The stub is
        // bound in a scope of its own, just for this phaser.
        if (r.kind == "BEGIN" && r.blk && !r.chain.empty()) {
            const auto& scope = *r.chain.back();
            size_t pi = scope.size();
            for (size_t i = 0; i < scope.size(); i++) if (scope[i].get() == r.blk) { pi = i; break; }
            auto isStubSub = [](const Stmt* s) {
                if (!s || s->kind != NK::SubDecl) return false;
                auto* sd = static_cast<const SubDecl*>(s);
                if (sd->body.size() != 1 || !sd->body[0] || sd->body[0]->kind != NK::ExprStmt) return false;
                const Expr* e = static_cast<const ExprStmt*>(sd->body[0].get())->e.get();
                if (!e || e->kind != NK::Call) return false;
                auto* c = static_cast<const Call*>(e);
                return (c->name == "..." || c->name == "!!!" || c->name == "???") && c->args.empty() && !c->callee;
            };
            std::shared_ptr<Env> ov;
            for (size_t i = 0; i < pi && pi < scope.size(); i++) {
                if (!isStubSub(scope[i].get())) continue;
                const std::string& nm = static_cast<const SubDecl*>(scope[i].get())->name;
                bool realLater = false;
                for (size_t j = pi + 1; j < scope.size() && !realLater; j++)
                    if (scope[j] && scope[j]->kind == NK::SubDecl && !isStubSub(scope[j].get()) &&
                        static_cast<const SubDecl*>(scope[j].get())->name == nm)
                        realLater = true;
                if (!realLater) continue;
                if (!ov) { ov = std::make_shared<Env>(); ov->parent = senv; }
                auto savedCur = tctx_.cur;
                tctx_.cur = ov;
                try { exec(scope[i].get()); } catch (...) { tctx_.cur = savedCur; throw; }
                tctx_.cur = savedCur;
            }
            if (ov) senv = ov;
        }
        auto saved = tctx_.cur;
        tctx_.cur = senv;
        Value v;
        try {
            // a statement-form phaser (`BEGIN $x = 1`, `BEGIN my $y = 2`) runs in
            // the scope itself, so a declaration it makes is the scope's
            if (r.blk && r.blk->stmtForm) v = execBlock(r.blk, senv);
            else if (r.blk) { auto sc = std::make_shared<Env>(); sc->parent = senv; v = execBlock(r.blk, sc); }
            else v = callCallable(makeClosure(r.be), {});
        } catch (ReturnEx&) {
            // `sub { CHECK return }` — no routine is running at compile time, so
            // the `return` has nothing to leave: X::ControlFlow::Return, which a
            // BEGIN or CHECK reports as a compile-time failure (return.t)
            tctx_.cur = saved;
            RakuError re{Value::typeObj("X::ControlFlow::Return"), "Attempt to return outside of any Routine"};
            if (r.kind == "INIT") throw re;
            Value inner = exceptionFor(re);
            throwTypedV("X::Comp::BeginTime", {{"exception", inner}, {"use-case", Value::str("evaluating a " + r.kind)}},
                        "An exception occurred while evaluating a " + r.kind + ": " + re.message);
        } catch (...) { tctx_.cur = saved; throw; }
        tctx_.cur = saved;
        staticPhaserVal_[key] = v;
    };
    for (auto& r : recs) if (r.kind == "BEGIN") runOne(r);
    for (auto it = recs.rbegin(); it != recs.rend(); ++it) if (it->kind == "CHECK") runOne(*it);
    for (auto& r : recs) if (r.kind == "INIT") runOne(r);
}
// A scope's FIRST run takes the lexicals its BEGIN-time phasers already set.
void Interpreter::seedStaticScope(const void* key, Env* env) {
    auto it = staticEnvs_.find(key);
    if (it == staticEnvs_.end() || !env) return;
    if (!staticSeeded_.insert(key).second) return;
    for (auto& kv : it->second->vars) env->define(kv.first, kv.second);
    env->staticSeeded = true;
}

// The frame of the routine whose body lexically encloses the current scope
// (0 when there is none — mainline, or a scope that lost its routine).
// A `return` needs the routine it was WRITTEN in to be running still: a
// block that outlived it (`sub a1 { my &x = { return }; &x }`) has nothing to
// return from — X::ControlFlow::Return, out of its dynamic scope
void Interpreter::checkReturnInDynamicScope(uint64_t lf) {
    if (!lf) return;
    // the routine ACTIVATION the return was written in, by identity (frame
    // numbers are reused by later calls at the same depth)
    Env* mine = nullptr;
    for (Env* e = tctx_.cur.get(); e; e = e->parent.get())
        if (e->routineFrame) { mine = e; break; }
    if (!mine || mine == tctx_.curRoutineEnv) return;
    // …running now: the current routine, or one of the callers'
    for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend(); ++it)
        for (Env* e = *it; e; e = e->parent.get())
            if (e->routineFrame) { if (e == mine) return; break; }
    throwTypedV("X::ControlFlow::Return", {{"out-of-dynamic-scope", Value::boolean(true)}},
                "Attempt to return outside of immediately-enclosing Routine (i.e. `return` execution "
                "is outside the dynamic scope of the Routine where `return` was used)");
}
uint64_t Interpreter::lexicalRoutineFrame() {
    for (Env* e = tctx_.cur.get(); e; e = e->parent.get())
        if (e->routineFrame) return e->routineFrameId;
    return 0;
}

// ---- gather as a coroutine -------------------------------------------------
// `gather BLOCK` runs nothing when it is evaluated. It is a Seq whose block
// runs on a stack of its own (Coro.h) the first time something pulls from it,
// and only as far as that pull needs: a `take` that brings the collected
// count up to what the consumer asked for switches straight back to the
// consumer, leaving the block suspended where it is — inside whatever loops,
// calls and scopes it was in — until the next pull resumes it. Rakudo's gather
// is the same shape (a continuation per take), which is what lets
//     my $g := gather { for 1..5 { $n++; take $_ } };  $g[0];   # $n == 1
// and `for gather { … } { last }` pull exactly one element.
//
// This replaced a probe that ran the block up to 64 takes when the gather was
// WRITTEN, and a block that outgrew the probe was re-run from the start, with a
// snapshot of the variables it wrote, every time more was wanted. That code is
// still here for targets that have no context switch (RAKUPP_HAVE_CORO).
//
// What a switch has to carry. The interpreter keeps the state of the code it is
// running in thread-local "registers" — tctx_ (an ExecContext) and a set of
// statics set around a region and restored after it. A suspended block is in
// the middle of such regions, and so is the consumer, so every switch swaps
// the whole set: the block runs with its own, the consumer gets its own back.
// Three kinds of state are not simply swapped:
//  · the DYNAMIC chains — dynamic variables, CONTROL handlers, callframes —
//    are the consumer's, with the block's own frames on top. Rakudo resolves
//    `$*X` in a gather's block through whoever is reifying it, and `quietly`
//    around the consumer mutes the block's warnings;
//  · the counted scopes the block inherits the same way (`quietly`, CATCH
//    depth) keep the block's own net change;
//  · the recursion guard measures the stack that is actually running.
//
// A block that dies, or a `last`/`next` in it aimed at a loop outside, ends
// its gather: the exception is carried to the consumer and raised where the
// pull happened — the dynamic scope Rakudo raises it in. A `return` has no
// routine to return from and is X::ControlFlow::Return, as before.
//
// A gather dropped while its block is suspended still has C++ frames on its
// stack that own things (and may hold locks). It goes to a graveyard, and at
// the next gather operation on its thread it is resumed once more with
// `cancel` set: its `take` throws StopGatherEx, the frames unwind, and LEAVE
// phasers stay silent on the way out (Rakudo never runs them for a gather that
// is simply dropped). A suspended block is thread-affine (Coro.h); one dropped
// on another thread, or resumed from one, cannot be.
#if RAKUPP_HAVE_CORO
struct GatherRegs {
    ExecContext ctx;
    Value* builtinTopicWB = nullptr;
    const Interpreter::ArgWriter* builtinArgWriter = nullptr;
    bool deferGather = false, valueSmartmatch = false,
         matchVarSuppressed = false, hoistingSubs = false,
         suppressLoopFirst = false, fatalTry = false;
    std::string declaringType;
    std::vector<Interpreter::RedispatchCtx> redispatchStack;
    std::vector<Interpreter::ProtoCtx> protoStack;
    std::vector<std::shared_ptr<ReactCtx>> reactStack;
    const Value* rxRoutine = nullptr;
    const std::string* hyperOpName = nullptr;
    std::vector<RxTempSave> rxTemps;
    std::vector<std::shared_ptr<Env>> evalUnits;
    std::vector<std::string> classBodies;
    char* stackTop = nullptr;
    size_t stackLimit = 0;
    long long gatherDeadline = 0;
    unsigned gatherTickCtr = 0;
    int stmtLine = 0;
    // the block's own net change to the counted scopes it inherits
    int quietDelta = 0, catchDelta = 0;
    // while the block runs: where the consumer's part of each chain ends, and
    // the consumer's counts
    size_t dynBase = 0, ctlBase = 0, framesBase = 0;
    int quietBase = 0, catchBase = 0;
};

struct GatherCoro {
    Interpreter* I = nullptr;
    Unary* gu = nullptr;          // the `gather` node
    Value block;                  // its closure, for a block operand
    std::shared_ptr<Env> env;     // the scope a statement operand runs in
    std::string pkgPrefix;        // the package the gather was written in
    std::vector<int> endUnitKey;  // …and where it stands in the END order
    const Stmt* endCurTopStmt = nullptr;
    // What the block has taken since the last hand-over: the collector of the
    // gather frame the block runs under. Emptied into the Seq at each pull.
    PRef<ValueList> buf = makePayload<ValueList>();
    size_t want = 1;              // hand back once buf holds this many
    bool cancel = false;          // resumed only to unwind (see above)
    std::exception_ptr err;       // what ended the block, raised at the pull
    GatherRegs regs;              // the block's registers while it is suspended
    Coro co{&GatherCoro::entry, this};
    static void entry(void* p);
};

static void setCurrentStmtLine(int l) {
    if (g_stmtLineThreaded.load(std::memory_order_relaxed)) t_stmtLine = l;
    else g_stmtLine.store(l, std::memory_order_relaxed);
}

// Swap two execution contexts member by member. std::swap on the whole struct
// builds and destroys a 1.4 KB temporary and move-ASSIGNS its containers — a
// deque that clears and shrinks, a hash map that rebuilds — and profiling a
// `for` over a gather put 40% of the loop in exactly that. Member by member,
// every container swap is O(1).
//
// EVERY member of ExecContext must be listed here: one left out would leak
// between a gather's block and its consumer. The size check below trips on the
// machine of record when the struct grows, so a new register cannot be missed
// silently; update the list and the number together.
static void swapExecContext(ExecContext& a, ExecContext& b) {
    using std::swap;
    swap(a.cur, b.cur); swap(a.subSigBind, b.subSigBind); swap(a.rwInvocantExpr, b.rwInvocantExpr); swap(a.endUnitKey, b.endUnitKey);
    swap(a.endsBeforeStmt, b.endsBeforeStmt); swap(a.endCurTopStmt, b.endCurTopStmt); swap(a.dynStack, b.dynStack); swap(a.callDepth, b.callDepth);
    swap(a.nqpArgs, b.nqpArgs); swap(a.nqpDepth, b.nqpDepth); swap(a.curStateEnv, b.curStateEnv); swap(a.gatherStack, b.gatherStack);
    swap(a.gatherLimits, b.gatherLimits); swap(a.gatherDeadlines, b.gatherDeadlines); swap(a.topicAliases, b.topicAliases); swap(a.supplyStack, b.supplyStack);
    swap(a.tapStack, b.tapStack); swap(a.makeTargets, b.makeTargets); swap(a.controlHandlers, b.controlHandlers); swap(a.pkgPrefix, b.pkgPrefix);
    swap(a.returning, b.returning); swap(a.returnV, b.returnV); swap(a.frameTop, b.frameTop); swap(a.redispatchFloor, b.redispatchFloor);
    swap(a.curRoutineFrame, b.curRoutineFrame); swap(a.curRoutineEnv, b.curRoutineEnv); swap(a.builtinFallback, b.builtinFallback); swap(a.loopCtl, b.loopCtl);
    swap(a.curStmtExpr, b.curStmtExpr); swap(a.valContained, b.valContained); swap(a.metaForwarding, b.metaForwarding); swap(a.curLoopFrame, b.curLoopFrame);
    swap(a.givenCtl, b.givenCtl); swap(a.givenV, b.givenV); swap(a.curGivenFrame, b.curGivenFrame); swap(a.curBlockVal, b.curBlockVal);
    swap(a.curRoutineVal, b.curRoutineVal); swap(a.callFrames, b.callFrames); swap(a.leaveResult, b.leaveResult); swap(a.leaveReturned, b.leaveReturned);
    swap(a.leaveReturnV, b.leaveReturnV); swap(a.leaveError, b.leaveError); swap(a.arityCallName, b.arityCallName); swap(a.wantLvalue, b.wantLvalue);
    swap(a.wantTailContainer, b.wantTailContainer); swap(a.catchFrames, b.catchFrames); swap(a.transpFrames, b.transpFrames); swap(a.ctxId, b.ctxId);
    swap(a.bindRawTails, b.bindRawTails); swap(a.tailVarSlot, b.tailVarSlot); swap(a.rwMirror, b.rwMirror); swap(a.rwMirrorSigil, b.rwMirrorSigil); swap(a.lvalueImmutable, b.lvalueImmutable);
    swap(a.lvalueImmutableGist, b.lvalueImmutableGist); swap(a.lvalueImmutableVal, b.lvalueImmutableVal); swap(a.lvalueOutLocal, b.lvalueOutLocal); swap(a.lvalueOut, b.lvalueOut);
    swap(a.lvalueOutCell, b.lvalueOutCell); swap(a.collectTailBody, b.collectTailBody); swap(a.collectTail, b.collectTail); swap(a.protoDepth, b.protoDepth);
    swap(a.lastLvalueAttrType, b.lastLvalueAttrType); swap(a.lastLvalueAttrWhere, b.lastLvalueAttrWhere); swap(a.lastLvalueAttrDefault, b.lastLvalueAttrDefault); swap(a.lastLvalueAttr, b.lastLvalueAttr); swap(a.lastLvalueElemType, b.lastLvalueElemType);
    swap(a.dynMethodNode, b.dynMethodNode); swap(a.dynMethodName, b.dynMethodName); swap(a.curGather, b.curGather);
    swap(a.ctorCatchSkip, b.ctorCatchSkip); swap(a.ctorCatchDepth, b.ctorCatchDepth);
    swap(a.topicWriteback, b.topicWriteback); swap(a.pendingRwSlots, b.pendingRwSlots);
    swap(a.pendingArgWriter, b.pendingArgWriter); swap(a.noAutothread, b.noAutothread);
    swap(a.forceRoutineFrame, b.forceRoutineFrame); swap(a.loopPhaserCtl, b.loopPhaserCtl);
    // (framePool is not listed, deliberately: it is the OS thread's scratch —
    // a free list of unused frames — and either side may use it)
}
#if defined(__APPLE__) && defined(__aarch64__) && defined(_LIBCPP_VERSION) && !defined(RAKUPP_IN_AUDIT)
static_assert(sizeof(ExecContext) == 1312,
              "ExecContext changed: list the new member in swapExecContext (Interpreter.cpp), "
              "then update this size");
#endif

// The addresses of every thread-local register a switch swaps, resolved ONCE
// per thread. Each mention of a thread_local is a _tlv_get_addr call on macOS
// (and an init guard for the non-trivial ones); a switch touching thirty of
// them four times over spent more in those calls than in the swap itself.
struct GatherTls {
    ExecContext* ctx;
    Value** builtinTopicWB;
    const Interpreter::ArgWriter** builtinArgWriter;
    bool *deferGather, *valueSmartmatch, *matchVarSuppressed,
         *hoistingSubs, *suppressLoopFirst, *fatalTry;
    std::string* declaringType;
    std::vector<Interpreter::RedispatchCtx>* redispatchStack;
    std::vector<Interpreter::ProtoCtx>* protoStack;
    std::vector<std::shared_ptr<ReactCtx>>* reactStack;
    const Value** rxRoutine;
    const std::string** hyperOpName;
    std::vector<RxTempSave>* rxTemps;
    std::vector<std::shared_ptr<Env>>* evalUnits;
    std::vector<std::string>* classBodies;
    char** stackTop; size_t* stackLimit;
    long long* gatherDeadline; unsigned* gatherTickCtr;
};
static thread_local GatherTls* t_gatherTls = nullptr;   // trivial: no init guard
static GatherTls& gatherTls() {
    if (GatherTls* p = t_gatherTls) return *p;
    auto* p = new GatherTls{   // one per thread that runs a gather, deliberately never freed
        &Interpreter::tctx_,
        &Interpreter::builtinTopicWB_,
        &Interpreter::builtinArgWriter_,
        &Interpreter::deferGather_, &Interpreter::valueSmartmatch_,
        &Interpreter::matchVarSuppressed_,
        &Interpreter::hoistingSubs_, &Interpreter::suppressLoopFirst_, &t_fatalTry,
        &Interpreter::declaringType_,
        &Interpreter::redispatchStack_, &Interpreter::protoStack_, &Interpreter::reactStack_,
        &g_rxRoutine, &g_hyperOpName, &g_rxTemps, &g_evalUnits, &g_classBodies,
        &t_stack.top, &t_stack.limit, &t_poll.gatherDeadline, &t_poll.gatherTickCtr};
    t_gatherTls = p;
    return *p;
}

// Swap every register but the ExecContext (identical going in and out).
static void gatherSwapStatics(const GatherTls& T, GatherRegs& r) {
    std::swap(*T.builtinTopicWB, r.builtinTopicWB);
    std::swap(*T.builtinArgWriter, r.builtinArgWriter);
    std::swap(*T.deferGather, r.deferGather);
    std::swap(*T.valueSmartmatch, r.valueSmartmatch);
    std::swap(*T.matchVarSuppressed, r.matchVarSuppressed);
    std::swap(*T.hoistingSubs, r.hoistingSubs);
    std::swap(*T.suppressLoopFirst, r.suppressLoopFirst);
    std::swap(*T.fatalTry, r.fatalTry);
    T.declaringType->swap(r.declaringType);
    T.redispatchStack->swap(r.redispatchStack);
    T.protoStack->swap(r.protoStack);
    T.reactStack->swap(r.reactStack);
    std::swap(*T.rxRoutine, r.rxRoutine);
    std::swap(*T.hyperOpName, r.hyperOpName);
    T.rxTemps->swap(r.rxTemps);
    T.evalUnits->swap(r.evalUnits);
    T.classBodies->swap(r.classBodies);
    std::swap(*T.stackTop, r.stackTop);
    std::swap(*T.stackLimit, r.stackLimit);
    std::swap(*T.gatherDeadline, r.gatherDeadline);
    std::swap(*T.gatherTickCtr, r.gatherTickCtr);
    { int line = currentStmtLine(); setCurrentStmtLine(r.stmtLine); r.stmtLine = line; }
}

// The consumer's registers out, the block's in. The resumer calls this right
// before Coro::resume and gatherSwapOut right after it returns, so the block
// itself never has to.
static void gatherSwapIn(Interpreter& I, GatherRegs& r) {
    const GatherTls& T = gatherTls();
    swapExecContext(*T.ctx, r.ctx);
    ExecContext& live = *T.ctx;
    const ExecContext& cons = r.ctx;
    // …the consumer's callers, then the consumer's own frame, exactly what a
    // call from the pull site would have stacked
    r.dynBase = cons.dynStack.size() + 1;
    live.dynStack.insert(live.dynStack.begin(), cons.dynStack.begin(), cons.dynStack.end());
    live.dynStack.insert(live.dynStack.begin() + cons.dynStack.size(),
                         cons.cur ? cons.cur.get() : nullptr);
    r.ctlBase = cons.controlHandlers.size();
    live.controlHandlers.insert(live.controlHandlers.begin(), cons.controlHandlers.begin(),
                                cons.controlHandlers.end());
    r.framesBase = cons.callFrames.size();
    live.callFrames.insert(live.callFrames.begin(), cons.callFrames.begin(), cons.callFrames.end());
    gatherSwapStatics(T, r);
    r.quietBase = I.quietDepth_; I.quietDepth_ += r.quietDelta;
    r.catchBase = I.catchDepth_; I.catchDepth_ += r.catchDelta;
}

static void gatherSwapOut(Interpreter& I, GatherRegs& r) {
    const GatherTls& T = gatherTls();
    ExecContext& live = *T.ctx;
    auto dropPrefix = [](auto& v, size_t n) { v.erase(v.begin(), v.begin() + std::min(n, v.size())); };
    dropPrefix(live.dynStack, r.dynBase);
    dropPrefix(live.controlHandlers, r.ctlBase);
    dropPrefix(live.callFrames, r.framesBase);
    swapExecContext(*T.ctx, r.ctx);
    gatherSwapStatics(T, r);
    r.quietDelta = I.quietDepth_ - r.quietBase; I.quietDepth_ = r.quietBase;
    r.catchDelta = I.catchDepth_ - r.catchBase; I.catchDepth_ = r.catchBase;
}

// Runs on the coroutine's own stack, with the block's registers already live.
void GatherCoro::entry(void* p) {
    auto* g = static_cast<GatherCoro*>(p);
    Interpreter& I = *g->I;
    ExecContext& t = Interpreter::tctx_;
    // the recursion guard measures THIS stack from here on (on Windows a fresh
    // fiber's top is not known yet, and the guard notes it at its first frame)
    t_stack.top = g->co.stackTop();
    t_stack.limit = g->co.stackUsable();
    t.curGather = g;
    t.cur = g->env;
    t.pkgPrefix = g->pkgPrefix;
    t.endUnitKey = g->endUnitKey;
    t.endCurTopStmt = g->endCurTopStmt;
    I.pushGatherFrame(g->buf, 0, 0);
    try {
        if (g->block.t == VT::Code) { ValueList none; I.callCallable(g->block, none); }
        else I.eval(g->gu->operand.get());
        // a `return` in the block has no routine to return from: the gather
        // runs lazily, after (or apart from) whatever routine wrote it
        if (t.returning) {
            t.returning = false;
            throw RakuError{Value::typeObj("X::ControlFlow::Return"),
                            "Attempt to return outside of any Routine"};
        }
    }
    catch (StopGatherEx&) {}   // cancelled: the consumer let go
    // a plain `last` — in the block itself or in a routine it calls — ends the
    // gather, as it does in Rakudo; one aimed at a LABEL flies on to its loop
    catch (LastEx& e) { if (!e.label.empty()) g->err = std::current_exception(); }
    catch (ReturnEx&) {
        t.returning = false;
        g->err = std::make_exception_ptr(RakuError{Value::typeObj("X::ControlFlow::Return"),
                                                   "Attempt to return outside of any Routine"});
    }
    catch (...) { g->err = std::current_exception(); }
    I.popGatherFrame();
    t.curGather = nullptr;
}

// Dropped while suspended, waiting to be unwound on their thread (see above).
// Deliberately a leaked pointer: it must survive this thread's TLS teardown,
// during which the last Values can still be released.
static thread_local std::vector<GatherCoro*>* t_gatherGraveyard = nullptr;

static void gatherRun(Interpreter& I, GatherCoro* g) {
    gatherSwapIn(I, g->regs);
    g->co.resume();
    gatherSwapOut(I, g->regs);
}

static void reapGatherGraveyard(Interpreter& I) {
    auto* gy = t_gatherGraveyard;
    if (!gy || gy->empty()) return;
    static thread_local bool reaping = false;
    if (reaping) return;
    reaping = true;
    while (!gy->empty()) {
        GatherCoro* g = gy->back();
        gy->pop_back();
        g->cancel = true;
        gatherRun(I, g);
        delete g;
    }
    reaping = false;
}

// The deleter of the GatherCoro a gather's Seq owns.
static void gatherRelease(GatherCoro* g) {
    if (g->co.started() && !g->co.finished() && !g->co.running() &&
        g->co.ownerThread() == std::this_thread::get_id()) {
        if (!t_gatherGraveyard) t_gatherGraveyard = new std::vector<GatherCoro*>;
        t_gatherGraveyard->push_back(g);
        return;
    }
    delete g;   // not started, finished — or suspended on another thread: abandoned
}

// One pull: run the block until it has taken `pullHint` more (at least one),
// or ends, and move what it took into `out`. False once it has ended.
static bool gatherPull(Interpreter& I, GatherCoro* g, LazySeqState* st, ValueList& out) {
    const size_t want = st->pullHint ? st->pullHint : 1;
    st->pullHint = 0;
    if (g->co.finished()) { st->exhausted = true; return false; }
    if (g->co.running())
        throw RakuError{Value::typeObj("X::AdHoc"),
                        "Cannot pull from a gather while its own block is producing it"};
    if (g->co.started() && g->co.ownerThread() != std::this_thread::get_id())
        throw RakuError{Value::typeObj("X::AdHoc"),
                        "A gather that has started producing can only be read on the thread that "
                        "started it"};
    reapGatherGraveyard(I);
    g->want = want;
    gatherRun(I, g);
    // (no reserve(): growing by the exact amount on every one-element pull
    // would defeat the vector's doubling and make a `for` over a gather
    // quadratic)
    if (!g->buf->empty()) {
        for (auto& x : *g->buf) out.push_back(std::move(x));
        g->buf->clear();
    }
    if (!g->co.finished()) return true;
    st->exhausted = true;
    if (g->err) { std::exception_ptr e = g->err; g->err = nullptr; std::rethrow_exception(e); }
    return false;
}

// The Seq over a gather whose block is `block` (a closure) or, when that is
// empty, `gu`'s statement operand run in the current scope.
static Value gatherSeqOver(Interpreter& I, Value block, Unary* gu, bool declaredLazy);
Value Interpreter::makeGatherSeq(Unary* gu, bool declaredLazy) {
    Value block;
    if (gu->operand->kind == NK::BlockExpr)
        block = makeClosure(static_cast<BlockExpr*>(gu->operand.get()));
    return gatherSeqOver(*this, std::move(block), gu, declaredLazy);
}
static Value gatherSeqOver(Interpreter& I, Value block, Unary* gu, bool declaredLazy) {
    ExecContext& tctx_ = Interpreter::tctx_;
    reapGatherGraveyard(I);
    std::shared_ptr<GatherCoro> g(new GatherCoro, &gatherRelease);
    g->I = &I;
    g->gu = gu;
    g->block = std::move(block);
    g->env = tctx_.cur;
    g->pkgPrefix = tctx_.pkgPrefix;
    g->endUnitKey = tctx_.endUnitKey;
    g->endCurTopStmt = tctx_.endCurTopStmt;
    g->regs.stmtLine = currentStmtLine();
    // The block is lexically inside the routine that WROTE the gather, and its
    // `samewith`/`callsame` mean that routine's dispatch however late it runs:
    // Digest's SHA-3 is `multi Keccak(…) { gather for samewith … { … } }`.
    // (The redispatch stack is a region-scoped static, so the block would
    // otherwise start with an empty one — see gatherSwapStatics.)
    // The block may run after that routine has returned, so only what outlives
    // it is kept: `restart` (samewith) holds its dispatcher by value, while a
    // WRAPPER frame's closures and every `next` (callsame, nextsame) point into
    // the dispatching call's own locals. Without `next` the frame reads as the
    // last candidate, which is what those then answer.
    for (const auto& rc : Interpreter::redispatchStack_) {
        if (rc.wrapperFrame) continue;
        Interpreter::RedispatchCtx kept = rc;
        kept.next = nullptr;
        kept.lastcall = true;
        g->regs.redispatchStack.push_back(std::move(kept));
    }
    Value arr = Value::array(); arr.isList = true; arr.s = "Seq";
    if (declaredLazy) arr.b = true;   // .is-lazy
    auto st = std::make_shared<LazySeqState>();
    st->gatherSeq = true;
    st->declaredLazy = declaredLazy;
    LazySeqState* stp = st.get();
    Interpreter* self = &I;
    st->appendNext = [self, g, stp](ValueList& out) -> bool { return gatherPull(*self, g.get(), stp, out); };
    arr.extM() = st;
    arr.setSeqTok(makeSlabShared<SeqToken>());   // read once (SeqToken)
    return arr;
}

void Interpreter::gatherTakeYield(ValueList& coll) {
    GatherCoro* g = tctx_.curGather;
    if (!g || &coll != g->buf.get()) return;
    if (g->cancel) {
        // keep unwinding: a take the block reaches after swallowing the first
        // StopGatherEx is thrown out of too — but never from a destructor
        if (std::uncaught_exceptions() == 0) throw StopGatherEx{};
        return;
    }
    if (coll.size() < g->want) return;
    g->co.yield();
    if (g->cancel) throw StopGatherEx{};
}

bool gatherCancelling() {
    GatherCoro* g = Interpreter::tctx_.curGather;
    return g && g->cancel;
}
Env* gatherDynBoundary(GatherCoro* g) {
    if (g->block.t == VT::Code && g->block.code()) return g->block.code()->closure.get();
    return g->env.get();
}
size_t gatherDynBase(GatherCoro* g) { return g->regs.dynBase; }
Value gatherSeqForNative(Interpreter& I, Value blockClosure) {
    return gatherSeqOver(I, std::move(blockClosure), nullptr, false);
}
#else
Value gatherSeqForNative(Interpreter&, Value) { return Value::any(); }
Env* gatherDynBoundary(GatherCoro*) { return nullptr; }
size_t gatherDynBase(GatherCoro*) { return 0; }
void Interpreter::gatherTakeYield(ValueList&) {}
Value Interpreter::makeGatherSeq(Unary*, bool) { return Value::any(); }
bool gatherCancelling() { return false; }
#endif

// Stringify honouring user-defined `method gist` / `method Str` (Raku: say/note use
// .gist; print/put/string interpolation use .Str, which itself falls back to .gist).
// The value stored into $! / $_ when an error is caught. A bare exception TYPE
// payload (`throw RakuError{Value::typeObj("X::Foo"), msg}`) would be an
// UNDEFINED type object — `$!.defined` must be True and `.message` must answer,
// so wrap it into a defined instance of that class (registered on the fly).
void Interpreter::warnUninit(const std::string& msg) {
    // …with the line it happened on, as `warn` shows it (Rakudo does both)
    if (quietDepth_ == 0 && !runControlWarn(msg)) std::cerr << msg << "\n" << warnFrame();
}
bool Interpreter::runControlWarn(const std::string& msg) {
    if (tctx_.controlHandlers.empty()) return false;
    return runControlException(exceptionFor(RakuError{Value::typeObj("CX::Warn"), msg}));
}
// A CONTROL exception (`warn`'s CX::Warn, or a user class `is X::Control`) goes
// to the innermost CONTROL block rather than to CATCH. True when the handler
// `.resume`d (execution carries on after the throw); a handler that matched
// without resuming leaves its block (ControlHandledEx); false = nobody took it.
bool Interpreter::runControlException(const Value& ex) {
    if (tctx_.controlHandlers.empty()) return false;
    // pop while running: a warn INSIDE the handler goes to the next one out
    auto handler = tctx_.controlHandlers.back();
    tctx_.controlHandlers.pop_back();
    struct Repush {
        ExecContext& t;
        std::pair<Block*, std::shared_ptr<Env>> h;
        ~Repush() { t.controlHandlers.push_back(std::move(h)); }
    } repush{tctx_, handler};

    auto env = std::make_shared<Env>();
    env->parent = handler.second;
    env->define("$_", ex);
    env->define("$!", ex);
    auto saved = tctx_.cur;
    tctx_.cur = env;
    uint64_t savedGF = tctx_.curGivenFrame;
    tctx_.curGivenFrame = ExecContext::kNoFrame; // `when` inside the handler must THROW to be seen
    struct Restore {
        Interpreter& I; std::shared_ptr<Env> e; uint64_t gf;
        ~Restore() { I.tctx_.cur = e; I.tctx_.curGivenFrame = gf; }
    } restore{*this, saved, savedGF};
    bool resumed = false, handled = false;
    try {
        struct G { int& d; G(int& x) : d(x) { d++; } ~G() { d--; } } g{catchDepth_}; // .resume is legal here
        for (auto& s : handler.first->stmts) exec(s.get());
    }
    catch (BreakGivenEx&) { handled = true; }   // a when/default matched
    catch (ResumeEx&) { resumed = true; }
    // Handled but not resumed: the warning is consumed AND the block that
    // declared the CONTROL is left, exactly as a CATCH leaves its block (Rakudo:
    // `{ CONTROL { when CX::Warn { … } }; warn "x"; say "after" }` never says
    // "after"). S32-basics/warn.t plans on it — its first block's two tests
    // after the warn are never reached, and the plan of 9 counts them out; we
    // ran on and emitted 11. A handler that matched nothing lets the warning
    // through to the default printer, and execution continues.
    if (handled && !resumed) throw ControlHandledEx{handler.first};
    return resumed;
}

// The opaque handle an exception object carries until something asks for its
// backtrace: one shared_ptr, no BacktraceFrame hashes built. `s` doubles as the
// origin file (the frame files that have no declaring routine).
static void attachFrames(Value& ex, const RakuError& e) {
    if (!e.bt || e.bt->frames.empty()) return;
    if (ex.t != VT::Object || !ex.obj()) return;
    // never overwrite: a rethrow, a `.resume`, or `die $caught` keeps the
    // position the exception was FIRST thrown from, as Rakudo's does
    if (ex.obj()->attrs.count("__bt")) return;
    Value h = Value::any();
    h.extM() = e.bt;
    ex.obj()->attrs["__bt"] = std::move(h);
}

Value Interpreter::exceptionFor(const RakuError& e) {
    // A Str payload NAMING an exception type ("X::Recursion", "X::Multi::NoMatch")
    // builds a real exception object like a type payload would — otherwise the
    // caught value is a bare Str and `.message` in CATCH dies, masking the error.
    bool strTypeName = e.payload.t == VT::Str && e.payload.s.rfind("X::", 0) == 0;
    if (e.payload.t != VT::Type && !strTypeName) {
        Value p = e.payload;
        attachFrames(p, e);   // `die $obj` / the X::AdHoc `die "msg"` builds
        return p;
    }
    std::string tn = e.payload.s.empty() ? std::string("X::AdHoc") : e.payload.s.str();
    std::shared_ptr<ClassInfo> ci;
    auto it = classes_.find(tn);
    if (it != classes_.end()) ci = it->second;
    else {
        ci = std::make_shared<ClassInfo>();
        ci->name = tn;
        ci->nativeParent = "Exception";   // as the registered ones are
        ClassAttr a; a.name = "message"; a.sigil = '$'; a.pub = true;
        ci->attrs.push_back(a);
        // …and an X::AdHoc kind needs the accessor as well as the value: the
        // attribute below is set on the object either way, but `.payload` is a
        // missing METHOD without a declaration to generate it from.
        if (isAdHocKind(tn)) { ClassAttr pa; pa.name = "payload"; pa.sigil = '$'; pa.pub = true; ci->attrs.push_back(pa); }
        classes_[tn] = ci;
    }
    auto od = makePayload<ObjectData>();
    od->cls = ci;
    od->attrs["message"] = Value::str(e.message);
    // An undeclared routine or type carries Rakudo's "Did you mean" hashes —
    // `routine_suggestion<huc>` is ["uc"] — recovered from the name the message
    // quotes, since every site that throws it names the symbol that way.
    if (tn == "X::Undeclared::Symbols") {
        auto quoted = [&](const std::string& lead) -> std::string {
            if (e.message.rfind(lead, 0) != 0) return "";
            size_t q = e.message.find('\'', lead.size());
            return q == std::string::npos ? "" : e.message.substr(lead.size(), q - lead.size());
        };
        std::string rn = quoted("Undefined routine '"), tyn = quoted("Undeclared name '");
        Value rs = Value::makeHash(), ts = Value::makeHash();
        std::vector<std::string> sug;
        if (!rn.empty()) sug = routineSuggestions(rn);
        else if (!tyn.empty()) sug = typeSuggestions(tyn);
        if (!sug.empty()) {
            Value sl = Value::array(); sl.isList = true;
            for (auto& n : sug) sl.arr()->push_back(Value::str(n));
            (*(rn.empty() ? ts : rs).hash())[rn.empty() ? tyn : rn] = sl;
            if (e.message.find("Did you mean") == std::string::npos)
                od->attrs["message"] = Value::str(e.message + didYouMean(sug));
        }
        od->attrs["routine_suggestion"] = rs;
        od->attrs["type_suggestion"] = ts;
        for (const char* an : {"routine_suggestion", "type_suggestion"})
            if (!ci->findAttr(an)) { ClassAttr a; a.name = an; a.sigil = '$'; a.pub = true; ci->attrs.push_back(a); }
    }
    // an X::AdHoc's .payload is whatever was passed to `die` — for `die "msg"`
    // that's the message itself, and the same holds for anything parented to it
    if (isAdHocKind(tn)) od->attrs["payload"] = Value::str(e.message);
    Value out = Value::object(od);
    attachFrames(out, e);
    return out;
}

// A Failure carries `exception` as a bare type object and the diagnostic in a
// separate `message` slot. That pair is fine internally, but the moment the
// exception reaches user code — `$!` after a `try`, `.exception`, `.throw` —
// it has to be a DEFINED instance: an undefined type object boolifies False,
// so `try { "a".Int }; if $! {…}` never entered its block, and `.message` on it
// was a missing method rather than the diagnostic we had all along.
Value Interpreter::failureException(const Value& failure) {
    if (failure.t != VT::Hash || failure.hashKind != "Failure" || !failure.hash())
        return Value::nil();
    auto it = failure.hash()->find("exception");
    Value ex = it != failure.hash()->end() ? it->second : Value::typeObj("X::AdHoc");
    if (ex.t != VT::Type) return ex; // already an instance (`fail $obj`)
    auto mm = failure.hash()->find("message");
    return exceptionFor(RakuError{ex, mm != failure.hash()->end() ? mm->second.toStr() : ex.s.str(),
                                  RakuError::NoCapture{}});
}
// An UNHANDLED Failure detonates the moment its value is actually used —
// `say +"a"` throws, while `(+"a").defined` stays quiet. (`.handled`, set by
// `try`/CATCH/`.so`, makes it inert.)
void failureDetonate(const Value& v) {
    if (v.t == VT::Hash && v.hashKind == "Failure" && v.hash()) {
        auto h = v.hash()->find("handled");
        if (h != v.hash()->end() && h->second.truthy()) return;
        auto m = v.hash()->find("message");
        auto e = v.hash()->find("exception");
        RakuError err{e != v.hash()->end() ? e->second : Value::typeObj("X::AdHoc"),
                      m != v.hash()->end() ? m->second.toStr() : "Failure"};
        // A Failure has TWO positions, and the one the reader needs first is
        // where it was made: `my $r = risky()` is where the error is, and this
        // line merely used the value. The constructor captured the detonation;
        // put the creation site in front of it and label the other, as Rakudo
        // does. (Rakudo's word for the detonation is "Actually thrown at:".)
        if (auto made = btOf(v)) {
            err.altBt = err.bt;
            err.altLabel = "Actually thrown at:";
            err.bt = made;
        }
        throw err;
    }
}
std::string Interpreter::gistOf(const Value& v, bool skipUser) {
    failureDetonate(v);
    if (v.t == VT::Type && !bareStubTypes_.empty() && bareStubTypes_.count(std::string(v.s.c_str())))
        throwTypedV("X::Method::NotFound", {{"method", Value::str("gist")}, {"typename", Value::str(v.s.str())}},
                    "Method " + v.s.str() + ".gist not found");
    // an enum's type object gists as any type object does: `say day` is (day)
    if (isEnumTypeObject(v)) return "(" + std::string(v.enumType.str()) + ")";
    // An ENDLESS lazy sequence gists as Rakudo's "(...)", and a lazy ARRAY as
    // "[...]" — neither shows what happens to be reified, because say/print
    // must not pretend the cached prefix is the whole list (sheet LA-02).
    if (v.t == VT::Array && v.arr() && v.ext() &&
        std::static_pointer_cast<LazySeqState>(v.ext())->infinite)
        return v.isList ? "(...)" : "[...]";
    // Gisting a container READS it, so a Proxy runs FETCH — `say $q<baz>` must
    // show the value, not the Proxy's own FETCH/STORE pair. The subscript path
    // still hands back the container so a write can reach STORE.
    if (v.t == VT::Hash && v.hashKind == "Proxy" && v.hash()) return gistOf(deproxy(v));
    // …and so does a Proxy sitting inside a list.
    if (v.t == VT::Array && v.arr() && v.enumName.empty()) {
        bool anyProxy = false;
        for (auto& e : *v.arr()) if (e.t == VT::Hash && e.hashKind == "Proxy") { anyProxy = true; break; }
        if (anyProxy) {
            Value copy = v;
            copy.setArr(makePayload<ValueList>());
            for (auto& e : *v.arr()) copy.arr()->push_back(deproxy(e));
            return gistOf(copy);
        }
    }
    // a DateTime/Date carrying a :formatter gists through .Str (which runs it) —
    // `say DateTime.now(formatter => …)` shows the formatted form
    if (v.t == VT::Hash && (v.hashKind == "DateTime" || v.hashKind == "Date") &&
        v.hash() && v.hash()->count("formatter"))
        return methodCall(v, "Str", {}).toStr();
    // a class's own `method gist` answers for its TYPE OBJECT too
    // (`my class { method gist { 'I ♥ Raku' } }` says that, not `(<anon>)`)
    if (v.t == VT::Type && !skipUser && !v.s.empty()) {
        auto ci = classes_.find(v.s);
        if (ci != classes_.end() && ci->second && !ci->second->isRole)
            if (Value* gm = ci->second->findMethod("gist"))
                if (gm->t == VT::Code && gm->code() && !gm->code()->builtin)
                    return invokeMethodChain("gist", ci->second.get(), v, {}, nullptr).toStr();
    }
    if (v.t == VT::Object && v.obj() && v.obj()->cls) {
        // through the CHAIN, not straight at the method: a user `method gist`
        // that defers (`nextsame`/`callsame`) needs the candidate under it —
        // a parent's gist, or the built-in one — and invokeMethod alone
        // establishes neither.
        if (!skipUser && v.obj()->cls->findMethod("gist"))
            return invokeMethodChain("gist", v.obj()->cls.get(), v, {}, nullptr).toStr();
        // exceptions gist to their message (`say $!` prints "boom", not X::AdHoc<obj>)
        // NOTE: this is the X::-NAME test, not "is it an Exception". A user
        // subclass of Exception not named X::* gists as Class<obj> here while `~$e`
        // finds its message — three tests for one question. Unifying them was tried
        // and BACKED OUT: widening the predicate to the class graph costs 3
        // assertions in S32-exceptions/misc.t (throws-like matchers on compile-time
        // exceptions), and neither method-first, attribute-first nor
        // own-declared-method-first recovers them. The audit finding is real; the
        // fix needs to understand how the built-in X:: classes are synthesised
        // first, which is its own piece of work.
        // CX:: too — a control exception gists like any other (CX::Warn is the
        // one a CONTROL block reads).
        if (v.obj()->cls->name.rfind("X::", 0) == 0 ||
            v.obj()->cls->name.rfind("CX::", 0) == 0) {
            // X::Await::Died: where it was awaited, then the death itself
            auto ab = v.obj()->attrs.find("__awaitbt");
            if (ab != v.obj()->attrs.end() && ab->second.ext()) {
                auto rec = std::static_pointer_cast<BtRecord>(ab->second.ext());
                Value inner = v;
                auto od = makePayload<ObjectData>(*v.obj());
                od->attrs.erase("__awaitbt");
                inner.setObj(od);
                BtStyle plain; plain.excerpt = plain.typeLine = plain.colour = false;
                std::string g = gistOf(inner), ind;
                size_t st = 0;
                while (st <= g.size()) {
                    size_t nl = g.find('\n', st);
                    std::string ln = g.substr(st, nl == std::string::npos ? std::string::npos : nl - st);
                    if (!ln.empty()) ind += "    " + ln;
                    if (nl == std::string::npos) break;
                    ind += "\n"; st = nl + 1;
                }
                return "An operation first awaited:\n" + renderFrames(*rec, plain) +
                       "\nDied with the exception:\n" + ind;
            }
            auto it = v.obj()->attrs.find("message");
            // a hand-built `X::AdHoc.new(payload => …)` has no message attribute:
            // its message IS the payload (see the .message accessor)
            if (v.obj()->cls->name == "X::AdHoc" && (it == v.obj()->attrs.end() || !rtIsDefined(it->second))) {
                auto pl = v.obj()->attrs.find("payload");
                if (pl != v.obj()->attrs.end() && rtIsDefined(pl->second)) return pl->second.toStr();
            }
            // `message` is a METHOD as often as an attribute — X::Protocol and
            // every other hand-rolled X:: class computes it from its other
            // fields. The base Exception hands down an undefined `message`
            // attribute, so the attribute branch below won and gisted to the
            // empty string: `say $e` printed a BLANK LINE where `~$e` printed
            // the message, because prefixStringify honours the method and this
            // did not. Ask the method whenever the attribute has nothing in it.
            // This does not widen the X::-name predicate above — it only
            // rescues classes already inside it.
            if (it == v.obj()->attrs.end() || !rtIsDefined(it->second)) {
                if (v.obj()->cls->findMethod("message")) {
                    std::string msg = invokeMethodChain("message", v.obj()->cls.get(), v, {}, nullptr).toStr();
                    if (!msg.empty()) {
                        if (v.obj()->attrs.count("__bt")) {
                            BtStyle plain; plain.excerpt = plain.typeLine = plain.colour = false;
                            std::string fr = renderBacktraceValue(backtraceOf(v), plain);
                            if (!fr.empty()) return msg + "\n" + fr;
                        }
                        return msg;
                    }
                }
            }
            if (it != v.obj()->attrs.end()) {
                // …plus the frames it was thrown from: `say $!` and `$!.gist`
                // print the chain in Rakudo, and consumers parse those lines.
                // PLAIN ones — no excerpt, no type line, no colour: a gist is a
                // string programs compare, not terminal output.
                std::string msg = it->second.toStr();
                if (v.obj()->attrs.count("__bt")) {
                    BtStyle plain; plain.excerpt = plain.typeLine = plain.colour = false;
                    std::string fr = renderBacktraceValue(backtraceOf(v), plain);
                    if (!fr.empty()) return msg + "\n" + fr;
                }
                return msg;
            }
        }
    }
    // a `but VALUE` mixin boxes the base and adds a method named after VALUE's type
    // (`42 but 'x'` → a Str method): its .gist is that mixed string, not the box's.
    // A boxed NON-Str gists through .Str — that is what `Int.gist` does, so
    // `say (41 but R)` is R's Str. A boxed STR does not: `Str.gist` is the
    // string itself, so `say ("v" but R)` is "v" and the role's Str is only
    // reached by asking for it. The two differ upstream and were one branch here.
    if (v.t == VT::Object && v.obj() && v.obj()->hasBoxed && v.obj()->cls &&
        v.obj()->boxed.t != VT::Str)
        if (Value* m = v.obj()->cls->findMethod("Str")) { ValueList none; return invokeMethod(*m, v, none).toStr(); }
    if (v.t == VT::Object && v.obj() && v.obj()->hasBoxed) return gistOf(v.obj()->boxed);
    // Rakudo's default gist for a hookless object IS its .raku — the same string,
    // byte for byte. Ours were two hand-written renderers that disagreed four ways:
    // .raku walked no parents (so it silently DROPPED every inherited attribute and
    // could not round-trip through EVAL), this one escaped only 5 characters (so a
    // Str attribute holding a newline printed a RAW newline inside what looked like
    // a string literal), they disagreed on container form, and they ordered the
    // attributes differently. One renderer now; the g_rakuRepr hook is already how
    // Value.cpp reaches it.
    if (v.t == VT::Object && v.obj() && v.obj()->cls && g_rakuRepr) return g_rakuRepr(v);
    return v.gist();
}
// A `Str but Role` mixin and a class `is Str` both box the underlying string;
// that box is what the Str:D candidates see. A NON-Str object (or one boxing a
// number) is not one and must keep going through `.Str`.
bool Interpreter::strishValue(const Value& v, std::string& out) {
    if (v.t != VT::Object || !v.obj() || !v.obj()->hasBoxed) return false;
    const Value& b = v.obj()->boxed;
    if (b.t != VT::Str) return false;
    out = b.s;
    return true;
}

// $*DISTRO / $*KERNEL / $*VM: tagged hashes that compare as their `.Str`
bool isPlatformHash(const Value& v) {
    return v.t == VT::Hash && v.hash() &&
           (v.hashKind == "Distro" || v.hashKind == "Kernel" || v.hashKind == "VM");
}
std::string Interpreter::strInStrContext(const Value& v) {
    std::string s;
    return strishValue(v, s) ? s : strOf(v);
}

std::string Interpreter::strOf(const Value& v) {
    failureDetonate(v);
    // a package's stash stringifies as the package's long name (`~Foo::Bar.WHO`)
    if (v.t == VT::Hash && v.hashKind == "Stash" && !v.s.empty()) return v.s.str();
    // A Code has no string form: Rakudo warns and yields "". Handing back
    // "sub { ... }" put a placeholder into real output — `~$block` and
    // `$block.join` both produced it — and never told anyone it was a mistake.
    // (.gist and .raku still show the routine; only .Str is empty.)
    if (v.t == VT::Code) {
        bool block = v.code() && v.code()->isBlock;
        std::string msg = std::string(block ? "Block" : "Sub") +
                          " object coerced to string (please use .gist or .raku to do that)";
        if (quietDepth_ == 0 && !runControlWarn(msg)) std::cerr << msg << "\n";
        // A NAMED routine still stringifies to its bare name (`~&infix:<+>` is
        // "infix:<+>"); only a block or an anonymous routine yields "".
        return block || !v.code() ? "" : v.code()->name;
    }
    // Stringifying a container READS it, so a Proxy runs its FETCH. The subscript
    // path deliberately hands back the container (that is how a write reaches
    // STORE), which leaves value contexts like this one to do the read — without
    // it `say $q<baz>` printed the Proxy's own FETCH/STORE pair.
    if (v.t == VT::Hash && v.hashKind == "Proxy" && v.hash()) return strOf(deproxy(v));
    // A JUNCTION stringifies by AUTOTHREADING .Str over its eigenstates and
    // concatenating — `print 1 & 2` writes "12" (Rakudo). (`say` is different: it
    // uses .gist, which is the junction's own "all(1, 2)".)
    if (v.t == VT::Array && v.arr() &&
        (v.enumName == "any" || v.enumName == "all" || v.enumName == "one" || v.enumName == "none")) {
        std::string out;
        for (auto& e : *v.arr()) out += strOf(e);
        return out;
    }
    // a DateTime/Date carrying a :formatter stringifies through .Str (which runs it)
    if (v.t == VT::Hash && (v.hashKind == "DateTime" || v.hashKind == "Date") &&
        v.hash() && v.hash()->count("formatter"))
        return methodCall(v, "Str", {}).toStr();
    // $*DISTRO / $*KERNEL / $*VM are tagged HASHES whose `.Str` is a method, so
    // nothing that stringified them generically ever asked it: `$*DISTRO eq
    // 'macos'` and `$*DISTRO ~~ 'macos'` were both False while `.Str` said
    // "macos". Clipboard picks its backend with that exact test, so every macOS
    // run took the Linux branch and shelled out to xclip — 7 dists behind it.
    if (v.t == VT::Hash && v.hash() &&
        (v.hashKind == "Distro" || v.hashKind == "Kernel" || v.hashKind == "VM"))
        return methodCall(v, "Str", {}).toStr();
    // A LIST stringifies its elements space-separated, and each element through
    // ITS OWN .Str — a list of objects with a user `method Str` must not come out
    // as `T<obj> T<obj>` (XML::Element.contents is a list of XML::Text).
    if (v.t == VT::Array && v.arr() && v.enumName.empty()) {
        bool anyObj = false;
        // …and a Proxy ELEMENT is read the same way (URI::Query hands back a list
        // of Proxy containers so the list itself stays immutable).
        for (auto& e : *v.arr())
            if (e.t == VT::Object || (e.t == VT::Hash && e.hashKind == "Proxy")) { anyObj = true; break; }
        if (anyObj) {
            std::string out;
            for (size_t k = 0; k < v.arr()->size(); k++) { if (k) out += " "; out += strOf((*v.arr())[k]); }
            return out;
        }
    }
    // A TYPE OBJECT that declares .Str stringifies through it too — `~(class {
    // method Str { 'foo' } })` is "foo", not the empty string. (Only a user Str:
    // the built-in one warns and yields "", which is the existing behaviour.)
    if (v.t == VT::Type) {
        auto cit = classes_.find(v.s);
        if (cit != classes_.end() && cit->second)
            if (Value* m = cit->second->findMethod("Str")) { ValueList none; return invokeMethod(*m, v, none).toStr(); }
    }
    // Any other type object has no string form: Rakudo warns (S32-basics/warn.t
    // asserts the first sentence of this message) and yields "". It was silent.
    if (v.t == VT::Type || v.t == VT::Any) {
        std::string msg = "Use of uninitialized value of type " + v.typeName() +
            " in string context.\nMethods .^name, .raku, .gist, or .say can be used to stringify it to something meaningful.";
        if (quietDepth_ == 0 && !runControlWarn(msg)) std::cerr << msg << "\n";
        return "";
    }
    // Nil has its own, shorter wording, and it fires wherever Nil reaches
    // string context: `~Nil`, an interpolation, `"x" ~ Nil`, and a Nil ELEMENT
    // of a list being joined (Nil-Any sheet NA-08, NA-41). `.gist` and `.raku`
    // still answer "Nil" silently — they never come through here.
    if (v.t == VT::Nil) {
        static const std::string msg = "Use of Nil in string context";
        if (quietDepth_ == 0 && !runControlWarn(msg)) std::cerr << msg << "\n";
        return "";
    }
    if (v.t == VT::Object && v.obj() && v.obj()->cls) {
        // through the CHAIN, as gistOf does: a user `method Str` that defers
        // (`nextsame` for the built-in stringification, as CSS::Writer's does
        // when it has no AST to write) needs the candidate under it, and
        // invokeMethod alone establishes no dispatcher — "nextsame is not in
        // the dynamic scope of a dispatcher" came out of `~$obj` while a plain
        // `$obj.Str` was fine.
        for (const char* nm : {"Str", "gist", "Stringy"}) // ~$o uses .Stringy, print uses .Str
            if (v.obj()->cls->findMethod(nm))
                return invokeMethodChain(nm, v.obj()->cls.get(), v, {}, nullptr).toStr();
        // …or an attribute that `handles <Str>` answers it
        for (ClassInfo* c = v.obj()->cls.get(); c; c = c->parent.get())
            for (auto& a : c->attrs)
                for (auto& h : a.handles)
                    if (h == "Str") return methodCall(v, "Str", ValueList{}).toStr();
        // an Exception stringifies to its .message (Raku: Exception.Str is .message),
        // whether message is a method or a plain attribute.
        if (Value* m = v.obj()->cls->findMethod("message")) { ValueList none; return invokeMethod(*m, v, none).toStr(); }
        auto mit = v.obj()->attrs.find("message");
        if (mit != v.obj()->attrs.end()) return strOf(mit->second);
        if (v.obj()->hasBoxed) return strOf(v.obj()->boxed);
        // A class that does the built-in SEQUENCE role stringifies as its
        // elements do (Sequence's `Str` is `self.cache.Str`): Red's ResultSeq
        // is one, and `is $rs.map(*.col), (10, 20, 30)` compares that string.
        bool sequence = v.obj()->cls->doesRole("Sequence");
        // (a lone `does Sequence` lands in the native-parent slot instead)
        for (ClassInfo* c = v.obj()->cls.get(); c && !sequence; c = c->parent.get())
            if (c->nativeParent == "Sequence") sequence = true;
        if (sequence) {
            ValueList items;
            if (objListItems(v, items)) {
                std::string out;
                for (size_t k = 0; k < items.size(); k++) { if (k) out += " "; out += strOf(items[k]); }
                return out;
            }
        }
    }
    return v.toStr();
}

// `temp $x` / `let $x` — snapshot the container now; temp restores when the
// scope leaves, let only when it leaves UNSUCCESSFULLY. Called BEFORE the
// generic argument pre-evaluation: `temp $a = 23` must snapshot $a first.
Value Interpreter::evalTempLet(Call* c) {
    // let = temp that only restores when the scope exits UNSUCCESSFULLY
    auto& restores = c->name == "let" ? tctx_.cur->x().letRestores
                                      : tctx_.cur->x().tempRestores;
    auto snap = [](Value v) { // detach container storage so later mutation misses the snapshot
        if (v.t == VT::Array && v.arr()) v.setArr(makePayload<ValueList>(*v.arr()));
        else if (v.t == VT::Hash && v.hash()) v.setHash(makePayload<ValueMap>(*v.hash()));
        return v;
    };
    // A VarExpr target restores THROUGH its owning Env by name — a raw
    // Value* into Env.vars (an unordered_map) dangles if the map rehashes
    // before the scope leaves (same reason the Index cases go by key).
    auto pushVarRestore = [&](Expr* tg) -> bool {
        if (tg->kind != NK::VarExpr) return false;
        std::string nm = static_cast<VarExpr*>(tg)->name;
        // a PACKAGE-qualified name (`$Test2::scalar`) lives where lvalue() finds
        // it, not under that spelling in the lexical chain: restore through it
        if (nm.find("::") != std::string::npos) {
            Value* lv = lvalue(tg);
            if (!lv) return false;
            // an `our` published under its long name is a VIEW (a Proxy onto
            // the package's own slot): snapshot what it shows, restore through it
            if (lv->t == VT::Hash && lv->hashKind == "Proxy" && lv->hash()) {
                Value proxy = *lv;
                Value snapshot = snap(deproxy(proxy));
                restores.push_back([this, proxy, snapshot]() {
                    try { proxyStore(proxy, snapshot); } catch (...) {}
                });
                return true;
            }
            Value snapshot = snap(*lv);
            Expr* tgt2 = tg;
            restores.push_back([this, tgt2, snapshot]() {
                try { if (Value* p = lvalue(tgt2)) *p = snapshot; } catch (...) {}
            });
            return true;
        }
        std::shared_ptr<Env> se = tctx_.cur;
        while (se && !se->local(nm)) se = se->parent;
        if (!se) return false;
        Value snapshot = snap(*se->local(nm));
        restores.push_back([se, nm, snapshot]() {
            // by name, not by pointer: a map slot moves on rehash. local()
            // re-finds either home (the pad slot stays live — the captured
            // shared_ptr keeps the frame, and its mask, alive).
            if (Value* p = se->local(nm)) *p = snapshot;
            else se->vars[nm] = snapshot;
        });
        return true;
    };
    // `temp $a = 23`: the arg is an ASSIGN — snapshot the target BEFORE
    // the assignment stores the new value (else we "restore" the new one)
    if (c->args[0]->kind == NK::Assign) {
        auto* as = static_cast<Assign*>(c->args[0].get());
        if (!pushVarRestore(as->target.get())) {
            if (Value* lv = lvalue(as->target.get())) {
                Value snapshot = snap(*lv);
                restores.push_back([lv, snapshot]() { *lv = snapshot; });
            }
        }
        return eval(c->args[0].get());
    }
    // `temp @a[$i]` / `temp %h<k>`: restore THROUGH the container by key, not
    // through a raw element pointer — the element storage can reallocate (a
    // later push) before the scope leaves, dangling a captured Value*.
    Expr* tgt = c->args[0].get();
    if (tgt->kind == NK::Index) {
        auto* ix = static_cast<Index*>(tgt);
        Value base = eval(ix->base.get());
        Value key = eval(ix->index.get());
        if (base.t == VT::Array && base.arr() && !ix->isHash) {
            auto arr = base.arrS(); long long i = key.toInt(); // shared: the restore runs at scope exit, `base` is long gone
            if (i < 0) negIndexThrow(i);
            if (i < (long long)arr->size()) {
                Value snapshot = snap((*arr)[i]);
                restores.push_back([arr, i, snapshot]() {
                    if (i < (long long)arr->size()) (*arr)[i] = snapshot;
                });
                return (*arr)[i];
            }
        }
        else if (base.t == VT::Hash && base.hash()) {
            auto h = base.hashS(); std::string k = key.toStr(); // shared: the restore runs at scope exit, `base` is long gone
            auto it = h->find(k);
            bool existed = it != h->end();
            Value snapshot = existed ? snap(it->second) : Value::any();
            restores.push_back([h, k, snapshot, existed]() {
                if (existed) (*h)[k] = snapshot; else h->erase(k);
            });
            return existed ? it->second : Value::any();
        }
    }
    if (pushVarRestore(tgt)) { // plain `temp $x` / `temp @a`: restore by env+name
        if (Value* lv = lvalue(tgt)) return *lv;
        return Value::any();
    }
    // `temp &f.wrap({…})`: a call answering a plain VALUE has no container to
    // restore — the call just happens (Rakudo's temp of a non-container)
    if (tgt->kind == NK::MethodCall) {
        Value* lv = nullptr;
        try { lv = lvalue(tgt); }
        catch (RakuError& e) {
            if (e.message.find("not assignable") == std::string::npos) throw;
            return eval(tgt);
        }
        if (!lv) return Value::any();
        Value snapshot = snap(*lv);
        restores.push_back([lv, snapshot]() { *lv = snapshot; });
        return *lv;
    }
    if (Value* lv = lvalue(tgt)) { // non-var lvalue (attribute etc.)
        Value snapshot = snap(*lv);
        restores.push_back([lv, snapshot]() { *lv = snapshot; });
        return *lv;
    }
    return Value::any();
}

std::string Interpreter::subsetTypeOfVar(const std::string& nm) {
    for (Env* en = tctx_.cur.get(); en; en = en->parent.get()) {
        auto di = en->xr().varDefault.find(nm);
        if (di != en->xr().varDefault.end()) {
            if (di->second.t == VT::Type && subsets_.count(std::string(di->second.s.c_str())))
                return di->second.s.c_str();
            return "";
        }
        if (en->local(nm)) break;
    }
    return "";
}

// A scalar typed with a type the PROGRAM declared (a class, a role, a role's
// parameterization) refuses an instance or type object of another user type:
// `class A {}; class B {}; my A $x = B.new` dies in Rakudo, and stored here.
// Only user objects are judged — a built-in value the engine represents its own
// way keeps the old leniency — and a pun (`my R[Int] $x`) is asked by the name
// it was written as, so a class doing R[Str] is refused.
bool Interpreter::userTypeRefuses(const Value& rhs, const std::string& want) {
    if (want.empty() || want == "Any" || want == "Mu") return false;
    // an ENUM type holds only its own values: `my Color $c = "for the fail"` dies
    if (enumPairs_.count(want) && rtIsDefined(rhs))
        return rhs.enumType.empty() || rhs.enumType.str() != want;
    if (rhs.t != VT::Object && rhs.t != VT::Type) {
        // a BUILT-IN value (42, [1], %h, `Any.new`) is no instance of a user
        // class — unless a mixin made it one (`42 but R` does R)
        if (rhs.t == VT::Nil || rhs.t == VT::Any || !rtIsDefined(rhs)) return false;
        if (rhs.t == VT::Hash && (rhs.hashKind == "Failure" || rhs.hashKind == "Proxy")) return false;
        if (isJunction(rhs) || subsets_.count(want)) return false;
        auto ci = classes_.find(want);
        if (ci == classes_.end() || !ci->second || !ci->second->nativeParent.empty()) return false;
        for (ClassInfo* c = ci->second.get(); c; c = c->parent.get())
            if (!c->nativeParent.empty()) return false;   // `class Foo is Int` — built-in backed
        return !typeMatchesArg(rhs, want);
    }
    if (subsets_.count(want)) return false;
    auto wi = classes_.find(want);
    // `my R[Int] $x` may carry the parameterization as WRITTEN: its base role
    // is what the program declared
    if (wi == classes_.end() && want.back() == ']') {
        size_t br = want.find('[');
        if (br != std::string::npos && br > 0) {
            auto bi = classes_.find(want.substr(0, br));
            if (bi != classes_.end() && bi->second && bi->second->isRole) wi = bi;
        }
    }
    if (wi == classes_.end() || !wi->second) return false;
    if (rhs.t == VT::Object) {
        if (!rhs.obj() || !rhs.obj()->cls || !classes_.count(rhs.obj()->cls->name)) return false;
    }
    else {
        if (rhs.s.empty() || rhs.s == want) return false;
        auto ri = classes_.find(rhs.s);
        if (ri == classes_.end() || !ri->second) return false;
    }
    const std::string& asked = wi->second->dispName.empty() || want.back() == ']' ? want : wi->second->dispName;
    if (rhs.t == VT::Type) {
        for (ClassInfo* c = classes_[rhs.s].get(); c; c = c->parent.get()) {
            if (c->name == want || c->doneRoles.count(asked)) return false;
            for (auto& ep : c->extraParents) if (ep && ep->name == want) return false;
        }
        return true;
    }
    return !typeMatchesArg(rhs, asked);
}

// A step or compound assignment on a SUBSET-typed scalar asks the subset
// about the new value, as `=` does: `my Even $x = 2; $x++` dies and keeps 2.
// Only programs that declare a subset pay for the lookup.
void Interpreter::subsetMutationCheck(const Expr* target, const Value& nv) {
    if (subsets_.empty() || !target || target->kind != NK::VarExpr) return;
    const std::string& nm = static_cast<const VarExpr*>(target)->name;
    if (nm.empty() || nm[0] != '$') return;
    std::string st = subsetTypeOfVar(nm);
    if (st.empty() || !isDefined(nv) || typeOrSubsetMatches(nv, st)) return;
    throwTypedV("X::TypeCheck::Assignment",
        {{"got", nv}, {"expected", Value::typeObj(st)}, {"symbol", Value::str(nm)}},
        "Type check failed in assignment to " + nm + "; expected " + st +
        " but got " + nv.typeName() + " (" + typeCheckRepr(nv) + ")");
}

// The typed-assign contract, shared by plain `=` (its own inline copy in
// evalAssignInner predates this) and atomic-assign: a declared core nominal
// type rejects a mismatched value; a declared atomicint coerces numerics to
// Int (Rakudo: $x \u269b= 4.5 stores 4) and rejects the rest.
// A value assigned into a coercion-typed variable: `my Rat(Str) $v = 1`
// refuses what its SOURCE type does not accept, and a nested source
// (`Str(Int(Cool))`) coerces inside out — 42.13 is Int 42 and then "42".
Value Interpreter::coerceVarValue(const Value& rhs, const std::string& target,
                                  const std::string& from, const std::string& nm) {
    if (typeOrSubsetMatches(rhs, target) && isDefined(rhs)) return rhs;
    Value v = rhs;
    if (!from.empty()) {
        size_t lp = from.find('(');
        if (lp != std::string::npos && lp > 0 && from.back() == ')') {
            std::string innerT = from.substr(0, lp), innerF = from.substr(lp + 1, from.size() - lp - 2);
            v = coerceVarValue(v, innerT, innerF, nm);
        }
        else if (from != "Any" && from != "Mu" && !typeOrSubsetMatches(v, from))
            throwTypedV("X::TypeCheck::Assignment",
                {{"got", v}, {"expected", Value::typeObj(target + "(" + from + ")")}, {"symbol", Value::str(nm)}},
                "Type check failed in assignment to " + nm + "; expected " + target + "(" + from +
                ") but got " + v.typeName() + (isDefined(v) ? " (" + typeCheckRepr(v) + ")" : ""));
    }
    if (typeOrSubsetMatches(v, target) && isDefined(v)) return v;
    return coerceToType(v, target);
}

// The stripe pool for cas and the atomic-* family: real mutual exclusion in
// parallel (no-GIL) mode, negligible uncontended cost under the GIL. Hashed by
// the container's ADDRESS so ops on the same atomicint always share a lock.
std::recursive_mutex& Interpreter::atomicStripe(const void* p) {
    static std::recursive_mutex stripes[64];
    return stripes[(reinterpret_cast<uintptr_t>(p) >> 4) & 63];
}

// A private call needs a `self` in scope — OR a routine declared inside the
// class itself: `my multi rulify(Path::Finder:D $rule) { $rule!rules }` sits in
// the class body, has no invocant of its own, and is exactly how Path::Finder
// reaches another instance's rules. Rakudo allows a private call anywhere
// lexically inside the declaring class; the running routine's own package is
// the part of that we can see at runtime.
// A child that declares an attribute its parent also declares does not share
// the parent's storage: each class's own methods see their own `$!name`. The
// most-derived declaration keeps the bare slot; an ancestor's is qualified by
// its class ("Parent\x01priv"), and so is `$!name` read from that ancestor's
// methods. Classes without such shadowing pay one cached flag test.
const std::string& Interpreter::attrSlotFor(const ObjectData* od, const std::string& bare, std::string& buf) {
    if (!od || !od->cls) return bare;
    ClassInfo* top = od->cls.get();
    if (top->shadowsAttrs < 0) {
        std::set<std::string> seen; bool dup = false;
        for (ClassInfo* c = top; c && !dup; c = c->parent.get())
            for (auto& a : c->attrs) if (!seen.insert(a.name).second) { dup = true; break; }
        top->shadowsAttrs = dup ? 1 : 0;
    }
    if (top->shadowsAttrs == 0) return bare;
    const Callable* rc = tctx_.curRoutineVal && tctx_.curRoutineVal->t == VT::Code ? tctx_.curRoutineVal->code() : nullptr;
    if (!rc || rc->pkg.empty()) return bare;
    ClassInfo* first = nullptr; bool ownerDeclares = false;
    for (ClassInfo* c = top; c; c = c->parent.get())
        for (auto& a : c->attrs)
            if (a.name == bare) {
                if (!first) first = c;
                if (c->name == rc->pkg) ownerDeclares = true;
                break;
            }
    if (!first || !ownerDeclares || first->name == rc->pkg) return bare;
    buf = rc->pkg + "\x01" + bare;
    return buf;
}

// `$o!Pkg::meth` from outside Pkg: only a package Pkg `trusts` may call it.
// (Roles are not judged: their methods run as the class that composes them.)
void Interpreter::checkPrivatePermission(const std::string& qualified) {
    size_t q = qualified.rfind("::");
    if (q == std::string::npos) return;
    std::string pkg = qualified.substr(0, q);
    auto ti = classes_.find(pkg);
    if (ti == classes_.end() && !tctx_.pkgPrefix.empty()) ti = classes_.find(tctx_.pkgPrefix + pkg);
    if (ti == classes_.end() || !ti->second || ti->second->isRole) return;
    ClassInfo* target = ti->second.get();
    std::string caller;
    if (tctx_.curRoutineVal && tctx_.curRoutineVal->t == VT::Code && tctx_.curRoutineVal->code())
        caller = tctx_.curRoutineVal->code()->pkg;
    ClassInfo* callerCls = nullptr;
    if (!caller.empty() && caller != "GLOBAL") {
        auto ci = classes_.find(caller);
        if (ci == classes_.end()) return;          // a package we cannot place: not judged
        callerCls = ci->second.get();
        if (!callerCls || callerCls->isRole || callerCls == target) return;
    }
    const std::string who = caller.empty() ? std::string("GLOBAL") : caller;
    if (target->trusts.count(who)) return;
    if (callerCls && target->trusts.count(callerCls->name)) return;
    {   // a trusted name written short (`trusts Trustee` inside a module)
        size_t w = who.rfind("::");
        if (w != std::string::npos && target->trusts.count(who.substr(w + 2))) return;
    }
    throwTypedV("X::Method::Private::Permission",
        {{"method", Value::str(qualified.substr(q + 2))}, {"source-package", Value::str(pkg)},
         {"calling-package", Value::str(who)}},
        "Cannot call private method '" + qualified.substr(q + 2) + "' on package " + pkg +
        " because it does not trust " + who);
}

void Interpreter::requirePrivateCallScope(const std::string& name) {
    if (tctx_.cur->findSelf()) return;
    if (tctx_.curRoutineVal && tctx_.curRoutineVal->t == VT::Code &&
        tctx_.curRoutineVal->code()) {
        const std::string& pkg = tctx_.curRoutineVal->code()->pkg;
        if (!pkg.empty() && pkg != "GLOBAL") {
            auto pit = classes_.find(pkg);
            if (pit != classes_.end() && pit->second &&
                pit->second->findMethod("!" + name)) return;
        }
    }
    throw RakuError{Value::typeObj("X::Method::NotFound"),
        "Private method call to '" + name + "' outside the defining class"};
}
// a QUALIFIED routine name is also looked up from the package the running
// routine was declared in, outward: `Our::Package::pkg()` inside `package
// PackageTest` is PackageTest::Our::Package::pkg
Value* Interpreter::pkgRelativeRoutine(const std::string& name) {
    if (name.find("::") == std::string::npos || !tctx_.cur) return nullptr;
    std::string pre;
    if (tctx_.curRoutineVal && tctx_.curRoutineVal->t == VT::Code && tctx_.curRoutineVal->code())
        pre = tctx_.curRoutineVal->code()->pkg;
    if (pre == "GLOBAL") pre.clear();
    if (pre.empty() && !tctx_.pkgPrefix.empty()) pre = tctx_.pkgPrefix.substr(0, tctx_.pkgPrefix.size() - 2);
    while (!pre.empty()) {
        const std::string full = "&" + pre + "::" + name;
        Value* f = tctx_.cur->find(full);
        if (!f && global_) f = global_->find(full);
        if (f && f->t == VT::Code) return f;
        size_t cut = pre.rfind("::");
        pre = cut == std::string::npos ? std::string() : pre.substr(0, cut);
    }
    return nullptr;
}

// An infinite Range keeps the ±LLONG_MAX sentinel in its integer endpoints; a
// fractional one (1.5..Inf) keeps a real infinity in its doubles.
bool isEndlessRange(const Value& v) {
    if (v.t != VT::Range) return false;
    if (v.rNum()) return std::isinf(v.n) || std::isinf(v.im());
    bool loSent = v.rFrom() <= -9000000000000000000LL, hiSent = v.rTo() >= 9000000000000000000LL;
    if (!loSent && !hiSent) return false;
    // The sentinel also stands in for an endpoint too big for a long long, so
    // the WRITTEN endpoint decides when it was kept: `1..10**100` is a finite
    // (very long) range — it parks its BigInt bound in `big` — and `1..Inf` is
    // not. A Rat/Num endpoint is kept in RangeEnds instead.
    if (v.big()) return false;
    if (const RangeEnds* re = rangeEnds(v)) {
        auto runaway = [](const Value& e) {
            return e.t == VT::Whatever || (e.t == VT::Num && std::isinf(e.n));
        };
        return (hiSent && runaway(re->to)) || (loSent && runaway(re->from));
    }
    return true;
}

bool isEndlessLazy(const Value& v) {
    return v.t == VT::Array && v.ext() &&
           std::static_pointer_cast<LazySeqState>(v.ext())->infinite;
}

Value endlessRangeSum(const Value& v) {
    bool lowInf = endlessLow(v), highInf = endlessHigh(v);
    if (lowInf && highInf) return Value::number(NAN); // -Inf..Inf: Rakudo says NaN
    return Value::number(highInf ? INFINITY : -INFINITY);
}

// `[-]` over an endless range: the first element minus a tail that grows
// without bound, which is -Inf whichever side ran away (a low-endless range
// starts AT -Inf and only gets smaller). Both sides is Inf - Inf: NaN.
static Value endlessRangeDiff(const Value& v) {
    if (endlessLow(v) && endlessHigh(v)) return Value::number(NAN);
    return Value::number(-INFINITY);
}

// `[*]` over an endless range. Zero is absorbing: once an element is 0 every
// partial product from there on is 0, so that IS the value. Otherwise the
// magnitudes grow without bound and only the sign is left to settle — a range
// bounded below has finitely many negative elements, so their count decides it.
// A range running down to -Inf has no first element to start from: the partial
// products flip sign forever without settling, which is NaN.
static Value endlessRangeProduct(const Value& v) {
    if (endlessLow(v)) return Value::number(NAN);
    // the WRITTEN low endpoint, which is where the walk starts: `-2.5..Inf`
    // keeps its -2.5 in RangeEnds while rFrom has already floored to -3
    const RangeEnds* re = rangeEnds(v);
    double lo = re && re->from.isNumeric() ? re->from.toNum()
              : v.rNum()                    ? v.n
                                          : (double)v.rFrom();
    if (v.rExFrom()) lo += 1.0;
    // stepping by 1 from `lo` lands on 0 only when lo is a non-positive integer:
    // `-2.5..Inf` skips straight from -0.5 to 0.5
    if (lo <= 0 && std::floor(lo) == lo) return Value::integer(0);
    long long negatives = lo < 0 ? (long long)std::ceil(-lo) : 0;
    return Value::number(negatives % 2 ? -INFINITY : INFINITY);
}

// The endpoint an endless Range is bounded by, as .min/.max report it: the
// written endpoint object when there is one (1.5..Inf keeps its 1.5), and ±Inf
// on the side that runs away.
static Value endlessRangeEnd(const Value& v, bool high) {
    if ((high ? endlessHigh(v) : endlessLow(v)))
        return Value::number(high ? INFINITY : -INFINITY);
    if (const RangeEnds* re = rangeEnds(v)) return high ? re->to : re->from;
    if (v.rNum()) return Value::number(high ? v.im() : v.n);
    return Value::integer(high ? v.rTo() : v.rFrom());
}

// A reduce over an operand that never ends — an infinite Range (1..Inf / 1..*)
// or an endless lazy list. Folding it element by element never returns (Rakudo
// spins forever on `[+] 1..Inf`), and folding the finite prefix Value::flatten
// hands back answers a DIFFERENT question: `[+] 1..Inf` came out 50005000, the
// sum of the first ten thousand, with nothing to say it was not the total.
// What CAN be answered is what the partial folds converge to, which the range's
// bounds already decide: `+` is the series limit `.sum` gives, `*` is 0 or a
// signed infinity, `-` runs away downward, and min/max settle on the bounds.
// `~` and the rest have no limit to name in their own domain and say so, as
// does every endless LAZY list — its elements do not follow from its bounds.
// A merely lazy operand is drained and folded in full, and refused only when it
// will not drain.
bool endlessReduce(const std::string& op, const Value& v, Value& out) {
    bool range = isEndlessRange(v);
    if (range) {
        if (op == "+") { out = endlessRangeSum(v); return true; }
        if (op == "-") { out = endlessRangeDiff(v); return true; }
        if (op == "*") { out = endlessRangeProduct(v); return true; }
        if (op == "min" || op == "max") { out = endlessRangeEnd(v, op == "max"); return true; }
        throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot reduce an infinite range"};
    }
    if (v.t != VT::Array || !v.ext() || !v.arr()) return false;         // not lazy: fold as usual
    if (!isEndlessLazy(v)) {
        // A merely LAZY list holds only the prefix something has already pulled
        // — folding that is how `[+] (1..Inf).grep(* %% 2)` answered 0 — so drain
        // it first. Whether a grep over an endless source ends is only knowable
        // by trying (`.grep({last if …})` does end); one that will not drain has
        // no honest answer, so it joins the endless ones below.
        auto st = std::static_pointer_cast<LazySeqState>(v.ext());
        const size_t CAP = 1000000; // materializeLazy's own ceiling
        if (st->appendNext)
            while (v.arr()->size() < CAP && st->appendNext(*v.arr())) {}
        // (a view that ran into an endless source's ceiling marks itself endless
        // while draining, so ask again before deciding it drained)
        if (v.arr()->size() < CAP && !isEndlessLazy(v)) return false; // every element is present
    }
    throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot reduce a lazy list"};
}

// [op] reduce over an item list — shared by the [op] unary, prefix:<[op]>(…)
// calls, and &prefix:<[op]> references. `op` may carry a leading '\' (scan form).
Value Interpreter::applyReduce(std::string op, ValueList& items) {
    bool scan = !op.empty() && op.front() == '\\'; // [\+] : running partial reductions
    if (scan) op = op.substr(1);
    // The reverse metaop turns the whole REDUCTION around rather than each step:
    // `[R-] 1,2,3` is 3-2-1 (0), not R-(R-(1,2),3) (2), and `[R,]` is how a list
    // gets reversed in an index. Reversing the operands and folding with the
    // base operator is exactly that. (Rakudo's `[\R,]` is the one exception: it
    // reports the growing prefixes of the ORIGINAL list, each reversed.)
    bool revMeta = op.size() > 1 && op[0] == 'R' &&
                   (!ascii::isalnum((unsigned char)op[1]) || reverseWordOp(op));
    if (revMeta) { op = op.substr(1); std::reverse(items.begin(), items.end()); }
    // a CHAINING user infix (`is assoc<chain>`) reduces like `<`: every
    // adjacent pair must hold
    if (!scan && tctx_.cur && items.size() >= 2)
        if (Value* uf = tctx_.cur->find("&infix:<" + op + ">"))
            if (uf->t == VT::Code && uf->code() && uf->code()->assocChain) {
                for (size_t i = 0; i + 1 < items.size(); i++)
                    if (!callCallable(*uf, ValueList{items[i], items[i + 1]}).truthy())
                        return Value::boolean(false);
                return Value::boolean(true);
            }
    // an N-ARY user infix (`sub infix:<leftly> { $^a + $^b * $^c }`) folds N-1
    // items per step — the .reduce method knows how
    if (tctx_.cur)
        if (Value* uf = tctx_.cur->find("&infix:<" + op + ">"))
            if (uf->t == VT::Code && uf->code() && !uf->code()->isMultiDispatcher) {
                size_t arity = 0;
                if (uf->code()->params && !uf->code()->params->empty()) {
                    for (auto& pp : *uf->code()->params)
                        if (!pp.named && !pp.slurpy && !pp.optional && !pp.defaultVal) arity++;
                } else
                    for (auto& ph : uf->code()->placeholders)
                        if (!ph.empty() && ph.find(':') == std::string::npos) arity++;
                if (arity > 2) {
                    Value lst = Value::array(items); lst.isList = true;
                    // …and the TRIANGLE form produces the same way
                    return methodCall(lst, scan ? "produce" : "reduce", ValueList{*uf});
                }
            }
    // comparison reduces CHAIN pairwise ([<] 1,2,3 == 1<2 && 2<3); a leading
    // `!` negates each pairwise test ([!=:=] $x,$y,$x == $x !=:= $y && $y !=:= $x)
    static const std::set<std::string> chainOps = {
        "<", "<=", ">", ">=", "==", "!=", "!==", "eq", "ne", "lt", "le", "gt", "ge",
        "=:=", "===", "eqv", "before", "after", "~~"};
    bool neg = op.size() > 1 && op[0] == '!' && op != "!=" && op != "!==";
    std::string base = neg ? op.substr(1) : op;
    if (scan) { // yield (a, a op b, a op b op c, …)
        Value out = Value::array(); out.isList = true; out.s = "Seq"; // `[\+]` is lazy (Rakudo)
        if (items.empty()) return out;
        if (op == ",") { // [\,] : growing prefixes — ((1) (1 2) (1 2 3))
            for (size_t k = 0; k < items.size(); k++) {
                Value pre = Value::array(); pre.isList = true;
                // `[\R,]`: items are already reversed, so the k-th REVERSED
                // prefix of the original is the k-th suffix of what we hold
                if (revMeta) pre.arr()->assign(items.end() - k - 1, items.end());
                else pre.arr()->assign(items.begin(), items.begin() + k + 1);
                out.arr()->push_back(pre);
            }
            return out;
        }
        if (op == "**") { // right-assoc scan: suffix reductions — [\**] 1,2,3 → (3 8 1)
            Value acc = items.back();
            out.arr()->push_back(acc);
            for (size_t k = items.size() - 1; k-- > 0; ) {
                acc = applyBinOp(op, items[k], acc);
                out.arr()->push_back(acc);
            }
            return out;
        }
        // the LIST infixes (`X~`, `Z~`, `minmax`) scan as the reduction of each
        // PREFIX, a lone list in a prefix standing for its own elements (the
        // one-argument rule): [\X~](<a b c>, <1 2 3>) is (("abc",), <a1 … c3>)
        if (op == "minmax" || (op.size() > 1 && (op[0] == 'X' || op[0] == 'Z') &&
                               !ascii::isalnum((unsigned char)op[1]))) {
            for (size_t k = 0; k < items.size(); k++) {
                ValueList pre(items.begin(), items.begin() + k + 1);
                if (pre.size() == 1 && pre[0].t == VT::Array && pre[0].arr()) pre = pre[0].flatten();
                out.arr()->push_back(applyReduce(op, pre));
            }
            return out;
        }
        if (op == "^^" || op == "xor") { // prefix one-xor: [\^^] 1,1,x → (1 Nil Nil)
            for (size_t k = 0; k < items.size(); k++) {
                ValueList pre(items.begin(), items.begin() + k + 1);
                out.arr()->push_back(applyReduce(op, pre));
            }
            return out;
        }
        if (chainOps.count(base)) { // sticky chain: [\<] 1,3,2,4 → (True True False False)
            bool ok = true;
            out.arr()->push_back(Value::boolean(true)); // a single element chains truthfully
            for (size_t k = 1; k < items.size(); k++) {
                bool c = applyBinOp(base, items[k - 1], items[k]).truthy();
                if (neg) c = !c;
                ok = ok && c;
                out.arr()->push_back(Value::boolean(ok));
            }
            return out;
        }
        Value acc = items[0];
        out.arr()->push_back(acc);
        for (size_t k = 1; k < items.size(); k++) { acc = applyBinOp(op, acc, items[k]); out.arr()->push_back(acc); }
        return out;
    }
    if (op == "minmax" && items.size() == 1)   // one operand still spans its extremes
        return applyBinOp("minmax", items[0], Value::list({}));
    if (op == "min" || op == "max") { // undefined items are skipped; empty → ±Inf
        ValueList def;
        for (auto& it : items) if (isDefined(it)) def.push_back(it);
        if (def.empty()) return Value::number(op == "min" ? INFINITY : -INFINITY);
        Value acc = def[0];
        for (size_t k = 1; k < def.size(); k++) acc = applyBinOp(op, acc, def[k]);
        return acc;
    }
    if (items.empty()) {
        // hyper form empty ([>>+<<]) falls back to the inner op's identity
        if (op.size() >= 5 && (op.compare(0, 2, ">>") == 0 || op.compare(0, 2, "<<") == 0) &&
            (op.compare(op.size() - 2, 2, ">>") == 0 || op.compare(op.size() - 2, 2, "<<") == 0))
            return applyReduce(op.substr(2, op.size() - 4), items);
        if (op == "+" || op == "-") return Value::integer(0);
        if (op == "*") return Value::integer(1);
        if (op == "~" || op == "~|" || op == "~^") return Value::str("");
        // `[/] ()` has no identity to name — 1 is the identity of `*`, and
        // reusing it here made the reduction of nothing come back defined.
        if (op == "/" || op == "+<" || op == "+>" || op == "~&" || op == "~<" || op == "~>")
            return armedFailure("X::NoZeroArgMeaning", "No zero-arg meaning for infix:<" + op + ">");
        // the junction constructors reduce to the EMPTY junction of their kind
        if (op == "&" || op == "|" || op == "^") {
            Value j = Value::array(); j.isList = true;
            j.enumName = op == "&" ? "all" : op == "|" ? "any" : "one";
            return j;
        }
        // the rest of the identities (Rakudo): `if [&&] @checks` with no checks
        // used to take the FALSE branch on an Any
        if (op == "&&" || op == "and" || op == "?&") return Value::boolean(true);
        if (op == "?|" || op == "?^") return Value::boolean(false);
        if (op == "||" || op == "or" || op == "^^" || op == "xor") return Value::boolean(false);
        if (op == "**") return Value::integer(1);
        if (op == "+&") return Value::integer(-1);
        if (op == "+|" || op == "+^") return Value::integer(0);
        if (op == "lcm") return Value::integer(1);
        if (op == "gcd" || op == "x" || op == "xx")
            return armedFailure("X::NoZeroArgMeaning", "No zero-arg meaning for infix:<" + op + ">");
        if (op == "(^)" || op == "\xE2\x8A\x96") return setWrap({}, setOpMinTier(op)); // [⊖] () is set()
        // list-building ops over nothing build nothing: [Z] () / [Z~] () / [X] () are ()
        if (op == "," ) { Value o = Value::array(); o.isList = true; return o; }
        if (op == "Z" || op == "X" ||
            (op.size() > 1 && (op[0] == 'Z' || op[0] == 'X')))
            return Value::seq();
        if (chainOps.count(base)) return Value::boolean(true); // [<] () is vacuously True
        return Value::any();
    }
    // the one-argument arithmetic candidates NUMIFY: `[+] "2"` is 2, `[*] $obj`
    // is `$obj.Numeric`, and `[-] 'hello'` throws X::Str::Numeric
    if (items.size() == 1 && (op == "+" || op == "-" || op == "*" || op == "/")) {
        if (items[0].t == VT::Object) { ValueList none; return methodCall(items[0], "Numeric", none); }
        return applyBinOp("+", items[0], Value::integer(0));
    }
    // …and the one-argument `~` STRINGIFIES: `[~] 1` and `&[~](1)` are "1"
    // (S32-list/unique.t maps with `&[~]`)
    if (items.size() == 1 && op == "~" && items[0].t != VT::Str && !isJunction(items[0]))
        return Value::str(strOf(items[0]));
    if (chainOps.count(base)) {
        for (size_t k = 1; k < items.size(); k++) {
            bool ok = applyBinOp(base, items[k - 1], items[k]).truthy();
            if (neg) ok = !ok;
            if (!ok) return Value::boolean(false);
        }
        return Value::boolean(true);
    }
    if (op == ",") { Value out = Value::list(items); return out; } // [,] : the list itself
    if (op == "^^" || op == "xor") {
        // one-xor is list-aware: exactly one truthy item → that item;
        // none truthy → the last item; more than one → Nil
        const Value* found = nullptr;
        for (auto& it : items) {
            if (!it.truthy()) continue;
            if (found) return Value::nil();
            found = &it;
        }
        return found ? *found : items.back();
    }
    // right-associative reduces fold from the right: [**] 2,3,2 == 2**(3**2),
    // [=>] 1,2,3 == 1 => (2 => 3)
    if (op == "=>" || op == "**" || (!rightAssocOps_.empty() && rightAssocOps_.count(op))) {
        Value acc = items.back();
        for (size_t k = items.size() - 1; k-- > 0; ) {
            if (op == "=>") { // `[=>] 1,2,3` keeps the Int keys (like Z=>)
                Value p = Value::pair(items[k].toStr(), acc);
                if (items[k].t != VT::Str) p.pairKeyM() = std::make_shared<Value>(items[k]);
                acc = p;
            }
            else acc = applyBinOp(op, items[k], acc);
        }
        return acc;
    }
    // an ENDLESS operand (`1..*`, an infinite lazy list) makes the n-ary zip or
    // cross itself endless and lazy: `1..* Z 1..* Z 1..*` and `42 X 1..* X 43`
    // pull one tuple at a time, the way the two-operand forms (zxOp) do
    if ((op == "Z" || op == "X") && items.size() > 1) {
        auto endless = [](const Value& v) {
            if (v.t == VT::Range && !v.rNum() && v.rTo() >= 9000000000000000000LL) return true;
            if (v.t == VT::Array && v.ext()) {
                auto st = std::static_pointer_cast<LazySeqState>(v.ext());
                return st && st->infinite;
            }
            return false;
        };
        size_t nEndless = 0;
        for (auto& it : items) if (endless(it)) nEndless++;
        if (nEndless > 0 && (op == "X" || nEndless == items.size())) {
            struct Dim { Value src; bool endless; ValueList fin; };
            auto dims = std::make_shared<std::vector<Dim>>();
            for (auto& it : items) {
                Dim d; d.src = it; d.endless = endless(it);
                if (!d.endless) {
                    if (it.t == VT::Array && it.arr() && it.itemized) d.fin = ValueList{it};
                    else if (it.t == VT::Array && it.arr()) d.fin = *it.arr();
                    else if (it.t == VT::Range) d.fin = it.flatten();
                    else d.fin = ValueList{it};
                }
                if (!d.endless && d.fin.empty()) return Value::seq();
                dims->push_back(std::move(d));
            }
            auto idx = std::make_shared<std::vector<size_t>>(items.size(), 0);
            auto done = std::make_shared<bool>(false);
            const bool zip = op == "Z";
            Value out = Value::seq();
            auto st = std::make_shared<LazySeqState>(); st->infinite = true;
            st->appendNext = [this, dims, idx, done, zip](ValueList& cache) -> bool {
                if (*done) return false;
                Value t = Value::array(); t.isList = true;
                for (size_t k = 0; k < dims->size(); k++) {
                    Dim& d = (*dims)[k];
                    size_t i = (*idx)[k];
                    if (!d.endless) t.arr()->push_back(d.fin[i]);
                    else if (d.src.t == VT::Range)
                        t.arr()->push_back(Value::integer(d.src.rFrom() + (d.src.rExFrom() ? 1 : 0) + (long long)i));
                    else {
                        materializeLazy(d.src, i + 1);
                        t.arr()->push_back(d.src.arr() && i < d.src.arr()->size() ? (*d.src.arr())[i] : Value::any());
                    }
                }
                cache.push_back(t);
                if (zip) { for (auto& i : *idx) i++; return true; }
                size_t k = dims->size();
                while (k > 0) {
                    Dim& d = (*dims)[k - 1];
                    if (++(*idx)[k - 1] < d.fin.size() || d.endless) break;
                    (*idx)[k - 1] = 0; k--;
                }
                if (k == 0) *done = true;
                return true;
            };
            out.extM() = st;
            return out;
        }
    }
    if (op == "Z") { // n-ary zip: [Z] rows == transpose (a fold would nest tuples)
        std::vector<ValueList> rows;
        for (auto& it : items) {
            if (it.t == VT::Array && it.arr()) rows.push_back(*it.arr());
            else if (it.t == VT::Range) rows.push_back(it.flatten());
            else if (it.t == VT::Str && (it.hashKind == "Blob" || it.hashKind == "Buf")) rows.push_back(it.blobList());
            else rows.push_back(ValueList{it});
        }
        Value out = Value::seq();
        if (!rows.empty()) {
            size_t n = rows[0].size();
            for (auto& r : rows) n = std::min(n, r.size());
            for (size_t i = 0; i < n; i++) {
                Value t = Value::array(); t.isList = true;
                for (auto& r : rows) t.arr()->push_back(r[i]);
                out.arr()->push_back(t);
            }
        }
        return out;
    }
    if (op == "X") { // n-ary cross: [X] 1..2, 3..4, 5..6 yields 3-tuples
        std::vector<ValueList> rows;
        for (auto& it : items) {
            if (it.t == VT::Array && it.arr()) rows.push_back(*it.arr());
            else if (it.t == VT::Range) rows.push_back(it.flatten());
            else if (it.t == VT::Str && (it.hashKind == "Blob" || it.hashKind == "Buf")) rows.push_back(it.blobList());
            else rows.push_back(ValueList{it});
        }
        Value out = Value::seq();
        bool any = !rows.empty();
        for (auto& r : rows) if (r.empty()) any = false;
        if (any) {
            std::vector<size_t> idx(rows.size(), 0);
            for (;;) {
                Value t = Value::array(); t.isList = true;
                for (size_t k = 0; k < rows.size(); k++) t.arr()->push_back(rows[k][idx[k]]);
                out.arr()->push_back(t);
                size_t k = rows.size();
                while (k > 0 && ++idx[k - 1] == rows[k - 1].size()) idx[--k] = 0;
                if (k == 0) break;
            }
        }
        return out;
    }
    // `Zop` / `Xop`: one n-way zip (or cross), each tuple then folded with the
    // INNER operator over ALL of its elements. Folding the outer metaop in pairs
    // instead zips tuples against a list, and for an inner operator that is not
    // associative — `(^)` — even a correctly shaped pairwise fold is the wrong
    // answer: `1..3, 1..3 Z(^) 2..4, 1..4 Z(^) 2..3, 2..3` is a THREE-way
    // symmetric difference per tuple.
    if (op.size() > 1 && (op[0] == 'Z' || op[0] == 'X')) {
        Value tuples = applyReduce(std::string(1, op[0]), items);
        std::string inner = op.substr(1);
        if (inner.empty() || inner == "," || !tuples.arr()) return tuples;
        if (tuples.ext()) { // an endless tuple stream folds lazily too
            auto src = std::make_shared<Value>(tuples);
            auto pos = std::make_shared<size_t>(0);
            Value out = Value::seq();
            auto st = std::make_shared<LazySeqState>(); st->infinite = true;
            st->appendNext = [this, src, pos, inner](ValueList& cache) -> bool {
                materializeLazy(*src, *pos + 1);
                if (!src->arr() || *pos >= src->arr()->size()) return false;
                const Value& t = (*src->arr())[(*pos)++];
                ValueList parts = t.t == VT::Array && t.arr() ? *t.arr() : ValueList{t};
                cache.push_back(applyReduce(inner, parts));
                return true;
            };
            out.extM() = st;
            return out;
        }
        Value out = Value::seq();
        for (auto& t : *tuples.arr()) {
            ValueList parts = t.t == VT::Array && t.arr() ? *t.arr() : ValueList{t};
            out.arr()->push_back(applyReduce(inner, parts));
        }
        return out;
    }
    // [(^)] / [⊖] : symmetric difference is a genuine list op (max − 2nd-max per
    // key), not the left fold the general reducer below would compute
    if (op == "(^)" || op == "\xE2\x8A\x96") return setSymDiffN(items);
    if (isSetOpStr(op) && !isSetPredicateStr(op))
        if (auto j = setOpFoldN(op, items)) return *j;   // one joint tier over every operand
    Value acc = items[0];
    for (size_t k = 1; k < items.size(); k++) acc = applyBinOp(op, acc, items[k]);
    return acc;
}

// $*TZ: a user-assigned dynamic wins; otherwise the system UTC offset
long long Interpreter::tzOffsetDyn() {
    Value* tp = findDynamicLenient("$*TZ");
    if (tp) return tp->toInt();
    // portable UTC offset: local time minus UTC of the same instant
    time_t t = ::time(nullptr);
    struct tm lt, gt;
#if defined(_WIN32)
    localtime_s(&lt, &t); gmtime_s(&gt, &t);
#else
    localtime_r(&t, &lt); gmtime_r(&t, &gt);
#endif
    long long off = (lt.tm_hour - gt.tm_hour) * 3600LL + (lt.tm_min - gt.tm_min) * 60LL;
    int dd = lt.tm_yday - gt.tm_yday;
    if (dd == 1 || dd < -1) off += 86400;      // local is a day ahead (incl. year wrap)
    else if (dd == -1 || dd > 1) off -= 86400; // local is a day behind
    return off;
}

double Interpreter::toleranceDyn() {
    Value* tp = findDynamicLenient("$*TOLERANCE");
    return tp ? tp->toNum() : 1e-15;
}

// postfix:<i> — multiply by the imaginary unit; honours .Numeric/.Bridge on
// objects and type objects (`postfix:<i>(class :: does Numeric {…})`).
Value Interpreter::postfixI(Value v) {
    if (v.t == VT::Complex) return Value::complex(-v.im(), v.n);
    if (!v.isNumeric()) {
        ClassInfo* ci = nullptr;
        if (v.t == VT::Object && v.obj()) ci = v.obj()->cls.get();
        else if (v.t == VT::Type) { auto it = classes_.find(v.s); if (it != classes_.end()) ci = it->second.get(); }
        if (ci)
            for (const char* nm : {"Numeric", "Bridge"})
                if (Value* m = ci->findMethod(nm)) { ValueList none; v = invokeMethod(*m, v, none); break; }
        if (v.t == VT::Complex) return Value::complex(-v.im(), v.n);
    }
    return Value::complex(0.0, v.toNum());
}

bool Interpreter::keySubscriptIsSlice(const Expr* ixExpr, const Value& iv) {
    if (!(iv.t == VT::Array || iv.t == VT::Range)) return false;
    if (iv.itemized) return false;
    // an ENUM TYPE object rides in a tagged pair-list, but as a subscript it
    // is ONE key, not a slice — `%converter-for-type{$type}` with an enum
    // $type must answer one entry (Getopt::Long), and a Junction key stays
    // one key for the autothreading layer to handle
    if (!iv.enumType.empty() || !iv.enumName.empty()) return false;
    if (ixExpr && ixExpr->kind == NK::VarExpr) {
        auto* ve = static_cast<const VarExpr*>(ixExpr);
        if (!ve->name.empty() && ve->name[0] == '$') return false;
    }
    return true;
}
// A subscript base whose evaluation has no side effects: a variable, a literal,
// a Range or `^N` of those, a list of them — evaluating it twice is harmless.
bool pureSubscriptBase(const Expr* b) {
    if (!b) return false;
    switch (b->kind) {
        case NK::VarExpr: case NK::IntLit: case NK::StrLit: case NK::NumLit: return true;
        case NK::InterpStr:   // a "quoted" string whose parts are all literal
            for (auto& p : static_cast<const InterpStr*>(b)->parts)
                if (!p || p->kind != NK::StrLit) return false;
            return true;
        case NK::Range: {
            auto* r = static_cast<const RangeExpr*>(b);
            return pureSubscriptBase(r->from.get()) && pureSubscriptBase(r->to.get());
        }
        case NK::Unary: {
            auto* u = static_cast<const Unary*>(b);
            return u->op == "^" && pureSubscriptBase(u->operand.get());
        }
        case NK::ListExpr:
            for (auto& it : static_cast<const ListExpr*>(b)->items)
                if (!pureSubscriptBase(it.get())) return false;
            return true;
        case NK::ArrayLit:   // …and `<b c>`, which is an Array literal of words
            for (auto& it : static_cast<const ArrayLit*>(b)->items)
                if (!pureSubscriptBase(it.get())) return false;
            return true;
        default: return false;
    }
}

#ifdef RAKUPP_NODE_COUNT
// IR campaign instrumentation (phase I0, docs/dev/experiments/IR-PLAN.md).
// Compiled in ONLY with -DRAKUPP_NODE_COUNT, so a shipped build carries neither
// the counters nor the increments. It answers the question the plan's escape
// hatch rests on: how many AST nodes does a run actually visit? Without that,
// the per-node crossing cost measured by tools/ir-boundary.cpp cannot be turned
// into a fraction of runtime.
unsigned long long g_evalNodes = 0, g_execStmts = 0;
namespace {
struct NodeCountReport {
    ~NodeCountReport() {
        fprintf(stderr, "[node-count] eval=%llu exec=%llu\n", g_evalNodes, g_execStmts);
    }
} g_nodeCountReport;
}  // namespace
#endif

} // namespace rakupp
