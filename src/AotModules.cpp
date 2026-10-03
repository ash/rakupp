// Native routine bodies for embedded modules — see AotModules.h.
#include "AotModules.h"
#include "AstSerial.h"
#include "Interpreter.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace rakupp {

// ---- the walk both sides share ---------------------------------------------

static void walkAot(std::vector<StmtPtr>& stmts, const std::function<void(SubDecl*)>& fn);

static void walkAotClass(ClassDecl* cd, const std::function<void(SubDecl*)>& fn) {
    for (auto& m : cd->methods) fn(m.get());
    walkAot(cd->body, fn);
}

static void walkAot(std::vector<StmtPtr>& stmts, const std::function<void(SubDecl*)>& fn) {
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == NK::SubDecl) fn(static_cast<SubDecl*>(s.get()));
        else if (s->kind == NK::ClassDecl) walkAotClass(static_cast<ClassDecl*>(s.get()), fn);
    }
}

void forEachAotRoutine(Program& prog, const std::function<void(SubDecl*)>& fn) {
    walkAot(prog.stmts, fn);
}

// ---- registration ----------------------------------------------------------

namespace {
struct AotTable { const AotEntry* entries; size_t n; };
// Written by generated startup code before the program runs, read-only after
// (the same contract as embeddedModules()).
std::map<std::string, AotTable>& aotTables() {
    static std::map<std::string, AotTable> m;
    return m;
}
bool aotTrace() {
    static const bool on = [] { const char* e = std::getenv("RAKUPP_AOT_TRACE"); return e && *e && *e != '0'; }();
    return on;
}
bool aotDisabled() {
    static const bool off = [] { const char* e = std::getenv("RAKUPP_NO_AOT"); return e && *e && *e != '0'; }();
    return off;
}
} // namespace

void rakuppRegisterModuleAot(const char* module, const AotEntry* table, size_t n) {
    aotTables()[module] = AotTable{table, n};
}

void attachAotBodies(const std::string& module, Program& prog) {
    auto it = aotTables().find(module);
    if (it == aotTables().end() || aotDisabled()) return;
    std::vector<SubDecl*> routines;
    forEachAotRoutine(prog, [&](SubDecl* d) { routines.push_back(d); });
    const AotTable& t = it->second;
    // Check the whole table before attaching any of it: an entry whose index
    // or name does not match means the AST is not the one the bodies were
    // compiled from, and then none of them can be trusted.
    for (size_t i = 0; i < t.n; i++) {
        const AotEntry& e = t.entries[i];
        if (e.index < 0 || (size_t)e.index >= routines.size() || routines[e.index]->name != e.name ||
            routines[e.index]->body.empty()) {
            if (aotTrace())
                std::fprintf(stderr, "[aot] %s: table does not fit the module AST; running it interpreted\n",
                             module.c_str());
            return;
        }
    }
    // RAKUPP_AOT_RANGE=LO-HI[,LO-HI…] attaches only the bodies whose ordinal
    // (counted across modules, in load order) falls in one of the ranges: a
    // bisection knob for finding the native bodies that disagree with the
    // interpreter.
    static std::vector<std::pair<long, long>> ranges;
    static long seq = 0;
    static const bool ranged = [] {
        const char* r = std::getenv("RAKUPP_AOT_RANGE");
        if (!r || !*r) return false;
        for (const char* p = r; *p;) {
            long lo = -1, hi = -1;
            if (std::sscanf(p, "%ld-%ld", &lo, &hi) == 2) ranges.push_back({lo, hi});
            const char* c = std::strchr(p, ',');
            if (!c) break;
            p = c + 1;
        }
        return true;
    }();
    for (size_t i = 0; i < t.n; i++) {
        const AotEntry& e = t.entries[i];
        long n = seq++;
        bool in = !ranged;
        for (auto& rg : ranges) if (n >= rg.first && n <= rg.second) in = true;
        if (!in) continue;
        if (ranged && aotTrace()) std::fprintf(stderr, "[aot] #%ld %s::%s\n", n, module.c_str(), e.name);
        routines[e.index]->body[0]->aotBody = reinterpret_cast<void*>(e.fn);
    }
    if (aotTrace())
        std::fprintf(stderr, "[aot] %s: %zu of %zu routines native\n", module.c_str(), t.n, routines.size());
}

