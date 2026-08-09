# Per-character string ops, and what the ASCII fast path actually cost

Found 9 August 2026, working the v3.0.1 release, by asking why JSON::Fast
needed 13.9 s to parse a 421 KB document that Rakudo parses in 51 ms. Most of
this is fixed and shipped in v3.0.1; the last two sections are open.

The reason it is worth writing down is that the defect was **invisible in the
shape of the code**. Every site looked like an optimisation, was commented as
an optimisation, and was one — for a single call. What none of them survived
was being called once per character, which is exactly how a tokenizer written
in Raku uses them.

## 1. The pattern — FIXED (v3.0.1)

Raku string positions are grapheme positions. rakupp stores a Str as UTF-8
bytes, so an op like `nqp::ordat($text, $pos)` has to establish that a byte
index is a codepoint index before it can index bytes. Each op did that by
scanning:

```cpp
size_t run = asciiRun(s, (size_t)i + 1);      // O(i), every call
if (run == (size_t)i + 1 || run == s.size())
    return Value::integer((unsigned char)s[i]);
```

That is O(position) per character examined, so scanning a string of length n
costs O(n²). The scan is cheap per byte — eight bytes a word — which is
precisely why it survived review: the constant is small and the complexity is
wrong.

The fix is not a faster scan but *not repeating it*. Both properties are pure
functions of immutable text, so with strings promoted to a shared immutable
body (`StrBody` in [Value.h](../../../src/Value.h)) the answer is cached on
the body and computed once per string:

- `cowAllAscii` — every byte < 0x80, so a byte index is a codepoint index
- `cowByteIsGraphemeIndex` — the above and no CR (CR LF is the one ASCII
  sequence that clusters, GB3), so a byte index is a *grapheme* index
- `cowGraphemeCount` — `.chars`

Sites converted: `nqp::ordat`, `eqat`, `substr`, `chars`, `index`,
`findnotcclass`, `iscclass`, and `.chars`/`rtBChars`.

**`findnotcclass` was the expensive one and was found last.** JSON::Fast calls
it once per string token to find the end of a word-character run; it called
`allAscii(s)` on the *whole document* each time. 12,001 calls over a 208 KB
document is 2.5 GB of scanning — on its own still quadratic, after the first
four sites had been fixed and the parse already looked linear-ish. It was only
caught by asking why the measured gap to Rakudo was bigger than the dispatch
cost explained.

The lesson worth keeping: **fixing four of seven sites of a quadratic leaves a
quadratic.** The scaling table, not the improvement factor, is what tells you
whether you are done — 255 → 777 → 3,245 → 13,216 ms is ×4 per doubling, and
102 → 190 → 381 → 764 ms is ×2.

## 2. Strings were copied by value — FIXED (v3.0.1)

Underneath the scans, `Value` held its Str as a `std::string` **by value**, and
`Value` is copied on every argument pass, operand evaluation and list element.
A long string was memcpy'd once per operation — the same O(length)-per-op
shape, from a different direction. Measured, 20k operations on a 200 KB string:

| | before | after |
|---|---:|---:|
| pass the string to a sub, no work done | 142 ms | 24 ms |
| `nqp::ordat` on a global | 79 ms | 11 ms |

`CowStr` keeps short strings inline (std::string's own small-buffer
optimisation already makes those copies free) and promotes from 64 bytes up
into a shared body, so copying is a refcount bump. Promotion is **eager**, at
construction, not lazy-on-first-copy: rakupp runs work in parallel, and a lazy
promotion would have to mutate the source from a const copy constructor, which
two threads copying the same Value would race on. Eager promotion means a
const `CowStr` is never written to.

Cost: `sizeof(Value)` went 376 → 392 bytes. The perf gate passed and Roast came
out marginally up, so it is paid for, but it is a real 4% tax on every Value.

## 3. `nqp::strtocodes` normalized ASCII — FIXED (v3.0.1)

It ran a full `uniNormalize` pass for the requested form even when the text was
pure ASCII, which every normalization form leaves unchanged. 4 µs per call
against Rakudo's ~0, once per escaped string. Now gated on `cowAllAscii`, and
the target array is `reserve`d.

## 4. What is left, and why it is not a bug — OPEN

After all of the above, rakupp is ~15× slower than Rakudo on this workload.
That residue is the architecture, measured directly rather than assumed:

| | per loop iteration | per AST node / op |
|---|---:|---:|
| rakupp | 0.46 µs | ~115 ns |
| Rakudo | 0.02 µs | ~5 ns |

MoarVM compiles to bytecode and JITs hot loops; rakupp walks the AST, so every
`nqp::push_i` is a node visit, a switch dispatch, argument evaluation into a
`ValueList` and a `Value` construction. ~23× is what that costs, and it
accounts for the whole remaining gap. Closing it is a bytecode VM, not an
optimisation, and nothing in this file should be read as suggesting otherwise.

One hypothesis tested and **rejected**: that the 392-byte `Value` was the cost,
since a native `Uni` buffer is a `std::vector<Value>` where MoarVM uses a
packed int32 array. Measured, a loop doing `nqp::push_i` costs the same as the
same loop doing no array work at all (0.45 vs 0.47 µs/iter) — the vector
amortises growth and the fat struct is noise next to dispatch. A packed
native-int array representation is still worth having for memory, but it is not
where the time goes.

## 5. Where else this pattern probably lives — OPEN

The seven ops fixed here were found by profiling one module. The same
"establish a string property by scanning, per call" shape is worth grepping for
elsewhere — `MethodCallPart3.cpp` has `byteIsGraphemeIndex(raw)` and
`byteIsGraphemeIndex(hay)`/`(ndl)` at the `.index`/substitution sites, which are
uncached and sit on paths a regex-driven loop can hit per character. They were
left alone in v3.0.1 because no measurement drove them; they should be measured
rather than converted on faith.

The general check, cheap to run against any candidate: time the operation over
inputs of n, 2n, 4n and 8n. A ×2 per doubling is fine; a ×4 is this bug.
