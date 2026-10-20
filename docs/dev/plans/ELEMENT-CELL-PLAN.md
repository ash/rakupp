# Plan: an array element that holds a container

*Written 2026-10-20, after the cheap half landed (see "Done" below) and one
experiment with the expensive half. Not started beyond that experiment.*

In Rakudo every array element is a `Scalar`, and binding to an element
(`my $x := @a[i]`, `my \l = @a[i]`, `@p.BIND-POS(j, @a[i])`) shares that
container. In rakupp an element is a bare `Value` in the array's buffer, so a
bind cannot share anything. It builds a **positional alias**: a `Proxy` hash
that names the array and the index (`makeArraySlotProxy`,
src/InterpreterCalls.cpp). This plan is about replacing the positional alias
with a real per-element container, and about why that has not been done.

## Done: the alias is cheap (2026-10-20)

BinaryHeap 2.0.3 binds three or four elements per heap level, and its
benchmarks ran 9–12x Rakudo
([docs/dev/ecosystem/BINARYHEAP.md](../ecosystem/BINARYHEAP.md)). Most of
the per-bind cost was the hash, not the binding:

- The slot proxy had grown to six entries (array, index, length at bind time,
  last value, FETCH, STORE). A `ValueHash` keeps four entries in its first
  chunk with no index, so a fifth grew the chunk list and built the probe
  index: five allocations and a rehash for every bind.
- `proxy.hashKind = "Proxy"` interned the string on every proxy, under the
  intern table's lock (5% of a bind).
- Reading a variable bound to a proxy (`$x = $n`) left the pad fast path,
  walked the whole of evalVarExpr, and called FETCH as a routine
  (`callCallable`), although deproxy already read both engine proxies directly.

Now the slot proxy has four entries. The index and the length at bind time are
packed into the array entry's `i` word, so the array is still an edge that
the cycle collector sees. The shared-cell proxy (`$a := $b`, `@a[i] := $x`)
holds its cell as a `Value::cellHolder` instead of a boxed `ext` handle. That
saves three allocations, and it makes the cell an edge the collector sees too.
The constant keys are hashed once (`ValueHash::findPre`/`addFresh`), and the
kind is interned once (`proxyKind()`). A variable read takes
`engineProxyRead` at the four plain-read fast paths and at the general one.
Numbers are in BINARYHEAP.md. In short: element binds take 1.3–1.9x less
time, and BinaryHeap's benchmarks run 17–20% fewer instructions.

## What a real container would buy

**Semantics.** A positional alias agrees with Rakudo only while the array
grows at the end. Three cases differ, all measured against Rakudo 2026.09:

| program | rakupp | Rakudo |
|---|---|---|
| `my $x := @a[1]; @a.unshift(0); $x = 55` | writes `@a[1]` | writes `@a[2]` (the container moved) |
| `my \l = @c[1]; @c = 7, 8, 9; say l` | 8 | 2 (assignment made new containers) |
| `my $z := @d[0]; @d[0] := 42; say $z` | 42 | 1 (the old container is kept) |

The shrink case (`pop`/`splice` past the bound element) is patched today by
the alias remembering the last value (`kSlotLast`). A container would do this
naturally.

**Speed.** After the cheap half, BinaryHeap pop spends about 9% of its
samples building, freeing and reading proxies. That is the most a real
container can save there. The rest is general interpreter cost:
`evalAssignInner`'s arm chain and Darwin's `_tlv_get_addr` (12–20% of the
profiles).

## The survey (129 `"Proxy"` sites, about 80 `isCell`/`deref`/`cellS` uses)

- **A. read and deproxy, about 50.** Examples: `deproxy`; the variable reads in
  evalVarExpr and eval; argument binding (InterpreterCore.cpp `~25830`);
  subscript bases; the bool, arithmetic and concatenation operators;
  gist/Str (InterpreterOperators.cpp); `deepEq`/`eqv`; `decontCopied`;
  `deproxyElems`.
- **B. write through, 9.** `proxyStore`, assignment STORE, `op=`, `++`/`--`,
  topic store, temp/let, and the container-list assignment (already
  `isCell`-aware).