// ---- what the generated bodies call ----------------------------------------
//
// Declared again, by prototype, at the top of the generated translation unit
// (which includes only Interpreter.h and Value.h).

// An outer name the body reads or writes, resolved through the frame the
// interpreter bound. Null declines the native body: either the name is not
// in scope at all, or it lives in the call frame itself — a frame is pooled
// and reused after the call, so a native closure must not keep a pointer
// into one. (Parameters are COPIED out of the frame instead; see rtAotParam.)
Value* rtAotOuter(Env* frame, const char* name) {
    if (!frame) return nullptr;
    if (frame->localRaw(name)) {
        if (aotTrace()) std::fprintf(stderr, "[aot] decline: %s lives in the call frame\n", name);
        return nullptr;
    }
    Env* up = frame->parent.get();
    Value* p = up ? up->find(name) : nullptr;
    if (!p && aotTrace()) std::fprintf(stderr, "[aot] decline: %s is not in scope\n", name);
    return p;
}

// `my $*X` in a module routine: declared in the routine's own frame.
Value& rtAotDeclDyn(Env* frame, const char* name) {
    return frame->define(name, Value::any());
}

// A variable the interpreter bound in the call frame (a parameter, `self`).
// The copy is a VALUE: the binder marks a parameter's slot read-only, and that
// mark must not travel with it into whatever the body stores it in — `$!title
// = $val` would have made the attribute read-only for every later write.
Value rtAotParam(Env* frame, const char* name) {
    if (frame)
        if (Value* p = frame->local(name)) { Value v = *p; v.readonly = false; return v; }
    return Value::any();
}

// A subtree the native body hands to the interpreter, deserialized once.
Expr* rtAotNode(const unsigned char* blob, size_t n) {
    static std::mutex mu;
    static std::vector<std::unique_ptr<Program>> keep;   // the nodes live as long as the program
    auto p = std::make_unique<Program>();
    deserializeAst(std::string(reinterpret_cast<const char*>(blob), n), *p);
    Expr* e = nullptr;
    if (!p->stmts.empty() && p->stmts[0]->kind == NK::ExprStmt)
        e = static_cast<ExprStmt*>(p->stmts[0].get())->e.get();
    std::lock_guard<std::mutex> g(mu);
    keep.push_back(std::move(p));
    return e;
}

// A statement the native body hands to the interpreter whole (Codegen's
// delegateStmt), deserialized once.
Stmt* rtAotStmt(const unsigned char* blob, size_t n) {
    static std::mutex mu;
    static std::vector<std::unique_ptr<Program>> keep;
    auto p = std::make_unique<Program>();
    deserializeAst(std::string(reinterpret_cast<const char*>(blob), n), *p);
    Stmt* st = p->stmts.empty() ? nullptr : p->stmts[0].get();
    std::lock_guard<std::mutex> g(mu);
    keep.push_back(std::move(p));
    return st;
}

// Run a delegated statement in a scope of its own under the current frame,
// with the native locals it names copied in — and copied back out, on the way
// out by any route, so a write the statement made (or made before it died)
// lands in the local.
Value rtAotExec(Interpreter& I, Stmt* st, const char* const* names, Value** slots, size_t n,
                const Value* self, bool sink) {
    ExecContext& tcx = I.tctx_;
    auto scope = std::make_shared<Env>();
    scope->parent = tcx.cur;
    if (self) scope->define("self", *self);
    for (size_t i = 0; i < n; i++) scope->define(names[i], *slots[i]);
    struct Back {
        ExecContext& t; std::shared_ptr<Env> saved; Env* scope;
        const char* const* names; Value** slots; size_t n;
        ~Back() {
            t.cur = std::move(saved);
            for (size_t i = 0; i < n; i++)
                if (Value* p = scope->local(names[i])) *slots[i] = *p;
        }
    } back{tcx, tcx.cur, scope.get(), names, slots, n};
    tcx.cur = scope;
    Value v = I.exec(st, sink);   // a statement in the middle of a body runs in sink context, as there
    // A `return` the interpreter ran cooperatively leaves the routine: hand it
    // on as the exception the native body's own boundary catches.
    if (tcx.returning) {
        tcx.returning = false;
        throw ReturnEx{std::move(tcx.returnV), 0};
    }
    return v;
}

