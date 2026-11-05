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
   suite and the module battery, then becomes a test.
2. **The type swap.** `ObjectData::attrs` becomes an AttrTable in its
   table-only form: every key in `extra`. The compiler finds the sites (the
   method below); most are `[]`, `find`, `end` and `count`, which keep their
   shape. Behaviour is unchanged; memory is slightly worse (the table moves
   out of line), and that cost is measured here, on RakuAST trees and on
   exception-heavy code, before layouts make it back.
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
  table), and layout objects pay for the tag only.
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
