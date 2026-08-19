# The Object System

Raku's object surface is one of the largest in the language: classes with
inheritance, roles, runtime mixins, a meta-object protocol, `augment` and
`supersede`, package stashes. A reader arriving from C++ or the JVM would
expect a subsystem to match. There is not one — the whole of it is two
structs and the operations that read them, and most of this chapter follows
from how little those structs hold.

A user class is a `ClassInfo`. An instance is an `ObjectData` that points at
one. There is no per-object method table, no vtable, and no per-class code
generation.

```cpp
// src/Value.h — abridged
struct ClassInfo {
    std::string name;
    std::shared_ptr<ClassInfo> parent;
    std::string nativeParent;                             // `is Str`, `is Cool`
    std::vector<std::shared_ptr<ClassInfo>> extraParents;
    std::vector<ClassAttr> attrs;
    std::map<std::string, Value> methods;                 // Code values
    std::map<std::string, std::string> rules;             // grammar rules
    std::vector<std::string> ruleOrder;
    bool isGrammar = false, isRole = false;
    std::string repr;                                     // is repr('CStruct')
    std::string ver, auth, api;
    std::set<std::string> requiredMethods, doneRoles;
    Value howObj;                                         // persistent .HOW
    std::shared_ptr<Env> declEnv;                         // for attr defaults
    ClassDecl* decl = nullptr;                            // for role parameters
    std::vector<std::pair<std::string, Value>> roleParamBindings;
};

struct ObjectData {
    std::shared_ptr<ClassInfo> cls;
    std::map<std::string, Value> attrs;
    Value boxed; bool hasBoxed = false;                   // for `5 but Role`
};
```

Everything a class *is* lives in that first struct, including the grammar rules
if it happens to be a grammar — which is how a grammar is just a class whose
methods can be regexes (Chapter 22).

## Registration and lookup

`ClassInfo`s are registered in one flat map at declaration:

```cpp
// src/Interpreter.h
std::unordered_map<std::string, std::shared_ptr<ClassInfo>> classes_;
std::unordered_map<std::string, std::string> classAliases_;
```

Flat, not per-compilation-unit. Two modules declaring the same unqualified class
name collide — a real divergence from Rakudo, listed in Chapter 32.

The alias map softens the consequences of flatness. Registering `URI::Path` also
aliases its tail, `Path`, unless a real class already claims that name. And a
qualified name that is a *partial* path resolves by unique suffix match:

```cpp
// src/Interpreter.h — resolveClassAlias
if (n.find("::") != std::string::npos) {
    const std::string suffix = "::" + n;
    const std::string* hit = nullptr;
    for (auto& kv : classes_) {
        if (…kv.first does not end with suffix…) continue;
        if (hit) return n;          // ambiguous — leave it alone
        hit = &kv.first;
    }
    if (hit) return classAliases_.emplace(n, *hit).first->second;
}
```

so `Globber::Match` inside `unit class IO::Glob` finds
`IO::Glob::Globber::Match`. Only qualified names take that road — a bare `Match`
must not silently find a nested one — and an ambiguous suffix is left alone
rather than guessed at. The successful answer is memoised into the alias map.

## Construction

The default `new` and `bless` share one path. It walks the class chain
**parent-first**, gives each attribute its default, folds named arguments into
the attribute map, and then runs `BUILD` and `TWEAK`:

```cpp
// src/Builtins.cpp — default construction, abridged
for (auto it = chain.rbegin(); it != chain.rend(); ++it)      // parent-first
    for (auto& at : (*it)->attrs) {
        Value dv = at.hasDefVal ? at.defVal
                 : at.def       ? eval(at.def)
                 : at.sigil == '@' ? Value::array()
                 : at.sigil == '%' ? Value::makeHash() : Value::any();
        od->attrs[at.name] = dv;
    }
for (auto& arg : args)
    if (arg.t == VT::Pair) od->attrs[arg.s] = *arg.pairVal;
if (Value* build = ci->findMethod("BUILD")) invokeMethod(*build, self, args);
if (Value* tweak = ci->findMethod("TWEAK")) invokeMethod(*tweak, self, args);
```

Parent-first matters: a subclass's default for an inherited attribute must win,
and it does because it is written last.

Attribute defaults are *expressions*, evaluated at construction in the scope the
class was declared in — which is what `ClassInfo::declEnv` is for. The compiled
backend precomputes them where it can, into `ClassAttr::defVal`, and falls back
to the expression otherwise.

`is required` throws when construction supplies no value, carrying the reason
string from `is required("it is a good idea")` into the message. `is built`
makes a *private* attribute settable by name at construction anyway.

## Attributes and accessors

`$!x` is a direct read or write of the attribute map. `$.x` is a public
accessor: method lookup runs first, and on a miss `findAttr` supplies the
attribute. The compiled backend uses the same map through two helpers:

```cpp
// src/Interpreter.cpp
Value rtAttrGet(const Value& self, const std::string& name) {
    if (self.t == VT::Object && self.obj) {
        auto it = self.obj->attrs.find(name);
        if (it != self.obj->attrs.end()) return it->second;
    }
    return Value::any();                     // absent attribute → (Any)
}
Value& rtAttrRef(Value& self, const std::string& name) {
    return self.obj->attrs[name];            // autovivifies
}
```

`ClassAttr` carries more than storage: the declared type and its `:D`/`:U`
smiley, `is rw`, `is required`, `is built`, a container trait (`has %.a is Set`),
a `handles` list for delegation, an object-keyed flag, and **user traits**.

That last one is how third-party traits reach their own code. `has $.id is
json-name('licenseId')` records `{"json-name", Str}` on the attribute, surfaced
on the `Attribute` meta-object, which is exactly what `JSON::Unmarshal`'s role
checks and accessors look for. The parser does not know what `json-name` means
and does not need to.

