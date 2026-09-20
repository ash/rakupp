# Plan: a two-attribute object should not cost a 4 KB block

*Written 2026-09-20. Comes out of
[PAYLOAD-SLAB-PLAN.md](PAYLOAD-SLAB-PLAN.md)'s allocation census, which went
looking for the allocator's cost and found most of it somewhere the slab cannot
reach. No code written yet.*

## The measurement

`tools/bench/objects.raku` builds 200k two-attribute `Point`s. Counted with
[tools/malloc-census.c](../../../tools/malloc-census.c), one object costs
**about eleven allocations**:

| size | per object | what |
|---:|---:|---|
| 4032 | 1 | **`ValueHash`'s first `std::deque` block** |
| 304 | 1 | `ObjectData` + control block |
| 168 | 3 | |
| 152 | 2 | |
| 136 | 1 | `ValueHash` control block |
| 64 | 1 | |
| 32 | 2 | |
| 8 | 1 | |

The 4032 is the whole story by volume: **200,006 × 4032 = 806 MB of the
kernel's 1088 MB**, or 74% of every byte it allocates. It is one block per
object, and it holds two attributes.

The number is not mysterious. `Entry` is 168 bytes
(`pair<const std::string, Value>` is 152, plus the cached hash and the dead
flag), and libc++ sizes a `std::deque` block at 4096 bytes' worth of elements:
24 × 168 = **4032**. Reproduce it with:

```bash
c++ -std=c++17 -O2 -Isrc -Iinclude -o /tmp/esz - <<'EOF' && /tmp/esz
#include "Value.h"
#include "ValueHash.h"
#include <cstdio>
int main(){ printf("%zu\n", sizeof(std::pair<const std::string, rakupp::Value>)); }
EOF
```

So every `ValueHash` that receives a single key allocates storage for 24
entries. Every object's attribute table is a `ValueHash`. Most objects have two
or three attributes and will never have more, because `has` fixed the count at
class-declaration time.

## Why the deque is there, and why it cannot simply go

[src/ValueHash.h](../../../src/ValueHash.h) states the contract:

> REFERENCE STABILITY. rtIndexRef and the lvalue paths hold `Value&` into the
> payload across further inserts (autovivification). Entries therefore live in
> a deque (push_back never moves existing elements) and erase only marks.

That is load-bearing and must survive intact. A `std::vector<Entry>` would
reallocate and leave the interpreter holding dangling `Value&` on the
autovivification path — which is exactly the bug the deque was chosen to
prevent. **Any design here that trades stability for size is wrong**, however
good its allocation numbers look.

The opportunity is narrower than "replace the deque", and better: the deque's
*stability* is needed, its *block size* is not. Nothing requires the first
chunk to hold 24 entries.

## The shape of the change

Replace `std::deque<Entry>` with a small chunked list that keeps the identical
guarantee — entries never move once constructed — but starts small:

- first chunk sized to the hash's actual need (4 entries = 672 bytes, against
  4032 today), growing geometrically after that;
- a chunk is never reallocated, only appended to a chunk table, so `&entry` is
  stable for life. Same property the deque provides, same erase-marks-only
  behaviour, same insertion-ordered iteration;
- `reserve(n)` so a caller that knows the count says so.

And the caller that knows the count is the interesting one: **an object's
attribute table has a known size before it is filled.** `ClassInfo` knows how
many `has` declarations it has. Sizing an object's `attrs` exactly once, at
construction, turns the 4032 into one right-sized block and removes the growth
path entirely for the overwhelmingly common case.

## Order

1. Write the chunked container behind `ValueHash`'s existing private interface,
   with the first-chunk size a constant. Nothing outside `ValueHash.h` changes.
   Gate on stability directly: a test that holds a `Value&` across enough
   inserts to cross several chunk boundaries and asserts the reference still
   reads correctly.
2. Re-run the census on `objects.raku`. The 4032 line should become a small
   size, and total bytes should fall by most of 806 MB. **If it does not, stop**
   — the block is coming from somewhere other than the first chunk.
3. Only then size an object's `attrs` from its `has` count.
4. Attribute the remaining census rows (168×3, 152×2, 64, 32×2, 8 per object).
   They are 8 of the 11 allocations and nobody has yet said what they are.

## What would falsify this

- If peak RSS on `objects.raku` does not fall, the 806 MB was transient churn
  that the allocator was already recycling, and the win is smaller than the
  byte count suggests. **Peak RSS is the honest gate here, not bytes
  allocated** — and it is the column
  [PAYLOAD-SLAB-PLAN.md](PAYLOAD-SLAB-PLAN.md) found trustworthy when the CPU
  numbers were not.
- If a smaller first chunk costs more in chunk-table indirection on
  hash-iteration-heavy programs than it saves on object construction, the right
  answer is to size only the object-attribute case and leave general hashes on
  the current block size.
- If the stability test fails at any chunk size, the container is wrong and no
  size number matters.

## What this is not

It is not a `sizeof(Value)` change, and it does not need one. `Entry` is 168
bytes mostly because `Value` is 128; [VALUE32-PLAN.md](VALUE32-PLAN.md) shrinking
`Value` would shrink `Entry` too and make each chunk hold more. The two are
independent and compose — but this plan's win does not depend on that one
landing, and should not be budgeted against it.