// Evaluate a delegated node with `self` bound to the given invocant — for a
// node inside a native CLOSURE, which may run after its method has returned
// and its frame has been reused, so the frame cannot supply `self`. The
// scope is a non-owning view of a stack Env whose only content is `self`;
// lookups past it continue in the scope the call is made from.
static Value evalWithSelf(Interpreter& I, const Value& self, Expr* node, bool lvalue, Value** lv) {
    Env scope;
    Value selfCopy = self;
    scope.selfSlot = &selfCopy;
    scope.vars.emplace("self", self);
    ExecContext& tcx = I.tctx_;
    scope.parent = tcx.cur;
    std::shared_ptr<Env> view(std::shared_ptr<Env>(), &scope);
    auto saved = tcx.cur;
    tcx.cur = view;
    struct Restore { ExecContext& t; std::shared_ptr<Env> s; ~Restore() { t.cur = std::move(s); } } r{tcx, std::move(saved)};
    if (lvalue) { *lv = I.lvalueOf(node); return Value(); }
    return I.eval(node);
}

// `$!x` / `$.x` in a method body. The common case — a plain object whose
// class chain declares no attribute twice, read through the `!` twigil — is
// the interpreter's own lookup done inline; everything else (`$.x`, which is
// a method call when a method of that name exists; shadowed private
// attributes; native-memory objects; twins) goes to the interpreter with
// `self` bound, so the answer is the interpreter's.
Value rtAotAttr(Interpreter& I, const Value& self, Expr* node, bool inClosure) {
    auto* ve = static_cast<VarExpr*>(node);
    if (self.t == VT::Object && self.obj() && ve->name.size() > 1 && ve->name[1] == '!' &&
        ve->attrTwin.empty()) {
        ObjectData* od = self.obj();
        ClassInfo* cls = od->cls.get();
        if (cls && cls->shadowsAttrs == 0 && cls->repr.empty()) {
            auto it = od->attrs.find(ve->attrBare);
            if (it != od->attrs.end()) {
                Interpreter::ParStripe rs(I, &it->second);
                return it->second;
            }
        }
    }
    if (!inClosure) return I.eval(node);
    return evalWithSelf(I, self, node, false, nullptr);
}

// `$!x = v`. A defined value into a plain slot is the interpreter's store
// done inline (a `$` attribute itemizes what it is given). Anything else — Nil
// or a type object (which reset the attribute to its default), a Proxy slot,
// shadowed or native-memory attributes — is assigned BY the interpreter: an
// `ATTR = $v` node built once around the attribute, run with `self` and `$v`
// bound in a scope of its own.
Value rtAotAttrAssign(Interpreter& I, const Value& self, Expr* node, bool inClosure, Value v) {
    auto* ve = static_cast<VarExpr*>(node);
    const bool definedValue = v.t != VT::Any && v.t != VT::Nil && v.t != VT::Type &&
                              v.hashKind != "Failure" && v.hashKind != "Proxy";
    if (definedValue && self.t == VT::Object && self.obj() && ve->name.size() > 1 && ve->name[1] == '!' &&
        ve->attrTwin.empty()) {
        ObjectData* od = self.obj();
        ClassInfo* cls = od->cls.get();
        if (cls && cls->shadowsAttrs == 0 && cls->repr.empty()) {
            auto it = od->attrs.find(ve->attrBare);
            if (it != od->attrs.end() && it->second.hashKind != "Proxy" && !it->second.isCell() &&
                !it->second.readonly) {
                if (ve->name[0] == '$') v = rtItemized(std::move(v));
                v.readonly = false;
                Interpreter::ParStripe ws(I, &it->second);
                it->second = std::move(v);
                return it->second;
            }
        }
    }
    // the slow path: `ATTR = $v` through the interpreter's own assignment
    static std::mutex mu;
    static std::map<Expr*, std::unique_ptr<Assign>> nodes;   // one per attribute site, kept for good
    Assign* as;
    {
        std::lock_guard<std::mutex> g(mu);
        auto& slot = nodes[node];
        if (!slot) {
            slot = std::make_unique<Assign>();
            slot->op = "=";
            auto t = std::make_unique<VarExpr>(ve->name);
            t->attrTwin = ve->attrTwin;
            t->line = ve->line;
            slot->target = std::move(t);
            slot->value = std::make_unique<VarExpr>("$\x01aot-value");
            slot->line = ve->line;
        }
        as = slot.get();
    }
    ExecContext& tcx = I.tctx_;
    auto scope = std::make_shared<Env>();
    (void)inClosure;   // self comes from the scope either way
    scope->parent = tcx.cur;
    scope->define("self", self);
    scope->define("$\x01aot-value", std::move(v));
    struct Restore { ExecContext& t; std::shared_ptr<Env> s; ~Restore() { t.cur = std::move(s); } } r{tcx, tcx.cur};
    tcx.cur = scope;
    return I.eval(as);
}

