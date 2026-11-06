# Plan: object slots — an object's attributes in an array its class lays out

*Written 2026-11-05, before any code: V6-PLAN P3.11, phase 1. Phase 0, the
same day (a90fb35e), made the hash an object keeps its attributes in leaner;
this replaces that hash for the objects whose attributes their class declares.*

## The number to move

200k two-attribute objects kept in an array, peak RSS above an empty
program, per object (tools/run-memory.raku's `objects` row; the shapes below
measured with the same method):

| | bytes | of which |
|---|---:|---|
| two attributes | **512** (Rakudo 494, 1.04×) | 94 the array's Value and slack, 144 the body, 224 the entries, ~50 the allocator |
| none | 258 | |
| three | 679 | |
| five | 1,009 | the entries' chunk is 5 × 112, and past four entries a probe index |

An attribute entry is 112 bytes: its 80-byte Value, a 24-byte `std::string`
holding the attribute's name — the same name in every object of the class —
and its 8-byte hash. The body is 144: `ObjectData` (128) is the class pointer
(16), the 96-byte `ValueHash` (a chunk list, an index, two counts and the cold
pointer), the cold pointer of its own and a flag.

**Target:** two attributes ~350 bytes (0.71× Rakudo), five ~590, none ~190 —
the names, hashes and table header gone from every object.

## The design

**AttrLayout, per class.** The keys an instance of the class keeps its
declared attributes under, in the order `runAttrDefaults` stores them: least
derived class first (its `lvlIx` walk), each class's attributes in
declaration order, each under `ClassInfo::attrSlot`'s key (the bare name; the
sigil twin `@name`; `Owner\x01name` where a more derived class declares the
name too). With each key its hash, and a small index from key to slot.
Immutable once built and refcounted: the objects built from it hold it. A
class whose attribute list changes after a layout was built — `makeTypedEx`
appends attributes to an exception class as call sites name them, and the MOP
(`add_attribute`, `augment`) can too — gets a new layout for the objects built
after; the earlier ones keep theirs. Built once per class version, under a
lock (objects are made on worker threads).

**AttrTable, the stand-in for `ObjectData::attrs`.** The same name, and the
interface the 397 uses below need, over two kinds of storage:

- `slots`: one Value per layout key, allocated once at the object's
  construction and never moved (the reference stability ChunkList gives
  ValueHash: a `Value&` into an attribute is held across further stores);
- presence bits: a key is in the table only once something stores it, as in
  ValueHash — `count` answers false and `find` gives `end()` for an attribute
  no default has run for yet. The construction walk reads attributes before
  all are stored (a later default reads an earlier one), and `mixinValue`
  skips a role attribute the object `count`s already. One word for up to 64
  slots, a second array past that;
- `extra`: a ValueHash, allocated on first use, for every key the layout does
  not have — the engine's hidden keys on user objects (`__native_ptr` on a
  CStruct, the `\x01` ones), the attributes a role mixed into an object adds
  (`$o does R` makes a new class and stores the role's attributes on the
  same object: mixinValue), and every key of an object that has no layout.

An object has a layout only when `runAttrDefaults` builds it — `.new`,
`bless`, and everything that reaches the construction protocol — at the line
that now calls `od->attrs.reserve(nAttr)`, which becomes "adopt the class's
layout" while the table is still empty. The other places that make an
`ObjectData` (78 `makePayload<ObjectData>` sites in all) and fill it by name
— `makeTypedEx`'s exceptions, RakuAST nodes, Promise and Supply internals, the
CompUnit repositories, mixin containers — keep a table with no layout: all
their keys go to `extra`, which is today's ValueHash, so they behave exactly
as they do now.

**Iteration** walks the present slots in layout order, then `extra` in
insertion order. For an object `runAttrDefaults` built that is the order its
keys were stored in today, and a key a mixin adds comes after them, as it does
now. An element is a proxy, `{ const std::string& first; Value& second; }`,
the key read from the layout; `it->first` and `it->second` go through an
arrow proxy, and the range-for sites that bind `auto& kv` change to `auto&&`
(10 sites: a reference cannot bind the proxy).

**Copy and assignment** (`clone`, a mixin over a prototype, `but`): the same
layout copies slots, presence and `extra`; a different layout copies by key
into this table's layout and `extra`.

**After:** `ObjectData` is the class pointer (16), the table (layout, slots,
presence, extra: ~32) and the cold pointer (8), ~64 bytes, an 80-byte body;
the slots one more block, 80 bytes a slot. Two attributes: 80 + 160 + 94 +
allocator ≈ 350.

## Batches

Each lands alone and is gated alone (below); 1 and 2 change no answer.