Assigning through an accessor is guarded: `$obj.attr = v` on a private or
read-only attribute throws, while `$obj!attr` and a plain method stay writable.
The declared type is enforced too, which needs the attribute's type at the
assignment site — hence:

```cpp
// src/Interpreter.h — ExecContext
std::string lastLvalueAttrType;
```

recorded by the method-call lvalue arm so a role-typed `has C $.x is rw` rejects
`42`.

## Roles

A role is a `ClassInfo` with `isRole` set, a set of `requiredMethods`, and a set
of `doneRoles`. Composition copies the role's methods and attributes into the
class and records membership.

Required methods are checked **at class declaration**, using the same
`findMethod` that dispatch uses:

```cpp
for (ClassInfo* role : composed)
    for (const std::string& req : role->requiredMethods)
        if (!ci->findMethod(req))
            throw RakuError{Value::typeObj("X::Role::Unimplemented"), …};
```

`.does` and `~~` consult `doesRole`, which is true for the role itself, for
directly or transitively composed roles, and for roles done by parents:

```cpp
// src/Value.h
bool doesRole(const std::string& rn) const {
    if (isRole && name == rn) return true;
    if (doneRoles.count(rn)) return true;
    if (parent && parent->doesRole(rn)) return true;
    for (auto& p : extraParents) if (p && p->doesRole(rn)) return true;
    return false;
}
```

Attribute composition deduplicates by the address of the *declaring*
attribute node, recorded in `ClassAttr::declId`. So a diamond composition —
two roles both doing a third — contributes the shared attribute once rather
than twice.

**Parameterised roles** carry their parameters as a `Param` list on the AST
declaration, and a composition binds arguments to them:

```cpp
// src/Value.h — ClassInfo
std::vector<std::pair<std::string, Value>> roleParamBindings;
```

The bindings are injected into the scope of the composing class's methods, so
the role's body sees `%phase-defaults` in `does Cro::Policy::Timeout[%h]`.
`makeRolePun` handles the anonymous case, `R[Int].new`, by building a punned
class on the spot.

Role composition is **last-writer-wins**: two roles defining the same method
both copy into the table, with no conflict diagnostic. That is a known
divergence.

## Mixins: `but` and `does`

`5 but Role` and `%h does R` add a role to a value at run time.

For a value that is already an object, the role is composed into a fresh
anonymous subclass. For a **non-object base** there is no `ObjectData` to
extend, so the value is *boxed*:

```cpp
// src/Interpreter.cpp — mixinValue
obj = std::make_shared<ObjectData>();
obj->boxed = base;          // the original 5 is kept here
obj->hasBoxed = true;
// … build an anonymous subclass composing the role, wrap in a VT::Object …
```

Dispatch on the result checks the role's methods first; anything not found —
and not an identity method — is delegated back to the box:

```cpp
// src/Builtins.cpp — methodCall
if (inv.t == VT::Object && inv.obj && inv.obj->hasBoxed && inv.obj->cls &&
    !inv.obj->cls->findMethod(m) && !inv.obj->cls->findAttr(m)) {
    static const std::set<std::string> keepOnObj =
        {"does","HOW","WHAT","WHICH","defined","DEFINITE"};
    if (!keepOnObj.count(m))
        return methodCall(inv.obj->boxed, m, args, rwArgs);
}
```

So `(5 but Role).succ` runs `Int.succ` on the `5`, while the role's methods and
`.does`/`.WHAT` see the mixed object. The `keepOnObj` set is the list of
questions that must be answered *about the mixin* rather than about the box.

`$x but Pair` mixes an attribute rather than a role, using the same machinery
with a synthesised anonymous role — which is what `anonMixinSeq_` names.

## `augment` and `supersede`

`augment class Foo { … }` on a user class merges methods into the existing
`ClassInfo`. On a **built-in type** there is no `ClassInfo`, so the methods go
into a side table consulted before the native dispatch ladder:

```cpp
// src/Interpreter.h
std::unordered_map<std::string,
                   std::unordered_map<std::string, Value>> builtinExt_;
```

keyed by type name then method name, and searched along the native ancestry so
augmenting `Cool` reaches `Int` and `Str`. Chapter 16 covers the dispatch side;
Chapter 28 covers why two compiled fast paths have to check whether this map is
empty.

`.^add_method($name, $code)` writes into `ClassInfo::methods` directly, which is
the whole of runtime method injection.

## Package stashes and `.WHO`

```cpp
// src/Interpreter.h
std::map<std::string,
         std::shared_ptr<std::map<std::string, Value>>> pkgStashes_;
```

One shared map per package **name**, and the sharing is the point. `.WHO` used
to build a fresh empty `Hash` on each call, so `EXPORTHOW.WHO.<grammar> =
SomeHOW` wrote into a temporary and died with "Target is not assignable".
Reading a stash also re-syncs the package's qualified globals into it, so
`our`-scoped symbols appear.

A `module` or `package` has no `ClassInfo` at all, so its `:ver`/`:auth`/`:api`
adverbs live in a separate small map, `pkgMeta_`, which is what `.^ver` answers
for a package.

## Honest limitations

- **Depth-first method resolution**, as in Chapter 16.
- **Role conflicts are silent**, last writer wins.
- **The class registry is flat**, so unqualified class names from different
  modules collide. The alias machinery reduces the pain but does not remove the
  cause.
- **`ClassInfo::decl` is a raw `ClassDecl*`** into the AST, kept alive by the
  same "the tree is immortal" rule as `Callable::body` (Chapter 7).