// A mutating method (push, append, write-…, splice …) on a value whose bytes
// live in the value itself — a Buf/Blob — changes the INVOCANT'S VARIABLE,
// which the interpreter reaches through the call's invocant expression. A
// native call has a container instead: the call runs through the interpreter
// on a scope variable holding the value, and the variable's new value is
// written back. Any other invocant takes the ordinary call.
Value rtMutMethod(Interpreter& I, Value& inv, const char* name, ValueList args) {
    Value* tgt = inv.deref();
    if (!(tgt->t == VT::Str && (tgt->hashKind == "Buf" || tgt->hashKind == "Blob")))
        return I.methodCall(*tgt, name, std::move(args));
    static std::mutex mu;
    static std::map<std::string, std::unique_ptr<MethodCall>> nodes;
    MethodCall* mc;
    {
        std::lock_guard<std::mutex> g(mu);
        auto& slot = nodes[name];
        if (!slot) {
            slot = std::make_unique<MethodCall>();
            slot->inv = std::make_unique<VarExpr>("$\x01aot-inv");
            slot->method = name;
            auto u = std::make_unique<Unary>();
            u->op = "|";
            u->operand = std::make_unique<VarExpr>("@\x01aot-args");
            slot->args.push_back(std::move(u));
        }
        mc = slot.get();
    }
    Value a = Value::array();
    for (auto& v : args) a.arr()->push_back(std::move(v));
    ExecContext& tcx = I.tctx_;
    auto scope = std::make_shared<Env>();
    scope->parent = tcx.cur;
    scope->define("$\x01aot-inv", *tgt);
    scope->define("@\x01aot-args", std::move(a));
    Value r;
    {
        struct Restore { ExecContext& t; std::shared_ptr<Env> s; ~Restore() { t.cur = std::move(s); } } rs{tcx, tcx.cur};
        tcx.cur = scope;
        r = I.eval(mc);
    }
    if (Value* p = scope->local("$\x01aot-inv")) *tgt = *p;
    return r;
}

Value& rtAotAttrRef(Interpreter& I, const Value& self, Expr* node, bool inClosure) {
    auto* ve = static_cast<VarExpr*>(node);
    if (self.t == VT::Object && self.obj() && ve->name.size() > 1 && ve->name[1] == '!' &&
        ve->attrTwin.empty()) {
        ObjectData* od = self.obj();
        ClassInfo* cls = od->cls.get();
        if (cls && cls->shadowsAttrs == 0 && cls->repr.empty()) {
            auto it = od->attrs.find(ve->attrBare);
            if (it != od->attrs.end()) return it->second;
        }
    }
    Value* p = nullptr;
    if (!inClosure) p = I.lvalueOf(node);
    else evalWithSelf(I, self, node, true, &p);
    if (!p) throw RakuError{Value::typeObj("X::AdHoc"), "Cannot assign to attribute " + ve->name};
    return *p;
}

} // namespace rakupp