1. **The layout, checked.** AttrLayout on ClassInfo, built by the code
   `runAttrDefaults` uses for its keys (factored out, so the two cannot
   drift), and a check that every object it builds stores exactly the
   layout's keys in the layout's order. The check runs on through Roast, the
   suite and the module battery, then becomes a test. *Done 2026-11-06:
   below.*
2. **The type swap.** `ObjectData::attrs` becomes an AttrTable in its
   table-only form: every key in `extra`. The compiler finds the sites (the
   method below); most are `[]`, `find`, `end` and `count`, which keep their
   shape. Behaviour is unchanged; memory is slightly worse (the table moves
   out of line), and that cost is measured here, on RakuAST trees and on
   exception-heavy code, before layouts make it back. *Done 2026-11-06,
   below: out of line it cost RakuAST 5.7%, so the hash stays in the
   object's own block.*
3. **Layout mode.** `runAttrDefaults` adopts the layout; slots, presence,
   the copy rules, mixins and hidden keys to `extra`. Measured on the memory
   table and perf-guard.
4. *Optional:* the slots inside the object's own body (a payload sized per
   class, from the slab), one allocation an object instead of two: about 20
   bytes and one malloc less.
5. *Optional:* a slot number cached where an attribute is read — the
   accessor (`findAccessorAttr`), `$!x` in a method — keyed by the layout,
   so a read skips the key lookup. A speed step: `objects` is 1.56× Rakudo at
   steady state (V6-PLAN P0).

## Batch 1, done 2026-11-06

`src/AttrLayout.h` holds the key rule, `attrStoreKey`, which runAttrDefaults
calls for every attribute it stores and the layout is built from: the bare
name, a sigil twin's `@name`, or `Owner\x01name` where a more derived level
declares the name too. `ClassInfo::attrLayout` is the layout, built on first
ask and kept per version of the chain: each level's class and attribute
count, so a class `makeTypedEx` or the MOP grows gets a new one. Nothing reads
it yet but the check.

`RAKUPP_ATTR_LAYOUT_CHECK=<file>` compares every object runAttrDefaults
builds with its class's layout, and appends each distinct difference and a
per-process summary to the file. Unset, it is one predicted branch per
construction. Retired instructions against main: objnew equal (within noise),
the three-level chain (B2 is B1 is B0, a shadowed `$.a`) +0.08%. The key rule
has to be inlined: out of line, as the compiler first left it, it cost every
construction ~86 instructions (objnew +0.8%, the chain +1.1%).

| run | processes | objects checked | differed |
|---|---:|---:|---:|
| Roast, all 1,425 files (221,034 tests, all pass) | 312 | 132,107 | 9 |
| `t/run.raku` (1,966 checks, all pass, with and without it) | 554 | 649,300 | 4 |
| installed modules (below) | 53 | 21,507 | 14 |

Every difference is the same kind, `nonempty,beyond`: an object whose class
is an instance of a user's metaclass already holds the engine's hidden
`__type` when its attributes are seeded, stored first. Its declared keys
follow in the layout's order (`stored=__type,aspects layout=aspects`). Roast's
come from ClassWithAspectsHOW, TestHOW, `EXPORTHOW::SUPERSEDE::class`, three
`EXPORTHOW::DECLARE` ones and ProfiledGrammarHOW. The suite's come from
MetamodelX::HookProbe, ProbeHOW and TracingHOW, and the modules' all from
MetamodelX::MonitorHOW (OO::Monitors). No object stored its declared keys in
another order, and none missed a key.

The module battery was not on this machine (tools/battery-scan.raku needs its
checkout). The 87 distributions installed in `~/.raku` stood in: `use` of each
of the 380 modules they provide with the check on, and the suite's module
tests that load them. 376 load. Cro::Tools::CLI, Webraku, Webraku::HTML and
Webraku::Server fail to load on the build before this batch too.

One order difference was found by building it, not in any run: a runtime
`set_build` closure that stores a LATER attribute stores it before its own
(nothing seeds an attribute that has no default first), (b, a) against the
layout's (a, b). `.raku` lists declaration order on both engines, so nothing
seen shows it.

t/regression/attr-layout-store-order.raku is the check made a test. It runs
programs covering shadowing, twins, roles and role parameters, a second
class parent, `does R is A`, defaults reading and writing other attributes,
`is built`, BUILD, TWEAK, typed and trait containers, a mixin and a class
under an exported HOW. It pins the two known differences.

For batch 3: in layout mode `__type` goes to `extra`, so it is iterated after
the slots where it is now first; the walks over an object's table must skip
hidden keys, which batch 3 checks site by site.

## Batch 2, done 2026-11-06

