# Append — plan

*Written 2026-10-06 at `a124d91a`, before any code. Goal: the three
string-building shapes that are still quadratic on every Raku++ engine become
linear, as they are in Rakudo, without making ordinary string code slower.
Follows issue #130, whose `~=` cases are all linear since `a124d91a`.*

**The number a stranger can re-measure:**

```bash
rakupp tools/bench/append-shapes.raku                  # interpreted
rakupp --exe -o /tmp/as tools/bench/append-shapes.raku && /tmp/as
rakudo tools/bench/append-shapes.raku                  # the reference
```

At `a124d91a`, 40,000 steps (ms), interpreted / `--exe` / Rakudo 2026.09:

| shape | Raku++ | `--exe` | Rakudo | growth 20k→40k |
|---|---|---|---|---|
| `$s = "abcde" ~ $s` (prepend) | 698 | 691 | 9 | 4.0x |
| `$k = $s; $s ~= "abcde"` | 103 | 97 | 5 | 3.0–3.4x |
| `$s = $s.substr(5)` while `$s.chars` | 245 | 278 | 3 | 4.0x |

The other ten shapes of the bench are linear on every engine, and
`t/regression/append-shapes-linear.raku` holds them there.

## Where it stands

*2026-10-06, uncommitted on top of `a124d91a`.* All 13 shapes of the bench are
linear on every engine: interpreted, `--cnp`, `--exe` and `--exe -O`. At
40,000 steps, interpreted, in ms:

| shape | before | after | Rakudo |
|---|---|---|---|
| `$s = "abcde" ~ $s` | 698 | 3.7 | 9 |
| `$k = $s; $s ~= "abcde"` | 103 | 5.5 | 5 |
| `$s = $s.substr(5)` | 245 | 20.7 | 3 |

The perf-guard kernels (100,000 steps): prepend 4.53 s → 0.01 s,
sharedappend 0.56 s → 0.01 s, substrloop 1.61 s → 0.05 s.

Done: A0 (the three kernels), A1 (views, plus `RAKUPP_APPEND=force`, which
makes every promoted string a view: Roast 1423/1424 under it, the one being the
`S17-supply/syntax-nonblocking-await.t` flake, which fails 2 runs in 60 on
`a124d91a` too), A2 and the A3 consumers above, A5 (codegen, cnp, kernel and
lane declines), A6 (the three shapes in
`t/regression/append-shapes-linear.raku`, the edge cases in
`t/regression/string-views.raku`, and `t/race/string-buffer-claims.raku`).
A4's probe: twenty 10 MB strings, each grown as a shared view (a 20 MB buffer)
and kept only as a 1,000-character slice, peak at 40 MB resident against 30 MB
on `a124d91a` — no slice pins its buffer (pinned, it would be ~400 MB).
Owed: perf-guard A/B against `a124d91a` on a quiet machine. An interleaved A/B
under load (load average 4–10) put the string and hash kernels within ±2% in
both orders, after the unique-append fast path was inlined again and
`flatView` moved out of line (inlined, it cost every string read ~1%).
Known limit: a `.substr` loop over NON-ASCII text still flattens each new view
for its grapheme table, so it stays quadratic there.

---

## Why a flat string cannot do these

A Raku++ string is a `CowStr` (src/Value.h): short text inline, longer text in
one shared, immutable `StrBody` holding a `std::string`. Since #130 a body with
one owner is appended to in place. These three shapes each need the OLD text
to survive unchanged while a NEW one is made from it:

- **`$k = $s; $s ~= x`** — `$k` holds the body, so `$s` must copy all of it
  before it can grow.
- **`$s.substr(5)`** — the result is a new string of everything but five
  characters: a copy of the rest, every step.
- **`"abcde" ~ $s`** — a contiguous buffer cannot grow at the front without
  moving everything behind it.

Rakudo's strings are not flat. As far as the design goes (MoarVM's string
representation, read at the design level only): a string may be a list of
*strands*, each a range of another string, so concatenation and `substr` refer
to the pieces instead of copying them, and a string is flattened when
something needs it contiguous. That is the idea this plan takes. The
implementation is our own.

## The constraint that shapes everything

