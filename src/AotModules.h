#pragma once
// Native routine bodies for the modules a `--exe` binary embeds.
//
// A compiled program carries its modules as serialized ASTs (AstSerial), and
// the interpreter runs them: every declaration, export, package, role, trait
// and signature binding keeps the one implementation it has. What `--exe`
// adds on top is a native BODY for each module routine the C++ backend can
// compile — a sub's or a method's statements, emitted by the same Codegen
// that compiles the program itself.
//
// The interpreter still makes the call: it dispatches, binds the signature
// into a fresh frame and runs the phasers. Where it would walk the body's
// statements it enters the native function instead (Stmt::aotBody on the
// body's first statement), which reads its parameters out of that frame and
// reaches every other outer name through it.
//
// The two sides agree on WHICH routine a native body belongs to by walking
// the same deserialized AST in the same order: forEachAotRoutine below is
// called by the compiler on the blob it embeds and by the loader on the blob
// it reads back, so the n-th routine is the same SubDecl on both sides.
#include <cstddef>
#include <functional>
#include <string>

namespace rakupp {

struct Program;
struct Stmt;
struct SubDecl;
struct Env;
struct Value;
class Interpreter;

// Runs the body in the frame the interpreter bound, leaving the result in
// `out`. Answers false BEFORE running anything when it cannot bind a name it
// needs; the interpreter then walks the body itself.
using AotBodyFn = bool (*)(Interpreter&, Env*, Value& out);

struct AotEntry {
    int index;          // position in forEachAotRoutine's walk
    const char* name;   // the routine's name, checked against the AST on attach
    AotBodyFn fn;
};

// Every routine declaration a native body may be attached to, in a fixed
// order: the unit's statements, class/role/package bodies and their method
// lists, recursively. Routine BODIES are not entered — a nested sub belongs
// to its enclosing routine's native body, or stays interpreted with it.
void forEachAotRoutine(Program& prog, const std::function<void(SubDecl*)>& fn);

// Called by a compiled binary's startup code, once per module that has native
// bodies, before the program runs.
void rakuppRegisterModuleAot(const char* module, const AotEntry* table, size_t n);

// Called by the module loader right after it deserializes an embedded module.
// Attaches each registered native body to its routine. A module with no
// table, or a table that does not fit the AST it was built for, attaches
// nothing and runs interpreted.
void attachAotBodies(const std::string& module, Program& prog);

// RAKUPP_AOT_RUNLOG=FILE (t/aot/): the binary records what became of each
// module's table (attached, refused, disabled) and how often each native body
// was entered or declined, and writes it to FILE at exit. The flag is read once
// at startup; while it is off, a call pays one predictable branch.
extern const bool g_aotRunLog;
bool aotRunLogged(Interpreter& I, Stmt* first, void* fn, Env* frame, Value& out);
//
// Test-only fault knobs, read at attach time (t/aot/ proves the fallbacks with
// them): RAKUPP_AOT_FAULT_DECLINE=NAME[,NAME…] attaches, for each routine so
// named, a body that declines at entry; RAKUPP_AOT_FAULT_TABLE=MODULE[:name]
// corrupts that module's table — the first entry's index, or with `:name` the
// last entry's name — so the attach must refuse the whole module.

} // namespace rakupp