`ObjectData::attrs` is an AttrTable (src/AttrTable.h): ValueHash's interface,
every key in `extra`, a ValueHash. Iteration yields a pair of references
(`kv.first`, `kv.second`), not the hash's own pair, so batch 3 can hand out a
key from the layout and a value from a slot. The compiler found the sites
again: of the 397 uses, only the 10 range-for loops that bound `auto&` failed
to compile (now `auto&&`). Everything else kept its shape. The cycle
collector's `hashEdges` takes either table.

**Where the hash lives.** First it was a block of its own, allocated on the
first store. That cost every object one more allocation, and RakuAST paid for
it: `.AST` makes ~2,500 nodes from 357 lines, and each one paid a malloc
(11,256 → 13,724 per parse). Retired instructions went up 5.7%, over the 2%
this plan allows. So an object made by makePayload carries its hash in its
own slab block, right behind the body, and frees it with the body. The
allocation count is back to main's, exactly. A table made any other way still
allocates its hash on the first store, and the flag that marks the in-body
hash fits in padding.

Retired instructions against main (min of 3 runs):

| row | own block | in the object's block |
|---|---:|---:|
| `.AST` of 357 lines, 300 times | +5.73% | **+0.36%** |
| 100 such trees held | +5.35% | **+0.28%** |
| 100k `RakuAST::ApplyInfix.new` held | +1.95% | **+0.02%** |
| `try { die "x" }`, 200k | +0.01% | **−0.07%** |
| `die` caught by CATCH in a sub, 200k | +0.12% | **−0.02%** |
| `try { Int.no-such-method }`, 20k | −3.58% | **−3.60%** |
| 100k `$!` held | +0.05% | **−0.10%** |
| 20k X::Method::NotFound held | −3.59% | **−3.62%** |
| perf-guard `objnew` | +0.56% | **+0.32%** |
| `attrread`, `method`, `privmeth`, `multimeth`, `userplus` | ±0.25% | **±0.05%** |

Peak RSS moved by no more than its run-to-run spread on any row, in either
form. tools/run-memory.raku is unchanged: objects 512 bytes, hashes 663 (662),
every other row within a byte. tools/malloc-census.raku is unchanged too:
`objects` 2.00 mallocs / 448 bytes, `objnew` 1.00 / 224. The block is 152
bytes (a 56-byte body and the 96-byte hash) where the body was 144: one slab
class up, which the memory table does not show.

Gates: t/run.raku 1,967/1,967. All of Roast, 1,425 files (221,034 tests,
all pass). The installed modules load as on main: 379 of 380, Cro::Tools::CLI
failing on both. The 113 regression files on objects and hashes give the
interpreter's output under `--cnp=threshold=0`. Under `--exe`, 22 of them
print something else, byte for byte what main's `--exe` binaries print. Those
are `--exe` lane gaps the suite does not run, not this change.
t/regression/attr-table-empty.raku walks an object that stores nothing
through every reader of its table.

For batch 3: an object made with a layout must be allocated without the
tail, so the factory has to know the class's layout at allocation time.
`bless` makes the object before runAttrDefaults runs. A layout object that
needs `extra` (a mixin, a hidden key) allocates it on first store. An object
with no layout keeps its hash in its block at +16 bytes over today, +32 once
the table holds the layout fields.

## The sites

397 uses of `ObjectData::attrs` in 24 files on 2026-11-05 (main at
6943e980), on 392 lines, every line listed in
[object-attrs-sites-2026-11-05.tsv](../findings/object-attrs-sites-2026-11-05.tsv)
with its operation and its code. Grep cannot tell them from `ClassInfo::attrs` (the
declarations); the compiler can:

1. rename the field in `src/Value.h` (`ValueMap attrs;` in `struct ObjectData`);
2. run every translation unit's command from `build/compile_commands.json`
   with `-fsyntax-only -ferror-limit=0 -w`;
3. keep each location of "no member named 'attrs' in 'rakupp::ObjectData'"
   once (a header reached from several units is reported by each);
4. restore the header.

By operation:

| what follows `attrs` | uses | in AttrTable |
|---|---:|---|
| `[key]` | 172 | the layout's slot (marked present), else `extra` |
| `.end()` | 74 | one past the last present slot and the last extra entry |
| `.find(key)` | 70 | slot if present, else `extra` |
| `.count(key)` | 33 | the same, as 0 or 1 |
| bound to a local (`auto& at = obj->attrs`) | 18 | unchanged; those locals then call `count` 23 times, `[]` 31, `find` 12, `end` 11, `emplace` once |
| range-for | 10 | `auto&` → `auto&&` |
| whole-table assignment | 6 | the copy rules above |
| `.at(key)` | 5 | as `find`, throwing |
| `.erase(key)` | 4 | presence cleared (slot reset), or `extra`'s erase — all four on objects with no layout (Promise's `__awaitbt`/`__reactbt`, RakuAST's `left`/`right`) |
| `.size()` | 2 | present slots + extra's size (`eqv`'s first test) |
| `.reserve()`, `.clear()`, passed as an argument | 1 each | adopt the layout; both stores; the cycle collector's `hashEdges` gets an overload |

