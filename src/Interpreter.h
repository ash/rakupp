#pragma once
#include "Ast.h"
#include "Value.h"
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace rakupp {

struct Env {
    std::unordered_map<std::string, Value> vars;
    std::shared_ptr<Env> parent;

    Value* find(const std::string& name) {
        auto it = vars.find(name);
        if (it != vars.end()) return &it->second;
        if (parent) return parent->find(name);
        return nullptr;
    }
    void define(const std::string& name, Value v) { vars[name] = std::move(v); }
};

// control-flow signals
struct ReturnEx { Value v; };
struct ExitEx { int code = 0; };
struct LastEx {};
struct NextEx {};
struct RedoEx {};
struct BreakGivenEx {}; // `when`/`succeed` exits the enclosing given/loop
struct RakuError { Value payload; std::string message; };

class Interpreter {
public:
    Interpreter();
    int run(Program& prog);
    void setArgs(std::vector<std::string> a) { argv_ = std::move(a); }

    // evaluation
    Value eval(Expr* e);
    Value exec(Stmt* s);           // returns last value (for implicit return)
    Value execBlock(Block* b, std::shared_ptr<Env> scope);
    bool runLoopBody(Block* b, std::shared_ptr<Env> scope); // handles redo/next/last; false => last

    // calling
    Value callCallable(const Value& codeVal, ValueList args);
    Value callBuiltin(const std::string& name, ValueList args); // invoke a named builtin (used by codegen)
    Value getArgs(); // @*ARGS as a List value (used by codegen)
    Value methodCall(Value inv, const std::string& method, ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr);
    Value invokeMethod(const Value& codeVal, const Value& self, ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr);
    void copyOutRw(const std::vector<Param>* params, std::shared_ptr<Env>& env, const std::vector<ExprPtr>* rwArgs, bool methodCtx);
    int scoreCandidate(const Value& cand, const ValueList& args); // -1 = no match, else specificity
    bool boolify(const Value& v); // boolean context: honours a custom .Bool method on objects
    void hoistSubs(const std::vector<StmtPtr>& stmts); // pre-register sub decls (whole-scope visibility)
    void runProcPromise(Value& promise, double timeoutSec); // run a Proc::Async .start promise (with optional timeout)
    void runEnterPhasers(const std::vector<StmtPtr>& stmts); // ENTER/FIRST at block entry (source order)
    void runLeavePhasers(const std::vector<StmtPtr>& stmts); // LEAVE/KEEP/UNDO at block exit (reverse order)
    Value evalString(const std::string& src); // EVAL
    void loadModule(const std::string& name);  // `use Foo::Bar` -> compile lib file into global scope
    std::vector<std::string> libPaths_{"lib", ".", "rakulib"}; // + env-derived paths, filled in the ctor
    std::set<std::string> loadedModules_;
    Value regexMatch(const std::string& subject, const std::string& pattern); // sets $/ $0..
    Value regexSubst(const std::string& subject, const std::string& pattern,
                     const std::string& repl, std::string& out, bool& changed);
    Value grammarParse(ClassInfo* g, const std::string& input, bool subparse, const std::string& startRule, Value actions);

    std::unordered_map<std::string, std::shared_ptr<ClassInfo>> classes_;

    std::shared_ptr<Env> cur_;
    std::shared_ptr<Env> global_;
    Env* curStateEnv_ = nullptr; // persistent env for `state` vars in the current sub call
    std::vector<std::string> argv_;
    std::vector<std::shared_ptr<ValueList>> gatherStack_; // active gather collectors
    std::vector<Value*> makeTargets_; // current Match being acted on (for `make`)
    std::string pkgPrefix_;           // current package/module qualified-name prefix
public:
    std::string finishData_;          // $=finish data block of the module being run
    std::string srcFile_;             // source file path (for $?FILE)
    std::string execPath_;            // absolute path of the rakupp binary (for $*EXECUTABLE)
private:
    int callDepth_ = 0; // guards against unbounded recursion (hang/stack overflow)

    // test harness state
    long planned_ = -1;
    long testNum_ = 0;
    long failCount_ = 0;
    bool usedTest_ = false;
    int subtestDepth_ = 0;
    bool subtestFailed_ = false;

    void emitTest(bool ok, const std::string& desc, const std::string& directive = "");

private:
    std::unordered_map<std::string, BuiltinFn> builtins_;
    std::vector<std::shared_ptr<Program>> keptPrograms_; // keep EVAL'd ASTs alive
    void registerBuiltins();

    Value* lvalue(Expr* e);
    ValueList evalArgs(const std::vector<ExprPtr>& exprs); // spreads `|@a`
    Value evalAssign(Assign* a);
    Value evalBinary(Binary* b);
    Value evalUnary(Unary* u);
    Value evalCall(Call* c);
    Value evalIndex(Index* idx);
    Value evalInterp(InterpStr* s);

    Value makeClosure(BlockExpr* be);
    void bindParams(const std::vector<Param>& params, ValueList& args, std::shared_ptr<Env>& env);
};

// helpers
Value listToArray(const ValueList& items);
ValueList argsToPositional(ValueList& args, std::map<std::string, Value>& named);
Value applyArith(const std::string& op, const Value& l, const Value& r); // binary op dispatch (also used by codegen)
// indexing helpers used by native codegen (value-level, with autovivification on write)
Value  rtIndexGet(const Value& base, const Value& key, bool isHash);
Value& rtIndexRef(Value& base, const Value& key, bool isHash);
Value  rtReduce(const std::string& op, const Value& list);  // [+] / [*] / … reduction metaop
Value  rtAttrGet(const Value& self, const std::string& name);   // $!attr / $.attr read (codegen)
Value& rtAttrRef(Value& self, const std::string& name);         // $!attr write (codegen)
bool   rtTypeMatch(const Value& v, const std::string& type);    // nominal type check for multi-dispatch (codegen)

} // namespace rakupp
