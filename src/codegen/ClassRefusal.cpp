// Can the native backend register this class as the interpreter would?
// (Codegen::classRegister)
//
// classRegister builds a ClassInfo from an attribute's name, sigil, twigil,
// `is rw`, type, `is required`, pod and default — nothing else. Every other
// attribute trait was dropped on the way into a binary, silently: `has Int $.n
// where * > 0` took -1, `has Int:D $.x` took Int, `has IO::Path() $.f` refused
// the Str it should coerce, `has @.a handles <elems>` answered the object's own
// method, `is default(5)` read (Any), `has @.a[2]` was unshaped and `has %.s
// is Set` an ordinary Hash. A parent that is not one of the program's own
// classes (`class G is Int`) was never found, so `G.new(7)` died. Those
// classes refuse here, and the program bundles with the interpreter.
#include "../Ast.h"
#include <set>
#include <string>

namespace rakupp {

std::string classRefusal(const ClassDecl* cd, const std::set<std::string>& programClasses) {
    static const std::set<std::string> valueTypes = {
        "Int", "Str", "Num", "Rat", "FatRat", "Complex", "Bool", "Cool", "Numeric", "Real", "Stringy",
        "Array", "List", "Hash", "Map", "Pair", "Range", "Seq", "Slip", "Set", "Bag", "Mix", "SetHash",
        "BagHash", "MixHash", "Buf", "Blob", "Date", "DateTime", "Duration", "Instant", "IO::Path"};
    if (!programClasses.count(cd->parent) && valueTypes.count(cd->parent))
        return "a class whose parent is the core type `" + cd->parent + "`";
    for (const AttrDecl& a : cd->attrs) {
        const std::string what = "the attribute `" + std::string(1, a.sigil) + (a.pub ? "." : "!") + a.name + "`";
        if (a.coerce) return "a coercion type on " + what;
        if (a.whereExpr) return "a `where` constraint on " + what;
        if (a.defConstraint) return "a `:D`/`:U` type on " + what;
        if (!a.containerIs.empty()) return "a container type (`is " + a.containerIs + "`) on " + what;
        if (a.shape) return "a shape on " + what;
        if (!a.handles.empty()) return "`handles` on " + what;
        if (a.defaultTrait) return "`is default` on " + what;
        if (!a.doesRoles.empty()) return "`does` on " + what;
        if (!a.userTraits.empty()) return "a user trait on " + what;
        if (a.objKeyed || a.inlined || a.deprecated || a.readonly || a.built || a.notBuilt)
            return "a trait on " + what;
    }
    // (a native `is rw` method hands back a copy of the container it returns)
    for (auto& md : cd->methods)
        if (md->retRw) return "an `is rw` method, whose result is a container";
    return "";
}

} // namespace rakupp
