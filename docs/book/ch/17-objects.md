# The Object System

Raku's object surface is one of the largest in the language: classes with
inheritance, roles, runtime mixins, a meta-object protocol, `augment` and
package stashes. A reader arriving from C++ or the JVM would
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
    std::shared_ptr<const AttrLayout> attrLayout;
    ValueMap methods;                                     // Code values
    std::map<std::string, std::string> rules;             // grammar rules
    std::vector<std::string> ruleOrder;
    bool isGrammar = false, isRole = false;
    std::string repr;                                     // is repr('CStruct')
    std::string ver, auth, api;
    std::vector<std::string> requiredMethods;
    std::set<std::string> doneRoles;
    Value howObj;                                         // persistent .HOW
    std::shared_ptr<Env> declEnv;                         // for attr defaults
    ClassDecl* decl = nullptr;                            // for role parameters
    std::vector<std::pair<std::string, Value>> roleParamBindings;
};

struct ObjectData {
    std::shared_ptr<ClassInfo> cls;
    AttrTable attrs;
    ColdSlot cold;                // boxed (for `5 but Role`), a monitor's lock
    bool hasBoxed = false;
};
```

Everything a class *is* lives in that first struct, including the grammar rules
if it happens to be a grammar — which is how a grammar is just a class whose
methods can be regexes (Chapter 22).

An object's attributes are an `AttrTable` (`src/AttrTable.h`). Today that is
the interface of a `ValueHash` over a `ValueHash`: every attribute is an entry
keyed by its name, and an object made by `makePayload` carries the hash in its
own block, right behind the body, so an object is one allocation. The type
exists for what comes next. A class already builds an `AttrLayout`, the keys
an instance stores its declared attributes under, in the order construction
stores them, and a check run across Roast and the test suite found no object
that stored its declared keys in another order. The step that puts the values
in an array the layout indexes, with the names and hashes gone from every
object, has not landed (`docs/dev/plans/OBJECT-SLOTS-PLAN.md`, batches 1 and 2
done 2026-11-06). The rare parts of an object — the boxed value of a mixin
over a non-object, a `monitor`'s lock — sit behind one pointer, allocated on
first use.

## Registration and lookup

`ClassInfo`s are registered in one flat map at declaration:

```cpp
// src/Interpreter.h
StrMap<std::shared_ptr<ClassInfo>> classes_;
StrMap<std::string> classAliases_;
```

Flat, not per-compilation-unit. Two modules declaring the same unqualified class
name collide — a real divergence from Rakudo, listed in Chapter 33.

The alias map softens the consequences of flatness. Registering `URI::Path` also
aliases its tail, `Path`, unless a real class already claims that name. And a
qualified name that is a *partial* path resolves by unique suffix match:

```cpp
// src/Interpreter.h — classAliasOf, behind resolveClassAlias
if (n.find("::") != std::string::npos && !isSettingMetamodelName(n)) {
    const std::string suffix = strCat("::", n);
    const std::string_view* hit = nullptr;
    for (auto& kv : classes_) {
        if (…kv.first does not end with suffix…) continue;
        if (hit) return nullptr;   // ambiguous — leave it alone
        hit = &kv.first;
    }
    if (hit) return &classAliases_.emplace(n, *hit).first->second;
}
```

so `Globber::Match` inside `unit class IO::Glob` finds
`IO::Glob::Globber::Match`. Only qualified names take that road — a bare `Match`
must not silently find a nested one — and an ambiguous suffix is left alone
rather than guessed at. The successful answer is memoised into the alias map.

## Construction

The default `new` and `bless` share one path, `constructDefault`, and its
core is `runAttrDefaults`. It walks the class chain **least derived first**,
and for each attribute stores either the caller's named argument or the
attribute's default, and then the `BUILD` and `TWEAK` submethods run:

```cpp
// src/MethodCallPart2.cpp — runAttrDefaults, abridged
for (size_t lvlIx = nChain; lvlIx-- > 0;) {          // least derived first
    ClassInfo* lvl = chainAt(lvlIx);
    …
        const std::string& slot = attrStoreKey(chain, lvlIx, at, slotBuf);
        …
        Value dv = at.hasDefVal ? at.defVal
        …
        od->attrs[slot] = dv;
}
```

Named arguments bind *during* the walk, not after it. Rakudo's `BUILDALL`
takes the caller's value for an attribute when one was passed and only
otherwise runs the default, so a later default that reads an earlier
attribute sees the constructed value. A class that declares its own
`submethod BUILD` initialises its attributes itself, and the arguments are
not bound for that level.

`attrStoreKey` is the one rule for the key an attribute is stored under: the
bare name; a sigil-prefixed `@name` where one class declares `$.name` and
`@.name` both; and a class-qualified key for an ancestor's attribute that a
more derived class declares again. A subclass that redeclares a name
therefore shadows the ancestor's attribute, it does not overwrite it, and
the subclass keeps the bare name, which is where its accessor reads.

Attribute defaults are *expressions*, evaluated at construction in the scope the
class was declared in — which is what `ClassInfo::declEnv` is for. The compiled
backend precomputes them where it can, into `ClassAttr::defVal`, and falls back
to the expression otherwise.

`is required` throws when construction supplies no value, carrying the reason
string from `is required("it is a good idea")` into the message. `is built`
makes a *private* attribute settable by name at construction anyway.

## Attributes and accessors

`$!x` is a direct read or write of the attribute table. `$.x` is a public
accessor: method lookup runs first, and on a miss `findAttr` supplies the
attribute. The compiled backend uses the same table through two helpers:

```cpp
// src/InterpreterBinding.cpp
Value rtAttrGet(const Value& self, const std::string& name) {
    if (self.t == VT::Object && self.obj()) {
        auto it = self.obj()->attrs.find(name);
        if (it != self.obj()->attrs.end()) return it->second;
    }
    return Value::any();                     // absent attribute → (Any)
}
Value& rtAttrRef(Value& self, const std::string& name) {
    …
    Value& slot = self.obj()->attrs[name];   // autovivifies
    …
    return slot;
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

Required methods are checked **at class declaration**, and the check is more
generous than a lookup, because there are four honest ways to satisfy a
requirement:

```cpp
// src/InterpreterModules.cpp — for each name a role requires
ok = hasImpl(ci.get(), rq, nullptr)   // a non-stub implementation anywhere
  || classOwn.count(rq);              // …or an own stub: a deliberate promise
if (!ok && attrCovers(ci.get(), rq)) ok = true;  // …or a public attribute's
                                                 // accessor, or a `handles`
…
if (!ok) {
    …
    throw RakuError{Value::typeObj("X::Comp::AdHoc"),
        "Method '" + rq + "' must be implemented by " + clsName +
        " because it is required by roles: " + (rl.empty() ? "?" : rl) + "."};
}
```

A *stubbed multi* is checked per candidate signature rather than by name, so a
class that implements one of two required signatures is still incomplete, and
that failure has its own type, `X::Role::Unimplemented::Multi`.

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

Two roles defining the same method is a **conflict**, not a race: composition
raises `X::Role::Unresolved::Method` — "Method 'r' must be resolved by class E
because it exists in multiple roles (R2, R1)" — unless the class declares the
name itself. That is Rakudo's sentence, roles in Rakudo's order (checked
against Rakudo 2026.09).

## Mixins: `but` and `does`

`5 but Role` and `%h does R` add a role to a value at run time.

For a value that is already an object, the role is composed into a fresh
anonymous subclass. For a **non-object base** there is no `ObjectData` to
extend, so the value is *boxed*:

```cpp
// src/InterpreterOperators.cpp — mixinValue
obj = makePayload<ObjectData>();
obj->boxed() = base;        // the original 5 is kept here
obj->hasBoxed = true;
// … build an anonymous subclass composing the role, wrap in a VT::Object …
```

Dispatch on the result checks the role's methods first; anything not found —
and not an identity method — is delegated back to the box:

```cpp
// src/Builtins.cpp — the method chain, abridged
if (inv.t == VT::Object && inv.obj() && inv.obj()->hasBoxed &&
    inv.obj()->cls &&
    (m.skipOwn || (!inv.obj()->cls->findMethod(m) &&
                   !inv.obj()->cls->findAttr(m.s)))) {
    static const std::set<std::string> keepOnObj = {
        "does", "HOW", "WHAT", "WHICH", "defined", "DEFINITE", "isa",
        "WHERE", "can", "VAR"};
    if (!keepOnObj.count(m)) {
        Value r = methodCall(inv.obj()->boxed(), m, args, rwArgs);
        …
    }
}
```

So `(5 but Role).succ` runs `Int.succ` on the `5`, while the role's methods and
`.does`/`.WHAT` see the mixed object. The `keepOnObj` set is the list of
questions that must be answered *about the mixin* rather than about the box.

`$x but Pair` mixes an attribute rather than a role, using the same machinery
with a synthesised anonymous role — which is what `anonMixinSeq_` names.

## `augment`, and the `supersede` that is not there

`augment class Foo { … }` on a user class merges methods into the existing
`ClassInfo`. On a **built-in type** there is no `ClassInfo`, so the methods go
into a side table consulted before the native dispatch ladder:

```cpp
// src/Interpreter.h
std::unordered_map<std::string,
                   std::unordered_map<std::string, Value>> builtinExt_;
```

keyed by type name then method name, and searched along the native ancestry so
augmenting `Cool` reaches `Int` and `Str`. Both forms need `use MONKEY-TYPING`,
as they do in Rakudo — without it the parser refuses `augment` outright.

`supersede` is a different matter: it is **not implemented in any form**. The
word is in the syntax highlighter's keyword list, which is the only place it
appears, so `supersede class S { … }` reaches the ordinary declaration path and
dies with `X::Redeclaration`. Replacing a method on a live class is
`.^add_method`.

Chapter 16 covers the dispatch side;
Chapter 28 covers why two compiled fast paths have to check whether this map is
empty.

`.^add_method($name, $code)` writes into `ClassInfo::methods` directly, which is
the whole of runtime method injection.

## Package stashes and `.WHO`

```cpp
// src/Interpreter.h
std::map<std::string, PRef<ValueMap>> pkgStashes_;
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
- **The class registry is flat**, so unqualified class names from different
  modules collide. The alias machinery reduces the pain but does not remove the
  cause.
- **`ClassInfo::decl` is a raw `ClassDecl*`** into the AST, kept alive by the
  same "the tree is immortal" rule as `Callable::body` (Chapter 7).