By file: MethodCallPart2.cpp 77, InterpreterCore.cpp 50, InterpreterBinding.cpp
44, Builtins.cpp 42, InterpreterOperators.cpp 34, RakuAstView.cpp 33,
Interpreter.cpp 19, RakuAstDeparse.cpp 17, MethodCallPart1b.cpp 15, Value.cpp 8,
MethodCallPart1c.cpp 8, BuiltinsSupply.cpp 7, RakuAstClasses.cpp 6,
MethodCallTail.cpp 6, AotModules.cpp 6, InterpreterModules.cpp 5,
BuiltinsRegister.cpp 4, BuiltinsRegister2.cpp 3, BuiltinsNqp.cpp 3,
RakuAstDump.cpp 2, RaceDetector.cpp 2, InterpreterCalls.cpp 2, Cycle.cpp 2,
BuiltinsRegister3.cpp 2.

Of the keys written literally at the `[]`, `find`, `count`, `at` and `erase`
sites, none carries a sigil: 142 are bare names (`name`, `message`, `payload`,
RakuAST fields) and 66 hidden `__` or `\x01` keys; 90 sites compute the key.
Generated `--exe` code reaches attributes through the runtime only (Codegen's
one `->attrs` string is `ClassInfo::attrs`), so the compiler's list is
complete for compiled programs too. The JavaScript runtime has its own
objects and is not touched.

## Risks

- **Iteration order.** Anything that walks an object's attributes — `.raku`,
  `.gist`, `eqv` (Value.cpp: by size, then key by key), `.Capture`, `clone`,
  introspection — sees layout order. That equals today's insertion order only
  if batch 1's check holds: it is what the check is for. A key erased and
  stored again moved to the end before and returns to its place now; the four
  erases are on objects with no layout.
- **Presence.** A slot that exists but was never stored must read as absent,
  or `runAttrDefaults`' "was it bound already" and BUILD's reads of an
  attribute its default has not set yet change their answers.
- **References into slots.** A `Value&` taken from a slot (an rw accessor, an
  lvalue store, the race detector's address match) must stay valid while the
  object lives: slots are allocated once and never resized, and a table
  assigned over keeps its slot storage when the layouts agree. Nothing takes
  a lasting alias into an attribute (no `aliasOf` on an object's table in the
  census), so only references within one expression are at stake.
- **Classes that change.** `makeTypedEx` grows exception classes at run time,
  and the MOP can add attributes after objects exist. Mitigated by layout
  versions: an object keeps the layout it was built with, and a key it lacks
  goes to `extra`.
- **Objects with no layout get slower and larger.** Their ValueHash moves
  behind a pointer: one more allocation and one more indirection for every
  exception, RakuAST node and engine-made object. Batch 2 measures it alone;
  if it shows, the table keeps its ValueHash inline for those (a tag in the
  table), and layout objects pay for the tag only. *It showed (RakuAST
  +5.7%), and the hash now lives in the object's own block: batch 2.*
- **Attribute reads.** A lookup goes key → the layout's index → slot, against
  today's scan of the entries' hashes. It must not be slower on perf-guard's
  `attrread`, `method`, `privmeth` and `objnew` (batch 5 then makes it
  faster).
- **Threads.** A layout built while workers construct objects of the class:
  once per version, under a lock, published atomically. Objects themselves
  keep today's rules (one writer, the stripes).
- **The cycle collector** walks and clears an object's held Values
  (`hashEdges`, `breakBody`): both learn the slots, the presence bits (an
  absent slot holds nothing) and `extra`.
- **Budget.** InterpreterCore.cpp is at its line ceiling: its 50 sites change
  in place, net zero.

## What would stop it

- Batch 1's check fails on a program whose attribute order is not the
  layout's, and the order is observable there — then the layout follows
  insertion order per object, and the slots buy only the names.
- Batch 2 costs RakuAST or exception-heavy code more than 2% (time or peak
  memory), and keeping their table inline costs layout objects what the
  slots save.
- Batch 3 leaves `attrread` or `method` slower by more than 2% in retired
  instructions.

## Gates, every batch

All of Roast (`spectest.data`, 1,425 files: these batches change how every
object is stored), `t/run.raku`, the module battery scan and `use` smoke
(tools/battery-scan.raku, tools/battery-use-smoke.raku), the regression files
on objects and hashes run under `--exe` and `--cnp` as well, perf-guard A/B
by instruction count on `attrread`, `method`, `privmeth`, `objnew`,
`multimeth` and `userplus`, tools/run-memory.raku (no row worse), and
tools/malloc-census.raku's `objects` and `objnew`.