- **C. container identity and promotion, about 25.** `cellOfProxy`,
  `containerId`, `varCell`, FlatRw, `keepListContainers`, `=:=`, and the
  promote-on-bind sites. InterpreterCore.cpp `~15137` (`@a[i] := @b[j]`) and
  `bindAttrAlias` already put a cell proxy in the source slot and a holder in
  the target.
- **D. type, dispatch and gist special cases, about 20.**
- **E. other, about 10.**

The array-element readers do not deproxy eagerly. `evalIndex`,
`rtIndexGet`, the `for` loop and the method kernels all hand the raw slot on.
A Proxy element works because every consumer deproxies lazily. The method
kernels take `decontList` (which handles both a Proxy and a raw cell) only
when the invocant is flagged `holdsContainers`, and an ordinary `@a` after
`@a[i] := …` is not flagged. About 340 range-for loops walk `…arr()` raw
across Builtins*, MethodCall* and InterpreterCore.

## Two ways to give an element a container

**(1) A raw cell in the buffer.** The element is a `Value::cellHolder(cell)`,
as an Env slot is, and the bound name holds the same cell. This costs one
allocation per bound element, no hash, and a deref to read. But a holder's
`t` is `Any`, so every reader that does not deref sees `(Any)`, silently.
That covers all ~50 A sites and all ~340 raw loops. The hole checks
(`t == VT::Any` in evalIndex, `rtSlotExists`, `:exists`) would answer "no
element". A copied holder would keep aliasing (`my @b = @a`). A `<->` loop's
write-back would overwrite the holder and unbind the name. This is the right
representation, and the most invasive change on this list. It needs a
chokepoint first: a per-buffer "holds cells" bit on the `ValueList` itself
(not on the `Value`, as `holdsCells` is now), so the hot readers can test
one bit and deref only when it is set.

**(2) A cell proxy in the buffer.** The element becomes
`makeSharedCellProxy(cell)` in place, and the alias is a copy of that
Value. Every reader already handles it, because `@a[i] := $x` leaves exactly
this. Rebinding the same element costs nothing.

## The experiment: (2), behind an environment variable (not landed)

Built and measured on 2026-10-20, then removed. The bind arm
(InterpreterCore.cpp, `my $x := @a[i]` and `my \l = @a[i]`) and
`containerOfExpr` (`BIND-POS`) promoted the element in place.

- **Semantics:** all three rows of the table above matched Rakudo, and
  t/regression/element-bind-compact-proxy.raku still passed.
- **Probes** (200k iterations, rebinding one element, so promotion is paid
  once):

  | probe | ms, before | ms, after |
  |---|---|---|
  | `my \l` | 120–140 | 80 |
  | `my $n :=` | 111 | 76 |
  | `BIND-POS` | 80 | 61 |
  | the `$node` shuffle | 200 | 170 |

- **BinaryHeap**, instructions retired and peak RSS, on top of the cheap half:

| benchmark | instructions | RSS |
|---|---|---|
| binaryheap-pop | 59.0G → 54.5G (−8%) | 24 → 87 MB |
| binaryheap-push (2^19 values) | 54.9G → 53.4G (−3%) | 100 → 465 MB |
| heapsort | 59.3G → 54.9G (−7.5%) | 19 → 83 MB |

Every element a heap touches stays promoted. That is a cell plus a hash
with a 480-byte first chunk, about 700 bytes per element. So (2) trades 3–8%
for 3.6–4.7x the memory, and it is not worth landing in this form.

## Recommendation

- Keep the positional alias for now. It is cheap, and its divergences are
  the three rows above.
- If the semantics become worth fixing, do (2) with a smaller container. A
  cell proxy that drops back to a plain value when its cell's count says
  no alias is left would bound the memory to the live aliases. That check
  would live in `slotProxyReadRef`/`deproxy`, which already see the cell.
- (1) is the representation Rakudo's speed comes from. It is a
  representation project in its own right, sized by the survey above. Start
  with the per-buffer bit, then convert the readers kernel by kernel, with
  `RAKUPP_CELL_ELEMS`-style poisoning that makes an unconverted reader fail
  loudly instead of reading `Any`.