Every reader in the runtime treats a string as one contiguous `std::string`:
about 2,000 places read a Value's text through the implicit `CowStr → const
std::string&` conversion, 212 call `.s.str()` explicitly, 42 take `.c_str()` or
`.data()` (FFI, I/O), and a handful reach `StrBody::text` directly
(NativeHelpers::Blob's pointer-to, the NativeCall buffer path). Rewriting 2,000
readers is not on the table. So:

**A pieced string flattens on first contiguous read, once, and caches the
result.** `str()` keeps its signature and its meaning; a reader that has not
been taught about pieces pays one flatten the first time and nothing after.
Speed comes from teaching the few operations that run in the hot loops of the
three shapes — `.chars`, `.substr`, `~`, `~=`, `eq`, printing — to read the
pieces without flattening. Everything else is correct from day one and fast
where it matters.

## Design

*Revised while implementing (2026-10-06). The first draft joined pieces into
a tree of Concat nodes. That cannot make `$k = $s; $s ~= x` linear: the chain
grows by one node per append, and flattening it at a depth cap is an O(n) copy
every 64 steps, which is still quadratic, only with a smaller constant. The
design below replaced it before any code was written.*

### Views of a shared buffer

A `StrBuf` is a byte buffer with free space at both ends. Its written region
`[lo, hi)` only ever grows outward, and bytes inside it are never moved or
changed. A *view* is a `StrBody` that holds a range `(off, len)` of a buffer
instead of text of its own (`StrBody::view`, src/Value.h).

- **Many readers.** Any number of views, and threads, may read a buffer at
  once, because nothing they can see ever changes.
- **Growing.** A view whose end is the region's end may *claim* the free
  space after it with one compare-and-swap on `hi`, write there, and become a
  longer view. The same works at the front, with `lo`. `$k`'s view keeps its
  range while `$s`'s grows past it.
- **Contention.** Two views with the same end race on the CAS. The loser gets
  a new buffer.
- **Running out of room.** The view's bytes move to a new buffer twice the
  size, with the free space split over both sides. Each step is therefore
  O(1) on average.
- **Flags.** A buffer keeps a monotone `clean` flag: every byte ever written
  to it is ASCII and none is CR. A view of a clean buffer is created with its
  ASCII and CR flags set and its grapheme count equal to its length. `.chars`
  and `.substr`'s fast lane then answer without reading the text.

**Flatten once.** `StrBody::str()` keeps its meaning: a view builds its
contiguous text on first use and keeps it, behind the same publish-once CAS
that `cpIndex` uses. `CowStr::size()`, `firstByte()` and the cached flags read
a view without flattening it.

### Who makes views (producers)

Views are opt-in: `+=` keeps exactly today's behaviour, because a Buf's
storage is handed to C and must stay one flat `std::string`.

| shape | where | how |
|---|---|---|
| `~=`, `$s = $s ~ X` into a body others hold | `CowStr::appendText` (from rtCatAppendText, rtCatAssign, the interpreter's `~=` lanes, `selfCatAssign`, cnp, codegen) | into a new buffer, at the one copy today makes anyway; a view then claims the space after it |
| `$s = X ~ $s` | `CowStr::prependText` (interpreter `selfCatAssign`, codegen `rtCatPrepend`, cnp `RK_OP_SELFCAT` with the destination on the right) | into a buffer's free space in front |
| `$t = $s ~ x` with `$s` already a view | applyArith's Str/Str `~` (`CowStr::joinAppend`) | claims after `$s` |
| `.substr` | `CowStr::sub` | a view of a view's buffer; from a flat source when the slice is ≥ 1/4 of it |

A sole-owned flat body keeps appending in place, exactly as before. Short
strings, under 256 bytes, never become views.

**The NFC junction.** The existing rule carries over unchanged. ASCII
appended to a Str joins as it is; anything else is renormalized by copying.
For a prepend, the old string's FIRST byte must be ASCII
(`rtCatPrependText`): `"e" ~ "\x[301]…"` composes to é, and it does, through
the copying path.

**Graphemes at the junction.** CR LF is one cluster. A buffer that ever held
a CR is not clean, so its views count graphemes from their flattened text,
which is correct by construction.

### Who reads views without flattening (consumers)

| operation | how |
|---|---|
| `.chars` | the grapheme count preset from a clean buffer |
| `.substr` | the text is read lazily, only by the lanes that need it whole; the ASCII lane slices into a view |
| `size()`, `empty()`, `firstByte()`, the ASCII and CR flags | from the view's range and flags |

Everything else — regex, hashing, `eq`, printing, FFI, `.c_str()` — flattens
once and carries on unchanged. Printing and `eq` reading a view directly are
left as later refinements: in the three shapes they happen once, at the end.

### Bounds

- **Retention.** A slice of a buffer bigger than 64 KB that keeps less than a
  quarter of it is copied out, so the buffer can be freed.
- **Kernels and lanes.** The loop kernels' and `-O` lanes' Str slots are plain
  `std::string`s. They decline the two self-referencing shapes (a string
  prepended to itself; a string cut down to its own `.substr`), and those
  loops run boxed, on views. Other string loops keep their kernels.

### What is never a view

- Buf / Blob / utf8 (`+=` and `mutInPlace` stay flat; `mutInPlace` flattens a
  view first).
- Inline (short) strings.

## Phases

Each phase lands on its own, gated, and is useful alone.

**A0 — the measuring stick.**
- Add the three shapes to `tools/perf-guard.raku` as kernels (`prepend`,
  `sharedappend`, `substrloop`), in all four lists (see the bench-kernel-lists
  note).
- Record a quiet-machine baseline of the string kernels that exist today
  (`strscan`, `strpass`, `intcat`, the `strcat` bench). Those are the kernels a
  slower `str()` would show in.

**A1 — views without producers.** *(done)*
- `StrBuf`, `StrView`, the CAS-cached flatten, and `str()` / `c_str()` /
  direct `->text` readers routed through it.
- A debug knob, `RAKUPP_APPEND=force`, makes every promoted string a view. It
  runs Roast and the module battery with every string a view, which is the
  correctness proof of the readers.
- The NFC rule needed no new proof: views take only the joins the existing
  in-place append already took (ASCII after anything; for a prepend, anything
  before an ASCII first byte).
- Exit when Roast, rakuglaze and `t/exe` pass, with the knob off and with it
  on, and `perf-guard` shows no regression on the string kernels with it off.

**A2 — producers.** *(done)*
- `~=` and `$s = $s ~ X` into a shared body, `$s = X ~ $s`, `~` after a view,
  and the doubling growth policy.
- Exit when `sharedappend` and `prepend` are linear interpreted (the bench
  growth is about 2x) and the A0 kernels are within tolerance.

**A3 — consumers.** *(`.chars`, `.substr`, the flags done; `eq`/`cmp`,
printing, `starts-with`/`ends-with` and a non-ASCII grapheme table for views
left)*
- `.chars`/`.codes`, `.substr` of a view, `eq`/`cmp`, printing,
  `starts-with`/`ends-with`, and the ASCII flags.
- Exit when `substrloop` is linear and no consumer on the list flattens in the
  three shapes. Count flattens with a `RAKUPP_APPEND=stats` counter, so this is
  measured, not assumed.

**A4 — slices and retention.** *(done)*
- `.substr` producing views, with the retention bounds.
- Probe memory: a 100 MB string with one short `substr` kept must not keep
  100 MB alive.

**A5 — the compiled side.** *(done)*
- `--exe` boxed code gets the producers for free, since it calls the same
  runtime. Then the unboxed lanes, the loop kernels' Str slots and `--cnp`'s
  `~` binop: decline or piece, per lane.
- Exit when all 13 shapes are linear on every engine: interpreted, `--exe`,
  `--exe -O` and `--cnp`.

**A6 — the gate.** *(done)*
- Move the three shapes into `t/regression/append-shapes-linear.raku`.
- Add rows on views crossing threads: one view read by eight `start` workers
  while the main thread appends to its buffer, run in fresh processes, in the
  style of `t/race/`.

## Gates for every phase

- The Roast sweep after every change to `src/` (the house rule).
- `t/exe`, the cnp differential, rakuglaze, and `t/aot` for A5.
- `perf-guard --check` on a quiet machine, run alone, before a phase closes.
  `str()` is the hottest accessor in the runtime, so a phase that slows it by
  more than the tolerance does not land, however much faster it makes the
  three shapes.
- The module battery once, at A1 with `RAKUPP_APPEND=force`. That is the run
  that finds a reader that reached `->text` without flattening.

## Risks

- **`str()` gets slower.** It is one branch on a predictable path, but it sits
  in a function that runs everywhere. A1 measures it before anything builds on
  it. The fallback is to keep pieced bodies out of `p_` and give them a tagged
  pointer, so a Flat body takes no extra test.
- **A reader that bypasses `str()`** (raw `->text`, a cached `data()`
  pointer). A1's forced mode exists to find these.
- **Lifetime of a flattened pointer.** C code handed `c_str()` of a view gets
  its flattened copy's address, which lives as long as the view does — the
  same lifetime a flat body gives today.
- **Memory.** A buffer is up to twice the text it holds, and a flattened view
  holds a second, contiguous copy. A view's record is about 40 bytes. The
  retention bound keeps a small slice from pinning a big buffer; A4's probe
  measures the rest.

## Not in this plan

- Ropes for arrays or Bufs.
- Changing `CowStr`'s size (32 bytes) or the inline threshold (23).
- Rewriting the 2,000 readers to take string views. The flatten-once design
  exists so that this is never needed.

## Open questions for the maintainer

1. Ship A1–A3 (interpreter linear) as one release item, with A5 (compiled
   lanes) following later — or hold everything until all engines are linear?
2. Is a view threshold of 256 bytes acceptable as the starting point, given
   that it only moves the crossover point and never changes an answer?
3. Should `RAKUPP_APPEND=force` stay as a permanent debug knob, like
   `RAKUPP_NO_KERNELS`, so a gate keeps exercising every reader on views?
