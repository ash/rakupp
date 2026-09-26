// Metamodel::Primitives: types whose metaobject is a USER object.
//
// `Metamodel::Primitives.create_type($how, $repr)` makes a type object whose
// every meta-question goes to `$how`: `.^name` is `$how.name($type)`, a
// `.^compose` is `$how.compose($type)`, and a type check asks the HOW — its
// `type_check` for "is this type one of those", its `accepts_type` for "does
// this value belong to me" once `configure_type_checking(:call_accepts)` says
// it wants to be asked. `configure_type_checking` also hands over a type-check
// CACHE, and an :authoritative one answers alone, without calling `type_check`.
//
// Such a type is an ordinary VT::Type under an internal name (`__CustomHOW3`)
// with its record in Interpreter::customHows_; an instance (`$type.CREATE`) is
// an ordinary object whose ClassInfo carries that name, so `rebless` swaps the
// ClassInfo. Nothing here runs unless a type was ever made this way.
#include "Interpreter.h"
#include "BuiltinsShared.h"

namespace rakupp {

struct Interpreter::CustomHowType {
    std::string name;             // the internal type name
    Value how;                    // the user metaobject
    std::string repr = "P6opaque";
    bool mixin = false;
    bool composed = false;
    bool haveCache = false;       // configure_type_checking was called
    bool authoritative = false;
    bool callAccepts = false;
    ValueList cache;              // the types it is known to be
    std::shared_ptr<ClassInfo> ci; // what an instance's class is
};

std::shared_ptr<Interpreter::CustomHowType> Interpreter::customHowOf(const Value& v) {
    const std::string* nm = nullptr;
    std::string tmp;
    if (v.t == VT::Type) { tmp = v.s.str(); nm = &tmp; }
    else if (v.t == VT::Object && v.obj() && v.obj()->cls) nm = &v.obj()->cls->name;
    if (!nm || nm->rfind("__CustomHOW", 0) != 0) return nullptr;
    std::lock_guard<std::mutex> lk(customHowMu_);
    auto it = customHows_.find(*nm);
    return it == customHows_.end() ? nullptr : it->second;
}

static bool howHas(const Value& how, const char* m) {
    return how.t == VT::Object && how.obj() && how.obj()->cls && how.obj()->cls->findMethod(m);
}

// Is `obj` (a value or a type) of type `type`, where either side may be made
// by a user metaobject? The nqp::istype of Rakudo, for the cases it reaches.
bool Interpreter::customIsType(const Value& obj, const Value& type) {
    auto oc = customHowOf(obj);
    auto tc = type.t == VT::Type ? customHowOf(type) : nullptr;
    if (oc) {
        if (tc && tc == oc) return true;
        if (oc->haveCache) {
            for (auto& c : oc->cache)
                if (c.t == VT::Type && type.t == VT::Type && c.s == type.s) return true;
            if (oc->authoritative) return false;
        }
        if (howHas(oc->how, "type_check")) {
            Value self = Value::typeObj(oc->name);
            return boolify(methodCall(oc->how, "type_check", ValueList{self, type}));
        }
        return false;
    }
    if (tc) {
        if (tc->callAccepts && howHas(tc->how, "accepts_type"))
            return boolify(methodCall(tc->how, "accepts_type", ValueList{type, obj}));
        return false;
    }
    return boolify(applyBinOp("~~", obj, type));
}

// `X ~~ T` where T (or X) is made by a user metaobject: -1 when neither is.
int Interpreter::customHowMatch(const Value& l, const Value& r) {
    auto rc = r.t == VT::Type ? customHowOf(r) : nullptr;
    if (rc) {
        // T.ACCEPTS is looked up through T's HOW, as every method on T is
        if (howHas(rc->how, "find_method"))
            methodCall(rc->how, "find_method", ValueList{r, Value::str("ACCEPTS")});
        return customIsType(l, r) ? 1 : 0;
    }
    if (r.t == VT::Type && customHowOf(l)) return customIsType(l, r) ? 1 : 0;
    return -1;
}

bool Interpreter::customHowMethod(const Value& inv, const std::string& m, ValueList& args, Value& out) {
    auto named = [&](const char* k) -> const Value* {
        for (auto& a : args) if (a.t == VT::Pair && a.namedArg && a.s == k) return a.pairVal();
        return nullptr;
    };
    ValueList pos;
    for (auto& a : args) if (!(a.t == VT::Pair && a.namedArg)) pos.push_back(a);
    if (inv.t == VT::Type && inv.s == "Metamodel::Primitives") {
        if (m == "create_type" && !pos.empty()) {
            auto ch = std::make_shared<CustomHowType>();
            static std::atomic<long> serial{0};
            ch->name = "__CustomHOW" + std::to_string(++serial);
            ch->how = pos[0];
            if (pos.size() > 1) ch->repr = pos[1].toStr();
            if (const Value* mx = named("mixin")) ch->mixin = boolify(*mx);
            ch->ci = std::make_shared<ClassInfo>();
            ch->ci->name = ch->name;
            {
                std::lock_guard<std::mutex> lk(customHowMu_);
                customHows_[ch->name] = ch;
            }
            haveCustomHows_ = true;
            out = Value::typeObj(ch->name);
            return true;
        }
        if (m == "configure_type_checking" && !pos.empty()) {
            auto ch = customHowOf(pos[0]);
            if (!ch) return false;
            ch->haveCache = true;
            ch->cache.clear();
            if (pos.size() > 1)
                for (auto& t : pos[1].flatten()) ch->cache.push_back(t);
            if (const Value* a = named("authoritative")) ch->authoritative = boolify(*a);
            if (const Value* a = named("call_accepts")) ch->callAccepts = boolify(*a);
            out = pos[0];
            return true;
        }
        if (m == "is_type" && pos.size() >= 2) {
            out = Value::boolean(customIsType(pos[0], pos[1]));
            return true;
        }
        if (m == "compose_type" && !pos.empty()) {
            if (auto ch = customHowOf(pos[0])) ch->composed = true;
            out = pos[0];
            return true;
        }
        if (m == "rebless" && pos.size() >= 2) {
            auto to = customHowOf(pos[1]);
            if (!to || pos[0].t != VT::Object || !pos[0].obj())
                throw RakuError{Value::typeObj("X::AdHoc"), "Cannot rebless to this type"};
            pos[0].obj()->cls = to->ci;
            out = pos[0];
            return true;
        }
        if (m == "set_method_cache" || m == "set_method_cache_authoritativeness" ||
            m == "set_boolification_mode" || m == "set_parameterizer" || m == "install_method_cache") {
            out = pos.empty() ? Value::any() : pos[0];
            return true;
        }
        return false;
    }
    auto ch = customHowOf(inv);
    if (!ch) return false;
    const bool isType = inv.t == VT::Type;
    Value self = isType ? inv : Value::typeObj(ch->name);
    if (m == "HOW") { out = ch->how; return true; }
    if (m == "REPR") { out = Value::str(ch->repr); return true; }
    if (m == "WHAT") { out = self; return true; }
    if (m == "DEFINITE") { out = Value::boolean(!isType); return true; }
    if (m == "^name" && !howHas(ch->how, "name")) { out = Value::str(ch->name); return true; }
    if (m.size() > 1 && m[0] == '^') {
        // every meta-method is the HOW's own, with the type in front
        ValueList ha; ha.push_back(self);
        for (auto& a : args) ha.push_back(a);
        if (m == "^compose") ch->composed = true;
        out = methodCall(ch->how, m.substr(1), std::move(ha));
        return true;
    }
    if (m == "CREATE" || m == "bless" || m == "new") {
        if (howHas(ch->how, "find_method"))
            methodCall(ch->how, "find_method", ValueList{self, Value::str(m)});
        auto od = makePayload<ObjectData>();
        od->cls = ch->ci;
        out = Value::object(od);
        return true;
    }
    return false;
}

} // namespace rakupp
